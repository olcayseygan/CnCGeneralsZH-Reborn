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

#include "ffvertex.h"

#include <stdio.h>

// The high half of D3DTSS_TEXCOORDINDEX is the generation mode and the low half the coordinate set.
static const DWORD COORDINATE_SET_MASK = 0xffff;

// D3DTTFF_PROJECTED sits above the count, which is the low three bits.
static const DWORD TEXTURE_TRANSFORM_COUNT_MASK = 0x07;

// The constant registers the D3D9 profile uses, in the order they are declared.  A vs_2_0 shader
// has 256 float4 registers and this uses 34 of them, so the layout is written for reading rather
// than for packing.
static const unsigned REGISTER_WORLD_VIEW_PROJECTION = 0;
static const unsigned REGISTER_WORLD_VIEW = 4;
static const unsigned REGISTER_NORMAL_TRANSFORM = 8;
static const unsigned REGISTER_TEXTURE_MATRIX_0 = 12;
static const unsigned REGISTER_TEXTURE_MATRIX_1 = 16;
static const unsigned REGISTER_MATERIAL_AMBIENT = 20;
static const unsigned REGISTER_MATERIAL_DIFFUSE = 21;
static const unsigned REGISTER_MATERIAL_SPECULAR = 22;
static const unsigned REGISTER_MATERIAL_EMISSIVE = 23;
static const unsigned REGISTER_MATERIAL_POWER = 24;
static const unsigned REGISTER_GLOBAL_AMBIENT = 25;
static const unsigned REGISTER_FOG_PARAMETERS = 26;
static const unsigned REGISTER_LIGHTS = 27;

// Six registers a light, the same six whatever type it is: where it is, which way it points, its
// diffuse and specular colours, its three attenuation terms with its range, and its cone.  A
// directional light reads two of them and a spot light reads all six, and the layout stays uniform
// so the register a light starts at is a multiplication rather than a running total over types.
static const unsigned REGISTERS_PER_LIGHT = 6;

static bool has_normal(DWORD fvf)
{
	return (fvf & D3DFVF_NORMAL) != 0;
}

static bool has_diffuse(DWORD fvf)
{
	return (fvf & D3DFVF_DIFFUSE) != 0;
}

