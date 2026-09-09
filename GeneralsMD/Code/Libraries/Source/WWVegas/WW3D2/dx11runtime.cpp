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

#include "dx11runtime.h"

#include "dx11backend.h"
#include "dx11device.h"

static bool Requested = false;
static bool Active = false;
static DX11DeviceClass Device;
static DX11BackendClass Backend;

void Direct3D11_Enable(bool enabled)
{
	Requested = enabled;
}

bool Direct3D11_Is_Enabled()
{
	return Requested;
}

bool Direct3D11_Create(HWND window, unsigned width, unsigned height)
{
	if (!Requested || Active) {
		return Active;
	}

	if (!Device.Create(window, width, height)) {
		return false;
	}
	if (!Backend.Initialise(&Device)) {
		Device.Release();
		return false;
	}

	Active = true;
	return true;
}

void Direct3D11_Release()
{
	if (!Active) {
		return;
	}
	Backend.Shutdown();
	Device.Release();
	Active = false;
}

bool Direct3D11_Is_Active()
{
	return Active;
}

DX11DeviceClass * Direct3D11_Device()
{
	return Active ? &Device : NULL;
}

DX11BackendClass * Direct3D11_Backend()
{
	return Active ? &Backend : NULL;
}

void Direct3D11_Mirror_Render_State(unsigned state, unsigned value)
{
	if (Active) {
		Backend.Set_Render_State(static_cast<D3DRENDERSTATETYPE>(state), value);
	}
}

void Direct3D11_Mirror_Texture_Stage_State(unsigned stage, unsigned state, unsigned value)
{
	if (Active) {
		Backend.Set_Texture_Stage_State(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(state), value);
	}
}

void Direct3D11_Mirror_Sampler_State(unsigned sampler, unsigned state, unsigned value)
{
	if (Active) {
		Backend.Set_Sampler_State(sampler, static_cast<D3DSAMPLERSTATETYPE>(state), value);
	}
}

void Direct3D11_Mirror_Transform(unsigned transform, const float matrix[16])
{
	if (Active) {
		Backend.Set_Transform(static_cast<D3DTRANSFORMSTATETYPE>(transform), matrix);
	}
}

void Direct3D11_Mirror_Material(const float ambient[4], const float diffuse[4],
	const float specular[4], const float emissive[4], float power)
{
	if (Active) {
		Backend.Set_Material(ambient, diffuse, specular, emissive, power);
	}
}

void Direct3D11_Mirror_Light(unsigned index, unsigned type, const float position[4],
	const float direction[4], const float diffuse[4], const float specular[4],
	const float attenuation[4], const float spot[4])
{
	if (Active) {
		Backend.Set_Light(index, type, position, direction, diffuse, specular, attenuation, spot);
	}
}

void Direct3D11_Mirror_Light_Disabled(unsigned index)
{
	if (Active) {
		Backend.Disable_Light(index);
	}
}

void Direct3D11_Statistics(unsigned & pipelines_built, unsigned long long & draws_made,
	unsigned long long & draws_refused)
{
	pipelines_built = 0;
	draws_made = 0;
	draws_refused = 0;
	if (Active) {
		Backend.Statistics(pipelines_built, draws_made, draws_refused);
	}
}
