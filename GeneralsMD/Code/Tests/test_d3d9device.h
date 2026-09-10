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

// A Direct3D 9 device with a small offscreen render target and no visible window, for tests that
// have to ask the runtime the game is being ported away from what it actually does.  Reading the
// D3D11 documentation against the D3D9 documentation settles nothing where the two describe the
// same thing in different words, which is most of the places a frame can go wrong.
//
// Nothing here is fatal on a machine without a device.  Create returns false and the test says so
// and passes: a build host with no GPU is not a broken renderer.

#ifndef TEST_D3D9DEVICE_H
#define TEST_D3D9DEVICE_H

#include <d3d9.h>
#include <string.h>

inline void D3D9Test_Set_Identity(D3DMATRIX & matrix)
{
	memset(&matrix, 0, sizeof(matrix));
	matrix._11 = 1.0f;
	matrix._22 = 1.0f;
	matrix._33 = 1.0f;
	matrix._44 = 1.0f;
}

class D3D9OffscreenDevice
{
public:
	D3D9OffscreenDevice()
		: Interface(NULL), Device(NULL), Target(NULL), Readback(NULL), Window(NULL), Size(0)
	{
	}

	~D3D9OffscreenDevice()
	{
		if (Readback != NULL) {
			Readback->Release();
		}
		if (Target != NULL) {
			Target->Release();
		}
		if (Device != NULL) {
			Device->Release();
		}
		if (Interface != NULL) {
			Interface->Release();
		}
		if (Window != NULL) {
			DestroyWindow(Window);
		}
	}

	// Software vertex processing on a HAL device, so a vertex shader runs wherever the test runs
	// and the fixed-function pipeline beside it is the runtime's own.
	bool Create(unsigned size)
	{
		Size = size;
		Interface = Direct3DCreate9(D3D_SDK_VERSION);
		if (Interface == NULL) {
			return false;
		}

		Window = CreateWindowA("STATIC", "d3d9test", WS_OVERLAPPED, 0, 0, 8, 8,
			NULL, NULL, NULL, NULL);
		if (Window == NULL) {
			return false;
		}

		D3DPRESENT_PARAMETERS parameters;
		memset(&parameters, 0, sizeof(parameters));
		parameters.Windowed = TRUE;
		parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
		parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
		parameters.BackBufferWidth = size;
		parameters.BackBufferHeight = size;
		parameters.hDeviceWindow = Window;

		if (FAILED(Interface->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, Window,
				D3DCREATE_SOFTWARE_VERTEXPROCESSING, &parameters, &Device))) {
			return false;
		}
		if (FAILED(Device->CreateRenderTarget(size, size, D3DFMT_A8R8G8B8,
				D3DMULTISAMPLE_NONE, 0, FALSE, &Target, NULL))) {
			return false;
		}
		if (FAILED(Device->CreateOffscreenPlainSurface(size, size, D3DFMT_A8R8G8B8,
				D3DPOOL_SYSTEMMEM, &Readback, NULL))) {
			return false;
		}
		return SUCCEEDED(Device->SetRenderTarget(0, Target));
	}

	IDirect3DDevice9 * Get_Device() const
	{
		return Device;
	}

	unsigned Get_Size() const
	{
		return Size;
	}

	// Copies the target back and hands out one pixel as blue, green, red.
	bool Read_Pixel(unsigned x, unsigned y, unsigned char colour[3])
	{
		if (FAILED(Device->GetRenderTargetData(Target, Readback))) {
			return false;
		}
		D3DLOCKED_RECT locked;
		if (FAILED(Readback->LockRect(&locked, NULL, D3DLOCK_READONLY))) {
			return false;
		}
		const unsigned char * pixel = static_cast<const unsigned char *>(locked.pBits)
			+ locked.Pitch * y + x * 4;
		colour[0] = pixel[0];
		colour[1] = pixel[1];
		colour[2] = pixel[2];
		Readback->UnlockRect();
		return true;
	}

	bool Clear(D3DCOLOR colour)
	{
		return SUCCEEDED(Device->Clear(0, NULL, D3DCLEAR_TARGET, colour, 1.0f, 0));
	}

private:
	IDirect3D9 * Interface;
	IDirect3DDevice9 * Device;
	IDirect3DSurface9 * Target;
	IDirect3DSurface9 * Readback;
	HWND Window;
	unsigned Size;
};

#endif // TEST_D3D9DEVICE_H
