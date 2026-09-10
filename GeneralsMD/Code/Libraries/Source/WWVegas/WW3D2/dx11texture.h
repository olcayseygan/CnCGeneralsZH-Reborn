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
** The Direct3D 11 copy of a texture the engine already built in Direct3D 9.
**
** Textures are not buffers.  A vertex buffer is write-only and had to have its write moved, but a
** managed texture can be read back level by level, and the loaders have finished writing before
** anything binds it.  So the copy is made at the moment of the first bind and from the D3D9
** texture itself, which costs one read of each level once and needs no hook in any of the loaders.
** Movies rewrite the same texture every frame after that; Unlock marks it dirty and the next bind
** fills the copy again.
**
** The copy is kept on the D3D9 texture as private data, so it is released when that texture is,
** and a texture that cannot be mirrored - a render target, a format D3D11 does not name - is
** marked as such and not tried again.
*/

#ifndef DX11TEXTURE_H
#define DX11TEXTURE_H

struct IDirect3DBaseTexture9;
struct IDirect3DSurface9;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;

// The view for this texture, built on the first call and returned from the texture itself after
// that.  Null when the texture cannot be mirrored, which is remembered so the next bind is free.
// The view is owned by the D3D9 texture; the caller borrows it.
ID3D11ShaderResourceView * DX11Texture_Mirror(ID3D11Device * device, ID3D11DeviceContext * context,
	IDirect3DBaseTexture9 * texture);

// Carry one of the engine's surface copies into the copy of the destination texture.  A
// default-pool texture - the shroud is one - cannot be read back through D3D9, so its copy is made
// empty and filled here, from the system memory surface the engine copies over it.
bool DX11Texture_Update(ID3D11Device * device, ID3D11DeviceContext * context,
	IDirect3DSurface9 * destination, IDirect3DSurface9 * source,
	const struct tagRECT * source_rectangle, const struct tagPOINT * destination_point);

// The CPU has just written this surface.  The next bind of its texture recopies the pixels; a
// surface with no texture behind it is ignored.
void DX11Texture_Mark_Dirty(IDirect3DSurface9 * surface);

// The render target view over the copy of the texture this surface is a level of, built on the
// first call and kept on that texture with the shader resource view.  Null when the surface has no
// texture behind it, which is the device's own back buffer.
ID3D11RenderTargetView * DX11Texture_Target(ID3D11Device * device, ID3D11DeviceContext * context,
	IDirect3DSurface9 * surface);

// For the run's report: how many textures were copied, how many binds found the copy already on
// the texture, and how many could not be copied at all.
void DX11Texture_Statistics(unsigned & mirrored, unsigned & reused, unsigned & refused);

// Why the first refused texture was refused.  Empty when none was.
const char * DX11Texture_First_Refusal();

// One line for each copy made from a sixteen bit colour texture, naming its size, its mip count and
// the average colour of its top level.  There are a handful of these in a match - the terrain
// atlas, the alpha edge - and a white average is the difference between a textured ground and the
// white one, which no count of mirrored textures can tell apart.
unsigned DX11Texture_Note_Count();
const char * DX11Texture_Note(unsigned index);

#endif // DX11TEXTURE_H
