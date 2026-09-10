/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "dx11texture.h"

#include "dx11resource.h"

#include <d3d9.h>
#include <d3d11.h>
#include <algorithm>
#include <map>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

// Where the copy and the refusal are kept on the D3D9 texture.  The view goes in as an IUnknown,
// which makes D3D9 release it when the texture goes; the refusal is one byte, because a refusal has
// nothing to hold on to and still has to be remembered.
// {2E2E9C21-1B2A-4C7E-9E2F-1D0B7E9A5C01}
static const GUID DX11_TEXTURE_VIEW =
	{ 0x2e2e9c21, 0x1b2a, 0x4c7e, { 0x9e, 0x2f, 0x1d, 0x0b, 0x7e, 0x9a, 0x5c, 0x01 } };
// {2E2E9C22-1B2A-4C7E-9E2F-1D0B7E9A5C01}
static const GUID DX11_TEXTURE_REFUSED =
	{ 0x2e2e9c22, 0x1b2a, 0x4c7e, { 0x9e, 0x2f, 0x1d, 0x0b, 0x7e, 0x9a, 0x5c, 0x01 } };
// The render target view over the same copy, for a texture the engine draws into.
// {2E2E9C23-1B2A-4C7E-9E2F-1D0B7E9A5C01}
static const GUID DX11_TEXTURE_TARGET =
	{ 0x2e2e9c23, 0x1b2a, 0x4c7e, { 0x9e, 0x2f, 0x1d, 0x0b, 0x7e, 0x9a, 0x5c, 0x01 } };
// Set when the CPU has written the D3D9 texture since the copy was last filled.  Movies do this
// every frame; the loaders do not.
// {2E2E9C24-1B2A-4C7E-9E2F-1D0B7E9A5C01}
static const GUID DX11_TEXTURE_DIRTY =
	{ 0x2e2e9c24, 0x1b2a, 0x4c7e, { 0x9e, 0x2f, 0x1d, 0x0b, 0x7e, 0x9a, 0x5c, 0x01 } };

// Why the first texture that could not be copied could not be copied.  A count of refusals says
// how much of the picture is missing its texture; this says what to fix.
static std::string RefusalReason;

static void note_refusal(const char * reason)
{
	if (RefusalReason.empty()) {
		RefusalReason = reason;
	}
}

static std::vector<std::string> Notes;

static unsigned Mirrored = 0;
static unsigned Refused = 0;
static unsigned Reused = 0;
static double FrameCopyMilliseconds = 0.0;
static unsigned FrameCopyCount = 0;

// Builds and refreshes by the shape of the texture, for the report.  A shape built thousands of
// times in a match is a texture the engine makes and throws away every frame.
struct CopyShapeCounts
{
	unsigned Builds;
	unsigned Refreshes;
};
static std::map<std::string, CopyShapeCounts> CopyShapes;
static std::vector<std::string> CopyShapeLines;
static const unsigned COPY_SHAPE_REPORT_LINES = 10;

// D3DFMT_X8R8G8B8 has no alpha and its bytes hold whatever the loader left there.  Read as
// B8G8R8A8 those bytes are the alpha channel, and a texture whose alpha reads as zero is a texture
// that draws nothing.  The rows are copied with the alpha forced opaque rather than translated to
// an alpha-less DXGI format, because there is no B8G8R8X8 that a shader resource view will take on
// every driver.
static void copy_rows_opaque(unsigned char * destination, unsigned destination_pitch,
	const unsigned char * source, unsigned source_pitch, unsigned width, unsigned height)
{
	for (unsigned row = 0; row < height; ++row) {
		const unsigned char * in = source + row * source_pitch;
		unsigned char * out = destination + row * destination_pitch;
		for (unsigned column = 0; column < width; ++column) {
			out[column * 4 + 0] = in[column * 4 + 0];
			out[column * 4 + 1] = in[column * 4 + 1];
			out[column * 4 + 2] = in[column * 4 + 2];
			out[column * 4 + 3] = 0xff;
		}
	}
}

