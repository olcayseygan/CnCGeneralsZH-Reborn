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

// The Direct3D 11 copy of a Direct3D 9 texture, made and then read back.
//
// This needs both devices, because the whole point of the file under test is that it reads a
// texture out of one API and writes it into the other.  The D3D9 device is created the way
// test_dx9_smoke creates one; a machine with no D3D9 device at all reports the tests as skipped
// rather than failing, since there is nothing to check there.
//
// What has to hold is that the pixels arrive unchanged, that a second bind of the same texture
// finds the copy instead of making another, and that an X8R8G8B8 texture - whose unused byte the
// loaders leave at whatever it happens to be - comes out opaque rather than invisible.

#include "test_harness.h"

#include "dx11device.h"
#include "dx11texture.h"

#include <d3d9.h>
#include <d3d11.h>
#include <windows.h>
#include <string.h>

static const unsigned TEXTURE_SIZE = 8;
static const unsigned ONE_LEVEL = 1;
static const unsigned char TRANSPARENT_UNUSED_BYTE = 0x00;

// The D3D9 device the copy is read from, plus the window it needs, torn down together.
class Direct3D9Fixture
{
public:
	Direct3D9Fixture() : Window(NULL), Direct3D(NULL), Device(NULL) {}

	~Direct3D9Fixture()
	{
		if (Device != NULL) {
			Device->Release();
		}
		if (Direct3D != NULL) {
			Direct3D->Release();
		}
		if (Window != NULL) {
			DestroyWindow(Window);
		}
	}

	bool Create()
	{
		Window = CreateWindowExA(0, "STATIC", "dx11texture", WS_OVERLAPPED, 0, 0, 64, 64, NULL,
			NULL, GetModuleHandle(NULL), NULL);
		if (Window == NULL) {
			return false;
		}

		Direct3D = Direct3DCreate9(D3D_SDK_VERSION);
		if (Direct3D == NULL) {
			return false;
		}

		D3DDISPLAYMODE display_mode;
		if (FAILED(Direct3D->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &display_mode))) {
			return false;
		}

		D3DPRESENT_PARAMETERS present;
		memset(&present, 0, sizeof(present));
		present.BackBufferWidth = 64;
		present.BackBufferHeight = 64;
		present.Windowed = TRUE;
		present.SwapEffect = D3DSWAPEFFECT_DISCARD;
		present.BackBufferFormat = display_mode.Format;
		present.hDeviceWindow = Window;

		return SUCCEEDED(Direct3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, Window,
			D3DCREATE_SOFTWARE_VERTEXPROCESSING, &present, &Device));
	}

	IDirect3DDevice9 * Get() { return Device; }

private:
	Direct3D9Fixture(const Direct3D9Fixture &);
	Direct3D9Fixture & operator=(const Direct3D9Fixture &);

	HWND Window;
	IDirect3D9 * Direct3D;
	IDirect3DDevice9 * Device;
};

// A managed texture with a known pattern in it, which is what every loader in the engine produces.
static IDirect3DTexture9 * filled_texture(IDirect3DDevice9 * device, D3DFORMAT format,
	unsigned char unused_byte)
{
	IDirect3DTexture9 * texture = NULL;
	if (FAILED(device->CreateTexture(TEXTURE_SIZE, TEXTURE_SIZE, ONE_LEVEL, 0, format,
			D3DPOOL_MANAGED, &texture, NULL))) {
		return NULL;
	}

	D3DLOCKED_RECT locked;
	if (FAILED(texture->LockRect(0, &locked, NULL, 0))) {
		texture->Release();
		return NULL;
	}

	for (unsigned row = 0; row < TEXTURE_SIZE; ++row) {
		unsigned char * out = (unsigned char *)locked.pBits + row * locked.Pitch;
		for (unsigned column = 0; column < TEXTURE_SIZE; ++column) {
			out[column * 4 + 0] = (unsigned char)column;			// blue
			out[column * 4 + 1] = (unsigned char)row;			// green
			out[column * 4 + 2] = (unsigned char)(row + column);	// red
			out[column * 4 + 3] = unused_byte;
		}
	}
	texture->UnlockRect(0);

	return texture;
}

// The copy, level zero, through a staging texture - the only way to see what UpdateSubresource put
// in a default-usage texture.
static bool read_back(DX11DeviceClass & device, ID3D11ShaderResourceView * view,
	unsigned char * out, unsigned out_pitch)
{
	ID3D11Resource * resource = NULL;
	view->GetResource(&resource);

	D3D11_TEXTURE2D_DESC description;
	((ID3D11Texture2D *)resource)->GetDesc(&description);
	description.Usage = D3D11_USAGE_STAGING;
	description.BindFlags = 0;
	description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	ID3D11Texture2D * staging = NULL;
	if (FAILED(device.Get_Device()->CreateTexture2D(&description, NULL, &staging))) {
		resource->Release();
		return false;
	}

	device.Get_Context()->CopyResource(staging, resource);

	D3D11_MAPPED_SUBRESOURCE mapped;
	const bool mapped_ok =
		SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped));
	if (mapped_ok) {
		for (unsigned row = 0; row < TEXTURE_SIZE; ++row) {
			memcpy(out + row * out_pitch, (const unsigned char *)mapped.pData + row * mapped.RowPitch,
				out_pitch);
		}
		device.Get_Context()->Unmap(staging, 0);
	}

	staging->Release();
	resource->Release();
	return mapped_ok;
}

