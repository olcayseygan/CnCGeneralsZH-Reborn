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
** Nothing here asks whether Direct3D 11 is running.  The device and its context arrive as
** arguments, which keeps the file testable against an offscreen device; dx11runtime answers null
** for the buffers on an ordinary run, and every caller carries that null through to the old path.
*/

#ifndef DX11TWIN_H
#define DX11TWIN_H

struct ID3D11Buffer;
struct ID3D11Device;
struct ID3D11DeviceContext;

class DX11BufferTwinClass
{
public:
	// Takes the mirror block and the buffer; the context is borrowed and outlives the twin.
	DX11BufferTwinClass(unsigned char * mirror, unsigned byte_count, ID3D11Buffer * buffer,
		ID3D11DeviceContext * context, bool dynamic);
	~DX11BufferTwinClass();

	// The block a lock hands out.  Offsets into it are byte offsets into the buffer.
	unsigned char * Mirror() const { return Mirror_Bytes; }
	unsigned Byte_Count() const { return Byte_Total; }
	ID3D11Buffer * Buffer() const { return D3D11Buffer; }

	// Copy one range of the mirror into the D3D11 buffer.  A count of zero means to the end, which
	// is what D3D9's own lock means by it.
	//
	// A static buffer is written with UpdateSubresource; a dynamic one is mapped, because a partial
	// UpdateSubresource cannot rename the buffer and so waits for every draw still reading it.  The
	// engine locks its dynamic buffers several hundred times a frame, and that wait was a second a
	// frame on Tournament Desert.  discard is D3D9's own D3DLOCK_DISCARD, passed through: it says
	// the rest of the buffer is not worth keeping, which is what lets the driver rename.
	void Upload(unsigned byte_offset, unsigned byte_count, bool discard);

private:
	DX11BufferTwinClass(const DX11BufferTwinClass &);
	DX11BufferTwinClass & operator=(const DX11BufferTwinClass &);

	unsigned char * Mirror_Bytes;
	unsigned Byte_Total;
	ID3D11Buffer * D3D11Buffer;
	ID3D11DeviceContext * Context;
	bool Dynamic;
};

// Null when the device refused the buffer.  The caller owns what comes back.  dynamic is the
// engine's own USAGE_DYNAMIC for this buffer and decides how an upload is written.
DX11BufferTwinClass * DX11Twin_Create_Vertex_Buffer(ID3D11Device * device,
	ID3D11DeviceContext * context, unsigned byte_count, bool dynamic);
DX11BufferTwinClass * DX11Twin_Create_Index_Buffer(ID3D11Device * device,
	ID3D11DeviceContext * context, unsigned byte_count, bool dynamic);

/*
** One redirected lock, held for as long as the buffer's own lock object is.
**
** Begin answers the pointer the lock should hand out, or null when there is no twin and the caller
** keeps the D3D9 pointer it already has.  End writes the range into the D3D9 memory and then into
** the D3D11 buffer, and has to run before the D3D9 unlock while that pointer is still mapped.
*/
class DX11BufferLockClass
{
public:
	DX11BufferLockClass();

	// lock_flags is the D3DLOCK_* set the caller handed Direct3D 9, so the upload can be written
	// the same way the D3D9 lock was.
	void * Begin(DX11BufferTwinClass * twin, void * d3d9_memory, unsigned byte_offset,
		unsigned byte_count, unsigned lock_flags);
	void End();

private:
	DX11BufferTwinClass * Twin;
	void * D3D9Memory;
	unsigned ByteOffset;
	unsigned ByteCount;
	bool Discard;
};

#endif // DX11TWIN_H
