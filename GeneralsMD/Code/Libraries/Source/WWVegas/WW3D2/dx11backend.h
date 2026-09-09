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
** The Direct3D 11 renderer, driven the way the engine drives a Direct3D 9 device.
**
** The engine sets one thing at a time and then draws: a render state, a texture stage state, a
** texture, a transform, a stream, and eventually a DrawIndexedPrimitive.  There are 236 places that
** do it and 5600 calls between them, and rewriting those into something D3D11 shaped is not a
** phase, it is a different program.  So this takes the calls as they are and resolves them at the
** moment of the draw, which is the only moment where everything needed to build a D3D11 pipeline is
** known at once.
**
** Resolving means five things.  The render states become the three state objects, the sampler
** states become sampler objects, the texture stage states become a pixel shader that ffshader
** generates, the lighting and transform states become a vertex shader that ffvertex generates, and
** the flexible vertex format becomes an input layout.  All five are cached on the description they
** came from, because the engine sets the same handful of combinations over and over: -ffprobe
** counted 42 distinct stage programs across four maps, and the state objects are fewer still.
**
** What this is not is a Direct3D 9 device.  It answers no COM interface, it is not handed to
** anything that expects one, and there is no D3D9 runtime underneath it: the calls have D3D9's
** names because the engine's calls have D3D9's names, and that is where the resemblance stops.
*/

#ifndef DX11BACKEND_H
#define DX11BACKEND_H

#include "dx11device.h"
#include "dx11state.h"
#include "ffshader.h"
#include "ffvertex.h"

#include <d3d9.h>
#include <map>
#include <string>

// The engine never binds more than two textures at once, which is what -ffprobe measured and what
// ffshader will generate for.  A third is a draw this refuses rather than draws wrongly.
const unsigned DX11_BACKEND_TEXTURE_STAGES = 2;

// D3DTSS_CONSTANT is the highest texture stage state at 32, so the block is indexed by the state
// itself and has to reach one past it.
const unsigned DX11_BACKEND_STAGE_STATES = 33;

class DX11BackendClass
{
public:
	DX11BackendClass();
	~DX11BackendClass();

	// device is borrowed, not owned: whoever created it outlives this.
	bool Initialise(DX11DeviceClass * device);
	void Shutdown();

	// The setters, named for the D3D9 calls the engine makes.  None of them touches the device;
	// they write into the shadow state that Draw resolves.
	void Set_Render_State(D3DRENDERSTATETYPE state, DWORD value);
	void Set_Texture_Stage_State(unsigned stage, D3DTEXTURESTAGESTATETYPE state, DWORD value);
	void Set_Sampler_State(unsigned sampler, D3DSAMPLERSTATETYPE state, DWORD value);
	void Set_Texture(unsigned stage, ID3D11ShaderResourceView * texture);
	void Set_Vertex_Format(DWORD fvf);
	void Set_Stream_Source(ID3D11Buffer * buffer, unsigned stride, unsigned offset);
	void Set_Indices(ID3D11Buffer * buffer, DXGI_FORMAT format);

	// The three transforms the generated vertex shader reads.  D3D9 keeps world, view and
	// projection apart and multiplies them itself; the shader wants the products, so they are
	// multiplied here when one of them changes rather than once per draw.
	void Set_Transform(D3DTRANSFORMSTATETYPE state, const float matrix[16]);

	// Everything the fixed-function vertex pipeline needs that is not a render state: the material
	// colours and the lights.  A light set here is on; Disable_Light takes it off again.
	void Set_Material(const float ambient[4], const float diffuse[4], const float specular[4],
		const float emissive[4], float power);
	void Set_Light(unsigned index, DWORD type, const float position[4], const float direction[4],
		const float diffuse[4], const float specular[4], const float attenuation[4],
		const float spot[4]);
	void Disable_Light(unsigned index);

	void Set_Viewport(unsigned x, unsigned y, unsigned width, unsigned height);
	void Clear(bool colour, bool depth, const float colour_value[4]);

	// The two draws.  Both resolve the shadow state into a pipeline first, and both return false
	// when some part of that state has no D3D11 answer, which leaves the draw undone rather than
	// drawn wrongly.
	bool Draw_Indexed_Triangles(unsigned index_count, unsigned start_index, unsigned base_vertex);
	bool Draw_Triangles(unsigned vertex_count, unsigned start_vertex);

	// How many pipelines were built and how many draws were refused, which is the same pair
	// -ffshader reports and the same thing it is for: a backend that silently refuses half the
	// draws looks like a renderer with a lot missing and no error anywhere.
	void Statistics(unsigned & pipelines_built, unsigned long long & draws_made,
		unsigned long long & draws_refused) const;

private:
	DX11BackendClass(const DX11BackendClass &);
	DX11BackendClass & operator=(const DX11BackendClass &);

	struct Pipeline
	{
		ID3D11VertexShader * VertexShader;
		ID3D11PixelShader * PixelShader;
		ID3D11InputLayout * Layout;
	};

	bool Resolve(Pipeline & pipeline);
	bool Build_Vertex_Description(VertexPipelineDescription & description) const;
	bool Build_Combiner_Description(CombinerDescription & description) const;
	void Upload_Constants();
	void Bind_State_Objects();
	void Release_Cached();

	ID3D11BlendState * Blend_State();
	ID3D11DepthStencilState * Depth_Stencil_State();
	ID3D11RasterizerState * Rasterizer_State();
	ID3D11SamplerState * Sampler_State(unsigned sampler);

	DX11DeviceClass * Device;

	DX11StateBlockClass RenderStates;
	DX11SamplerBlockClass Samplers[DX11_BACKEND_TEXTURE_STAGES];
	DWORD StageStates[DX11_BACKEND_TEXTURE_STAGES][DX11_BACKEND_STAGE_STATES];
	ID3D11ShaderResourceView * Textures[DX11_BACKEND_TEXTURE_STAGES];

	DWORD VertexFormat;
	ID3D11Buffer * StreamBuffer;
	unsigned StreamStride;
	unsigned StreamOffset;
	ID3D11Buffer * IndexBuffer;
	DXGI_FORMAT IndexFormat;

	float World[16];
	float View[16];
	float Projection[16];

	float MaterialAmbient[4];
	float MaterialDiffuse[4];
	float MaterialSpecular[4];
	float MaterialEmissive[4];
	float MaterialPower;

	struct Light
	{
		bool Enabled;
		DWORD Type;
		float Position[4];
		float Direction[4];
		float Diffuse[4];
		float Specular[4];
		float Attenuation[4];
		float Spot[4];
	};
	Light Lights[MAXIMUM_VERTEX_LIGHTS];

	ID3D11Buffer * VertexConstantBuffer;
	ID3D11Buffer * PixelConstantBuffer;

	std::map<std::string, Pipeline> Pipelines;
	std::map<std::string, ID3D11BlendState *> BlendStates;
	std::map<std::string, ID3D11DepthStencilState *> DepthStencilStates;
	std::map<std::string, ID3D11RasterizerState *> RasterizerStates;
	std::map<std::string, ID3D11SamplerState *> SamplerStates;

	unsigned PipelinesBuilt;
	unsigned long long DrawsMade;
	unsigned long long DrawsRefused;
};

#endif // DX11BACKEND_H
