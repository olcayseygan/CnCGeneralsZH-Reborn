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
** The fixed-function texture stages, written out as HLSL.
**
** D3D11 has no texture stage combiners, so RENDERER-ROADMAP.md's phase 2 has to say in a shader
** what the stages were computing.  -ffprobe counted what the game actually asks for across four
** maps: 28 distinct combiner programs, never more than two stages, and a vocabulary of five
** operations over four arguments.  This turns one of those descriptions into the shader.
**
** It generates for D3D9 first, on purpose.  A pixel shader on the existing device can be compared
** against the fixed-function pipeline it replaces with tree-check.ps1, which turns the phase from
** one untestable port into a generator that is proved against the real game before any of it is
** carried to a second backend.  Fog and the alpha test are not generated for that reason: D3D9
** still applies both around a pixel shader, so leaving them alone keeps the comparison to the one
** thing being replaced.  D3D11 has neither and will want them here.
*/

#ifndef FFSHADER_H
#define FFSHADER_H

#include <d3d9.h>

#include <string>

// Two is what the game uses.  The generator refuses a description with more rather than emitting a
// shader nobody has compared against anything.
const unsigned MAXIMUM_COMBINER_STAGES = 2;

// One texture stage, in the terms D3D8 set it in.  Arguments carry D3DTA_COMPLEMENT and
// D3DTA_ALPHAREPLICATE the way the device does; the generator applies both.
struct CombinerStage
{
	DWORD ColourOperation;
	DWORD ColourArgument0;
	DWORD ColourArgument1;
	DWORD ColourArgument2;
	DWORD AlphaOperation;
	DWORD AlphaArgument0;
	DWORD AlphaArgument1;
	DWORD AlphaArgument2;
	DWORD TextureCoordinateIndex;
	bool  TextureBound;
};

// What a draw asks the combiners to compute.  Stages past StageCount are not read.
// The two pieces of the D3D9 pixel pipeline that are neither texture stages nor shader
// instructions.  D3D9 applies both around a bound pixel shader and D3D11 has neither, so they are
// part of the program on one profile and absent from it on the other.  The alpha reference and the
// fog colour are not here: they are uniforms, and two draws differing only in one are one program.
struct PixelPipelineDescription
{
	bool AlphaTestEnabled;

	// D3DCMP_*, the comparison the surviving alpha has to pass.
	DWORD AlphaFunction;

	bool FogEnabled;
};

struct CombinerDescription
{
	CombinerStage Stages[MAXIMUM_COMBINER_STAGES];
	unsigned      StageCount;

	// Only read when generating for D3D11.  On D3D9 the device still applies both itself around a
	// bound pixel shader, and generating them there would apply each of them twice.
	PixelPipelineDescription PixelPipeline;
};

// The HLSL for one description, or false when the description names an operation or an argument
// this does not generate.  A refusal is not a failure: the caller keeps the fixed-function path for
// that draw, which is the only reason an unmeasured operation is safe to meet at run time.
// Which profile the generated text is for.  ps_2_0 and ps_4_0 are not the same language: a sampler
// is a sampler2D read with tex2D in one and a Texture2D beside a SamplerState read with Sample in
// the other, the output semantic is COLOR against SV_Target, and the texture factor is a constant
// register against a constant buffer.  The arithmetic between them is the same text.
enum CombinerShaderTarget
{
	COMBINER_SHADER_TARGET_D3D9,
	COMBINER_SHADER_TARGET_D3D11
};

bool CombinerShader_Generate(const CombinerDescription & description, CombinerShaderTarget target,
	std::string & hlsl);

// The description two draws share iff they can share a compiled shader.  Stages past StageCount are
// zeroed, so two descriptions that differ only in a stage nobody reads compare equal.
std::string CombinerShader_Key(const CombinerDescription & description);

#endif
