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

#include "d3dx9runtime.h"
#include "d3dx9math.h"

#include <stdio.h>

D3DXAssembleShaderFunction			D3DXAssembleShader = NULL;
D3DXDisassembleShaderFunction		D3DXDisassembleShader = NULL;
D3DXCreateTextureFunction			D3DXCreateTexture = NULL;
D3DXCreateCubeTextureFunction		D3DXCreateCubeTexture = NULL;
D3DXCreateVolumeTextureFunction		D3DXCreateVolumeTexture = NULL;
D3DXCreateTextureFromFileExFunction	D3DXCreateTextureFromFileExA = NULL;
D3DXFilterTextureFunction			D3DXFilterTexture = NULL;
D3DXLoadSurfaceFromSurfaceFunction	D3DXLoadSurfaceFromSurface = NULL;
D3DXGetFVFVertexSizeFunction		D3DXGetFVFVertexSize = NULL;

D3DXMatrixInverseFunction	D3DXMatrixInverse = NULL;
D3DXMatrixBinaryFunction	D3DXMatrixMultiply = NULL;
D3DXMatrixUnaryFunction		D3DXMatrixTranspose = NULL;
D3DXMatrixTripleFunction	D3DXMatrixScaling = NULL;
D3DXMatrixTripleFunction	D3DXMatrixTranslation = NULL;
D3DXMatrixAngleFunction		D3DXMatrixRotationZ = NULL;
D3DXVec4TransformFunction	D3DXVec4Transform = NULL;
D3DXVec3TransformFunction	D3DXVec3Transform = NULL;

// The last D3DX9 release, and the one d3d8to9 binds, so a machine that runs this fork
// today already has it.  There is no fallback to an earlier d3dx9_NN.dll on purpose:
// the earlier ones differ in behaviour, and a renderer that silently landed on one
// would be the hardest kind of bug to see.
static const char D3DX9_MODULE_NAME[] = "d3dx9_43.dll";

static HMODULE D3DX9Module = NULL;
static bool BindAttempted = false;
static bool BindSucceeded = false;

static void release_module(void);

struct ErrorName
{
	HRESULT Result;
	const char * Name;
};

// Every code the renderer's own paths return.  D3DERR values are D3D_OK plus the
// Direct3D facility, so they cannot be written as plain integers here.
static const ErrorName ERROR_NAMES[] =
{
	{ D3D_OK,							"D3D_OK" },
	{ D3DERR_DEVICELOST,				"D3DERR_DEVICELOST" },
	{ D3DERR_DEVICENOTRESET,			"D3DERR_DEVICENOTRESET" },
	{ D3DERR_DRIVERINTERNALERROR,		"D3DERR_DRIVERINTERNALERROR" },
	{ D3DERR_INVALIDCALL,				"D3DERR_INVALIDCALL" },
	{ D3DERR_INVALIDDEVICE,				"D3DERR_INVALIDDEVICE" },
	{ D3DERR_NOTAVAILABLE,				"D3DERR_NOTAVAILABLE" },
	{ D3DERR_NOTFOUND,					"D3DERR_NOTFOUND" },
	{ D3DERR_OUTOFVIDEOMEMORY,			"D3DERR_OUTOFVIDEOMEMORY" },
	{ D3DERR_TOOMANYOPERATIONS,			"D3DERR_TOOMANYOPERATIONS" },
	{ D3DERR_UNSUPPORTEDTEXTUREFILTER,	"D3DERR_UNSUPPORTEDTEXTUREFILTER" },
	{ D3DERR_WRONGTEXTUREFORMAT,		"D3DERR_WRONGTEXTUREFORMAT" },
	{ E_OUTOFMEMORY,					"E_OUTOFMEMORY" },
	{ E_INVALIDARG,						"E_INVALIDARG" },
	{ E_FAIL,							"E_FAIL" }
};

static const int ERROR_NAME_COUNT = sizeof(ERROR_NAMES) / sizeof(ERROR_NAMES[0]);
static const int ERROR_TEXT_SIZE = 32;

static char UnknownErrorText[ERROR_TEXT_SIZE] = "";

