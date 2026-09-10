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

// Where an edge lands, on both runtimes, from the same numbers: the edge a triangle's coverage
// makes, and the edge an alpha test cuts.  Both are answered by drawing on a Direct3D 9 device and
// through DX11BackendClass in one binary and reading the pixels back.
//
// Direct3D 9 and Direct3D 10 onwards disagree about where a pixel's centre is, and every account of
// that disagreement is written about screen space quads and texel alignment.  The engine's screen
// space quads are handled: they carry D3D9's own -0.5 and the generated program gives the half back.
// What no document here has ever established is whether ordinary projected geometry moves as well,
// and that is the question the residual now turns on.  Splitting a frame by surface fits a line
// through the two pictures and gets a slope of 1.000 on the command bar, 0.954 on terrain and 0.825
// on models: contrast lost in proportion to how much detail a surface carries, which is what a
// picture sampled half a pixel away from where it was drawn looks like.
//
// So this asks instead of arguing.  One quad, its right and bottom edges at screen 32.25 by
// arithmetic neither runtime is allowed a say in, rasterised once by each.  The last pixel each edge
// covers is the answer, and it was 32 against 31: the whole frame, not the screen space quads.
// Moving the D3D11 viewport half a pixel takes Alpine Assault from 5.99% away from the Direct3D 9
// frame to 1%.

#include "test_harness.h"

#include "dx11backend.h"
#include "dx11resource.h"
#include "test_d3d9device.h"

#include <stdio.h>
#include <string.h>

static const unsigned TARGET_SIZE = 64;

// A quarter of a pixel past a pixel boundary, so neither runtime is answering a tie.  Direct3D 9
// samples a pixel at its integer coordinate and covers pixel 32; Direct3D 11 samples at the
// half-integer and stops at 31 unless its viewport carries the half.
static const float EDGE_SCREEN = 32.25f;
static const float EDGE_CLIP_X = EDGE_SCREEN * 2.0f / TARGET_SIZE - 1.0f;

// Screen y runs down and clip y runs up, so the bottom edge of the quad is the one that lands at
// screen y 32.25.  Both axes are measured because the offset has a sign and a viewport carries two
// of them; getting y backwards moves the frame a whole pixel rather than none.
static const float EDGE_CLIP_Y = 1.0f - EDGE_SCREEN * 2.0f / TARGET_SIZE;

// Anything that is not the clear colour is covered.  The clear is green and the quad is red, so one
// channel decides it.
static const unsigned char COVERAGE_THRESHOLD = 0x80;

static const DWORD QUAD_COLOUR = 0xffff0000;

struct EdgeVertex
{
	float Position[3];
	float Normal[3];
	DWORD Diffuse;
	float TexCoord0[2];
	float TexCoord1[2];
};

static const DWORD EDGE_FVF = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX2;

// A rectangle hanging off the top-left corner of the target and ending at EDGE_CLIP_X across and
// EDGE_CLIP_Y down.  It starts outside the frame on purpose: an edge that lands exactly on row zero
// is a tie between two fill rules and this test is not about fill rules.  Both runtimes get these
// numbers unchanged, every matrix in the test being the identity, so nothing between the vertex and
// the rasteriser can move an edge.
static const float OUTSIDE = 1.2f;

