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

// 22.8% of every draw call measured: one stage, SELECTARG1 on the texture, coordinate set 2.
TEST(ffshader_the_most_used_combination_selects_the_texture)
{
	CombinerDescription description;
	description.StageCount = 1;
	description.Stages[0] = one_stage(D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT,
		D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT, 131072 | 2, true);

	std::string hlsl;
	CHECK(CombinerShader_Generate(description, hlsl));
	CHECK(contains(hlsl, "texel = tex2D(Sampler0, input.TexCoord2)"));
	CHECK(contains(hlsl, "current.rgb = texel.rgb"));
	CHECK(contains(hlsl, "current.a   = texel.a"));
}

// The second most used: MODULATE of the texture against the diffuse colour.
TEST(ffshader_modulate_multiplies_texture_by_diffuse)
{
	CombinerDescription description;
	description.StageCount = 1;
	description.Stages[0] = one_stage(D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE,
		D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE, 0, true);

	std::string hlsl;
	CHECK(CombinerShader_Generate(description, hlsl));
	CHECK(contains(hlsl, "(texel * input.Diffuse).rgb"));
	CHECK(contains(hlsl, "(texel * input.Diffuse).a"));
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
	CHECK(CombinerShader_Generate(description, hlsl));
	CHECK(contains(hlsl, "texel = float4(1.0, 1.0, 1.0, 1.0)"));
	CHECK(!contains(hlsl, "tex2D"));
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
	CHECK(CombinerShader_Generate(description, hlsl));
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
	CHECK(CombinerShader_Generate(description, hlsl));
	CHECK(contains(hlsl, "(1.0 - (texel).aaaa).rgb"));
	CHECK(contains(hlsl, "(1.0 - texel).a"));
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
	CHECK(!CombinerShader_Generate(description, hlsl));
}

TEST(ffshader_refuses_more_stages_than_the_game_uses)
{
	CombinerDescription description;
	description.StageCount = MAXIMUM_COMBINER_STAGES + 1;
	description.Stages[0] = one_stage(D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT,
		D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT, 0, true);

	std::string hlsl;
	CHECK(!CombinerShader_Generate(description, hlsl));
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

// The coordinate generation mode rides in the high bits of D3DTSS_TEXCOORDINDEX and the coordinate
// set in the low ones.  Keying on the whole word would compile the same program twice.
TEST(ffshader_the_key_reads_the_coordinate_set_not_the_generation_mode)
{
	CombinerDescription plain;
	plain.StageCount = 1;
	plain.Stages[0] = one_stage(D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE,
		D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE, 1, true);

	CombinerDescription generated = plain;
	generated.Stages[0].TextureCoordinateIndex = 131072 | 1;

	CHECK_STR(CombinerShader_Key(plain).c_str(), CombinerShader_Key(generated).c_str());
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
		CHECK(CombinerShader_Generate(description, hlsl));
		CHECK(contains(hlsl, "float4 main(Input input) : COLOR"));
		CHECK(contains(hlsl, "return current;"));
	}
}
