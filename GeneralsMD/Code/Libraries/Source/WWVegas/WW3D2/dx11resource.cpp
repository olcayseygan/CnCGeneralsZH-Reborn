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

#include "dx11resource.h"

#include <string.h>

DXGI_FORMAT DX11Resource_Translate_Format(D3DFORMAT format)
{
	switch (format) {
	// D3D9 names a colour most significant byte first and DXGI names it least significant first,
	// so an A8R8G8B8 and a B8G8R8A8 are the same four bytes in the same order under two spellings.
	case D3DFMT_A8R8G8B8:  return DXGI_FORMAT_B8G8R8A8_UNORM;
	case D3DFMT_X8R8G8B8:  return DXGI_FORMAT_B8G8R8A8_UNORM;
	case D3DFMT_R5G6B5:    return DXGI_FORMAT_B5G6R5_UNORM;
	case D3DFMT_A1R5G5B5:  return DXGI_FORMAT_B5G5R5A1_UNORM;
	case D3DFMT_X1R5G5B5:  return DXGI_FORMAT_B5G5R5A1_UNORM;
	case D3DFMT_A4R4G4B4:  return DXGI_FORMAT_B4G4R4A4_UNORM;
	case D3DFMT_A8:        return DXGI_FORMAT_A8_UNORM;

	// A luminance texture is one channel read into all three, which D3D11 has no format for.  The
	// channel survives as red and the spreading becomes a swizzle in whatever samples it.
	case D3DFMT_L8:        return DXGI_FORMAT_R8_UNORM;

	// The block compressed formats are the same blocks under different names.  DXT2 and DXT4 are
	// DXT3 and DXT5 with the colour premultiplied by the alpha, which is a property of the data
	// rather than of the format, so they share a DXGI name with the ones they premultiply.
	case D3DFMT_DXT1:      return DXGI_FORMAT_BC1_UNORM;
	case D3DFMT_DXT2:      return DXGI_FORMAT_BC2_UNORM;
	case D3DFMT_DXT3:      return DXGI_FORMAT_BC2_UNORM;
	case D3DFMT_DXT4:      return DXGI_FORMAT_BC3_UNORM;
	case D3DFMT_DXT5:      return DXGI_FORMAT_BC3_UNORM;

	case D3DFMT_D16:       return DXGI_FORMAT_D16_UNORM;
	case D3DFMT_D24S8:     return DXGI_FORMAT_D24_UNORM_S8_UINT;
	case D3DFMT_D24X8:     return DXGI_FORMAT_D24_UNORM_S8_UINT;
	case D3DFMT_D32:       return DXGI_FORMAT_D32_FLOAT;

	// D3DFMT_P8 and D3DFMT_A8P8 are palettised, and D3D11 has no palette anywhere.  A texture in
	// one of these has to be expanded before it arrives, which is the loader's job, so saying so
	// here is better than handing back a format that would draw the indices as grey.
	default:               return DXGI_FORMAT_UNKNOWN;
	}
}

bool DX11Resource_Format_Needs_Swizzle(D3DFORMAT format)
{
	switch (format) {
	// The X formats become formats with an alpha channel, and whatever is in those bits is now
	// meaningful where D3D9 ignored it.
	case D3DFMT_X8R8G8B8:
	case D3DFMT_X1R5G5B5:
	// A luminance channel arrives as red and has to be spread.
	case D3DFMT_L8:
		return true;
	default:
		return false;
	}
}

bool DX11Resource_Translate_Usage(D3DPOOL pool, DWORD d3d9_usage, D3D11_USAGE & usage,
	UINT & cpu_access_flags, UINT & bind_flags)
{
	usage = D3D11_USAGE_DEFAULT;
	cpu_access_flags = 0;
	bind_flags = 0;

	if ((d3d9_usage & D3DUSAGE_RENDERTARGET) != 0) {
		bind_flags |= D3D11_BIND_RENDER_TARGET;
	}
	if ((d3d9_usage & D3DUSAGE_DEPTHSTENCIL) != 0) {
		bind_flags |= D3D11_BIND_DEPTH_STENCIL;
	}

	// A render target or a depth buffer is written by the device and cannot be anything but a
	// default-usage resource, whatever pool D3D9 was asked for.
	if (bind_flags != 0) {
		return pool == D3DPOOL_DEFAULT;
	}

	if (pool == D3DPOOL_SYSTEMMEM) {
		// System memory is where D3D9 puts a surface the CPU reads back out of, and D3D11's
		// staging usage is the only one that can be both read and written by the CPU.  It cannot
		// be bound to the pipeline at all, which is the same restriction from the other side.
		usage = D3D11_USAGE_STAGING;
		cpu_access_flags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
		return true;
	}

	if ((d3d9_usage & D3DUSAGE_DYNAMIC) != 0) {
		usage = D3D11_USAGE_DYNAMIC;
		cpu_access_flags = D3D11_CPU_ACCESS_WRITE;
		return true;
	}

	// Everything else - the managed pool and the default pool without the dynamic bit - is a
	// resource the CPU fills once and the device reads from then on.
	return pool == D3DPOOL_MANAGED || pool == D3DPOOL_DEFAULT;
}

