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

#include "dx11backend.h"

#include "dx11layout.h"

#include <d3dcommon.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

typedef HRESULT (WINAPI *D3DCompileFunction)(LPCVOID source_data, SIZE_T source_size,
	LPCSTR source_name, const D3D_SHADER_MACRO * defines, ID3DInclude * include,
	LPCSTR entry_point, LPCSTR target, UINT flags1, UINT flags2, ID3DBlob ** code,
	ID3DBlob ** error_messages);

// d3dcompiler_47.dll ships with Windows.  Binding it by hand rather than linking it keeps a machine
// without it running the Direct3D 9 device instead of failing to start.
static const char * const COMPILER_MODULE = "d3dcompiler_47.dll";
static const char * const ENTRY_POINT = "main";
static const char * const VERTEX_PROFILE = "vs_4_0";
static const char * const PIXEL_PROFILE = "ps_4_0";

// The whole constant block the generated vertex shader reads, always uploaded in full.  A shader
// generated for fewer lights declares a prefix of this, and a constant buffer larger than what a
// shader declares is legal, so one buffer serves every program.
struct VertexConstantBlock
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
	float LightFields[MAXIMUM_VERTEX_LIGHTS][6][4];
};

struct PixelConstantBlock
{
	float TextureFactor[4];
	float FogColour[4];
	float AlphaReference[4];
};

static D3DCompileFunction compiler_function()
{
	static D3DCompileFunction compiler = NULL;
	static bool attempted = false;
	if (!attempted) {
		attempted = true;
		HMODULE module = LoadLibraryA(COMPILER_MODULE);
		if (module != NULL) {
			compiler = reinterpret_cast<D3DCompileFunction>(GetProcAddress(module, "D3DCompile"));
		}
	}
	return compiler;
}

static void set_identity(float matrix[16])
{
	memset(matrix, 0, sizeof(float) * 16);
	matrix[0] = 1.0f;
	matrix[5] = 1.0f;
	matrix[10] = 1.0f;
	matrix[15] = 1.0f;
}

// Row major throughout, the way D3D9 stores a matrix and the way the generated shaders declare one.
static void multiply(const float left[16], const float right[16], float result[16])
{
	for (unsigned row = 0; row < 4; ++row) {
		for (unsigned column = 0; column < 4; ++column) {
			float sum = 0.0f;
			for (unsigned index = 0; index < 4; ++index) {
				sum += left[row * 4 + index] * right[index * 4 + column];
			}
			result[row * 4 + column] = sum;
		}
	}
}

// A normal is transformed by the inverse transpose of the upper three by three, not by the matrix
// itself.  With a uniform scale the two differ only by a factor that normalize removes, but the
// engine scales models unevenly and a normal carried through the matrix itself comes out pointing
// off the surface, which is a lighting error that looks like a modelling error.
static void inverse_transpose(const float source[16], float result[16])
{
	const float a = source[0], b = source[1], c = source[2];
	const float d = source[4], e = source[5], f = source[6];
	const float g = source[8], h = source[9], i = source[10];

	const float determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);

	set_identity(result);
	if (determinant == 0.0f) {
		return;
	}
	const float scale = 1.0f / determinant;

	// The inverse is the adjugate over the determinant; transposing it is the same as reading the
	// cofactors along the rows instead of down the columns, which is what this does.
	result[0] = (e * i - f * h) * scale;
	result[1] = (f * g - d * i) * scale;
	result[2] = (d * h - e * g) * scale;
	result[4] = (c * h - b * i) * scale;
	result[5] = (a * i - c * g) * scale;
	result[6] = (b * g - a * h) * scale;
	result[8] = (b * f - c * e) * scale;
	result[9] = (c * d - a * f) * scale;
	result[10] = (a * e - b * d) * scale;
}

