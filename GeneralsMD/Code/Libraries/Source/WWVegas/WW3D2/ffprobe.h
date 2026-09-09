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
** The fixed-function inventory.
**
** RENDERER-ROADMAP.md's phase 2 replaces fixed-function multitexture with HLSL, and the first thing
** that has to be known is how many different things the pipeline is actually asked to compute.  The
** bit layout in shader.h can express tens of thousands of combinations; what the game sets during a
** match is a far smaller set, and it is the smaller set that has to be written as shaders.
**
** This reads the configuration back off the device at every draw, so it sees what a call site set
** directly as well as what went through DX8Wrapper, and counts each distinct combination once.
** Started with -ffprobe and dumped to Run/ffprobe.txt at shutdown.
*/

#ifndef FFPROBE_H
#define FFPROBE_H

#include <d3d9.h>

// Off unless -ffprobe was given.  Reading forty states back per draw call is not free.
void FixedFunctionProbe_Enable(bool enabled);
bool FixedFunctionProbe_Is_Enabled();

// Off unless -ffshader was given.  Replaces the texture stage combiners with a generated pixel
// shader for every draw the generator will take, which is what phase 2 compares against the
// fixed-function pipeline before carrying any of it to a second backend.
void CombinerShaders_Enable(bool enabled);
bool CombinerShaders_Are_Enabled();

// One draw call's worth: reads the stage combiners and the pixel-affecting render states off the
// device and counts the combination.
void FixedFunctionProbe_Record(IDirect3DDevice9 * device);

// Writes the inventory, most used first, and forgets it.  Does nothing when nothing was recorded.
void FixedFunctionProbe_Dump(const char * path);

#endif