// The three sixteen bit colour formats.  Their DXGI names exist and a device will create a texture
// in one, and then a shader resource view over it is refused or samples nothing on drivers that
// never had to support them: they were legal in D3D9 and optional from D3D11 on.  The terrain atlas
// is A1R5G5B5, and this is the difference between a textured ground and a white one.  Expanding to
// eight bits a channel costs twice the memory for those few textures and asks nothing of the
// driver.
static bool is_sixteen_bit_colour(D3DFORMAT format)
{
	return format == D3DFMT_A1R5G5B5 || format == D3DFMT_R5G6B5 || format == D3DFMT_A4R4G4B4;
}

static void expand_sixteen_bit(unsigned char * destination, unsigned destination_pitch,
	const unsigned char * source, unsigned source_pitch, unsigned width, unsigned height,
	D3DFORMAT format)
{
	for (unsigned row = 0; row < height; ++row) {
		const unsigned short * in = (const unsigned short *)(source + row * source_pitch);
		unsigned char * out = destination + row * destination_pitch;
		for (unsigned column = 0; column < width; ++column) {
			const unsigned short pixel = in[column];
			unsigned blue = 0, green = 0, red = 0, alpha = 0xff;
			switch (format) {
			case D3DFMT_A1R5G5B5:
				blue  = ((pixel      ) & 0x1f) * 255 / 31;
				green = ((pixel >>  5) & 0x1f) * 255 / 31;
				red   = ((pixel >> 10) & 0x1f) * 255 / 31;
				alpha = ((pixel >> 15) & 0x01) * 255;
				break;
			case D3DFMT_R5G6B5:
				blue  = ((pixel      ) & 0x1f) * 255 / 31;
				green = ((pixel >>  5) & 0x3f) * 255 / 63;
				red   = ((pixel >> 11) & 0x1f) * 255 / 31;
				break;
			default:	// D3DFMT_A4R4G4B4
				blue  = ((pixel      ) & 0x0f) * 255 / 15;
				green = ((pixel >>  4) & 0x0f) * 255 / 15;
				red   = ((pixel >>  8) & 0x0f) * 255 / 15;
				alpha = ((pixel >> 12) & 0x0f) * 255 / 15;
				break;
			}
			out[column * 4 + 0] = (unsigned char)blue;
			out[column * 4 + 1] = (unsigned char)green;
			out[column * 4 + 2] = (unsigned char)red;
			out[column * 4 + 3] = (unsigned char)alpha;
		}
	}
}

static bool upload_levels(ID3D11DeviceContext * context, ID3D11Texture2D * destination,
	IDirect3DTexture9 * source, D3DFORMAT format, unsigned level_count)
{
	const bool force_opaque = (format == D3DFMT_X8R8G8B8);
	const bool expand = is_sixteen_bit_colour(format);

	for (unsigned level = 0; level < level_count; ++level) {
		D3DSURFACE_DESC level_description;
		if (FAILED(source->GetLevelDesc(level, &level_description))) {
			return false;
		}

		D3DLOCKED_RECT locked;
		if (FAILED(source->LockRect(level, &locked, NULL, D3DLOCK_READONLY))) {
			return false;
		}

		if (expand) {
			const unsigned pitch = level_description.Width * 4;
			std::vector<unsigned char> expanded(pitch * level_description.Height);
			expand_sixteen_bit(&expanded[0], pitch, (const unsigned char *)locked.pBits,
				locked.Pitch, level_description.Width, level_description.Height, format);
			context->UpdateSubresource(destination, level, NULL, &expanded[0], pitch, 0);
		}
		else if (force_opaque) {
			const unsigned pitch = level_description.Width * 4;
			std::vector<unsigned char> opaque(pitch * level_description.Height);
			copy_rows_opaque(&opaque[0], pitch, (const unsigned char *)locked.pBits, locked.Pitch,
				level_description.Width, level_description.Height);
			context->UpdateSubresource(destination, level, NULL, &opaque[0], pitch, 0);
		}
		else {
			context->UpdateSubresource(destination, level, NULL, locked.pBits, locked.Pitch, 0);
		}

		source->UnlockRect(level);
	}

	return true;
}