DX11BackendClass::DX11BackendClass()
	: Device(NULL)
	, VertexFormat(0)
	, StreamBuffer(NULL)
	, StreamStride(0)
	, StreamOffset(0)
	, IndexBuffer(NULL)
	, IndexFormat(DXGI_FORMAT_R16_UINT)
	, MaterialPower(0.0f)
	, VertexConstantBuffer(NULL)
	, PixelConstantBuffer(NULL)
	, PipelinesBuilt(0)
	, DrawsMade(0)
	, DrawsRefused(0)
{
	memset(StageStates, 0, sizeof(StageStates));
	memset(Textures, 0, sizeof(Textures));
	memset(Lights, 0, sizeof(Lights));
	memset(MaterialAmbient, 0, sizeof(MaterialAmbient));
	memset(MaterialDiffuse, 0, sizeof(MaterialDiffuse));
	memset(MaterialSpecular, 0, sizeof(MaterialSpecular));
	memset(MaterialEmissive, 0, sizeof(MaterialEmissive));

	set_identity(World);
	set_identity(View);
	set_identity(Projection);

	// The device's own defaults, so a stage nobody has written to is a stage that is off.
	for (unsigned stage = 0; stage < DX11_BACKEND_TEXTURE_STAGES; ++stage) {
		StageStates[stage][D3DTSS_COLOROP] = (stage == 0) ? D3DTOP_MODULATE : D3DTOP_DISABLE;
		StageStates[stage][D3DTSS_ALPHAOP] = (stage == 0) ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
		StageStates[stage][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
		StageStates[stage][D3DTSS_COLORARG2] = D3DTA_CURRENT;
		StageStates[stage][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
		StageStates[stage][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
		StageStates[stage][D3DTSS_TEXCOORDINDEX] = stage;
	}
}

DX11BackendClass::~DX11BackendClass()
{
	Shutdown();
}

bool DX11BackendClass::Initialise(DX11DeviceClass * device)
{
	Device = device;

	D3D11_BUFFER_DESC description;
	memset(&description, 0, sizeof(description));
	description.ByteWidth = sizeof(VertexConstantBlock);
	description.Usage = D3D11_USAGE_DYNAMIC;
	description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	if (FAILED(Device->Get_Device()->CreateBuffer(&description, NULL, &VertexConstantBuffer))) {
		return false;
	}

	description.ByteWidth = sizeof(PixelConstantBlock);
	return SUCCEEDED(Device->Get_Device()->CreateBuffer(&description, NULL, &PixelConstantBuffer));
}

void DX11BackendClass::Release_Cached()
{
	for (std::map<std::string, Pipeline>::iterator entry = Pipelines.begin();
			entry != Pipelines.end(); ++entry) {
		entry->second.VertexShader->Release();
		entry->second.PixelShader->Release();
		entry->second.Layout->Release();
	}
	Pipelines.clear();

	for (std::map<std::string, ID3D11BlendState *>::iterator entry = BlendStates.begin();
			entry != BlendStates.end(); ++entry) {
		entry->second->Release();
	}
	BlendStates.clear();

	for (std::map<std::string, ID3D11DepthStencilState *>::iterator entry
			= DepthStencilStates.begin(); entry != DepthStencilStates.end(); ++entry) {
		entry->second->Release();
	}
	DepthStencilStates.clear();

	for (std::map<std::string, ID3D11RasterizerState *>::iterator entry = RasterizerStates.begin();
			entry != RasterizerStates.end(); ++entry) {
		entry->second->Release();
	}
	RasterizerStates.clear();

	for (std::map<std::string, ID3D11SamplerState *>::iterator entry = SamplerStates.begin();
			entry != SamplerStates.end(); ++entry) {
		entry->second->Release();
	}
	SamplerStates.clear();
}

void DX11BackendClass::Shutdown()
{
	Release_Cached();

	if (PixelConstantBuffer != NULL) {
		PixelConstantBuffer->Release();
		PixelConstantBuffer = NULL;
	}
	if (VertexConstantBuffer != NULL) {
		VertexConstantBuffer->Release();
		VertexConstantBuffer = NULL;
	}
	Device = NULL;
}

void DX11BackendClass::Set_Render_State(D3DRENDERSTATETYPE state, DWORD value)
{
	RenderStates.Set_Render_State(state, value);
}

void DX11BackendClass::Set_Texture_Stage_State(unsigned stage, D3DTEXTURESTAGESTATETYPE state,
	DWORD value)
{
	if (stage < DX11_BACKEND_TEXTURE_STAGES && static_cast<unsigned>(state) < DX11_BACKEND_STAGE_STATES) {
		StageStates[stage][state] = value;
	}
}

void DX11BackendClass::Set_Sampler_State(unsigned sampler, D3DSAMPLERSTATETYPE state, DWORD value)
{
	if (sampler < DX11_BACKEND_TEXTURE_STAGES) {
		Samplers[sampler].Set_Sampler_State(state, value);
	}
}

void DX11BackendClass::Set_Texture(unsigned stage, ID3D11ShaderResourceView * texture)
{
	if (stage < DX11_BACKEND_TEXTURE_STAGES) {
		Textures[stage] = texture;
	}
}

void DX11BackendClass::Set_Vertex_Format(DWORD fvf)
{
	VertexFormat = fvf;
}

void DX11BackendClass::Set_Stream_Source(ID3D11Buffer * buffer, unsigned stride, unsigned offset)
{
	StreamBuffer = buffer;
	StreamStride = stride;
	StreamOffset = offset;
}

void DX11BackendClass::Set_Indices(ID3D11Buffer * buffer, DXGI_FORMAT format)
{
	IndexBuffer = buffer;
	IndexFormat = format;
}

void DX11BackendClass::Set_Transform(D3DTRANSFORMSTATETYPE state, const float matrix[16])
{
	switch (state) {
	case D3DTS_WORLD:       memcpy(World, matrix, sizeof(World)); break;
	case D3DTS_VIEW:        memcpy(View, matrix, sizeof(View)); break;
	case D3DTS_PROJECTION:  memcpy(Projection, matrix, sizeof(Projection)); break;
	default:                break;
	}
}

void DX11BackendClass::Set_Material(const float ambient[4], const float diffuse[4],
	const float specular[4], const float emissive[4], float power)
{
	memcpy(MaterialAmbient, ambient, sizeof(MaterialAmbient));
	memcpy(MaterialDiffuse, diffuse, sizeof(MaterialDiffuse));
	memcpy(MaterialSpecular, specular, sizeof(MaterialSpecular));
	memcpy(MaterialEmissive, emissive, sizeof(MaterialEmissive));
	MaterialPower = power;
}

void DX11BackendClass::Set_Light(unsigned index, DWORD type, const float position[4],
	const float direction[4], const float diffuse[4], const float specular[4],
	const float attenuation[4], const float spot[4])
{
	if (index >= MAXIMUM_VERTEX_LIGHTS) {
		return;
	}

	Light & light = Lights[index];
	light.Enabled = true;
	light.Type = type;
	memcpy(light.Position, position, sizeof(light.Position));
	memcpy(light.Direction, direction, sizeof(light.Direction));
	memcpy(light.Diffuse, diffuse, sizeof(light.Diffuse));
	memcpy(light.Specular, specular, sizeof(light.Specular));
	memcpy(light.Attenuation, attenuation, sizeof(light.Attenuation));
	memcpy(light.Spot, spot, sizeof(light.Spot));
}

void DX11BackendClass::Disable_Light(unsigned index)
{
	if (index < MAXIMUM_VERTEX_LIGHTS) {
		Lights[index].Enabled = false;
	}
}

void DX11BackendClass::Set_Viewport(unsigned x, unsigned y, unsigned width, unsigned height)
{
	D3D11_VIEWPORT viewport;
	viewport.TopLeftX = static_cast<float>(x);
	viewport.TopLeftY = static_cast<float>(y);
	viewport.Width = static_cast<float>(width);
	viewport.Height = static_cast<float>(height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	Device->Get_Context()->RSSetViewports(1, &viewport);
}

void DX11BackendClass::Clear(bool colour, bool depth, const float colour_value[4])
{
	if (colour && Device->Get_Back_Buffer_View() != NULL) {
		Device->Get_Context()->ClearRenderTargetView(Device->Get_Back_Buffer_View(), colour_value);
	}
	if (depth && Device->Get_Depth_Stencil_View() != NULL) {
		Device->Get_Context()->ClearDepthStencilView(Device->Get_Depth_Stencil_View(),
			D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	}
}

bool DX11BackendClass::Build_Combiner_Description(CombinerDescription & description) const
{
	memset(&description, 0, sizeof(description));

	// The alpha test and the fog are pipeline state under D3D9 and shader instructions here, so
	// they are read off the render states and handed to the generator as part of the program.
	description.PixelPipeline.AlphaTestEnabled =
		RenderStates.Get_Render_State(D3DRS_ALPHATESTENABLE) != FALSE;
	description.PixelPipeline.AlphaFunction = RenderStates.Get_Render_State(D3DRS_ALPHAFUNC);
	description.PixelPipeline.FogEnabled = RenderStates.Get_Render_State(D3DRS_FOGENABLE) != FALSE;

	description.StageCount = 0;
	for (unsigned stage = 0; stage < MAXIMUM_COMBINER_STAGES; ++stage) {
		if (StageStates[stage][D3DTSS_COLOROP] == D3DTOP_DISABLE) {
			break;
		}

		CombinerStage & target = description.Stages[stage];
		target.ColourOperation = StageStates[stage][D3DTSS_COLOROP];
		target.ColourArgument0 = StageStates[stage][D3DTSS_COLORARG0];
		target.ColourArgument1 = StageStates[stage][D3DTSS_COLORARG1];
		target.ColourArgument2 = StageStates[stage][D3DTSS_COLORARG2];
		target.AlphaOperation = StageStates[stage][D3DTSS_ALPHAOP];
		target.AlphaArgument0 = StageStates[stage][D3DTSS_ALPHAARG0];
		target.AlphaArgument1 = StageStates[stage][D3DTSS_ALPHAARG1];
		target.AlphaArgument2 = StageStates[stage][D3DTSS_ALPHAARG2];
		target.TextureCoordinateIndex = StageStates[stage][D3DTSS_TEXCOORDINDEX];
		target.TextureBound = Textures[stage] != NULL;
		description.StageCount = stage + 1;
	}
	return description.StageCount > 0;
}

bool DX11BackendClass::Build_Vertex_Description(VertexPipelineDescription & description) const
{
	memset(&description, 0, sizeof(description));
	description.FVF = VertexFormat;
	description.LightingEnabled = RenderStates.Get_Render_State(D3DRS_LIGHTING) != FALSE;
	description.SpecularEnabled = RenderStates.Get_Render_State(D3DRS_SPECULARENABLE) != FALSE;
	description.ColourVertexEnabled = RenderStates.Get_Render_State(D3DRS_COLORVERTEX) != FALSE;
	description.DiffuseMaterialSource = RenderStates.Get_Render_State(D3DRS_DIFFUSEMATERIALSOURCE);
	description.AmbientMaterialSource = RenderStates.Get_Render_State(D3DRS_AMBIENTMATERIALSOURCE);
	description.EmissiveMaterialSource = RenderStates.Get_Render_State(D3DRS_EMISSIVEMATERIALSOURCE);
	description.SpecularMaterialSource = RenderStates.Get_Render_State(D3DRS_SPECULARMATERIALSOURCE);

	// The lights have to be contiguous in the shader, so an enabled light after a disabled one
	// moves down.  Which register a light ends up in is the shader's business and the constants
	// are packed to match below.
	description.LightCount = 0;
	for (unsigned index = 0; index < MAXIMUM_VERTEX_LIGHTS; ++index) {
		if (Lights[index].Enabled) {
			description.Lights[description.LightCount].Type = Lights[index].Type;
			++description.LightCount;
		}
	}

	description.StageCount = 0;
	for (unsigned stage = 0; stage < MAXIMUM_VERTEX_STAGES; ++stage) {
		if (StageStates[stage][D3DTSS_COLOROP] == D3DTOP_DISABLE) {
			break;
		}
		description.Stages[stage].TextureCoordinateIndex = StageStates[stage][D3DTSS_TEXCOORDINDEX];
		description.Stages[stage].TextureTransformFlags =
			StageStates[stage][D3DTSS_TEXTURETRANSFORMFLAGS];
		description.StageCount = stage + 1;
	}

	description.FogEnabled = RenderStates.Get_Render_State(D3DRS_FOGENABLE) != FALSE;
	description.FogVertexMode = RenderStates.Get_Render_State(D3DRS_FOGVERTEXMODE);
	return true;
}

bool DX11BackendClass::Resolve(Pipeline & pipeline)
{
	VertexPipelineDescription vertex_description;
	CombinerDescription combiner_description;
	if (!Build_Vertex_Description(vertex_description)
		|| !Build_Combiner_Description(combiner_description)) {
		return false;
	}

	char format[32];
	snprintf(format, sizeof(format), "|%lu", VertexFormat);
	const std::string key = VertexShader_Key(vertex_description)
		+ CombinerShader_Key(combiner_description) + format;

	std::map<std::string, Pipeline>::const_iterator existing = Pipelines.find(key);
	if (existing != Pipelines.end()) {
		pipeline = existing->second;
		return true;
	}

	D3DCompileFunction compiler = compiler_function();
	if (compiler == NULL) {
		return false;
	}

	std::string vertex_hlsl;
	std::string pixel_hlsl;
	if (!VertexShader_Generate(vertex_description, VERTEX_SHADER_TARGET_D3D11, vertex_hlsl)
		|| !CombinerShader_Generate(combiner_description, COMBINER_SHADER_TARGET_D3D11,
				pixel_hlsl)) {
		return false;
	}

	D3D11_INPUT_ELEMENT_DESC elements[MAXIMUM_LAYOUT_ELEMENTS];
	unsigned element_count = 0;
	unsigned stride = 0;
	if (!DX11Layout_From_FVF(VertexFormat, elements, element_count, stride)) {
		return false;
	}

	ID3DBlob * vertex_code = NULL;
	ID3DBlob * pixel_code = NULL;
	if (FAILED(compiler(vertex_hlsl.c_str(), vertex_hlsl.size(), "ffvertex", NULL, NULL,
			ENTRY_POINT, VERTEX_PROFILE, 0, 0, &vertex_code, NULL))) {
		return false;
	}
	if (FAILED(compiler(pixel_hlsl.c_str(), pixel_hlsl.size(), "ffshader", NULL, NULL,
			ENTRY_POINT, PIXEL_PROFILE, 0, 0, &pixel_code, NULL))) {
		vertex_code->Release();
		return false;
	}

	Pipeline built;
	built.VertexShader = NULL;
	built.PixelShader = NULL;
	built.Layout = NULL;

	ID3D11Device * device = Device->Get_Device();
	const bool created = SUCCEEDED(device->CreateVertexShader(vertex_code->GetBufferPointer(),
			vertex_code->GetBufferSize(), NULL, &built.VertexShader))
		&& SUCCEEDED(device->CreatePixelShader(pixel_code->GetBufferPointer(),
			pixel_code->GetBufferSize(), NULL, &built.PixelShader))
		&& SUCCEEDED(device->CreateInputLayout(elements, element_count,
			vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), &built.Layout));

	pixel_code->Release();
	vertex_code->Release();

	if (!created) {
		if (built.Layout != NULL) {
			built.Layout->Release();
		}
		if (built.PixelShader != NULL) {
			built.PixelShader->Release();
		}
		if (built.VertexShader != NULL) {
			built.VertexShader->Release();
		}
		return false;
	}

	Pipelines[key] = built;
	++PipelinesBuilt;
	pipeline = built;
	return true;
}

void DX11BackendClass::Upload_Constants()
{
	ID3D11DeviceContext * context = Device->Get_Context();

	VertexConstantBlock vertex_block;
	memset(&vertex_block, 0, sizeof(vertex_block));

	float world_view[16];
	multiply(World, View, world_view);
	multiply(world_view, Projection, vertex_block.WorldViewProjection);
	memcpy(vertex_block.WorldView, world_view, sizeof(world_view));
	inverse_transpose(world_view, vertex_block.NormalTransform);
	set_identity(vertex_block.TextureMatrix0);
	set_identity(vertex_block.TextureMatrix1);

	memcpy(vertex_block.MaterialAmbient, MaterialAmbient, sizeof(MaterialAmbient));
	memcpy(vertex_block.MaterialDiffuse, MaterialDiffuse, sizeof(MaterialDiffuse));
	memcpy(vertex_block.MaterialSpecular, MaterialSpecular, sizeof(MaterialSpecular));
	memcpy(vertex_block.MaterialEmissive, MaterialEmissive, sizeof(MaterialEmissive));
	vertex_block.MaterialPower[0] = MaterialPower;

	const DWORD ambient = RenderStates.Get_Render_State(D3DRS_AMBIENT);
	vertex_block.GlobalAmbient[0] = static_cast<float>((ambient >> 16) & 0xff) / 255.0f;
	vertex_block.GlobalAmbient[1] = static_cast<float>((ambient >> 8) & 0xff) / 255.0f;
	vertex_block.GlobalAmbient[2] = static_cast<float>(ambient & 0xff) / 255.0f;
	vertex_block.GlobalAmbient[3] = static_cast<float>((ambient >> 24) & 0xff) / 255.0f;

	// The fog start, end and density, in the order the generated shader reads them.  D3D9 carries
	// all three as floats inside a DWORD render state.
	const DWORD fog_start = RenderStates.Get_Render_State(D3DRS_FOGSTART);
	const DWORD fog_end = RenderStates.Get_Render_State(D3DRS_FOGEND);
	const DWORD fog_density = RenderStates.Get_Render_State(D3DRS_FOGDENSITY);
	memcpy(&vertex_block.FogParameters[0], &fog_start, sizeof(float));
	memcpy(&vertex_block.FogParameters[1], &fog_end, sizeof(float));
	memcpy(&vertex_block.FogParameters[2], &fog_density, sizeof(float));

	// Packed the way the shader declares them, and only the enabled ones, in order: the shader was
	// generated from the same walk.
	unsigned slot = 0;
	for (unsigned index = 0; index < MAXIMUM_VERTEX_LIGHTS; ++index) {
		if (!Lights[index].Enabled) {
			continue;
		}
		memcpy(vertex_block.LightFields[slot][0], Lights[index].Position, sizeof(float) * 4);
		memcpy(vertex_block.LightFields[slot][1], Lights[index].Direction, sizeof(float) * 4);
		memcpy(vertex_block.LightFields[slot][2], Lights[index].Diffuse, sizeof(float) * 4);
		memcpy(vertex_block.LightFields[slot][3], Lights[index].Specular, sizeof(float) * 4);
		memcpy(vertex_block.LightFields[slot][4], Lights[index].Attenuation, sizeof(float) * 4);
		memcpy(vertex_block.LightFields[slot][5], Lights[index].Spot, sizeof(float) * 4);
		++slot;
	}

	D3D11_MAPPED_SUBRESOURCE mapped;
	if (SUCCEEDED(context->Map(VertexConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		memcpy(mapped.pData, &vertex_block, sizeof(vertex_block));
		context->Unmap(VertexConstantBuffer, 0);
	}

	PixelConstantBlock pixel_block;
	memset(&pixel_block, 0, sizeof(pixel_block));
	RenderStates.Get_Texture_Factor(pixel_block.TextureFactor);

	const DWORD fog_colour = RenderStates.Get_Render_State(D3DRS_FOGCOLOR);
	pixel_block.FogColour[0] = static_cast<float>((fog_colour >> 16) & 0xff) / 255.0f;
	pixel_block.FogColour[1] = static_cast<float>((fog_colour >> 8) & 0xff) / 255.0f;
	pixel_block.FogColour[2] = static_cast<float>(fog_colour & 0xff) / 255.0f;
	pixel_block.FogColour[3] = static_cast<float>((fog_colour >> 24) & 0xff) / 255.0f;

	// D3DRS_ALPHAREF is an eight bit value and the shader compares a float.
	pixel_block.AlphaReference[0] =
		static_cast<float>(RenderStates.Get_Render_State(D3DRS_ALPHAREF) & 0xff) / 255.0f;

	if (SUCCEEDED(context->Map(PixelConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		memcpy(mapped.pData, &pixel_block, sizeof(pixel_block));
		context->Unmap(PixelConstantBuffer, 0);
	}
}

ID3D11BlendState * DX11BackendClass::Blend_State()
{
	D3D11_BLEND_DESC description;
	RenderStates.Build_Blend_Description(description);

	const std::string key(reinterpret_cast<const char *>(&description), sizeof(description));
	std::map<std::string, ID3D11BlendState *>::const_iterator existing = BlendStates.find(key);
	if (existing != BlendStates.end()) {
		return existing->second;
	}

	ID3D11BlendState * state = NULL;
	if (FAILED(Device->Get_Device()->CreateBlendState(&description, &state))) {
		return NULL;
	}
	BlendStates[key] = state;
	return state;
}

ID3D11DepthStencilState * DX11BackendClass::Depth_Stencil_State()
{
	D3D11_DEPTH_STENCIL_DESC description;
	RenderStates.Build_Depth_Stencil_Description(description);

	const std::string key(reinterpret_cast<const char *>(&description), sizeof(description));
	std::map<std::string, ID3D11DepthStencilState *>::const_iterator existing
		= DepthStencilStates.find(key);
	if (existing != DepthStencilStates.end()) {
		return existing->second;
	}

	ID3D11DepthStencilState * state = NULL;
	if (FAILED(Device->Get_Device()->CreateDepthStencilState(&description, &state))) {
		return NULL;
	}
	DepthStencilStates[key] = state;
	return state;
}

ID3D11RasterizerState * DX11BackendClass::Rasterizer_State()
{
	D3D11_RASTERIZER_DESC description;
	RenderStates.Build_Rasterizer_Description(description);

	const std::string key(reinterpret_cast<const char *>(&description), sizeof(description));
	std::map<std::string, ID3D11RasterizerState *>::const_iterator existing
		= RasterizerStates.find(key);
	if (existing != RasterizerStates.end()) {
		return existing->second;
	}

	ID3D11RasterizerState * state = NULL;
	if (FAILED(Device->Get_Device()->CreateRasterizerState(&description, &state))) {
		return NULL;
	}
	RasterizerStates[key] = state;
	return state;
}

ID3D11SamplerState * DX11BackendClass::Sampler_State(unsigned sampler)
{
	D3D11_SAMPLER_DESC description;
	Samplers[sampler].Build_Sampler_Description(description);

	const std::string key(reinterpret_cast<const char *>(&description), sizeof(description));
	std::map<std::string, ID3D11SamplerState *>::const_iterator existing = SamplerStates.find(key);
	if (existing != SamplerStates.end()) {
		return existing->second;
	}

	ID3D11SamplerState * state = NULL;
	if (FAILED(Device->Get_Device()->CreateSamplerState(&description, &state))) {
		return NULL;
	}
	SamplerStates[key] = state;
	return state;
}

void DX11BackendClass::Bind_State_Objects()
{
	ID3D11DeviceContext * context = Device->Get_Context();

	context->OMSetBlendState(Blend_State(), NULL, 0xffffffff);
	context->OMSetDepthStencilState(Depth_Stencil_State(), RenderStates.Get_Stencil_Reference());
	context->RSSetState(Rasterizer_State());

	ID3D11SamplerState * samplers[DX11_BACKEND_TEXTURE_STAGES];
	for (unsigned sampler = 0; sampler < DX11_BACKEND_TEXTURE_STAGES; ++sampler) {
		samplers[sampler] = Sampler_State(sampler);
	}
	context->PSSetSamplers(0, DX11_BACKEND_TEXTURE_STAGES, samplers);
	context->PSSetShaderResources(0, DX11_BACKEND_TEXTURE_STAGES, Textures);
}

bool DX11BackendClass::Draw_Indexed_Triangles(unsigned index_count, unsigned start_index,
	unsigned base_vertex)
{
	Pipeline pipeline;
	if (StreamBuffer == NULL || IndexBuffer == NULL || !Resolve(pipeline)) {
		++DrawsRefused;
		return false;
	}

	ID3D11DeviceContext * context = Device->Get_Context();
	Upload_Constants();
	Bind_State_Objects();

	const UINT stride = StreamStride;
	const UINT offset = StreamOffset;
	context->IASetInputLayout(pipeline.Layout);
	context->IASetVertexBuffers(0, 1, &StreamBuffer, &stride, &offset);
	context->IASetIndexBuffer(IndexBuffer, IndexFormat, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(pipeline.VertexShader, NULL, 0);
	context->VSSetConstantBuffers(0, 1, &VertexConstantBuffer);
	context->PSSetShader(pipeline.PixelShader, NULL, 0);
	context->PSSetConstantBuffers(0, 1, &PixelConstantBuffer);
	context->DrawIndexed(index_count, start_index, base_vertex);

	++DrawsMade;
	return true;
}

bool DX11BackendClass::Draw_Triangles(unsigned vertex_count, unsigned start_vertex)
{
	Pipeline pipeline;
	if (StreamBuffer == NULL || !Resolve(pipeline)) {
		++DrawsRefused;
		return false;
	}

	ID3D11DeviceContext * context = Device->Get_Context();
	Upload_Constants();
	Bind_State_Objects();

	const UINT stride = StreamStride;
	const UINT offset = StreamOffset;
	context->IASetInputLayout(pipeline.Layout);
	context->IASetVertexBuffers(0, 1, &StreamBuffer, &stride, &offset);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(pipeline.VertexShader, NULL, 0);
	context->VSSetConstantBuffers(0, 1, &VertexConstantBuffer);
	context->PSSetShader(pipeline.PixelShader, NULL, 0);
	context->PSSetConstantBuffers(0, 1, &PixelConstantBuffer);
	context->Draw(vertex_count, start_vertex);

	++DrawsMade;
	return true;
}

void DX11BackendClass::Statistics(unsigned & pipelines_built, unsigned long long & draws_made,
	unsigned long long & draws_refused) const
{
	pipelines_built = PipelinesBuilt;
	draws_made = DrawsMade;
	draws_refused = DrawsRefused;
}
