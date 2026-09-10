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

// The whole translation, drawn.
//
// Every other test in this set checks one piece against what it should say: the state block against
// the three descriptions, the layout against the offsets, the generators against the text they
// write.  All of them can pass while the pieces refuse to work together, because the thing that
// joins them is D3D11's own validation - an input layout has to match the vertex shader's input
// signature, a pixel shader's inputs have to be a subset of what the vertex shader wrote, and none
// of that is visible in any single one of the descriptions.
//
// So this builds a device, generates both shaders from descriptions the game produces, builds the
// input layout out of the flexible vertex format the dynamic buffers use, builds the three state
// objects out of the render states, draws a quad through the lot and reads the pixel back.  The
// colour that comes out is the vertex colour that went in, which is what an unlit pass-through
// draw means.

#include "test_harness.h"

#include "dx11device.h"
#include "dx11layout.h"
#include "dx11state.h"
#include "ffshader.h"
#include "ffvertex.h"

#include <d3dcommon.h>
#include <d3d11sdklayers.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

typedef HRESULT (WINAPI *D3DCompileFunction)(LPCVOID source_data, SIZE_T source_size,
	LPCSTR source_name, const D3D_SHADER_MACRO * defines, ID3DInclude * include,
	LPCSTR entry_point, LPCSTR target, UINT flags1, UINT flags2, ID3DBlob ** code,
	ID3DBlob ** error_messages);

static const char * const COMPILER_MODULE = "d3dcompiler_47.dll";
static const char * const ENTRY_POINT = "main";
static const char * const VERTEX_PROFILE = "vs_4_0";
static const char * const PIXEL_PROFILE = "ps_4_0";

static const unsigned TARGET_WIDTH = 64;
static const unsigned TARGET_HEIGHT = 64;

// Packed the way a D3DCOLOR is: alpha, red, green, blue, most significant byte first.  All four
// bytes differ so that a channel read in the wrong order is a wrong answer rather than a lucky one,
// which a symmetric colour would hide.
static const DWORD VERTEX_COLOUR = 0xff112233;
static const unsigned char EXPECTED_BLUE = 0x33;
static const unsigned char EXPECTED_GREEN = 0x22;
static const unsigned char EXPECTED_RED = 0x11;
static const unsigned char EXPECTED_ALPHA = 0xff;

static const DWORD PIPELINE_FVF = D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX2|D3DFVF_DIFFUSE;

// The constant buffer ffvertex declares for a description with no lights: five matrices and seven
// float4s, in that order.
struct VertexConstants
{
	float WorldViewProjection[16];
	float WorldView[16];
	float NormalTransform[16];
	float TextureMatrix0[16];
	float TextureMatrix1[16];
	float MaterialAmbient[4];
	float MaterialDiffuse[4];
	float MaterialSpecular[4];
	float MaterialEmissive[4];
	float MaterialPower[4];
	float GlobalAmbient[4];
	float FogParameters[4];
};

struct PixelConstants
{
	float TextureFactor[4];
};

// One vertex of DX8_FVF_XYZNDUV2, which is the format every dynamic buffer in the game is filled
// through, laid out here exactly as dx11layout says it is.
struct PipelineVertex
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

// What the validation layer has to say, if it is there.  Its messages go to the debugger by
// default, which a test run from a script never sees, so they are pulled out of the queue and
// printed.  Silence here means the layer found nothing, not that it was not asked.
static void print_validation_messages(ID3D11Device * device)
{
	ID3D11InfoQueue * queue = NULL;
	if (FAILED(device->QueryInterface(__uuidof(ID3D11InfoQueue),
			reinterpret_cast<void **>(&queue)))) {
		return;
	}

	const UINT64 count = queue->GetNumStoredMessages();
	for (UINT64 index = 0; index < count; ++index) {
		SIZE_T length = 0;
		queue->GetMessage(index, NULL, &length);

		D3D11_MESSAGE * message = static_cast<D3D11_MESSAGE *>(malloc(length));
		if (SUCCEEDED(queue->GetMessage(index, message, &length))) {
			printf("  validation: %s\n", message->pDescription);
		}
		free(message);
	}
	queue->Release();
}

