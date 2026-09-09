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
** The Direct3D 11 copy of a vertex or index buffer, and the heap block both copies are written
** through.
**
** A D3D9 buffer is created write-only, so nothing can read back what the engine put in it and there
** is no way to build the D3D11 copy after the fact.  The twin solves that by moving the write: when
** one exists, a lock hands out the twin's own heap block rather than the D3D9 pointer, and the
** unlock writes that range into both buffers.  The D3D9 side is byte for byte what it was without
** the twin, so a -dx11 run draws the same frame it would have drawn; the difference is that the
** D3D11 buffer now holds the same bytes and a draw can go through the backend.
**
** Twins exist only while Direct3D11_Is_Active, so the factory answers null on an ordinary run and
** every caller carries the null through to the old path.
*/

#ifndef DX11TWIN_H
#define DX11TWIN_H

struct ID3D11Buffer;

class DX11BufferTwinClass
{
public:
	DX11BufferTwinClass(unsigned char * mirror, unsigned byte_count, ID3D11Buffer * buffer);
	~DX11BufferTwinClass();

	// The block a lock hands out.  Offsets into it are byte offsets into the buffer.
	unsigned char * Mirror() const { return Mirror_Bytes; }
	unsigned Byte_Count() const { return Byte_Total; }
	ID3D11Buffer * Buffer() const { return D3D11Buffer; }

	// Copy one range of the mirror into the D3D11 buffer.  A count of zero means to the end, which
	// is what D3D9's own lock means by it.
	void Upload(unsigned byte_offset, unsigned byte_count);

private:
	DX11BufferTwinClass(const DX11BufferTwinClass &);
	DX11BufferTwinClass & operator=(const DX11BufferTwinClass &);

	unsigned char * Mirror_Bytes;
	unsigned Byte_Total;
	ID3D11Buffer * D3D11Buffer;
};

// Null unless there is a live D3D11 device and it took the buffer.
DX11BufferTwinClass * DX11Twin_Create_Vertex_Buffer(unsigned byte_count);
DX11BufferTwinClass * DX11Twin_Create_Index_Buffer(unsigned byte_count);

#endif // DX11TWIN_H
