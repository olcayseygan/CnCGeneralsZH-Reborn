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

#include "dx11post.h"

#include "dx11device.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <windows.h>

typedef HRESULT (WINAPI *D3DCompileFunction)(LPCVOID source_data, SIZE_T source_size,
	LPCSTR source_name, const D3D_SHADER_MACRO * defines, ID3DInclude * include,
	LPCSTR entry_point, LPCSTR target, UINT flags1, UINT flags2, ID3DBlob ** code,
	ID3DBlob ** error_messages);

// The same hand binding dx11backend.cpp does, and deliberately its own copy: this is a separate
// static library and linking it against the backend to share twelve lines would drag the whole
// state translation in behind them.
static const char * const COMPILER_MODULE = "d3dcompiler_47.dll";
static const char * const ENTRY_POINT = "main";
static const char * const VERTEX_PROFILE = "vs_4_0";
static const char * const PIXEL_PROFILE = "ps_4_0";

static const unsigned FULL_SCREEN_VERTEX_COUNT = 3;

// The swap chain's own format for the eight bit stages, so the last pass in a chain writes what it
// reads and nothing converts anything on the way out.
static const DXGI_FORMAT EIGHT_BIT_FORMAT = DXGI_FORMAT_B8G8R8A8_UNORM;

// Half floats rather than full ones: the scene is a colour, sixteen bits of it are more range than
// this game's art can fill, and it halves the bandwidth of every pass over the frame.
static const DXGI_FORMAT FLOAT_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;

// Bloom runs at a quarter of the window on each axis.  A glow is low frequency by definition, so
// the resolution it is computed at is the one thing about it nobody can see, and a quarter costs a
// sixteenth of the samples.
static const unsigned BLOOM_DIVISOR = 4;

// Two horizontal and two vertical, which at a quarter resolution reaches about sixteen pixels of
// the finished frame.  A third pair was tried and the glow got wider without getting better.
static const unsigned BLOOM_BLUR_PASSES = 2;

// What counts as bright enough to bleed, what the bleed is worth when it is added back, and where
// the tone curve starts bending.
//
// A threshold of one means brighter than white, which is a thing an eight bit target could not have
// said at all, and it was worth checking that the game ever gets there rather than assuming it.
// Eight inferno cannons firing into a column does not: at a threshold of one the frame comes back
// unchanged even with the intensity at four, because a fire a few pixels across never stacks enough
// additive particles to pass white.  Thirty of them does, and at the same threshold of one the
// fireballs come back with white centres and a glow on the ground around them.  So the float target
// earns its place, and the threshold stays where it is: a lower one blooms pale buildings and the
// sky, which is the look this is trying not to have.
//
// An intensity of 1.5 against a quiet frame with nothing burning costs half a level a channel,
// which is the cost of having this on when there is nothing for it to do.
static const float BLOOM_THRESHOLD = 1.0f;
static const float BLOOM_INTENSITY = 1.5f;
static const float TONE_CURVE_KNEE = 0.8f;

struct PostConstantBlock
{
	// One over the size of the texture being sampled, and its size.  Per pass rather than per
	// frame, because the bloom passes do not run at the size of the window.
	float TexelSize[4];

	// The step a separable blur takes, already in texture coordinates.
	float BlurDirection[4];

	// Threshold, intensity, knee.
	float Tuning[4];
};

// A triangle big enough to cover the screen, made out of nothing.  No vertex buffer and no input
// layout: SV_VertexID walks 0, 1, 2 and the arithmetic turns them into (0,0), (2,0), (0,2) in
// texture coordinates, whose triangle covers the whole of the unit square.
static const char * const VERTEX_SHADER_SOURCE =
	"struct VertexOutput\n"
	"{\n"
	"    float4 Position : SV_POSITION;\n"
	"    float2 Texture : TEXCOORD0;\n"
	"};\n"
	"\n"
	"VertexOutput main(uint index : SV_VertexID)\n"
	"{\n"
	"    VertexOutput output;\n"
	"    output.Texture = float2((index << 1) & 2, index & 2);\n"
	"    output.Position = float4(output.Texture.x * 2.0 - 1.0,\n"
	"        1.0 - output.Texture.y * 2.0, 0.0, 1.0);\n"
	"    return output;\n"
	"}\n";

