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

// The render state translation, walked end to end.  Every one of these is a pair of numbers that
// look nothing like each other, a mistake in one is a single wrong-looking pass somewhere in a
// frame, and there is no way to read a rasteriser state object back out of D3D11 and see what it
// says.  So the table is checked here rather than on screen.

#include "test_harness.h"

#include "dx11state.h"

// The states the engine actually sets, taken from -ffprobe's dump rather than from the header: the
// most used blend pair in the game is SRCALPHA over INVSRCALPHA, and the second is ONE over
// INVSRCALPHA.
static const DWORD MEASURED_SOURCE_BLEND = D3DBLEND_SRCALPHA;
static const DWORD MEASURED_DESTINATION_BLEND = D3DBLEND_INVSRCALPHA;

TEST(dx11state_a_fresh_block_describes_the_device_d3d9_starts_with)
{
	DX11StateBlockClass block;

	D3D11_DEPTH_STENCIL_DESC depth;
	block.Build_Depth_Stencil_Description(depth);
	CHECK_EQ(depth.DepthEnable, TRUE);
	CHECK_EQ(depth.DepthWriteMask, D3D11_DEPTH_WRITE_MASK_ALL);
	CHECK_EQ(depth.DepthFunc, D3D11_COMPARISON_LESS_EQUAL);
	CHECK_EQ(depth.StencilEnable, FALSE);

	D3D11_BLEND_DESC blend;
	block.Build_Blend_Description(blend);
	CHECK_EQ(blend.RenderTarget[0].BlendEnable, FALSE);
	CHECK_EQ(blend.RenderTarget[0].RenderTargetWriteMask, 0x0f);

	D3D11_RASTERIZER_DESC raster;
	block.Build_Rasterizer_Description(raster);
	CHECK_EQ(raster.FillMode, D3D11_FILL_SOLID);
	CHECK_EQ(raster.CullMode, D3D11_CULL_BACK);
	CHECK_EQ(raster.FrontCounterClockwise, FALSE);
}

TEST(dx11state_the_blend_the_game_uses_most_translates)
{
	DX11StateBlockClass block;
	block.Set_Render_State(D3DRS_ALPHABLENDENABLE, TRUE);
	block.Set_Render_State(D3DRS_SRCBLEND, MEASURED_SOURCE_BLEND);
	block.Set_Render_State(D3DRS_DESTBLEND, MEASURED_DESTINATION_BLEND);

	D3D11_BLEND_DESC blend;
	block.Build_Blend_Description(blend);
	CHECK_EQ(blend.RenderTarget[0].BlendEnable, TRUE);
	CHECK_EQ(blend.RenderTarget[0].SrcBlend, D3D11_BLEND_SRC_ALPHA);
	CHECK_EQ(blend.RenderTarget[0].DestBlend, D3D11_BLEND_INV_SRC_ALPHA);
	CHECK_EQ(blend.RenderTarget[0].BlendOp, D3D11_BLEND_OP_ADD);

	// D3D9 blends alpha with the colour terms unless told otherwise, and it is never told
	// otherwise here, so a description whose alpha terms drifted apart is wrong.
	CHECK_EQ(blend.RenderTarget[0].SrcBlendAlpha, blend.RenderTarget[0].SrcBlend);
	CHECK_EQ(blend.RenderTarget[0].DestBlendAlpha, blend.RenderTarget[0].DestBlend);
}

