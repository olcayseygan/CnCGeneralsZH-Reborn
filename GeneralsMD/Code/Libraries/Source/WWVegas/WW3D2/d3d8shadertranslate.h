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

// Loading the shipped D3D8 shaders on a Direct3D 9 device.  RENDERER-ROADMAP.md phase 1.
//
// The game reads six compiled shaders out of its big archives: Trees.vso and wave.vso
// are vs_1_1, terrain.pso, terrainnoise.pso, terrainnoise2.pso and monochrome.pso are
// ps_1_1.  None of them loads on a D3D9 device as it stands, because D3D9 tightened
// rules that the D3D8 assembler allowed:
//
//   - a vertex declaration travelled beside the shader as a DWORD array, and D3D9 wants
//     dcl_ statements inside the shader text plus a separate declaration object;
//   - a register could be read before it was written;
//   - an m3x3 or m4x4 could name its destination as its own first source;
//   - oFog and oPts could be written with more than one component.
//
// So a shader is disassembled, its assembly text repaired, and reassembled.  The
// repairs are lifted from crosire's d3d8to9 (d3d8to9_device.cpp), which is BSD-licensed
// and vendored in this tree, and which is what has been translating these same six
// shaders under this renderer since the port.  What is not lifted is that project's
// ps_1_4 rewriting path, which exists for shaders that put modifiers on constant
// registers; none of the six does, and a shader that trips it fails to load and says so
// rather than being half-translated.  A failed pixel shader is not fatal to the game:
// W3DShaderManager falls back to its fixed-function two-stage terrain shader, the same
// path a card with no pixel shader support takes.

#ifndef D3D8SHADERTRANSLATE_H
#define D3D8SHADERTRANSLATE_H

#include <d3d9.h>
#include <string>

// The D3D8 vertex declaration tokens, so the declaration arrays the engine already
// writes keep compiling.  D3D9 has no such tokens; Create_Translated_Vertex_Shader
// decodes them into a D3DVERTEXELEMENT9 array.
#define D3DVSD_TOKEN_STREAM			1
#define D3DVSD_TOKEN_STREAMDATA		2
#define D3DVSD_TOKENTYPESHIFT		29
#define D3DVSD_TOKENTYPEMASK		(0x7 << D3DVSD_TOKENTYPESHIFT)
#define D3DVSD_STREAMNUMBERSHIFT	0
#define D3DVSD_STREAMNUMBERMASK		(0xF << D3DVSD_STREAMNUMBERSHIFT)
#define D3DVSD_VERTEXREGSHIFT		0
#define D3DVSD_VERTEXREGMASK		(0x1F << D3DVSD_VERTEXREGSHIFT)
#define D3DVSD_DATATYPESHIFT		16
#define D3DVSD_DATATYPEMASK			(0xF << D3DVSD_DATATYPESHIFT)
#define D3DVSD_SKIPCOUNTSHIFT		16
#define D3DVSD_SKIPCOUNTMASK		(0xF << D3DVSD_SKIPCOUNTSHIFT)

#define D3DVSD_MAKETOKENTYPE(token_type) (((token_type) << D3DVSD_TOKENTYPESHIFT) & D3DVSD_TOKENTYPEMASK)
#define D3DVSD_STREAM(stream_number) (D3DVSD_MAKETOKENTYPE(D3DVSD_TOKEN_STREAM) | (stream_number))
#define D3DVSD_REG(vertex_register, type) \
	(D3DVSD_MAKETOKENTYPE(D3DVSD_TOKEN_STREAMDATA) | \
	 (((type) << D3DVSD_DATATYPESHIFT) & D3DVSD_DATATYPEMASK) | (vertex_register))
#define D3DVSD_SKIP(dword_count) \
	(D3DVSD_MAKETOKENTYPE(D3DVSD_TOKEN_STREAMDATA) | 0x10000000 | \
	 (((dword_count) << D3DVSD_SKIPCOUNTSHIFT) & D3DVSD_SKIPCOUNTMASK))
#define D3DVSD_END() 0xFFFFFFFF

#define D3DVSDT_FLOAT1		0x00
#define D3DVSDT_FLOAT2		0x01
#define D3DVSDT_FLOAT3		0x02
#define D3DVSDT_FLOAT4		0x03
#define D3DVSDT_D3DCOLOR	0x04
#define D3DVSDT_UBYTE4		0x05
#define D3DVSDT_SHORT2		0x06
#define D3DVSDT_SHORT4		0x07

// Translates ps_1_x bytecode compiled for D3D8 and creates the shader.  Returns
// D3DERR_INVALIDCALL for anything that is not ps_1_0 to ps_1_4.  translated_source, when given,
// comes back holding the assembly the shader was built from: the .vso and .pso files ship as
// bytecode and this is the only place their source exists, which is what anything rewriting them
// for another API has to read.
HRESULT Create_Translated_Pixel_Shader(IDirect3DDevice9 * device, const DWORD * function,
	IDirect3DPixelShader9 ** shader, std::string * translated_source = NULL);

// Translates vs_1_x bytecode and its D3D8 declaration array, and creates both the shader
// and the D3D9 vertex declaration that has to be bound with it.  Either output may come
// back null only if the call failed; on success both are set and both are the caller's
// to release.
HRESULT Create_Translated_Vertex_Shader(IDirect3DDevice9 * device, const DWORD * d3d8_declaration,
	const DWORD * function, IDirect3DVertexShader9 ** shader,
	IDirect3DVertexDeclaration9 ** vertex_declaration, std::string * translated_source = NULL);

#endif // D3D8SHADERTRANSLATE_H
