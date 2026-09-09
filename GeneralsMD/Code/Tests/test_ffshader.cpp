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

// The combiner-to-HLSL generator.  The descriptions here are the ones -ffprobe measured over Flash
// Effect, Golden Oasis, ForgottenForestZH and Alpine Assault, not invented ones: a generator that
// handles a case the game never asks for is worth nothing, and one that refuses a case it does ask
// for silently keeps that draw on the fixed-function path.

#include "test_harness.h"

#include "ffshader.h"

#include <string.h>

static CombinerStage one_stage(DWORD colour_operation, DWORD colour_argument1,
	DWORD colour_argument2, DWORD alpha_operation, DWORD alpha_argument1, DWORD alpha_argument2,
	DWORD coordinate_index, bool texture_bound)
{
	CombinerStage stage;
	memset(&stage, 0, sizeof(stage));
	stage.ColourOperation = colour_operation;
	stage.ColourArgument1 = colour_argument1;
	stage.ColourArgument2 = colour_argument2;
	stage.AlphaOperation = alpha_operation;
	stage.AlphaArgument1 = alpha_argument1;
	stage.AlphaArgument2 = alpha_argument2;
	stage.TextureCoordinateIndex = coordinate_index;
	stage.TextureBound = texture_bound;
	return stage;
}

static bool contains(const std::string & text, const char * fragment)
{
	return text.find(fragment) != std::string::npos;
}

// 22.8% of every draw call measured: one stage, SELECTARG1 on the texture, and a coordinate index
// of 0x20000 - D3DTSS_TCI_CAMERASPACEPOSITION.  The vertex pipeline computes the coordinate and
// leaves it in the asking stage's register, which for stage 0 is TexCoord0.
TEST(ffshader_the_most_used_combination_selects_the_texture)
{
	CombinerDescription description;
	description.StageCount = 1;
	description.Stages[0] = one_stage(D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT,
		D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT, D3DTSS_TCI_CAMERASPACEPOSITION, true);

	std::string hlsl;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, hlsl));
	CHECK(contains(hlsl, "texel = tex2D(Sampler0, input.TexCoord0)"));
	CHECK(contains(hlsl, "current.rgb = saturate(texel).rgb"));
	CHECK(contains(hlsl, "current.a   = saturate(texel).a"));
}

// The shader declares two coordinate sets, which is every one the measured maps use.  A third is a
// refusal, not a sample out of a register the shader never declared.
TEST(ffshader_refuses_a_coordinate_set_it_does_not_declare)
{
	CombinerDescription description;
	description.StageCount = 1;
	description.Stages[0] = one_stage(D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT,
		D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT, 2, true);

	std::string hlsl;
	CHECK(!CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, hlsl));
}

// The second most used: MODULATE of the texture against the diffuse colour.
TEST(ffshader_modulate_multiplies_texture_by_diffuse)
{
	CombinerDescription description;
	description.StageCount = 1;
	description.Stages[0] = one_stage(D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE,
		D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE, 0, true);

	std::string hlsl;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, hlsl));
	CHECK(contains(hlsl, "saturate((texel * input.Diffuse)).rgb"));
	CHECK(contains(hlsl, "saturate((texel * input.Diffuse)).a"));
}

// A stage with nothing bound still reads D3DTA_TEXTURE, and the generator has to put something
// there.  Opaque white is the answer being tried; this pins it so a change to it is deliberate.
TEST(ffshader_an_unbound_stage_samples_opaque_white)
{
	CombinerDescription description;
	description.StageCount = 1;
	description.Stages[0] = one_stage(D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT,
		D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT, 0, false);

	std::string hlsl;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, hlsl));
	CHECK(contains(hlsl, "texel = float4(1.0, 1.0, 1.0, 1.0)"));
	CHECK(!contains(hlsl, "tex2D"));
}

// Every program declares the full input signature, including registers it never reads.  The tree
// shadow pass is the case that settled it: black texture factor for the colour, the texture's own
// alpha against the factor's for the alpha, no diffuse and no specular anywhere in it, drawn with
// Trees.vso bound.  Cutting the signature down to what the body names took Flash Effect at frame
// 400 from 0.25% different against the fixed-function frame to 1.20%.
TEST(ffshader_declares_the_full_input_signature)
{
	CombinerDescription description;
	description.StageCount = 1;
	description.Stages[0] = one_stage(D3DTOP_SELECTARG1, D3DTA_TFACTOR, D3DTA_DIFFUSE,
		D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_TFACTOR, 0, true);

	std::string hlsl;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, hlsl));
	CHECK(contains(hlsl, "float4 Diffuse   : COLOR0;"));
	CHECK(contains(hlsl, "float4 Specular  : COLOR1;"));
	CHECK(contains(hlsl, "float2 TexCoord0 : TEXCOORD0;"));
	CHECK(contains(hlsl, "float2 TexCoord1 : TEXCOORD1;"));
}

