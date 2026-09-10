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

#include "engineshader.h"

#include <stdio.h>
#include <string.h>

/*
** The terrain and the roads are eight variations on one program: a base colour, and then a chain of
** things multiplied into it.  Written out of the -dx11dump disassembly, one line of ps_1_1 each:
**
**     terrain.pso         lrp r0, v0.w, t1, t0 ; mul r0, r0, v0
**     terrainnoise.pso    the same, then mul r0, r0, t2
**     terrainnoise2.pso   the same, then mul r0, r0, t3
**     fterrain.pso        mul r0, t1, t0 ; mul r0, r0, v0
**     fterrain0.pso       mov r0, t1 ; mul r0, r0, v0
**     fterrainnoise.pso   fterrain, then mul r0, r0, t2
**     fterrainnoise2.pso  the same, then mul r0, r0, t3
**     roadnoise2.pso      mul r0, t0, t1 ; mul r0, r0, t2 ; mul r0, r0, v0
**
** lrp dest, a, b, c is c + a * (b - c), so v0.w weights the second layer over the first: that is
** the terrain blending one ground texture into the next by the alpha in its own vertices.  The road
** one multiplies the vertex colour in last rather than second, which is the same arithmetic until a
** term saturates.
*/
static const char * const TERRAIN_CHAIN[] = { "input.Diffuse", NULL };
static const char * const TERRAIN_NOISE_CHAIN[] = { "input.Diffuse", "texel2", NULL };
static const char * const TERRAIN_NOISE_2_CHAIN[] = { "input.Diffuse", "texel2", "texel3", NULL };
static const char * const ROAD_NOISE_2_CHAIN[] = { "texel2", "input.Diffuse", NULL };

static const char * const TERRAIN_OPENING = "lerp(texel0, texel1, input.Diffuse.a)";
static const char * const FLAT_TERRAIN_OPENING = "texel1 * texel0";
static const char * const FLAT_TERRAIN_BASE_OPENING = "texel1";
static const char * const ROAD_NOISE_2_OPENING = "texel0 * texel1";

// One entry per shipped shader this can write.  The file name is the whole key: the engine loads
// each of them from a fixed path and there is exactly one shader per file.  Opening and Chain are
// set for the multiply-chain programs above and null for the ones written out by hand.
struct EngineShaderEntry
{
	EngineShaderProgram Program;
	const char * FileName;
	const char * Name;
	const char * Opening;
	const char * const * Chain;
};

static const EngineShaderEntry ENGINE_SHADERS[] = {
	{ ENGINE_SHADER_TREES, "trees.vso", "engine:trees", NULL, NULL },
	{ ENGINE_SHADER_WATER_TRAPEZOID, "trapezoid water ps.1.1", "engine:trapezoidwater",
		NULL, NULL },
	{ ENGINE_SHADER_WATER_RIVER, "river water ps.1.1", "engine:riverwater", NULL, NULL },
	{ ENGINE_SHADER_TERRAIN, "terrain.pso", "engine:terrain",
		TERRAIN_OPENING, TERRAIN_CHAIN },
	{ ENGINE_SHADER_TERRAIN_NOISE, "terrainnoise.pso", "engine:terrainnoise",
		TERRAIN_OPENING, TERRAIN_NOISE_CHAIN },
	{ ENGINE_SHADER_TERRAIN_NOISE_2, "terrainnoise2.pso", "engine:terrainnoise2",
		TERRAIN_OPENING, TERRAIN_NOISE_2_CHAIN },
	{ ENGINE_SHADER_FLAT_TERRAIN, "fterrain.pso", "engine:flatterrain",
		FLAT_TERRAIN_OPENING, TERRAIN_CHAIN },
	{ ENGINE_SHADER_FLAT_TERRAIN_BASE, "fterrain0.pso", "engine:flatterrainbase",
		FLAT_TERRAIN_BASE_OPENING, TERRAIN_CHAIN },
	{ ENGINE_SHADER_FLAT_TERRAIN_NOISE, "fterrainnoise.pso", "engine:flatterrainnoise",
		FLAT_TERRAIN_OPENING, TERRAIN_NOISE_CHAIN },
	{ ENGINE_SHADER_FLAT_TERRAIN_NOISE_2, "fterrainnoise2.pso", "engine:flatterrainnoise2",
		FLAT_TERRAIN_OPENING, TERRAIN_NOISE_2_CHAIN },
	{ ENGINE_SHADER_ROAD_NOISE_2, "roadnoise2.pso", "engine:roadnoise2",
		ROAD_NOISE_2_OPENING, ROAD_NOISE_2_CHAIN }
};