// The average colour of the texture's top level, read the same way the copy read it.  A count of
// mirrored textures says nothing about what is in them; this says whether the atlas that draws the
// ground holds the ground or holds white.
static void note_average_colour(IDirect3DTexture9 * texture, const D3DSURFACE_DESC & description,
	unsigned level_count)
{
	D3DLOCKED_RECT locked;
	if (FAILED(texture->LockRect(0, &locked, NULL, D3DLOCK_READONLY))) {
		return;
	}

	const unsigned pitch = description.Width * 4;
	std::vector<unsigned char> expanded(pitch * description.Height);
	expand_sixteen_bit(&expanded[0], pitch, (const unsigned char *)locked.pBits, locked.Pitch,
		description.Width, description.Height, description.Format);
	texture->UnlockRect(0);

	double blue = 0.0, green = 0.0, red = 0.0, alpha = 0.0;
	const unsigned pixels = description.Width * description.Height;
	for (unsigned pixel = 0; pixel < pixels; ++pixel) {
		blue  += expanded[pixel * 4 + 0];
		green += expanded[pixel * 4 + 1];
		red   += expanded[pixel * 4 + 2];
		alpha += expanded[pixel * 4 + 3];
	}

	char line[192];
	sprintf(line, "%ux%u format %u levels %u average rgba %.0f,%.0f,%.0f,%.0f",
		description.Width, description.Height, (unsigned)description.Format, level_count,
		red / pixels, green / pixels, blue / pixels, alpha / pixels);
	Notes.push_back(line);
}

// Everything that decides whether this texture can be mirrored at all, in the order that answers
// cheapest first.
static bool can_mirror(IDirect3DTexture9 * texture, D3DSURFACE_DESC & description)
{
	if (FAILED(texture->GetLevelDesc(0, &description))) {
		note_refusal("the D3D9 texture would not describe its top level");
		return false;
	}
	// A default-pool texture that is not a render target cannot be locked and nothing writes it
	// through D3D11 either, so there is nothing to copy and nothing that would fill the copy.  A
	// render target is the other way round: it holds nothing worth reading now, and D3D11 fills it
	// itself once the pass that draws into it is routed.
	if (DX11Resource_Translate_Format(description.Format) == DXGI_FORMAT_UNKNOWN) {
		note_refusal("the format has no DXGI counterpart");
		return false;
	}
	return true;
}

static ID3D11ShaderResourceView * build(ID3D11Device * device, ID3D11DeviceContext * context,
	IDirect3DBaseTexture9 * texture)
{
	IDirect3DTexture9 * two_dimensional = NULL;
	if (FAILED(texture->QueryInterface(IID_IDirect3DTexture9, (void **)&two_dimensional))) {
		note_refusal("not a two dimensional texture");
		return NULL;
	}

	D3DSURFACE_DESC description;
	if (!can_mirror(two_dimensional, description)) {
		two_dimensional->Release();
		return NULL;
	}

	// A render target is made empty, with one level and the render target bind, and is filled by
	// whatever draws into it.  A default-pool texture is made empty too: D3D9 will not let it be
	// read back, and the engine fills it by copying a system memory surface over it, which is
	// mirrored by DX11Texture_Update.  Everything else is copied out of the D3D9 texture as it
	// stands.
	const bool render_target = (description.Usage & D3DUSAGE_RENDERTARGET) != 0;
	const bool empty = render_target || description.Pool == D3DPOOL_DEFAULT;
	const unsigned level_count = empty ? 1 : two_dimensional->GetLevelCount();

	// A texture made empty keeps one level, and DX11Texture_Update writes subresource zero, so a
	// mip chain on one of these would be dropped and every distant surface wearing it would sample
	// the top level and alias.  Nothing in the measured maps has one - the empty ones are the bloom
	// targets, the reflection and the scene copy, all single level - and this says so out loud
	// rather than leaving it to be rediscovered from a picture.
	if (empty && two_dimensional->GetLevelCount() > 1) {
		char line[128];
		sprintf(line, "%ux%u default pool with %u levels mirrored as one",
			description.Width, description.Height, two_dimensional->GetLevelCount());
		Notes.push_back(line);
	}
	const D3DFORMAT copy_format = is_sixteen_bit_colour(description.Format)
		? D3DFMT_A8R8G8B8
		: description.Format;

	ID3D11Texture2D * copy = NULL;
	ID3D11ShaderResourceView * view = NULL;
	const bool created = DX11Resource_Create_Texture(device, description.Width, description.Height,
		level_count, copy_format,
		empty ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED,
		render_target ? D3DUSAGE_RENDERTARGET : 0,
		&copy, &view);
	if (!created) {
		note_refusal("the D3D11 device refused a texture of that size, format or mip count");
		two_dimensional->Release();
		return NULL;
	}

	if (empty) {
		copy->Release();
		two_dimensional->Release();
		return view;
	}

	const bool uploaded = upload_levels(context, copy, two_dimensional, description.Format,
		level_count);

	if (uploaded && is_sixteen_bit_colour(description.Format)) {
		note_average_colour(two_dimensional, description, level_count);
	}

	copy->Release();
	two_dimensional->Release();

	if (!uploaded) {
		note_refusal("a level of the D3D9 texture would not lock");
		view->Release();
		return NULL;
	}

	return view;
}

