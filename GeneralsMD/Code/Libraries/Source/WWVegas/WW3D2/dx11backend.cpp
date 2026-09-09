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
#include "dx11resource.h"

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
	float TextureMatrix[MAXIMUM_VERTEX_STAGES][16];
	float MaterialAmbient[4];
	float MaterialDiffuse[4];
	float MaterialSpecular[4];
	float MaterialEmissive[4];
	float MaterialPower[4];
	float GlobalAmbient[4];
	float FogParameters[4];
	// One over the viewport's width and height, for the pre-transformed draws.  It goes before the
	// lights because the generated block declares only as many lights as the state has.
	float ViewportInverse[4];
	float LightFields[MAXIMUM_VERTEX_LIGHTS][6][4];
};

// The three stage counts are one count in three headers.  The block above is copied wholesale out
// of the backend's own texture transforms, the generated pixel shader declares one sampler per
// texture the backend binds, and a mismatch is a silent overrun rather than a build failure.
static_assert(MAXIMUM_VERTEX_STAGES == DX11_BACKEND_TEXTURE_STAGES,
	"ffvertex declares one texture matrix per stage and the backend uploads one per stage");
static_assert(MAXIMUM_COMBINER_STAGES == DX11_BACKEND_TEXTURE_STAGES,
	"ffshader declares one sampler per stage and the backend binds one per stage");

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

// A light is handed to Direct3D 9 in world space and lit in camera space: the fixed-function
// pipeline carries its position and direction through the view matrix itself, once per light rather
// than once per vertex.  The generated shader lights in camera space too, so the same two products
// happen here.  Without them a world space light direction is dotted against a camera space normal
// and every lit model is shaded from the wrong angle - which reads as a lighting mood, not as a
// bug, and cost the whole of Alpine Assault's remaining 9.23%.
static void transform_point(const float source[4], const float matrix[16], float result[4])
{
	for (unsigned column = 0; column < 4; ++column) {
		result[column] = source[0] * matrix[column]
			+ source[1] * matrix[4 + column]
			+ source[2] * matrix[8 + column]
			+ matrix[12 + column];
	}
}

