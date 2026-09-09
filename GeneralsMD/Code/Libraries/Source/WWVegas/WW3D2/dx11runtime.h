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
** The one Direct3D 11 device and backend the process has, and the switch that asks for them.
**
** WW3D2 cannot see GlobalData, so -dx11 arrives the way -msaa and -ffshader do: pushed in from
** W3DDisplay::init before the device is made.  Asking for it is not the same as getting it - a
** machine with no Direct3D 11 driver and no WARP has neither - so Is_Active answers what actually
** happened rather than what was asked for, and everything that branches on the backend branches on
** that.
**
** RENDERER-ROADMAP.md's phase 2 is not finished while this is here: the engine still reaches the
** Direct3D 9 device directly from 236 places, and until those go through DX8Wrapper a -dx11 run
** has a backend that only the wrapper's own state calls reach.  Enable_Reports says how far that
** has got on any given run rather than leaving it to be guessed.
*/

#ifndef DX11RUNTIME_H
#define DX11RUNTIME_H

#include <windows.h>

class DX11BackendClass;
class DX11DeviceClass;

// Asked for on the command line, before any device exists.
void Direct3D11_Enable(bool enabled);
bool Direct3D11_Is_Enabled();

// Built once the window and its size are known.  False means the machine could not make one, and
// the caller carries on with Direct3D 9 rather than failing to start.
bool Direct3D11_Create(HWND window, unsigned width, unsigned height);
void Direct3D11_Release();

// True only between a successful Create and the Release that follows it.
bool Direct3D11_Is_Active();

DX11DeviceClass * Direct3D11_Device();
DX11BackendClass * Direct3D11_Backend();

// The state the wrapper is setting on the Direct3D 9 device, copied into the backend as it goes.
// Cheap enough to be unconditional - a comparison against a bool and a return when there is no
// backend - and it is what makes a -dx11 run's backend hold the same state the frame was drawn
// with, which is the thing every later step is measured against.  Declared here rather than on
// DX11BackendClass so dx8wrapper.h does not have to include the D3D11 headers.
void Direct3D11_Mirror_Render_State(unsigned state, unsigned value);
void Direct3D11_Mirror_Texture_Stage_State(unsigned stage, unsigned state, unsigned value);
void Direct3D11_Mirror_Sampler_State(unsigned sampler, unsigned state, unsigned value);

// The rest of what the fixed-function vertex pipeline reads.  The matrix is sixteen floats in the
// order D3D9 stores them, which is by rows; the material is five colours and a power; a light is
// the six four-float fields ffvertex declares, and a null one disables that index.
void Direct3D11_Mirror_Transform(unsigned transform, const float matrix[16]);
void Direct3D11_Mirror_Material(const float ambient[4], const float diffuse[4],
	const float specular[4], const float emissive[4], float power);
void Direct3D11_Mirror_Light(unsigned index, unsigned type, const float position[4],
	const float direction[4], const float diffuse[4], const float specular[4],
	const float attenuation[4], const float spot[4]);
void Direct3D11_Mirror_Light_Disabled(unsigned index);

// What the run did, for the log: how many pipelines were built and how many draws the backend
// refused.  A backend that refuses most of the draws looks like a renderer with a lot missing and
// says nothing about it otherwise.
void Direct3D11_Statistics(unsigned & pipelines_built, unsigned long long & draws_made,
	unsigned long long & draws_refused);

#endif // DX11RUNTIME_H
