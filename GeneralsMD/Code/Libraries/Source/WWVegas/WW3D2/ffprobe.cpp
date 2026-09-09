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

#include "ffprobe.h"

#include <map>
#include <stdio.h>
#include <vector>

// A stage is read until one turns its colour operation off, which is where the fixed-function
// pipeline stops looking as well.  Eight is the device maximum the wrapper allows for.
static const unsigned MAXIMUM_STAGES = 8;

// The per-stage states that decide what the stage computes.  Filtering, addressing and the
// LOD bias are sampler states in D3D9 and do not change the arithmetic, so they are not here.
static const D3DTEXTURESTAGESTATETYPE STAGE_STATES[] =
{
	D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_COLORARG2, D3DTSS_COLORARG0,
	D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2, D3DTSS_ALPHAARG0,
	D3DTSS_RESULTARG, D3DTSS_TEXCOORDINDEX, D3DTSS_TEXTURETRANSFORMFLAGS
};

// The render states that reach the pixel as well, and so become part of the shader or of the
// state object it is paired with rather than something the shader can ignore.
//
// D3DRS_TEXTUREFACTOR and D3DRS_ALPHAREF are deliberately not here.  They are uniforms: two draws
// that differ only by the shade of a texture factor are one thing to write, and keying on their
// values would count a fade as a new combination on every frame of it.
static const D3DRENDERSTATETYPE PIXEL_RENDER_STATES[] =
{
	D3DRS_LIGHTING, D3DRS_SPECULARENABLE, D3DRS_COLORVERTEX,
	D3DRS_FOGENABLE, D3DRS_FOGTABLEMODE, D3DRS_FOGVERTEXMODE,
	D3DRS_ALPHATESTENABLE, D3DRS_ALPHAFUNC,
	D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND
};

static const size_t STAGE_STATE_COUNT = sizeof(STAGE_STATES)/sizeof(STAGE_STATES[0]);
static const size_t PIXEL_RENDER_STATE_COUNT = sizeof(PIXEL_RENDER_STATES)/sizeof(PIXEL_RENDER_STATES[0]);

// A combination is the render states, then one block of stage states per live stage, then a flag
// for whether a vertex shader was doing the transform.  Compared and ordered as a plain sequence,
// which is all a std::map needs of it.
typedef std::vector<DWORD> Combination;

static bool _Enabled = false;
static bool _CombinerShadersEnabled = false;
static std::map<Combination, unsigned> _Counts;
static std::map<IDirect3DPixelShader9 *, unsigned> _PixelShaderIds;
static unsigned long long _DrawsSeen = 0;

void FixedFunctionProbe_Enable(bool enabled)
{
	_Enabled = enabled;
}

bool FixedFunctionProbe_Is_Enabled()
{
	return _Enabled;
}

void CombinerShaders_Enable(bool enabled)
{
	_CombinerShadersEnabled = enabled;
}

bool CombinerShaders_Are_Enabled()
{
	return _CombinerShadersEnabled;
}

void FixedFunctionProbe_Record(IDirect3DDevice9 * device)
{
	if (!_Enabled || device == NULL) {
		return;
	}
	++_DrawsSeen;

	Combination combination;
	combination.reserve(PIXEL_RENDER_STATE_COUNT + MAXIMUM_STAGES*STAGE_STATE_COUNT + 2);

	for (size_t index = 0; index < PIXEL_RENDER_STATE_COUNT; ++index) {
		DWORD value = 0;
		device->GetRenderState(PIXEL_RENDER_STATES[index], &value);
		combination.push_back(value);
	}

	IDirect3DVertexShader9 * vertex_shader = NULL;
	device->GetVertexShader(&vertex_shader);
	combination.push_back(vertex_shader != NULL ? 1 : 0);
	if (vertex_shader != NULL) {
		vertex_shader->Release();
	}

	// A bound pixel shader replaces the stage combiners outright, so the stage states are stale
	// values the device is ignoring and recording them would count noise as variety.  What matters
	// for such a draw is which shader it is, numbered in the order the probe first sees each one.
	IDirect3DPixelShader9 * pixel_shader = NULL;
	device->GetPixelShader(&pixel_shader);
	if (pixel_shader != NULL) {
		std::map<IDirect3DPixelShader9 *, unsigned>::const_iterator known = _PixelShaderIds.find(pixel_shader);
		unsigned identifier;
		if (known != _PixelShaderIds.end()) {
			identifier = known->second;
		}
		else {
			identifier = (unsigned)_PixelShaderIds.size() + 1;
			_PixelShaderIds[pixel_shader] = identifier;
		}
		combination.push_back(identifier);
		pixel_shader->Release();
		++_Counts[combination];
		return;
	}
	combination.push_back(0);

	for (unsigned stage = 0; stage < MAXIMUM_STAGES; ++stage) {
		DWORD colour_operation = D3DTOP_DISABLE;
		device->GetTextureStageState(stage, D3DTSS_COLOROP, &colour_operation);
		if (colour_operation == D3DTOP_DISABLE) {
			break;
		}
		for (size_t index = 0; index < STAGE_STATE_COUNT; ++index) {
			DWORD value = 0;
			device->GetTextureStageState(stage, STAGE_STATES[index], &value);
			combination.push_back(value);
		}
		// Whether a texture is bound changes what the stage's arguments mean, and it is one bit
		// rather than the texture's identity: two draws differing only by which grass they sample
		// are the same thing to write in HLSL.
		IDirect3DBaseTexture9 * texture = NULL;
		device->GetTexture(stage, &texture);
		combination.push_back(texture != NULL ? 1 : 0);
		if (texture != NULL) {
			texture->Release();
		}
	}

	++_Counts[combination];
}

