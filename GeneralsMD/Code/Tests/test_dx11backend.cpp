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

// The backend driven the way the engine drives a device: set a transform, set a stage state, set a
// stream, draw.  Nothing here builds a shader or a state object by hand, because the point is that
// the engine does not either.
//
// What it checks is the colour that lands in the render target, which is the only thing that tells
// the whole chain apart from a chain that merely runs: a transform applied on the wrong side, a
// vertex colour read as the wrong format, a combiner argument crossed, all of them draw something.

#include "test_harness.h"

#include "dx11backend.h"
#include "dx11resource.h"

#include <stdio.h>
#include <string.h>
#include <vector>

static const unsigned TARGET_SIZE = 64;
static const DWORD BACKEND_FVF = D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX2|D3DFVF_DIFFUSE;

// All four bytes different, so a channel read in the wrong order is a wrong answer rather than a
// lucky one.  D3DCOLOR order is alpha, red, green, blue.
static const DWORD VERTEX_COLOUR = 0xff112233;
static const unsigned char EXPECTED_BLUE = 0x33;
static const unsigned char EXPECTED_GREEN = 0x22;
static const unsigned char EXPECTED_RED = 0x11;

struct BackendVertex
{
	float Position[3];
	float Normal[3];
	DWORD Diffuse;
	float TexCoord0[2];
	float TexCoord1[2];
};

static void set_identity(float matrix[16])
{
	memset(matrix, 0, sizeof(float) * 16);
	matrix[0] = 1.0f;
	matrix[5] = 1.0f;
	matrix[10] = 1.0f;
	matrix[15] = 1.0f;
}

// A quad already in clip space, drawn as two triangles the way every mesh in the engine is.
static const BackendVertex QUAD_VERTICES[4] = {
	{ { -1.0f, -1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
	{ { -1.0f,  1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
	{ {  1.0f,  1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
	{ {  1.0f, -1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } }
};

static const unsigned short QUAD_INDICES[6] = { 0, 1, 2, 0, 2, 3 };

// Sets the backend up the way an unlit textureless draw is set up, and leaves it ready to draw.
static void configure_unlit_pass_through(DX11BackendClass & backend)
{
	float identity[16];
	set_identity(identity);
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

	// One stage, selecting the diffuse colour, with nothing bound: the combiner that hands the
	// vertex colour through untouched.
	backend.Set_Texture_Stage_State(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	backend.Set_Texture_Stage_State(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	backend.Set_Texture_Stage_State(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	backend.Set_Texture_Stage_State(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	backend.Set_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU);
	backend.Set_Texture_Stage_State(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	backend.Set_Texture_Stage_State(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	backend.Set_Texture_Stage_State(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

	backend.Set_Vertex_Format(BACKEND_FVF);
}

TEST(dx11backend_a_draw_set_up_the_d3d9_way_lands_in_the_render_target)
{
	DX11DeviceClass device;
	device.Request_Debug_Layer();
	CHECK(device.Create_Offscreen());
	ID3D11Device * d3d = device.Get_Device();

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));

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

	ID3D11Texture2D * target = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&target_description, NULL, &target)));

	D3D11_TEXTURE2D_DESC staging_description = target_description;
	staging_description.Usage = D3D11_USAGE_STAGING;
	staging_description.BindFlags = 0;
	staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	ID3D11Texture2D * staging = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&staging_description, NULL, &staging)));

	ID3D11RenderTargetView * target_view = NULL;
	CHECK(SUCCEEDED(d3d->CreateRenderTargetView(target, NULL, &target_view)));

	const float clear_colour[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
	device.Get_Context()->ClearRenderTargetView(target_view, clear_colour);
	device.Get_Context()->OMSetRenderTargets(1, &target_view, NULL);

	backend.Set_Viewport(0, 0, TARGET_SIZE, TARGET_SIZE);
	configure_unlit_pass_through(backend);

	// The buffers, asked for the way the engine asks: a managed vertex buffer filled once and a
	// managed index buffer beside it.
	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(d3d, sizeof(QUAD_VERTICES), D3DPOOL_MANAGED, 0,
		QUAD_VERTICES, &vertices));

	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(d3d, sizeof(QUAD_INDICES), D3DPOOL_MANAGED, 0,
		QUAD_INDICES, &indices));

	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);

	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));

	device.Get_Context()->CopyResource(staging, target);

	D3D11_MAPPED_SUBRESOURCE mapped;
	CHECK(SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));

	const unsigned char * centre = static_cast<const unsigned char *>(mapped.pData)
		+ mapped.RowPitch * (TARGET_SIZE / 2) + (TARGET_SIZE / 2) * 4;
	if (centre[0] != EXPECTED_BLUE) {
		printf("  centre pixel bgra %02x %02x %02x %02x\n",
			centre[0], centre[1], centre[2], centre[3]);
	}
	CHECK_EQ(centre[0], EXPECTED_BLUE);
	CHECK_EQ(centre[1], EXPECTED_GREEN);
	CHECK_EQ(centre[2], EXPECTED_RED);

	// The quad covers the target, so a corner says the same thing.  A transform applied on the
	// wrong side leaves the centre right and the corners green.
	const unsigned char * corner = static_cast<const unsigned char *>(mapped.pData);
	CHECK_EQ(corner[0], EXPECTED_BLUE);
	CHECK_EQ(corner[2], EXPECTED_RED);

	device.Get_Context()->Unmap(staging, 0);

	unsigned pipelines = 0;
	unsigned long long made = 0;
	unsigned long long refused = 0;
	backend.Statistics(pipelines, made, refused);
	CHECK_EQ(pipelines, 1u);
	CHECK_EQ(made, 1ull);
	CHECK_EQ(refused, 0ull);

	indices->Release();
	vertices->Release();
	target_view->Release();
	staging->Release();
	target->Release();
	backend.Shutdown();
}

