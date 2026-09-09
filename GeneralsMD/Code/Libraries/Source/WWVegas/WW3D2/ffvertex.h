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

/*
** The fixed-function vertex pipeline, written out as HLSL.
**
** ffshader.h does this for the pixel half.  This is the other half and it is the larger one: the
** transform, the vertex lighting, the texture coordinate generation and the fog factor all live in
** the fixed-function vertex pipeline, and D3D11 has none of it.
**
** What the game actually asks for was counted rather than assumed, the same way the combiners
** were.  Three light types, two material sources, four coordinate generation modes and texture
** transforms with a count and an optional projection.  Everything outside that list is refused
** rather than approximated, and a refusal is visible - the draw stays on the pipeline it was on -
** where a wrong approximation is a picture nobody can explain.
**
** It generates for both profiles on purpose.  Asked for D3D9 it writes a vs_2_0-shaped shader with
** float4 constant registers and a POSITION output, which can be bound on the existing device and
** compared against the fixed-function pipeline it replaces, one frame each.  Asked for D3D11 it
** writes the same body against a constant buffer with an SV_Position output.  The comparison is
** the whole reason phase 2 is being done in this order.
*/

#ifndef FFVERTEX_H
#define FFVERTEX_H

#include <d3d9.h>

#include <string>

// The engine's widest vertex format carries four coordinate sets and its texture stage code never
// looks past two, so two is what a generated shader writes and a third is a refusal.
const unsigned MAXIMUM_VERTEX_STAGES = 2;

// D3D9 allows eight simultaneous lights.  The scenes measured never light a draw with more than
// three, and every register past this one is a register the D3D9 profile does not have to spare.
const unsigned MAXIMUM_VERTEX_LIGHTS = 4;

struct VertexLightDescription
{
	// D3DLIGHT_DIRECTIONAL, D3DLIGHT_POINT or D3DLIGHT_SPOT.
	DWORD Type;
};

struct VertexStageDescription
{
	// D3DTSS_TEXCOORDINDEX whole: the generation mode in the high half, the coordinate set in the
	// low one.  Unlike the pixel half, both matter here - generating the coordinate is this
	// shader's job now.
	DWORD TextureCoordinateIndex;

	// D3DTSS_TEXTURETRANSFORMFLAGS: a count of how many coordinates the texture matrix produces,
	// optionally with D3DTTFF_PROJECTED.  D3DTTFF_DISABLE means the matrix is not applied at all.
	DWORD TextureTransformFlags;
};

struct VertexPipelineDescription
{
	// The flexible vertex format the draw is reading, which says whether there is a normal and
	// whether there is a vertex colour to read D3DMCS_COLOR out of.
	DWORD FVF;

	bool LightingEnabled;
	bool SpecularEnabled;

	// D3DRS_COLORVERTEX.  With it off the vertex colour is ignored whatever the material sources
	// say, which is how a lit draw with a colour in its vertices still comes out unlit by it.
	bool ColourVertexEnabled;

	// D3DMCS_MATERIAL or D3DMCS_COLOR, one each.  D3DMCS_COLOR2 is the specular vertex colour and
	// nothing in the game selects it.
	DWORD DiffuseMaterialSource;
	DWORD AmbientMaterialSource;
	DWORD EmissiveMaterialSource;
	DWORD SpecularMaterialSource;

	unsigned LightCount;
	VertexLightDescription Lights[MAXIMUM_VERTEX_LIGHTS];

	unsigned StageCount;
	VertexStageDescription Stages[MAXIMUM_VERTEX_STAGES];

	bool FogEnabled;

	// D3DRS_FOGVERTEXMODE: D3DFOG_LINEAR, D3DFOG_EXP or D3DFOG_EXP2.  D3DFOG_NONE with the fog
	// enabled is table fog, which is the pixel half's business and not this one's.
	DWORD FogVertexMode;
};

// Which profile the generated text is for.  The two differ in the output semantic and in how the
// constants are declared; the arithmetic between them is the same text.
enum VertexShaderTarget
{
	VERTEX_SHADER_TARGET_D3D9,
	VERTEX_SHADER_TARGET_D3D11
};

bool VertexShader_Generate(const VertexPipelineDescription & description,
	VertexShaderTarget target, std::string & hlsl);

// Two descriptions with the same key generate the same text, so the key is what a cache is built
// on.  It carries no matrix, no colour and no light direction: those are constants the draw
// uploads, not shapes of the program.
std::string VertexShader_Key(const VertexPipelineDescription & description);

#endif // FFVERTEX_H
