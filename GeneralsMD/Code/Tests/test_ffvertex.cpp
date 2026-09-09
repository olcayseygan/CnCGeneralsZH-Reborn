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

// The fixed-function vertex pipeline written out as HLSL.  What is checked here is the shape of
// the program - which term is present, which register it reads, which case is refused - because
// that is what a description decides.  Whether the arithmetic matches the device is a question for
// a picture, and the generator exists in the D3D9 form so that picture can be taken.

#include "test_harness.h"

#include "ffvertex.h"

#include <string.h>

static const DWORD FVF_XYZNDUV2 = D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX2|D3DFVF_DIFFUSE;
static const DWORD FVF_XYZUV1 = D3DFVF_XYZ|D3DFVF_TEX1;

static bool contains(const std::string & text, const char * fragment)
{
	return text.find(fragment) != std::string::npos;
}

// Everything off: no lighting, no fog, one pass-through coordinate set.  This is the shape most of
// the game's draws are, and every other test starts from it.
static VertexPipelineDescription plain_description()
{
	VertexPipelineDescription description;
	memset(&description, 0, sizeof(description));
	description.FVF = FVF_XYZNDUV2;
	description.LightingEnabled = false;
	description.SpecularEnabled = false;
	description.ColourVertexEnabled = true;
	description.DiffuseMaterialSource = D3DMCS_COLOR1;
	description.AmbientMaterialSource = D3DMCS_MATERIAL;
	description.EmissiveMaterialSource = D3DMCS_MATERIAL;
	description.SpecularMaterialSource = D3DMCS_MATERIAL;
	description.LightCount = 0;
	description.StageCount = 1;
	description.Stages[0].TextureCoordinateIndex = D3DTSS_TCI_PASSTHRU | 0;
	description.Stages[0].TextureTransformFlags = D3DTTFF_DISABLE;
	description.FogEnabled = false;
	description.FogVertexMode = D3DFOG_NONE;
	return description;
}

TEST(ffvertex_an_unlit_draw_carries_the_vertex_colour_through)
{
	std::string hlsl;
	CHECK(VertexShader_Generate(plain_description(), VERTEX_SHADER_TARGET_D3D9, hlsl));
	CHECK(contains(hlsl, "output.Diffuse = input.Diffuse;"));
	CHECK(contains(hlsl, "output.Position = mul(model_position, WorldViewProjection);"));

	// Nothing lit means nothing to light with.
	CHECK(!contains(hlsl, "diffuse_light"));
	CHECK(!contains(hlsl, "Light0Diffuse"));
}

// A format with no colour in it and lighting off is white, not black and not whatever the last
// draw left in the register.
TEST(ffvertex_an_unlit_draw_with_no_vertex_colour_is_white)
{
	VertexPipelineDescription description = plain_description();
	description.FVF = FVF_XYZUV1;

	std::string hlsl;
	CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D9, hlsl));
	CHECK(contains(hlsl, "output.Diffuse = float4(1.0, 1.0, 1.0, 1.0);"));
}

TEST(ffvertex_a_directional_light_needs_no_attenuation)
{
	VertexPipelineDescription description = plain_description();
	description.LightingEnabled = true;
	description.LightCount = 1;
	description.Lights[0].Type = D3DLIGHT_DIRECTIONAL;

	std::string hlsl;
	CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D9, hlsl));
	CHECK(contains(hlsl, "to_light = -normalize(Light0Direction.xyz);"));
	CHECK(contains(hlsl, "attenuation = 1.0;"));
	CHECK(contains(hlsl, "diffuse_light += Light0Diffuse.rgb"));

	// A directional light has no distance, so its attenuation and its cone are never read.  Both
	// are still declared: the register layout is the same six for every light whatever its type,
	// so a light's first register is a multiplication rather than a running total.
	CHECK(!contains(hlsl, "Light0Attenuation.x + Light0Attenuation.y"));
	CHECK(!contains(hlsl, "spot_cosine"));
	CHECK(contains(hlsl, "float4 Light0Attenuation : register("));
}

