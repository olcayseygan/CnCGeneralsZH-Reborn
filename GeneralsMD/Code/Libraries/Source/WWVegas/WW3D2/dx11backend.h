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
#include "engineshader.h"
#include "ffshader.h"
#include "ffvertex.h"

#include <d3d9.h>
#include <map>
#include <string>
#include <vector>

// Two everywhere except the water, which binds four: the river texture, the sparkles, the noise
// and the shroud.  This is what ffshader and ffvertex generate for, and a fifth is a draw this
// refuses rather than draws wrongly.
const unsigned DX11_BACKEND_TEXTURE_STAGES = 4;

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

	// The engine bound a texture at this stage that has no D3D11 copy - a render target it drew
	// into, most often.  Sampling white there paints a full screen quad over the frame, so a draw
	// that reads one is refused instead.
	void Set_Texture_Missing(unsigned stage, bool missing);
	void Set_Vertex_Format(DWORD fvf);

	// One of the engine's own D3D9 shaders is bound.  Most of the shipped .vso and .pso files have
	// no D3D11 counterpart, so a draw made while one is bound is refused and counted apart; the
	// ones engineshader transcribes are named instead and the draw takes that program.  The file
	// name comes with it because a refusal count says how much is missing and only the name says
	// which shader has to be written next.
	void Set_Pixel_Program(EngineShaderProgram program, bool bound, const char * file_name);
	void Set_Vertex_Program(EngineShaderProgram program, bool bound, const char * file_name);

	// The engine's own float4 shader constant bank, as it sets it.  The transcribed programs index
	// it by the same register number the SetVertexShaderConstantF call used.
	void Set_Vertex_Program_Constant(unsigned first_register, const float * values, unsigned count);
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

	// Write every program this builds to a file in this directory, named by the state it was built
	// from.  A generated program that draws the wrong thing is unreadable from the outside: the
	// description is a key, and the key is not the code.
	void Set_Dump_Directory(const char * directory);

	// Keep every compiled program in this file across runs: read now, written at Shutdown when a
	// program was compiled that the file did not hold.  A program compiled mid-match costs 25 to
	// 60ms on the frame that first needs it, which is the stutter a new explosion brought.
	void Set_Shader_Cache_Path(const char * path);
	unsigned Compiled_Program_Count() const { return static_cast<unsigned>(CompiledPrograms.size()); }

	// Binds the swap chain's back buffer and depth buffer and sets the viewport over the whole of
	// it.  D3D11 keeps no default target: without this every draw is complete, legal, and lands
	// nowhere.  Called at the start of each scene rather than once, because a resize replaces both
	// views and nothing tells the backend when that happens.
	void Begin_Scene();

	void Set_Viewport(unsigned x, unsigned y, unsigned width, unsigned height);
	void Clear(bool colour, bool depth, const float colour_value[4]);

	// Draw into a texture instead of into the back buffer.  A null target goes back to the back
	// buffer.  D3D11 wants the depth buffer to match the target's size, so one is made for each
	// size a target comes in and kept; the engine uses two or three of them in a match.
	void Set_Render_Target(ID3D11RenderTargetView * target);

	// The two draws.  Both resolve the shadow state into a pipeline first, and both return false
	// when some part of that state has no D3D11 answer, which leaves the draw undone rather than
	// drawn wrongly.
	bool Draw_Indexed_Triangles(unsigned index_count, unsigned start_index, unsigned base_vertex);
	bool Draw_Triangles(unsigned vertex_count, unsigned start_vertex);

	// The same indexed draw over a triangle strip, which is how the water lays out both of its
	// grids.  A strip drawn as a list reads three indices where the strip meant one triangle and
	// paints a third of the surface in torn triangles, so the topology cannot be assumed.
	bool Draw_Indexed_Strip(unsigned index_count, unsigned start_index, unsigned base_vertex);

	// A triangle strip handed over as vertices rather than as a buffer, which is how the engine
	// draws every screen-space quad it has.
	bool Draw_User_Strip(const void * vertices, unsigned primitive_count, unsigned stride);

	// How many pipelines were built and how many draws were refused, which is the same pair
	// -ffshader reports and the same thing it is for: a backend that silently refuses half the
	// draws looks like a renderer with a lot missing and no error anywhere.
	void Statistics(unsigned & pipelines_built, unsigned long long & draws_made,
		unsigned long long & draws_refused) const;

	// The pipelines built since the last call and the milliseconds they took, generation, compile
	// and device objects together.  A pipeline is built the first time its state reaches a draw, so
	// this is the stutter a new effect costs on the frame it first appears.
	void Take_Frame_Build_Cost(double & milliseconds, unsigned & pipelines);

	// Why the refusals happened, in the order Draw checks them: no buffer bound, no texture stage
	// enabled, a vertex format with no input layout, and a program the generator or the compiler
	// would not produce.  "Half the draws are refused" is not a finding; which half is.
	void Refusals(unsigned long long & no_buffer, unsigned long long & no_stage,
		unsigned long long & no_layout, unsigned long long & no_program,
		unsigned long long & no_object, unsigned long long & foreign_shader,
		unsigned long long & no_texture) const;

	// The distinct states that were refused, as the keys they were cached under, and whatever the
	// shader compiler said about the first one it rejected.  A count says how much is missing; the
	// key says what, and it is the only thing that does.
	unsigned Refused_Description_Count() const;
	const char * Refused_Description(unsigned index) const;

	// What each pipeline that did draw actually painted with: how many draws took it, and the size
	// of the texture bound at stage zero the first time one did.  A count of draws made says the
	// picture was drawn; it does not say what was drawn into it, and a ground that comes out white
	// looks exactly like a ground that came out right from every other number here.
	unsigned Pipeline_Report_Count() const;
	const char * Pipeline_Report(unsigned index);

	// The shipped shaders the refused draws had bound, and how many draws each cost.  This is the
	// list of what engineshader still has to transcribe, in the order worth doing it.
	unsigned Foreign_Report_Count() const;
	const char * Foreign_Report(unsigned index);

	// How many times the draws were sent into a texture instead of the back buffer, and how many
	// landed there.  A scene that comes back black usually means a target was set and not unset.
	void Target_Statistics(unsigned long long & bound, unsigned long long & restored,
		unsigned long long & draws) const
		{ bound = TargetsBound; restored = TargetsRestored; draws = DrawsIntoTargets; }
	const char * Diagnostic_Line() const { return Diagnostic.c_str(); }
	unsigned Target_Trace_Count() const { return (unsigned)TargetTrace.size(); }
	const char * Target_Trace(unsigned index) const
		{ return (index < TargetTrace.size()) ? TargetTrace[index].c_str() : ""; }
	const char * First_Compiler_Error() const { return CompilerError.c_str(); }

