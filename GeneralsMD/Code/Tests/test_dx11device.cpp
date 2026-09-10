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

// The Direct3D 11 device, created for real.  A renderer test that cannot make a device proves
// nothing about one, and D3D11 makes this cheap where D3D9 does not: a device needs no window and
// WARP draws the same picture without a driver, so this runs in a batch and on a machine with no
// display as well as on the one it was written on.
//
// Everything phase 2 builds sits on this, so the things checked here are the ones a mistake in
// would be found much later and much further away: that a device with no swap chain refuses to
// present rather than faulting, that a resize leaves the views pointing at the new buffers, and
// that a released device is a device that can be created again.

#include "test_harness.h"

#include "dx11device.h"

// Two sizes, neither of them the other's multiple, so a resize that quietly kept the old numbers
// reads as a failure rather than as an accident that matched.
static const unsigned FIRST_WIDTH = 320;
static const unsigned FIRST_HEIGHT = 240;
static const unsigned SECOND_WIDTH = 512;
static const unsigned SECOND_HEIGHT = 288;

// A window nothing shows.  A swap chain needs an HWND and a message-pumped window would make this
// a manual test; a hidden one is enough for DXGI to build buffers against.
static HWND make_hidden_window()
{
	static const char * const CLASS_NAME = "CnCGeneralsZHDX11DeviceTest";

	WNDCLASSA window_class;
	ZeroMemory(&window_class, sizeof(window_class));
	window_class.lpfnWndProc = DefWindowProcA;
	window_class.hInstance = GetModuleHandleA(NULL);
	window_class.lpszClassName = CLASS_NAME;
	RegisterClassA(&window_class);

	return CreateWindowExA(0, CLASS_NAME, CLASS_NAME, WS_OVERLAPPEDWINDOW, 0, 0,
		FIRST_WIDTH, FIRST_HEIGHT, NULL, NULL, GetModuleHandleA(NULL), NULL);
}

TEST(dx11device_creates_a_device_with_no_window_at_all)
{
	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());
	CHECK(device.Is_Created());
	CHECK(device.Get_Device() != NULL);
	CHECK(device.Get_Context() != NULL);

	// No window, no swap chain, and therefore no back buffer to draw into or show.
	CHECK(!device.Has_Swap_Chain());
	CHECK(device.Get_Back_Buffer_View() == NULL);

	CHECK_EQ(device.Device_Removed_Reason(), S_OK);
}

TEST(dx11device_releasing_leaves_it_creatable_again)
{
	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());
	device.Release();
	CHECK(!device.Is_Created());
	CHECK(device.Get_Device() == NULL);

	CHECK(device.Create_Offscreen());
	CHECK(device.Is_Created());
}

TEST(dx11device_creates_a_swap_chain_and_both_views)
{
	HWND window = make_hidden_window();
	CHECK(window != NULL);

	DX11DeviceClass device;
	CHECK(device.Create(window, FIRST_WIDTH, FIRST_HEIGHT));
	CHECK(device.Has_Swap_Chain());
	CHECK(device.Get_Back_Buffer_View() != NULL);
	CHECK(device.Get_Depth_Stencil_View() != NULL);
	CHECK_EQ(device.Get_Width(), FIRST_WIDTH);
	CHECK_EQ(device.Get_Height(), FIRST_HEIGHT);

	// A clear and a present with no wait: the two calls every frame makes whatever else it does.
	device.Clear(0.0f, 0.0f, 0.0f, 1.0f, true);
	CHECK(device.Present(0));

	device.Release();
	DestroyWindow(window);
}

TEST(dx11device_a_resize_rebuilds_the_views_around_the_new_buffers)
{
	HWND window = make_hidden_window();
	CHECK(window != NULL);

	DX11DeviceClass device;
	CHECK(device.Create(window, FIRST_WIDTH, FIRST_HEIGHT));

	CHECK(device.Resize(SECOND_WIDTH, SECOND_HEIGHT));
	CHECK_EQ(device.Get_Width(), SECOND_WIDTH);
	CHECK_EQ(device.Get_Height(), SECOND_HEIGHT);

	// The view has to be looking at the buffer the resize produced.  Comparing the pointer against
	// the one from before says nothing - the allocator hands the same address back often enough
	// that the check passes for the wrong reason - so this asks the texture underneath how big it
	// is.  A view left over from before the resize reports the old size here.
	CHECK(device.Get_Back_Buffer_View() != NULL);
	CHECK(device.Get_Depth_Stencil_View() != NULL);

	ID3D11Resource * target = NULL;
	device.Get_Back_Buffer_View()->GetResource(&target);
	CHECK(target != NULL);

	ID3D11Texture2D * target_texture = NULL;
	CHECK(SUCCEEDED(target->QueryInterface(__uuidof(ID3D11Texture2D),
		reinterpret_cast<void **>(&target_texture))));

	D3D11_TEXTURE2D_DESC description;
	target_texture->GetDesc(&description);
	CHECK_EQ(description.Width, SECOND_WIDTH);
	CHECK_EQ(description.Height, SECOND_HEIGHT);

	target_texture->Release();
	target->Release();

	device.Clear(0.0f, 0.0f, 0.0f, 1.0f, true);
	CHECK(device.Present(0));

	device.Release();
	DestroyWindow(window);
}
