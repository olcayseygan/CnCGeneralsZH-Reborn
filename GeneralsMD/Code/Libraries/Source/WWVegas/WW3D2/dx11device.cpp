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

#include "dx11device.h"

// The back buffer is the same format the D3D9 device asks for, so a picture taken off one can be
// compared against the other without a conversion standing between them.
static const DXGI_FORMAT BACK_BUFFER_FORMAT = DXGI_FORMAT_B8G8R8A8_UNORM;

// D24S8 is what the D3D9 device settles on, and the stencil half is not spare: the shadow volumes
// are drawn with it.
static const DXGI_FORMAT DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D24_UNORM_S8_UINT;

// Feature level 11_0 is the floor.  Below it there are no compute shaders and no unordered access
// views, which is most of what phase 2 is being done for, and a machine that cannot reach it can
// still run the Direct3D 9 device.
static const D3D_FEATURE_LEVEL REQUESTED_FEATURE_LEVELS[] = { D3D_FEATURE_LEVEL_11_0 };

static const UINT SWAP_CHAIN_BUFFER_COUNT = 2;

static void release_interface(IUnknown ** object)
{
	if (*object != NULL) {
		(*object)->Release();
		*object = NULL;
	}
}

DX11DeviceClass::DX11DeviceClass()
	: Device(NULL)
	, Context(NULL)
	, SwapChain(NULL)
	, BackBufferView(NULL)
	, DepthStencilView(NULL)
	, DepthStencilTexture(NULL)
	, FeatureLevel(D3D_FEATURE_LEVEL_11_0)
	, Width(0)
	, Height(0)
	, Software(false)
	, DebugLayerRequested(false)
	, DebugLayerPresent(false)
{
}

DX11DeviceClass::~DX11DeviceClass()
{
	Release();
}

bool DX11DeviceClass::Create(HWND window, unsigned width, unsigned height)
{
	return Create_Device(true, window, width, height);
}

bool DX11DeviceClass::Create_Offscreen()
{
	return Create_Device(false, NULL, 0, 0);
}

bool DX11DeviceClass::Create_Device(bool with_swap_chain, HWND window, unsigned width,
	unsigned height)
{
	DXGI_SWAP_CHAIN_DESC swap_chain;
	ZeroMemory(&swap_chain, sizeof(swap_chain));
	swap_chain.BufferCount = SWAP_CHAIN_BUFFER_COUNT;
	swap_chain.BufferDesc.Width = width;
	swap_chain.BufferDesc.Height = height;
	swap_chain.BufferDesc.Format = BACK_BUFFER_FORMAT;
	swap_chain.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain.OutputWindow = window;
	swap_chain.SampleDesc.Count = 1;
	swap_chain.SampleDesc.Quality = 0;

	// The game asks for a windowed device and resizes the window itself for a borderless one, so
	// DXGI is never asked to take the display mode.  A real fullscreen mode is SetFullscreenState
	// afterwards, which is a decision for whoever owns the window and not for this.
	swap_chain.Windowed = TRUE;
	swap_chain.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	// Hardware first, WARP second.  WARP draws the same picture on a machine with no D3D11 driver,
	// which is what makes the device creatable on a build agent as well as on this one.
	const D3D_DRIVER_TYPE driver_types[] = { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP };
	const unsigned driver_type_count = sizeof(driver_types)/sizeof(driver_types[0]);

	// With the layer asked for, try it first and fall back to the same driver type without it.  A
	// machine without the graphics tools feature refuses the create outright, and a refusal there
	// must not be a refusal to make a device at all.
	const UINT flag_choices[] = { D3D11_CREATE_DEVICE_DEBUG, 0 };
	const unsigned flag_start = DebugLayerRequested ? 0u : 1u;

	const unsigned flag_choice_count = sizeof(flag_choices)/sizeof(flag_choices[0]);

	for (unsigned index = 0; index < driver_type_count; ++index) {
		for (unsigned flag_index = flag_start; flag_index < flag_choice_count; ++flag_index) {
			const UINT flags = flag_choices[flag_index];
			HRESULT result;
			if (with_swap_chain) {
				result = D3D11CreateDeviceAndSwapChain(NULL, driver_types[index], NULL, flags,
					REQUESTED_FEATURE_LEVELS,
					sizeof(REQUESTED_FEATURE_LEVELS)/sizeof(D3D_FEATURE_LEVEL),
					D3D11_SDK_VERSION, &swap_chain, &SwapChain, &Device, &FeatureLevel, &Context);
			}
			else {
				result = D3D11CreateDevice(NULL, driver_types[index], NULL, flags,
					REQUESTED_FEATURE_LEVELS,
					sizeof(REQUESTED_FEATURE_LEVELS)/sizeof(D3D_FEATURE_LEVEL),
					D3D11_SDK_VERSION, &Device, &FeatureLevel, &Context);
			}

			if (FAILED(result)) {
				continue;
			}

			Software = (driver_types[index] == D3D_DRIVER_TYPE_WARP);
			DebugLayerPresent = (flags & D3D11_CREATE_DEVICE_DEBUG) != 0;
			Width = width;
			Height = height;
			if (!with_swap_chain) {
				return true;
			}
			if (Create_Views()) {
				return true;
			}
			Release();
			return false;
		}
	}

	Release();
	return false;
}