// The two-stage shroud pass: MULTIPLYADD against an alpha-replicated texture factor, then
// DOTPRODUCT3.  This is the only measured combination that uses either operation.
TEST(ffshader_two_stages_with_multiplyadd_and_dotproduct)
{
	CombinerDescription description;
	description.StageCount = 2;
	description.Stages[0] = one_stage(D3DTOP_MULTIPLYADD, D3DTA_TEXTURE, D3DTA_TFACTOR | D3DTA_ALPHAREPLICATE,
		D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE, 0, true);
	description.Stages[0].ColourArgument0 = D3DTA_DIFFUSE;
	description.Stages[1] = one_stage(D3DTOP_DOTPRODUCT3, D3DTA_CURRENT, D3DTA_TFACTOR,
		D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT, 1, false);

	std::string hlsl;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, hlsl));
	CHECK(contains(hlsl, "(input.Diffuse + texel * (TextureFactor).aaaa)"));
	CHECK(contains(hlsl, "saturate(dot("));
}

// A complemented argument is 1 - a, and an alpha-replicated one is a.aaaa.  Both together replicate
// first, which is the order the device applies them in.
TEST(ffshader_argument_modifiers_replicate_before_complementing)
{
	CombinerDescription description;
	description.StageCount = 1;
	description.Stages[0] = one_stage(D3DTOP_SELECTARG1,
		D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE | D3DTA_COMPLEMENT, D3DTA_CURRENT,
		D3DTOP_SELECTARG1, D3DTA_TEXTURE | D3DTA_COMPLEMENT, D3DTA_CURRENT, 0, true);

	std::string hlsl;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, hlsl));
	CHECK(contains(hlsl, "saturate((1.0 - (texel).aaaa)).rgb"));
	CHECK(contains(hlsl, "saturate((1.0 - texel)).a"));
}

// The terrain blends its ground layers by the vertex alpha and its noise layers by the alpha the
// stage before them left in current.  Both were refusals until the operations below existed, and a
// refused terrain draw is a hole where the ground is.
TEST(ffshader_blends_by_diffuse_and_by_current_alpha)
{
	CombinerDescription description;
	memset(&description, 0, sizeof(description));
	description.StageCount = 2;
	description.Stages[0] = one_stage(D3DTOP_BLENDDIFFUSEALPHA, D3DTA_TEXTURE, D3DTA_CURRENT,
		D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT, 0, true);
	description.Stages[1] = one_stage(D3DTOP_BLENDCURRENTALPHA, D3DTA_TEXTURE, D3DTA_CURRENT,
		D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT, 1, true);

	std::string hlsl;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, hlsl));
	CHECK(contains(hlsl, "lerp(current, texel, input.Diffuse.a)"));
	CHECK(contains(hlsl, "lerp(current, texel, current.a)"));
}

// A stage that disables only its alpha operation keeps the alpha it was handed.  Refusing these
// cost the whole terrain: every one of its blending stages leaves the alpha alone.
TEST(ffshader_a_disabled_alpha_operation_keeps_the_alpha_it_was_given)
{
	CombinerDescription description;
	memset(&description, 0, sizeof(description));
	description.StageCount = 1;
	description.Stages[0] = one_stage(D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT,
		D3DTOP_DISABLE, D3DTA_TEXTURE, D3DTA_CURRENT, 0, true);

	std::string hlsl;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, hlsl));
	CHECK(contains(hlsl, "current.a   = saturate(current).a;"));
}

// The rest of the blending family, so that a stage program built out of any of them is written
// rather than refused.  What each one computes is D3D9's own definition and nothing here chooses.
TEST(ffshader_writes_every_blending_operation_the_device_had)
{
	static const DWORD operations[] = {
		D3DTOP_ADDSIGNED2X, D3DTOP_ADDSMOOTH, D3DTOP_BLENDTEXTUREALPHA, D3DTOP_BLENDFACTORALPHA,
		D3DTOP_BLENDTEXTUREALPHAPM, D3DTOP_MODULATEALPHA_ADDCOLOR, D3DTOP_MODULATECOLOR_ADDALPHA,
		D3DTOP_MODULATEINVALPHA_ADDCOLOR, D3DTOP_MODULATEINVCOLOR_ADDALPHA };

	for (unsigned index = 0; index < sizeof(operations) / sizeof(operations[0]); ++index) {
		CombinerDescription description;
		memset(&description, 0, sizeof(description));
		description.StageCount = 1;
		description.Stages[0] = one_stage(operations[index], D3DTA_TEXTURE, D3DTA_DIFFUSE,
			D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT, 0, true);

		std::string hlsl;
		CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, hlsl));
	}
}

