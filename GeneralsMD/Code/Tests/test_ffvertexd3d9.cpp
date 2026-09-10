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

// The generated vertex program against the fixed-function pipeline it replaces, on one Direct3D 9
// device, one draw each, comparing the pixels that come out.
//
// This is the vertex half of what -ffshader measures for the pixel half, and it exists because
// nothing else can tell a generator that follows the documented formula from a device that does
// something else.  Reading the two against each other proves only that the code matches the book.
//
// The frame the D3D11 backend draws is still a few percent from the Direct3D 9 one, and fitting a
// line through the two frames channel by channel puts the whole of that difference on lit meshes:
// terrain comes back at 0.954 of Direct3D 9's value, the command bar at exactly 1.000, and the
// buildings at 0.825 with a lift of nearly six levels at black.  Everything a picture can say about
// that has been said.  These tests are the next instrument.
//
// A machine with no Direct3D 9 device passes them by saying so; that is a test host without a GPU,
// not a failure of the generator.

#include "test_harness.h"

#include "ffvertex.h"
#include "test_d3d9device.h"

#include <d3d9.h>
#include <d3dcommon.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef HRESULT (WINAPI *D3DCompileFunction)(LPCVOID source_data, SIZE_T source_size,
	LPCSTR source_name, const D3D_SHADER_MACRO * defines, ID3DInclude * include,
	LPCSTR entry_point, LPCSTR target, UINT flags1, UINT flags2, ID3DBlob ** code,
	ID3DBlob ** error_messages);

static const char * const COMPILER_MODULE = "d3dcompiler_47.dll";
static const char * const ENTRY_POINT = "main";
static const char * const D3D9_PROFILE = "vs_3_0";
static const unsigned TARGET_SIZE = 64;

// Two paths that agree to within a level are the same arithmetic quantised twice.  The fixed
// function runs wherever the driver runs it and the generated program runs in the software vertex
// processor, so the last bit of a colour is not a claim either of them makes.
static const int LEVEL_TOLERANCE = 2;

// Which of a light's six registers ffvertex reads for a directional one.  The order it declares
// them in is position, direction, diffuse, specular, attenuation, spot.
static const unsigned LIGHT_DIRECTION = 1;
static const unsigned LIGHT_DIFFUSE = 2;

// Nothing here turns the specular on, so the exponent only has to be a value both paths agree is
// not zero: D3D9 refuses a material power of zero on some drivers and the shader raises to it.
static const float MATERIAL_POWER = 1.0f;

// One quad already in clip space, so the world, view and projection matrices are free to be
// anything the test wants without moving it off the screen.
struct LitVertex
{
	float Position[3];
	float Normal[3];
	float TexCoord[2];
};

