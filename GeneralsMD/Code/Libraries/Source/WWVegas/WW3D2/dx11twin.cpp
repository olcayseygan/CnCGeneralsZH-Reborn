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

#include "dx11twin.h"

#include "dx11device.h"
#include "dx11resource.h"
#include "dx11runtime.h"

#include <d3d11.h>

// The pool and usage the twin asks dx11resource for.  Managed with no usage bits is a
// default-usage buffer the CPU never maps, which is what UpdateSubresource wants; the engine's own
// D3DUSAGE_DYNAMIC is a D3D9 memory-placement choice and does not have to be repeated here.
static const D3DPOOL TWIN_POOL = D3DPOOL_MANAGED;
static const DWORD TWIN_USAGE = 0;

DX11BufferTwinClass::DX11BufferTwinClass(unsigned char * mirror, unsigned byte_count,
	ID3D11Buffer * buffer)
	:
	Mirror_Bytes(mirror),
	Byte_Total(byte_count),
	D3D11Buffer(buffer)
{
}

DX11BufferTwinClass::~DX11BufferTwinClass()
{
	delete [] Mirror_Bytes;
	D3D11Buffer->Release();
}

void DX11BufferTwinClass::Upload(unsigned byte_offset, unsigned byte_count)
{
	if (byte_count == 0) {
		byte_count = Byte_Total - byte_offset;
	}
	if (byte_count == 0) {
		return;
	}

	D3D11_BOX box;
	box.left = byte_offset;
	box.right = byte_offset + byte_count;
	box.top = 0;
	box.bottom = 1;
	box.front = 0;
	box.back = 1;

	Direct3D11_Device()->Get_Context()->UpdateSubresource(D3D11Buffer, 0, &box,
		Mirror_Bytes + byte_offset, 0, 0);
}

static DX11BufferTwinClass * create(unsigned byte_count, bool index_buffer)
{
	if (!Direct3D11_Is_Active()) {
		return NULL;
	}

	ID3D11Device * device = Direct3D11_Device()->Get_Device();
	ID3D11Buffer * buffer = NULL;
	const bool made = index_buffer
		? DX11Resource_Create_Index_Buffer(device, byte_count, TWIN_POOL, TWIN_USAGE, NULL, &buffer)
		: DX11Resource_Create_Vertex_Buffer(device, byte_count, TWIN_POOL, TWIN_USAGE, NULL, &buffer);
	if (!made) {
		return NULL;
	}

	return new DX11BufferTwinClass(new unsigned char[byte_count], byte_count, buffer);
}

DX11BufferTwinClass * DX11Twin_Create_Vertex_Buffer(unsigned byte_count)
{
	return create(byte_count, false);
}

DX11BufferTwinClass * DX11Twin_Create_Index_Buffer(unsigned byte_count)
{
	return create(byte_count, true);
}