static D3DCompileFunction load_compiler()
{
	HMODULE module = LoadLibraryA(COMPILER_MODULE);
	if (module == NULL) {
		return NULL;
	}
	return reinterpret_cast<D3DCompileFunction>(GetProcAddress(module, "D3DCompile"));
}

static ID3DBlob * compile(D3DCompileFunction compiler, const std::string & hlsl,
	const char * profile)
{
	ID3DBlob * code = NULL;
	ID3DBlob * errors = NULL;
	const HRESULT result = compiler(hlsl.c_str(), hlsl.size(), "pipeline", NULL, NULL,
		ENTRY_POINT, profile, 0, 0, &code, &errors);

	if (FAILED(result) && errors != NULL) {
		printf("  %s: %s\n", profile, static_cast<const char *>(errors->GetBufferPointer()));
	}
	if (errors != NULL) {
		errors->Release();
	}
	return SUCCEEDED(result) ? code : NULL;
}

// An unlit draw with one pass-through coordinate set: the shape most of the game's draws are.
static VertexPipelineDescription unlit_vertex_description()
{
	VertexPipelineDescription description;
	memset(&description, 0, sizeof(description));
	description.FVF = PIPELINE_FVF;
	description.ColourVertexEnabled = true;
	description.DiffuseMaterialSource = D3DMCS_COLOR1;
	description.AmbientMaterialSource = D3DMCS_MATERIAL;
	description.EmissiveMaterialSource = D3DMCS_MATERIAL;
	description.SpecularMaterialSource = D3DMCS_MATERIAL;
	description.StageCount = 1;
	description.Stages[0].TextureCoordinateIndex = D3DTSS_TCI_PASSTHRU;
	description.Stages[0].TextureTransformFlags = D3DTTFF_DISABLE;
	description.FogVertexMode = D3DFOG_NONE;
	return description;
}

// SELECTARG1 on the diffuse colour with nothing bound: the combiner that hands the vertex colour
// straight through, so what lands in the render target is what was put in the vertex.
static CombinerDescription diffuse_combiner_description()
{
	CombinerDescription description;
	memset(&description, 0, sizeof(description));
	description.StageCount = 1;
	description.Stages[0].ColourOperation = D3DTOP_SELECTARG1;
	description.Stages[0].ColourArgument1 = D3DTA_DIFFUSE;
	description.Stages[0].ColourArgument2 = D3DTA_CURRENT;
	description.Stages[0].AlphaOperation = D3DTOP_SELECTARG1;
	description.Stages[0].AlphaArgument1 = D3DTA_DIFFUSE;
	description.Stages[0].AlphaArgument2 = D3DTA_CURRENT;
	description.Stages[0].TextureCoordinateIndex = 0;
	description.Stages[0].TextureBound = false;
	return description;
}