TEST(dx11texture_a_managed_texture_arrives_pixel_for_pixel)
{
	Direct3D9Fixture d3d9;
	if (!d3d9.Create()) {
		printf("skip: no Direct3D 9 device on this machine\n");
		return;
	}

	DX11DeviceClass d3d11;
	CHECK(d3d11.Create_Offscreen());

	IDirect3DTexture9 * texture = filled_texture(d3d9.Get(), D3DFMT_A8R8G8B8, 0x80);
	CHECK(texture != NULL);

	ID3D11ShaderResourceView * view =
		DX11Texture_Mirror(d3d11.Get_Device(), d3d11.Get_Context(), texture);
	CHECK(view != NULL);

	unsigned char pixels[TEXTURE_SIZE * TEXTURE_SIZE * 4];
	CHECK(read_back(d3d11, view, pixels, TEXTURE_SIZE * 4));

	for (unsigned row = 0; row < TEXTURE_SIZE; ++row) {
		for (unsigned column = 0; column < TEXTURE_SIZE; ++column) {
			const unsigned char * pixel = pixels + row * TEXTURE_SIZE * 4 + column * 4;
			CHECK_EQ(pixel[0], (unsigned char)column);
			CHECK_EQ(pixel[1], (unsigned char)row);
			CHECK_EQ(pixel[2], (unsigned char)(row + column));
			CHECK_EQ(pixel[3], 0x80);
		}
	}

	texture->Release();
}

// The second bind of a texture has to find the copy on it.  Without that the whole texture is read
// and uploaded again on every bind, which is several hundred times a frame.
TEST(dx11texture_a_second_bind_finds_the_copy)
{
	Direct3D9Fixture d3d9;
	if (!d3d9.Create()) {
		printf("skip: no Direct3D 9 device on this machine\n");
		return;
	}

	DX11DeviceClass d3d11;
	CHECK(d3d11.Create_Offscreen());

	IDirect3DTexture9 * texture = filled_texture(d3d9.Get(), D3DFMT_A8R8G8B8, 0xff);
	CHECK(texture != NULL);

	unsigned mirrored_before = 0;
	unsigned reused_before = 0;
	unsigned refused_before = 0;
	DX11Texture_Statistics(mirrored_before, reused_before, refused_before);

	ID3D11ShaderResourceView * first =
		DX11Texture_Mirror(d3d11.Get_Device(), d3d11.Get_Context(), texture);
	ID3D11ShaderResourceView * second =
		DX11Texture_Mirror(d3d11.Get_Device(), d3d11.Get_Context(), texture);
	CHECK(first != NULL);
	CHECK(first == second);

	unsigned mirrored_after = 0;
	unsigned reused_after = 0;
	unsigned refused_after = 0;
	DX11Texture_Statistics(mirrored_after, reused_after, refused_after);
	CHECK_EQ(mirrored_after - mirrored_before, 1u);
	CHECK_EQ(reused_after - reused_before, 1u);

	texture->Release();
}

// X8R8G8B8 has no alpha channel and the byte where DXGI reads one holds whatever the loader left.
// Left alone, a texture whose unused byte is zero is a texture that draws nothing at all.
TEST(dx11texture_an_alphaless_format_comes_out_opaque)
{
	Direct3D9Fixture d3d9;
	if (!d3d9.Create()) {
		printf("skip: no Direct3D 9 device on this machine\n");
		return;
	}

	DX11DeviceClass d3d11;
	CHECK(d3d11.Create_Offscreen());

	IDirect3DTexture9 * texture =
		filled_texture(d3d9.Get(), D3DFMT_X8R8G8B8, TRANSPARENT_UNUSED_BYTE);
	CHECK(texture != NULL);

	ID3D11ShaderResourceView * view =
		DX11Texture_Mirror(d3d11.Get_Device(), d3d11.Get_Context(), texture);
	CHECK(view != NULL);

	unsigned char pixels[TEXTURE_SIZE * TEXTURE_SIZE * 4];
	CHECK(read_back(d3d11, view, pixels, TEXTURE_SIZE * 4));

	for (unsigned row = 0; row < TEXTURE_SIZE; ++row) {
		for (unsigned column = 0; column < TEXTURE_SIZE; ++column) {
			const unsigned char * pixel = pixels + row * TEXTURE_SIZE * 4 + column * 4;
			CHECK_EQ(pixel[0], (unsigned char)column);
			CHECK_EQ(pixel[3], 0xff);
		}
	}

	texture->Release();
}