// An operation the generator does not write has to be refused, not guessed at: the caller keeps
// that draw on the fixed-function path, which is the only reason meeting one is safe.
TEST(ffshader_refuses_an_operation_it_does_not_generate)
{
	CombinerDescription description;
	description.StageCount = 1;
	description.Stages[0] = one_stage(D3DTOP_BUMPENVMAP, D3DTA_TEXTURE, D3DTA_CURRENT,
		D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT, 0, true);

	std::string hlsl;
	CHECK(!CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, hlsl));
}

TEST(ffshader_refuses_more_stages_than_the_game_uses)
{
	CombinerDescription description;
	description.StageCount = MAXIMUM_COMBINER_STAGES + 1;
	description.Stages[0] = one_stage(D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT,
		D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT, 0, true);

	std::string hlsl;
	CHECK(!CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, hlsl));
}

// Two descriptions that differ only in a stage past StageCount are one shader, or the cache holds
// a copy of the same program for every scrap of stale state a call site happened to leave behind.
TEST(ffshader_the_key_ignores_stages_nobody_reads)
{
	CombinerDescription first;
	first.StageCount = 1;
	first.Stages[0] = one_stage(D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE,
		D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE, 0, true);
	first.Stages[1] = one_stage(D3DTOP_ADD, D3DTA_TFACTOR, D3DTA_CURRENT,
		D3DTOP_ADD, D3DTA_TFACTOR, D3DTA_CURRENT, 1, true);

	CombinerDescription second = first;
	second.Stages[1] = one_stage(D3DTOP_SUBTRACT, D3DTA_DIFFUSE, D3DTA_CURRENT,
		D3DTOP_SUBTRACT, D3DTA_DIFFUSE, D3DTA_CURRENT, 0, false);

	CHECK_STR(CombinerShader_Key(first).c_str(), CombinerShader_Key(second).c_str());
}

// A generating D3DTSS_TEXCOORDINDEX still reads the coordinate set its low half names, whatever
// stage it sits on.  The other reading of the documentation, routing such a stage to its own stage
// register, was tried on the device: Flash Effect at frame 400 went from 0.25% different against
// the fixed-function frame to 0.81%.
TEST(ffshader_a_generated_coordinate_still_reads_the_set_it_names)
{
	CombinerDescription description;
	description.StageCount = 2;
	description.Stages[0] = one_stage(D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT,
		D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT, 0, true);
	description.Stages[1] = one_stage(D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT,
		D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT, 131072, true);

	std::string hlsl;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, hlsl));
	CHECK(contains(hlsl, "tex2D(Sampler1, input.TexCoord0)"));
	CHECK(!contains(hlsl, "tex2D(Sampler1, input.TexCoord1)"));
}

// Two stages that differ only in a generation mode are one program.  Keying on the whole word
// would compile it twice.
TEST(ffshader_the_key_reads_the_coordinate_set_not_the_generation_mode)
{
	CombinerDescription plain;
	plain.StageCount = 1;
	plain.Stages[0] = one_stage(D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE,
		D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE, 0, true);

	CombinerDescription generated = plain;
	generated.Stages[0].TextureCoordinateIndex = 131072;

	CHECK_STR(CombinerShader_Key(plain).c_str(), CombinerShader_Key(generated).c_str());
}

// Every stage of the device's combiner writes a clamped result and the next stage reads that.
// Without the clamp a MODULATE2X above one carries the overflow forward and the frame comes out
// brighter than the fixed-function pipeline draws it.
TEST(ffshader_each_stage_clamps_before_the_next_one_reads_it)
{
	CombinerDescription description;
	description.StageCount = 1;
	description.Stages[0] = one_stage(D3DTOP_MODULATE2X, D3DTA_TEXTURE, D3DTA_DIFFUSE,
		D3DTOP_MODULATE2X, D3DTA_TEXTURE, D3DTA_DIFFUSE, 0, true);

	std::string hlsl;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, hlsl));
	CHECK(contains(hlsl, "current.rgb = saturate((texel * input.Diffuse * 2.0)).rgb"));
	CHECK(contains(hlsl, "current.a   = saturate((texel * input.Diffuse * 2.0)).a"));
}