static void clear_dirty(IDirect3DBaseTexture9 * texture)
{
	const unsigned char clear = 0;
	texture->SetPrivateData(DX11_TEXTURE_DIRTY, &clear, sizeof(clear), 0);
}

static bool texture_is_dirty(IDirect3DBaseTexture9 * texture)
{
	unsigned char dirty = 0;
	DWORD size = sizeof(dirty);
	return SUCCEEDED(texture->GetPrivateData(DX11_TEXTURE_DIRTY, &dirty, &size)) && dirty != 0;
}

// Put the D3D9 texture's current pixels into a copy that already exists.  Default-pool and render
// target copies are filled another way and cannot be locked, so they are left alone.
static bool refresh_copy(ID3D11DeviceContext * context, ID3D11ShaderResourceView * view,
	IDirect3DBaseTexture9 * texture)
{
	IDirect3DTexture9 * two_dimensional = NULL;
	if (FAILED(texture->QueryInterface(IID_IDirect3DTexture9, (void **)&two_dimensional))) {
		return false;
	}

	D3DSURFACE_DESC description;
	if (FAILED(two_dimensional->GetLevelDesc(0, &description))) {
		two_dimensional->Release();
		return false;
	}
	if ((description.Usage & D3DUSAGE_RENDERTARGET) != 0 || description.Pool == D3DPOOL_DEFAULT) {
		two_dimensional->Release();
		return false;
	}

	ID3D11Resource * resource = NULL;
	view->GetResource(&resource);
	if (resource == NULL) {
		two_dimensional->Release();
		return false;
	}

	ID3D11Texture2D * copy = NULL;
	resource->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&copy);
	resource->Release();
	if (copy == NULL) {
		two_dimensional->Release();
		return false;
	}

	const bool uploaded = upload_levels(context, copy, two_dimensional, description.Format,
		two_dimensional->GetLevelCount());
	copy->Release();
	two_dimensional->Release();
	return uploaded;
}

static CopyShapeCounts & copy_shape(IDirect3DBaseTexture9 * texture)
{
	char shape[96] = "not a 2D texture";
	IDirect3DTexture9 * two_dimensional = NULL;
	if (SUCCEEDED(texture->QueryInterface(IID_IDirect3DTexture9, (void **)&two_dimensional))) {
		D3DSURFACE_DESC description;
		if (SUCCEEDED(two_dimensional->GetLevelDesc(0, &description))) {
			snprintf(shape, sizeof(shape), "%ux%u format %d pool %d, %lu levels", description.Width,
				description.Height, static_cast<int>(description.Format),
				static_cast<int>(description.Pool), two_dimensional->GetLevelCount());
		}
		two_dimensional->Release();
	}

	std::map<std::string, CopyShapeCounts>::iterator entry = CopyShapes.find(shape);
	if (entry == CopyShapes.end()) {
		const CopyShapeCounts none = { 0, 0 };
		entry = CopyShapes.insert(std::make_pair(std::string(shape), none)).first;
	}
	return entry->second;
}

