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

// The post-process chain, run for real on a device with a swap chain.
//
// A known picture is written into the texture the scene would have been drawn into, the chain is
// run, and the swap chain's back buffer is read back.  So these are not tests that the code was
// called; they are tests of what came out the other end.
//
// The two that matter are opposites.  The copy pass has to give back every pixel unchanged, which
// is what says the offscreen target, the full screen triangle, the sampler and the viewport are all
// exactly one to one - a half pixel anywhere in that path shows up here as a whole frame that moved.
// FXAA has to change a stepped diagonal and leave a flat field alone, which is what says the shader
// is doing the thing it is named after rather than blurring or nothing.
//
// A machine with no Direct3D 11 device at all reports these as skipped: there is nothing to check
// there, and the build agents that cannot make a device still have to come back green.

#include "test_harness.h"

#include "dx11device.h"
#include "dx11post.h"

#include <d3d11.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

static const unsigned SURFACE_SIZE = 64;
static const unsigned BYTES_A_PIXEL = 4;
static const unsigned char DARK_LEVEL = 0;
static const unsigned char LIGHT_LEVEL = 255;
static const unsigned char OPAQUE_ALPHA = 255;

// A device with a swap chain, and the window it needs, torn down together.
class DeviceFixture
{
public:
	DeviceFixture() : Window(NULL) {}

	~DeviceFixture()
	{
		Post.Shutdown();
		Device.Release();
		if (Window != NULL) {
			DestroyWindow(Window);
		}
	}

	bool Create()
	{
		Window = CreateWindowExA(0, "STATIC", "dx11post", WS_OVERLAPPED, 0, 0, SURFACE_SIZE,
			SURFACE_SIZE, NULL, NULL, GetModuleHandle(NULL), NULL);
		if (Window == NULL) {
			return false;
		}
		if (!Device.Create(Window, SURFACE_SIZE, SURFACE_SIZE)) {
			return false;
		}
		return Post.Initialise(&Device);
	}

	DX11DeviceClass & Get_Device() { return Device; }
	DX11PostProcessClass & Get_Post() { return Post; }

private:
	DeviceFixture(const DeviceFixture &);
	DeviceFixture & operator=(const DeviceFixture &);

	HWND Window;
	DX11DeviceClass Device;
	DX11PostProcessClass Post;
};

// One pixel of the test picture, in the swap chain's own blue-green-red-alpha order.
static void set_pixel(unsigned char * pixels, unsigned x, unsigned y, unsigned char level)
{
	unsigned char * pixel = pixels + (y * SURFACE_SIZE + x) * BYTES_A_PIXEL;
	pixel[0] = level;
	pixel[1] = level;
	pixel[2] = level;
	pixel[3] = OPAQUE_ALPHA;
}

static unsigned char pixel_level(const unsigned char * pixels, unsigned pitch, unsigned x,
	unsigned y)
{
	return pixels[y * pitch + x * BYTES_A_PIXEL];
}

// Everything one level, which has no edge in it for an edge filter to find.
static void fill_flat(unsigned char * pixels, unsigned char level)
{
	for (unsigned y = 0; y < SURFACE_SIZE; ++y) {
		for (unsigned x = 0; x < SURFACE_SIZE; ++x) {
			set_pixel(pixels, x, y, level);
		}
	}
}

// A diagonal, which is the shape that aliases.  A vertical or a horizontal edge lands exactly on
// the pixel grid and has nothing to antialias, and FXAA leaves those alone by design, so a test
// built on one would pass whether the shader worked or not.
static void fill_diagonal(unsigned char * pixels)
{
	for (unsigned y = 0; y < SURFACE_SIZE; ++y) {
		for (unsigned x = 0; x < SURFACE_SIZE; ++x) {
			set_pixel(pixels, x, y, (x > y) ? LIGHT_LEVEL : DARK_LEVEL);
		}
	}
}

// Write the picture into the texture the scene would have been drawn into.
static bool write_scene(DX11DeviceClass & device, DX11PostProcessClass & post,
	const unsigned char * pixels)
{
	ID3D11RenderTargetView * view = post.Scene_View();
	if (view == NULL) {
		return false;
	}

	ID3D11Resource * resource = NULL;
	view->GetResource(&resource);
	if (resource == NULL) {
		return false;
	}
	device.Get_Context()->UpdateSubresource(resource, 0, NULL, pixels,
		SURFACE_SIZE * BYTES_A_PIXEL, 0);
	resource->Release();
	return true;
}