static const unsigned ENGINE_SHADER_COUNT = sizeof(ENGINE_SHADERS) / sizeof(ENGINE_SHADERS[0]);

static const EngineShaderEntry * entry_for(EngineShaderProgram program)
{
	for (unsigned entry = 0; entry < ENGINE_SHADER_COUNT; ++entry) {
		if (ENGINE_SHADERS[entry].Program == program) {
			return &ENGINE_SHADERS[entry];
		}
	}
	return NULL;
}

// The declaration every transcribed vertex program opens with.  The constant bank is the engine's
// own float4 registers, so the shader indexes them by the same number the SetVertexShaderConstantF
// call used and the transcription reads like the assembly it came from.
static void write_preamble(std::string & hlsl)
{
	char line[128];
	snprintf(line, sizeof(line), "cbuffer EngineConstants : register(b0)\n{\n    float4 c[%u];\n};\n\n",
		ENGINE_SHADER_CONSTANTS);
	hlsl += line;
}

// ffvertex's output structure, member for member.  See the file comment for why it cannot differ.
static void write_output_structure(std::string & hlsl)
{
	hlsl +=
		"struct Output\n"
		"{\n"
		"    float4 Position : SV_Position;\n"
		"    float4 Diffuse  : COLOR0;\n"
		"    float4 Specular : COLOR1;\n"
		"    float2 TexCoord0 : TEXCOORD0;\n"
		"    float2 TexCoord1 : TEXCOORD1;\n"
		"    float Fog : FOG;\n"
		"};\n"
		"\n";
}

/*
** Trees.vso, transcribed from the vs_1_1 the translator disassembles:
**
**     dcl_position v0, dcl_blendweight v1, dcl_blendindices v2, dcl_texcoord v7
**     mov r2, v1.wwzw       ; a float3 input reads w as one, so this is (1, 1, v1.z, 1)
**     add r2, v0, -r2       ; r2.z is the vertex height above the tree's own base
**     mov a0.x, v1          ; the sway type, rounded
**     mov r0, c8[a0.x]      ; c8 is the no-sway slot, c9 to c18 the ten breezes
**     mad r1, r2.zzzw, r0, v0
**     m4x4 oPos, r1, c4
**     mov r2, v1.yyyw
**     mul oD0, v2, r2       ; the tree's lit shade, with the vertex alpha kept
**     mov oT0, v7
**     add r1, v0, c32
**     mul oT1, r1, c33      ; the shroud coordinate
**
** The declaration beside it (W3DTreeBuffer::initData) puts position in register 0, the normal slot
** in 1 and the D3DCOLOR in 2, which is the byte layout of DX8_FVF_XYZNDUV1.  Nothing in the tree
** vertex is a normal: nx is the sway type, ny the shade the push-aside left, nz the base height.
**
** oFog is never written, so the fog factor is one and the trees are drawn unfogged, which is what
** the Direct3D 9 device does with this shader bound.
*/
static void write_trees(std::string & hlsl)
{
	write_preamble(hlsl);
	hlsl +=
		"struct Input\n"
		"{\n"
		"    float3 Position : POSITION;\n"
		"    float3 Sway : NORMAL;\n"
		"    float4 Diffuse : COLOR0;\n"
		"    float2 TexCoord0 : TEXCOORD0;\n"
		"};\n"
		"\n";
	write_output_structure(hlsl);
	hlsl +=
		"Output main(Input input)\n"
		"{\n"
		"    Output output;\n"
		"    int sway_type = (int)(input.Sway.x + 0.5);\n"
		"    float height = input.Position.z - input.Sway.z;\n"
		"    float4 swayed = float4(height * c[8 + sway_type].xyz + input.Position, 1.0);\n"
		"    output.Position = float4(dot(swayed, c[4]), dot(swayed, c[5]),\n"
		"        dot(swayed, c[6]), dot(swayed, c[7]));\n"
		"    output.Diffuse = input.Diffuse * float4(input.Sway.yyy, 1.0);\n"
		"    output.Specular = float4(0.0, 0.0, 0.0, 0.0);\n"
		"    output.TexCoord0 = input.TexCoord0;\n"
		"    output.TexCoord1 = ((float4(input.Position, 1.0) + c[32]) * c[33]).xy;\n"
		"    output.Fog = 1.0;\n"
		"    return output;\n"
		"}\n";
}