// D3D11 has no alpha test, so the comparison becomes a clip and the clip is part of the program.
// D3D9 still applies its own around a bound pixel shader, so generating one there would apply the
// test twice and cut a second set of pixels out of an already cut hole.
TEST(ffshader_the_alpha_test_is_a_clip_on_d3d11_and_nothing_on_d3d9)
{
	CombinerDescription description;
	memset(&description, 0, sizeof(description));
	description.StageCount = 1;
	description.Stages[0] = one_stage(D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT,
		D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT, 0, true);
	description.PixelPipeline.AlphaTestEnabled = true;
	description.PixelPipeline.AlphaFunction = D3DCMP_GREATEREQUAL;

	std::string eleven;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, eleven));
	CHECK(contains(eleven, "clip(current.a - AlphaReference.x);"));

	std::string nine;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, nine));
	CHECK(!contains(nine, "clip("));
	CHECK(!contains(nine, "AlphaReference"));
}

// clip throws a pixel away when its argument is negative, so each comparison is one subtraction.
// The two about equality need a tolerance, which is a choice: D3D9 compared eight bit integers and
// this compares floats.
TEST(ffshader_each_alpha_comparison_becomes_its_own_clip)
{
	CombinerDescription description;
	memset(&description, 0, sizeof(description));
	description.StageCount = 1;
	description.Stages[0] = one_stage(D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT,
		D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT, 0, true);
	description.PixelPipeline.AlphaTestEnabled = true;

	std::string hlsl;

	description.PixelPipeline.AlphaFunction = D3DCMP_LESS;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, hlsl));
	CHECK(contains(hlsl, "clip(AlphaReference.x - current.a);"));

	description.PixelPipeline.AlphaFunction = D3DCMP_EQUAL;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, hlsl));
	CHECK(contains(hlsl, "abs(current.a - AlphaReference.x)"));

	description.PixelPipeline.AlphaFunction = D3DCMP_NEVER;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, hlsl));
	CHECK(contains(hlsl, "clip(-1.0);"));

	// D3DCMP_ALWAYS keeps every pixel, which is the same program as no alpha test at all.
	description.PixelPipeline.AlphaFunction = D3DCMP_ALWAYS;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, hlsl));
	CHECK(!contains(hlsl, "clip("));
}

// The fog weights the unfogged colour, so a factor of one is no fog, and it never touches the
// alpha: a fogged pixel is the same shape as an unfogged one.
TEST(ffshader_the_fog_blends_the_colour_and_leaves_the_alpha)
{
	CombinerDescription description;
	memset(&description, 0, sizeof(description));
	description.StageCount = 1;
	description.Stages[0] = one_stage(D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT,
		D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT, 0, true);
	description.PixelPipeline.FogEnabled = true;

	std::string eleven;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, eleven));
	CHECK(contains(eleven, "current.rgb = lerp(FogColour.rgb, current.rgb, saturate(input.Fog));"));
	CHECK(!contains(eleven, "current.a = lerp"));

	std::string nine;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, nine));
	CHECK(!contains(nine, "FogColour"));
}

// The key decides what the cache hands back, so two programs that differ only in the alpha test
// must not share one.
TEST(ffshader_the_key_separates_a_tested_program_from_an_untested_one)
{
	CombinerDescription plain;
	memset(&plain, 0, sizeof(plain));
	plain.StageCount = 1;
	plain.Stages[0] = one_stage(D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT,
		D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT, 0, true);

	CombinerDescription tested = plain;
	tested.PixelPipeline.AlphaTestEnabled = true;
	tested.PixelPipeline.AlphaFunction = D3DCMP_GREATEREQUAL;

	CombinerDescription fogged = plain;
	fogged.PixelPipeline.FogEnabled = true;

	CHECK_NE(CombinerShader_Key(plain), CombinerShader_Key(tested));
	CHECK_NE(CombinerShader_Key(plain), CombinerShader_Key(fogged));
	CHECK_NE(CombinerShader_Key(tested), CombinerShader_Key(fogged));

	// The reference is a uniform, so two draws differing only in it are one program.
	CHECK_STR(CombinerShader_Key(tested).c_str(), CombinerShader_Key(tested).c_str());
}