static const char * const PIXEL_SHADER_PROLOGUE =
	"struct VertexOutput\n"
	"{\n"
	"    float4 Position : SV_POSITION;\n"
	"    float2 Texture : TEXCOORD0;\n"
	"};\n"
	"\n"
	"Texture2D Source : register(t0);\n"
	"Texture2D Extra : register(t1);\n"
	"SamplerState Sampler : register(s0);\n"
	"\n"
	"cbuffer PostConstants : register(b0)\n"
	"{\n"
	"    float4 TexelSize;\n"
	"    float4 BlurDirection;\n"
	"    float4 Tuning;\n"
	"};\n"
	"\n"
	// Below the knee nothing moves, so a frame with nothing overbright in it comes back out as the
	// frame that went in and the tone curve costs the picture nothing.  From the knee upward every
	// value between it and infinity is folded into the gap between the knee and white, which is
	// what an eight bit buffer could not do: there, everything over white was white.
	"float3 tone_curve(float3 colour)\n"
	"{\n"
	"    float knee = Tuning.z;\n"
	"    float headroom = max(1.0 - knee, 0.0001);\n"
	"    float3 excess = max(colour - knee, 0.0);\n"
	"    return min(colour, knee) + headroom * (1.0 - exp(-excess / headroom));\n"
	"}\n"
	"\n";

static const char * const COPY_SHADER_BODY =
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    return Source.Sample(Sampler, input.Texture);\n"
	"}\n";

static const char * const TONE_MAP_SHADER_BODY =
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    return float4(tone_curve(Source.Sample(Sampler, input.Texture).rgb), 1.0);\n"
	"}\n";

// FXAA, the 2009 shape of it: read the luminance of the four diagonal neighbours and the centre,
// leave the pixel alone unless the spread between them says an edge runs through it, work out which
// way the edge lies from which pair of corners is brighter, and average along it.  Two samples give
// the cheap answer and four the better one; if the four sample answer has strayed outside the
// luminance range the neighbourhood had, it has blurred across something and the two sample answer
// is taken instead.
//
// It runs on the finished world with no depth and no motion vectors, which is why it can sit here
// at all.  What it cannot do is recover an edge the frame never had: a thin wire drawn into one
// pixel column stays one pixel wide.
static const char * const FXAA_SHADER_BODY =
	"static const float3 LUMINANCE_WEIGHTS = float3(0.299, 0.587, 0.114);\n"
	"static const float EDGE_THRESHOLD = 0.125;\n"
	"static const float EDGE_THRESHOLD_MINIMUM = 0.0416;\n"
	"static const float SPAN_MAXIMUM = 8.0;\n"
	"static const float REDUCE_SCALE = 0.125;\n"
	"static const float REDUCE_MINIMUM = 0.0078125;\n"
	"\n"
	"float luminance(float3 colour)\n"
	"{\n"
	"    return dot(colour, LUMINANCE_WEIGHTS);\n"
	"}\n"
	"\n"
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float2 texel = TexelSize.xy;\n"
	"    float3 centre = Source.Sample(Sampler, input.Texture).rgb;\n"
	"    float3 northWest = Source.Sample(Sampler, input.Texture + float2(-texel.x, -texel.y)).rgb;\n"
	"    float3 northEast = Source.Sample(Sampler, input.Texture + float2(texel.x, -texel.y)).rgb;\n"
	"    float3 southWest = Source.Sample(Sampler, input.Texture + float2(-texel.x, texel.y)).rgb;\n"
	"    float3 southEast = Source.Sample(Sampler, input.Texture + float2(texel.x, texel.y)).rgb;\n"
	"\n"
	"    float centreLuminance = luminance(centre);\n"
	"    float northWestLuminance = luminance(northWest);\n"
	"    float northEastLuminance = luminance(northEast);\n"
	"    float southWestLuminance = luminance(southWest);\n"
	"    float southEastLuminance = luminance(southEast);\n"
	"\n"
	"    float lowest = min(centreLuminance, min(min(northWestLuminance, northEastLuminance),\n"
	"        min(southWestLuminance, southEastLuminance)));\n"
	"    float highest = max(centreLuminance, max(max(northWestLuminance, northEastLuminance),\n"
	"        max(southWestLuminance, southEastLuminance)));\n"
	"    float spread = highest - lowest;\n"
	"    if (spread < max(EDGE_THRESHOLD_MINIMUM, highest * EDGE_THRESHOLD)) {\n"
	"        return float4(centre, 1.0);\n"
	"    }\n"
	"\n"
	"    float2 direction;\n"
	"    direction.x = -((northWestLuminance + northEastLuminance)\n"
	"        - (southWestLuminance + southEastLuminance));\n"
	"    direction.y = (northWestLuminance + southWestLuminance)\n"
	"        - (northEastLuminance + southEastLuminance);\n"
	"\n"
	"    float reduction = max((northWestLuminance + northEastLuminance + southWestLuminance\n"
	"        + southEastLuminance) * 0.25 * REDUCE_SCALE, REDUCE_MINIMUM);\n"
	"    float shortest = 1.0 / (min(abs(direction.x), abs(direction.y)) + reduction);\n"
	"    direction = clamp(direction * shortest, -SPAN_MAXIMUM, SPAN_MAXIMUM) * texel;\n"
	"\n"
	"    float3 inner = 0.5 * (Source.Sample(Sampler, input.Texture + direction * (1.0 / 3.0 - 0.5)).rgb\n"
	"        + Source.Sample(Sampler, input.Texture + direction * (2.0 / 3.0 - 0.5)).rgb);\n"
	"    float3 outer = inner * 0.5\n"
	"        + 0.25 * (Source.Sample(Sampler, input.Texture + direction * -0.5).rgb\n"
	"        + Source.Sample(Sampler, input.Texture + direction * 0.5).rgb);\n"
	"\n"
	"    float outerLuminance = luminance(outer);\n"
	"    float3 result = (outerLuminance < lowest || outerLuminance > highest) ? inner : outer;\n"
	"    return float4(result, 1.0);\n"
	"}\n";