// The swap chain's back buffer, copied into a staging texture and mapped.  The caller frees it.
static unsigned char * read_back_buffer(DX11DeviceClass & device, unsigned & pitch)
{
	pitch = 0;
	ID3D11RenderTargetView * view = device.Get_Back_Buffer_View();
	if (view == NULL) {
		return NULL;
	}

	ID3D11Resource * back_buffer = NULL;
	view->GetResource(&back_buffer);
	if (back_buffer == NULL) {
		return NULL;
	}

	D3D11_TEXTURE2D_DESC description;
	((ID3D11Texture2D *)back_buffer)->GetDesc(&description);
	description.Usage = D3D11_USAGE_STAGING;
	description.BindFlags = 0;
	description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	description.MiscFlags = 0;

	ID3D11Texture2D * staging = NULL;
	if (FAILED(device.Get_Device()->CreateTexture2D(&description, NULL, &staging))) {
		back_buffer->Release();
		return NULL;
	}
	device.Get_Context()->CopyResource(staging, back_buffer);

	D3D11_MAPPED_SUBRESOURCE mapped;
	unsigned char * pixels = NULL;
	if (SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
		pitch = SURFACE_SIZE * BYTES_A_PIXEL;
		pixels = new unsigned char[pitch * SURFACE_SIZE];
		for (unsigned row = 0; row < SURFACE_SIZE; ++row) {
			memcpy(pixels + row * pitch, (const unsigned char *)mapped.pData + row * mapped.RowPitch,
				pitch);
		}
		device.Get_Context()->Unmap(staging, 0);
	}

	staging->Release();
	back_buffer->Release();
	return pixels;
}

// How many pixels of the result differ from the picture that went in.
static unsigned differing_pixels(const unsigned char * source, const unsigned char * result,
	unsigned pitch)
{
	unsigned differing = 0;
	for (unsigned y = 0; y < SURFACE_SIZE; ++y) {
		for (unsigned x = 0; x < SURFACE_SIZE; ++x) {
			if (pixel_level(source, SURFACE_SIZE * BYTES_A_PIXEL, x, y)
					!= pixel_level(result, pitch, x, y)) {
				++differing;
			}
		}
	}
	return differing;
}

// Runs one chain over one picture and hands back what reached the swap chain.  NULL when this
// machine has no device to run it on.
static unsigned char * run_chain(DeviceFixture & fixture, const DX11PostEffect * effects,
	unsigned count, const unsigned char * picture, unsigned & pitch)
{
	fixture.Get_Post().Set_Chain(effects, count);
	fixture.Get_Post().Begin_Frame();
	if (!write_scene(fixture.Get_Device(), fixture.Get_Post(), picture)) {
		return NULL;
	}
	fixture.Get_Post().Run_Chain();
	return read_back_buffer(fixture.Get_Device(), pitch);
}

TEST(dx11post_chain_names_parse)
{
	DX11PostEffect effects[DX11_POST_CHAIN_LIMIT];
	unsigned count = 0;

	CHECK(DX11Post_Parse_Chain("fxaa", effects, count));
	CHECK_EQ(1u, count);
	CHECK_EQ((int)DX11_POST_FXAA, (int)effects[0]);

	CHECK(DX11Post_Parse_Chain("fxaa,sharpen", effects, count));
	CHECK_EQ(2u, count);
	CHECK_EQ((int)DX11_POST_FXAA, (int)effects[0]);
	CHECK_EQ((int)DX11_POST_SHARPEN, (int)effects[1]);

	CHECK(DX11Post_Parse_Chain("copy", effects, count));
	CHECK_EQ(1u, count);
	CHECK_EQ((int)DX11_POST_COPY, (int)effects[0]);

	CHECK(DX11Post_Parse_Chain("bloom", effects, count));
	CHECK_EQ(1u, count);
	CHECK_EQ((int)DX11_POST_BLOOM, (int)effects[0]);

	// Bloom first and antialiasing after it is the order that means something: the frame is back in
	// eight bits by then and FXAA is looking at what the player will see.
	CHECK(DX11Post_Parse_Chain("bloom,fxaa", effects, count));
	CHECK_EQ(2u, count);
	CHECK_EQ((int)DX11_POST_BLOOM, (int)effects[0]);

	// The other way round cannot be answered, so it is refused rather than quietly reordered.
	CHECK(!DX11Post_Parse_Chain("fxaa,bloom", effects, count));
	CHECK_EQ(0u, count);

	CHECK_STR("fxaa", DX11Post_Effect_Name(DX11_POST_FXAA));
	CHECK_STR("sharpen", DX11Post_Effect_Name(DX11_POST_SHARPEN));
	CHECK_STR("copy", DX11Post_Effect_Name(DX11_POST_COPY));
	CHECK_STR("bloom", DX11Post_Effect_Name(DX11_POST_BLOOM));
}