TEST(dx11state_every_blend_factor_has_a_translation)
{
	CHECK_EQ(DX11State_Translate_Blend(D3DBLEND_ZERO), D3D11_BLEND_ZERO);
	CHECK_EQ(DX11State_Translate_Blend(D3DBLEND_ONE), D3D11_BLEND_ONE);
	CHECK_EQ(DX11State_Translate_Blend(D3DBLEND_SRCCOLOR), D3D11_BLEND_SRC_COLOR);
	CHECK_EQ(DX11State_Translate_Blend(D3DBLEND_INVSRCCOLOR), D3D11_BLEND_INV_SRC_COLOR);
	CHECK_EQ(DX11State_Translate_Blend(D3DBLEND_SRCALPHA), D3D11_BLEND_SRC_ALPHA);
	CHECK_EQ(DX11State_Translate_Blend(D3DBLEND_INVSRCALPHA), D3D11_BLEND_INV_SRC_ALPHA);
	CHECK_EQ(DX11State_Translate_Blend(D3DBLEND_DESTALPHA), D3D11_BLEND_DEST_ALPHA);
	CHECK_EQ(DX11State_Translate_Blend(D3DBLEND_INVDESTALPHA), D3D11_BLEND_INV_DEST_ALPHA);
	CHECK_EQ(DX11State_Translate_Blend(D3DBLEND_DESTCOLOR), D3D11_BLEND_DEST_COLOR);
	CHECK_EQ(DX11State_Translate_Blend(D3DBLEND_INVDESTCOLOR), D3D11_BLEND_INV_DEST_COLOR);
	CHECK_EQ(DX11State_Translate_Blend(D3DBLEND_SRCALPHASAT), D3D11_BLEND_SRC_ALPHA_SAT);
}

TEST(dx11state_every_comparison_has_a_translation)
{
	CHECK_EQ(DX11State_Translate_Comparison(D3DCMP_NEVER), D3D11_COMPARISON_NEVER);
	CHECK_EQ(DX11State_Translate_Comparison(D3DCMP_LESS), D3D11_COMPARISON_LESS);
	CHECK_EQ(DX11State_Translate_Comparison(D3DCMP_EQUAL), D3D11_COMPARISON_EQUAL);
	CHECK_EQ(DX11State_Translate_Comparison(D3DCMP_LESSEQUAL), D3D11_COMPARISON_LESS_EQUAL);
	CHECK_EQ(DX11State_Translate_Comparison(D3DCMP_GREATER), D3D11_COMPARISON_GREATER);
	CHECK_EQ(DX11State_Translate_Comparison(D3DCMP_NOTEQUAL), D3D11_COMPARISON_NOT_EQUAL);
	CHECK_EQ(DX11State_Translate_Comparison(D3DCMP_GREATEREQUAL), D3D11_COMPARISON_GREATER_EQUAL);
	CHECK_EQ(DX11State_Translate_Comparison(D3DCMP_ALWAYS), D3D11_COMPARISON_ALWAYS);
}

TEST(dx11state_every_stencil_operation_has_a_translation)
{
	CHECK_EQ(DX11State_Translate_Stencil_Operation(D3DSTENCILOP_KEEP), D3D11_STENCIL_OP_KEEP);
	CHECK_EQ(DX11State_Translate_Stencil_Operation(D3DSTENCILOP_ZERO), D3D11_STENCIL_OP_ZERO);
	CHECK_EQ(DX11State_Translate_Stencil_Operation(D3DSTENCILOP_REPLACE), D3D11_STENCIL_OP_REPLACE);
	CHECK_EQ(DX11State_Translate_Stencil_Operation(D3DSTENCILOP_INCRSAT), D3D11_STENCIL_OP_INCR_SAT);
	CHECK_EQ(DX11State_Translate_Stencil_Operation(D3DSTENCILOP_DECRSAT), D3D11_STENCIL_OP_DECR_SAT);
	CHECK_EQ(DX11State_Translate_Stencil_Operation(D3DSTENCILOP_INVERT), D3D11_STENCIL_OP_INVERT);
	CHECK_EQ(DX11State_Translate_Stencil_Operation(D3DSTENCILOP_INCR), D3D11_STENCIL_OP_INCR);
	CHECK_EQ(DX11State_Translate_Stencil_Operation(D3DSTENCILOP_DECR), D3D11_STENCIL_OP_DECR);
}