// Unsharp mask: the pixel plus what it has that a blur of its neighbours does not.  The amount is
// low on purpose.  This runs over the world and the world alone, but the world holds thin wires and
// aerials as well as ground, and anything stronger rings around them.
static const char * const SHARPEN_SHADER_BODY =
	"static const float SHARPEN_AMOUNT = 0.35;\n"
	"\n"
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float2 texel = TexelSize.xy;\n"
	"    float4 centre = Source.Sample(Sampler, input.Texture);\n"
	"    float3 neighbours = Source.Sample(Sampler, input.Texture + float2(-texel.x, 0.0)).rgb\n"
	"        + Source.Sample(Sampler, input.Texture + float2(texel.x, 0.0)).rgb\n"
	"        + Source.Sample(Sampler, input.Texture + float2(0.0, -texel.y)).rgb\n"
	"        + Source.Sample(Sampler, input.Texture + float2(0.0, texel.y)).rgb;\n"
	"    float3 sharpened = centre.rgb + (centre.rgb - neighbours * 0.25) * SHARPEN_AMOUNT;\n"
	"    return float4(saturate(sharpened), centre.a);\n"
	"}\n";

// The bright pass, downsampling as it goes: four taps of the full size frame averaged into one
// quarter size texel, then the threshold taken off what is left.  What survives is the amount by
// which something was brighter than white, which is a quantity an eight bit scene target could not
// have held.
static const char * const BLOOM_EXTRACT_SHADER_BODY =
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float2 texel = TexelSize.xy;\n"
	"    float3 sum = Source.Sample(Sampler, input.Texture + float2(-texel.x, -texel.y)).rgb\n"
	"        + Source.Sample(Sampler, input.Texture + float2(texel.x, -texel.y)).rgb\n"
	"        + Source.Sample(Sampler, input.Texture + float2(-texel.x, texel.y)).rgb\n"
	"        + Source.Sample(Sampler, input.Texture + float2(texel.x, texel.y)).rgb;\n"
	"    return float4(max(sum * 0.25 - Tuning.x, 0.0), 1.0);\n"
	"}\n";

// One half of a separable gaussian.  The direction comes in as a constant, so the same program is
// the horizontal pass and the vertical one.
static const char * const BLOOM_BLUR_SHADER_BODY =
	"static const float WEIGHTS[5] =\n"
	"    { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };\n"
	"\n"
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float2 step = BlurDirection.xy;\n"
	"    float3 sum = Source.Sample(Sampler, input.Texture).rgb * WEIGHTS[0];\n"
	"    for (int index = 1; index < 5; ++index) {\n"
	"        float2 offset = step * index;\n"
	"        sum += Source.Sample(Sampler, input.Texture + offset).rgb * WEIGHTS[index];\n"
	"        sum += Source.Sample(Sampler, input.Texture - offset).rgb * WEIGHTS[index];\n"
	"    }\n"
	"    return float4(sum, 1.0);\n"
	"}\n";

// The scene and the glow added, then the tone curve, then eight bits.  One pass rather than two
// because the addition is what makes a value overbright and the curve is what brings it back, and
// splitting them would mean writing the sum into a float target nobody else reads.
static const char * const BLOOM_COMPOSITE_SHADER_BODY =
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float3 scene = Source.Sample(Sampler, input.Texture).rgb;\n"
	"    float3 glow = Extra.Sample(Sampler, input.Texture).rgb;\n"
	"    return float4(tone_curve(scene + glow * Tuning.y), 1.0);\n"
	"}\n";

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

