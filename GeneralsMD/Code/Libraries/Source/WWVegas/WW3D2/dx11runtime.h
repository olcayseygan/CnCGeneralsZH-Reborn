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
class DX11BufferTwinClass;
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

// The Direct3D 11 copy of a vertex or index buffer the engine is about to create, or null on an
// ordinary run.  The buffer classes own what comes back and free it with themselves.
DX11BufferTwinClass * Direct3D11_Twin_Vertex_Buffer(unsigned byte_count, bool dynamic);
DX11BufferTwinClass * Direct3D11_Twin_Index_Buffer(unsigned byte_count, bool dynamic);

// The texture bound to a stage.  The Direct3D 11 copy is built from the Direct3D 9 texture on the
// first bind and kept on it, so nothing in the loaders has to know this exists.
void Direct3D11_Mirror_Texture(unsigned stage, struct IDirect3DBaseTexture9 * texture);

// The CPU has just written this surface.  The next bind of its texture fills the Direct3D 11 copy
// again.  A no-op when the backend is not running.
void Direct3D11_Mark_Surface_Dirty(struct IDirect3DSurface9 * surface);

// Send the draws into the copy of whatever texture this surface belongs to.  A null surface, or one
// with no texture behind it, is the back buffer.
void Direct3D11_Mirror_Render_Target(struct IDirect3DSurface9 * surface);

// One of the engine's surface copies, carried into the copy of the destination texture.  This is
// how a default-pool texture the CPU cannot read - the shroud - reaches D3D11 at all.
void Direct3D11_Mirror_Surface_Copy(struct IDirect3DSurface9 * destination,
	struct IDirect3DSurface9 * source, const struct tagRECT * source_rectangle,
	const struct tagPOINT * destination_point);

// The frame, alongside Direct3D 9's own.  Begin binds the back buffer and the viewport, Clear
// takes the same arguments DX8Wrapper::Clear was given, and End presents only when the run asked
// to see the Direct3D 11 picture - otherwise the frame is drawn into a back buffer nothing shows,
// which is what makes it safe to leave both paths running.
void Direct3D11_Begin_Scene();
void Direct3D11_Mirror_Clear(bool colour, bool depth, float red, float green, float blue,
	float alpha);
void Direct3D11_End_Scene(bool flip_frames);

// -dx11present: show the Direct3D 11 frame in the window instead of Direct3D 9's, and read
// screenshots back from it.  Without it nothing on screen changes when -dx11 is passed.
// -dx11dump: write every generated program to this directory as it is built.
void Direct3D11_Dump_Programs_To(const char * directory);

void Direct3D11_Present_Enable(bool enabled);
bool Direct3D11_Present_Is_Enabled();

// Wait for the monitor on the Direct3D 11 present.  DXGI takes the interval per Present, so this
// does not need a device reset the way Direct3D 9's presentation interval does.
void Direct3D11_Set_VSync(bool enabled);

// -dx11post: the effects run over the finished frame, named in the order they run, as in "fxaa" or
// "fxaa,sharpen".  "off" or an empty chain draws straight into the swap chain the way the backend
// always has.  Returns false for a name it does not know and turns the chain off, so a misspelt
// switch is a plain frame and a report line rather than a different picture.
//
// This is the first thing in the backend that draws a frame Direct3D 9 does not draw, so it is off
// unless it is asked for and dx11-check.ps1's exit measurement is taken without it.
bool Direct3D11_Post_Chain(const char * chain);

// The chain and the size it runs at, for the shutdown report, or why there is no chain.
const char * Direct3D11_Post_Diagnostic();

// Run the chain into the swap chain's back buffer, now, because the world is drawn and the command
// bar is about to go on top of it.  Everything drawn after this lands in the swap chain directly.
// An edge filter over a bitmap font does not antialias the letters, it doubles them, so where this
// is called from is as much a part of the feature as the shader is.
void Direct3D11_Finish_Scene();

// The scene in front of the player with nothing over it, for a frame that never reached the world:
// a menu, a loading screen. The present path and the screenshot path both call it and neither knows
// whether the other has, so the first one to ask pays and the second finds the work done.
void Direct3D11_Finish_Frame();

// The back buffer as it stands, in the eight-bit blue-green-red-alpha order the screenshot writer
// wants, top row first.  Has to be called before the present that discards it.  Null when there is
// nothing to read; what comes back is freed with Direct3D11_Release_Capture and nothing else.
unsigned char * Direct3D11_Capture_Back_Buffer(unsigned & width, unsigned & height,
	unsigned & pitch);
void Direct3D11_Release_Capture(unsigned char * pixels);

// The buffers and the vertex format a draw is about to read, mirrored as DX8Wrapper binds them.
// A null twin unbinds, which is what a stream with no buffer means.  Index buffers in this engine
// are 16-bit without exception, so the format is not a parameter.
void Direct3D11_Mirror_Stream_Source(DX11BufferTwinClass * twin, unsigned stride, unsigned offset);
void Direct3D11_Mirror_Indices(DX11BufferTwinClass * twin);
void Direct3D11_Mirror_Vertex_Format(unsigned fvf);