TEST(ffvertex_a_point_light_attenuates_and_stops_at_its_range)
{
	VertexPipelineDescription description = plain_description();
	description.LightingEnabled = true;
	description.LightCount = 1;
	description.Lights[0].Type = D3DLIGHT_POINT;

	std::string hlsl;
	CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D9, hlsl));
	CHECK(contains(hlsl, "Light0Attenuation.x + Light0Attenuation.y * distance"));
	CHECK(contains(hlsl, "step(distance, Light0Attenuation.w)"));

	// A point light has no cone, so the cone terms are declared and never read.
	CHECK(!contains(hlsl, "spot_cosine"));
}

TEST(ffvertex_a_spot_light_adds_the_cone_to_the_point_light_terms)
{
	VertexPipelineDescription description = plain_description();
	description.LightingEnabled = true;
	description.LightCount = 1;
	description.Lights[0].Type = D3DLIGHT_SPOT;

	std::string hlsl;
	CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D9, hlsl));
	CHECK(contains(hlsl, "step(distance, Light0Attenuation.w)"));
	CHECK(contains(hlsl, "spot_cosine"));
	CHECK(contains(hlsl, "Light0Spot.z"));
}

// D3DMCS_COLOR1 reads the diffuse vertex colour, but only where the format carries one and
// D3DRS_COLORVERTEX allows it.  D3D9 falls back to the material in both of those cases and so does
// this, which is a fallback rather than a refusal because the device makes it silently too.
TEST(ffvertex_a_colour_material_source_falls_back_to_the_material)
{
	VertexPipelineDescription lit = plain_description();
	lit.LightingEnabled = true;
	lit.LightCount = 1;
	lit.Lights[0].Type = D3DLIGHT_DIRECTIONAL;
	lit.DiffuseMaterialSource = D3DMCS_COLOR1;

	std::string with_colour;
	CHECK(VertexShader_Generate(lit, VERTEX_SHADER_TARGET_D3D9, with_colour));
	CHECK(contains(with_colour, "input.Diffuse.rgb * diffuse_light"));

	// The same description with D3DRS_COLORVERTEX off.
	lit.ColourVertexEnabled = false;
	std::string without_colour;
	CHECK(VertexShader_Generate(lit, VERTEX_SHADER_TARGET_D3D9, without_colour));
	CHECK(contains(without_colour, "MaterialDiffuse.rgb * diffuse_light"));

	// And with the colour allowed but absent from the format.
	lit.ColourVertexEnabled = true;
	lit.FVF = D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX1;
	std::string without_format;
	CHECK(VertexShader_Generate(lit, VERTEX_SHADER_TARGET_D3D9, without_format));
	CHECK(contains(without_format, "MaterialDiffuse.rgb * diffuse_light"));
}

// The three generation modes the game uses, each of which is a different vector.  These are the
// terms that make an environment map look like an environment map, and there is no reading of the
// picture that says which one produced it.
TEST(ffvertex_each_coordinate_generation_mode_produces_its_own_vector)
{
	VertexPipelineDescription description = plain_description();

	description.Stages[0].TextureCoordinateIndex = D3DTSS_TCI_CAMERASPACEPOSITION;
	std::string position;
	CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D9, position));
	CHECK(contains(position, "generated0 = float4(view_position.xyz, 1.0)"));

	description.Stages[0].TextureCoordinateIndex = D3DTSS_TCI_CAMERASPACENORMAL;
	std::string normal;
	CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D9, normal));
	CHECK(contains(normal, "generated0 = float4(view_normal, 1.0)"));

	description.Stages[0].TextureCoordinateIndex = D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR;
	std::string reflection;
	CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D9, reflection));
	CHECK(contains(reflection, "reflect(normalize(view_position.xyz), view_normal)"));
}