// ffshader's declarations, member for member and in the same order, so a transcribed pixel program
// is bound with the same constant buffer, the same textures and the same samplers as a generated
// one.  Reading fewer of them than are declared costs nothing.
static void write_pixel_preamble(std::string & hlsl)
{
	for (unsigned stage = 0; stage < MAXIMUM_COMBINER_STAGES; ++stage) {
		char line[128];
		snprintf(line, sizeof(line),
			"Texture2D Texture%u : register(t%u);\n"
			"SamplerState Sampler%u : register(s%u);\n",
			stage, stage, stage, stage);
		hlsl += line;
	}

	hlsl +=
		"cbuffer CombinerConstants : register(b0)\n"
		"{\n"
		"    float4 TextureFactor;\n"
		"    float4 FogColour;\n"
		"    float4 AlphaReference;\n"
		"};\n"
		"\n"
		"struct Input\n"
		"{\n"
		"    float4 Position  : SV_Position;\n"
		"    float4 Diffuse   : COLOR0;\n"
		"    float4 Specular  : COLOR1;\n";

	for (unsigned stage = 0; stage < MAXIMUM_COMBINER_STAGES; ++stage) {
		char line[64];
		snprintf(line, sizeof(line), "    float2 TexCoord%u : TEXCOORD%u;\n", stage, stage);
		hlsl += line;
	}

	hlsl +=
		"    float Fog        : FOG;\n"
		"};\n"
		"\n"
		"float4 main(Input input) : SV_Target\n"
		"{\n";

	for (unsigned stage = 0; stage < MAXIMUM_COMBINER_STAGES; ++stage) {
		char line[128];
		snprintf(line, sizeof(line),
			"    float4 texel%u = Texture%u.Sample(Sampler%u, input.TexCoord%u);\n",
			stage, stage, stage, stage);
		hlsl += line;
	}
}

/*
** The trapezoid water, the ps.1.1 W3DWater assembles for every flat water body:
**
**     tex t0 ; the water texture
**     tex t1 ; white highlights on black
**     tex t2 ; the same highlights, tiled harder, on the camera space position
**     tex t3 ; the shroud, or white where there is none
**     mul r0, v0, t0
**     mad r0.rgb, t1, t2, r0
**     mul r0.rgb, r0, t3
**
** Every ps_1_1 instruction clamps its result to zero and one, which is the saturate on each line.
** The alpha is the vertex alpha times the water texture's and the shroud never touches it: a
** shrouded stretch of water is dark, not transparent.
*/
static void write_trapezoid_water(std::string & hlsl)
{
	write_pixel_preamble(hlsl);
	hlsl +=
		"    float4 current = saturate(input.Diffuse * texel0);\n"
		"    current.rgb = saturate(texel1.rgb * texel2.rgb + current.rgb);\n"
		"    current.rgb = saturate(current.rgb * texel3.rgb);\n";
}

