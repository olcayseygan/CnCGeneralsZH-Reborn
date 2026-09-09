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

#include "ffshader.h"

#include <stdio.h>

// The texture factor and the sampler registers the generated shader expects the caller to have
// filled.  They are the constant register D3DRS_TEXTUREFACTOR is copied into and the stage samplers
// the device already has bound, so nothing about how a draw is set up has to change.
static const char * const TEXTURE_FACTOR_REGISTER = "c0";

// D3DTA_SELECTMASK is the argument itself; the two flags above it are modifiers the device applies
// to whatever the argument turned out to be.
static const DWORD ARGUMENT_SELECT_MASK = D3DTA_SELECTMASK;

// "current" is initialised to the diffuse colour, which is what the device gives D3DTA_CURRENT at
// stage 0, so the two need no distinguishing here.
static bool argument_expression(DWORD argument, std::string & expression)
{
	switch (argument & ARGUMENT_SELECT_MASK) {
	case D3DTA_DIFFUSE:  expression = "input.Diffuse";  break;
	case D3DTA_CURRENT:  expression = "current"; break;
	case D3DTA_TEXTURE:  expression = "texel"; break;
	case D3DTA_TFACTOR:  expression = "TextureFactor"; break;
	case D3DTA_SPECULAR: expression = "input.Specular"; break;
	default:
		return false;
	}

	// Replicate first, complement second: that is the order the reference rasteriser applies them,
	// so 1 - a.a and not (1 - a).a for an argument carrying both.
	if ((argument & D3DTA_ALPHAREPLICATE) != 0) {
		expression = "(" + expression + ").aaaa";
	}
	if ((argument & D3DTA_COMPLEMENT) != 0) {
		expression = "(1.0 - " + expression + ")";
	}
	return true;
}

static bool operation_expression(DWORD operation, const std::string & argument0,
	const std::string & argument1, const std::string & argument2, std::string & expression)
{
	switch (operation) {
	case D3DTOP_SELECTARG1:
		expression = argument1;
		return true;
	case D3DTOP_SELECTARG2:
		expression = argument2;
		return true;
	case D3DTOP_MODULATE:
		expression = "(" + argument1 + " * " + argument2 + ")";
		return true;
	case D3DTOP_MODULATE2X:
		expression = "(" + argument1 + " * " + argument2 + " * 2.0)";
		return true;
	case D3DTOP_MODULATE4X:
		expression = "(" + argument1 + " * " + argument2 + " * 4.0)";
		return true;
	case D3DTOP_ADD:
		expression = "(" + argument1 + " + " + argument2 + ")";
		return true;
	case D3DTOP_ADDSIGNED:
		expression = "(" + argument1 + " + " + argument2 + " - 0.5)";
		return true;
	case D3DTOP_SUBTRACT:
		expression = "(" + argument1 + " - " + argument2 + ")";
		return true;
	case D3DTOP_MULTIPLYADD:
		expression = "(" + argument0 + " + " + argument1 + " * " + argument2 + ")";
		return true;
	case D3DTOP_LERP:
		expression = "lerp(" + argument2 + ", " + argument1 + ", " + argument0 + ")";
		return true;
	case D3DTOP_DOTPRODUCT3:
		// The device works on signed values here and saturates the result into all four channels.
		expression = "saturate(dot((" + argument1 + ").rgb * 2.0 - 1.0, ("
			+ argument2 + ").rgb * 2.0 - 1.0)).xxxx";
		return true;
	default:
		return false;
	}
}