static const LitVertex QUAD[4] = {
	{ { -1.0f, -1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
	{ { -1.0f,  1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
	{ {  1.0f,  1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
	{ {  1.0f, -1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } }
};

static const DWORD QUAD_FVF = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1;

// Everything both draws share: no texture, the vertex colour straight through, no depth test.
static void configure_common(IDirect3DDevice9 * device)
{
	device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	device->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
	device->SetRenderState(D3DRS_COLORVERTEX, FALSE);
	device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

static void set_material(IDirect3DDevice9 * device, const float ambient[4], const float diffuse[4],
	const float emissive[4], float power)
{
	D3DMATERIAL9 material;
	memset(&material, 0, sizeof(material));
	memcpy(&material.Ambient, ambient, sizeof(float) * 4);
	memcpy(&material.Diffuse, diffuse, sizeof(float) * 4);
	memcpy(&material.Emissive, emissive, sizeof(float) * 4);
	material.Power = power;
	device->SetMaterial(&material);
}

static D3DCompileFunction load_compiler()
{
	HMODULE module = LoadLibraryA(COMPILER_MODULE);
	if (module == NULL) {
		return NULL;
	}
	return reinterpret_cast<D3DCompileFunction>(GetProcAddress(module, "D3DCompile"));
}

// A matrix goes into the register file exactly as D3D9 stores it, four registers a row, because the
// generated program declares every matrix row_major and multiplies a row vector by it.
static void set_matrix(IDirect3DDevice9 * device, unsigned first_register, const D3DMATRIX & source)
{
	device->SetVertexShaderConstantF(first_register, reinterpret_cast<const float *>(&source),
		VERTEX_REGISTERS_PER_MATRIX);
}

static void multiply(const D3DMATRIX & left, const D3DMATRIX & right, D3DMATRIX & result)
{
	const float * a = reinterpret_cast<const float *>(&left);
	const float * b = reinterpret_cast<const float *>(&right);
	float * out = reinterpret_cast<float *>(&result);
	for (unsigned row = 0; row < 4; ++row) {
		for (unsigned column = 0; column < 4; ++column) {
			float sum = 0.0f;
			for (unsigned term = 0; term < 4; ++term) {
				sum += a[row * 4 + term] * b[term * 4 + column];
			}
			out[row * 4 + column] = sum;
		}
	}
}

// D3D9's row-vector rotation about x.  Its inverse is the same rotation by the negative angle, which
// is how the test builds a world and a view that cancel: the geometry lands where it started while
// every normal and every light direction still goes the long way round.
static void set_rotation_x(D3DMATRIX & matrix, float radians)
{
	D3D9Test_Set_Identity(matrix);
	matrix._22 = cosf(radians);
	matrix._23 = sinf(radians);
	matrix._32 = -sinf(radians);
	matrix._33 = cosf(radians);
}

// The direction half of what DX11BackendClass::Upload_Constants does to a light before it reaches
// the shader: D3D9 carries a light through the view matrix once per light and lights in camera
// space, so a direction left in world space lights the wrong side of everything.
static void transform_direction(const float source[3], const D3DMATRIX & matrix, float result[4])
{
	const float * m = reinterpret_cast<const float *>(&matrix);
	for (unsigned column = 0; column < 4; ++column) {
		result[column] = source[0] * m[column] + source[1] * m[4 + column]
			+ source[2] * m[8 + column];
	}
}

// A directional light as the map files describe one: the way the light travels, in world space,
// with a colour.  Every light in the dumped building pipeline is one of these.
struct DirectionalLight
{
	float Direction[3];
	float Diffuse[4];
};

// Everything both draws are given.  The world and the view are kept orthonormal on purpose, so the
// normal transform is the world-view matrix itself and the test is about the lighting rather than
// about who inverts a matrix.
struct LitScene
{
	D3DMATRIX World;
	D3DMATRIX View;
	float MaterialAmbient[4];
	float MaterialDiffuse[4];
	float MaterialEmissive[4];
	float GlobalAmbient[4];
	unsigned LightCount;
	DirectionalLight Lights[MAXIMUM_VERTEX_LIGHTS];
};

static const unsigned COLOUR_LEVELS = 255;

// The scene ambient reaches the fixed function as a packed colour and the shader as four floats, so
// the two only agree if the floats are levels to begin with.  Every global ambient in this file is.
static DWORD to_packed_colour(const float colour[4])
{
	return D3DCOLOR_ARGB(
		static_cast<unsigned>(colour[3] * COLOUR_LEVELS + 0.5f),
		static_cast<unsigned>(colour[0] * COLOUR_LEVELS + 0.5f),
		static_cast<unsigned>(colour[1] * COLOUR_LEVELS + 0.5f),
		static_cast<unsigned>(colour[2] * COLOUR_LEVELS + 0.5f));
}

static VertexPipelineDescription describe(const LitScene & scene)
{
	VertexPipelineDescription description;
	memset(&description, 0, sizeof(description));
	description.FVF = QUAD_FVF;
	description.LightingEnabled = true;
	description.SpecularEnabled = false;
	description.ColourVertexEnabled = false;
	description.DiffuseMaterialSource = D3DMCS_MATERIAL;
	description.AmbientMaterialSource = D3DMCS_MATERIAL;
	description.EmissiveMaterialSource = D3DMCS_MATERIAL;
	description.SpecularMaterialSource = D3DMCS_MATERIAL;
	description.LightCount = scene.LightCount;
	for (unsigned index = 0; index < scene.LightCount; ++index) {
		description.Lights[index].Type = D3DLIGHT_DIRECTIONAL;
	}
	description.StageCount = 1;
	description.Stages[0].TextureCoordinateIndex = 0;
	description.Stages[0].TextureTransformFlags = D3DTTFF_DISABLE;
	description.FogEnabled = false;
	return description;
}

static void clear_target(IDirect3DDevice9 * device)
{
	device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 255, 0), 1.0f, 0);
	configure_common(device);
}

static bool draw_quad(IDirect3DDevice9 * device)
{
	if (FAILED(device->BeginScene())) {
		return false;
	}
	const HRESULT drawn = device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, QUAD, sizeof(LitVertex));
	device->EndScene();
	return SUCCEEDED(drawn);
}

static bool draw_fixed_function(D3D9OffscreenDevice & fixture, const LitScene & scene,
	unsigned char colour[3])
{
	IDirect3DDevice9 * device = fixture.Get_Device();
	D3DMATRIX identity;
	D3D9Test_Set_Identity(identity);

	clear_target(device);
	device->SetTransform(D3DTS_WORLD, &scene.World);
	device->SetTransform(D3DTS_VIEW, &scene.View);
	device->SetTransform(D3DTS_PROJECTION, &identity);
	device->SetRenderState(D3DRS_LIGHTING, TRUE);
	device->SetRenderState(D3DRS_AMBIENT, to_packed_colour(scene.GlobalAmbient));
	device->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
	device->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
	device->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
	set_material(device, scene.MaterialAmbient, scene.MaterialDiffuse, scene.MaterialEmissive,
		MATERIAL_POWER);

	for (unsigned index = 0; index < MAXIMUM_VERTEX_LIGHTS; ++index) {
		if (index >= scene.LightCount) {
			device->LightEnable(index, FALSE);
			continue;
		}
		D3DLIGHT9 light;
		memset(&light, 0, sizeof(light));
		light.Type = D3DLIGHT_DIRECTIONAL;
		memcpy(&light.Diffuse, scene.Lights[index].Diffuse, sizeof(float) * 4);
		light.Direction.x = scene.Lights[index].Direction[0];
		light.Direction.y = scene.Lights[index].Direction[1];
		light.Direction.z = scene.Lights[index].Direction[2];
		device->SetLight(index, &light);
		device->LightEnable(index, TRUE);
	}

	device->SetVertexShader(NULL);
	device->SetFVF(QUAD_FVF);
	if (!draw_quad(device)) {
		return false;
	}
	return fixture.Read_Pixel(TARGET_SIZE / 2, TARGET_SIZE / 2, colour);
}

// Compiles the generated program and hands back the shader, or NULL with the compiler's own words
// printed.  The caller owns what comes back.
static IDirect3DVertexShader9 * build_shader(IDirect3DDevice9 * device,
	D3DCompileFunction compile, const std::string & hlsl)
{
	ID3DBlob * code = NULL;
	ID3DBlob * errors = NULL;
	const HRESULT compiled = compile(hlsl.c_str(), hlsl.size(), "ffvertex", NULL, NULL,
		ENTRY_POINT, D3D9_PROFILE, 0, 0, &code, &errors);
	if (errors != NULL) {
		printf("  %s\n", static_cast<const char *>(errors->GetBufferPointer()));
		errors->Release();
	}
	if (FAILED(compiled) || code == NULL) {
		return NULL;
	}

	IDirect3DVertexShader9 * shader = NULL;
	const HRESULT created = device->CreateVertexShader(
		static_cast<const DWORD *>(code->GetBufferPointer()), &shader);
	code->Release();
	return SUCCEEDED(created) ? shader : NULL;
}

static bool draw_generated(D3D9OffscreenDevice & fixture, D3DCompileFunction compile,
	const LitScene & scene, unsigned char colour[3])
{
	IDirect3DDevice9 * device = fixture.Get_Device();

	std::string hlsl;
	if (!VertexShader_Generate(describe(scene), VERTEX_SHADER_TARGET_D3D9, hlsl)) {
		return false;
	}
	IDirect3DVertexShader9 * shader = build_shader(device, compile, hlsl);
	if (shader == NULL) {
		return false;
	}

	clear_target(device);

	D3DMATRIX world_view;
	multiply(scene.World, scene.View, world_view);
	set_matrix(device, VERTEX_REGISTER_WORLD_VIEW_PROJECTION, world_view);
	set_matrix(device, VERTEX_REGISTER_WORLD_VIEW, world_view);
	set_matrix(device, VERTEX_REGISTER_NORMAL_TRANSFORM, world_view);

	device->SetVertexShaderConstantF(VERTEX_REGISTER_MATERIAL_AMBIENT, scene.MaterialAmbient, 1);
	device->SetVertexShaderConstantF(VERTEX_REGISTER_MATERIAL_DIFFUSE, scene.MaterialDiffuse, 1);
	device->SetVertexShaderConstantF(VERTEX_REGISTER_MATERIAL_EMISSIVE, scene.MaterialEmissive, 1);
	const float power[4] = { MATERIAL_POWER, 0.0f, 0.0f, 0.0f };
	device->SetVertexShaderConstantF(VERTEX_REGISTER_MATERIAL_POWER, power, 1);
	device->SetVertexShaderConstantF(VERTEX_REGISTER_GLOBAL_AMBIENT, scene.GlobalAmbient, 1);
	const float no_fog[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	device->SetVertexShaderConstantF(VERTEX_REGISTER_FOG_PARAMETERS, no_fog, 1);

	for (unsigned index = 0; index < scene.LightCount; ++index) {
		float fields[VERTEX_REGISTERS_PER_LIGHT][4];
		memset(fields, 0, sizeof(fields));
		transform_direction(scene.Lights[index].Direction, scene.View, fields[LIGHT_DIRECTION]);
		memcpy(fields[LIGHT_DIFFUSE], scene.Lights[index].Diffuse, sizeof(float) * 4);
		device->SetVertexShaderConstantF(
			VERTEX_REGISTER_LIGHTS + index * VERTEX_REGISTERS_PER_LIGHT,
			&fields[0][0], VERTEX_REGISTERS_PER_LIGHT);
	}

	device->SetFVF(QUAD_FVF);
	device->SetVertexShader(shader);
	const bool drawn = draw_quad(device);
	device->SetVertexShader(NULL);
	shader->Release();

	return drawn && fixture.Read_Pixel(TARGET_SIZE / 2, TARGET_SIZE / 2, colour);
}

// Runs one scene both ways and reports what came back.  A machine with no device or no compiler
// says so and the test passes; that is a test host without a GPU, not a broken generator.
static void compare(const LitScene & scene)
{
	D3D9OffscreenDevice fixture;
	if (!fixture.Create(TARGET_SIZE)) {
		printf("  no Direct3D 9 device on this machine - skipped\n");
		return;
	}
	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  no %s on this machine - skipped\n", COMPILER_MODULE);
		return;
	}

	unsigned char fixed_function[3] = { 0, 0, 0 };
	unsigned char generated[3] = { 0, 0, 0 };
	CHECK(draw_fixed_function(fixture, scene, fixed_function));
	CHECK(draw_generated(fixture, compile, scene, generated));

	printf("  fixed function bgr %u,%u,%u   generated bgr %u,%u,%u\n",
		fixed_function[0], fixed_function[1], fixed_function[2],
		generated[0], generated[1], generated[2]);

	for (unsigned channel = 0; channel < 3; ++channel) {
		const int difference = static_cast<int>(generated[channel])
			- static_cast<int>(fixed_function[channel]);
		CHECK(difference >= -LEVEL_TOLERANCE && difference <= LEVEL_TOLERANCE);
	}
}

static LitScene plain_scene()
{
	LitScene scene;
	memset(&scene, 0, sizeof(scene));
	D3D9Test_Set_Identity(scene.World);
	D3D9Test_Set_Identity(scene.View);

	// Every value differs from every other one, so a channel read in the wrong order comes back as a
	// wrong answer rather than a lucky one.
	const float material_ambient[4] = { 0.4f, 0.3f, 0.2f, 1.0f };
	const float material_diffuse[4] = { 0.9f, 0.8f, 0.7f, 1.0f };
	const float material_emissive[4] = { 0.05f, 0.05f, 0.05f, 0.0f };
	const float global_ambient[4] = { 60.0f / COLOUR_LEVELS, 40.0f / COLOUR_LEVELS,
		20.0f / COLOUR_LEVELS, 1.0f };
	memcpy(scene.MaterialAmbient, material_ambient, sizeof(material_ambient));
	memcpy(scene.MaterialDiffuse, material_diffuse, sizeof(material_diffuse));
	memcpy(scene.MaterialEmissive, material_emissive, sizeof(material_emissive));
	memcpy(scene.GlobalAmbient, global_ambient, sizeof(global_ambient));
	return scene;
}

TEST(ffvertexd3d9_the_generated_program_lights_a_quad_like_the_fixed_function_does)
{
	LitScene scene = plain_scene();

	// One light coming at the quad from straight in front, so the whole face is lit and the answer
	// is a flat colour rather than a gradient.
	scene.LightCount = 1;
	scene.Lights[0].Direction[2] = -1.0f;
	const float diffuse[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
	memcpy(scene.Lights[0].Diffuse, diffuse, sizeof(diffuse));

	compare(scene);
}

// The building pipeline the D3D11 backend dumps out of a real match, drawn twice.  Its key is
// 274:100:0,0,0,0:L3:L3:L3 - the position, normal and one coordinate set that FVF 274 is, lighting
// on with the specular and the vertex colour off, all four material sources reading the material,
// and three directional lights.  Nothing else in the frame is shaded by more of the pipeline than
// this, and the buildings are where the residual sits: fitting a line through the two frames puts
// terrain at 0.954 of Direct3D 9's value and models at 0.825 with a lift of nearly six levels.
//
// The world and the view here cancel, so the quad is where it was, but every normal goes through
// the pair and every light direction goes through the view alone.  A light left in world space
// lights the wrong side of this.
TEST(ffvertexd3d9_the_building_pipeline_lights_a_quad_like_the_fixed_function_does)
{
	const float ROTATION = 0.7f;

	LitScene scene = plain_scene();
	set_rotation_x(scene.World, ROTATION);
	set_rotation_x(scene.View, -ROTATION);

	scene.LightCount = 3;

	// A key light down and forward, a fill from the left, and a rim from behind and below, which is
	// as close to three unlike directions as one quad can be given.
	const float key_direction[3] = { 0.0f, -0.6f, -0.8f };
	const float key_diffuse[4] = { 0.6f, 0.55f, 0.5f, 1.0f };
	memcpy(scene.Lights[0].Direction, key_direction, sizeof(key_direction));
	memcpy(scene.Lights[0].Diffuse, key_diffuse, sizeof(key_diffuse));

	const float fill_direction[3] = { 0.8f, 0.0f, -0.6f };
	const float fill_diffuse[4] = { 0.2f, 0.25f, 0.3f, 1.0f };
	memcpy(scene.Lights[1].Direction, fill_direction, sizeof(fill_direction));
	memcpy(scene.Lights[1].Diffuse, fill_diffuse, sizeof(fill_diffuse));

	const float rim_direction[3] = { -0.5f, 0.5f, 0.707f };
	const float rim_diffuse[4] = { 0.15f, 0.1f, 0.05f, 1.0f };
	memcpy(scene.Lights[2].Direction, rim_direction, sizeof(rim_direction));
	memcpy(scene.Lights[2].Diffuse, rim_diffuse, sizeof(rim_diffuse));

	compare(scene);
}

// The coordinate a stage generates for itself, read back as a colour.
//
// Nineteen of the sixty-five texture stages a Flash Effect frame sets do not read a coordinate out
// of the vertex at all: they ask for the camera space position, or the camera space normal, and put
// it through a texture matrix.  That is the scrolling noise the terrain and the models are
// multiplied by, and until now nothing checked it - the lighting tests above use one pass-through
// stage, and a wrong coordinate there would look like a texture, not like an error.
//
// The probe is a strip 256 texels wide whose red channel is its own index, sampled with a point
// filter and clamped. Whatever coordinate reaches the sampler comes back as a number: red 89 means
// u landed in the 90th texel and nowhere else. The matrix maps the camera space z to u, so both
// paths are asked the same question about the same axis.
static const unsigned PROBE_WIDTH = 256;

// u = 0.5 * z + 0.1 written for D3D9's row vector convention, so the third row scales z into u and
// the fourth row carries the offset.  With z at 0.5 across the quad, u is 0.35 and the texel is 89.
static void set_probe_matrix(D3DMATRIX & matrix)
{
	memset(&matrix, 0, sizeof(matrix));
	matrix._31 = 0.5f;
	matrix._41 = 0.1f;
	matrix._42 = 0.25f;
	matrix._44 = 1.0f;
}

static IDirect3DTexture9 * make_probe(IDirect3DDevice9 * device)
{
	IDirect3DTexture9 * texture = NULL;
	if (FAILED(device->CreateTexture(PROBE_WIDTH, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
			&texture, NULL))) {
		return NULL;
	}
	D3DLOCKED_RECT locked;
	if (FAILED(texture->LockRect(0, &locked, NULL, 0))) {
		texture->Release();
		return NULL;
	}
	unsigned char * row = static_cast<unsigned char *>(locked.pBits);
	for (unsigned texel = 0; texel < PROBE_WIDTH; ++texel) {
		row[texel * 4 + 0] = 64;
		row[texel * 4 + 1] = static_cast<unsigned char>(255 - texel);
		row[texel * 4 + 2] = static_cast<unsigned char>(texel);
		row[texel * 4 + 3] = 255;
	}
	texture->UnlockRect(0);
	return texture;
}

// Everything the probe shares with itself on both paths: the texture selected rather than the
// vertex colour, no filtering to blur which texel was chosen, and no lighting to colour it.
static void configure_probe(IDirect3DDevice9 * device)
{
	device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	device->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
	device->SetRenderState(D3DRS_COLORVERTEX, FALSE);
	device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	device->SetRenderState(D3DRS_LIGHTING, FALSE);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
}

static VertexPipelineDescription describe_probe(DWORD generation)
{
	VertexPipelineDescription description;
	memset(&description, 0, sizeof(description));
	description.FVF = QUAD_FVF;
	description.LightingEnabled = false;
	description.SpecularEnabled = false;
	description.ColourVertexEnabled = false;
	description.DiffuseMaterialSource = D3DMCS_MATERIAL;
	description.AmbientMaterialSource = D3DMCS_MATERIAL;
	description.EmissiveMaterialSource = D3DMCS_MATERIAL;
	description.SpecularMaterialSource = D3DMCS_MATERIAL;
	description.LightCount = 0;
	description.StageCount = 1;
	description.Stages[0].TextureCoordinateIndex = generation;
	description.Stages[0].TextureTransformFlags = D3DTTFF_COUNT2;
	description.FogEnabled = false;
	return description;
}

static void compare_probe(DWORD generation, const char * what)
{
	D3D9OffscreenDevice fixture;
	if (!fixture.Create(TARGET_SIZE)) {
		printf("  no Direct3D 9 device on this machine - skipped\n");
		return;
	}
	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  no %s on this machine - skipped\n", COMPILER_MODULE);
		return;
	}
	IDirect3DDevice9 * device = fixture.Get_Device();

	IDirect3DTexture9 * probe = make_probe(device);
	CHECK(probe != NULL);
	if (probe == NULL) {
		return;
	}

	D3DMATRIX identity;
	D3D9Test_Set_Identity(identity);
	D3DMATRIX texture_matrix;
	set_probe_matrix(texture_matrix);

	clear_target(device);
	configure_probe(device);
	device->SetTexture(0, probe);
	device->SetTransform(D3DTS_WORLD, &identity);
	device->SetTransform(D3DTS_VIEW, &identity);
	device->SetTransform(D3DTS_PROJECTION, &identity);
	device->SetTransform(D3DTS_TEXTURE0, &texture_matrix);
	device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, generation);
	device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
	device->SetVertexShader(NULL);
	device->SetFVF(QUAD_FVF);
	CHECK(draw_quad(device));

	unsigned char fixed_function[3] = { 0, 0, 0 };
	CHECK(fixture.Read_Pixel(TARGET_SIZE / 2, TARGET_SIZE / 2, fixed_function));

	std::string hlsl;
	CHECK(VertexShader_Generate(describe_probe(generation), VERTEX_SHADER_TARGET_D3D9, hlsl));
	IDirect3DVertexShader9 * shader = build_shader(device, compile, hlsl);
	CHECK(shader != NULL);
	if (shader == NULL) {
		probe->Release();
		return;
	}

	clear_target(device);
	configure_probe(device);
	device->SetTexture(0, probe);
	set_matrix(device, VERTEX_REGISTER_WORLD_VIEW_PROJECTION, identity);
	set_matrix(device, VERTEX_REGISTER_WORLD_VIEW, identity);
	set_matrix(device, VERTEX_REGISTER_NORMAL_TRANSFORM, identity);
	set_matrix(device, VERTEX_REGISTER_TEXTURE_MATRICES, texture_matrix);
	device->SetFVF(QUAD_FVF);
	device->SetVertexShader(shader);
	CHECK(draw_quad(device));
	device->SetVertexShader(NULL);
	shader->Release();

	unsigned char generated[3] = { 0, 0, 0 };
	CHECK(fixture.Read_Pixel(TARGET_SIZE / 2, TARGET_SIZE / 2, generated));
	probe->Release();

	printf("  %s: fixed function texel %u, generated texel %u\n",
		what, fixed_function[2], generated[2]);

	const int difference = static_cast<int>(generated[2]) - static_cast<int>(fixed_function[2]);
	CHECK(difference >= -1 && difference <= 1);
}

TEST(ffvertexd3d9_a_camera_space_coordinate_goes_through_the_texture_matrix_the_same_way)
{
	compare_probe(D3DTSS_TCI_CAMERASPACEPOSITION, "camera space position");
}

TEST(ffvertexd3d9_a_camera_space_normal_goes_through_the_texture_matrix_the_same_way)
{
	compare_probe(D3DTSS_TCI_CAMERASPACENORMAL, "camera space normal");
}