/*
** The river water, the same four stages with the sparkles kept apart from the base colour:
**
**     mul r0.rgb, v0, t0    ; the water, tinted by the vertex colour
**     mov r0.a, t0          ; the vertex alpha carries the shroud and must not fade the water
**     mul r1, t1, t2
**     add r1.rgb, r1, t3
**     mul r1.rgb, r1, v0.a  ; the sparkles and the edge glow do get darkened by the shroud
**     +mul r0.a, r0, t3
**     add r0.rgb, r0, r1
**
** The + pairs that alpha instruction with the colour one above it, so it reads r0.a as the previous
** line left it and writes the shroud's alpha into it.
*/
static void write_river_water(std::string & hlsl)
{
	write_pixel_preamble(hlsl);
	hlsl +=
		"    float4 current;\n"
		"    current.rgb = saturate(input.Diffuse.rgb * texel0.rgb);\n"
		"    current.a = saturate(texel0.a * texel3.a);\n"
		"    float3 sparkle = saturate(texel1.rgb * texel2.rgb);\n"
		"    sparkle = saturate(sparkle + texel3.rgb);\n"
		"    sparkle = saturate(sparkle * input.Diffuse.a);\n"
		"    current.rgb = saturate(current.rgb + sparkle);\n";
}

// Every ps_1_1 instruction clamps its result to zero and one, so each step saturates and not only
// the last: a chain that overflows in the middle and comes back down is a different colour with the
// clamps than without them.
static void write_multiply_chain(std::string & hlsl, const EngineShaderEntry & entry)
{
	write_pixel_preamble(hlsl);

	hlsl += "    float4 current = saturate(";
	hlsl += entry.Opening;
	hlsl += ");\n";

	for (const char * const * step = entry.Chain; *step != NULL; ++step) {
		hlsl += "    current = saturate(current * ";
		hlsl += *step;
		hlsl += ");\n";
	}
}

static char lowered(char character)
{
	return (character >= 'A' && character <= 'Z') ? (char)(character - 'A' + 'a') : character;
}

// The last path component, lowered, compared against the table.  The engine writes its paths with
// backslashes and mixed case, and neither is worth carrying into the table.
EngineShaderProgram EngineShader_From_File(const char * file_path)
{
	if (file_path == NULL) {
		return ENGINE_SHADER_NONE;
	}

	const char * name = file_path;
	for (const char * step = file_path; *step != '\0'; ++step) {
		if (*step == '\\' || *step == '/') {
			name = step + 1;
		}
	}

	for (unsigned entry = 0; entry < ENGINE_SHADER_COUNT; ++entry) {
		const char * wanted = ENGINE_SHADERS[entry].FileName;
		const char * found = name;
		while (*wanted != '\0' && lowered(*found) == *wanted) {
			++wanted;
			++found;
		}
		if (*wanted == '\0' && *found == '\0') {
			return ENGINE_SHADERS[entry].Program;
		}
	}
	return ENGINE_SHADER_NONE;
}

bool EngineShader_Vertex_Program(EngineShaderProgram program, std::string & hlsl)
{
	hlsl.clear();
	switch (program) {
	case ENGINE_SHADER_TREES:
		write_trees(hlsl);
		return true;
	default:
		return false;
	}
}

bool EngineShader_Pixel_Program(EngineShaderProgram program,
	const PixelPipelineDescription & pipeline, std::string & hlsl)
{
	hlsl.clear();

	const EngineShaderEntry * entry = entry_for(program);
	if (entry != NULL && entry->Opening != NULL) {
		write_multiply_chain(hlsl, *entry);
	}
	else if (program == ENGINE_SHADER_WATER_TRAPEZOID) {
		write_trapezoid_water(hlsl);
	}
	else if (program == ENGINE_SHADER_WATER_RIVER) {
		write_river_water(hlsl);
	}
	else {
		return false;
	}

	if (!CombinerShader_Append_Pixel_Pipeline(pipeline, hlsl)) {
		return false;
	}

	hlsl +=
		"    return current;\n"
		"}\n";
	return true;
}

const char * EngineShader_Name(EngineShaderProgram program)
{
	const EngineShaderEntry * entry = entry_for(program);
	return (entry != NULL) ? entry->Name : "engine:none";
}
