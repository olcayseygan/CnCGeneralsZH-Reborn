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

// Every vertex format the engine names, turned into a D3D11 input layout and checked against the
// bytes the structs in dx8fvf.h already lay down.  An offset that is wrong by four reads every
// vertex at the wrong place and reports nothing: no error, no warning, a picture made of noise.
//
// The strides here are counted off those structs by hand rather than taken from the code under
// test, which is the point of writing them down.

#include "test_harness.h"

#include "dx11layout.h"

#include <string.h>

// The formats, spelled out rather than included: dx8fvf.h drags in the rest of WW3D2 and this test
// links one translation unit.  They are the enum in dx8fvf.h, value for value.
static const DWORD FVF_XYZ = D3DFVF_XYZ;
static const DWORD FVF_XYZN = D3DFVF_XYZ|D3DFVF_NORMAL;
static const DWORD FVF_XYZDUV1 = D3DFVF_XYZ|D3DFVF_TEX1|D3DFVF_DIFFUSE;
static const DWORD FVF_XYZNDUV2 = D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX2|D3DFVF_DIFFUSE;
static const DWORD FVF_XYZNDCUBEMAP = D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE;
static const DWORD FVF_XYZNDUV1TG3 = D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE|D3DFVF_TEX4
	|D3DFVF_TEXCOORDSIZE2(0)|D3DFVF_TEXCOORDSIZE3(1)|D3DFVF_TEXCOORDSIZE3(2)|D3DFVF_TEXCOORDSIZE3(3);
static const DWORD FVF_XYZNUV2DMAP = D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX3
	|D3DFVF_TEXCOORDSIZE1(0)|D3DFVF_TEXCOORDSIZE4(1)|D3DFVF_TEXCOORDSIZE2(2);

static bool semantic_is(const D3D11_INPUT_ELEMENT_DESC & element, const char * name, unsigned index)
{
	return strcmp(element.SemanticName, name) == 0 && element.SemanticIndex == index;
}

TEST(dx11layout_a_position_only_format_is_one_element_of_twelve_bytes)
{
	D3D11_INPUT_ELEMENT_DESC elements[MAXIMUM_LAYOUT_ELEMENTS];
	unsigned count = 0;
	unsigned stride = 0;
	CHECK(DX11Layout_From_FVF(FVF_XYZ, elements, count, stride));
	CHECK_EQ(count, 1u);
	CHECK_EQ(stride, 12u);
	CHECK(semantic_is(elements[0], "POSITION", 0));
	CHECK_EQ(elements[0].Format, DXGI_FORMAT_R32G32B32_FLOAT);
	CHECK_EQ(elements[0].AlignedByteOffset, 0u);
}

TEST(dx11layout_position_and_normal_pack_back_to_back)
{
	D3D11_INPUT_ELEMENT_DESC elements[MAXIMUM_LAYOUT_ELEMENTS];
	unsigned count = 0;
	unsigned stride = 0;
	CHECK(DX11Layout_From_FVF(FVF_XYZN, elements, count, stride));
	CHECK_EQ(count, 2u);
	CHECK_EQ(stride, 24u);
	CHECK(semantic_is(elements[1], "NORMAL", 0));
	CHECK_EQ(elements[1].AlignedByteOffset, 12u);
}

// DX8_FVF_XYZNDUV2 is the one format DynamicVBAccessClass hands out, so it is the one every
// dynamic buffer in the game is filled through.
TEST(dx11layout_the_dynamic_buffer_format_lays_out_as_its_struct_does)
{
	D3D11_INPUT_ELEMENT_DESC elements[MAXIMUM_LAYOUT_ELEMENTS];
	unsigned count = 0;
	unsigned stride = 0;
	CHECK(DX11Layout_From_FVF(FVF_XYZNDUV2, elements, count, stride));
	CHECK_EQ(count, 5u);
	CHECK_EQ(stride, 44u);

	CHECK(semantic_is(elements[0], "POSITION", 0));
	CHECK_EQ(elements[0].AlignedByteOffset, 0u);
	CHECK(semantic_is(elements[1], "NORMAL", 0));
	CHECK_EQ(elements[1].AlignedByteOffset, 12u);
	CHECK(semantic_is(elements[2], "COLOR", 0));
	CHECK_EQ(elements[2].AlignedByteOffset, 24u);
	CHECK(semantic_is(elements[3], "TEXCOORD", 0));
	CHECK_EQ(elements[3].AlignedByteOffset, 28u);
	CHECK(semantic_is(elements[4], "TEXCOORD", 1));
	CHECK_EQ(elements[4].AlignedByteOffset, 36u);
}

