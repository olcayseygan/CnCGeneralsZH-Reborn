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

#include "dx11resource.h"

#include <d3d11.h>
#include <string.h>

// What the twin asks dx11resource for.  A buffer the engine fills once becomes a default-usage
// buffer the CPU never maps, written with UpdateSubresource; one it refills every frame keeps
// D3D9's own dynamic usage and is mapped, for the reason on Upload.
static const D3DPOOL STATIC_POOL = D3DPOOL_MANAGED;
static const DWORD STATIC_USAGE = 0;
static const D3DPOOL DYNAMIC_POOL = D3DPOOL_DEFAULT;
static const DWORD DYNAMIC_USAGE = D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY;

DX11BufferTwinClass::DX11BufferTwinClass(unsigned char * mirror, unsigned byte_count,
	ID3D11Buffer * buffer, ID3D11DeviceContext * context, bool dynamic)
	:
	Mirror_Bytes(mirror),
	Byte_Total(byte_count),
	D3D11Buffer(buffer),
	Context(context),
	Dynamic(dynamic)
{
}

DX11BufferTwinClass::~DX11BufferTwinClass()
{
	delete [] Mirror_Bytes;
	D3D11Buffer->Release();
}

void DX11BufferTwinClass::Upload(unsigned byte_offset, unsigned byte_count, bool discard)
{
	if (byte_count == 0) {
		byte_count = Byte_Total - byte_offset;
	}
	if (byte_count == 0) {
		return;
	}

	if (Dynamic) {
		// NO_OVERWRITE promises the range being written is one no outstanding draw reads, which is
		// the same promise the engine already made to D3D9 when it passed D3DLOCK_NOOVERWRITE.
		const D3D11_MAP map_type = discard ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE;
		D3D11_MAPPED_SUBRESOURCE mapped;
		if (SUCCEEDED(Context->Map(D3D11Buffer, 0, map_type, 0, &mapped))) {
			memcpy((unsigned char *)mapped.pData + byte_offset, Mirror_Bytes + byte_offset,
				byte_count);
			Context->Unmap(D3D11Buffer, 0);
		}
		return;
	}

	D3D11_BOX box;
	box.left = byte_offset;
	box.right = byte_offset + byte_count;
	box.top = 0;
	box.bottom = 1;
	box.front = 0;
	box.back = 1;

	Context->UpdateSubresource(D3D11Buffer, 0, &box, Mirror_Bytes + byte_offset, 0, 0);
}

static DX11BufferTwinClass * create(ID3D11Device * device, ID3D11DeviceContext * context,
	unsigned byte_count, bool dynamic, bool index_buffer)
{
	const D3DPOOL pool = dynamic ? DYNAMIC_POOL : STATIC_POOL;
	const DWORD usage = dynamic ? DYNAMIC_USAGE : STATIC_USAGE;

	ID3D11Buffer * buffer = NULL;
	const bool made = index_buffer
		? DX11Resource_Create_Index_Buffer(device, byte_count, pool, usage, NULL, &buffer)
		: DX11Resource_Create_Vertex_Buffer(device, byte_count, pool, usage, NULL, &buffer);
	if (!made) {
		return NULL;
	}

	return new DX11BufferTwinClass(new unsigned char[byte_count], byte_count, buffer, context,
		dynamic);
}

DX11BufferTwinClass * DX11Twin_Create_Vertex_Buffer(ID3D11Device * device,
	ID3D11DeviceContext * context, unsigned byte_count, bool dynamic)
{
	return create(device, context, byte_count, dynamic, false);
}

DX11BufferTwinClass * DX11Twin_Create_Index_Buffer(ID3D11Device * device,
	ID3D11DeviceContext * context, unsigned byte_count, bool dynamic)
{
	return create(device, context, byte_count, dynamic, true);
}

DX11BufferLockClass::DX11BufferLockClass()
	:
	Twin(NULL),
	D3D9Memory(NULL),
	ByteOffset(0),
	ByteCount(0),
	Discard(false)
{
}

void * DX11BufferLockClass::Begin(DX11BufferTwinClass * twin, void * d3d9_memory,
	unsigned byte_offset, unsigned byte_count, unsigned lock_flags)
{
	Twin = twin;
	if (Twin == NULL) {
		return NULL;
	}

	D3D9Memory = d3d9_memory;
	ByteOffset = byte_offset;
	ByteCount = byte_count == 0 ? Twin->Byte_Count() - byte_offset : byte_count;
	Discard = (lock_flags & D3DLOCK_DISCARD) != 0;

	return Twin->Mirror() + ByteOffset;
}

void DX11BufferLockClass::End()
{
	if (Twin == NULL) {
		return;
	}

	memcpy(D3D9Memory, Twin->Mirror() + ByteOffset, ByteCount);
	Twin->Upload(ByteOffset, ByteCount, Discard);
	Twin = NULL;
}