static void release_interface(IUnknown ** object)
{
	if (*object != NULL) {
		(*object)->Release();
		*object = NULL;
	}
}

static const char * simple_effect_body(DX11PostEffect effect)
{
	switch (effect) {
	case DX11_POST_FXAA:
		return FXAA_SHADER_BODY;
	case DX11_POST_SHARPEN:
		return SHARPEN_SHADER_BODY;
	default:
		return COPY_SHADER_BODY;
	}
}

const char * DX11Post_Effect_Name(DX11PostEffect effect)
{
	switch (effect) {
	case DX11_POST_FXAA:
		return "fxaa";
	case DX11_POST_SHARPEN:
		return "sharpen";
	case DX11_POST_BLOOM:
		return "bloom";
	default:
		return "copy";
	}
}

bool DX11Post_Parse_Chain(const char * text, DX11PostEffect effects[DX11_POST_CHAIN_LIMIT],
	unsigned & count)
{
	count = 0;
	if (text == NULL) {
		return false;
	}

	std::string remaining(text);
	while (!remaining.empty()) {
		const std::string::size_type separator = remaining.find(',');
		std::string name = remaining.substr(0, separator);
		remaining = (separator == std::string::npos) ? std::string() : remaining.substr(separator + 1);

		if (name.empty()) {
			continue;
		}
		if (name == "off" || name == "none") {
			count = 0;
			return true;
		}
		if (count == DX11_POST_CHAIN_LIMIT) {
			count = 0;
			return false;
		}

		if (name == "copy") {
			effects[count++] = DX11_POST_COPY;
		}
		else if (name == "fxaa") {
			effects[count++] = DX11_POST_FXAA;
		}
		else if (name == "sharpen") {
			effects[count++] = DX11_POST_SHARPEN;
		}
		else if (name == "bloom") {
			// Bloom decides the format the scene is kept in and hands the frame back in eight bits,
			// so a chain cannot reach it after something else has already read the scene. Refusing
			// beats quietly reordering: "fxaa,bloom" is a request somebody meant, and it is not one
			// this can answer.
			if (count != 0) {
				count = 0;
				return false;
			}
			effects[count++] = DX11_POST_BLOOM;
		}
		else {
			count = 0;
			return false;
		}
	}
	return true;
}

DX11PostProcessClass::DX11PostProcessClass()
	: Device(NULL)
	, VertexShader(NULL)
	, ToneMapShader(NULL)
	, BloomExtractShader(NULL)
	, BloomBlurShader(NULL)
	, BloomCompositeShader(NULL)
	, Sampler(NULL)
	, BlendState(NULL)
	, DepthState(NULL)
	, RasterizerState(NULL)
	, ConstantBuffer(NULL)
	, ChainLength(0)
	, Width(0)
	, Height(0)
	, BloomWidth(0)
	, BloomHeight(0)
	, TargetsAreFloat(false)
	, ShadersReady(false)
	, Resolved(false)
{
	memset(PixelShaders, 0, sizeof(PixelShaders));
	memset(&SceneTarget, 0, sizeof(SceneTarget));
	memset(ChainTargets, 0, sizeof(ChainTargets));
	memset(BloomTargets, 0, sizeof(BloomTargets));
	memset(Chain, 0, sizeof(Chain));
	strcpy(Diagnostic, "post-process off");
}

DX11PostProcessClass::~DX11PostProcessClass()
{
	Shutdown();
}

bool DX11PostProcessClass::Initialise(DX11DeviceClass * device)
{
	Shutdown();
	Device = device;
	if (Device == NULL || Device->Get_Device() == NULL) {
		strcpy(Diagnostic, "post-process off: no device");
		return false;
	}

	ShadersReady = Create_Shaders() && Create_States();
	if (!ShadersReady) {
		Shutdown();
		strcpy(Diagnostic, "post-process off: the compiler refused the passes");
		return false;
	}

	Describe();
	return true;
}

