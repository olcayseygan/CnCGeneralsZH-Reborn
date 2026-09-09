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

#include "dx11state.h"

#include <string.h>

// D3DCOLORWRITEENABLE_RED and its three neighbours are the same four bits D3D11 uses, in the same
// order, so the mask carries across unchanged and only its width has to be trimmed.
static const DWORD COLOUR_WRITE_MASK = 0x0f;

// D3D9 measures the depth bias in units of the depth buffer's smallest representable value and
// takes it as a float in a DWORD; D3D11 takes an integer count of those units.  The engine only
// ever sets small multiples, so the conversion is a rounded scale rather than a format change.
static const float DEPTH_BIAS_UNIT_SCALE = 16777216.0f;

D3D11_BLEND DX11State_Translate_Blend(DWORD d3d9_blend)
{
	switch (d3d9_blend) {
	case D3DBLEND_ZERO:            return D3D11_BLEND_ZERO;
	case D3DBLEND_ONE:             return D3D11_BLEND_ONE;
	case D3DBLEND_SRCCOLOR:        return D3D11_BLEND_SRC_COLOR;
	case D3DBLEND_INVSRCCOLOR:     return D3D11_BLEND_INV_SRC_COLOR;
	case D3DBLEND_SRCALPHA:        return D3D11_BLEND_SRC_ALPHA;
	case D3DBLEND_INVSRCALPHA:     return D3D11_BLEND_INV_SRC_ALPHA;
	case D3DBLEND_DESTALPHA:       return D3D11_BLEND_DEST_ALPHA;
	case D3DBLEND_INVDESTALPHA:    return D3D11_BLEND_INV_DEST_ALPHA;
	case D3DBLEND_DESTCOLOR:       return D3D11_BLEND_DEST_COLOR;
	case D3DBLEND_INVDESTCOLOR:    return D3D11_BLEND_INV_DEST_COLOR;
	case D3DBLEND_SRCALPHASAT:     return D3D11_BLEND_SRC_ALPHA_SAT;
	case D3DBLEND_BLENDFACTOR:     return D3D11_BLEND_BLEND_FACTOR;
	case D3DBLEND_INVBLENDFACTOR:  return D3D11_BLEND_INV_BLEND_FACTOR;
	default:                       return D3D11_BLEND_ONE;
	}
}

D3D11_BLEND_OP DX11State_Translate_Blend_Operation(DWORD d3d9_operation)
{
	switch (d3d9_operation) {
	case D3DBLENDOP_ADD:          return D3D11_BLEND_OP_ADD;
	case D3DBLENDOP_SUBTRACT:     return D3D11_BLEND_OP_SUBTRACT;
	case D3DBLENDOP_REVSUBTRACT:  return D3D11_BLEND_OP_REV_SUBTRACT;
	case D3DBLENDOP_MIN:          return D3D11_BLEND_OP_MIN;
	case D3DBLENDOP_MAX:          return D3D11_BLEND_OP_MAX;
	default:                      return D3D11_BLEND_OP_ADD;
	}
}

D3D11_COMPARISON_FUNC DX11State_Translate_Comparison(DWORD d3d9_comparison)
{
	switch (d3d9_comparison) {
	case D3DCMP_NEVER:         return D3D11_COMPARISON_NEVER;
	case D3DCMP_LESS:          return D3D11_COMPARISON_LESS;
	case D3DCMP_EQUAL:         return D3D11_COMPARISON_EQUAL;
	case D3DCMP_LESSEQUAL:     return D3D11_COMPARISON_LESS_EQUAL;
	case D3DCMP_GREATER:       return D3D11_COMPARISON_GREATER;
	case D3DCMP_NOTEQUAL:      return D3D11_COMPARISON_NOT_EQUAL;
	case D3DCMP_GREATEREQUAL:  return D3D11_COMPARISON_GREATER_EQUAL;
	case D3DCMP_ALWAYS:        return D3D11_COMPARISON_ALWAYS;
	default:                   return D3D11_COMPARISON_LESS_EQUAL;
	}
}