private:
	DX11BackendClass(const DX11BackendClass &);
	DX11BackendClass & operator=(const DX11BackendClass &);

	struct Pipeline
	{
		ID3D11VertexShader * VertexShader;
		ID3D11PixelShader * PixelShader;
		ID3D11InputLayout * Layout;
	};

	// The whole constant block the generated vertex shader reads, always uploaded in full.  A shader
	// generated for fewer lights declares a prefix of this, and a constant buffer larger than what a
	// shader declares is legal, so one buffer serves every program.
	struct VertexConstantBlock
	{
		float WorldViewProjection[16];
		float WorldView[16];
		float NormalTransform[16];
		float TextureMatrix[MAXIMUM_VERTEX_STAGES][16];
		float MaterialAmbient[4];
		float MaterialDiffuse[4];
		float MaterialSpecular[4];
		float MaterialEmissive[4];
		float MaterialPower[4];
		float GlobalAmbient[4];
		float FogParameters[4];
		// One over the viewport's width and height, for the pre-transformed draws.  It goes before
		// the lights because the generated block declares only as many lights as the state has.
		float ViewportInverse[4];
		float LightFields[MAXIMUM_VERTEX_LIGHTS][6][4];
	};

	struct PixelConstantBlock
	{
		float TextureFactor[4];
		float FogColour[4];
		float AlphaReference[4];
	};

	bool Resolve(Pipeline & pipeline);
	bool Build_Vertex_Description(VertexPipelineDescription & description) const;
	bool Build_Combiner_Description(CombinerDescription & description) const;
	void Upload_Constants();

	// Both indexed draws land here; only the topology differs.
	bool Draw_Indexed(unsigned index_count, unsigned start_index, unsigned base_vertex,
		D3D11_PRIMITIVE_TOPOLOGY topology);

	// Which bank the vertex half is reading: the engine's own registers while a transcribed
	// program is bound, the generated block otherwise.
	ID3D11Buffer * Vertex_Constants() const;
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
	// The per-stage texture transforms.  Two of them, because ffvertex writes two and the game sets
	// no more than two stages in any state the probe counted.  These are what turn a camera space
	// position into the cloud shadow's coordinates, and with an identity in their place the whole
	// terrain samples one texel of it.
	float TextureTransforms[DX11_BACKEND_TEXTURE_STAGES][16];

	// What the viewport is, so a pre-transformed vertex can be put back into clip space.
	unsigned ViewportWidth;
	unsigned ViewportHeight;

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

	// The engine's own constant bank and the buffer it is uploaded through.  It replaces the
	// generated vertex block at slot zero while a transcribed program is bound, because that
	// program reads registers and not the fixed-function fields.
	ID3D11Buffer * EngineConstantBuffer;
	float EngineConstants[ENGINE_SHADER_CONSTANTS][4];

	// What each constant buffer already holds.
	//
	// Upload_Constants runs on every draw and each Map asks for D3D11_MAP_WRITE_DISCARD, which is
	// the driver being told to hand back a fresh region: three renames a draw, thousands of draws a
	// frame, and NtGdiDdDDICreateAllocation sitting in a steady-state profile where nothing should
	// be allocating at all.  Most draws in a row change none of this - the transforms, the material
	// and the lights are set once for a whole batch - so the block is built into a local, compared
	// against the copy here, and written through only when it differs.
	//
	// Comparing bytes rather than flagging the setters dirty is deliberate: there are a dozen ways
	// into these blocks, including three render states read straight out of the state block, and a
	// setter nobody flagged would draw with the previous batch's lighting.
	VertexConstantBlock HeldVertexConstants;
	PixelConstantBlock HeldPixelConstants;
	float HeldEngineConstants[ENGINE_SHADER_CONSTANTS][4];

	// A freshly created dynamic buffer holds nothing in particular, and a Map can fail, so a copy
	// above only stands for what is in its buffer once a write has actually gone through.  One flag
	// each rather than one between them: the engine bank is skipped on every draw that takes no
	// transcribed program, and a shared flag would claim it was written when it was not.
	bool VertexConstantsHeld;
	bool PixelConstantsHeld;
	bool EngineConstantsHeld;
	EngineShaderProgram VertexProgram;
	EngineShaderProgram PixelProgram;

	// Where the screen quads are copied to, grown to fit and never shrunk.
	ID3D11Buffer * UserBuffer;
	unsigned UserBufferBytes;
	bool Reserve_User_Buffer(unsigned byte_count);

	std::map<std::string, Pipeline> Pipelines;
	// The state combinations that could not be built.  A refusal costs two HLSL generations and two
	// D3DCompile calls, and the states that produce one are produced again on the next frame by the
	// same object: a scene with a hundred and thirty of them a frame spent a second a frame
	// compiling shaders it had already failed to compile.
	std::map<std::string, unsigned> RefusedPipelines;

	// What a pipeline drew with, kept beside the pipeline itself rather than in it, because a draw
	// holds a copy of the pipeline and would be updating the copy.
	struct PipelineUse
	{
		unsigned long long Draws;
		unsigned TextureWidth;
		unsigned TextureHeight;
	};
	std::map<std::string, PipelineUse> PipelineUses;
	std::string ReportLine;
	PipelineUse * Record_Use(const std::string & key);

	// The last pipeline Resolve worked out, and the state it was worked out from.
	//
	// Resolve runs once a draw and its key costs about twenty snprintf calls, a handful of string
	// appends and two lookups in a map keyed by that string.  Sampling a burning column at 1280x720
	// put a eighth of the whole frame inside the C runtime's integer formatter.  The renderer
	// batches by texture and material, so consecutive draws ask for the same pipeline far more
	// often than not, and a draw that matches this skips the key entirely.
	//
	// The descriptions are compared with memcmp, which is safe because Build_Vertex_Description and
	// Build_Combiner_Description both memset before they fill: no padding byte is ever undefined.
	// Use points into PipelineUses, whose nodes are stable across insertion; the whole memo is
	// dropped in Release_Cached, where the pipelines it names are released.
	struct ResolveMemo
	{
		bool Valid;
		VertexPipelineDescription Vertex;
		CombinerDescription Combiner;
		DWORD Format;
		EngineShaderProgram VertexProgram;
		EngineShaderProgram PixelProgram;
		Pipeline Resolved;
		PipelineUse * Use;
	};
	ResolveMemo Memo;
	void Remember_Resolution(const std::string & key, const Pipeline & resolved,
		const VertexPipelineDescription & vertex, const CombinerDescription & combiner);

	// Where the draws are landing.  Null means the device's own back buffer and depth buffer.
	// The first few target changes, in order, with the draw count at each one.  Which target is
	// bound when the scene is drawn is the whole question and no total answers it.
	static const unsigned TARGET_TRACE_LIMIT = 24;
	std::vector<std::string> TargetTrace;
	void Trace_Target(const char * what, unsigned width, unsigned height);
	bool TracedUserStrip;
	DWORD MaskWhileTargeted;
	std::string Diagnostic;
	std::string Texture_Average(unsigned stage);

	unsigned long long TargetsBound;
	unsigned long long TargetsRestored;
	unsigned long long DrawsIntoTargets;
	ID3D11RenderTargetView * CurrentTarget;
	ID3D11DepthStencilView * CurrentDepth;
	std::map<unsigned long long, ID3D11DepthStencilView *> TargetDepths;
	ID3D11DepthStencilView * Depth_For(unsigned width, unsigned height);

	std::map<std::string, ID3D11BlendState *> BlendStates;
	std::map<std::string, ID3D11DepthStencilState *> DepthStencilStates;
	std::map<std::string, ID3D11RasterizerState *> RasterizerStates;
	std::map<std::string, ID3D11SamplerState *> SamplerStates;

	unsigned PipelinesBuilt;
	unsigned long long DrawsMade;
	unsigned long long DrawsRefused;
	double FrameBuildMilliseconds;
	unsigned FrameBuildCount;
	enum RefusalReason { REFUSED_NO_STAGE, REFUSED_NO_LAYOUT, REFUSED_NO_PROGRAM,
		REFUSED_NO_OBJECT };

	void Refuse(const std::string & key, RefusalReason reason);
	void Record_Compiler_Error(const char * which, ID3DBlob * errors);
	void Note_Refusal(const char * reason);
	bool Any_Missing_Texture() const;
	void Dump_Program(const std::string & key, const std::string & vertex_hlsl,
		const std::string & pixel_hlsl);
	bool Compile_Program(const std::string & source, const char * name, const char * profile,
		std::vector<unsigned char> & bytecode);
	void Save_Shader_Cache() const;

	std::string CompilerError;
	std::string DumpDirectory;

	// Bytecode by the hash of the profile and the source it was compiled from.  Two pipelines that
	// generate the same vertex program share one compile, and so do two runs through the file.
	std::map<unsigned long long, std::vector<unsigned char> > CompiledPrograms;
	std::string ShaderCachePath;
	bool ShaderCacheChanged;

	unsigned long long RefusedNoBuffer;
	unsigned long long RefusedNoStage;
	unsigned long long RefusedNoLayout;
	unsigned long long RefusedNoProgram;
	unsigned long long RefusedNoObject;
	unsigned long long RefusedForeignShader;
	unsigned long long RefusedNoTexture;

	bool MissingTexture[DX11_BACKEND_TEXTURE_STAGES];
	bool ForeignPixelShader;
	bool ForeignVertexShader;
	std::string ForeignPixelName;
	std::string ForeignVertexName;

	// Which shipped shader the refusals are on, counted by file name.  Nothing else says it: the
	// refusal happens before there is a pipeline key, so the state that would describe the draw is
	// never built.
	std::map<std::string, unsigned long long> ForeignRefusals;
	void Note_Foreign_Refusal();
};

#endif // DX11BACKEND_H
