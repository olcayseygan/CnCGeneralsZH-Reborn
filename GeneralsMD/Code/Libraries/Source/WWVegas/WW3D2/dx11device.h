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
** The Direct3D 11 device, its swap chain and the two views that hang off it.
**
** This is the bottom of RENDERER-ROADMAP.md's phase 2 and it knows nothing about the game: a
** device, a back buffer, a depth buffer, a clear and a present.  Everything above it - the state
** translation, the combiner programs -ffshader generates, the two .vso rewrites - is built on this
** and can be written against a device that already exists rather than one being designed at the
** same time.
**
** It creates on hardware and falls back to WARP, and it can be created with no window at all,
** which is what lets a test run it: a device with no swap chain has no back buffer and cannot
** present, and Create_Offscreen says so by refusing both.
**
** Nothing here is bound to D3D9's rules.  A D3D11 device is not lost by an alt-tab, so there is no
** TestCooperativeLevel and no Reset_Device; a resize is DXGI's ResizeBuffers and the views are
** rebuilt around it.  A device is removed rather than lost, which is a driver reset or a removed
** adapter, and Device_Removed_Reason reports it.
*/

#ifndef DX11DEVICE_H
#define DX11DEVICE_H

#include <d3d11.h>

class DX11DeviceClass
{
public:
	DX11DeviceClass();
	~DX11DeviceClass();

	// Ask for the validation layer on the next Create.  It reports every misuse D3D11 otherwise
	// answers by drawing nothing - a vertex buffer bound at the wrong stride, an input layout that
	// does not match the shader's signature, a constant buffer too small for what the shader
	// declares - and there is no other way to be told about any of them.  It needs the graphics
	// tools feature installed; without it the create is refused and this falls back to a device
	// without it, so asking is always safe.
	void Request_Debug_Layer() { DebugLayerRequested = true; }
	bool Has_Debug_Layer() const { return DebugLayerPresent; }

	// A device with a window: swap chain, back buffer, depth buffer, the lot.  Width and height
	// are the back buffer's, not the window's, so a borderless window and a fullscreen mode ask
	// for the same thing.
	bool Create(HWND window, unsigned width, unsigned height);

	// A device with no window.  It draws into whatever render target is given to it and cannot
	// present.  Tests use it, and so does anything that wants a device before there is a window.
	bool Create_Offscreen();

	void Release();

	bool Is_Created() const { return Device != NULL; }
	bool Has_Swap_Chain() const { return SwapChain != NULL; }

	// True when the adapter was created by WARP rather than by a driver.  The picture is the same
	// and the speed is not, so anything measuring frame time has to know which one it got.
	bool Is_Software() const { return Software; }

	ID3D11Device * Get_Device() const { return Device; }
	ID3D11DeviceContext * Get_Context() const { return Context; }
	ID3D11RenderTargetView * Get_Back_Buffer_View() const { return BackBufferView; }
	ID3D11DepthStencilView * Get_Depth_Stencil_View() const { return DepthStencilView; }

	// Where the scene goes, which is not always the swap chain.  The post-process chain hands its
	// own texture's view over here at the top of every frame and the backend binds whatever it
	// finds, so nothing between the two knows the frame is being drawn somewhere else first.  NULL
	// puts it back on the swap chain, which is what an absent or a failed chain leaves behind.
	//
	// The view is borrowed, not owned: whoever handed it over releases it, and hands it over again
	// on the next frame if it is still there to hand over.
	void Set_Scene_View(ID3D11RenderTargetView * view) { SceneView = view; }
	ID3D11RenderTargetView * Get_Scene_View() const
	{
		return (SceneView != NULL) ? SceneView : BackBufferView;
	}

	unsigned Get_Width() const { return Width; }
	unsigned Get_Height() const { return Height; }

	// Rebuilds the swap chain's buffers and the two views around them.  DXGI refuses to resize
	// while a view still holds a back buffer, so both views go first.
	bool Resize(unsigned width, unsigned height);

	void Clear(float red, float green, float blue, float alpha, bool clear_depth);

	// present_interval is DXGI's sync interval: 0 for no wait, 1 for the next vertical blank.
	bool Present(unsigned present_interval);

	// D3DERR_DEVICELOST's replacement.  S_OK while the device is usable; anything else is a driver
	// reset or a removed adapter and the device has to be built again from scratch.
	HRESULT Device_Removed_Reason() const;

private:
	DX11DeviceClass(const DX11DeviceClass &);
	DX11DeviceClass & operator=(const DX11DeviceClass &);

	bool Create_Device(bool with_swap_chain, HWND window, unsigned width, unsigned height);
	bool Create_Views();
	void Release_Views();

	ID3D11Device * Device;
	ID3D11DeviceContext * Context;
	IDXGISwapChain * SwapChain;
	ID3D11RenderTargetView * BackBufferView;
	ID3D11RenderTargetView * SceneView;
	ID3D11DepthStencilView * DepthStencilView;
	ID3D11Texture2D * DepthStencilTexture;

	D3D_FEATURE_LEVEL FeatureLevel;
	unsigned Width;
	unsigned Height;
	bool Software;
	bool DebugLayerRequested;
	bool DebugLayerPresent;
};

#endif // DX11DEVICE_H