TEST(ffvertex_a_texture_matrix_is_applied_and_a_projected_one_divides)
{
	VertexPipelineDescription description = plain_description();
	description.Stages[0].TextureTransformFlags = D3DTTFF_COUNT2;

	std::string plain_matrix;
	CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D9, plain_matrix));
	CHECK(contains(plain_matrix, "generated0 = mul(generated0, TextureMatrix0);"));
	CHECK(!contains(plain_matrix, "generated0.xy /="));

	description.Stages[0].TextureTransformFlags = D3DTTFF_COUNT3 | D3DTTFF_PROJECTED;
	std::string projected;
	CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D9, projected));
	CHECK(contains(projected, "generated0 = mul(generated0, TextureMatrix0);"));
	CHECK(contains(projected, "generated0.xy /= max(generated0.z, 0.0001);"));
}

TEST(ffvertex_the_three_fog_modes_each_write_their_own_factor)
{
	VertexPipelineDescription description = plain_description();
	description.FogEnabled = true;

	description.FogVertexMode = D3DFOG_LINEAR;
	std::string linear;
	CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D9, linear));
	CHECK(contains(linear, "FogParameters.y - view_position.z"));

	description.FogVertexMode = D3DFOG_EXP;
	std::string exponential;
	CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D9, exponential));
	CHECK(contains(exponential, "exp(-FogParameters.z * view_position.z)"));

	description.FogVertexMode = D3DFOG_EXP2;
	std::string squared;
	CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D9, squared));
	CHECK(contains(squared, "FogParameters.z * FogParameters.z"));

	// With the fog off the factor is one, which is the weight of the unfogged colour.
	description.FogEnabled = false;
	std::string none;
	CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D9, none));
	CHECK(contains(none, "output.Fog = 1.0;"));
}

// A stage the description does not reach still has an output.  Left unwritten it carries whatever
// was on the stack, and the pixel shader samples a texture with it.
TEST(ffvertex_a_stage_that_was_not_generated_is_still_written)
{
	VertexPipelineDescription description = plain_description();
	description.StageCount = 1;

	std::string hlsl;
	CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D9, hlsl));
	CHECK(contains(hlsl, "output.TexCoord1 = float2(0.0, 0.0);"));
}

// The two profiles differ in the output semantic and in how the constants are declared, and in
// nothing else.  That is what makes the D3D9 form a check on the D3D11 one.
TEST(ffvertex_the_two_profiles_differ_only_where_they_have_to)
{
	const VertexPipelineDescription description = plain_description();

	std::string nine;
	std::string eleven;
	CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D9, nine));
	CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D11, eleven));

	CHECK(contains(nine, "row_major float4x4 WorldViewProjection : register(c0);"));
	CHECK(contains(nine, "float4 Position : POSITION;"));
	CHECK(!contains(nine, "cbuffer"));

	CHECK(contains(eleven, "cbuffer VertexPipeline : register(b0)"));
	CHECK(contains(eleven, "float4 Position : SV_Position;"));
	CHECK(!contains(eleven, "register(c0)"));

	// The arithmetic is the same text in both.
	CHECK(contains(nine, "output.Position = mul(model_position, WorldViewProjection);"));
	CHECK(contains(eleven, "output.Position = mul(model_position, WorldViewProjection);"));
}

TEST(ffvertex_puts_a_pretransformed_vertex_back_into_clip_space)
{
	// The screen-space quads - the filter that puts the rendered scene back on the screen, the
	// smudges - come in with x and y already in pixels.  D3D9 takes them as they are; D3D11 has no
	// such thing, so the program divides by the viewport and does not touch the world transform.
	VertexPipelineDescription pretransformed = plain_description();
	pretransformed.FVF = D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1;

	std::string hlsl;
	CHECK(VertexShader_Generate(pretransformed, VERTEX_SHADER_TARGET_D3D11, hlsl));
	CHECK(contains(hlsl, "float4 Position : POSITION;"));
	CHECK(contains(hlsl, "input.Position.xy * ViewportInverse.xy"));
	CHECK(contains(hlsl, "output.TexCoord0 = input.TexCoord0;"));
	CHECK(contains(hlsl, "output.Diffuse = input.Diffuse;"));

	// The one thing it must not do is transform a position that is already transformed.
	CHECK(!contains(hlsl, "WorldViewProjection)"));
}