void DX11PostProcessClass::Shutdown()
{
	Release_Targets();
	release_interface(reinterpret_cast<IUnknown **>(&ConstantBuffer));
	release_interface(reinterpret_cast<IUnknown **>(&RasterizerState));
	release_interface(reinterpret_cast<IUnknown **>(&DepthState));
	release_interface(reinterpret_cast<IUnknown **>(&BlendState));
	release_interface(reinterpret_cast<IUnknown **>(&Sampler));
	release_interface(reinterpret_cast<IUnknown **>(&BloomCompositeShader));
	release_interface(reinterpret_cast<IUnknown **>(&BloomBlurShader));
	release_interface(reinterpret_cast<IUnknown **>(&BloomExtractShader));
	release_interface(reinterpret_cast<IUnknown **>(&ToneMapShader));
	for (unsigned index = 0; index < DX11_POST_SIMPLE_EFFECTS; ++index) {
		release_interface(reinterpret_cast<IUnknown **>(&PixelShaders[index]));
	}
	release_interface(reinterpret_cast<IUnknown **>(&VertexShader));
	Device = NULL;
	ShadersReady = false;
	Resolved = false;
}

void DX11PostProcessClass::Set_Chain(const DX11PostEffect effects[DX11_POST_CHAIN_LIMIT],
	unsigned count)
{
	ChainLength = (count > DX11_POST_CHAIN_LIMIT) ? DX11_POST_CHAIN_LIMIT : count;
	for (unsigned index = 0; index < ChainLength; ++index) {
		Chain[index] = effects[index];
	}
	Describe();
}

bool DX11PostProcessClass::Scene_Is_Float() const
{
	return ChainLength > 0 && Chain[0] == DX11_POST_BLOOM;
}

static bool compile_pixel_shader(D3DCompileFunction compiler, ID3D11Device * device,
	const char * body, ID3D11PixelShader ** shader)
{
	const std::string source = std::string(PIXEL_SHADER_PROLOGUE) + body;
	ID3DBlob * code = NULL;
	ID3DBlob * errors = NULL;
	HRESULT result = compiler(source.c_str(), source.size(), "dx11post.ps", NULL, NULL, ENTRY_POINT,
		PIXEL_PROFILE, 0, 0, &code, &errors);
	release_interface(reinterpret_cast<IUnknown **>(&errors));
	if (FAILED(result) || code == NULL) {
		release_interface(reinterpret_cast<IUnknown **>(&code));
		return false;
	}
	result = device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), NULL, shader);
	release_interface(reinterpret_cast<IUnknown **>(&code));
	return SUCCEEDED(result);
}

bool DX11PostProcessClass::Create_Shaders()
{
	D3DCompileFunction compiler = compiler_function();
	if (compiler == NULL) {
		return false;
	}

	ID3DBlob * code = NULL;
	ID3DBlob * errors = NULL;
	HRESULT result = compiler(VERTEX_SHADER_SOURCE, strlen(VERTEX_SHADER_SOURCE), "dx11post.vs",
		NULL, NULL, ENTRY_POINT, VERTEX_PROFILE, 0, 0, &code, &errors);
	release_interface(reinterpret_cast<IUnknown **>(&errors));
	if (FAILED(result) || code == NULL) {
		release_interface(reinterpret_cast<IUnknown **>(&code));
		return false;
	}
	result = Device->Get_Device()->CreateVertexShader(code->GetBufferPointer(),
		code->GetBufferSize(), NULL, &VertexShader);
	release_interface(reinterpret_cast<IUnknown **>(&code));
	if (FAILED(result)) {
		return false;
	}

	ID3D11Device * device = Device->Get_Device();
	for (unsigned index = 0; index < DX11_POST_SIMPLE_EFFECTS; ++index) {
		if (!compile_pixel_shader(compiler, device,
				simple_effect_body(static_cast<DX11PostEffect>(index)), &PixelShaders[index])) {
			return false;
		}
	}

	return compile_pixel_shader(compiler, device, TONE_MAP_SHADER_BODY, &ToneMapShader)
		&& compile_pixel_shader(compiler, device, BLOOM_EXTRACT_SHADER_BODY, &BloomExtractShader)
		&& compile_pixel_shader(compiler, device, BLOOM_BLUR_SHADER_BODY, &BloomBlurShader)
		&& compile_pixel_shader(compiler, device, BLOOM_COMPOSITE_SHADER_BODY,
			&BloomCompositeShader);
}

