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
** The generated combiner shaders, compiled once and kept.
**
** ffshader.cpp writes a texture stage description out as HLSL; this compiles it and hands the same
** shader back for every draw that asks for the same description.  Twenty-eight programs were
** measured over four maps, so the cache is small and never grows during a match once the frame has
** been through its passes.
**
** Reading the description off the device at every draw is what the caller does, because a call site
** that set its own stage states never went through DX8Wrapper's cache.  That is the slow half and
** it is deliberate: this exists to be compared against the fixed-function pipeline, not to be fast.
*/

#ifndef FFSHADERCACHE_H
#define FFSHADERCACHE_H

#include "ffshader.h"

// The description the device is currently configured for, read back off it.  StageCount is zero
// when stage 0's colour operation is D3DTOP_DISABLE, which is the device drawing untextured.
void CombinerShaderCache_Read_Device(IDirect3DDevice9 * device, CombinerDescription & description);

// The compiled shader for a description, compiling it the first time it is asked for.  NULL when
// the generator refuses the description or the compile fails, and the caller then leaves the draw
// on the fixed-function path.
IDirect3DPixelShader9 * CombinerShaderCache_Get(IDirect3DDevice9 * device,
	const CombinerDescription & description);

// Hands every compiled shader back.  The device owns them, so this has to run before it goes.
void CombinerShaderCache_Release();

// What the cache did, for the log line at shutdown: how many programs it compiled, how many
// descriptions it refused, and how many draws each of those was.
void CombinerShaderCache_Statistics(unsigned & compiled, unsigned & refused_descriptions,
	unsigned long long & shaded_draws, unsigned long long & refused_draws);

#endif