TEST(dx11pipeline_a_generated_program_draws_the_vertex_colour_it_was_given)
{
	D3DCompileFunction compiler = load_compiler();
	if (compiler == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping\n");
		return;
	}

	DX11DeviceClass device;
	device.Request_Debug_Layer();
	CHECK(device.Create_Offscreen());
	ID3D11Device * d3d = device.Get_Device();
	ID3D11DeviceContext * context = device.Get_Context();

	// The target to draw into, and the staging copy the result is read out of.  A default-usage
	// texture cannot be mapped, which is why there are two.
	D3D11_TEXTURE2D_DESC target_description;
	memset(&target_description, 0, sizeof(target_description));
	target_description.Width = TARGET_WIDTH;
	target_description.Height = TARGET_HEIGHT;
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

	std::string vertex_hlsl;
	std::string pixel_hlsl;
	CHECK(VertexShader_Generate(unlit_vertex_description(), VERTEX_SHADER_TARGET_D3D11, vertex_hlsl));
	CHECK(CombinerShader_Generate(diffuse_combiner_description(), COMBINER_SHADER_TARGET_D3D11,
		pixel_hlsl));

	ID3DBlob * vertex_code = compile(compiler, vertex_hlsl, VERTEX_PROFILE);
	ID3DBlob * pixel_code = compile(compiler, pixel_hlsl, PIXEL_PROFILE);
	CHECK(vertex_code != NULL);
	CHECK(pixel_code != NULL);

	ID3D11VertexShader * vertex_shader = NULL;
	ID3D11PixelShader * pixel_shader = NULL;
	CHECK(SUCCEEDED(d3d->CreateVertexShader(vertex_code->GetBufferPointer(),
		vertex_code->GetBufferSize(), NULL, &vertex_shader)));
	CHECK(SUCCEEDED(d3d->CreatePixelShader(pixel_code->GetBufferPointer(),
		pixel_code->GetBufferSize(), NULL, &pixel_shader)));

	// The layout has to match the vertex shader's input signature, and CreateInputLayout is where
	// D3D11 says so.  This is the one call that checks dx11layout against ffvertex.
	D3D11_INPUT_ELEMENT_DESC elements[MAXIMUM_LAYOUT_ELEMENTS];
	unsigned element_count = 0;
	unsigned stride = 0;
	CHECK(DX11Layout_From_FVF(PIPELINE_FVF, elements, element_count, stride));
	CHECK_EQ(stride, sizeof(PipelineVertex));

	ID3D11InputLayout * layout = NULL;
	CHECK(SUCCEEDED(d3d->CreateInputLayout(elements, element_count,
		vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), &layout)));

	// A quad covering the target, already in clip space, which the identity transforms below leave
	// where it is.
	const PipelineVertex vertices[4] = {
		{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
		{ { -1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
		{ {  1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
		{ {  1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } }
	};

	D3D11_BUFFER_DESC vertex_buffer_description;
	memset(&vertex_buffer_description, 0, sizeof(vertex_buffer_description));
	vertex_buffer_description.ByteWidth = sizeof(vertices);
	vertex_buffer_description.Usage = D3D11_USAGE_IMMUTABLE;
	vertex_buffer_description.BindFlags = D3D11_BIND_VERTEX_BUFFER;

	D3D11_SUBRESOURCE_DATA vertex_data;
	memset(&vertex_data, 0, sizeof(vertex_data));
	vertex_data.pSysMem = vertices;

	ID3D11Buffer * vertex_buffer = NULL;
	CHECK(SUCCEEDED(d3d->CreateBuffer(&vertex_buffer_description, &vertex_data, &vertex_buffer)));

	VertexConstants vertex_constants;
	memset(&vertex_constants, 0, sizeof(vertex_constants));
	set_identity(vertex_constants.WorldViewProjection);
	set_identity(vertex_constants.WorldView);
	set_identity(vertex_constants.NormalTransform);
	set_identity(vertex_constants.TextureMatrix0);
	set_identity(vertex_constants.TextureMatrix1);

	D3D11_BUFFER_DESC constant_buffer_description;
	memset(&constant_buffer_description, 0, sizeof(constant_buffer_description));
	constant_buffer_description.ByteWidth = sizeof(VertexConstants);
	constant_buffer_description.Usage = D3D11_USAGE_IMMUTABLE;
	constant_buffer_description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

	D3D11_SUBRESOURCE_DATA constant_data;
	memset(&constant_data, 0, sizeof(constant_data));
	constant_data.pSysMem = &vertex_constants;

	ID3D11Buffer * vertex_constant_buffer = NULL;
	CHECK(SUCCEEDED(d3d->CreateBuffer(&constant_buffer_description, &constant_data,
		&vertex_constant_buffer)));

	PixelConstants pixel_constants;
	memset(&pixel_constants, 0, sizeof(pixel_constants));

	constant_buffer_description.ByteWidth = sizeof(PixelConstants);
	constant_data.pSysMem = &pixel_constants;

	ID3D11Buffer * pixel_constant_buffer = NULL;
	CHECK(SUCCEEDED(d3d->CreateBuffer(&constant_buffer_description, &constant_data,
		&pixel_constant_buffer)));

	// The render states this draw would have set under D3D9, translated.  No depth buffer is bound,
	// so the depth test is off; the quad is drawn from both sides so its winding cannot decide the
	// answer.
	DX11StateBlockClass states;
	states.Set_Render_State(D3DRS_ZENABLE, D3DZB_FALSE);
	states.Set_Render_State(D3DRS_ZWRITEENABLE, FALSE);
	states.Set_Render_State(D3DRS_CULLMODE, D3DCULL_NONE);

	D3D11_BLEND_DESC blend_description;
	D3D11_DEPTH_STENCIL_DESC depth_description;
	D3D11_RASTERIZER_DESC rasterizer_description;
	states.Build_Blend_Description(blend_description);
	states.Build_Depth_Stencil_Description(depth_description);
	states.Build_Rasterizer_Description(rasterizer_description);

	ID3D11BlendState * blend_state = NULL;
	ID3D11DepthStencilState * depth_state = NULL;
	ID3D11RasterizerState * rasterizer_state = NULL;
	CHECK(SUCCEEDED(d3d->CreateBlendState(&blend_description, &blend_state)));
	CHECK(SUCCEEDED(d3d->CreateDepthStencilState(&depth_description, &depth_state)));
	CHECK(SUCCEEDED(d3d->CreateRasterizerState(&rasterizer_description, &rasterizer_state)));

	// Green, which the draw never produces, so a centre pixel that is still green is a draw that
	// did not happen rather than one that produced the wrong colour.
	const float clear_colour[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
	context->ClearRenderTargetView(target_view, clear_colour);
	context->OMSetRenderTargets(1, &target_view, NULL);

	D3D11_VIEWPORT viewport;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = static_cast<float>(TARGET_WIDTH);
	viewport.Height = static_cast<float>(TARGET_HEIGHT);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	const UINT offset = 0;
	const UINT vertex_stride = stride;
	context->IASetInputLayout(layout);
	context->IASetVertexBuffers(0, 1, &vertex_buffer, &vertex_stride, &offset);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	context->VSSetShader(vertex_shader, NULL, 0);
	context->VSSetConstantBuffers(0, 1, &vertex_constant_buffer);
	context->PSSetShader(pixel_shader, NULL, 0);
	context->PSSetConstantBuffers(0, 1, &pixel_constant_buffer);
	context->OMSetBlendState(blend_state, NULL, 0xffffffff);
	context->OMSetDepthStencilState(depth_state, states.Get_Stencil_Reference());
	context->RSSetState(rasterizer_state);
	context->Draw(4, 0);

	context->CopyResource(staging, target);
	print_validation_messages(d3d);

	D3D11_MAPPED_SUBRESOURCE mapped;
	CHECK(SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));

	// The middle of the target, read as the four bytes a B8G8R8A8 pixel is.
	const unsigned char * row = static_cast<const unsigned char *>(mapped.pData)
		+ mapped.RowPitch * (TARGET_HEIGHT / 2);
	const unsigned char * pixel = row + (TARGET_WIDTH / 2) * 4;
	if (pixel[0] != EXPECTED_BLUE || pixel[2] != EXPECTED_RED) {
		printf("  centre pixel bgra %02x %02x %02x %02x\n", pixel[0], pixel[1], pixel[2], pixel[3]);
		printf("---- vertex ----\n%s---- pixel ----\n%s----\n", vertex_hlsl.c_str(),
			pixel_hlsl.c_str());
	}
	CHECK_EQ(pixel[0], EXPECTED_BLUE);
	CHECK_EQ(pixel[1], EXPECTED_GREEN);
	CHECK_EQ(pixel[2], EXPECTED_RED);
	CHECK_EQ(pixel[3], EXPECTED_ALPHA);

	// The quad covers the whole target, so a corner has to say the same thing.  A draw that covered
	// only part of it would pass at the centre and be wrong everywhere else, which is what a
	// mistranslated position or a dropped triangle looks like.
	const unsigned char * corner = static_cast<const unsigned char *>(mapped.pData);
	CHECK_EQ(corner[0], EXPECTED_BLUE);
	CHECK_EQ(corner[2], EXPECTED_RED);

	context->Unmap(staging, 0);

	pixel_constant_buffer->Release();
	vertex_constant_buffer->Release();
	vertex_buffer->Release();
	rasterizer_state->Release();
	depth_state->Release();
	blend_state->Release();
	layout->Release();
	pixel_shader->Release();
	vertex_shader->Release();
	pixel_code->Release();
	vertex_code->Release();
	target_view->Release();
	staging->Release();
	target->Release();
}
