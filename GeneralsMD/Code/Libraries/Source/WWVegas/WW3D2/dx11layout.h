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
** A D3D9 flexible vertex format, read out as a D3D11 input layout.
**
** The vertex buffers the engine fills do not change in phase 2: the same bytes in the same order,
** written by the same code, because every one of them is filled by hand through a struct in
** dx8fvf.h and rewriting that is a rewrite of the mesh system.  What changes is that D3D11 has no
** idea what an FVF is.  It wants the layout spelled out, one element per field, with a semantic, a
** format and a byte offset, and it wants that description to have been validated against the
** vertex shader that will read it.
**
** So this walks the FVF in the order D3D9 packs it - position, blend weights, normal, point size,
** diffuse, specular, then the texture coordinates - and writes the elements out.  The offsets it
** produces are the offsets the structs in dx8fvf.h already have, which is the property worth
** testing: get one wrong and every vertex is read at the wrong place with no error anywhere.
**
** The texture coordinate sizes are not all two floats.  DX8_FVF_XYZNDUV1TG3 carries a two-float
** set and three three-float ones for the tangent frame, and DX8_FVF_XYZNUV2DMAP has a one, a four
** and a two.  The D3DFVF_TEXCOORDSIZE bits say which, and reading them is not optional.
*/

#ifndef DX11LAYOUT_H
#define DX11LAYOUT_H

#include <d3d9.h>
#include <d3d11.h>

// D3D9 allows eight texture coordinate sets and the engine's widest format uses four.
const unsigned MAXIMUM_TEXTURE_COORDINATE_SETS = 8;

// Position, blend weights, normal, point size, diffuse, specular and the coordinate sets.
const unsigned MAXIMUM_LAYOUT_ELEMENTS = 6 + MAXIMUM_TEXTURE_COORDINATE_SETS;

// Fills elements with the layout the FVF describes and says how many it wrote; stride comes back
// as the vertex size in bytes, which is what IASetVertexBuffers wants and what DX8Wrapper's own
// Get_FVF_Vertex_Size has been working out from the same bits.  A format naming no position, or
// one whose coordinate sets run past the array, is refused rather than half described.
bool DX11Layout_From_FVF(DWORD fvf, D3D11_INPUT_ELEMENT_DESC elements[MAXIMUM_LAYOUT_ELEMENTS],
	unsigned & element_count, unsigned & stride);

#endif // DX11LAYOUT_H