bool DX11PostProcessClass::Create_States()
{
	ID3D11Device * device = Device->Get_Device();

	D3D11_SAMPLER_DESC sampler;
	ZeroMemory(&sampler, sizeof(sampler));
	// Bilinear, because FXAA samples between texels on purpose and the bloom passes read a smaller
	// texture than they write.  At the one to one mapping the full size passes have, a sample on a
	// texel centre still returns that texel exactly.
	sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampler.MaxLOD = D3D11_FLOAT32_MAX;
	if (FAILED(device->CreateSamplerState(&sampler, &Sampler))) {
		return false;
	}

	D3D11_BLEND_DESC blend;
	ZeroMemory(&blend, sizeof(blend));
	blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	if (FAILED(device->CreateBlendState(&blend, &BlendState))) {
		return false;
	}

	D3D11_DEPTH_STENCIL_DESC depth;
	ZeroMemory(&depth, sizeof(depth));
	if (FAILED(device->CreateDepthStencilState(&depth, &DepthState))) {
		return false;
	}

	D3D11_RASTERIZER_DESC rasterizer;
	ZeroMemory(&rasterizer, sizeof(rasterizer));
	rasterizer.FillMode = D3D11_FILL_SOLID;
	// The full screen triangle is written once and its winding is whatever the arithmetic gives; not
	// culling means the pass cannot be lost to a sign somewhere in it.
	rasterizer.CullMode = D3D11_CULL_NONE;
	rasterizer.DepthClipEnable = TRUE;
	if (FAILED(device->CreateRasterizerState(&rasterizer, &RasterizerState))) {
		return false;
	}

	D3D11_BUFFER_DESC constants;
	ZeroMemory(&constants, sizeof(constants));
	constants.ByteWidth = sizeof(PostConstantBlock);
	constants.Usage = D3D11_USAGE_DYNAMIC;
	constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	return SUCCEEDED(device->CreateBuffer(&constants, NULL, &ConstantBuffer));
}

bool DX11PostProcessClass::Create_Target(Target & target, unsigned width, unsigned height,
	DXGI_FORMAT format)
{
	ID3D11Device * device = Device->Get_Device();

	D3D11_TEXTURE2D_DESC description;
	ZeroMemory(&description, sizeof(description));
	description.Width = width;
	description.Height = height;
	description.MipLevels = 1;
	description.ArraySize = 1;
	description.Format = format;
	description.SampleDesc.Count = 1;
	description.Usage = D3D11_USAGE_DEFAULT;
	description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	if (FAILED(device->CreateTexture2D(&description, NULL, &target.Texture))) {
		return false;
	}
	if (FAILED(device->CreateRenderTargetView(target.Texture, NULL, &target.View))) {
		return false;
	}
	return SUCCEEDED(device->CreateShaderResourceView(target.Texture, NULL, &target.Resource));
}

bool DX11PostProcessClass::Create_Targets(unsigned width, unsigned height, bool floating)
{
	const DXGI_FORMAT scene_format = floating ? FLOAT_FORMAT : EIGHT_BIT_FORMAT;
	if (!Create_Target(SceneTarget, width, height, scene_format)) {
		Release_Targets();
		return false;
	}

	const unsigned chain_target_count = sizeof(ChainTargets) / sizeof(ChainTargets[0]);
	for (unsigned index = 0; index < chain_target_count; ++index) {
		if (!Create_Target(ChainTargets[index], width, height, EIGHT_BIT_FORMAT)) {
			Release_Targets();
			return false;
		}
	}

	// A window narrower than the divisor would ask for a zero wide texture, which is refused.
	BloomWidth = (width / BLOOM_DIVISOR) > 0 ? (width / BLOOM_DIVISOR) : 1;
	BloomHeight = (height / BLOOM_DIVISOR) > 0 ? (height / BLOOM_DIVISOR) : 1;
	if (floating) {
		const unsigned bloom_target_count = sizeof(BloomTargets) / sizeof(BloomTargets[0]);
		for (unsigned index = 0; index < bloom_target_count; ++index) {
			if (!Create_Target(BloomTargets[index], BloomWidth, BloomHeight, FLOAT_FORMAT)) {
				Release_Targets();
				return false;
			}
		}
	}

	Width = width;
	Height = height;
	TargetsAreFloat = floating;
	return true;
}

void DX11PostProcessClass::Release_Target(Target & target)
{
	release_interface(reinterpret_cast<IUnknown **>(&target.Resource));
	release_interface(reinterpret_cast<IUnknown **>(&target.View));
	release_interface(reinterpret_cast<IUnknown **>(&target.Texture));
}

void DX11PostProcessClass::Release_Targets()
{
	Release_Target(SceneTarget);
	const unsigned chain_target_count = sizeof(ChainTargets) / sizeof(ChainTargets[0]);
	for (unsigned index = 0; index < chain_target_count; ++index) {
		Release_Target(ChainTargets[index]);
	}
	const unsigned bloom_target_count = sizeof(BloomTargets) / sizeof(BloomTargets[0]);
	for (unsigned index = 0; index < bloom_target_count; ++index) {
		Release_Target(BloomTargets[index]);
	}
	Width = 0;
	Height = 0;
	BloomWidth = 0;
	BloomHeight = 0;
	TargetsAreFloat = false;
}