TEST(dx11post_off_and_nonsense_both_leave_no_chain)
{
	DX11PostEffect effects[DX11_POST_CHAIN_LIMIT];
	unsigned count = 0;

	// "off" is understood and asks for nothing.
	CHECK(DX11Post_Parse_Chain("off", effects, count));
	CHECK_EQ(0u, count);
	CHECK(DX11Post_Parse_Chain("none", effects, count));
	CHECK_EQ(0u, count);
	CHECK(DX11Post_Parse_Chain("", effects, count));
	CHECK_EQ(0u, count);

	// A name nobody knows is refused rather than skipped, so -dx11post motionblur does not quietly
	// run something else.
	CHECK(!DX11Post_Parse_Chain("motionblur", effects, count));
	CHECK_EQ(0u, count);
	CHECK(!DX11Post_Parse_Chain("fxaa,motionblur", effects, count));
	CHECK_EQ(0u, count);
	CHECK(!DX11Post_Parse_Chain(NULL, effects, count));
	CHECK_EQ(0u, count);

	// Longer than the chain can hold is refused too, rather than being cut short.
	CHECK(!DX11Post_Parse_Chain("fxaa,fxaa,fxaa,fxaa,fxaa", effects, count));
	CHECK_EQ(0u, count);
}

TEST(dx11post_no_chain_leaves_the_scene_on_the_swap_chain)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	fixture.Get_Post().Set_Chain(NULL, 0);
	CHECK_EQ(0u, fixture.Get_Post().Chain_Length());
	// NULL is the device's word for the swap chain, so an absent chain costs nothing at all: the
	// scene is drawn where it has always been drawn.
	CHECK(fixture.Get_Post().Scene_View() == NULL);
}

TEST(dx11post_copy_gives_every_pixel_back_unchanged)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	unsigned char * picture = new unsigned char[SURFACE_SIZE * SURFACE_SIZE * BYTES_A_PIXEL];
	fill_diagonal(picture);

	const DX11PostEffect chain[] = { DX11_POST_COPY };
	unsigned pitch = 0;
	unsigned char * result = run_chain(fixture, chain, 1, picture, pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		// Exactly zero. A sampler set to point instead of linear, a viewport carrying the scene's
		// half pixel, or a triangle that does not land on the corners would all put a number here.
		CHECK_EQ(0u, differing_pixels(picture, result, pitch));
		delete [] result;
	}
	delete [] picture;
}

TEST(dx11post_fxaa_leaves_a_flat_picture_alone)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	unsigned char * picture = new unsigned char[SURFACE_SIZE * SURFACE_SIZE * BYTES_A_PIXEL];
	fill_flat(picture, 128);

	const DX11PostEffect chain[] = { DX11_POST_FXAA };
	unsigned pitch = 0;
	unsigned char * result = run_chain(fixture, chain, 1, picture, pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		// The edge threshold is what keeps the filter off a surface that has no edge in it. Without
		// it every flat wall in the game would be run through a blur.
		CHECK_EQ(0u, differing_pixels(picture, result, pitch));
		delete [] result;
	}
	delete [] picture;
}

TEST(dx11post_fxaa_works_on_a_stepped_diagonal)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	unsigned char * picture = new unsigned char[SURFACE_SIZE * SURFACE_SIZE * BYTES_A_PIXEL];
	fill_diagonal(picture);

	const DX11PostEffect chain[] = { DX11_POST_FXAA };
	unsigned pitch = 0;
	unsigned char * result = run_chain(fixture, chain, 1, picture, pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		// The staircase is 64 steps long and every step has pixels either side of it that should
		// have come out between the two levels. Well short of that and the shader is not running.
		const unsigned changed = differing_pixels(picture, result, pitch);
		CHECK(changed > 32u);

		// And it is antialiasing rather than repainting: the far corners are nowhere near the edge
		// and have to come back untouched.
		CHECK_EQ(DARK_LEVEL, pixel_level(result, pitch, 1, SURFACE_SIZE - 2));
		CHECK_EQ(LIGHT_LEVEL, pixel_level(result, pitch, SURFACE_SIZE - 2, 1));
		delete [] result;
	}
	delete [] picture;
}