// A D3DCOLOR is packed blue, green, red, alpha.  R8G8B8A8 compiles, draws, and swaps red and blue
// in every vertex colour in the game, which is why this is checked rather than assumed.
TEST(dx11layout_a_vertex_colour_is_bgra_not_rgba)
{
	D3D11_INPUT_ELEMENT_DESC elements[MAXIMUM_LAYOUT_ELEMENTS];
	unsigned count = 0;
	unsigned stride = 0;
	CHECK(DX11Layout_From_FVF(FVF_XYZDUV1, elements, count, stride));
	CHECK_EQ(count, 3u);
	CHECK_EQ(stride, 24u);
	CHECK(semantic_is(elements[1], "COLOR", 0));
	CHECK_EQ(elements[1].Format, DXGI_FORMAT_B8G8R8A8_UNORM);
}

TEST(dx11layout_a_format_with_no_texture_coordinates_has_none)
{
	D3D11_INPUT_ELEMENT_DESC elements[MAXIMUM_LAYOUT_ELEMENTS];
	unsigned count = 0;
	unsigned stride = 0;
	CHECK(DX11Layout_From_FVF(FVF_XYZNDCUBEMAP, elements, count, stride));
	CHECK_EQ(count, 3u);
	CHECK_EQ(stride, 28u);
}

// The two formats whose coordinate sets are not two floats each.  Reading the size bits is what
// this is for: without them the tangent frame's three-float sets are read as two and everything
// after the first one is at the wrong offset.
TEST(dx11layout_the_tangent_frame_format_reads_its_coordinate_sizes)
{
	D3D11_INPUT_ELEMENT_DESC elements[MAXIMUM_LAYOUT_ELEMENTS];
	unsigned count = 0;
	unsigned stride = 0;
	CHECK(DX11Layout_From_FVF(FVF_XYZNDUV1TG3, elements, count, stride));
	CHECK_EQ(count, 7u);
	CHECK_EQ(stride, 72u);

	CHECK(semantic_is(elements[3], "TEXCOORD", 0));
	CHECK_EQ(elements[3].Format, DXGI_FORMAT_R32G32_FLOAT);
	CHECK_EQ(elements[3].AlignedByteOffset, 28u);

	CHECK(semantic_is(elements[4], "TEXCOORD", 1));
	CHECK_EQ(elements[4].Format, DXGI_FORMAT_R32G32B32_FLOAT);
	CHECK_EQ(elements[4].AlignedByteOffset, 36u);

	CHECK(semantic_is(elements[6], "TEXCOORD", 3));
	CHECK_EQ(elements[6].AlignedByteOffset, 60u);
}

TEST(dx11layout_the_displacement_map_format_reads_a_one_a_four_and_a_two)
{
	D3D11_INPUT_ELEMENT_DESC elements[MAXIMUM_LAYOUT_ELEMENTS];
	unsigned count = 0;
	unsigned stride = 0;
	CHECK(DX11Layout_From_FVF(FVF_XYZNUV2DMAP, elements, count, stride));
	CHECK_EQ(count, 5u);
	CHECK_EQ(stride, 52u);

	CHECK_EQ(elements[2].Format, DXGI_FORMAT_R32_FLOAT);
	CHECK_EQ(elements[2].AlignedByteOffset, 24u);
	CHECK_EQ(elements[3].Format, DXGI_FORMAT_R32G32B32A32_FLOAT);
	CHECK_EQ(elements[3].AlignedByteOffset, 28u);
	CHECK_EQ(elements[4].Format, DXGI_FORMAT_R32G32_FLOAT);
	CHECK_EQ(elements[4].AlignedByteOffset, 44u);
}

// A pre-transformed vertex keeps all four floats and its w is already divided.  The 2D passes are
// drawn this way and there is no transform to apply to them.
TEST(dx11layout_a_pretransformed_position_keeps_four_floats)
{
	D3D11_INPUT_ELEMENT_DESC elements[MAXIMUM_LAYOUT_ELEMENTS];
	unsigned count = 0;
	unsigned stride = 0;
	CHECK(DX11Layout_From_FVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1, elements, count, stride));
	CHECK_EQ(count, 3u);
	CHECK_EQ(stride, 28u);
	CHECK_EQ(elements[0].Format, DXGI_FORMAT_R32G32B32A32_FLOAT);
}

// A format the engine never fills is refused outright.  Half a layout is a buffer read at the
// wrong stride, and that is worse than a draw that does not happen.
TEST(dx11layout_refuses_a_format_with_no_position)
{
	D3D11_INPUT_ELEMENT_DESC elements[MAXIMUM_LAYOUT_ELEMENTS];
	unsigned count = 0;
	unsigned stride = 0;
	CHECK(!DX11Layout_From_FVF(D3DFVF_DIFFUSE|D3DFVF_TEX1, elements, count, stride));
	CHECK_EQ(count, 0u);
	CHECK_EQ(stride, 0u);
}