TEST(ffvertex_reads_zero_for_a_coordinate_set_the_format_lacks)
{
	// The shadow quads keep a texture stage on and carry no texture coordinates at all.  D3D9
	// reads zeroes there and draws them; refusing left 202 draws a match out of the picture.
	VertexPipelineDescription missing_set = plain_description();
	missing_set.FVF = D3DFVF_XYZ|D3DFVF_NORMAL;
	missing_set.Stages[0].TextureCoordinateIndex = D3DTSS_TCI_PASSTHRU | 0;

	std::string hlsl;
	CHECK(VertexShader_Generate(missing_set, VERTEX_SHADER_TARGET_D3D11, hlsl));
	CHECK(contains(hlsl, "float4 generated0 = float4(0.0, 0.0, 0.0, 1.0);"));
	CHECK(!contains(hlsl, "input.TexCoord0"));
}

TEST(ffvertex_refuses_what_it_cannot_generate)
{
	std::string hlsl;

	// Lighting with no normal to light.
	VertexPipelineDescription unlit_format = plain_description();
	unlit_format.FVF = D3DFVF_XYZ|D3DFVF_TEX1;
	unlit_format.LightingEnabled = true;
	CHECK(!VertexShader_Generate(unlit_format, VERTEX_SHADER_TARGET_D3D9, hlsl));

	// More lights than there are registers for.
	VertexPipelineDescription too_many = plain_description();
	too_many.LightingEnabled = true;
	too_many.LightCount = MAXIMUM_VERTEX_LIGHTS + 1;
	CHECK(!VertexShader_Generate(too_many, VERTEX_SHADER_TARGET_D3D9, hlsl));

	// More stages than the pixel half will read.
	VertexPipelineDescription too_many_stages = plain_description();
	too_many_stages.StageCount = MAXIMUM_VERTEX_STAGES + 1;
	CHECK(!VertexShader_Generate(too_many_stages, VERTEX_SHADER_TARGET_D3D9, hlsl));

	// D3DMCS_COLOR2 is the specular vertex colour and no format in the game carries one.  Handing
	// back the material instead would draw something, and it would be the wrong thing quietly.
	VertexPipelineDescription specular_source = plain_description();
	specular_source.LightingEnabled = true;
	specular_source.LightCount = 1;
	specular_source.Lights[0].Type = D3DLIGHT_DIRECTIONAL;
	specular_source.DiffuseMaterialSource = D3DMCS_COLOR2;
	CHECK(!VertexShader_Generate(specular_source, VERTEX_SHADER_TARGET_D3D9, hlsl));
}

// The key is what a cache is built on, so two descriptions that generate the same text have to
// share one and two that do not have to differ.
TEST(ffvertex_the_key_follows_the_shape_and_not_the_constants)
{
	const VertexPipelineDescription plain = plain_description();

	VertexPipelineDescription lit = plain;
	lit.LightingEnabled = true;
	lit.LightCount = 1;
	lit.Lights[0].Type = D3DLIGHT_DIRECTIONAL;

	VertexPipelineDescription point_lit = lit;
	point_lit.Lights[0].Type = D3DLIGHT_POINT;

	CHECK_NE(VertexShader_Key(plain), VertexShader_Key(lit));
	CHECK_NE(VertexShader_Key(lit), VertexShader_Key(point_lit));
	CHECK_STR(VertexShader_Key(plain).c_str(), VertexShader_Key(plain_description()).c_str());
}