// Direct3D 9 takes a light in world space and lights in camera space, carrying the light through the
// view matrix itself.  The generated shader lights in camera space too, so the backend has to make
// the same product before the light goes up; without it a world space direction is dotted against a
// camera space normal and every model is shaded from the wrong angle.
//
// The view here is a quarter turn about x and the world is its inverse, so the quad and its normal
// come out exactly where they started and the only thing the view matrix touches is the light.  A
// world direction of (0,-1,0) becomes (0,0,-1) in camera space, which is straight at the quad: the
// fix lights it fully and the fault lights it not at all.
TEST(dx11backend_a_light_is_carried_into_camera_space)
{
	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());
	ID3D11Device * d3d = device.Get_Device();

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));

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

	ID3D11Texture2D * target = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&target_description, NULL, &target)));

	D3D11_TEXTURE2D_DESC staging_description = target_description;
	staging_description.Usage = D3D11_USAGE_STAGING;
	staging_description.BindFlags = 0;
	staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	ID3D11Texture2D * staging = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&staging_description, NULL, &staging)));

	ID3D11RenderTargetView * target_view = NULL;
	CHECK(SUCCEEDED(d3d->CreateRenderTargetView(target, NULL, &target_view)));

	const float clear_colour[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
	device.Get_Context()->ClearRenderTargetView(target_view, clear_colour);
	device.Get_Context()->OMSetRenderTargets(1, &target_view, NULL);

	backend.Set_Viewport(0, 0, TARGET_SIZE, TARGET_SIZE);
	configure_unlit_pass_through(backend);

	// A quarter turn about x, and the world matrix that undoes it.
	float view[16];
	set_identity(view);
	view[5] = 0.0f;  view[6] = 1.0f;
	view[9] = -1.0f; view[10] = 0.0f;

	float world[16];
	set_identity(world);
	world[5] = 0.0f;  world[6] = -1.0f;
	world[9] = 1.0f;  world[10] = 0.0f;

	backend.Set_Transform(D3DTS_WORLD, world);
	backend.Set_Transform(D3DTS_VIEW, view);

	backend.Set_Render_State(D3DRS_LIGHTING, TRUE);
	backend.Set_Render_State(D3DRS_SPECULARENABLE, FALSE);
	backend.Set_Render_State(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
	backend.Set_Render_State(D3DRS_AMBIENT, 0);

	const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	const float black[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	backend.Set_Material(black, white, black, black, 1.0f);

	// Blue brightest, red dimmest, so a channel swap is a wrong answer rather than a lucky one.
	const float light_diffuse[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
	const float direction[4] = { 0.0f, -1.0f, 0.0f, 0.0f };
	const float attenuation[4] = { 1.0f, 0.0f, 0.0f, 100000.0f };
	backend.Set_Light(0, D3DLIGHT_DIRECTIONAL, black, direction, light_diffuse, black,
		attenuation, black);

	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(d3d, sizeof(QUAD_VERTICES), D3DPOOL_MANAGED, 0,
		QUAD_VERTICES, &vertices));

	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(d3d, sizeof(QUAD_INDICES), D3DPOOL_MANAGED, 0,
		QUAD_INDICES, &indices));

	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);

	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));

	device.Get_Context()->CopyResource(staging, target);

	D3D11_MAPPED_SUBRESOURCE mapped;
	CHECK(SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));

	const unsigned char * centre = static_cast<const unsigned char *>(mapped.pData)
		+ mapped.RowPitch * (TARGET_SIZE / 2) + (TARGET_SIZE / 2) * 4;
	if (centre[0] < 189 || centre[0] > 193) {
		printf("  centre pixel bgra %02x %02x %02x %02x\n",
			centre[0], centre[1], centre[2], centre[3]);
	}
	CHECK(centre[0] >= 189 && centre[0] <= 193);
	CHECK(centre[1] >= 126 && centre[1] <= 130);
	CHECK(centre[2] >= 62 && centre[2] <= 66);

	device.Get_Context()->Unmap(staging, 0);

	indices->Release();
	vertices->Release();
	target_view->Release();
	staging->Release();
	target->Release();
	backend.Shutdown();
}