ID3D11ShaderResourceView * DX11Texture_Mirror(ID3D11Device * device, ID3D11DeviceContext * context,
	IDirect3DBaseTexture9 * texture)
{
	ID3D11ShaderResourceView * view = NULL;
	DWORD size = sizeof(view);
	if (SUCCEEDED(texture->GetPrivateData(DX11_TEXTURE_VIEW, &view, &size))) {
		// GetPrivateData on an IUnknown adds a reference for the caller and the texture keeps its
		// own, so this one is handed straight back.
		view->Release();
		if (texture_is_dirty(texture)) {
			const double refresh_started = DX11Resource_Milliseconds_Now();
			refresh_copy(context, view, texture);
			clear_dirty(texture);
			FrameCopyMilliseconds += DX11Resource_Milliseconds_Now() - refresh_started;
			++FrameCopyCount;
			++copy_shape(texture).Refreshes;
		}
		++Reused;
		return view;
	}

	unsigned char refused = 0;
	size = sizeof(refused);
	if (SUCCEEDED(texture->GetPrivateData(DX11_TEXTURE_REFUSED, &refused, &size))) {
		return NULL;
	}

	const double build_started = DX11Resource_Milliseconds_Now();
	view = build(device, context, texture);
	FrameCopyMilliseconds += DX11Resource_Milliseconds_Now() - build_started;
	++FrameCopyCount;
	++copy_shape(texture).Builds;
	if (view == NULL) {
		const unsigned char marker = 1;
		texture->SetPrivateData(DX11_TEXTURE_REFUSED, &marker, sizeof(marker), 0);
		++Refused;
		return NULL;
	}

	texture->SetPrivateData(DX11_TEXTURE_VIEW, view, sizeof(view), D3DSPD_IUNKNOWN);
	view->Release();
	clear_dirty(texture);
	++Mirrored;
	return view;
}

void DX11Texture_Mark_Dirty(IDirect3DSurface9 * surface)
{
	if (surface == NULL) {
		return;
	}

	IDirect3DTexture9 * texture = NULL;
	if (FAILED(surface->GetContainer(IID_IDirect3DTexture9, (void **)&texture))
		|| texture == NULL) {
		return;
	}

	const unsigned char dirty = 1;
	texture->SetPrivateData(DX11_TEXTURE_DIRTY, &dirty, sizeof(dirty), 0);
	texture->Release();
}

bool DX11Texture_Update(ID3D11Device * device, ID3D11DeviceContext * context,
	IDirect3DSurface9 * destination, IDirect3DSurface9 * source, const RECT * source_rectangle,
	const POINT * destination_point)
{
	if (destination == NULL || source == NULL) {
		return false;
	}

	IDirect3DTexture9 * texture = NULL;
	if (FAILED(destination->GetContainer(IID_IDirect3DTexture9, (void **)&texture))
		|| texture == NULL) {
		return false;
	}

	ID3D11ShaderResourceView * view = DX11Texture_Mirror(device, context, texture);
	texture->Release();
	if (view == NULL) {
		return false;
	}

	D3DSURFACE_DESC source_description;
	if (FAILED(source->GetDesc(&source_description))) {
		return false;
	}

	const unsigned left = (source_rectangle != NULL) ? source_rectangle->left : 0;
	const unsigned top = (source_rectangle != NULL) ? source_rectangle->top : 0;
	const unsigned width = (source_rectangle != NULL)
		? (unsigned)(source_rectangle->right - source_rectangle->left)
		: source_description.Width;
	const unsigned height = (source_rectangle != NULL)
		? (unsigned)(source_rectangle->bottom - source_rectangle->top)
		: source_description.Height;
	if (width == 0 || height == 0) {
		return false;
	}

	D3DLOCKED_RECT locked;
	if (FAILED(source->LockRect(&locked, source_rectangle, D3DLOCK_READONLY))) {
		note_refusal("the surface being copied from would not lock");
		return false;
	}

	ID3D11Resource * resource = NULL;
	view->GetResource(&resource);
	if (resource == NULL) {
		source->UnlockRect();
		return false;
	}

	D3D11_BOX box;
	box.left = (destination_point != NULL) ? destination_point->x : left;
	box.top = (destination_point != NULL) ? destination_point->y : top;
	box.front = 0;
	box.right = box.left + width;
	box.bottom = box.top + height;
	box.back = 1;

	// The copy was expanded to eight bits a channel when it was made, so the update has to be
	// expanded the same way or the rows land at the wrong stride in the wrong format.
	if (is_sixteen_bit_colour(source_description.Format)) {
		const unsigned pitch = width * 4;
		std::vector<unsigned char> expanded(pitch * height);
		expand_sixteen_bit(&expanded[0], pitch, (const unsigned char *)locked.pBits, locked.Pitch,
			width, height, source_description.Format);
		context->UpdateSubresource(resource, 0, &box, &expanded[0], pitch, 0);
	}
	else {
		context->UpdateSubresource(resource, 0, &box, locked.pBits, locked.Pitch, 0);
	}

	resource->Release();
	source->UnlockRect();
	return true;
}