static const EdgeVertex EDGE_QUAD[4] = {
	{ { -OUTSIDE, OUTSIDE, 0.5f }, { 0.0f, 0.0f, 1.0f }, QUAD_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
	{ { EDGE_CLIP_X, OUTSIDE, 0.5f }, { 0.0f, 0.0f, 1.0f }, QUAD_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
	{ { EDGE_CLIP_X, EDGE_CLIP_Y, 0.5f }, { 0.0f, 0.0f, 1.0f }, QUAD_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
	{ { -OUTSIDE, EDGE_CLIP_Y, 0.5f }, { 0.0f, 0.0f, 1.0f }, QUAD_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } }
};

static const unsigned short EDGE_INDICES[6] = { 0, 1, 2, 0, 2, 3 };

// The same rectangle with its two edges put wherever the caller asks, so the sweep below can walk
// them across a pixel instead of testing one position and calling the rule proven.
static void build_edge_quad(float screen, EdgeVertex quad[4])
{
	memcpy(quad, EDGE_QUAD, sizeof(EDGE_QUAD));
	const float clip_x = screen * 2.0f / TARGET_SIZE - 1.0f;
	const float clip_y = 1.0f - screen * 2.0f / TARGET_SIZE;
	quad[1].Position[0] = clip_x;
	quad[2].Position[0] = clip_x;
	quad[2].Position[1] = clip_y;
	quad[3].Position[1] = clip_y;
}

// The second question needs a quad that covers the target with its alpha running from nothing on the
// left to full on the right, so an alpha test cuts it somewhere in the middle and the column it cuts
// at is the threshold read back in pixels.  Eight of the sixty-six pipelines a Flash Effect frame
// builds carry an alpha test, and all eight are the greater-or-equal one.
static DWORD AlphaTestReference = 128;

// One knife-edge reference proves the rounding and nothing else.  A sweep proves the rule, which
// matters because what is left of the difference between the two frames sits on thin alpha tested
// foliage: a palm trunk a pixel wide is exactly where a threshold half a level out shows.
static const DWORD ALPHA_REFERENCES[] = { 1, 16, 32, 64, 96, 128, 160, 192, 224, 254 };
static const unsigned ALPHA_REFERENCE_COUNT =
	sizeof(ALPHA_REFERENCES) / sizeof(ALPHA_REFERENCES[0]);

static const EdgeVertex ALPHA_QUAD[4] = {
	{ { -1.0f,  1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, 0x00ff0000, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
	{ {  1.0f,  1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, 0xffff0000, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
	{ {  1.0f, -1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, 0xffff0000, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
	{ { -1.0f, -1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, 0x00ff0000, { 0.0f, 0.0f }, { 0.0f, 0.0f } }
};

// One line of the target, read out of whichever runtime drew it, as blue, green, red.
typedef unsigned char TargetLine[TARGET_SIZE][3];

// The index of the last covered pixel along a line of the target, or -1 if none of it is covered.
// The line is read through a stride, so one function reads a row and a column both.
static int last_covered(const unsigned char * line, unsigned stride)
{
	int last = -1;
	for (unsigned step = 0; step < TARGET_SIZE; ++step) {
		// bgra, so red is the third byte.
		if (line[step * stride + 2] >= COVERAGE_THRESHOLD) {
			last = static_cast<int>(step);
		}
	}
	return last;
}

static int first_covered(const TargetLine line)
{
	for (unsigned step = 0; step < TARGET_SIZE; ++step) {
		if (line[step][2] >= COVERAGE_THRESHOLD) {
			return static_cast<int>(step);
		}
	}
	return -1;
}

static bool direct3d9_draw(const EdgeVertex quad[4], bool alpha_test, TargetLine row,
	TargetLine column)
{
	D3D9OffscreenDevice fixture;
	if (!fixture.Create(TARGET_SIZE)) {
		return false;
	}
	IDirect3DDevice9 * device = fixture.Get_Device();

	D3DMATRIX identity;
	D3D9Test_Set_Identity(identity);

	fixture.Clear(D3DCOLOR_ARGB(255, 0, 255, 0));
	device->SetTransform(D3DTS_WORLD, &identity);
	device->SetTransform(D3DTS_VIEW, &identity);
	device->SetTransform(D3DTS_PROJECTION, &identity);
	device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	device->SetRenderState(D3DRS_LIGHTING, FALSE);
	device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, FALSE);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	device->SetRenderState(D3DRS_ALPHATESTENABLE, alpha_test ? TRUE : FALSE);
	device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
	device->SetRenderState(D3DRS_ALPHAREF, AlphaTestReference);
	device->SetVertexShader(NULL);
	device->SetFVF(EDGE_FVF);

	if (FAILED(device->BeginScene())) {
		return false;
	}
	const HRESULT drawn = device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2,
		EDGE_INDICES, D3DFMT_INDEX16, quad, sizeof(EdgeVertex));
	device->EndScene();
	if (FAILED(drawn)) {
		return false;
	}

	// Row zero for anything that varies across the target and column zero for anything that varies
	// down it, both of them lines the quads cover whatever the two runtimes decide.
	for (unsigned step = 0; step < TARGET_SIZE; ++step) {
		if (!fixture.Read_Pixel(step, 0, row[step])) {
			return false;
		}
	}
	for (unsigned step = 0; step < TARGET_SIZE; ++step) {
		if (!fixture.Read_Pixel(0, step, column[step])) {
			return false;
		}
	}
	return true;
}

// The same draw through the backend the engine drives, set up the way it sets an unlit pass through
// up: no lighting, the vertex colour selected, nothing bound.
static void configure_unlit(DX11BackendClass & backend)
{
	float identity[16];
	memset(identity, 0, sizeof(identity));
	identity[0] = 1.0f;
	identity[5] = 1.0f;
	identity[10] = 1.0f;
	identity[15] = 1.0f;
	backend.Set_Transform(D3DTS_WORLD, identity);
	backend.Set_Transform(D3DTS_VIEW, identity);
	backend.Set_Transform(D3DTS_PROJECTION, identity);

	backend.Set_Render_State(D3DRS_LIGHTING, FALSE);
	backend.Set_Render_State(D3DRS_COLORVERTEX, TRUE);
	backend.Set_Render_State(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
	backend.Set_Render_State(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
	backend.Set_Render_State(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
	backend.Set_Render_State(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);
	backend.Set_Render_State(D3DRS_ZENABLE, D3DZB_FALSE);
	backend.Set_Render_State(D3DRS_ZWRITEENABLE, FALSE);
	backend.Set_Render_State(D3DRS_CULLMODE, D3DCULL_NONE);
	backend.Set_Render_State(D3DRS_ALPHABLENDENABLE, FALSE);

	backend.Set_Texture_Stage_State(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	backend.Set_Texture_Stage_State(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	backend.Set_Texture_Stage_State(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	backend.Set_Texture_Stage_State(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	backend.Set_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU);
	backend.Set_Texture_Stage_State(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	backend.Set_Texture_Stage_State(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	backend.Set_Texture_Stage_State(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

	backend.Set_Vertex_Format(EDGE_FVF);
}

static bool direct3d11_draw(const EdgeVertex quad[4], bool alpha_test, TargetLine row,
	TargetLine column)
{
	DX11DeviceClass device;
	if (!device.Create_Offscreen()) {
		return false;
	}
	ID3D11Device * d3d = device.Get_Device();

	DX11BackendClass backend;
	if (!backend.Initialise(&device)) {
		return false;
	}

	D3D11_TEXTURE2D_DESC target_description;
	memset(&target_description, 0, sizeof(target_description));
	target_description.Width = TARGET_SIZE;
	target_description.Height = TARGET_SIZE;
	target_description.MipLevels = 1;
	target_description.ArraySize = 1;
	target_description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	target_description.SampleDesc.Count = 1;
	target_description.Usage = D3D11_USAGE_DEFAULT;
	target_description.BindFlags = D3D11_BIND_RENDER_TARGET;

	D3D11_TEXTURE2D_DESC staging_description = target_description;
	staging_description.Usage = D3D11_USAGE_STAGING;
	staging_description.BindFlags = 0;
	staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	ID3D11Texture2D * target = NULL;
	ID3D11Texture2D * staging = NULL;
	ID3D11RenderTargetView * target_view = NULL;
	ID3D11Buffer * vertices = NULL;
	ID3D11Buffer * indices = NULL;
	bool read = false;

	if (SUCCEEDED(d3d->CreateTexture2D(&target_description, NULL, &target))
		&& SUCCEEDED(d3d->CreateTexture2D(&staging_description, NULL, &staging))
		&& SUCCEEDED(d3d->CreateRenderTargetView(target, NULL, &target_view))
		&& DX11Resource_Create_Vertex_Buffer(d3d, sizeof(EdgeVertex) * 4, D3DPOOL_MANAGED, 0,
			quad, &vertices)
		&& DX11Resource_Create_Index_Buffer(d3d, sizeof(EDGE_INDICES), D3DPOOL_MANAGED, 0,
			EDGE_INDICES, &indices)) {

		const float clear_colour[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
		device.Get_Context()->ClearRenderTargetView(target_view, clear_colour);
		device.Get_Context()->OMSetRenderTargets(1, &target_view, NULL);

		backend.Set_Viewport(0, 0, TARGET_SIZE, TARGET_SIZE);
		configure_unlit(backend);
		backend.Set_Render_State(D3DRS_ALPHATESTENABLE, alpha_test ? TRUE : FALSE);
		backend.Set_Render_State(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
		backend.Set_Render_State(D3DRS_ALPHAREF, AlphaTestReference);
		backend.Set_Stream_Source(vertices, sizeof(EdgeVertex), 0);
		backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);

		if (backend.Draw_Indexed_Triangles(6, 0, 0)) {
			device.Get_Context()->CopyResource(staging, target);
			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
				const unsigned char * corner = static_cast<const unsigned char *>(mapped.pData);
				for (unsigned step = 0; step < TARGET_SIZE; ++step) {
					memcpy(row[step], corner + step * 4, 3);
					memcpy(column[step], corner + step * mapped.RowPitch, 3);
				}
				device.Get_Context()->Unmap(staging, 0);
				read = true;
			}
		}
	}

	if (indices != NULL) {
		indices->Release();
	}
	if (vertices != NULL) {
		vertices->Release();
	}
	if (target_view != NULL) {
		target_view->Release();
	}
	if (staging != NULL) {
		staging->Release();
	}
	if (target != NULL) {
		target->Release();
	}
	backend.Shutdown();
	return read;
}

TEST(pixelcentre_the_two_runtimes_cover_the_same_pixels)
{
	TargetLine nine_row;
	TargetLine nine_column;
	TargetLine eleven_row;
	TargetLine eleven_column;

	if (!direct3d9_draw(EDGE_QUAD, false, nine_row, nine_column)
		|| !direct3d11_draw(EDGE_QUAD, false, eleven_row, eleven_column)) {
		printf("  no Direct3D 9 or Direct3D 11 device on this machine - skipped\n");
		return;
	}

	const int nine_across = last_covered(&nine_row[0][0], 3);
	const int nine_down = last_covered(&nine_column[0][0], 3);
	const int eleven_across = last_covered(&eleven_row[0][0], 3);
	const int eleven_down = last_covered(&eleven_column[0][0], 3);

	printf("  edge at screen %.2f: Direct3D 9 covers to %d,%d and Direct3D 11 to %d,%d\n",
		EDGE_SCREEN, nine_across, nine_down, eleven_across, eleven_down);

	CHECK_EQ(eleven_across, nine_across);
	CHECK_EQ(eleven_down, nine_down);
}

// One position proves the offset. It does not prove the fill rule, and the difference that is left
// between the two frames is boundary pixels, so the fill rule is worth proving: walk both edges
// across a whole pixel in sixteenths and check that the two runtimes cover the same pixel at every
// step, ties included.
//
// If they agree all the way across, their sub-pixel grids and their fill rules are the same and a
// boundary pixel can only land differently because the vertex reaching the rasteriser differs in its
// last bits - which is the transform, not the rasteriser, and is not something a backend can fix.
TEST(pixelcentre_the_two_runtimes_agree_at_every_sub_pixel_position)
{
	const unsigned STEPS = 16;
	const float START = 32.0f;

	TargetLine nine_row;
	TargetLine nine_column;
	TargetLine eleven_row;
	TargetLine eleven_column;
	unsigned disagreements = 0;

	for (unsigned step = 0; step < STEPS; ++step) {
		const float screen = START + static_cast<float>(step) / STEPS;
		EdgeVertex quad[4];
		build_edge_quad(screen, quad);

		if (!direct3d9_draw(quad, false, nine_row, nine_column)
			|| !direct3d11_draw(quad, false, eleven_row, eleven_column)) {
			printf("  no Direct3D 9 or Direct3D 11 device on this machine - skipped\n");
			return;
		}

		const int nine_across = last_covered(&nine_row[0][0], 3);
		const int nine_down = last_covered(&nine_column[0][0], 3);
		const int eleven_across = last_covered(&eleven_row[0][0], 3);
		const int eleven_down = last_covered(&eleven_column[0][0], 3);

		if (eleven_across != nine_across || eleven_down != nine_down) {
			++disagreements;
			printf("  screen %.4f: Direct3D 9 covers to %d,%d and Direct3D 11 to %d,%d\n",
				screen, nine_across, nine_down, eleven_across, eleven_down);
		}
	}

	printf("  %u of %u sub-pixel positions disagree\n", disagreements, STEPS);
	CHECK_EQ(disagreements, 0u);
}

// The alpha test, which the generated pixel program turns into a clip because D3D11 has no alpha
// test of its own.  Direct3D 9 compared the eight bit alpha that was about to be written; the clip
// compares a float, and where those two disagree is a one pixel band along every alpha tested edge
// in the frame - which is where the difference that is left lives.  Eight of the sixty-six pipelines
// a Flash Effect frame builds carry one, and that view is one of the three still outside the margin.
TEST(pixelcentre_the_two_runtimes_cut_the_same_pixels)
{
	TargetLine nine_row;
	TargetLine nine_column;
	TargetLine eleven_row;
	TargetLine eleven_column;

	for (unsigned index = 0; index < ALPHA_REFERENCE_COUNT; ++index) {
		AlphaTestReference = ALPHA_REFERENCES[index];

		if (!direct3d9_draw(ALPHA_QUAD, true, nine_row, nine_column)
			|| !direct3d11_draw(ALPHA_QUAD, true, eleven_row, eleven_column)) {
			printf("  no Direct3D 9 or Direct3D 11 device on this machine - skipped\n");
			return;
		}

		const int nine_cut = first_covered(nine_row);
		const int eleven_cut = first_covered(eleven_row);

		if (eleven_cut != nine_cut) {
			printf("  alpha reference %u: Direct3D 9 keeps from column %d, Direct3D 11 from %d\n",
				AlphaTestReference, nine_cut, eleven_cut);
		}
		CHECK_EQ(eleven_cut, nine_cut);
	}
}