// The same state twice is one pipeline, and a changed stage program is a second.  The engine sets
// the same handful of combinations thousands of times a frame, so a cache that missed would compile
// a shader per draw and the frame time would be the symptom rather than the picture.
TEST(dx11backend_the_same_state_resolves_to_one_pipeline)
{
	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));

	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(device.Get_Device(), sizeof(QUAD_VERTICES),
		D3DPOOL_MANAGED, 0, QUAD_VERTICES, &vertices));

	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(device.Get_Device(), sizeof(QUAD_INDICES),
		D3DPOOL_MANAGED, 0, QUAD_INDICES, &indices));

	backend.Set_Viewport(0, 0, TARGET_SIZE, TARGET_SIZE);
	configure_unlit_pass_through(backend);
	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);

	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));
	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));

	unsigned pipelines = 0;
	unsigned long long made = 0;
	unsigned long long refused = 0;
	backend.Statistics(pipelines, made, refused);
	CHECK_EQ(pipelines, 1u);
	CHECK_EQ(made, 2ull);

	// A different combiner program is a different pipeline.
	backend.Set_Texture_Stage_State(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	backend.Set_Texture_Stage_State(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));

	backend.Statistics(pipelines, made, refused);
	CHECK_EQ(pipelines, 2u);
	CHECK_EQ(refused, 0ull);

	indices->Release();
	vertices->Release();
	backend.Shutdown();
}

static const char * const CACHE_FILE_NAME = "dx11backend_test_shaders.cache";
static const long LAST_RECORD_CUT_BYTES = 4;

// A pipeline compiled in one run is not compiled again in the next: the backend writes what it
// compiled when it shuts down, and a second one reading the file holds both halves of the program
// before it has drawn anything.  A file cut short while it was written gives up its last record and
// keeps the rest, which is what a crash during the write leaves behind.
TEST(dx11backend_compiled_programs_are_kept_in_the_cache_file)
{
	char path[MAX_PATH];
	const DWORD directory_length = GetTempPathA(MAX_PATH, path);
	CHECK(directory_length > 0 && directory_length + strlen(CACHE_FILE_NAME) < MAX_PATH);
	strcat(path, CACHE_FILE_NAME);
	DeleteFileA(path);

	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());

	DX11BackendClass writer;
	CHECK(writer.Initialise(&device));
	writer.Set_Shader_Cache_Path(path);
	CHECK_EQ(writer.Compiled_Program_Count(), 0u);

	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(device.Get_Device(), sizeof(QUAD_VERTICES),
		D3DPOOL_MANAGED, 0, QUAD_VERTICES, &vertices));
	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(device.Get_Device(), sizeof(QUAD_INDICES),
		D3DPOOL_MANAGED, 0, QUAD_INDICES, &indices));

	writer.Set_Viewport(0, 0, TARGET_SIZE, TARGET_SIZE);
	configure_unlit_pass_through(writer);
	writer.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	writer.Set_Indices(indices, DXGI_FORMAT_R16_UINT);
	CHECK(writer.Draw_Indexed_Triangles(6, 0, 0));
	CHECK_EQ(writer.Compiled_Program_Count(), 2u);

	indices->Release();
	vertices->Release();
	writer.Shutdown();

	DX11BackendClass reader;
	CHECK(reader.Initialise(&device));
	reader.Set_Shader_Cache_Path(path);
	CHECK_EQ(reader.Compiled_Program_Count(), 2u);
	reader.Shutdown();

	std::vector<unsigned char> bytes;
	FILE * file = fopen(path, "rb");
	CHECK(file != NULL);
	if (file != NULL) {
		fseek(file, 0, SEEK_END);
		bytes.resize(static_cast<size_t>(ftell(file)));
		fseek(file, 0, SEEK_SET);
		CHECK(fread(&bytes[0], bytes.size(), 1, file) == 1);
		fclose(file);
	}
	CHECK(bytes.size() > static_cast<size_t>(LAST_RECORD_CUT_BYTES));

	file = fopen(path, "wb");
	CHECK(file != NULL);
	if (file != NULL) {
		fwrite(&bytes[0], bytes.size() - LAST_RECORD_CUT_BYTES, 1, file);
		fclose(file);
	}

	DX11BackendClass cut_short;
	CHECK(cut_short.Initialise(&device));
	cut_short.Set_Shader_Cache_Path(path);
	CHECK_EQ(cut_short.Compiled_Program_Count(), 1u);
	cut_short.Shutdown();

	DeleteFileA(path);
}