// The shadow volumes are the reason the stencil half of the depth buffer is there, and they are
// the one thing in the game that sets every stencil state at once.  This is that pass.
TEST(dx11state_the_shadow_volume_stencil_pass_translates_whole)
{
	DX11StateBlockClass block;
	block.Set_Render_State(D3DRS_STENCILENABLE, TRUE);
	block.Set_Render_State(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
	block.Set_Render_State(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
	block.Set_Render_State(D3DRS_STENCILZFAIL, D3DSTENCILOP_INCR);
	block.Set_Render_State(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
	block.Set_Render_State(D3DRS_STENCILREF, 1);
	block.Set_Render_State(D3DRS_STENCILMASK, 0xff);
	block.Set_Render_State(D3DRS_STENCILWRITEMASK, 0xff);
	block.Set_Render_State(D3DRS_ZWRITEENABLE, FALSE);

	D3D11_DEPTH_STENCIL_DESC depth;
	block.Build_Depth_Stencil_Description(depth);
	CHECK_EQ(depth.StencilEnable, TRUE);
	CHECK_EQ(depth.StencilReadMask, 0xff);
	CHECK_EQ(depth.StencilWriteMask, 0xff);
	CHECK_EQ(depth.DepthWriteMask, D3D11_DEPTH_WRITE_MASK_ZERO);
	CHECK_EQ(depth.FrontFace.StencilDepthFailOp, D3D11_STENCIL_OP_INCR);
	CHECK_EQ(depth.FrontFace.StencilFunc, D3D11_COMPARISON_ALWAYS);

	// D3D9 applies one set of operations to whichever side survived the cull, so both D3D11 faces
	// have to carry it.  A back face left at its defaults keeps the stencil untouched on that side.
	CHECK_EQ(depth.BackFace.StencilDepthFailOp, D3D11_STENCIL_OP_INCR);

	// The reference is a bind argument, not a field of the description.
	CHECK_EQ(block.Get_Stencil_Reference(), 1u);
}

// D3D9 names the side it throws away, D3D11 names the side it keeps, and both call a clockwise
// triangle the front one.  Getting this backwards turns every model inside out, which is the kind
// of mistake that is obvious on screen and impossible to find in a diff.
TEST(dx11state_culling_keeps_the_same_side_it_kept_under_d3d9)
{
	CHECK_EQ(DX11State_Translate_Cull_Mode(D3DCULL_NONE), D3D11_CULL_NONE);
	CHECK_EQ(DX11State_Translate_Cull_Mode(D3DCULL_CCW), D3D11_CULL_BACK);
	CHECK_EQ(DX11State_Translate_Cull_Mode(D3DCULL_CW), D3D11_CULL_FRONT);
}

TEST(dx11state_wireframe_survives_and_point_fill_does_not)
{
	CHECK_EQ(DX11State_Translate_Fill_Mode(D3DFILL_SOLID), D3D11_FILL_SOLID);
	CHECK_EQ(DX11State_Translate_Fill_Mode(D3DFILL_WIREFRAME), D3D11_FILL_WIREFRAME);

	// D3D11 has no point fill.  Nothing in the engine asks for one, and drawing solid is a picture
	// where refusing the state object would be a black screen.
	CHECK_EQ(DX11State_Translate_Fill_Mode(D3DFILL_POINT), D3D11_FILL_SOLID);
}

TEST(dx11state_the_colour_write_mask_carries_across_unchanged)
{
	DX11StateBlockClass block;
	block.Set_Render_State(D3DRS_COLORWRITEENABLE,
		D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_ALPHA);

	D3D11_BLEND_DESC blend;
	block.Build_Blend_Description(blend);
	CHECK_EQ(blend.RenderTarget[0].RenderTargetWriteMask,
		D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_ALPHA);
}

// D3DRS_TEXTUREFACTOR is a packed ARGB colour and the shader wants four floats in RGBA order.  The
// tree shadow pass reads it for its colour and its alpha both, so a swapped pair there is a shadow
// of the wrong colour rather than a compile error.
TEST(dx11state_the_texture_factor_unpacks_argb_into_rgba)
{
	DX11StateBlockClass block;
	block.Set_Render_State(D3DRS_TEXTUREFACTOR, 0x20406080);

	float factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	block.Get_Texture_Factor(factor);
	CHECK_NEAR(factor[0], 0x40 / 255.0f, 0.0001f);
	CHECK_NEAR(factor[1], 0x60 / 255.0f, 0.0001f);
	CHECK_NEAR(factor[2], 0x80 / 255.0f, 0.0001f);
	CHECK_NEAR(factor[3], 0x20 / 255.0f, 0.0001f);
}

// D3D9's three filter states are one D3D11_FILTER, and the mapping is a bit each rather than a
// name each.  The two the engine sets on almost every draw are point everywhere and linear
// everywhere; the third is the anisotropic one the detail setting turns on.
TEST(dx11state_the_three_filter_states_become_one_filter)
{
	CHECK_EQ(DX11State_Translate_Filter(D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_NONE),
		D3D11_FILTER_MIN_MAG_MIP_POINT);
	CHECK_EQ(DX11State_Translate_Filter(D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_LINEAR),
		D3D11_FILTER_MIN_MAG_MIP_LINEAR);
	CHECK_EQ(DX11State_Translate_Filter(D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_POINT),
		D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT);
	CHECK_EQ(DX11State_Translate_Filter(D3DTEXF_POINT, D3DTEXF_LINEAR, D3DTEXF_NONE),
		D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT);

	// One anisotropic state makes the whole sampler anisotropic, whatever the other two say.
	CHECK_EQ(DX11State_Translate_Filter(D3DTEXF_ANISOTROPIC, D3DTEXF_POINT, D3DTEXF_NONE),
		D3D11_FILTER_ANISOTROPIC);
}

TEST(dx11state_every_address_mode_has_a_translation)
{
	CHECK_EQ(DX11State_Translate_Address_Mode(D3DTADDRESS_WRAP), D3D11_TEXTURE_ADDRESS_WRAP);
	CHECK_EQ(DX11State_Translate_Address_Mode(D3DTADDRESS_MIRROR), D3D11_TEXTURE_ADDRESS_MIRROR);
	CHECK_EQ(DX11State_Translate_Address_Mode(D3DTADDRESS_CLAMP), D3D11_TEXTURE_ADDRESS_CLAMP);
	CHECK_EQ(DX11State_Translate_Address_Mode(D3DTADDRESS_BORDER), D3D11_TEXTURE_ADDRESS_BORDER);
	CHECK_EQ(DX11State_Translate_Address_Mode(D3DTADDRESS_MIRRORONCE),
		D3D11_TEXTURE_ADDRESS_MIRROR_ONCE);
}

// The sampler the terrain sets: linear everywhere, clamped in both directions so the edge of a tile
// does not fetch the far side of it.
TEST(dx11state_a_clamped_linear_sampler_translates_whole)
{
	DX11SamplerBlockClass sampler;
	sampler.Set_Sampler_State(D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	sampler.Set_Sampler_State(D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	sampler.Set_Sampler_State(D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
	sampler.Set_Sampler_State(D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	sampler.Set_Sampler_State(D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

	D3D11_SAMPLER_DESC description;
	sampler.Build_Sampler_Description(description);
	CHECK_EQ(description.Filter, D3D11_FILTER_MIN_MAG_MIP_LINEAR);
	CHECK_EQ(description.AddressU, D3D11_TEXTURE_ADDRESS_CLAMP);
	CHECK_EQ(description.AddressV, D3D11_TEXTURE_ADDRESS_CLAMP);

	// D3DSAMP_ADDRESSW is never set for a two dimensional texture and keeps its default.
	CHECK_EQ(description.AddressW, D3D11_TEXTURE_ADDRESS_WRAP);
}

// A fresh sampler block is the device D3D9 starts with: point filtering, no mip filter, wrapping.
TEST(dx11state_a_fresh_sampler_block_is_the_d3d9_default_sampler)
{
	DX11SamplerBlockClass sampler;

	D3D11_SAMPLER_DESC description;
	sampler.Build_Sampler_Description(description);
	CHECK_EQ(description.Filter, D3D11_FILTER_MIN_MAG_MIP_POINT);
	CHECK_EQ(description.AddressU, D3D11_TEXTURE_ADDRESS_WRAP);
	CHECK_EQ(description.MaxAnisotropy, 1u);
}

// A state above the block's range is one D3D11 has nowhere to put anyway.  Writing it must not
// walk off the end of the array, and reading it back reads zero rather than whatever was there.
TEST(dx11state_a_state_outside_the_block_is_dropped_not_written_past_the_end)
{
	DX11StateBlockClass block;
	const D3DRENDERSTATETYPE beyond = static_cast<D3DRENDERSTATETYPE>(RENDER_STATE_COUNT + 8);
	block.Set_Render_State(beyond, 0x1234);
	CHECK_EQ(block.Get_Render_State(beyond), 0u);

	// Nothing in range moved.
	CHECK_EQ(block.Get_Render_State(D3DRS_ZFUNC), (DWORD)D3DCMP_LESSEQUAL);
}