D3D11_STENCIL_OP DX11State_Translate_Stencil_Operation(DWORD d3d9_operation)
{
	switch (d3d9_operation) {
	case D3DSTENCILOP_KEEP:     return D3D11_STENCIL_OP_KEEP;
	case D3DSTENCILOP_ZERO:     return D3D11_STENCIL_OP_ZERO;
	case D3DSTENCILOP_REPLACE:  return D3D11_STENCIL_OP_REPLACE;
	case D3DSTENCILOP_INCRSAT:  return D3D11_STENCIL_OP_INCR_SAT;
	case D3DSTENCILOP_DECRSAT:  return D3D11_STENCIL_OP_DECR_SAT;
	case D3DSTENCILOP_INVERT:   return D3D11_STENCIL_OP_INVERT;
	case D3DSTENCILOP_INCR:     return D3D11_STENCIL_OP_INCR;
	case D3DSTENCILOP_DECR:     return D3D11_STENCIL_OP_DECR;
	default:                    return D3D11_STENCIL_OP_KEEP;
	}
}

D3D11_CULL_MODE DX11State_Translate_Cull_Mode(DWORD d3d9_cull_mode)
{
	// Both APIs call a clockwise triangle the front one by default, so the winding needs no
	// flipping and D3D11_RASTERIZER_DESC::FrontCounterClockwise stays false.  D3D9 names the side
	// it throws away and D3D11 names it the other way round, which is the whole difference.
	switch (d3d9_cull_mode) {
	case D3DCULL_NONE:  return D3D11_CULL_NONE;
	case D3DCULL_CW:    return D3D11_CULL_FRONT;
	case D3DCULL_CCW:   return D3D11_CULL_BACK;
	default:            return D3D11_CULL_BACK;
	}
}

D3D11_FILL_MODE DX11State_Translate_Fill_Mode(DWORD d3d9_fill_mode)
{
	// D3D9's D3DFILL_POINT has no D3D11 equivalent.  Nothing in the engine sets it - the point
	// sprites go through D3DRS_POINTSPRITEENABLE, not through the fill mode - so it draws solid.
	switch (d3d9_fill_mode) {
	case D3DFILL_WIREFRAME:  return D3D11_FILL_WIREFRAME;
	default:                 return D3D11_FILL_SOLID;
	}
}

DX11StateBlockClass::DX11StateBlockClass()
{
	Reset_To_Defaults();
}