// The alpha test, which on D3D11 is a clip inside the generated pixel shader rather than anything
// the pipeline does.  The vertices carry an alpha of 0xff, so a reference above it cuts the whole
// quad away and the target keeps the colour it was cleared to.  Get the comparison backwards and
// this passes at every reference, which is why the test is run at both ends.
TEST(dx11backend_the_alpha_test_cuts_the_pixels_the_comparison_rejects)
{
	DX11DeviceClass device;
	device.Request_Debug_Layer();
	CHECK(device.Create_Offscreen());
	ID3D11Device * d3d = device.Get_Device();

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));

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

	ID3D11Texture2D * target = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&target_description, NULL, &target)));

	D3D11_TEXTURE2D_DESC staging_description = target_description;
	staging_description.Usage = D3D11_USAGE_STAGING;
	staging_description.BindFlags = 0;
	staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	ID3D11Texture2D * staging = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&staging_description, NULL, &staging)));

	ID3D11RenderTargetView * target_view = NULL;
	CHECK(SUCCEEDED(d3d->CreateRenderTargetView(target, NULL, &target_view)));

	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(d3d, sizeof(QUAD_VERTICES), D3DPOOL_MANAGED, 0,
		QUAD_VERTICES, &vertices));

	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(d3d, sizeof(QUAD_INDICES), D3DPOOL_MANAGED, 0,
		QUAD_INDICES, &indices));

	backend.Set_Viewport(0, 0, TARGET_SIZE, TARGET_SIZE);
	configure_unlit_pass_through(backend);
	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);

	// The vertices carry an opaque alpha, so half is a reference it is above.  The two comparisons
	// below therefore disagree about every pixel in the quad, which is what makes this a pair
	// rather than one measurement: a clip written the wrong way round passes one of them.
	backend.Set_Render_State(D3DRS_ALPHATESTENABLE, TRUE);
	backend.Set_Render_State(D3DRS_ALPHAREF, 0x80);

	backend.Set_Render_State(D3DRS_ALPHAFUNC, D3DCMP_LESS);

	const float clear_colour[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
	device.Get_Context()->ClearRenderTargetView(target_view, clear_colour);
	device.Get_Context()->OMSetRenderTargets(1, &target_view, NULL);
	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));

	device.Get_Context()->CopyResource(staging, target);
	D3D11_MAPPED_SUBRESOURCE mapped;
	CHECK(SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));
	const unsigned char * cut = static_cast<const unsigned char *>(mapped.pData)
		+ mapped.RowPitch * (TARGET_SIZE / 2) + (TARGET_SIZE / 2) * 4;
	CHECK_EQ(cut[1], 0xff);
	CHECK_EQ(cut[0], 0x00);
	device.Get_Context()->Unmap(staging, 0);

	// The same reference, the opposite comparison: now every pixel passes.
	device.Get_Context()->ClearRenderTargetView(target_view, clear_colour);
	backend.Set_Render_State(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));

	device.Get_Context()->CopyResource(staging, target);
	CHECK(SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));
	const unsigned char * kept = static_cast<const unsigned char *>(mapped.pData)
		+ mapped.RowPitch * (TARGET_SIZE / 2) + (TARGET_SIZE / 2) * 4;
	CHECK_EQ(kept[0], EXPECTED_BLUE);
	CHECK_EQ(kept[2], EXPECTED_RED);
	device.Get_Context()->Unmap(staging, 0);

	// The comparison is part of the program and the reference is not, so two comparisons are two
	// pipelines and the reference could have changed between them for free.
	unsigned pipelines = 0;
	unsigned long long made = 0;
	unsigned long long refused = 0;
	backend.Statistics(pipelines, made, refused);
	CHECK_EQ(pipelines, 2u);
	CHECK_EQ(made, 2ull);

	indices->Release();
	vertices->Release();
	target_view->Release();
	staging->Release();
	target->Release();
	backend.Shutdown();
}

// A draw with no stream bound is refused rather than attempted.  D3D9 would have drawn nothing and
// said nothing; a count that goes up is the difference between a missing pass and a silent one.
TEST(dx11backend_a_draw_with_nothing_bound_is_counted_as_refused)
{
	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));
	configure_unlit_pass_through(backend);

	CHECK(!backend.Draw_Indexed_Triangles(6, 0, 0));

	unsigned pipelines = 0;
	unsigned long long made = 0;
	unsigned long long refused = 0;
	backend.Statistics(pipelines, made, refused);
	CHECK_EQ(made, 0ull);
	CHECK_EQ(refused, 1ull);

	backend.Shutdown();
}