bool CombinerShader_Generate(const CombinerDescription & description, std::string & hlsl)
{
	if (description.StageCount == 0 || description.StageCount > MAXIMUM_COMBINER_STAGES) {
		return false;
	}

	std::string body;
	for (unsigned stage = 0; stage < description.StageCount; ++stage) {
		const CombinerStage & source = description.Stages[stage];

		char line[256];
		if (source.TextureBound) {
			// D3DTSS_TEXCOORDINDEX carries the generation mode in its high bits and the coordinate
			// set in its low ones.  Only the set is read here: every generation mode the game uses
			// is a transform the vertex stage has already applied by the time a coordinate arrives.
			const unsigned coordinate_set = source.TextureCoordinateIndex & 0xffff;
			snprintf(line, sizeof(line), "    texel = tex2D(Sampler%u, input.TexCoord%u);\n",
				stage, coordinate_set);
		}
		else {
			// What a stage with no texture bound gives D3DTA_TEXTURE is not something this can
			// assert: the documentation does not say and reports of what hardware does disagree.
			// Opaque white is the choice that makes MODULATE a no-op, which is what the call sites
			// that leave a stage untextured appear to want, and one measured combination reads
			// D3DTA_TEXTURE with nothing bound - SELECTARG1 on an untextured stage - so the two
			// answers are distinguishable on screen.  tree-check.ps1 settles it, not this comment.
			snprintf(line, sizeof(line), "    texel = float4(1.0, 1.0, 1.0, 1.0);\n");
		}
		body += line;

		std::string colour_argument0, colour_argument1, colour_argument2;
		std::string alpha_argument0, alpha_argument1, alpha_argument2;
		if (!argument_expression(source.ColourArgument0, colour_argument0)
			|| !argument_expression(source.ColourArgument1, colour_argument1)
			|| !argument_expression(source.ColourArgument2, colour_argument2)
			|| !argument_expression(source.AlphaArgument0, alpha_argument0)
			|| !argument_expression(source.AlphaArgument1, alpha_argument1)
			|| !argument_expression(source.AlphaArgument2, alpha_argument2)) {
			return false;
		}

		std::string colour_expression;
		std::string alpha_expression;
		if (!operation_expression(source.ColourOperation, colour_argument0, colour_argument1,
				colour_argument2, colour_expression)
			|| !operation_expression(source.AlphaOperation, alpha_argument0, alpha_argument1,
				alpha_argument2, alpha_expression)) {
			return false;
		}

		body += "    current.rgb = " + colour_expression + ".rgb;\n";
		body += "    current.a   = " + alpha_expression + ".a;\n";
	}

	hlsl =
		"// Generated from a fixed-function texture stage description.  See ffshader.h.\n"
		"sampler2D Sampler0 : register(s0);\n"
		"sampler2D Sampler1 : register(s1);\n"
		"float4 TextureFactor : register(";
	hlsl += TEXTURE_FACTOR_REGISTER;
	hlsl +=
		");\n"
		"\n"
		"struct Input\n"
		"{\n"
		"    float4 Diffuse   : COLOR0;\n"
		"    float4 Specular  : COLOR1;\n"
		"    float2 TexCoord0 : TEXCOORD0;\n"
		"    float2 TexCoord1 : TEXCOORD1;\n"
		"};\n"
		"\n"
		"float4 main(Input input) : COLOR\n"
		"{\n"
		"    float4 texel;\n"
		"    float4 current = input.Diffuse;\n";
	hlsl += body;
	hlsl +=
		"    return current;\n"
		"}\n";
	return true;
}

std::string CombinerShader_Key(const CombinerDescription & description)
{
	std::string key;
	char field[32];
	snprintf(field, sizeof(field), "%u", description.StageCount);
	key += field;

	for (unsigned stage = 0; stage < description.StageCount; ++stage) {
		const CombinerStage & source = description.Stages[stage];
		snprintf(field, sizeof(field), ":%lu,%lu,%lu,%lu",
			source.ColourOperation, source.ColourArgument0,
			source.ColourArgument1, source.ColourArgument2);
		key += field;
		snprintf(field, sizeof(field), ",%lu,%lu,%lu,%lu",
			source.AlphaOperation, source.AlphaArgument0,
			source.AlphaArgument1, source.AlphaArgument2);
		key += field;
		snprintf(field, sizeof(field), ",%lu,%u",
			source.TextureCoordinateIndex & 0xffff, source.TextureBound ? 1u : 0u);
		key += field;
	}
	return key;
}