bool DX11PostProcessClass::Ensure_Buffers()
{
	if (!ShadersReady || ChainLength == 0 || Device == NULL) {
		return false;
	}

	const unsigned width = Device->Get_Width();
	const unsigned height = Device->Get_Height();
	if (width == 0 || height == 0) {
		return false;
	}

	const bool floating = Scene_Is_Float();
	if (SceneTarget.View != NULL && Width == width && Height == height
			&& TargetsAreFloat == floating) {
		return true;
	}

	// A resolution change makes the old set the wrong size, and a chain that gained or lost bloom
	// makes the scene target the wrong format.  Both are for the rest of the run, not for a frame.
	Release_Targets();
	if (!Create_Targets(width, height, floating)) {
		Release_Targets();
		strcpy(Diagnostic, "post-process off: the device refused the offscreen targets");
		return false;
	}
	Describe();
	return true;
}

ID3D11RenderTargetView * DX11PostProcessClass::Scene_View()
{
	return Ensure_Buffers() ? SceneTarget.View : NULL;
}

void DX11PostProcessClass::Begin_Frame()
{
	Resolved = false;
}

void DX11PostProcessClass::Draw_Pass(const PassSetup & pass)
{
	ID3D11DeviceContext * context = Device->Get_Context();

	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(context->Map(ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		return;
	}
	PostConstantBlock block;
	block.TexelSize[0] = 1.0f / static_cast<float>(pass.SourceWidth);
	block.TexelSize[1] = 1.0f / static_cast<float>(pass.SourceHeight);
	block.TexelSize[2] = static_cast<float>(pass.SourceWidth);
	block.TexelSize[3] = static_cast<float>(pass.SourceHeight);
	block.BlurDirection[0] = pass.BlurX;
	block.BlurDirection[1] = pass.BlurY;
	block.BlurDirection[2] = 0.0f;
	block.BlurDirection[3] = 0.0f;
	block.Tuning[0] = BLOOM_THRESHOLD;
	block.Tuning[1] = BLOOM_INTENSITY;
	block.Tuning[2] = TONE_CURVE_KNEE;
	block.Tuning[3] = 0.0f;
	memcpy(mapped.pData, &block, sizeof(block));
	context->Unmap(ConstantBuffer, 0);

	// No depth: the pass covers every pixel exactly once and a depth buffer left bound would test
	// the triangle against the scene it is reading.
	context->OMSetRenderTargets(1, &pass.Destination, NULL);

	D3D11_VIEWPORT viewport;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = static_cast<float>(pass.DestinationWidth);
	viewport.Height = static_cast<float>(pass.DestinationHeight);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	const float blend_factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	context->OMSetBlendState(BlendState, blend_factor, 0xffffffff);
	context->OMSetDepthStencilState(DepthState, 0);
	context->RSSetState(RasterizerState);

	context->IASetInputLayout(NULL);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(VertexShader, NULL, 0);
	context->PSSetShader(pass.Shader, NULL, 0);
	context->PSSetSamplers(0, 1, &Sampler);
	ID3D11ShaderResourceView * resources[2] = { pass.Source, pass.Extra };
	context->PSSetShaderResources(0, 2, resources);
	context->PSSetConstantBuffers(0, 1, &ConstantBuffer);

	context->Draw(FULL_SCREEN_VERTEX_COUNT, 0);

	// The texture this pass read is the target the next one writes, and D3D11 unbinds a resource
	// silently when it is bound both ways at once.  Letting go here says which of the two bindings
	// was meant.
	ID3D11ShaderResourceView * no_resources[2] = { NULL, NULL };
	context->PSSetShaderResources(0, 2, no_resources);
}

ID3D11ShaderResourceView * DX11PostProcessClass::Run_Bloom(ID3D11RenderTargetView * destination)
{
	const float bloom_texel_x = 1.0f / static_cast<float>(BloomWidth);
	const float bloom_texel_y = 1.0f / static_cast<float>(BloomHeight);

	PassSetup pass;
	memset(&pass, 0, sizeof(pass));

	// The bright pass reads the full size scene and writes the quarter size target, so it is the
	// one pass whose source and destination sizes differ.
	pass.Shader = BloomExtractShader;
	pass.Source = SceneTarget.Resource;
	pass.Destination = BloomTargets[0].View;
	pass.DestinationWidth = BloomWidth;
	pass.DestinationHeight = BloomHeight;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	Draw_Pass(pass);

	pass.Shader = BloomBlurShader;
	pass.SourceWidth = BloomWidth;
	pass.SourceHeight = BloomHeight;
	unsigned source = 0;
	for (unsigned iteration = 0; iteration < BLOOM_BLUR_PASSES; ++iteration) {
		for (unsigned axis = 0; axis < 2; ++axis) {
			const unsigned other = 1 - source;
			pass.Source = BloomTargets[source].Resource;
			pass.Destination = BloomTargets[other].View;
			pass.BlurX = (axis == 0) ? bloom_texel_x : 0.0f;
			pass.BlurY = (axis == 0) ? 0.0f : bloom_texel_y;
			Draw_Pass(pass);
			source = other;
		}
	}

	pass.Shader = BloomCompositeShader;
	pass.Source = SceneTarget.Resource;
	pass.Extra = BloomTargets[source].Resource;
	pass.Destination = destination;
	pass.DestinationWidth = Width;
	pass.DestinationHeight = Height;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	pass.BlurX = 0.0f;
	pass.BlurY = 0.0f;
	Draw_Pass(pass);

	return ChainTargets[0].Resource;
}

bool DX11PostProcessClass::Finish(const DX11PostEffect * effects, unsigned count)
{
	if (Resolved || count == 0 || !Ensure_Buffers()) {
		return false;
	}

	ID3D11RenderTargetView * back_buffer = Device->Get_Back_Buffer_View();
	if (back_buffer == NULL) {
		return false;
	}

	unsigned first = 0;
	ID3D11ShaderResourceView * source = SceneTarget.Resource;
	if (effects[0] == DX11_POST_BLOOM) {
		// Bloom is the only effect that reads the half float scene, and the frame is eight bits by
		// the time it hands over. If it is also the last thing in the chain it writes the screen.
		const bool alone = (count == 1);
		source = Run_Bloom(alone ? back_buffer : ChainTargets[0].View);
		if (alone) {
			Resolved = true;
			return true;
		}
		first = 1;
	}

	// Each remaining pass reads one eight bit target and writes the other, and the last one writes
	// the swap chain instead, so an odd or an even chain both end up on the screen.
	unsigned destination = (first == 0) ? 0u : 1u;
	PassSetup pass;
	memset(&pass, 0, sizeof(pass));
	pass.DestinationWidth = Width;
	pass.DestinationHeight = Height;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	for (unsigned index = first; index < count; ++index) {
		const bool last = (index + 1 == count);
		pass.Shader = PixelShaders[effects[index]];
		pass.Source = source;
		pass.Destination = last ? back_buffer : ChainTargets[destination].View;
		Draw_Pass(pass);
		source = ChainTargets[destination].Resource;
		destination = 1 - destination;
	}

	Resolved = true;
	return true;
}

bool DX11PostProcessClass::Run_Chain()
{
	return Finish(Chain, ChainLength);
}

bool DX11PostProcessClass::Copy_Through()
{
	if (Resolved || ChainLength == 0 || !Ensure_Buffers()) {
		return false;
	}

	ID3D11RenderTargetView * back_buffer = Device->Get_Back_Buffer_View();
	if (back_buffer == NULL) {
		return false;
	}

	// The tone curve rather than the copy when the scene is half float, because the swap chain
	// cannot hold what the scene target holds and a raw write would clamp it. In eight bits the
	// copy is exact, which is what the plumbing test measures.
	PassSetup pass;
	memset(&pass, 0, sizeof(pass));
	pass.Shader = TargetsAreFloat ? ToneMapShader : PixelShaders[DX11_POST_COPY];
	pass.Source = SceneTarget.Resource;
	pass.Destination = back_buffer;
	pass.DestinationWidth = Width;
	pass.DestinationHeight = Height;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	Draw_Pass(pass);

	Resolved = true;
	return true;
}

void DX11PostProcessClass::Describe()
{
	if (!ShadersReady) {
		return;
	}
	if (ChainLength == 0) {
		strcpy(Diagnostic, "post-process off");
		return;
	}

	std::string names;
	for (unsigned index = 0; index < ChainLength; ++index) {
		if (index > 0) {
			names += ",";
		}
		names += DX11Post_Effect_Name(Chain[index]);
	}
	snprintf(Diagnostic, sizeof(Diagnostic), "post-process %s over %ux%u, scene in %s", names.c_str(),
		Width, Height, TargetsAreFloat ? "half floats" : "eight bits");
}