static bool is_pretransformed(DWORD fvf)
{
	return (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
}

static unsigned texture_coordinate_set_count(DWORD fvf)
{
	return (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
}

// A material source resolves to a constant or to a vertex colour.  D3DMCS_COLOR1 reads the diffuse
// vertex colour, and only where the format carries one and D3DRS_COLORVERTEX allows it: D3D9 falls
// back to the material in both of those cases rather than reading a register that is not there.
// D3DMCS_COLOR2 is the specular vertex colour, which no format in the game carries, so a
// description asking for it is refused rather than quietly given the material instead.
static bool material_source_expression(DWORD source, const char * material_constant,
	const VertexPipelineDescription & description, std::string & expression)
{
	switch (source) {
	case D3DMCS_MATERIAL:
		expression = material_constant;
		return true;

	case D3DMCS_COLOR1:
		if (has_diffuse(description.FVF) && description.ColourVertexEnabled) {
			expression = "input.Diffuse";
		}
		else {
			expression = material_constant;
		}
		return true;

	default:
		return false;
	}
}

static void append_light(std::string & body, unsigned index, DWORD type)
{
	char line[1024];

	snprintf(line, sizeof(line),
		"    {\n"
		"        float3 to_light;\n"
		"        float attenuation;\n");
	body += line;

	if (type == D3DLIGHT_DIRECTIONAL) {
		// A directional light's direction is the way the light travels, so the vector towards it
		// is the negative of it and there is nothing to attenuate.
		snprintf(line, sizeof(line),
			"        to_light = -normalize(Light%uDirection.xyz);\n"
			"        attenuation = 1.0;\n", index);
		body += line;
	}
	else {
		// A point light and a spot light share the distance and the attenuation; the spot's cone
		// is the extra term below.
		snprintf(line, sizeof(line),
			"        float3 offset = Light%uPosition.xyz - view_position.xyz;\n"
			"        float distance = length(offset);\n"
			"        to_light = offset / max(distance, 0.0001);\n"
			"        attenuation = 1.0 / max(Light%uAttenuation.x + Light%uAttenuation.y * distance"
			" + Light%uAttenuation.z * distance * distance, 0.0001);\n"
			"        attenuation *= step(distance, Light%uAttenuation.w);\n",
			index, index, index, index, index);
		body += line;
	}

	if (type == D3DLIGHT_SPOT) {
		// D3D9's cone is a smooth falloff between the inner and the outer cosine raised to the
		// falloff power.  Light%uSpot carries cos(theta/2), cos(phi/2) and the falloff.
		snprintf(line, sizeof(line),
			"        float spot_cosine = dot(-to_light, normalize(Light%uDirection.xyz));\n"
			"        attenuation *= pow(saturate((spot_cosine - Light%uSpot.y)"
			" / max(Light%uSpot.x - Light%uSpot.y, 0.0001)), Light%uSpot.z);\n",
			index, index, index, index, index);
		body += line;
	}

	snprintf(line, sizeof(line),
		"        float lambert = max(dot(view_normal, to_light), 0.0);\n"
		"        diffuse_light += Light%uDiffuse.rgb * lambert * attenuation;\n",
		index);
	body += line;

	// The specular term is Blinn's half vector, which is what D3D9's fixed-function pipeline uses
	// with D3DRS_LOCALVIEWER off - and the engine never turns it on.
	snprintf(line, sizeof(line),
		"        float3 half_vector = normalize(to_light + float3(0.0, 0.0, 1.0));\n"
		"        float highlight = pow(max(dot(view_normal, half_vector), 0.0), MaterialPower.x);\n"
		"        specular_light += Light%uSpecular.rgb * highlight * attenuation"
		" * step(0.0001, lambert);\n"
		"    }\n", index);
	body += line;
}

static bool append_texture_coordinates(std::string & body,
	const VertexPipelineDescription & description)
{
	for (unsigned stage = 0; stage < description.StageCount; ++stage) {
		const VertexStageDescription & source = description.Stages[stage];
		const DWORD generation = source.TextureCoordinateIndex & ~COORDINATE_SET_MASK;
		const unsigned set = source.TextureCoordinateIndex & COORDINATE_SET_MASK;

		char line[1024];
		switch (generation) {
		case D3DTSS_TCI_PASSTHRU:
			if (set >= texture_coordinate_set_count(description.FVF)) {
				// The format does not carry the set the stage is asking for.  D3D9 reads zeroes;
				// refusing says so instead of drawing something that looks nearly right.
				return false;
			}
			snprintf(line, sizeof(line), "    float4 generated%u = float4(input.TexCoord%u, 0.0, 1.0);\n",
				stage, set);
			break;

		case D3DTSS_TCI_CAMERASPACEPOSITION:
			snprintf(line, sizeof(line), "    float4 generated%u = float4(view_position.xyz, 1.0);\n",
				stage);
			break;

		case D3DTSS_TCI_CAMERASPACENORMAL:
			snprintf(line, sizeof(line), "    float4 generated%u = float4(view_normal, 1.0);\n",
				stage);
			break;

		case D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR:
			snprintf(line, sizeof(line),
				"    float4 generated%u = float4(reflect(normalize(view_position.xyz), view_normal), 1.0);\n",
				stage);
			break;

		default:
			return false;
		}
		body += line;

		const DWORD transform_count = source.TextureTransformFlags & TEXTURE_TRANSFORM_COUNT_MASK;
		if (transform_count != D3DTTFF_DISABLE) {
			snprintf(line, sizeof(line), "    generated%u = mul(generated%u, TextureMatrix%u);\n",
				stage, stage, stage);
			body += line;

			if ((source.TextureTransformFlags & D3DTTFF_PROJECTED) != 0) {
				// A projected transform divides by the last coordinate the count named, which for
				// every projected stage the game sets is the third.
				snprintf(line, sizeof(line),
					"    generated%u.xy /= max(generated%u.z, 0.0001);\n", stage, stage);
				body += line;
			}
		}

		snprintf(line, sizeof(line), "    output.TexCoord%u = generated%u.xy;\n", stage, stage);
		body += line;
	}
	return true;
}

static void append_constants_d3d9(std::string & hlsl, const VertexPipelineDescription & description)
{
	// Long enough for the widest block written below.  A short buffer here truncates the last
	// declaration rather than failing, and the compiler then reports a syntax error on the line
	// after it, which says nothing about where the fault is.
	char line[1024];
	// row_major for the reason the D3D11 block gives: a D3D9 matrix is stored by rows and HLSL
	// assumes columns.
	snprintf(line, sizeof(line),
		"row_major float4x4 WorldViewProjection : register(c%u);\n"
		"row_major float4x4 WorldView : register(c%u);\n"
		"row_major float4x4 NormalTransform : register(c%u);\n"
		"row_major float4x4 TextureMatrix0 : register(c%u);\n"
		"row_major float4x4 TextureMatrix1 : register(c%u);\n",
		REGISTER_WORLD_VIEW_PROJECTION, REGISTER_WORLD_VIEW, REGISTER_NORMAL_TRANSFORM,
		REGISTER_TEXTURE_MATRIX_0, REGISTER_TEXTURE_MATRIX_1);
	hlsl += line;

	snprintf(line, sizeof(line),
		"float4 MaterialAmbient : register(c%u);\n"
		"float4 MaterialDiffuse : register(c%u);\n"
		"float4 MaterialSpecular : register(c%u);\n"
		"float4 MaterialEmissive : register(c%u);\n"
		"float4 MaterialPower : register(c%u);\n"
		"float4 GlobalAmbient : register(c%u);\n"
		"float4 FogParameters : register(c%u);\n",
		REGISTER_MATERIAL_AMBIENT, REGISTER_MATERIAL_DIFFUSE, REGISTER_MATERIAL_SPECULAR,
		REGISTER_MATERIAL_EMISSIVE, REGISTER_MATERIAL_POWER, REGISTER_GLOBAL_AMBIENT,
		REGISTER_FOG_PARAMETERS);
	hlsl += line;

	for (unsigned index = 0; index < description.LightCount; ++index) {
		const unsigned base = REGISTER_LIGHTS + index * REGISTERS_PER_LIGHT;
		snprintf(line, sizeof(line),
			"float4 Light%uPosition : register(c%u);\n"
			"float4 Light%uDirection : register(c%u);\n"
			"float4 Light%uDiffuse : register(c%u);\n"
			"float4 Light%uSpecular : register(c%u);\n"
			"float4 Light%uAttenuation : register(c%u);\n"
			"float4 Light%uSpot : register(c%u);\n",
			index, base, index, base + 1, index, base + 2,
			index, base + 3, index, base + 4, index, base + 5);
		hlsl += line;
	}
}

static void append_constants_d3d11(std::string & hlsl,
	const VertexPipelineDescription & description)
{
	// row_major, because every matrix the engine has is a D3D9 matrix and D3D9 stores a matrix by
	// rows.  HLSL's default is by columns, so without this each one arrives transposed and the
	// whole world is drawn through the wrong basis - which is not subtly wrong, it is a screen with
	// nothing recognisable on it.
	hlsl +=
		"cbuffer VertexPipeline : register(b0)\n"
		"{\n"
		"    row_major float4x4 WorldViewProjection;\n"
		"    row_major float4x4 WorldView;\n"
		"    row_major float4x4 NormalTransform;\n"
		"    row_major float4x4 TextureMatrix0;\n"
		"    row_major float4x4 TextureMatrix1;\n"
		"    float4 MaterialAmbient;\n"
		"    float4 MaterialDiffuse;\n"
		"    float4 MaterialSpecular;\n"
		"    float4 MaterialEmissive;\n"
		"    float4 MaterialPower;\n"
		"    float4 GlobalAmbient;\n"
		"    float4 FogParameters;\n";

	for (unsigned index = 0; index < description.LightCount; ++index) {
		char line[1024];
		snprintf(line, sizeof(line),
			"    float4 Light%uPosition;\n"
			"    float4 Light%uDirection;\n"
			"    float4 Light%uDiffuse;\n"
			"    float4 Light%uSpecular;\n"
			"    float4 Light%uAttenuation;\n"
			"    float4 Light%uSpot;\n",
			index, index, index, index, index, index);
		hlsl += line;
	}
	hlsl += "};\n";
}

bool VertexShader_Generate(const VertexPipelineDescription & description,
	VertexShaderTarget target, std::string & hlsl)
{
	if (description.StageCount > MAXIMUM_VERTEX_STAGES
		|| description.LightCount > MAXIMUM_VERTEX_LIGHTS) {
		return false;
	}

	// Lighting needs a normal to light.  D3D9 lights a vertex with no normal as though the normal
	// were zero, which is black, and no draw in the game asks for that.
	if (description.LightingEnabled && !has_normal(description.FVF)) {
		return false;
	}

	// A pre-transformed vertex has been through all of this already.  The 2D passes draw that way
	// and a generated shader for one would be a transform applied twice.
	if (is_pretransformed(description.FVF)) {
		return false;
	}

	for (unsigned index = 0; index < description.LightCount; ++index) {
		const DWORD type = description.Lights[index].Type;
		if (type != D3DLIGHT_DIRECTIONAL && type != D3DLIGHT_POINT && type != D3DLIGHT_SPOT) {
			return false;
		}
	}

	std::string body;
	body +=
		"    float4 model_position = float4(input.Position, 1.0);\n"
		"    float4 view_position = mul(model_position, WorldView);\n";

	if (has_normal(description.FVF)) {
		body += "    float3 view_normal = normalize(mul(float4(input.Normal, 0.0), NormalTransform).xyz);\n";
	}
	else {
		// Nothing reads it, but the coordinate generation modes name it and a shader that declares
		// it unconditionally is one branch fewer here.  The compiler drops it when it is unused.
		body += "    float3 view_normal = float3(0.0, 0.0, 1.0);\n";
	}

	if (!append_texture_coordinates(body, description)) {
		return false;
	}

	if (description.LightingEnabled) {
		body += "    float3 diffuse_light = GlobalAmbient.rgb;\n";
		body += "    float3 specular_light = float3(0.0, 0.0, 0.0);\n";
		for (unsigned index = 0; index < description.LightCount; ++index) {
			append_light(body, index, description.Lights[index].Type);
		}

		std::string diffuse;
		std::string ambient;
		std::string emissive;
		if (!material_source_expression(description.DiffuseMaterialSource, "MaterialDiffuse",
				description, diffuse)
			|| !material_source_expression(description.AmbientMaterialSource, "MaterialAmbient",
				description, ambient)
			|| !material_source_expression(description.EmissiveMaterialSource, "MaterialEmissive",
				description, emissive)) {
			return false;
		}

		body += "    output.Diffuse.rgb = saturate(" + diffuse + ".rgb * diffuse_light + "
			+ ambient + ".rgb * GlobalAmbient.rgb + " + emissive + ".rgb);\n";
		body += "    output.Diffuse.a = " + diffuse + ".a;\n";

		if (description.SpecularEnabled) {
			std::string specular;
			if (!material_source_expression(description.SpecularMaterialSource, "MaterialSpecular",
					description, specular)) {
				return false;
			}
			body += "    output.Specular = float4(saturate(" + specular
				+ ".rgb * specular_light), 0.0);\n";
		}
		else {
			body += "    output.Specular = float4(0.0, 0.0, 0.0, 0.0);\n";
		}
	}
	else {
		// With D3DRS_LIGHTING off the diffuse colour is the vertex colour where there is one and
		// white where there is not, which is what an unlit draw with no colour comes out as.
		if (has_diffuse(description.FVF)) {
			body += "    output.Diffuse = input.Diffuse;\n";
		}
		else {
			body += "    output.Diffuse = float4(1.0, 1.0, 1.0, 1.0);\n";
		}
		body += "    output.Specular = float4(0.0, 0.0, 0.0, 0.0);\n";
	}

	if (description.FogEnabled) {
		// FogParameters carries the start, the end and the density; the mode picks which two of
		// them are read.  D3D9's factor is the weight of the unfogged colour, so 1 is no fog.
		switch (description.FogVertexMode) {
		case D3DFOG_LINEAR:
			body += "    output.Fog = saturate((FogParameters.y - view_position.z)"
				" / max(FogParameters.y - FogParameters.x, 0.0001));\n";
			break;
		case D3DFOG_EXP:
			body += "    output.Fog = saturate(exp(-FogParameters.z * view_position.z));\n";
			break;
		case D3DFOG_EXP2:
			body += "    output.Fog = saturate(exp(-FogParameters.z * FogParameters.z"
				" * view_position.z * view_position.z));\n";
			break;
		default:
			return false;
		}
	}
	else {
		body += "    output.Fog = 1.0;\n";
	}

	const bool for_d3d11 = (target == VERTEX_SHADER_TARGET_D3D11);

	hlsl = "// Generated from a fixed-function vertex pipeline description.  See ffvertex.h.\n";
	if (for_d3d11) {
		append_constants_d3d11(hlsl, description);
	}
	else {
		append_constants_d3d9(hlsl, description);
	}

	hlsl +=
		"\n"
		"struct Input\n"
		"{\n"
		"    float3 Position : POSITION;\n"
		"    float3 Normal   : NORMAL;\n"
		"    float4 Diffuse  : COLOR0;\n"
		"    float2 TexCoord0 : TEXCOORD0;\n"
		"    float2 TexCoord1 : TEXCOORD1;\n"
		"};\n"
		"\n"
		"struct Output\n"
		"{\n";
	hlsl += for_d3d11 ? "    float4 Position : SV_Position;\n" : "    float4 Position : POSITION;\n";
	hlsl +=
		"    float4 Diffuse  : COLOR0;\n"
		"    float4 Specular : COLOR1;\n"
		"    float2 TexCoord0 : TEXCOORD0;\n"
		"    float2 TexCoord1 : TEXCOORD1;\n";
	hlsl += for_d3d11 ? "    float Fog : FOG;\n" : "    float Fog : FOG;\n";
	hlsl +=
		"};\n"
		"\n"
		"Output main(Input input)\n"
		"{\n"
		"    Output output;\n";
	hlsl += body;

	// The stages that were not generated still have an output to fill, or the structure carries
	// whatever was on the stack and the pixel shader samples it.
	for (unsigned stage = description.StageCount; stage < MAXIMUM_VERTEX_STAGES; ++stage) {
		char line[128];
		snprintf(line, sizeof(line), "    output.TexCoord%u = float2(0.0, 0.0);\n", stage);
		hlsl += line;
	}

	hlsl +=
		"    output.Position = mul(model_position, WorldViewProjection);\n"
		"    return output;\n"
		"}\n";
	return true;
}

std::string VertexShader_Key(const VertexPipelineDescription & description)
{
	std::string key;
	char field[64];

	snprintf(field, sizeof(field), "%lu:%u%u%u:%lu,%lu,%lu,%lu",
		description.FVF, description.LightingEnabled ? 1u : 0u,
		description.SpecularEnabled ? 1u : 0u, description.ColourVertexEnabled ? 1u : 0u,
		description.DiffuseMaterialSource, description.AmbientMaterialSource,
		description.EmissiveMaterialSource, description.SpecularMaterialSource);
	key += field;

	for (unsigned index = 0; index < description.LightCount; ++index) {
		snprintf(field, sizeof(field), ":L%lu", description.Lights[index].Type);
		key += field;
	}

	for (unsigned stage = 0; stage < description.StageCount; ++stage) {
		snprintf(field, sizeof(field), ":T%lu,%lu", description.Stages[stage].TextureCoordinateIndex,
			description.Stages[stage].TextureTransformFlags);
		key += field;
	}

	snprintf(field, sizeof(field), ":F%u,%lu", description.FogEnabled ? 1u : 0u,
		description.FogEnabled ? description.FogVertexMode : 0ul);
	key += field;
	return key;
}