bool Bind_D3DX9_Runtime(void)
{
	if (BindAttempted) {
		return BindSucceeded;
	}
	BindAttempted = true;

	D3DX9Module = LoadLibraryA(D3DX9_MODULE_NAME);
	if (D3DX9Module == NULL) {
		return false;
	}

	D3DXAssembleShader = (D3DXAssembleShaderFunction)
		GetProcAddress(D3DX9Module, "D3DXAssembleShader");
	D3DXDisassembleShader = (D3DXDisassembleShaderFunction)
		GetProcAddress(D3DX9Module, "D3DXDisassembleShader");
	D3DXCreateTexture = (D3DXCreateTextureFunction)
		GetProcAddress(D3DX9Module, "D3DXCreateTexture");
	D3DXCreateCubeTexture = (D3DXCreateCubeTextureFunction)
		GetProcAddress(D3DX9Module, "D3DXCreateCubeTexture");
	D3DXCreateVolumeTexture = (D3DXCreateVolumeTextureFunction)
		GetProcAddress(D3DX9Module, "D3DXCreateVolumeTexture");
	D3DXCreateTextureFromFileExA = (D3DXCreateTextureFromFileExFunction)
		GetProcAddress(D3DX9Module, "D3DXCreateTextureFromFileExA");
	D3DXFilterTexture = (D3DXFilterTextureFunction)
		GetProcAddress(D3DX9Module, "D3DXFilterTexture");
	D3DXLoadSurfaceFromSurface = (D3DXLoadSurfaceFromSurfaceFunction)
		GetProcAddress(D3DX9Module, "D3DXLoadSurfaceFromSurface");
	D3DXGetFVFVertexSize = (D3DXGetFVFVertexSizeFunction)
		GetProcAddress(D3DX9Module, "D3DXGetFVFVertexSize");

	D3DXMatrixInverse = (D3DXMatrixInverseFunction)
		GetProcAddress(D3DX9Module, "D3DXMatrixInverse");
	D3DXMatrixMultiply = (D3DXMatrixBinaryFunction)
		GetProcAddress(D3DX9Module, "D3DXMatrixMultiply");
	D3DXMatrixTranspose = (D3DXMatrixUnaryFunction)
		GetProcAddress(D3DX9Module, "D3DXMatrixTranspose");
	D3DXMatrixScaling = (D3DXMatrixTripleFunction)
		GetProcAddress(D3DX9Module, "D3DXMatrixScaling");
	D3DXMatrixTranslation = (D3DXMatrixTripleFunction)
		GetProcAddress(D3DX9Module, "D3DXMatrixTranslation");
	D3DXMatrixRotationZ = (D3DXMatrixAngleFunction)
		GetProcAddress(D3DX9Module, "D3DXMatrixRotationZ");
	D3DXVec4Transform = (D3DXVec4TransformFunction)
		GetProcAddress(D3DX9Module, "D3DXVec4Transform");
	D3DXVec3Transform = (D3DXVec3TransformFunction)
		GetProcAddress(D3DX9Module, "D3DXVec3Transform");

	BindSucceeded = D3DXAssembleShader != NULL
		&& D3DXDisassembleShader != NULL
		&& D3DXCreateTexture != NULL
		&& D3DXCreateCubeTexture != NULL
		&& D3DXCreateVolumeTexture != NULL
		&& D3DXCreateTextureFromFileExA != NULL
		&& D3DXFilterTexture != NULL
		&& D3DXLoadSurfaceFromSurface != NULL
		&& D3DXGetFVFVertexSize != NULL
		&& D3DXMatrixInverse != NULL
		&& D3DXMatrixMultiply != NULL
		&& D3DXMatrixTranspose != NULL
		&& D3DXMatrixScaling != NULL
		&& D3DXMatrixTranslation != NULL
		&& D3DXMatrixRotationZ != NULL
		&& D3DXVec4Transform != NULL
		&& D3DXVec3Transform != NULL;

	if (!BindSucceeded) {
		release_module();
	}
	return BindSucceeded;
}

void Unbind_D3DX9_Runtime(void)
{
	release_module();
	BindAttempted = false;
	BindSucceeded = false;
}

static void release_module(void)
{
	D3DXAssembleShader = NULL;
	D3DXDisassembleShader = NULL;
	D3DXCreateTexture = NULL;
	D3DXCreateCubeTexture = NULL;
	D3DXCreateVolumeTexture = NULL;
	D3DXCreateTextureFromFileExA = NULL;
	D3DXFilterTexture = NULL;
	D3DXLoadSurfaceFromSurface = NULL;
	D3DXGetFVFVertexSize = NULL;

	D3DXMatrixInverse = NULL;
	D3DXMatrixMultiply = NULL;
	D3DXMatrixTranspose = NULL;
	D3DXMatrixScaling = NULL;
	D3DXMatrixTranslation = NULL;
	D3DXMatrixRotationZ = NULL;
	D3DXVec4Transform = NULL;
	D3DXVec3Transform = NULL;

	if (D3DX9Module != NULL) {
		FreeLibrary(D3DX9Module);
		D3DX9Module = NULL;
	}
}

const char * Get_D3D_Error_String(HRESULT result)
{
	for (int index = 0; index < ERROR_NAME_COUNT; ++index) {
		if (ERROR_NAMES[index].Result == result) {
			return ERROR_NAMES[index].Name;
		}
	}
	_snprintf(UnknownErrorText, ERROR_TEXT_SIZE, "0x%08lx", (unsigned long)result);
	UnknownErrorText[ERROR_TEXT_SIZE - 1] = '\0';
	return UnknownErrorText;
}
