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
// has 256 float4 registers and this uses fewer than 70 of them, so the layout is written for
// reading rather than for packing.  Everything after the texture matrices is derived from where
// they end, because how many there are follows MAXIMUM_VERTEX_STAGES.
static const unsigned REGISTERS_PER_MATRIX = 4;
static const unsigned REGISTER_WORLD_VIEW_PROJECTION = 0;
static const unsigned REGISTER_WORLD_VIEW = 4;
static const unsigned REGISTER_NORMAL_TRANSFORM = 8;
static const unsigned REGISTER_TEXTURE_MATRICES = 12;
static const unsigned REGISTER_MATERIAL_AMBIENT =
	REGISTER_TEXTURE_MATRICES + MAXIMUM_VERTEX_STAGES * REGISTERS_PER_MATRIX;
static const unsigned REGISTER_MATERIAL_DIFFUSE = REGISTER_MATERIAL_AMBIENT + 1;
static const unsigned REGISTER_MATERIAL_SPECULAR = REGISTER_MATERIAL_AMBIENT + 2;
static const unsigned REGISTER_MATERIAL_EMISSIVE = REGISTER_MATERIAL_AMBIENT + 3;
static const unsigned REGISTER_MATERIAL_POWER = REGISTER_MATERIAL_AMBIENT + 4;
static const unsigned REGISTER_GLOBAL_AMBIENT = REGISTER_MATERIAL_AMBIENT + 5;
static const unsigned REGISTER_FOG_PARAMETERS = REGISTER_MATERIAL_AMBIENT + 6;
// The reciprocal of the viewport's width and height, which is all a pre-transformed vertex needs:
// its position is already in pixels and the shader has to put it back into clip space.  It sits
// before the lights because the D3D11 block declares only as many lights as the description has,
// and anything after them would move with that count.
static const unsigned REGISTER_VIEWPORT = REGISTER_MATERIAL_AMBIENT + 7;
static const unsigned REGISTER_LIGHTS = REGISTER_MATERIAL_AMBIENT + 8;

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
				// The format does not carry the set the stage is asking for, which the engine does
				// on purpose: a shadow quad has a texture stage on and no coordinates in its
				// vertices.  D3D9 reads zeroes there, so this does too.  Refusing instead left 202
				// draws a match out of the picture.
				snprintf(line, sizeof(line),
					"    float4 generated%u = float4(0.0, 0.0, 0.0, 1.0);\n", stage);
			}
			else {
				snprintf(line, sizeof(line),
					"    float4 generated%u = float4(input.TexCoord%u, 0.0, 1.0);\n", stage, set);
			}
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
		"row_major float4x4 NormalTransform : register(c%u);\n",
		REGISTER_WORLD_VIEW_PROJECTION, REGISTER_WORLD_VIEW, REGISTER_NORMAL_TRANSFORM);
	hlsl += line;

	for (unsigned stage = 0; stage < MAXIMUM_VERTEX_STAGES; ++stage) {
		snprintf(line, sizeof(line), "row_major float4x4 TextureMatrix%u : register(c%u);\n",
			stage, REGISTER_TEXTURE_MATRICES + stage * REGISTERS_PER_MATRIX);
		hlsl += line;
	}

	snprintf(line, sizeof(line),
		"float4 MaterialAmbient : register(c%u);\n"
		"float4 MaterialDiffuse : register(c%u);\n"
		"float4 MaterialSpecular : register(c%u);\n"
		"float4 MaterialEmissive : register(c%u);\n"
		"float4 MaterialPower : register(c%u);\n"
		"float4 GlobalAmbient : register(c%u);\n"
		"float4 FogParameters : register(c%u);\n"
		"float4 ViewportInverse : register(c%u);\n",
		REGISTER_MATERIAL_AMBIENT, REGISTER_MATERIAL_DIFFUSE, REGISTER_MATERIAL_SPECULAR,
		REGISTER_MATERIAL_EMISSIVE, REGISTER_MATERIAL_POWER, REGISTER_GLOBAL_AMBIENT,
		REGISTER_FOG_PARAMETERS, REGISTER_VIEWPORT);
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
		"    row_major float4x4 NormalTransform;\n";

	for (unsigned stage = 0; stage < MAXIMUM_VERTEX_STAGES; ++stage) {
		char matrix[64];
		snprintf(matrix, sizeof(matrix), "    row_major float4x4 TextureMatrix%u;\n", stage);
		hlsl += matrix;
	}

	hlsl +=
		"    float4 MaterialAmbient;\n"
		"    float4 MaterialDiffuse;\n"
		"    float4 MaterialSpecular;\n"
		"    float4 MaterialEmissive;\n"
		"    float4 MaterialPower;\n"
		"    float4 GlobalAmbient;\n"
		"    float4 FogParameters;\n"
		"    float4 ViewportInverse;\n";

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

// The coordinate sets the vertex format actually carries.  Direct3D 9 let a shader declare an
// input the format did not supply; D3D11 refuses the input layout for it.
static void append_input_coordinate_sets(std::string & hlsl, unsigned coordinate_sets)
{
	for (unsigned set = 0; set < coordinate_sets && set < MAXIMUM_VERTEX_STAGES; ++set) {
		char line[64];
		snprintf(line, sizeof(line), "    float2 TexCoord%u : TEXCOORD%u;\n", set, set);
		hlsl += line;
	}
}