ID3D11RenderTargetView * DX11Texture_Target(ID3D11Device * device, ID3D11DeviceContext * context,
	IDirect3DSurface9 * surface)
{
	if (surface == NULL) {
		return NULL;
	}

	// The surface the engine hands over is a level of a texture, and the copy lives on the texture.
	// A surface with no texture behind it is the device's own back buffer, which the backend has
	// already.
	IDirect3DTexture9 * texture = NULL;
	if (FAILED(surface->GetContainer(IID_IDirect3DTexture9, (void **)&texture)) || texture == NULL) {
		return NULL;
	}

	ID3D11RenderTargetView * target = NULL;
	DWORD size = sizeof(target);
	if (SUCCEEDED(texture->GetPrivateData(DX11_TEXTURE_TARGET, &target, &size))) {
		target->Release();
		texture->Release();
		return target;
	}

	ID3D11ShaderResourceView * view = DX11Texture_Mirror(device, context, texture);
	if (view == NULL) {
		texture->Release();
		return NULL;
	}

	ID3D11Resource * resource = NULL;
	view->GetResource(&resource);
	if (resource == NULL) {
		texture->Release();
		return NULL;
	}

	if (FAILED(device->CreateRenderTargetView(resource, NULL, &target))) {
		note_refusal("the D3D11 device would not make a render target view of the copy");
		resource->Release();
		texture->Release();
		return NULL;
	}
	resource->Release();

	texture->SetPrivateData(DX11_TEXTURE_TARGET, target, sizeof(target), D3DSPD_IUNKNOWN);
	target->Release();
	texture->Release();
	return target;
}

void DX11Texture_Take_Frame_Cost(double & milliseconds, unsigned & copies)
{
	milliseconds = FrameCopyMilliseconds;
	copies = FrameCopyCount;
	FrameCopyMilliseconds = 0.0;
	FrameCopyCount = 0;
}

void DX11Texture_Statistics(unsigned & mirrored, unsigned & reused, unsigned & refused)
{
	mirrored = Mirrored;
	reused = Reused;
	refused = Refused;
}

const char * DX11Texture_First_Refusal()
{
	return RefusalReason.c_str();
}

unsigned DX11Texture_Note_Count()
{
	return (unsigned)Notes.size();
}

const char * DX11Texture_Note(unsigned index)
{
	return (index < Notes.size()) ? Notes[index].c_str() : "";
}

static bool more_copies(const std::pair<std::string, CopyShapeCounts> & left,
	const std::pair<std::string, CopyShapeCounts> & right)
{
	return left.second.Builds + left.second.Refreshes > right.second.Builds + right.second.Refreshes;
}

unsigned DX11Texture_Copy_Shape_Count()
{
	std::vector<std::pair<std::string, CopyShapeCounts> > ranked(CopyShapes.begin(),
		CopyShapes.end());
	std::sort(ranked.begin(), ranked.end(), more_copies);

	CopyShapeLines.clear();
	for (size_t index = 0; index < ranked.size() && index < COPY_SHAPE_REPORT_LINES; ++index) {
		char line[160];
		snprintf(line, sizeof(line), "%s: %u built, %u refreshed", ranked[index].first.c_str(),
			ranked[index].second.Builds, ranked[index].second.Refreshes);
		CopyShapeLines.push_back(line);
	}
	return static_cast<unsigned>(CopyShapeLines.size());
}

const char * DX11Texture_Copy_Shape(unsigned index)
{
	return (index < CopyShapeLines.size()) ? CopyShapeLines[index].c_str() : "";
}