void DX11StateBlockClass::Reset_To_Defaults()
{
	memset(RenderStates, 0, sizeof(RenderStates));

	RenderStates[D3DRS_ZENABLE] = D3DZB_TRUE;
	RenderStates[D3DRS_ZWRITEENABLE] = TRUE;
	RenderStates[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
	RenderStates[D3DRS_FILLMODE] = D3DFILL_SOLID;
	RenderStates[D3DRS_CULLMODE] = D3DCULL_CCW;
	RenderStates[D3DRS_ALPHABLENDENABLE] = FALSE;
	RenderStates[D3DRS_SRCBLEND] = D3DBLEND_ONE;
	RenderStates[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
	RenderStates[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
	RenderStates[D3DRS_COLORWRITEENABLE] = COLOUR_WRITE_MASK;
	RenderStates[D3DRS_ALPHATESTENABLE] = FALSE;
	RenderStates[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
	RenderStates[D3DRS_ALPHAREF] = 0;
	RenderStates[D3DRS_STENCILENABLE] = FALSE;
	RenderStates[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
	RenderStates[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
	RenderStates[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
	RenderStates[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
	RenderStates[D3DRS_STENCILREF] = 0;
	RenderStates[D3DRS_STENCILMASK] = 0xffffffff;
	RenderStates[D3DRS_STENCILWRITEMASK] = 0xffffffff;
	RenderStates[D3DRS_TEXTUREFACTOR] = 0xffffffff;
	RenderStates[D3DRS_CLIPPING] = TRUE;
	RenderStates[D3DRS_MULTISAMPLEANTIALIAS] = TRUE;
}

void DX11StateBlockClass::Set_Render_State(D3DRENDERSTATETYPE state, DWORD value)
{
	if (static_cast<unsigned>(state) < RENDER_STATE_COUNT) {
		RenderStates[state] = value;
	}
}

DWORD DX11StateBlockClass::Get_Render_State(D3DRENDERSTATETYPE state) const
{
	if (static_cast<unsigned>(state) < RENDER_STATE_COUNT) {
		return RenderStates[state];
	}
	return 0;
}

void DX11StateBlockClass::Build_Blend_Description(D3D11_BLEND_DESC & description) const
{
	memset(&description, 0, sizeof(description));

	// One render target and no independent blending: the engine has never drawn into more than one
	// at a time, and every multi-pass effect it has is a second draw rather than a second target.
	description.AlphaToCoverageEnable = FALSE;
	description.IndependentBlendEnable = FALSE;

	D3D11_RENDER_TARGET_BLEND_DESC & target = description.RenderTarget[0];
	target.BlendEnable = (RenderStates[D3DRS_ALPHABLENDENABLE] != FALSE) ? TRUE : FALSE;
	target.SrcBlend = DX11State_Translate_Blend(RenderStates[D3DRS_SRCBLEND]);
	target.DestBlend = DX11State_Translate_Blend(RenderStates[D3DRS_DESTBLEND]);
	target.BlendOp = DX11State_Translate_Blend_Operation(RenderStates[D3DRS_BLENDOP]);

	// D3D9 blends the alpha channel with the colour terms unless D3DRS_SEPARATEALPHABLENDENABLE
	// says otherwise, and nothing in the engine sets that, so the alpha terms are the colour ones.
	target.SrcBlendAlpha = target.SrcBlend;
	target.DestBlendAlpha = target.DestBlend;
	target.BlendOpAlpha = target.BlendOp;

	target.RenderTargetWriteMask =
		static_cast<UINT8>(RenderStates[D3DRS_COLORWRITEENABLE] & COLOUR_WRITE_MASK);
}

void DX11StateBlockClass::Build_Depth_Stencil_Description(D3D11_DEPTH_STENCIL_DESC & description) const
{
	memset(&description, 0, sizeof(description));

	description.DepthEnable = (RenderStates[D3DRS_ZENABLE] != D3DZB_FALSE) ? TRUE : FALSE;
	description.DepthWriteMask = (RenderStates[D3DRS_ZWRITEENABLE] != FALSE)
		? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
	description.DepthFunc = DX11State_Translate_Comparison(RenderStates[D3DRS_ZFUNC]);

	description.StencilEnable = (RenderStates[D3DRS_STENCILENABLE] != FALSE) ? TRUE : FALSE;
	description.StencilReadMask = static_cast<UINT8>(RenderStates[D3DRS_STENCILMASK]);
	description.StencilWriteMask = static_cast<UINT8>(RenderStates[D3DRS_STENCILWRITEMASK]);

	// D3D9 has one set of stencil operations and applies it to whichever side survived the cull;
	// D3D11 always has two.  Both sides get the same operations, which is what the two-sided
	// stencil render states would be for and the engine sets none of them.
	D3D11_DEPTH_STENCILOP_DESC face;
	face.StencilFailOp = DX11State_Translate_Stencil_Operation(RenderStates[D3DRS_STENCILFAIL]);
	face.StencilDepthFailOp = DX11State_Translate_Stencil_Operation(RenderStates[D3DRS_STENCILZFAIL]);
	face.StencilPassOp = DX11State_Translate_Stencil_Operation(RenderStates[D3DRS_STENCILPASS]);
	face.StencilFunc = DX11State_Translate_Comparison(RenderStates[D3DRS_STENCILFUNC]);
	description.FrontFace = face;
	description.BackFace = face;
}

void DX11StateBlockClass::Build_Rasterizer_Description(D3D11_RASTERIZER_DESC & description) const
{
	memset(&description, 0, sizeof(description));

	description.FillMode = DX11State_Translate_Fill_Mode(RenderStates[D3DRS_FILLMODE]);
	description.CullMode = DX11State_Translate_Cull_Mode(RenderStates[D3DRS_CULLMODE]);
	description.FrontCounterClockwise = FALSE;

	float depth_bias = 0.0f;
	memcpy(&depth_bias, &RenderStates[D3DRS_DEPTHBIAS], sizeof(depth_bias));
	description.DepthBias = static_cast<INT>(depth_bias * DEPTH_BIAS_UNIT_SCALE);

	float slope_bias = 0.0f;
	memcpy(&slope_bias, &RenderStates[D3DRS_SLOPESCALEDEPTHBIAS], sizeof(slope_bias));
	description.SlopeScaledDepthBias = slope_bias;

	description.DepthClipEnable = (RenderStates[D3DRS_CLIPPING] != FALSE) ? TRUE : FALSE;
	description.ScissorEnable = FALSE;
	description.MultisampleEnable = (RenderStates[D3DRS_MULTISAMPLEANTIALIAS] != FALSE) ? TRUE : FALSE;
	description.AntialiasedLineEnable =
		(RenderStates[D3DRS_ANTIALIASEDLINEENABLE] != FALSE) ? TRUE : FALSE;
}

D3D11_TEXTURE_ADDRESS_MODE DX11State_Translate_Address_Mode(DWORD d3d9_address_mode)
{
	switch (d3d9_address_mode) {
	case D3DTADDRESS_WRAP:        return D3D11_TEXTURE_ADDRESS_WRAP;
	case D3DTADDRESS_MIRROR:      return D3D11_TEXTURE_ADDRESS_MIRROR;
	case D3DTADDRESS_CLAMP:       return D3D11_TEXTURE_ADDRESS_CLAMP;
	case D3DTADDRESS_BORDER:      return D3D11_TEXTURE_ADDRESS_BORDER;
	case D3DTADDRESS_MIRRORONCE:  return D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
	default:                      return D3D11_TEXTURE_ADDRESS_WRAP;
	}
}

D3D11_FILTER DX11State_Translate_Filter(DWORD minification, DWORD magnification, DWORD mip)
{
	// One anisotropic filter makes the sampler anisotropic whatever the other two say, which is
	// what the device does with the same three states.
	if (minification == D3DTEXF_ANISOTROPIC || magnification == D3DTEXF_ANISOTROPIC) {
		return D3D11_FILTER_ANISOTROPIC;
	}

	// D3D11_FILTER is three bits in one number: bit 4 for the minification, bit 2 for the
	// magnification, bit 0 for the mip, each set when that filter is linear.  Writing it this way
	// rather than as eight named constants is what keeps the three D3D9 states visible.
	const bool minify_linear = (minification == D3DTEXF_LINEAR);
	const bool magnify_linear = (magnification == D3DTEXF_LINEAR);
	const bool mip_linear = (mip == D3DTEXF_LINEAR);

	unsigned bits = 0;
	if (minify_linear) {
		bits |= 0x10;
	}
	if (magnify_linear) {
		bits |= 0x04;
	}
	if (mip_linear) {
		bits |= 0x01;
	}
	return static_cast<D3D11_FILTER>(bits);
}

DX11SamplerBlockClass::DX11SamplerBlockClass()
{
	Reset_To_Defaults();
}

void DX11SamplerBlockClass::Reset_To_Defaults()
{
	memset(SamplerStates, 0, sizeof(SamplerStates));

	SamplerStates[D3DSAMP_ADDRESSU] = D3DTADDRESS_WRAP;
	SamplerStates[D3DSAMP_ADDRESSV] = D3DTADDRESS_WRAP;
	SamplerStates[D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
	SamplerStates[D3DSAMP_MAGFILTER] = D3DTEXF_POINT;
	SamplerStates[D3DSAMP_MINFILTER] = D3DTEXF_POINT;
	SamplerStates[D3DSAMP_MIPFILTER] = D3DTEXF_NONE;
	SamplerStates[D3DSAMP_MAXANISOTROPY] = 1;
}

void DX11SamplerBlockClass::Set_Sampler_State(D3DSAMPLERSTATETYPE state, DWORD value)
{
	if (static_cast<unsigned>(state) < SAMPLER_STATE_COUNT) {
		SamplerStates[state] = value;
	}
}

DWORD DX11SamplerBlockClass::Get_Sampler_State(D3DSAMPLERSTATETYPE state) const
{
	if (static_cast<unsigned>(state) < SAMPLER_STATE_COUNT) {
		return SamplerStates[state];
	}
	return 0;
}

void DX11SamplerBlockClass::Build_Sampler_Description(D3D11_SAMPLER_DESC & description) const
{
	memset(&description, 0, sizeof(description));

	description.Filter = DX11State_Translate_Filter(SamplerStates[D3DSAMP_MINFILTER],
		SamplerStates[D3DSAMP_MAGFILTER], SamplerStates[D3DSAMP_MIPFILTER]);
	description.AddressU = DX11State_Translate_Address_Mode(SamplerStates[D3DSAMP_ADDRESSU]);
	description.AddressV = DX11State_Translate_Address_Mode(SamplerStates[D3DSAMP_ADDRESSV]);
	description.AddressW = DX11State_Translate_Address_Mode(SamplerStates[D3DSAMP_ADDRESSW]);

	// D3D9 takes the bias as a float in a DWORD.
	memcpy(&description.MipLODBias, &SamplerStates[D3DSAMP_MIPMAPLODBIAS],
		sizeof(description.MipLODBias));

	description.MaxAnisotropy = static_cast<UINT>(SamplerStates[D3DSAMP_MAXANISOTROPY]);
	description.ComparisonFunc = D3D11_COMPARISON_NEVER;

	// D3DSAMP_MAXMIPLEVEL is the most detailed level the sampler may use, which is D3D11's minimum
	// LOD.  There is no D3D9 state for the other end, so it stays open.
	description.MinLOD = static_cast<float>(SamplerStates[D3DSAMP_MAXMIPLEVEL]);
	description.MaxLOD = D3D11_FLOAT32_MAX;

	const DWORD border = SamplerStates[D3DSAMP_BORDERCOLOR];
	description.BorderColor[0] = static_cast<float>((border >> 16) & 0xff) / 255.0f;
	description.BorderColor[1] = static_cast<float>((border >> 8) & 0xff) / 255.0f;
	description.BorderColor[2] = static_cast<float>(border & 0xff) / 255.0f;
	description.BorderColor[3] = static_cast<float>((border >> 24) & 0xff) / 255.0f;
}

UINT DX11StateBlockClass::Get_Stencil_Reference() const
{
	return static_cast<UINT>(RenderStates[D3DRS_STENCILREF]);
}

void DX11StateBlockClass::Get_Texture_Factor(float factor[4]) const
{
	const DWORD packed = RenderStates[D3DRS_TEXTUREFACTOR];
	factor[0] = static_cast<float>((packed >> 16) & 0xff) / 255.0f;
	factor[1] = static_cast<float>((packed >> 8) & 0xff) / 255.0f;
	factor[2] = static_cast<float>(packed & 0xff) / 255.0f;
	factor[3] = static_cast<float>((packed >> 24) & 0xff) / 255.0f;
}