TEST(dx11post_a_frame_with_no_world_in_it_goes_through_untouched)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	unsigned char * picture = new unsigned char[SURFACE_SIZE * SURFACE_SIZE * BYTES_A_PIXEL];
	fill_diagonal(picture);

	// A menu draws no world, so nothing calls Run_Chain and the frame would sit in the offscreen
	// texture with the screen black behind it. Copy_Through is what the present path falls back to,
	// and because it is a copy rather than the chain, a screen made entirely of text is not run
	// through an edge filter on its way out.
	const DX11PostEffect chain[] = { DX11_POST_FXAA };
	fixture.Get_Post().Set_Chain(chain, 1);
	fixture.Get_Post().Begin_Frame();
	CHECK(write_scene(fixture.Get_Device(), fixture.Get_Post(), picture));
	CHECK(fixture.Get_Post().Copy_Through());

	unsigned pitch = 0;
	unsigned char * result = read_back_buffer(fixture.Get_Device(), pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		CHECK_EQ(0u, differing_pixels(picture, result, pitch));
		delete [] result;
	}
	delete [] picture;
}

// Half floats, written by hand.  Only three values are needed and a general converter would be more
// code than the constants: 4.0 is sign 0, exponent 17 biased, mantissa 0, and 1.0 is exponent 15.
static const unsigned short HALF_ZERO = 0x0000;
static const unsigned short HALF_ONE = 0x3C00;
static const unsigned short HALF_FOUR = 0x4400;

static const unsigned BRIGHT_BLOCK_SIZE = 4;
static const unsigned HALF_CHANNELS = 4;

// Clear the scene to one colour.  Works whatever the target's format, which an UpdateSubresource of
// eight bit pixels does not: with bloom in the chain the scene target holds half floats.
static bool clear_scene(DX11DeviceClass & device, DX11PostProcessClass & post, float level)
{
	ID3D11RenderTargetView * view = post.Scene_View();
	if (view == NULL) {
		return false;
	}
	const float colour[4] = { level, level, level, 1.0f };
	device.Get_Context()->ClearRenderTargetView(view, colour);
	return true;
}

// A small block of light in the middle of a black scene, at four times the brightness of white.
static bool write_bright_block(DX11DeviceClass & device, DX11PostProcessClass & post)
{
	ID3D11RenderTargetView * view = post.Scene_View();
	if (view == NULL) {
		return false;
	}

	unsigned short block[BRIGHT_BLOCK_SIZE * BRIGHT_BLOCK_SIZE * HALF_CHANNELS];
	for (unsigned index = 0; index < BRIGHT_BLOCK_SIZE * BRIGHT_BLOCK_SIZE; ++index) {
		block[index * HALF_CHANNELS + 0] = HALF_FOUR;
		block[index * HALF_CHANNELS + 1] = HALF_FOUR;
		block[index * HALF_CHANNELS + 2] = HALF_FOUR;
		block[index * HALF_CHANNELS + 3] = HALF_ONE;
	}

	ID3D11Resource * resource = NULL;
	view->GetResource(&resource);
	if (resource == NULL) {
		return false;
	}

	const unsigned left = SURFACE_SIZE / 2;
	const unsigned top = SURFACE_SIZE / 2;
	D3D11_BOX box;
	box.left = left;
	box.top = top;
	box.front = 0;
	box.right = left + BRIGHT_BLOCK_SIZE;
	box.bottom = top + BRIGHT_BLOCK_SIZE;
	box.back = 1;
	device.Get_Context()->UpdateSubresource(resource, 0, &box, block,
		BRIGHT_BLOCK_SIZE * HALF_CHANNELS * sizeof(unsigned short), 0);
	resource->Release();
	return true;
}

