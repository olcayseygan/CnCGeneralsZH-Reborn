// Phase 1 checkpoint: prove the vendored DX8 SDK headers/libs and the d3d8.dll
// we end up with actually work - create a windowed device on a hidden window,
// clear, present.  Prints adapter info and which runtime the device landed on;
// "DX8 smoke OK" means pass.  The build copies our d3d8to9 d3d8.dll next to the
// exe, so the CTest runs go through it like the game does: plain (d3d8to9's
// default, Direct3D 9) and with "-d3d12" (the opt-in, Direct3D 9On12 ->
// Direct3D 12); each run requires to have really landed on its runtime.

#include <windows.h>
#include <d3d8.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
	bool wantD3D12 = false;
	bool show = false;	// "-show": a visible window, cleared purple and presented for 8 s - for a human to check the present path
	bool exact = false;	// "-exact": client area exactly the back buffer size (like the game; overlay/direct-flip eligible)
	bool popup = false;	// "-popup": the game's window style + black background brush
	bool big = false;	// "-big": 1280x768 back buffer instead of 640x480
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-d3d12") == 0) wantD3D12 = true;
		if (strcmp(argv[i], "-show") == 0) show = true;
		if (strcmp(argv[i], "-exact") == 0) exact = true;
		if (strcmp(argv[i], "-popup") == 0) popup = true;
		if (strcmp(argv[i], "-big") == 0) big = true;
	}
	bool vsync = false, copy = false, flip = false, bb2 = false;	// present-parameter variants for the human check
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-vsync") == 0) vsync = true;	// D3DPRESENT_INTERVAL_ONE (d3d8 windowed is always IMMEDIATE otherwise)
		if (strcmp(argv[i], "-copy") == 0) copy = true;	// D3DSWAPEFFECT_COPY
		if (strcmp(argv[i], "-flip") == 0) flip = true;	// D3DSWAPEFFECT_FLIP
		if (strcmp(argv[i], "-bb2") == 0) bb2 = true;	// BackBufferCount = 2
	}
	int bbW = big ? 1280 : 640, bbH = big ? 768 : 480, posX = 100, posY = 100;
	int msaa = 0;	// "-msaa N": multisampled back buffer, plus the render-to-texture pairing it forces
	for (int i = 1; i + 1 < argc; i++) {	// "-w N -h N": back buffer size, "-x N -y N": window position
		if (strcmp(argv[i], "-msaa") == 0) msaa = atoi(argv[i + 1]);
		if (strcmp(argv[i], "-w") == 0) bbW = atoi(argv[i + 1]);
		if (strcmp(argv[i], "-h") == 0) bbH = atoi(argv[i + 1]);
		if (strcmp(argv[i], "-x") == 0) posX = atoi(argv[i + 1]);
		if (strcmp(argv[i], "-y") == 0) posY = atoi(argv[i + 1]);
	}

	IDirect3D8 * d3d = Direct3DCreate8(D3D_SDK_VERSION);
	if (!d3d) { printf("FAIL: Direct3DCreate8 returned NULL\n"); return 1; }

	D3DADAPTER_IDENTIFIER8 id;
	if (d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id) == D3D_OK) {
		printf("adapter: %s\n", id.Description);
	}

	WNDCLASSA wc = {0};
	wc.lpfnWndProc = DefWindowProcA;
	wc.hInstance = GetModuleHandleA(NULL);
	wc.lpszClassName = "dx8smoke";
	if (popup) wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);	// the game's class (WinMain.cpp)
	RegisterClassA(&wc);
	DWORD style = popup ? (WS_POPUP | WS_DLGFRAME | WS_CAPTION | WS_SYSMENU) : WS_OVERLAPPEDWINDOW;
	if (show) style |= WS_VISIBLE;
	RECT wr = { 0, 0, show ? bbW : 64, show ? bbH : 64 };
	if (exact) AdjustWindowRect(&wr, style, FALSE);	// client == back buffer, as the game sizes its window
	HWND hwnd = CreateWindowA("dx8smoke", "dx8smoke - should be PURPLE", style,
	                          posX, posY, wr.right - wr.left, wr.bottom - wr.top, NULL, NULL, wc.hInstance, NULL);
	if (!hwnd) { printf("FAIL: CreateWindow\n"); d3d->Release(); return 1; }

	D3DDISPLAYMODE mode;
	d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode);

	// the game's own choices (dx8wrapper.cpp): windowed, discard, auto depth, mixed vertex
	// processing - plus a lockable back buffer so the result can be read back and checked
	D3DPRESENT_PARAMETERS pp = {0};
	if (show) { pp.BackBufferWidth = bbW; pp.BackBufferHeight = bbH; }
	pp.Windowed = TRUE;
	pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp.BackBufferFormat = mode.Format;
	pp.hDeviceWindow = hwnd;
	pp.EnableAutoDepthStencil = TRUE;
	pp.AutoDepthStencilFormat = D3DFMT_D16;
	pp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
	// d3d8to9 makes every windowed present IMMEDIATE; only D3DSWAPEFFECT_COPY_VSYNC reaches D3D9 as COPY + INTERVAL_ONE
	if (vsync) pp.SwapEffect = D3DSWAPEFFECT_COPY_VSYNC;
	if (copy) pp.SwapEffect = D3DSWAPEFFECT_COPY;
	if (flip) pp.SwapEffect = D3DSWAPEFFECT_FLIP;
	if (bb2) pp.BackBufferCount = 2;

	// "-msaa N": the highest level up to N that the device reports for BOTH the back buffer
	// and the depth/stencil format, exactly as dx8wrapper.cpp picks it.  A multisampled back
	// buffer cannot be lockable, so the pixel readback below is skipped in that mode.
	D3DMULTISAMPLE_TYPE msType = D3DMULTISAMPLE_NONE;
	if (msaa >= 2) {
		for (int s = msaa > 16 ? 16 : msaa; s >= 2; --s) {
			D3DMULTISAMPLE_TYPE type = (D3DMULTISAMPLE_TYPE)s;
			if (d3d->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
					pp.BackBufferFormat, TRUE, type) != D3D_OK) continue;
			if (d3d->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
					pp.AutoDepthStencilFormat, TRUE, type) != D3D_OK) continue;
			msType = type;
			break;
		}
		printf("msaa: %dx requested, %dx supported\n", msaa, (int)msType);
		if (msType == D3DMULTISAMPLE_NONE) {
			printf("FAIL: no multisample level up to %dx is supported\n", msaa);
			d3d->Release(); DestroyWindow(hwnd);
			return 1;
		}
		pp.MultiSampleType = msType;
		pp.SwapEffect = D3DSWAPEFFECT_DISCARD;	// the only swap effect D3D allows with multisampling
		pp.Flags = 0;
	}

	IDirect3DDevice8 * dev = NULL;
	HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
	                               D3DCREATE_MIXED_VERTEXPROCESSING, &pp, &dev);
	if (hr != D3D_OK || !dev) {
		printf("FAIL: CreateDevice hr=0x%08lx\n", (unsigned long)hr);
		d3d->Release();
		return 1;
	}

	dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(64, 0, 128), 1.0f, 0);

	// read the cleared back buffer back: proves the runtime really rendered, not just created
	// (a D3D12 path that only returned a device but painted nothing would pass otherwise)
	if (msType == D3DMULTISAMPLE_NONE) {
		IDirect3DSurface8 * bb = NULL;
		if (dev->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb) == D3D_OK && bb) {
			D3DLOCKED_RECT lr;
			if (bb->LockRect(&lr, NULL, D3DLOCK_READONLY) == D3D_OK) {
				const unsigned char * px = (const unsigned char *)lr.pBits + 10 * lr.Pitch + 10 * 4;
				printf("back buffer pixel (b,g,r) = (%u,%u,%u)\n", px[0], px[1], px[2]);
				bool ok = px[2] > 48 && px[2] < 80 && px[1] < 16 && px[0] > 112 && px[0] < 144;
				bb->UnlockRect();
				if (!ok) {
					printf("FAIL: back buffer does not hold the clear colour (64,0,128)\n");
					bb->Release(); dev->Release(); d3d->Release(); DestroyWindow(hwnd);
					return 1;
				}
			} else {
				printf("(back buffer not lockable on this runtime - pixel check skipped)\n");
			}
			bb->Release();
		}
	}
	// With a multisampled back buffer the auto depth/stencil is multisampled too, and Direct3D
	// requires the depth buffer to match the render target - so the plain render-target textures
	// every screen filter and the water reflection bind need a non-multisampled depth buffer of
	// their own.  That is DX8Wrapper::_Get_Non_MultiSampled_Depth_Buffer; this is its pairing,
	// cleared green and read back to prove the target really was drawn into.
	if (msType != D3DMULTISAMPLE_NONE) {
		IDirect3DTexture8 * rtt = NULL;
		IDirect3DSurface8 * rttSurf = NULL, * rttZ = NULL, * bb = NULL, * autoZ = NULL, * copy = NULL;
		if (FAILED(dev->CreateTexture(256, 256, 1, D3DUSAGE_RENDERTARGET, pp.BackBufferFormat,
				D3DPOOL_DEFAULT, &rtt))
			|| FAILED(rtt->GetSurfaceLevel(0, &rttSurf))
			|| FAILED(dev->CreateDepthStencilSurface(256, 256, pp.AutoDepthStencilFormat,
				D3DMULTISAMPLE_NONE, &rttZ))
			|| FAILED(dev->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb))
			|| FAILED(dev->GetDepthStencilSurface(&autoZ))
			|| FAILED(dev->CreateImageSurface(256, 256, pp.BackBufferFormat, &copy))) {
			printf("FAIL: could not build the render-to-texture surfaces\n");
			dev->Release(); d3d->Release(); DestroyWindow(hwnd);
			return 1;
		}
		// For the record: the retail runtime does NOT reject the mismatch at bind time (the debug
		// runtime and the D3D9 docs do), which is why this is reported rather than asserted.
		printf("rtt: multisampled z with a plain target hr=0x%08lx (0 = the runtime allows it)\n",
			(unsigned long)dev->SetRenderTarget(rttSurf, autoZ));
		HRESULT good = dev->SetRenderTarget(rttSurf, rttZ);
		HRESULT cleared = dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
			D3DCOLOR_XRGB(0, 255, 0), 1.0f, 0);
		HRESULT copied = dev->CopyRects(rttSurf, NULL, 0, copy, NULL);
		HRESULT back = dev->SetRenderTarget(bb, autoZ);
		printf("rtt: matched z hr=0x%08lx, clear hr=0x%08lx, copy hr=0x%08lx, restore hr=0x%08lx\n",
			(unsigned long)good, (unsigned long)cleared, (unsigned long)copied, (unsigned long)back);
		if (FAILED(good) || FAILED(cleared) || FAILED(copied) || FAILED(back)) {
			printf("FAIL: render-to-texture with a matching non-multisampled depth buffer failed\n");
			dev->Release(); d3d->Release(); DestroyWindow(hwnd);
			return 1;
		}
		D3DLOCKED_RECT lr;
		bool green = false;
		if (SUCCEEDED(copy->LockRect(&lr, NULL, D3DLOCK_READONLY))) {
			const unsigned char * px = (const unsigned char *)lr.pBits + 10 * lr.Pitch + 10 * 4;
			printf("render texture pixel (b,g,r) = (%u,%u,%u)\n", px[0], px[1], px[2]);
			green = px[1] > 200 && px[0] < 32 && px[2] < 32;
			copy->UnlockRect();
		}
		if (!green) {
			printf("FAIL: the render texture does not hold the clear colour (0,255,0)\n");
			dev->Release(); d3d->Release(); DestroyWindow(hwnd);
			return 1;
		}
		copy->Release(); autoZ->Release(); bb->Release(); rttZ->Release();
		rttSurf->Release(); rtt->Release();
	}

	// The bloom bright pass (W3DShaderManager.cpp's renderBloom) is fixed function on purpose,
	// and it leans on two things no caps bit here guarantees through d3d8to9: D3DTOP_SUBTRACT
	// against D3DTA_TFACTOR, and binding a render target with a NULL depth surface.  Run exactly
	// that: a flat 200/255 source texture minus a 166/255 threshold must come back as 34.
	{
		IDirect3DTexture8 * src = NULL, * dst = NULL;
		IDirect3DSurface8 * dstSurf = NULL, * bb = NULL, * autoZ = NULL, * copySurf = NULL;
		D3DLOCKED_RECT lr;
		bool built = SUCCEEDED(dev->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &src))
			&& SUCCEEDED(dev->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET, pp.BackBufferFormat,
					D3DPOOL_DEFAULT, &dst))
			&& SUCCEEDED(dst->GetSurfaceLevel(0, &dstSurf))
			&& SUCCEEDED(dev->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb))
			&& SUCCEEDED(dev->CreateImageSurface(16, 16, pp.BackBufferFormat, &copySurf))
			&& SUCCEEDED(src->LockRect(0, &lr, NULL, 0));
		if (!built) {
			printf("FAIL: could not build the bloom bright pass surfaces\n");
			dev->Release(); d3d->Release(); DestroyWindow(hwnd);
			return 1;
		}
		dev->GetDepthStencilSurface(&autoZ);	// may be NULL, that is fine
		for (int y = 0; y < 4; y++) {
			unsigned long * row = (unsigned long *)((unsigned char *)lr.pBits + y * lr.Pitch);
			for (int x = 0; x < 4; x++) row[x] = 0xffc8c8c8;	// opaque 200,200,200
		}
		src->UnlockRect(0);

		struct QuadVertex { float x, y, z, rhw; unsigned long color; float u, v; };
		const QuadVertex quad[4] = {
			{ 15.5f, 15.5f, 0.0f, 1.0f, 0xffffffff, 1.0f, 1.0f },
			{ 15.5f, -0.5f, 0.0f, 1.0f, 0xffffffff, 1.0f, 0.0f },
			{ -0.5f, 15.5f, 0.0f, 1.0f, 0xffffffff, 0.0f, 1.0f },
			{ -0.5f, -0.5f, 0.0f, 1.0f, 0xffffffff, 0.0f, 0.0f },
		};
		HRESULT bound = dev->SetRenderTarget(dstSurf, NULL);	// NULL depth: nothing here is depth tested
		dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
		dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		dev->SetRenderState(D3DRS_LIGHTING, FALSE);
		dev->SetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB(255, 166, 166, 166));
		dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SUBTRACT);
		dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
		dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
		dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
		dev->SetTexture(0, src);
		dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
		HRESULT begun = dev->BeginScene();
		HRESULT drawn = dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(QuadVertex));
		dev->EndScene();
		HRESULT copied = dev->CopyRects(dstSurf, NULL, 0, copySurf, NULL);
		HRESULT restored = dev->SetRenderTarget(bb, autoZ);
		dev->SetTexture(0, NULL);
		dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		printf("bloom: bind(null z) hr=0x%08lx, begin hr=0x%08lx, draw hr=0x%08lx, copy hr=0x%08lx, restore hr=0x%08lx\n",
			(unsigned long)bound, (unsigned long)begun, (unsigned long)drawn,
			(unsigned long)copied, (unsigned long)restored);
		int got = -1;
		if (SUCCEEDED(copySurf->LockRect(&lr, NULL, D3DLOCK_READONLY))) {
			const unsigned char * px = (const unsigned char *)lr.pBits + 8 * lr.Pitch + 8 * 4;
			printf("bloom: 200 - 166 -> (b,g,r) = (%u,%u,%u), want 34\n", px[0], px[1], px[2]);
			if (px[0] == px[1] && px[1] == px[2]) got = px[0];
			copySurf->UnlockRect();
		}
		copySurf->Release();
		if (autoZ) autoZ->Release();
		bb->Release(); dstSurf->Release(); dst->Release(); src->Release();
		if (FAILED(bound) || FAILED(drawn) || FAILED(copied) || FAILED(restored) || got < 30 || got > 38) {
			printf("FAIL: the bloom bright pass (D3DTOP_SUBTRACT of D3DTA_TFACTOR into a render target with no depth buffer) did not work\n");
			dev->Release(); d3d->Release(); DestroyWindow(hwnd);
			return 1;
		}
	}

	// The water reflection (WaterRenderObjClass::renderMirror) draws the world into a small
	// off-screen target and then samples that target back as a texture in a later pass.  The
	// checks above never do the second half: they render into a target and read it on the CPU,
	// which passes even if the result is unusable as a shader input.  So bind a 64x64 target,
	// fill it, restore the back buffer, then sample it across a 16x16 target and read that.
	// Nothing here touches the back buffer's format or sample count, so it runs in the plain,
	// -d3d12 and -msaa configurations alike.
	//
	// The viewport is checked on the way through because binding the mirror target has to bring
	// its own viewport with it: 64x64 at 0,0, not the window's.  A viewport left at the window
	// size draws the mirrored scene into one corner of the texture.  Direct3D itself enforces
	// this (a viewport larger than the target is rejected - tried it, GetViewport still said
	// 64x64), so what the check really pins is d3d8to9's SetRenderTarget, which is vendored
	// code we patch.  It is not a check that has ever been made to fail here.
	{
		IDirect3DTexture8 * mirror = NULL, * out = NULL;
		IDirect3DSurface8 * mirrorSurf = NULL, * mirrorZ = NULL, * outSurf = NULL;
		IDirect3DSurface8 * bb = NULL, * autoZ = NULL, * copySurf = NULL;
		D3DLOCKED_RECT lr;
		bool built = SUCCEEDED(dev->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET, pp.BackBufferFormat,
				D3DPOOL_DEFAULT, &mirror))
			&& SUCCEEDED(mirror->GetSurfaceLevel(0, &mirrorSurf))
			&& SUCCEEDED(dev->CreateDepthStencilSurface(64, 64, pp.AutoDepthStencilFormat,
				D3DMULTISAMPLE_NONE, &mirrorZ))
			&& SUCCEEDED(dev->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET, pp.BackBufferFormat,
				D3DPOOL_DEFAULT, &out))
			&& SUCCEEDED(out->GetSurfaceLevel(0, &outSurf))
			&& SUCCEEDED(dev->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb))
			&& SUCCEEDED(dev->CreateImageSurface(16, 16, pp.BackBufferFormat, &copySurf));
		if (!built) {
			printf("FAIL: could not build the reflection surfaces\n");
			dev->Release(); d3d->Release(); DestroyWindow(hwnd);
			return 1;
		}
		dev->GetDepthStencilSurface(&autoZ);	// may be NULL under -msaa's pairing, that is fine

		// half of Set_Render_Target_With_Z: the mirror pass binds colour and a matching depth
		HRESULT boundMirror = dev->SetRenderTarget(mirrorSurf, mirrorZ);
		D3DVIEWPORT8 vp;
		memset(&vp, 0, sizeof(vp));
		HRESULT gotVp = dev->GetViewport(&vp);
		printf("reflection: 64x64 target -> viewport %lux%lu at %lu,%lu\n",
			(unsigned long)vp.Width, (unsigned long)vp.Height,
			(unsigned long)vp.X, (unsigned long)vp.Y);
		HRESULT clearedMirror = dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
			D3DCOLOR_XRGB(0, 96, 192), 1.0f, 0);
		HRESULT restored = dev->SetRenderTarget(bb, autoZ);

		// now the half nothing else covers: that target, as a texture, into another one
		struct QuadVertex { float x, y, z, rhw; unsigned long color; float u, v; };
		const QuadVertex quad[4] = {
			{ 15.5f, 15.5f, 0.0f, 1.0f, 0xffffffff, 1.0f, 1.0f },
			{ 15.5f, -0.5f, 0.0f, 1.0f, 0xffffffff, 1.0f, 0.0f },
			{ -0.5f, 15.5f, 0.0f, 1.0f, 0xffffffff, 0.0f, 1.0f },
			{ -0.5f, -0.5f, 0.0f, 1.0f, 0xffffffff, 0.0f, 0.0f },
		};
		HRESULT boundOut = dev->SetRenderTarget(outSurf, NULL);
		dev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(255, 0, 0), 1.0f, 0);	// red: seen if the sample fails
		dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
		dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		dev->SetRenderState(D3DRS_LIGHTING, FALSE);
		dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
		dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
		dev->SetTexture(0, mirror);
		dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
		dev->BeginScene();
		HRESULT drawn = dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(QuadVertex));
		dev->EndScene();
		HRESULT copied = dev->CopyRects(outSurf, NULL, 0, copySurf, NULL);
		HRESULT restoredAgain = dev->SetRenderTarget(bb, autoZ);
		dev->SetTexture(0, NULL);
		dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		printf("reflection: bind hr=0x%08lx, clear hr=0x%08lx, restore hr=0x%08lx, resample bind hr=0x%08lx, draw hr=0x%08lx, copy hr=0x%08lx, restore hr=0x%08lx\n",
			(unsigned long)boundMirror, (unsigned long)clearedMirror, (unsigned long)restored,
			(unsigned long)boundOut, (unsigned long)drawn, (unsigned long)copied,
			(unsigned long)restoredAgain);

		bool sampled = false;
		if (SUCCEEDED(copySurf->LockRect(&lr, NULL, D3DLOCK_READONLY))) {
			const unsigned char * px = (const unsigned char *)lr.pBits + 8 * lr.Pitch + 8 * 4;
			printf("reflection: sampled (b,g,r) = (%u,%u,%u), want (192,96,0)\n", px[0], px[1], px[2]);
			sampled = px[0] > 184 && px[0] < 200 && px[1] > 88 && px[1] < 104 && px[2] < 8;
			copySurf->UnlockRect();
		}
		const bool viewportFollowed = SUCCEEDED(gotVp) && vp.Width == 64 && vp.Height == 64
			&& vp.X == 0 && vp.Y == 0;

		copySurf->Release();
		if (autoZ) autoZ->Release();
		bb->Release(); outSurf->Release(); out->Release();
		mirrorZ->Release(); mirrorSurf->Release(); mirror->Release();

		if (!viewportFollowed) {
			printf("FAIL: binding a 64x64 render target left the viewport at %lux%lu - a reflection drawn through this lands in one corner of its texture\n",
				(unsigned long)vp.Width, (unsigned long)vp.Height);
			dev->Release(); d3d->Release(); DestroyWindow(hwnd);
			return 1;
		}
		if (FAILED(boundMirror) || FAILED(clearedMirror) || FAILED(restored)
			|| FAILED(boundOut) || FAILED(drawn) || FAILED(copied) || FAILED(restoredAgain)
			|| !sampled) {
			printf("FAIL: a render target could not be sampled as a texture in a later pass (the water reflection's whole job)\n");
			dev->Release(); d3d->Release(); DestroyWindow(hwnd);
			return 1;
		}
	}

	HRESULT phr = dev->Present(NULL, NULL, NULL, NULL);
	printf("Present hr=0x%08lx\n", (unsigned long)phr);
	if (show) {
		// keep clearing + presenting for 8 s while pumping messages, so a human can look
		const DWORD until = GetTickCount() + 8000;
		while (GetTickCount() < until) {
			MSG msg;
			while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
			dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(160, 0, 200), 1.0f, 0);
			dev->Present(NULL, NULL, NULL, NULL);
			Sleep(16);
		}
	}

	// which runtime the d3d8.dll we ended up with put the device on: the system d3d8.dll
	// (Direct3D 8), our d3d8to9 (Direct3D 9), or its "-d3d12" / D3D8TO9_D3D12 opt-in
	// (Direct3D 9On12 -> Direct3D 12).  Checked while the device is alive: d3d9 unloads
	// d3d9on12.dll/d3d12.dll again once the last object is released.
	printf("runtime: %s\n", GetModuleHandleA("d3d9on12.dll") ? "Direct3D 12 (9On12)"
	                      : GetModuleHandleA("d3d9.dll")     ? "Direct3D 9"
	                                                         : "Direct3D 8");
	printf("loaded: d3d9=%d d3d9on12=%d d3d12=%d D3D12Core=%d\n",
	       GetModuleHandleA("d3d9.dll") != NULL, GetModuleHandleA("d3d9on12.dll") != NULL,
	       GetModuleHandleA("d3d12.dll") != NULL, GetModuleHandleA("D3D12Core.dll") != NULL);
	const bool onD3D12 = GetModuleHandleA("d3d9on12.dll") != NULL && GetModuleHandleA("d3d12.dll") != NULL;
	if (wantD3D12 ? !onD3D12 : onD3D12) {
		printf(wantD3D12 ? "FAIL: -d3d12 requested but the device is not on Direct3D 12 (d3d8to9 fell back, or this is not our d3d8.dll)\n"
		                 : "FAIL: the device is on Direct3D 9On12 without -d3d12\n");
		dev->Release();
		d3d->Release();
		DestroyWindow(hwnd);
		return 1;
	}

	dev->Release();
	d3d->Release();
	DestroyWindow(hwnd);
	printf("DX8 smoke OK\n");
	return 0;
}