static void transform_direction(const float source[4], const float matrix[16], float result[4])
{
	for (unsigned column = 0; column < 4; ++column) {
		result[column] = source[0] * matrix[column]
			+ source[1] * matrix[4 + column]
			+ source[2] * matrix[8 + column];
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
	, EngineConstantBuffer(NULL)
	, VertexProgram(ENGINE_SHADER_NONE)
	, PixelProgram(ENGINE_SHADER_NONE)
	, UserBuffer(NULL)
	, UserBufferBytes(0)
	, PipelinesBuilt(0)
	, TracedUserStrip(false)
	, MaskWhileTargeted(0)
	, TargetsBound(0)
	, TargetsRestored(0)
	, DrawsIntoTargets(0)
	, CurrentTarget(NULL)
	, CurrentDepth(NULL)
	, DrawsMade(0)
	, DrawsRefused(0)
	, RefusedNoBuffer(0)
	, RefusedNoStage(0)
	, RefusedNoLayout(0)
	, RefusedNoProgram(0)
	, RefusedNoObject(0)
	, RefusedForeignShader(0)
	, RefusedNoTexture(0)
	, ForeignPixelShader(false)
	, ForeignVertexShader(false)
{
	memset(StageStates, 0, sizeof(StageStates));
	memset(Textures, 0, sizeof(Textures));
	memset(EngineConstants, 0, sizeof(EngineConstants));
	memset(MissingTexture, 0, sizeof(MissingTexture));
	memset(Lights, 0, sizeof(Lights));
	memset(MaterialAmbient, 0, sizeof(MaterialAmbient));
	memset(MaterialDiffuse, 0, sizeof(MaterialDiffuse));
	memset(MaterialSpecular, 0, sizeof(MaterialSpecular));
	memset(MaterialEmissive, 0, sizeof(MaterialEmissive));

	set_identity(World);
	set_identity(View);
	set_identity(Projection);
	for (unsigned stage = 0; stage < DX11_BACKEND_TEXTURE_STAGES; ++stage) {
		set_identity(TextureTransforms[stage]);
	}
	ViewportWidth = 1;
	ViewportHeight = 1;

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
	if (FAILED(Device->Get_Device()->CreateBuffer(&description, NULL, &PixelConstantBuffer))) {
		return false;
	}

	description.ByteWidth = sizeof(EngineConstants);
	return SUCCEEDED(Device->Get_Device()->CreateBuffer(&description, NULL, &EngineConstantBuffer));
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

	for (std::map<unsigned long long, ID3D11DepthStencilView *>::iterator entry
			= TargetDepths.begin(); entry != TargetDepths.end(); ++entry) {
		if (entry->second != NULL) {
			entry->second->Release();
		}
	}
	TargetDepths.clear();
	CurrentTarget = NULL;
	CurrentDepth = NULL;

	for (std::map<std::string, ID3D11SamplerState *>::iterator entry = SamplerStates.begin();
			entry != SamplerStates.end(); ++entry) {
		entry->second->Release();
	}
	SamplerStates.clear();
}

void DX11BackendClass::Shutdown()
{
	Release_Cached();

	if (EngineConstantBuffer != NULL) {
		EngineConstantBuffer->Release();
		EngineConstantBuffer = NULL;
	}
	if (PixelConstantBuffer != NULL) {
		PixelConstantBuffer->Release();
		PixelConstantBuffer = NULL;
	}
	if (VertexConstantBuffer != NULL) {
		VertexConstantBuffer->Release();
		VertexConstantBuffer = NULL;
	}
	if (UserBuffer != NULL) {
		UserBuffer->Release();
		UserBuffer = NULL;
		UserBufferBytes = 0;
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

void DX11BackendClass::Set_Pixel_Program(EngineShaderProgram program, bool bound,
	const char * file_name)
{
	PixelProgram = bound ? program : ENGINE_SHADER_NONE;
	ForeignPixelShader = bound && program == ENGINE_SHADER_NONE;
	ForeignPixelName = ForeignPixelShader ? file_name : "";
}

void DX11BackendClass::Set_Vertex_Program(EngineShaderProgram program, bool bound,
	const char * file_name)
{
	VertexProgram = bound ? program : ENGINE_SHADER_NONE;
	ForeignVertexShader = bound && program == ENGINE_SHADER_NONE;
	ForeignVertexName = ForeignVertexShader ? file_name : "";
}

// Named by whichever half has no program, and by both when neither does.
void DX11BackendClass::Note_Foreign_Refusal()
{
	std::string which = ForeignVertexShader ? ForeignVertexName : std::string();
	if (ForeignPixelShader) {
		if (!which.empty()) {
			which += " with ";
		}
		which += ForeignPixelName;
	}
	++ForeignRefusals[which];
}

unsigned DX11BackendClass::Foreign_Report_Count() const
{
	return (unsigned)ForeignRefusals.size();
}

const char * DX11BackendClass::Foreign_Report(unsigned index)
{
	std::map<std::string, unsigned long long>::const_iterator entry = ForeignRefusals.begin();
	for (unsigned step = 0; step < index && entry != ForeignRefusals.end(); ++step) {
		++entry;
	}
	if (entry == ForeignRefusals.end()) {
		return "";
	}

	char tail[64];
	snprintf(tail, sizeof(tail), " -> %llu draws", entry->second);
	ReportLine = entry->first + tail;
	return ReportLine.c_str();
}

void DX11BackendClass::Set_Vertex_Program_Constant(unsigned first_register, const float * values,
	unsigned count)
{
	if (first_register + count > ENGINE_SHADER_CONSTANTS) {
		return;
	}
	memcpy(EngineConstants[first_register], values, sizeof(float) * 4 * count);
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
	default:
		if (state >= D3DTS_TEXTURE0
			&& state < D3DTS_TEXTURE0 + (int)DX11_BACKEND_TEXTURE_STAGES) {
			memcpy(TextureTransforms[state - D3DTS_TEXTURE0], matrix, sizeof(float) * 16);
		}
		break;
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

void DX11BackendClass::Set_Texture_Missing(unsigned stage, bool missing)
{
	if (stage < DX11_BACKEND_TEXTURE_STAGES) {
		MissingTexture[stage] = missing;
	}
}

// A stage only matters while its colour operation is on: the engine leaves textures bound at stages
// it has switched off, and refusing for one of those would refuse most of the frame.
bool DX11BackendClass::Any_Missing_Texture() const
{
	for (unsigned stage = 0; stage < DX11_BACKEND_TEXTURE_STAGES; ++stage) {
		if (StageStates[stage][D3DTSS_COLOROP] == D3DTOP_DISABLE) {
			break;
		}
		if (MissingTexture[stage]) {
			return true;
		}
	}
	return false;
}

void DX11BackendClass::Set_Dump_Directory(const char * directory)
{
	DumpDirectory = directory == NULL ? "" : directory;

	// fopen will not make the directory, and a dump that writes nothing looks exactly like a run
	// that built no pipelines.  An existing one comes back as an error and is ignored.
	if (!DumpDirectory.empty()) {
		CreateDirectoryA(DumpDirectory.c_str(), NULL);
	}
}

// The key has characters a file name cannot carry, so it becomes the file's first line and the
// name is a number.  Both programs of one pipeline go in one file, in the order they run.
void DX11BackendClass::Dump_Program(const std::string & key, const std::string & vertex_hlsl,
	const std::string & pixel_hlsl)
{
	if (DumpDirectory.empty()) {
		return;
	}

	char path[512];
	snprintf(path, sizeof(path), "%s/pipeline%03u.hlsl", DumpDirectory.c_str(), PipelinesBuilt);
	FILE * file = fopen(path, "wb");
	if (file == NULL) {
		return;
	}

	fprintf(file, "// state: %s\n\n// ---- vertex ----\n%s\n// ---- pixel ----\n%s\n",
		key.c_str(), vertex_hlsl.c_str(), pixel_hlsl.c_str());
	fclose(file);
}

void DX11BackendClass::Begin_Scene()
{
	// A scene can begin with a render target already set: the water's reflection and the shadow
	// projector both set theirs and then call WW3D::Begin_Render, and forcing the back buffer here
	// sent the reflection scene, and its clear, over the picture that was already drawn.
	if (CurrentTarget != NULL) {
		return;
	}

	ID3D11RenderTargetView * target = Device->Get_Back_Buffer_View();
	if (target == NULL) {
		return;
	}

	Device->Get_Context()->OMSetRenderTargets(1, &target, Device->Get_Depth_Stencil_View());
	Set_Viewport(0, 0, Device->Get_Width(), Device->Get_Height());
}

ID3D11DepthStencilView * DX11BackendClass::Depth_For(unsigned width, unsigned height)
{
	// A target the size of the back buffer shares the back buffer's depth, because that is what
	// the Direct3D 9 device does: the screen filters redirect the scene into a texture and hand
	// SetRenderTarget the depth surface the frame's Begin_Render already cleared.  Made separately
	// here, nothing ever cleared it, every triangle in the scene failed the depth test, and the
	// texture the composite sampled held its clear colour and nothing else - a black world under a
	// live command bar, which is what the bloom looked like for two sessions.
	if (width == Device->Get_Width() && height == Device->Get_Height()) {
		return Device->Get_Depth_Stencil_View();
	}

	const unsigned long long key = (static_cast<unsigned long long>(width) << 32) | height;
	std::map<unsigned long long, ID3D11DepthStencilView *>::const_iterator existing
		= TargetDepths.find(key);
	if (existing != TargetDepths.end()) {
		return existing->second;
	}

	ID3D11Texture2D * texture = NULL;
	ID3D11ShaderResourceView * unused = NULL;
	if (!DX11Resource_Create_Texture(Device->Get_Device(), width, height, 1, D3DFMT_D24S8,
			D3DPOOL_DEFAULT, D3DUSAGE_DEPTHSTENCIL, &texture, &unused)) {
		Note_Refusal("the device refused a depth buffer for a render target");
		TargetDepths[key] = NULL;
		return NULL;
	}

	ID3D11DepthStencilView * view = NULL;
	if (FAILED(Device->Get_Device()->CreateDepthStencilView(texture, NULL, &view))) {
		Note_Refusal("the device refused a depth view for a render target");
		view = NULL;
	}
	else {
		// A new depth texture holds whatever the driver left in it, which reads as everything being
		// nearer than anything about to be drawn.  Nothing else clears this one: the engine's own
		// clear goes to whichever surface Direct3D 9 has bound, and this surface has no D3D9 twin.
		Device->Get_Context()->ClearDepthStencilView(view,
			D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	}
	texture->Release();

	TargetDepths[key] = view;
	return view;
}

// The last few target changes rather than the first few: the first are the loading screen's, and
// the question is which target the scene itself is drawn into.
void DX11BackendClass::Trace_Target(const char * what, unsigned width, unsigned height)
{
	char line[80];
	if (width == 0) {
		snprintf(line, sizeof(line), "%s after %llu draws", what, DrawsMade);
	}
	else {
		snprintf(line, sizeof(line), "%s %ux%u after %llu draws", what, width, height, DrawsMade);
	}
	TargetTrace.push_back(line);
	if (TargetTrace.size() > TARGET_TRACE_LIMIT) {
		TargetTrace.erase(TargetTrace.begin());
	}
}

void DX11BackendClass::Set_Render_Target(ID3D11RenderTargetView * target)
{
	if (target == NULL) {
		++TargetsRestored;
		Trace_Target("back buffer", 0, 0);
		CurrentTarget = NULL;
		CurrentDepth = NULL;
		ID3D11RenderTargetView * back_buffer = Device->Get_Back_Buffer_View();
		if (back_buffer != NULL) {
			Device->Get_Context()->OMSetRenderTargets(1, &back_buffer,
				Device->Get_Depth_Stencil_View());
			Set_Viewport(0, 0, Device->Get_Width(), Device->Get_Height());
		}
		return;
	}

	ID3D11Resource * resource = NULL;
	target->GetResource(&resource);
	if (resource == NULL) {
		return;
	}

	unsigned width = 0;
	unsigned height = 0;
	ID3D11Texture2D * texture = NULL;
	if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&texture))) {
		D3D11_TEXTURE2D_DESC description;
		texture->GetDesc(&description);
		width = description.Width;
		height = description.Height;
		texture->Release();
	}
	resource->Release();

	if (width == 0 || height == 0) {
		return;
	}

	++TargetsBound;
	Trace_Target("texture", width, height);
	CurrentTarget = target;
	CurrentDepth = Depth_For(width, height);
	Device->Get_Context()->OMSetRenderTargets(1, &CurrentTarget, CurrentDepth);
	Set_Viewport(0, 0, width, height);
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

	ViewportWidth = (width == 0) ? 1 : width;
	ViewportHeight = (height == 0) ? 1 : height;
}

void DX11BackendClass::Clear(bool colour, bool depth, const float colour_value[4])
{
	// Whatever the draws are landing in is what a clear clears; a pass that clears its own render
	// target and then draws into it would otherwise clear the picture instead.
	ID3D11RenderTargetView * target = (CurrentTarget != NULL)
		? CurrentTarget : Device->Get_Back_Buffer_View();
	ID3D11DepthStencilView * depth_view = (CurrentTarget != NULL)
		? CurrentDepth : Device->Get_Depth_Stencil_View();

	if (colour && target != NULL) {
		Device->Get_Context()->ClearRenderTargetView(target, colour_value);
	}
	if (depth && depth_view != NULL) {
		Device->Get_Context()->ClearDepthStencilView(depth_view,
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

	// With a pixel shader bound the colour operations mean nothing - the shader replaces the
	// combiners and reads whatever coordinate sets it likes - so the walk cannot stop at a
	// disabled one.  The water leaves stages one to three disabled and samples all four.
	const bool every_stage = PixelProgram != ENGINE_SHADER_NONE;

	description.StageCount = 0;
	for (unsigned stage = 0; stage < MAXIMUM_VERTEX_STAGES; ++stage) {
		if (!every_stage && StageStates[stage][D3DTSS_COLOROP] == D3DTOP_DISABLE) {
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
	const bool built_combiners = Build_Combiner_Description(combiner_description);
	if (!Build_Vertex_Description(vertex_description)
		|| (!built_combiners && PixelProgram == ENGINE_SHADER_NONE)) {
		// Stage zero's colour operation is disabled, so there is no texture stage to write a
		// program from.  A transcribed pixel program does not need one: it is the combiners.  There
		// is no key yet at this point.
		++RefusedNoStage;
		return false;
	}

	char format[32];
	snprintf(format, sizeof(format), "|%lu", VertexFormat);

	// A transcribed program is the whole half it replaces: the fixed-function description says
	// nothing about it and two draws with the same shader and different stage state are the same
	// program, so its name replaces that description in the key rather than joining it.  The alpha
	// test and the fog stay in the pixel one, because they are still written into the program.
	const std::string vertex_key = (VertexProgram != ENGINE_SHADER_NONE)
		? std::string(EngineShader_Name(VertexProgram))
		: VertexShader_Key(vertex_description);
	const std::string pixel_key = (PixelProgram != ENGINE_SHADER_NONE)
		? EngineShader_Name(PixelProgram)
			+ CombinerShader_Pipeline_Key(combiner_description.PixelPipeline)
		: CombinerShader_Key(combiner_description);
	const std::string key = vertex_key + pixel_key + format;

	std::map<std::string, Pipeline>::const_iterator existing = Pipelines.find(key);
	if (existing != Pipelines.end()) {
		pipeline = existing->second;
		Record_Use(key);
		return true;
	}
	std::map<std::string, unsigned>::const_iterator refused = RefusedPipelines.find(key);
	if (refused != RefusedPipelines.end()) {
		Refuse(key, static_cast<RefusalReason>(refused->second));
		return false;
	}

	D3DCompileFunction compiler = compiler_function();
	if (compiler == NULL) {
		Refuse(key, REFUSED_NO_PROGRAM);
		return false;
	}

	std::string vertex_hlsl;
	std::string pixel_hlsl;
	const bool wrote_vertex = (VertexProgram != ENGINE_SHADER_NONE)
		? EngineShader_Vertex_Program(VertexProgram, vertex_hlsl)
		: VertexShader_Generate(vertex_description, VERTEX_SHADER_TARGET_D3D11, vertex_hlsl);
	if (!wrote_vertex) {
		Note_Refusal("the vertex half would not generate this description");
		Refuse(key, REFUSED_NO_PROGRAM);
		return false;
	}
	const bool wrote_pixel = (PixelProgram != ENGINE_SHADER_NONE)
		? EngineShader_Pixel_Program(PixelProgram, combiner_description.PixelPipeline, pixel_hlsl)
		: CombinerShader_Generate(combiner_description, COMBINER_SHADER_TARGET_D3D11, pixel_hlsl);
	if (!wrote_pixel) {
		Note_Refusal("the pixel half would not generate this description");
		Refuse(key, REFUSED_NO_PROGRAM);
		return false;
	}

	Dump_Program(key, vertex_hlsl, pixel_hlsl);

	D3D11_INPUT_ELEMENT_DESC elements[MAXIMUM_LAYOUT_ELEMENTS];
	unsigned element_count = 0;
	unsigned stride = 0;
	if (!DX11Layout_From_FVF(VertexFormat, elements, element_count, stride)) {
		Refuse(key, REFUSED_NO_LAYOUT);
		return false;
	}

	ID3DBlob * vertex_code = NULL;
	ID3DBlob * pixel_code = NULL;
	ID3DBlob * errors = NULL;
	if (FAILED(compiler(vertex_hlsl.c_str(), vertex_hlsl.size(), "ffvertex", NULL, NULL,
			ENTRY_POINT, VERTEX_PROFILE, 0, 0, &vertex_code, &errors))) {
		Record_Compiler_Error("ffvertex", errors);
		Refuse(key, REFUSED_NO_PROGRAM);
		return false;
	}
	if (errors != NULL) {
		errors->Release();
		errors = NULL;
	}
	if (FAILED(compiler(pixel_hlsl.c_str(), pixel_hlsl.size(), "ffshader", NULL, NULL,
			ENTRY_POINT, PIXEL_PROFILE, 0, 0, &pixel_code, &errors))) {
		Record_Compiler_Error("ffshader", errors);
		vertex_code->Release();
		Refuse(key, REFUSED_NO_PROGRAM);
		return false;
	}
	if (errors != NULL) {
		errors->Release();
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
		Note_Refusal("the device would not make the shaders or the input layout");
		Refuse(key, REFUSED_NO_OBJECT);
		return false;
	}

	Pipelines[key] = built;
	++PipelinesBuilt;
	pipeline = built;
	Record_Use(key);
	return true;
}

void DX11BackendClass::Record_Use(const std::string & key)
{
	PipelineUse & use = PipelineUses[key];
	if (use.Draws++ != 0) {
		return;
	}

	use.TextureWidth = 0;
	use.TextureHeight = 0;
	if (Textures[0] == NULL) {
		return;
	}

	ID3D11Resource * resource = NULL;
	Textures[0]->GetResource(&resource);
	if (resource == NULL) {
		return;
	}

	ID3D11Texture2D * texture = NULL;
	if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&texture))) {
		D3D11_TEXTURE2D_DESC description;
		texture->GetDesc(&description);
		use.TextureWidth = description.Width;
		use.TextureHeight = description.Height;
		texture->Release();
	}
	resource->Release();
}

unsigned DX11BackendClass::Pipeline_Report_Count() const
{
	return (unsigned)PipelineUses.size();
}

const char * DX11BackendClass::Pipeline_Report(unsigned index)
{
	std::map<std::string, PipelineUse>::const_iterator entry = PipelineUses.begin();
	for (unsigned step = 0; step < index && entry != PipelineUses.end(); ++step) {
		++entry;
	}
	if (entry == PipelineUses.end()) {
		return "";
	}

	char tail[96];
	snprintf(tail, sizeof(tail), " -> %llu draws, stage 0 texture %ux%u",
		entry->second.Draws, entry->second.TextureWidth, entry->second.TextureHeight);
	ReportLine = entry->first + tail;
	return ReportLine.c_str();
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
	vertex_block.ViewportInverse[0] = 1.0f / static_cast<float>(ViewportWidth);
	vertex_block.ViewportInverse[1] = 1.0f / static_cast<float>(ViewportHeight);

	memcpy(vertex_block.TextureMatrix, TextureTransforms, sizeof(vertex_block.TextureMatrix));

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
		transform_point(Lights[index].Position, View, vertex_block.LightFields[slot][0]);
		transform_direction(Lights[index].Direction, View, vertex_block.LightFields[slot][1]);
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

	// A transcribed program reads the engine's own register bank instead of the block above, so
	// that bank goes up only on the draws that take one.
	if (VertexProgram != ENGINE_SHADER_NONE
		&& SUCCEEDED(context->Map(EngineConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		memcpy(mapped.pData, EngineConstants, sizeof(EngineConstants));
		context->Unmap(EngineConstantBuffer, 0);
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
		// A null blend state is not a refused draw: the draw goes ahead with the default state,
		// which blends nothing, so a pass that was multiplying itself into the frame buffer paints
		// over it instead.  That is invisible in every count, hence the line.
		Note_Refusal("the device refused a blend state");
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
		Note_Refusal("the device refused a depth stencil state");
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
		Note_Refusal("the device refused a rasterizer state");
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
		Note_Refusal("the device refused a sampler state");
		return NULL;
	}
	SamplerStates[key] = state;
	return state;
}

ID3D11Buffer * DX11BackendClass::Vertex_Constants() const
{
	return (VertexProgram != ENGINE_SHADER_NONE) ? EngineConstantBuffer : VertexConstantBuffer;
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
	return Draw_Indexed(index_count, start_index, base_vertex,
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

bool DX11BackendClass::Draw_Indexed_Strip(unsigned index_count, unsigned start_index,
	unsigned base_vertex)
{
	return Draw_Indexed(index_count, start_index, base_vertex,
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
}

bool DX11BackendClass::Draw_Indexed(unsigned index_count, unsigned start_index,
	unsigned base_vertex, D3D11_PRIMITIVE_TOPOLOGY topology)
{
	Pipeline pipeline;
	if (StreamBuffer == NULL || IndexBuffer == NULL) {
		++RefusedNoBuffer;
		++DrawsRefused;
		return false;
	}
	if (ForeignPixelShader || ForeignVertexShader) {
		Note_Foreign_Refusal();
		++RefusedForeignShader;
		++DrawsRefused;
		return false;
	}
	if (Any_Missing_Texture()) {
		++RefusedNoTexture;
		++DrawsRefused;
		return false;
	}
	if (!Resolve(pipeline)) {
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
	context->IASetPrimitiveTopology(topology);
	context->VSSetShader(pipeline.VertexShader, NULL, 0);
	ID3D11Buffer * vertex_constants = Vertex_Constants();
	context->VSSetConstantBuffers(0, 1, &vertex_constants);
	context->PSSetShader(pipeline.PixelShader, NULL, 0);
	context->PSSetConstantBuffers(0, 1, &PixelConstantBuffer);
	context->DrawIndexed(index_count, start_index, base_vertex);

	++DrawsMade;
	if (CurrentTarget != NULL) {
		++DrawsIntoTargets;
		MaskWhileTargeted |= RenderStates.Get_Render_State(D3DRS_COLORWRITEENABLE);
	}
	return true;
}

bool DX11BackendClass::Draw_Triangles(unsigned vertex_count, unsigned start_vertex)
{
	Pipeline pipeline;
	if (StreamBuffer == NULL) {
		++RefusedNoBuffer;
		++DrawsRefused;
		return false;
	}
	if (ForeignPixelShader || ForeignVertexShader) {
		Note_Foreign_Refusal();
		++RefusedForeignShader;
		++DrawsRefused;
		return false;
	}
	if (Any_Missing_Texture()) {
		++RefusedNoTexture;
		++DrawsRefused;
		return false;
	}
	if (!Resolve(pipeline)) {
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
	ID3D11Buffer * vertex_constants = Vertex_Constants();
	context->VSSetConstantBuffers(0, 1, &vertex_constants);
	context->PSSetShader(pipeline.PixelShader, NULL, 0);
	context->PSSetConstantBuffers(0, 1, &PixelConstantBuffer);
	context->Draw(vertex_count, start_vertex);

	++DrawsMade;
	if (CurrentTarget != NULL) {
		++DrawsIntoTargets;
		MaskWhileTargeted |= RenderStates.Get_Render_State(D3DRS_COLORWRITEENABLE);
	}
	return true;
}

// What is actually in the texture bound at a stage, read back through a staging copy.  A draw that
// samples a texture nobody filled looks exactly like a draw that never happened, and no count tells
// them apart.  This costs a full stall and is only ever called once.
std::string DX11BackendClass::Texture_Average(unsigned stage)
{
	if (stage >= DX11_BACKEND_TEXTURE_STAGES || Textures[stage] == NULL) {
		return "no texture";
	}

	ID3D11Resource * resource = NULL;
	Textures[stage]->GetResource(&resource);
	if (resource == NULL) {
		return "no resource";
	}

	ID3D11Texture2D * texture = NULL;
	if (FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&texture))) {
		resource->Release();
		return "not a 2D texture";
	}

	D3D11_TEXTURE2D_DESC description;
	texture->GetDesc(&description);
	description.Usage = D3D11_USAGE_STAGING;
	description.BindFlags = 0;
	description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	description.MiscFlags = 0;
	description.MipLevels = 1;

	ID3D11Texture2D * staging = NULL;
	if (FAILED(Device->Get_Device()->CreateTexture2D(&description, NULL, &staging))) {
		texture->Release();
		resource->Release();
		return "no staging copy";
	}

	Device->Get_Context()->CopySubresourceRegion(staging, 0, 0, 0, 0, resource, 0, NULL);

	std::string answer = "unreadable";
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (SUCCEEDED(Device->Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
		double blue = 0.0, green = 0.0, red = 0.0, alpha = 0.0;
		const unsigned pixels = description.Width * description.Height;
		for (unsigned row = 0; row < description.Height; ++row) {
			const unsigned char * line = (const unsigned char *)mapped.pData + row * mapped.RowPitch;
			for (unsigned column = 0; column < description.Width; ++column) {
				blue  += line[column * 4 + 0];
				green += line[column * 4 + 1];
				red   += line[column * 4 + 2];
				alpha += line[column * 4 + 3];
			}
		}
		Device->Get_Context()->Unmap(staging, 0);

		char line[128];
		snprintf(line, sizeof(line), "%ux%u average rgba %.0f,%.0f,%.0f,%.0f",
			description.Width, description.Height, red / pixels, green / pixels, blue / pixels,
			alpha / pixels);
		answer = line;
	}

	staging->Release();
	texture->Release();
	resource->Release();
	return answer;
}

// The screen-space quads the engine draws with DrawPrimitiveUP - the filter that puts the rendered
// scene back on the screen, the smudges, the shadow volume's darkening pass - come as four
// vertices in a strip and no buffer at all.  D3D11 has no equivalent, so they are copied into a
// dynamic buffer the backend owns and drawn as a list, which is also what turns the strip's
// alternating winding into triangles.
bool DX11BackendClass::Draw_User_Strip(const void * vertices, unsigned primitive_count,
	unsigned stride)
{
	if (vertices == NULL || primitive_count == 0 || stride == 0) {
		return false;
	}

	Pipeline pipeline;
	if (ForeignPixelShader || ForeignVertexShader) {
		Note_Foreign_Refusal();
		++RefusedForeignShader;
		++DrawsRefused;
		return false;
	}
	if (Any_Missing_Texture()) {
		++RefusedNoTexture;
		++DrawsRefused;
		return false;
	}

	// Not the first quad of the run: that one is the loading screen's, drawn before anything has
	// been rendered into the texture it samples, and reading it back proves nothing.
	const unsigned long long DRAWS_BEFORE_TRACING = 50000;
	const DWORD SCREEN_QUAD_FVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
	if (!TracedUserStrip && DrawsMade > DRAWS_BEFORE_TRACING && Textures[0] != NULL
		&& VertexFormat == SCREEN_QUAD_FVF) {
		TracedUserStrip = true;
		const float * corner = static_cast<const float *>(vertices);
		char line[192];
		snprintf(line, sizeof(line),
			"quad %.0f,%.0f fvf %lu viewport %ux%u, colour write %lu, into targets %lu, stage 0 %s",
			corner[0], corner[1], VertexFormat, ViewportWidth, ViewportHeight,
			(unsigned long)RenderStates.Get_Render_State(D3DRS_COLORWRITEENABLE),
			(unsigned long)MaskWhileTargeted, Texture_Average(0).c_str());
		Diagnostic = line;
	}

	const unsigned list_vertex_count = primitive_count * 3;
	const unsigned byte_count = list_vertex_count * stride;
	if (!Reserve_User_Buffer(byte_count)) {
		++RefusedNoBuffer;
		++DrawsRefused;
		return false;
	}

	ID3D11DeviceContext * context = Device->Get_Context();
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(context->Map(UserBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		++RefusedNoBuffer;
		++DrawsRefused;
		return false;
	}

	const unsigned char * source = static_cast<const unsigned char *>(vertices);
	unsigned char * destination = static_cast<unsigned char *>(mapped.pData);
	for (unsigned primitive = 0; primitive < primitive_count; ++primitive) {
		// A strip alternates winding: the odd triangle takes its first two vertices the other way
		// round, which is what keeps every triangle facing the same way.
		const unsigned order[3] = {
			(primitive % 2 == 0) ? primitive : primitive + 1,
			(primitive % 2 == 0) ? primitive + 1 : primitive,
			primitive + 2 };
		for (unsigned corner = 0; corner < 3; ++corner) {
			memcpy(destination + (primitive * 3 + corner) * stride,
				source + order[corner] * stride, stride);
		}
	}
	context->Unmap(UserBuffer, 0);

	// The pipeline is resolved after the copy so a refusal does not leave a mapped buffer behind.
	if (!Resolve(pipeline)) {
		++DrawsRefused;
		return false;
	}

	Upload_Constants();
	Bind_State_Objects();

	const UINT vertex_stride = stride;
	const UINT offset = 0;
	context->IASetInputLayout(pipeline.Layout);
	context->IASetVertexBuffers(0, 1, &UserBuffer, &vertex_stride, &offset);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(pipeline.VertexShader, NULL, 0);
	ID3D11Buffer * vertex_constants = Vertex_Constants();
	context->VSSetConstantBuffers(0, 1, &vertex_constants);
	context->PSSetShader(pipeline.PixelShader, NULL, 0);
	context->PSSetConstantBuffers(0, 1, &PixelConstantBuffer);
	context->Draw(list_vertex_count, 0);

	++DrawsMade;
	if (CurrentTarget != NULL) {
		++DrawsIntoTargets;
		MaskWhileTargeted |= RenderStates.Get_Render_State(D3DRS_COLORWRITEENABLE);
	}
	return true;
}

bool DX11BackendClass::Reserve_User_Buffer(unsigned byte_count)
{
	if (UserBuffer != NULL && UserBufferBytes >= byte_count) {
		return true;
	}

	if (UserBuffer != NULL) {
		UserBuffer->Release();
		UserBuffer = NULL;
		UserBufferBytes = 0;
	}

	if (!DX11Resource_Create_Vertex_Buffer(Device->Get_Device(), byte_count, D3DPOOL_DEFAULT,
			D3DUSAGE_DYNAMIC, NULL, &UserBuffer)) {
		Note_Refusal("the device refused the buffer the screen quads are copied into");
		return false;
	}
	UserBufferBytes = byte_count;
	return true;
}

// The first thing the compiler complained about, kept whole.  A generated program that will not
// compile is a defect in the generator, and the message names the line of it.
void DX11BackendClass::Record_Compiler_Error(const char * which, ID3DBlob * errors)
{
	if (!CompilerError.empty()) {
		return;
	}
	CompilerError = which;
	CompilerError += ": ";
	if (errors != NULL) {
		CompilerError.append((const char *)errors->GetBufferPointer(), errors->GetBufferSize());
		errors->Release();
	}
	else {
		CompilerError += "no message";
	}
}

unsigned DX11BackendClass::Refused_Description_Count() const
{
	return (unsigned)RefusedPipelines.size();
}

const char * DX11BackendClass::Refused_Description(unsigned index) const
{
	std::map<std::string, unsigned>::const_iterator entry = RefusedPipelines.begin();
	for (unsigned step = 0; step < index && entry != RefusedPipelines.end(); ++step) {
		++entry;
	}
	return entry == RefusedPipelines.end() ? "" : entry->first.c_str();
}

// The first refusal that was not the compiler's doing, in the same field the compiler's message
// uses: a generator that declines a description and a device that declines the program it produced
// are different defects and the count alone tells them apart from nothing.
void DX11BackendClass::Note_Refusal(const char * reason)
{
	if (CompilerError.empty()) {
		CompilerError = reason;
	}
}

void DX11BackendClass::Refuse(const std::string & key, RefusalReason reason)
{
	RefusedPipelines[key] = reason;
	switch (reason) {
	case REFUSED_NO_STAGE:   ++RefusedNoStage; break;
	case REFUSED_NO_LAYOUT:  ++RefusedNoLayout; break;
	case REFUSED_NO_PROGRAM: ++RefusedNoProgram; break;
	case REFUSED_NO_OBJECT:  ++RefusedNoObject; break;
	}
}

void DX11BackendClass::Refusals(unsigned long long & no_buffer, unsigned long long & no_stage,
	unsigned long long & no_layout, unsigned long long & no_program,
	unsigned long long & no_object, unsigned long long & foreign_shader,
	unsigned long long & no_texture) const
{
	no_buffer = RefusedNoBuffer;
	no_stage = RefusedNoStage;
	no_layout = RefusedNoLayout;
	no_program = RefusedNoProgram;
	no_object = RefusedNoObject;
	foreign_shader = RefusedForeignShader;
	no_texture = RefusedNoTexture;
}

void DX11BackendClass::Statistics(unsigned & pipelines_built, unsigned long long & draws_made,
	unsigned long long & draws_refused) const
{
	pipelines_built = PipelinesBuilt;
	draws_made = DrawsMade;
	draws_refused = DrawsRefused;
}