// What every generated vertex program writes, whatever it was generated from.  Shader model 4
// links the stages by slot in declaration order, so this structure and ffshader's input structure
// are one thing in two files: a program that writes a different set links against nothing.
static void append_output_structure(std::string & hlsl, bool for_d3d11)
{
	hlsl +=
		"struct Output\n"
		"{\n";
	hlsl += for_d3d11 ? "    float4 Position : SV_Position;\n" : "    float4 Position : POSITION;\n";
	hlsl +=
		"    float4 Diffuse  : COLOR0;\n"
		"    float4 Specular : COLOR1;\n";

	for (unsigned stage = 0; stage < MAXIMUM_VERTEX_STAGES; ++stage) {
		char line[64];
		snprintf(line, sizeof(line), "    float2 TexCoord%u : TEXCOORD%u;\n", stage, stage);
		hlsl += line;
	}

	hlsl +=
		"    float Fog : FOG;\n"
		"};\n"
		"\n";
}

// The whole program for a pre-transformed vertex.  D3D9 takes x and y as pixels inside the
// viewport, z as the depth it will write and the fourth float as the reciprocal of w; clip space
// wants the opposite of all four, so the position is mapped back and multiplied by w, which is what
// keeps the texture coordinates perspective correct on the rare quad whose w is not one.
static bool generate_pretransformed(const VertexPipelineDescription & description,
	VertexShaderTarget target, std::string & hlsl)
{
	const bool for_d3d11 = (target == VERTEX_SHADER_TARGET_D3D11);
	const unsigned coordinate_sets = texture_coordinate_set_count(description.FVF);

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
		"    float4 Position : POSITION;\n";
	if (has_diffuse(description.FVF)) {
		hlsl += "    float4 Diffuse  : COLOR0;\n";
	}
	append_input_coordinate_sets(hlsl, coordinate_sets);
	hlsl +=
		"};\n"
		"\n";
	append_output_structure(hlsl, for_d3d11);
	hlsl +=
		"Output main(Input input)\n"
		"{\n"
		"    Output output;\n"
		"    float reciprocal_w = (input.Position.w == 0.0) ? 1.0 : input.Position.w;\n"
		"    float w = 1.0 / reciprocal_w;\n"
		"    float2 normalised = input.Position.xy * ViewportInverse.xy;\n"
		"    output.Position = float4((normalised.x * 2.0 - 1.0) * w,\n"
		"        (1.0 - normalised.y * 2.0) * w, input.Position.z * w, w);\n";

	for (unsigned stage = 0; stage < MAXIMUM_VERTEX_STAGES; ++stage) {
		const unsigned set = (stage < description.StageCount)
			? (description.Stages[stage].TextureCoordinateIndex & COORDINATE_SET_MASK)
			: 0;
		char line[128];
		if (stage < description.StageCount && set < coordinate_sets) {
			snprintf(line, sizeof(line), "    output.TexCoord%u = input.TexCoord%u;\n", stage, set);
		}
		else {
			snprintf(line, sizeof(line), "    output.TexCoord%u = float2(0.0, 0.0);\n", stage);
		}
		hlsl += line;
	}

	if (has_diffuse(description.FVF)) {
		hlsl += "    output.Diffuse = input.Diffuse;\n";
	}
	else {
		hlsl += "    output.Diffuse = float4(1.0, 1.0, 1.0, 1.0);\n";
	}

	hlsl +=
		"    output.Specular = float4(0.0, 0.0, 0.0, 0.0);\n"
		"    output.Fog = 1.0;\n"
		"    return output;\n"
		"}\n";
	return true;
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

	// A pre-transformed vertex has been through the transform, the lighting and the coordinate
	// generation already: its position is in pixels, its colour is in the vertex, and D3D9 reads
	// the texture coordinates straight out of the format whatever the stage's generation mode
	// says.  All the shader has to do is put the pixels back into clip space, so it is written
	// here whole rather than sharing the body below.
	if (is_pretransformed(description.FVF)) {
		return generate_pretransformed(description, target, hlsl);
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

	// The input signature carries exactly what the vertex format carries, and no more.  Direct3D 9
	// let a shader declare an input the format did not supply and read zeroes out of it; D3D11
	// refuses the input layout outright, which is a draw that does not happen rather than a draw
	// that looks slightly wrong - 62534 of them in two minutes of Tournament Desert, which is every
	// model in the game.  What the format leaves out is filled in below with the value the fixed
	// function pipeline would have used.
	const unsigned coordinate_sets = texture_coordinate_set_count(description.FVF);

	hlsl +=
		"\n"
		"struct Input\n"
		"{\n";
	// Position stays three floats even for a pretransformed format, where the layout element has
	// four: the extra one is the reciprocal homogeneous w and the body does not read it.
	hlsl += "    float3 Position : POSITION;\n";
	if (has_normal(description.FVF)) {
		hlsl += "    float3 Normal   : NORMAL;\n";
	}
	if (has_diffuse(description.FVF)) {
		hlsl += "    float4 Diffuse  : COLOR0;\n";
	}
	append_input_coordinate_sets(hlsl, coordinate_sets);
	hlsl +=
		"};\n"
		"\n";
	append_output_structure(hlsl, for_d3d11);
	hlsl +=
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