bool DX11DeviceClass::Create_Views()
{
	ID3D11Texture2D * back_buffer = NULL;
	if (FAILED(SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
			reinterpret_cast<void **>(&back_buffer)))) {
		return false;
	}

	const HRESULT view_result = Device->CreateRenderTargetView(back_buffer, NULL, &BackBufferView);
	back_buffer->Release();
	if (FAILED(view_result)) {
		return false;
	}

	D3D11_TEXTURE2D_DESC depth;
	ZeroMemory(&depth, sizeof(depth));
	depth.Width = Width;
	depth.Height = Height;
	depth.MipLevels = 1;
	depth.ArraySize = 1;
	depth.Format = DEPTH_STENCIL_FORMAT;
	depth.SampleDesc.Count = 1;
	depth.Usage = D3D11_USAGE_DEFAULT;
	depth.BindFlags = D3D11_BIND_DEPTH_STENCIL;

	if (FAILED(Device->CreateTexture2D(&depth, NULL, &DepthStencilTexture))) {
		return false;
	}
	if (FAILED(Device->CreateDepthStencilView(DepthStencilTexture, NULL, &DepthStencilView))) {
		return false;
	}

	Context->OMSetRenderTargets(1, &BackBufferView, DepthStencilView);

	D3D11_VIEWPORT viewport;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = static_cast<float>(Width);
	viewport.Height = static_cast<float>(Height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	Context->RSSetViewports(1, &viewport);
	return true;
}

void DX11DeviceClass::Release_Views()
{
	if (Context != NULL) {
		// ResizeBuffers refuses while anything still references a back buffer, and the context's
		// own output merger binding counts as a reference.
		ID3D11RenderTargetView * no_target = NULL;
		Context->OMSetRenderTargets(1, &no_target, NULL);
	}
	release_interface(reinterpret_cast<IUnknown **>(&DepthStencilView));
	release_interface(reinterpret_cast<IUnknown **>(&DepthStencilTexture));
	release_interface(reinterpret_cast<IUnknown **>(&BackBufferView));
}

void DX11DeviceClass::Release()
{
	Release_Views();
	release_interface(reinterpret_cast<IUnknown **>(&SwapChain));
	release_interface(reinterpret_cast<IUnknown **>(&Context));
	release_interface(reinterpret_cast<IUnknown **>(&Device));
	Width = 0;
	Height = 0;
	Software = false;
}

bool DX11DeviceClass::Resize(unsigned width, unsigned height)
{
	Release_Views();
	if (FAILED(SwapChain->ResizeBuffers(SWAP_CHAIN_BUFFER_COUNT, width, height,
			BACK_BUFFER_FORMAT, 0))) {
		return false;
	}
	Width = width;
	Height = height;
	return Create_Views();
}

void DX11DeviceClass::Clear(float red, float green, float blue, float alpha, bool clear_depth)
{
	const float colour[4] = { red, green, blue, alpha };
	Context->ClearRenderTargetView(BackBufferView, colour);
	if (clear_depth) {
		Context->ClearDepthStencilView(DepthStencilView,
			D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	}
}

bool DX11DeviceClass::Present(unsigned present_interval)
{
	return SUCCEEDED(SwapChain->Present(present_interval, 0));
}

HRESULT DX11DeviceClass::Device_Removed_Reason() const
{
	return Device->GetDeviceRemovedReason();
}