TEST(dx11post_bloom_keeps_ordinary_levels_and_folds_overbright_to_white)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	const DX11PostEffect chain[] = { DX11_POST_BLOOM };
	fixture.Get_Post().Set_Chain(chain, 1);
	CHECK(fixture.Get_Post().Scene_Is_Float());

	// Half of white is below the tone curve's knee and below the bright threshold, so it has to
	// come back out as half of white. If it does not, the curve is being applied to the whole range
	// and every ordinary pixel in the game has been moved.
	fixture.Get_Post().Begin_Frame();
	CHECK(clear_scene(fixture.Get_Device(), fixture.Get_Post(), 0.5f));
	CHECK(fixture.Get_Post().Run_Chain());
	unsigned pitch = 0;
	unsigned char * result = read_back_buffer(fixture.Get_Device(), pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		const unsigned char level = pixel_level(result, pitch, SURFACE_SIZE / 2, SURFACE_SIZE / 2);
		CHECK(level >= 127 && level <= 128);
		delete [] result;
	}

	// Four times white is what an eight bit target could not have held at all. It arrives as white
	// rather than as anything darker, which is the curve reaching its asymptote.
	fixture.Get_Post().Begin_Frame();
	CHECK(clear_scene(fixture.Get_Device(), fixture.Get_Post(), 4.0f));
	CHECK(fixture.Get_Post().Run_Chain());
	result = read_back_buffer(fixture.Get_Device(), pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		CHECK_EQ(255, (int)pixel_level(result, pitch, SURFACE_SIZE / 2, SURFACE_SIZE / 2));
		delete [] result;
	}
}

TEST(dx11post_bloom_spreads_light_past_the_pixels_that_carry_it)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	const DX11PostEffect chain[] = { DX11_POST_BLOOM };
	fixture.Get_Post().Set_Chain(chain, 1);
	fixture.Get_Post().Begin_Frame();
	CHECK(clear_scene(fixture.Get_Device(), fixture.Get_Post(), 0.0f));
	CHECK(write_bright_block(fixture.Get_Device(), fixture.Get_Post()));
	CHECK(fixture.Get_Post().Run_Chain());

	unsigned pitch = 0;
	unsigned char * result = read_back_buffer(fixture.Get_Device(), pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		// Ten pixels above the block there was nothing at all in the scene. The glow reaches it,
		// which is the whole of what bloom is; without the bright pass or the blur this is zero.
		// "near" is a macro in windows.h, which is why this is not called that.
		const unsigned char aboveTheBlock = pixel_level(result, pitch, SURFACE_SIZE / 2,
			SURFACE_SIZE / 2 - 10);
		CHECK(aboveTheBlock > 0);

		// The opposite corner is far outside the blur's reach and has to stay black, or the glow is
		// not a glow, it is a wash over the frame.
		CHECK_EQ(0, (int)pixel_level(result, pitch, 1, 1));
		delete [] result;
	}
}

TEST(dx11post_resolves_once_a_frame)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	unsigned char * first = new unsigned char[SURFACE_SIZE * SURFACE_SIZE * BYTES_A_PIXEL];
	unsigned char * second = new unsigned char[SURFACE_SIZE * SURFACE_SIZE * BYTES_A_PIXEL];
	fill_flat(first, LIGHT_LEVEL);
	fill_flat(second, DARK_LEVEL);

	const DX11PostEffect chain[] = { DX11_POST_COPY };
	unsigned pitch = 0;
	unsigned char * result = run_chain(fixture, chain, 1, first, pitch);
	CHECK(result != NULL);
	delete [] result;

	// A second finish without a Begin_Frame between them is the screenshot path asking for a frame
	// the present path has already finished. It has to do nothing, or the shot and the screen would
	// be two different pictures.
	CHECK(write_scene(fixture.Get_Device(), fixture.Get_Post(), second));
	CHECK(!fixture.Get_Post().Run_Chain());
	CHECK(!fixture.Get_Post().Copy_Through());
	result = read_back_buffer(fixture.Get_Device(), pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		CHECK_EQ(LIGHT_LEVEL, pixel_level(result, pitch, SURFACE_SIZE / 2, SURFACE_SIZE / 2));
		delete [] result;
	}

	// Armed again, the new picture goes through.
	fixture.Get_Post().Begin_Frame();
	CHECK(fixture.Get_Post().Run_Chain());
	result = read_back_buffer(fixture.Get_Device(), pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		CHECK_EQ(DARK_LEVEL, pixel_level(result, pitch, SURFACE_SIZE / 2, SURFACE_SIZE / 2));
		delete [] result;
	}

	delete [] first;
	delete [] second;
}
