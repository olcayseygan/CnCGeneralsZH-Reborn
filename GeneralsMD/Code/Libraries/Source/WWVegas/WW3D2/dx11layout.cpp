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

#include "dx11layout.h"

#include <string.h>

// A D3DCOLOR is a packed BGRA byte quad, which is exactly what DXGI calls B8G8R8A8_UNORM.  Handing
// D3D11 R8G8B8A8 instead swaps red and blue in every vertex colour in the game, and the picture it
// makes is plausible enough to argue about.
static const DXGI_FORMAT VERTEX_COLOUR_FORMAT = DXGI_FORMAT_B8G8R8A8_UNORM;

// The two bits per set that D3DFVF_TEXCOORDSIZE writes start here.
static const unsigned TEXTURE_COORDINATE_SIZE_SHIFT = 16;
static const DWORD TEXTURE_COORDINATE_SIZE_MASK = 0x03;

static const DXGI_FORMAT FLOAT_FORMATS[] = {
	DXGI_FORMAT_R32_FLOAT,
	DXGI_FORMAT_R32G32_FLOAT,
	DXGI_FORMAT_R32G32B32_FLOAT,
	DXGI_FORMAT_R32G32B32A32_FLOAT
};

// D3D9 packs the size of coordinate set n into two bits, and the four codes are not in the order
// anybody would guess: 0 means two floats, 1 means three, 2 means four and 3 means one.
static unsigned texture_coordinate_float_count(DWORD fvf, unsigned set)
{
	const DWORD code = (fvf >> (TEXTURE_COORDINATE_SIZE_SHIFT + set * 2)) & TEXTURE_COORDINATE_SIZE_MASK;
	switch (code) {
	case 0:  return 2;
	case 1:  return 3;
	case 2:  return 4;
	default: return 1;
	}
}

static void append_element(D3D11_INPUT_ELEMENT_DESC elements[MAXIMUM_LAYOUT_ELEMENTS],
	unsigned & element_count, const char * semantic, unsigned semantic_index, DXGI_FORMAT format,
	unsigned & offset, unsigned size)
{
	D3D11_INPUT_ELEMENT_DESC & element = elements[element_count];
	element.SemanticName = semantic;
	element.SemanticIndex = semantic_index;
	element.Format = format;
	element.InputSlot = 0;
	element.AlignedByteOffset = offset;
	element.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	element.InstanceDataStepRate = 0;

	++element_count;
	offset += size;
}

bool DX11Layout_From_FVF(DWORD fvf, D3D11_INPUT_ELEMENT_DESC elements[MAXIMUM_LAYOUT_ELEMENTS],
	unsigned & element_count, unsigned & stride)
{
	memset(elements, 0, sizeof(D3D11_INPUT_ELEMENT_DESC) * MAXIMUM_LAYOUT_ELEMENTS);
	element_count = 0;
	stride = 0;

	unsigned offset = 0;

	const DWORD position = fvf & D3DFVF_POSITION_MASK;
	if (position == D3DFVF_XYZ) {
		append_element(elements, element_count, "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
			offset, 12);
	}
	else if (position == D3DFVF_XYZRHW) {
		// A pre-transformed vertex.  The 2D passes use it and its w is already divided, so it
		// keeps all four floats and the vertex shader passes it through untouched.
		append_element(elements, element_count, "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
			offset, 16);
	}
	else {
		// Every other D3DFVF_POSITION_MASK value is a blend-weight form the engine never fills.
		return false;
	}

	if ((fvf & D3DFVF_NORMAL) != 0) {
		append_element(elements, element_count, "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,
			offset, 12);
	}
	if ((fvf & D3DFVF_PSIZE) != 0) {
		append_element(elements, element_count, "PSIZE", 0, DXGI_FORMAT_R32_FLOAT, offset, 4);
	}
	if ((fvf & D3DFVF_DIFFUSE) != 0) {
		append_element(elements, element_count, "COLOR", 0, VERTEX_COLOUR_FORMAT, offset, 4);
	}
	if ((fvf & D3DFVF_SPECULAR) != 0) {
		append_element(elements, element_count, "COLOR", 1, VERTEX_COLOUR_FORMAT, offset, 4);
	}

	const unsigned set_count = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
	if (set_count > MAXIMUM_TEXTURE_COORDINATE_SETS) {
		return false;
	}

	for (unsigned set = 0; set < set_count; ++set) {
		const unsigned float_count = texture_coordinate_float_count(fvf, set);
		append_element(elements, element_count, "TEXCOORD", set,
			FLOAT_FORMATS[float_count - 1], offset, float_count * 4);
	}

	stride = offset;
	return true;
}
