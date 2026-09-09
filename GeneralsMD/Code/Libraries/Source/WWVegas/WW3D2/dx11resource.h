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
** The buffers and the textures, made the D3D11 way from the D3D9 arguments the engine passes.
**
** Three vocabularies have to be translated and none of them is a rename.  A surface format is a
** D3DFORMAT naming its channels most significant first and a DXGI_FORMAT naming them least
** significant first, so the letters come out reversed and two of them have no counterpart at all.
** A pool and a usage flag are one concept in D3D9 - where the memory lives and who writes it - and
** in D3D11 they are a usage plus a CPU access mask plus a bind flag, all three of which have to
** agree or the create is refused. And a lock flag is a map type, where D3D9's "no flags" means two
** different things depending on where the resource lives.
**
** What is not translated is the bytes. Every vertex buffer in the engine is filled through a struct
** in dx8fvf.h and every texture through the same loader, and none of that changes: dx11layout says
** where the fields are and this says how the memory is asked for.
*/

#ifndef DX11RESOURCE_H
#define DX11RESOURCE_H

#include <d3d9.h>
#include <d3d11.h>

// DXGI_FORMAT_UNKNOWN comes back for a format with no D3D11 counterpart, which the caller has to
// treat as a refusal: a texture created as UNKNOWN is not a texture.  Two of the engine's formats
// are in that class - the palettised ones, which D3D11 dropped outright.
DXGI_FORMAT DX11Resource_Translate_Format(D3DFORMAT format);

// True where the translation loses something the caller may care about.  D3DFMT_X8R8G8B8 becomes a
// format with an alpha channel it is meant to ignore, and D3DFMT_L8 becomes a single red channel
// that a shader has to spread across three, which is a shader's job and not this one's.
bool DX11Resource_Format_Needs_Swizzle(D3DFORMAT format);

// How the resource is stored and who may write it.  A pool and the usage bits go in; the usage, the
// CPU access mask and the misc flags come out.  Returns false for a combination D3D11 cannot
// express, rather than picking the nearest one.
bool DX11Resource_Translate_Usage(D3DPOOL pool, DWORD d3d9_usage, D3D11_USAGE & usage,
	UINT & cpu_access_flags, UINT & bind_flags);

// A D3DLOCK_* set, read as a map type.  D3D9's zero means read and write where the resource can be
// read and write only where it cannot, so the usage has to be known to answer.
bool DX11Resource_Translate_Lock(DWORD lock_flags, D3D11_USAGE usage, D3D11_MAP & map_type);

// The two buffer creates, which differ only in a bind flag.  initial_data may be null for a buffer
// that is filled later, and a buffer with no initial data cannot be immutable.
bool DX11Resource_Create_Vertex_Buffer(ID3D11Device * device, unsigned byte_width,
	D3DPOOL pool, DWORD d3d9_usage, const void * initial_data, ID3D11Buffer ** buffer);

bool DX11Resource_Create_Index_Buffer(ID3D11Device * device, unsigned byte_width,
	D3DPOOL pool, DWORD d3d9_usage, const void * initial_data, ID3D11Buffer ** buffer);

// A texture and, unless it is a staging one, the shader resource view that reads it.  D3D9 hands
// out one object for both; D3D11 wants the view created alongside and bound separately, and a
// texture without one cannot be sampled however correct its contents are.
bool DX11Resource_Create_Texture(ID3D11Device * device, unsigned width, unsigned height,
	unsigned mip_levels, D3DFORMAT format, D3DPOOL pool, DWORD d3d9_usage,
	ID3D11Texture2D ** texture, ID3D11ShaderResourceView ** view);

#endif // DX11RESOURCE_H
