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

#include "ffshadercache.h"

#include "d3dx9runtime.h"

#include <map>
#include <string.h>
#include <string>

// ps_2_0 rather than ps_1_1: the generated arithmetic is written as HLSL and a two-stage combiner
// with a dot product does not fit the older model's instruction set.  Every card that runs this
// fork has it, and a card that does not fails the compile and keeps the fixed-function path.
static const char * const COMPILE_PROFILE = "ps_2_0";
static const char * const COMPILE_ENTRY_POINT = "main";

static std::map<std::string, IDirect3DPixelShader9 *> _Shaders;
static unsigned _Compiled = 0;
static unsigned _RefusedDescriptions = 0;
static unsigned long long _ShadedDraws = 0;
static unsigned long long _RefusedDraws = 0;

void CombinerShaderCache_Read_Device(IDirect3DDevice9 * device, CombinerDescription & description)
{
	// Zeroed whole, so the alpha test and fog fields are off rather than whatever the caller's
	// stack held.  On the D3D9 profile they generate nothing either way, but they are in the cache
	// key, and a key built out of uninitialised memory is a cache that never hits.
	memset(&description, 0, sizeof(description));

	description.StageCount = 0;
	for (unsigned stage = 0; stage < MAXIMUM_COMBINER_STAGES; ++stage) {
		DWORD colour_operation = D3DTOP_DISABLE;
		device->GetTextureStageState(stage, D3DTSS_COLOROP, &colour_operation);
		if (colour_operation == D3DTOP_DISABLE) {
			return;
		}

		CombinerStage & target = description.Stages[stage];
		target.ColourOperation = colour_operation;
		device->GetTextureStageState(stage, D3DTSS_COLORARG0, &target.ColourArgument0);
		device->GetTextureStageState(stage, D3DTSS_COLORARG1, &target.ColourArgument1);
		device->GetTextureStageState(stage, D3DTSS_COLORARG2, &target.ColourArgument2);
		device->GetTextureStageState(stage, D3DTSS_ALPHAOP, &target.AlphaOperation);
		device->GetTextureStageState(stage, D3DTSS_ALPHAARG0, &target.AlphaArgument0);
		device->GetTextureStageState(stage, D3DTSS_ALPHAARG1, &target.AlphaArgument1);
		device->GetTextureStageState(stage, D3DTSS_ALPHAARG2, &target.AlphaArgument2);
		device->GetTextureStageState(stage, D3DTSS_TEXCOORDINDEX, &target.TextureCoordinateIndex);

		IDirect3DBaseTexture9 * texture = NULL;
		device->GetTexture(stage, &texture);
		target.TextureBound = texture != NULL;
		if (texture != NULL) {
			texture->Release();
		}

		description.StageCount = stage + 1;
	}

	// A third live stage is a description the generator refuses anyway, but it has to be seen as
	// three rather than silently truncated to two, or the wrong shader is handed back for it.
	DWORD third_stage_operation = D3DTOP_DISABLE;
	device->GetTextureStageState(MAXIMUM_COMBINER_STAGES, D3DTSS_COLOROP, &third_stage_operation);
	if (third_stage_operation != D3DTOP_DISABLE) {
		description.StageCount = MAXIMUM_COMBINER_STAGES + 1;
	}
}

IDirect3DPixelShader9 * CombinerShaderCache_Get(IDirect3DDevice9 * device,
	const CombinerDescription & description)
{
	const std::string key = CombinerShader_Key(description);
	const std::map<std::string, IDirect3DPixelShader9 *>::const_iterator known = _Shaders.find(key);
	if (known != _Shaders.end()) {
		if (known->second != NULL) {
			++_ShadedDraws;
		}
		else {
			++_RefusedDraws;
		}
		return known->second;
	}

	std::string hlsl;
	if (!CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, hlsl)) {
		_Shaders[key] = NULL;
		++_RefusedDescriptions;
		++_RefusedDraws;
		return NULL;
	}

	LPD3DXBUFFER compiled = NULL;
	LPD3DXBUFFER errors = NULL;
	const HRESULT result = D3DXCompileShader(hlsl.c_str(), (UINT)hlsl.size(), NULL, NULL,
		COMPILE_ENTRY_POINT, COMPILE_PROFILE, 0, &compiled, &errors, NULL);
	if (errors != NULL) {
		errors->Release();
	}

	IDirect3DPixelShader9 * shader = NULL;
	if (SUCCEEDED(result) && compiled != NULL) {
		device->CreatePixelShader((const DWORD *)compiled->GetBufferPointer(), &shader);
	}
	if (compiled != NULL) {
		compiled->Release();
	}

	_Shaders[key] = shader;
	if (shader != NULL) {
		++_Compiled;
		++_ShadedDraws;
	}
	else {
		++_RefusedDescriptions;
		++_RefusedDraws;
	}
	return shader;
}

void CombinerShaderCache_Release()
{
	for (std::map<std::string, IDirect3DPixelShader9 *>::iterator entry = _Shaders.begin();
			entry != _Shaders.end(); ++entry) {
		if (entry->second != NULL) {
			entry->second->Release();
		}
	}
	_Shaders.clear();
}

void CombinerShaderCache_Statistics(unsigned & compiled, unsigned & refused_descriptions,
	unsigned long long & shaded_draws, unsigned long long & refused_draws)
{
	compiled = _Compiled;
	refused_descriptions = _RefusedDescriptions;
	shaded_draws = _ShadedDraws;
	refused_draws = _RefusedDraws;
}