bool DX11Resource_Translate_Lock(DWORD lock_flags, D3D11_USAGE usage, D3D11_MAP & map_type)
{
	if ((lock_flags & D3DLOCK_DISCARD) != 0) {
		// Throwing the contents away and writing the whole thing again is what the dynamic vertex
		// buffers do every frame, and it is the only map a dynamic resource can take without
		// waiting for the device to finish reading the old contents.
		map_type = D3D11_MAP_WRITE_DISCARD;
		return usage == D3D11_USAGE_DYNAMIC;
	}

	if ((lock_flags & D3DLOCK_NOOVERWRITE) != 0) {
		map_type = D3D11_MAP_WRITE_NO_OVERWRITE;
		return usage == D3D11_USAGE_DYNAMIC;
	}

	if ((lock_flags & D3DLOCK_READONLY) != 0) {
		map_type = D3D11_MAP_READ;
		return usage == D3D11_USAGE_STAGING;
	}

	// No flags at all.  On a staging resource that is a read and a write, which is what the
	// readback surfaces want; on a dynamic one there is nothing to read, so it is a discard.
	if (usage == D3D11_USAGE_STAGING) {
		map_type = D3D11_MAP_READ_WRITE;
		return true;
	}
	if (usage == D3D11_USAGE_DYNAMIC) {
		map_type = D3D11_MAP_WRITE_DISCARD;
		return true;
	}

	// A default-usage resource cannot be mapped at all.  D3D9 would have allowed the lock and
	// stalled; refusing says so instead of hiding a stall.
	map_type = D3D11_MAP_READ;
	return false;
}

static bool create_buffer(ID3D11Device * device, unsigned byte_width, D3DPOOL pool,
	DWORD d3d9_usage, const void * initial_data, UINT bind_flag, ID3D11Buffer ** buffer)
{
	*buffer = NULL;

	D3D11_USAGE usage = D3D11_USAGE_DEFAULT;
	UINT cpu_access_flags = 0;
	UINT bind_flags = 0;
	if (!DX11Resource_Translate_Usage(pool, d3d9_usage, usage, cpu_access_flags, bind_flags)) {
		return false;
	}

	// A staging buffer is bound to nothing; anything else is bound as the kind of buffer it is.
	D3D11_BUFFER_DESC description;
	memset(&description, 0, sizeof(description));
	description.ByteWidth = byte_width;
	description.Usage = usage;
	description.BindFlags = (usage == D3D11_USAGE_STAGING) ? 0 : bind_flag;
	description.CPUAccessFlags = cpu_access_flags;

	D3D11_SUBRESOURCE_DATA data;
	memset(&data, 0, sizeof(data));
	data.pSysMem = initial_data;

	return SUCCEEDED(device->CreateBuffer(&description,
		(initial_data != NULL) ? &data : NULL, buffer));
}

bool DX11Resource_Create_Vertex_Buffer(ID3D11Device * device, unsigned byte_width,
	D3DPOOL pool, DWORD d3d9_usage, const void * initial_data, ID3D11Buffer ** buffer)
{
	return create_buffer(device, byte_width, pool, d3d9_usage, initial_data,
		D3D11_BIND_VERTEX_BUFFER, buffer);
}

bool DX11Resource_Create_Index_Buffer(ID3D11Device * device, unsigned byte_width,
	D3DPOOL pool, DWORD d3d9_usage, const void * initial_data, ID3D11Buffer ** buffer)
{
	return create_buffer(device, byte_width, pool, d3d9_usage, initial_data,
		D3D11_BIND_INDEX_BUFFER, buffer);
}

bool DX11Resource_Create_Texture(ID3D11Device * device, unsigned width, unsigned height,
	unsigned mip_levels, D3DFORMAT format, D3DPOOL pool, DWORD d3d9_usage,
	ID3D11Texture2D ** texture, ID3D11ShaderResourceView ** view)
{
	*texture = NULL;
	*view = NULL;

	const DXGI_FORMAT dxgi_format = DX11Resource_Translate_Format(format);
	if (dxgi_format == DXGI_FORMAT_UNKNOWN) {
		return false;
	}

	D3D11_USAGE usage = D3D11_USAGE_DEFAULT;
	UINT cpu_access_flags = 0;
	UINT bind_flags = 0;
	if (!DX11Resource_Translate_Usage(pool, d3d9_usage, usage, cpu_access_flags, bind_flags)) {
		return false;
	}

	// Anything the device can read is bound as a shader resource as well as whatever else it is.
	// A staging texture is bound to nothing and a depth buffer cannot be sampled through the same
	// format it is written with, so neither gets the bit.
	const bool sampleable = (usage != D3D11_USAGE_STAGING)
		&& ((bind_flags & D3D11_BIND_DEPTH_STENCIL) == 0);
	if (sampleable) {
		bind_flags |= D3D11_BIND_SHADER_RESOURCE;
	}

	D3D11_TEXTURE2D_DESC description;
	memset(&description, 0, sizeof(description));
	description.Width = width;
	description.Height = height;
	description.MipLevels = mip_levels;
	description.ArraySize = 1;
	description.Format = dxgi_format;
	description.SampleDesc.Count = 1;
	description.Usage = usage;
	description.BindFlags = bind_flags;
	description.CPUAccessFlags = cpu_access_flags;

	if (FAILED(device->CreateTexture2D(&description, NULL, texture))) {
		return false;
	}

	if (!sampleable) {
		return true;
	}

	if (FAILED(device->CreateShaderResourceView(*texture, NULL, view))) {
		(*texture)->Release();
		*texture = NULL;
		return false;
	}
	return true;
}