static const char * stage_state_name(D3DTEXTURESTAGESTATETYPE state)
{
	switch (state) {
	case D3DTSS_COLOROP:                return "COLOROP";
	case D3DTSS_COLORARG1:              return "COLORARG1";
	case D3DTSS_COLORARG2:              return "COLORARG2";
	case D3DTSS_COLORARG0:              return "COLORARG0";
	case D3DTSS_ALPHAOP:                return "ALPHAOP";
	case D3DTSS_ALPHAARG1:              return "ALPHAARG1";
	case D3DTSS_ALPHAARG2:              return "ALPHAARG2";
	case D3DTSS_ALPHAARG0:              return "ALPHAARG0";
	case D3DTSS_RESULTARG:              return "RESULTARG";
	case D3DTSS_TEXCOORDINDEX:          return "TEXCOORDINDEX";
	case D3DTSS_TEXTURETRANSFORMFLAGS:  return "TEXTURETRANSFORMFLAGS";
	default:                            return "?";
	}
}

static const char * render_state_name(D3DRENDERSTATETYPE state)
{
	switch (state) {
	case D3DRS_LIGHTING:           return "LIGHTING";
	case D3DRS_SPECULARENABLE:     return "SPECULARENABLE";
	case D3DRS_COLORVERTEX:        return "COLORVERTEX";
	case D3DRS_FOGENABLE:          return "FOGENABLE";
	case D3DRS_FOGTABLEMODE:       return "FOGTABLEMODE";
	case D3DRS_FOGVERTEXMODE:      return "FOGVERTEXMODE";
	case D3DRS_ALPHATESTENABLE:    return "ALPHATESTENABLE";
	case D3DRS_ALPHAFUNC:          return "ALPHAFUNC";
	case D3DRS_ALPHAREF:           return "ALPHAREF";
	case D3DRS_ALPHABLENDENABLE:   return "ALPHABLENDENABLE";
	case D3DRS_SRCBLEND:           return "SRCBLEND";
	case D3DRS_DESTBLEND:          return "DESTBLEND";
	case D3DRS_TEXTUREFACTOR:      return "TEXTUREFACTOR";
	default:                       return "?";
	}
}

void FixedFunctionProbe_Dump(const char * path)
{
	if (_Counts.empty()) {
		return;
	}

	FILE * file = fopen(path, "wt");
	if (file == NULL) {
		return;
	}

	// Most used first: the head of this list is what an HLSL replacement has to get right before
	// anything else, and the tail is what can be left to a slow general path.
	std::multimap<unsigned, const Combination *> by_use;
	for (std::map<Combination, unsigned>::const_iterator entry = _Counts.begin();
			entry != _Counts.end(); ++entry) {
		by_use.insert(std::make_pair(entry->second, &entry->first));
	}

	// Two populations, and they are two different jobs.  A draw with a pixel shader already has its
	// arithmetic written down and only has to be translated; a draw without one is arithmetic that
	// exists nowhere but in the stage combiners and has to be written from scratch.
	unsigned shader_combinations = 0;
	unsigned shader_draws = 0;
	for (std::map<Combination, unsigned>::const_iterator entry = _Counts.begin();
			entry != _Counts.end(); ++entry) {
		if (entry->first[PIXEL_RENDER_STATE_COUNT + 1] != 0) {
			++shader_combinations;
			shader_draws += entry->second;
		}
	}

	fprintf(file, "%u distinct combinations over %llu draw calls\n",
		(unsigned)_Counts.size(), _DrawsSeen);
	fprintf(file, "%u of them fixed function (%llu draws), %u driven by a pixel shader (%u draws)\n",
		(unsigned)_Counts.size() - shader_combinations, _DrawsSeen - shader_draws,
		shader_combinations, shader_draws);
	fprintf(file, "%u distinct pixel shaders seen\n\n", (unsigned)_PixelShaderIds.size());

	unsigned rank = 0;
	for (std::multimap<unsigned, const Combination *>::const_reverse_iterator entry = by_use.rbegin();
			entry != by_use.rend(); ++entry) {
		const Combination & combination = *entry->second;
		fprintf(file, "#%u  %u draws (%.2f%%)\n", ++rank, entry->first,
			100.0 * entry->first / (double)(_DrawsSeen ? _DrawsSeen : 1));

		size_t position = 0;
		for (size_t index = 0; index < PIXEL_RENDER_STATE_COUNT; ++index, ++position) {
			fprintf(file, "    %-22s %u\n", render_state_name(PIXEL_RENDER_STATES[index]),
				combination[position]);
		}
		fprintf(file, "    %-22s %u\n", "vertex shader", combination[position++]);
		fprintf(file, "    %-22s %u\n", "pixel shader", combination[position++]);

		unsigned stage = 0;
		while (position < combination.size()) {
			fprintf(file, "    stage %u\n", stage++);
			for (size_t index = 0; index < STAGE_STATE_COUNT; ++index, ++position) {
				fprintf(file, "        %-22s %u\n", stage_state_name(STAGE_STATES[index]),
					combination[position]);
			}
			fprintf(file, "        %-22s %u\n", "texture bound", combination[position++]);
		}
		fprintf(file, "\n");
	}

	fclose(file);
	_Counts.clear();
	_PixelShaderIds.clear();
	_DrawsSeen = 0;
}