// Whether the engine has one of its own Direct3D 9 shaders bound.  The backend generates its own
// programs out of the fixed-function state, so a draw made with a shipped .vso or .pso is a draw it
// has no equivalent for: it refuses that one rather than drawing the fixed-function approximation,
// which is how the terrain came out white instead of missing.  The exceptions are the shaders
// engineshader transcribes, which is why the vertex half is mirrored by pointer: the pointer is the
// only thing that says which file a bound shader came from.
void Direct3D11_Mirror_Pixel_Shader(const void * shader);
void Direct3D11_Mirror_Vertex_Shader(const void * shader);

// The engine loaded a shader out of the big archives and the device made it.  Registering it here
// is what lets a later bind name it; a shader that is never registered is foreign, which is the
// state everything was in before this existed.
void Direct3D11_Register_Engine_Shader(const void * shader, const char * file_path);

// The float4 register bank the engine's own shaders read, mirrored as DX8Wrapper sets it.
void Direct3D11_Mirror_Vertex_Shader_Constant(unsigned first_register, const float * values,
	unsigned count);

// The draw, made alongside the Direct3D 9 one rather than instead of it: phase 2 is finished when
// the D3D11 picture can replace the D3D9 one, and until then both have to be able to produce it.
// False means the backend could not resolve a pipeline for this state and drew nothing, which is
// counted and reported rather than asserted.
bool Direct3D11_Draw_Indexed_Triangles(unsigned index_count, unsigned start_index,
	unsigned base_vertex);

// The same draw over a triangle strip, which is the topology both water grids are indexed for.
bool Direct3D11_Draw_Indexed_Strip(unsigned index_count, unsigned start_index,
	unsigned base_vertex);

// How many twins the run made and what they cost in video memory, counted as they are created.  A
// -dx11 run that mirrors nothing draws exactly like one that mirrors everything until the draws
// move over, so the count is the only thing that says the buffers went across.
void Direct3D11_Twin_Statistics(unsigned & buffers_made, unsigned long long & bytes_made);

// The same for textures: how many were copied, how many binds reused a copy, and how many could
// not be copied.
void Direct3D11_Texture_Statistics(unsigned & textures_made, unsigned & textures_reused,
	unsigned & textures_refused);
const char * Direct3D11_Texture_First_Refusal();

// One line per sixteen bit colour texture copied, naming its size and the average colour of its top
// level.  The terrain atlas is one of these, and its average is what says whether the copy holds
// the ground.
unsigned Direct3D11_Texture_Note_Count();
const char * Direct3D11_Texture_Note(unsigned index);

// The texture shapes copied most often, most first.  Count before reading the lines.
unsigned Direct3D11_Texture_Copy_Shape_Count();
const char * Direct3D11_Texture_Copy_Shape(unsigned index);

// What the run did, for the log: how many pipelines were built and how many draws the backend
// refused.  A backend that refuses most of the draws looks like a renderer with a lot missing and
// says nothing about it otherwise.
void Direct3D11_Statistics(unsigned & pipelines_built, unsigned long long & draws_made,
	unsigned long long & draws_refused);

// What the frame since the last call spent building pipelines and copying textures, and how many
// of each.  Taking it resets it.
void Direct3D11_Take_Frame_Cost(double & pipeline_milliseconds, unsigned & pipelines,
	double & texture_milliseconds, unsigned & textures);

// The refusals split by cause: no buffer bound, no texture stage enabled, a vertex format with no
// input layout, and a program that could not be generated or compiled.
void Direct3D11_Refusals(unsigned long long & no_buffer, unsigned long long & no_stage,
	unsigned long long & no_layout, unsigned long long & no_program,
	unsigned long long & no_object, unsigned long long & foreign_shader,
	unsigned long long & no_texture);

// The distinct refused states, as the keys the backend cached them under, and the first thing the
// shader compiler objected to.  Both are strings for the log and nothing else reads them.
unsigned Direct3D11_Refused_Description_Count();
const char * Direct3D11_Refused_Description(unsigned index);

// The same listing for the pipelines that did draw, with the draws each took and the size of the
// texture bound at stage zero the first time it was taken.
// How many times a texture was bound as the render target, and how many draws landed in one.
void Direct3D11_Target_Statistics(unsigned long long & bound, unsigned long long & restored,
	unsigned long long & draws);

// A screen-space quad handed over as vertices rather than as a buffer.  Returns false when the
// backend refused it, the way the other draw mirrors do.
bool Direct3D11_Draw_User_Strip(const void * vertices, unsigned primitive_count, unsigned stride);

// One line about the first screen-space quad drawn: where it was, and what was in the texture it
// sampled.  Empty when none was drawn.
const char * Direct3D11_Diagnostic();

// The first few target changes in order, each with the draw count when it happened.
unsigned Direct3D11_Target_Trace_Count();
const char * Direct3D11_Target_Trace(unsigned index);

unsigned Direct3D11_Pipeline_Report_Count();
const char * Direct3D11_Pipeline_Report(unsigned index);

// One line per shipped shader the refused draws had bound, with what each cost.  This is the list
// of what is still to be transcribed, and nothing else in the run names them.
unsigned Direct3D11_Foreign_Report_Count();
const char * Direct3D11_Foreign_Report(unsigned index);
const char * Direct3D11_First_Compiler_Error();

#endif // DX11RUNTIME_H