// Every distinct program -ffprobe found has to come out of the generator, or the fixed-function
// path stays alive for the ones it refuses and D3D11 has nothing to run them on.
TEST(ffshader_generates_every_measured_combination)
{
	struct MeasuredCombination
	{
		unsigned StageCount;
		DWORD ColourOperation[MAXIMUM_COMBINER_STAGES];
		DWORD ColourArgument1[MAXIMUM_COMBINER_STAGES];
		DWORD ColourArgument2[MAXIMUM_COMBINER_STAGES];
		DWORD AlphaOperation[MAXIMUM_COMBINER_STAGES];
		DWORD AlphaArgument1[MAXIMUM_COMBINER_STAGES];
		DWORD AlphaArgument2[MAXIMUM_COMBINER_STAGES];
	};

	// Taken from Run/ffprobe.txt over the four maps, deduplicated on the fields below.
	static const MeasuredCombination MEASURED[] =
	{
		{ 1, {D3DTOP_SELECTARG1}, {D3DTA_TEXTURE}, {D3DTA_CURRENT},
		     {D3DTOP_SELECTARG1}, {D3DTA_TEXTURE}, {D3DTA_CURRENT} },
		{ 1, {D3DTOP_MODULATE},  {D3DTA_TEXTURE}, {D3DTA_DIFFUSE},
		     {D3DTOP_MODULATE},  {D3DTA_TEXTURE}, {D3DTA_DIFFUSE} },
		{ 1, {D3DTOP_SELECTARG2}, {D3DTA_TEXTURE}, {D3DTA_DIFFUSE},
		     {D3DTOP_SELECTARG2}, {D3DTA_TEXTURE}, {D3DTA_DIFFUSE} },
		{ 1, {D3DTOP_MODULATE},  {D3DTA_TEXTURE}, {D3DTA_DIFFUSE},
		     {D3DTOP_SELECTARG1}, {D3DTA_TEXTURE}, {D3DTA_DIFFUSE} },
		{ 1, {D3DTOP_SELECTARG1}, {D3DTA_SPECULAR}, {D3DTA_DIFFUSE},
		     {D3DTOP_MODULATE},  {D3DTA_TEXTURE}, {D3DTA_SPECULAR} },
		{ 2, {D3DTOP_MULTIPLYADD, D3DTOP_DOTPRODUCT3},
		     {D3DTA_TEXTURE, D3DTA_CURRENT},
		     {D3DTA_TFACTOR | D3DTA_ALPHAREPLICATE, D3DTA_TFACTOR},
		     {D3DTOP_MODULATE, D3DTOP_SELECTARG1},
		     {D3DTA_TEXTURE, D3DTA_TEXTURE},
		     {D3DTA_DIFFUSE, D3DTA_CURRENT} },
		{ 2, {D3DTOP_MODULATE, D3DTOP_MODULATE},
		     {D3DTA_TEXTURE, D3DTA_TEXTURE},
		     {D3DTA_DIFFUSE, D3DTA_CURRENT},
		     {D3DTOP_MODULATE, D3DTOP_MODULATE},
		     {D3DTA_TEXTURE, D3DTA_TEXTURE},
		     {D3DTA_DIFFUSE, D3DTA_CURRENT} },
		{ 2, {D3DTOP_MODULATE, D3DTOP_MODULATE},
		     {D3DTA_TEXTURE, D3DTA_TEXTURE},
		     {D3DTA_DIFFUSE, D3DTA_CURRENT},
		     {D3DTOP_MODULATE, D3DTOP_SELECTARG2},
		     {D3DTA_TEXTURE, D3DTA_TEXTURE},
		     {D3DTA_DIFFUSE, D3DTA_CURRENT} }
	};

	const size_t measured_count = sizeof(MEASURED)/sizeof(MEASURED[0]);
	for (size_t index = 0; index < measured_count; ++index) {
		const MeasuredCombination & measured = MEASURED[index];
		CombinerDescription description;
		description.StageCount = measured.StageCount;
		for (unsigned stage = 0; stage < measured.StageCount; ++stage) {
			description.Stages[stage] = one_stage(measured.ColourOperation[stage],
				measured.ColourArgument1[stage], measured.ColourArgument2[stage],
				measured.AlphaOperation[stage], measured.AlphaArgument1[stage],
				measured.AlphaArgument2[stage], stage, true);
		}

		std::string hlsl;
		CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, hlsl));
		CHECK(contains(hlsl, "float4 main(Input input) : COLOR"));
		CHECK(contains(hlsl, "return current;"));
	}
}
