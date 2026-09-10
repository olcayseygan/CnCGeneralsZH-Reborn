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
** Direct3D 9 render states, turned into the three Direct3D 11 state objects.
**
** D3D9 sets one state at a time and the device sorts it out; D3D11 has no such thing.  It takes a
** blend state, a depth stencil state and a rasteriser state, each an immutable object created up
** front from a descriptor, and a draw binds all three.  So the wrapper keeps every render state
** the engine sets in a block of its own and builds the descriptors from it when something is about
** to be drawn.
**
** Three groups of D3D9 state have no D3D11 object to go into and are not this file's business.
** The alpha test and the fog are gone from the pipeline entirely and become shader instructions,
** which is why -ffshader's generator has to grow them.  Lighting, the material sources and the
** vertex blend are the fixed-function vertex pipeline, which becomes a vertex shader.  And
** D3DRS_TEXTUREFACTOR and D3DRS_STENCILREF are not descriptor fields at all: they are values
** passed at bind time, so they are read off the block rather than built into an object.
**
** This deliberately holds no device.  It is a translation from one set of numbers to another and it
** is tested as one, which is the only way this many enumerations get checked at all.
*/

#ifndef DX11STATE_H
#define DX11STATE_H

#include <d3d9.h>
#include <d3d11.h>

// The engine sets more render states than D3D11 has anywhere to put, and the highest of them is
// well under this.  Indexing the block by the D3DRENDERSTATETYPE itself keeps the setter free of a
// switch that would have to name every state twice.
const unsigned RENDER_STATE_COUNT = 256;

class DX11StateBlockClass
{
public:
	DX11StateBlockClass();

	// Every state the D3D9 device would have started with, so a block that is never written to
	// describes the same pipeline the D3D9 device describes on the frame it is created.
	void Reset_To_Defaults();

	void Set_Render_State(D3DRENDERSTATETYPE state, DWORD value);
	DWORD Get_Render_State(D3DRENDERSTATETYPE state) const;

	void Build_Blend_Description(D3D11_BLEND_DESC & description) const;
	void Build_Depth_Stencil_Description(D3D11_DEPTH_STENCIL_DESC & description) const;
	void Build_Rasterizer_Description(D3D11_RASTERIZER_DESC & description) const;

	// Not descriptor fields: the stencil reference is an argument to OMSetDepthStencilState and
	// the texture factor is a shader constant, so both come off the block at bind time.
	UINT Get_Stencil_Reference() const;
	void Get_Texture_Factor(float factor[4]) const;

private:
	DWORD RenderStates[RENDER_STATE_COUNT];
};

// D3D9 sets a sampler one state at a time the way it sets a render state; D3D11 wants a sampler
// state object built from a descriptor.  The engine sets seven of the fourteen, and the three
// filters are one field in D3D11 rather than three, so the combination has to be worked out rather
// than copied across.
const unsigned SAMPLER_STATE_COUNT = 16;

class DX11SamplerBlockClass
{
public:
	DX11SamplerBlockClass();

	void Reset_To_Defaults();

	void Set_Sampler_State(D3DSAMPLERSTATETYPE state, DWORD value);
	DWORD Get_Sampler_State(D3DSAMPLERSTATETYPE state) const;

	void Build_Sampler_Description(D3D11_SAMPLER_DESC & description) const;

private:
	DWORD SamplerStates[SAMPLER_STATE_COUNT];
};

// D3D9's three filter states become one D3D11_FILTER.  Anisotropic on either the minification or
// the magnification filter makes the whole thing anisotropic, which is what the device does; every
// other combination is a bit each for minify, magnify and mip.
D3D11_FILTER DX11State_Translate_Filter(DWORD minification, DWORD magnification, DWORD mip);
D3D11_TEXTURE_ADDRESS_MODE DX11State_Translate_Address_Mode(DWORD d3d9_address_mode);

// The individual translations, exposed because each one is a table a test can walk end to end.
// An unrecognised value returns the D3D11 default rather than refusing: a render state the engine
// never sets to that value cannot be proved either way, and a default draws something.
D3D11_BLEND DX11State_Translate_Blend(DWORD d3d9_blend);
D3D11_BLEND_OP DX11State_Translate_Blend_Operation(DWORD d3d9_operation);
D3D11_COMPARISON_FUNC DX11State_Translate_Comparison(DWORD d3d9_comparison);
D3D11_STENCIL_OP DX11State_Translate_Stencil_Operation(DWORD d3d9_operation);
D3D11_CULL_MODE DX11State_Translate_Cull_Mode(DWORD d3d9_cull_mode);
D3D11_FILL_MODE DX11State_Translate_Fill_Mode(DWORD d3d9_fill_mode);

#endif // DX11STATE_H
