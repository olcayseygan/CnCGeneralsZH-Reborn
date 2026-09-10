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

#include <map>
#include <string>

#include "dx11backend.h"
#include "dx11device.h"
#include "dx11post.h"
#include "dx11texture.h"
#include "dx11twin.h"

static bool Requested = false;
static bool PresentRequested = false;
static bool VSyncRequested = false;
static bool Active = false;
static DX11DeviceClass Device;
static DX11BackendClass Backend;
static DX11PostProcessClass Post;
static unsigned TwinBuffers = 0;
static unsigned long long TwinBytes = 0;
static std::string DumpDirectory;

// Relative to the working directory, which for the game is Run/, next to the exe.
static const char * const SHADER_CACHE_FILE = "dx11shaders.cache";

// What -dx11post asked for, kept as the effects rather than as the text so a name nobody knows is
// refused when the switch is read and not once a frame.
static DX11PostEffect PostChain[DX11_POST_CHAIN_LIMIT];
static unsigned PostChainLength = 0;

// Which shipped shader each created Direct3D 9 shader came from.  Registered at load, read at every
// bind.  It outlives the shaders themselves, which cost the map an entry each and nothing else: the
// engine loads ten of them once and releases them when the game ends.
struct EngineShaderRecord
{
	EngineShaderProgram Program;
	std::string FileName;
};
static std::map<const void *, EngineShaderRecord> EngineShaders;

static const EngineShaderRecord & engine_shader_record(const void * shader)
{
	static const EngineShaderRecord UNKNOWN = { ENGINE_SHADER_NONE, "an unregistered shader" };

	std::map<const void *, EngineShaderRecord>::const_iterator entry = EngineShaders.find(shader);
	return (entry != EngineShaders.end()) ? entry->second : UNKNOWN;
}

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

	Backend.Set_Dump_Directory(DumpDirectory.c_str());
	Backend.Set_Shader_Cache_Path(SHADER_CACHE_FILE);

	// The chain is the one thing here that is allowed to fail without taking the backend with it:
	// a machine whose compiler refuses the passes still gets the frame, just not the effect.
	if (Post.Initialise(&Device)) {
		Post.Set_Chain(PostChain, PostChainLength);
	}

	Active = true;
	return true;
}

void Direct3D11_Release()
{
	if (!Active) {
		return;
	}
	Post.Shutdown();
	Device.Set_Scene_View(NULL);
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

static DX11BufferTwinClass * count_twin(DX11BufferTwinClass * twin, unsigned byte_count)
{
	if (twin != NULL) {
		TwinBuffers++;
		TwinBytes += byte_count;
	}
	return twin;
}

DX11BufferTwinClass * Direct3D11_Twin_Vertex_Buffer(unsigned byte_count, bool dynamic)
{
	if (!Active) {
		return NULL;
	}
	return count_twin(
		DX11Twin_Create_Vertex_Buffer(Device.Get_Device(), Device.Get_Context(), byte_count,
			dynamic),
		byte_count);
}

DX11BufferTwinClass * Direct3D11_Twin_Index_Buffer(unsigned byte_count, bool dynamic)
{
	if (!Active) {
		return NULL;
	}
	return count_twin(
		DX11Twin_Create_Index_Buffer(Device.Get_Device(), Device.Get_Context(), byte_count,
			dynamic),
		byte_count);
}

void Direct3D11_Mark_Surface_Dirty(struct IDirect3DSurface9 * surface)
{
	if (!Active || surface == NULL) {
		return;
	}
	DX11Texture_Mark_Dirty(surface);
}

void Direct3D11_Mirror_Texture(unsigned stage, struct IDirect3DBaseTexture9 * texture)
{
	if (!Active) {
		return;
	}
	ID3D11ShaderResourceView * view = texture == NULL
		? NULL
		: DX11Texture_Mirror(Device.Get_Device(), Device.Get_Context(), texture);
	Backend.Set_Texture(stage, view);
	Backend.Set_Texture_Missing(stage, texture != NULL && view == NULL);
}

void Direct3D11_Mirror_Render_Target(struct IDirect3DSurface9 * surface)
{
	if (!Active) {
		return;
	}
	Backend.Set_Render_Target(DX11Texture_Target(Device.Get_Device(), Device.Get_Context(),
		surface));
}

void Direct3D11_Mirror_Surface_Copy(struct IDirect3DSurface9 * destination,
	struct IDirect3DSurface9 * source, const struct tagRECT * source_rectangle,
	const struct tagPOINT * destination_point)
{
	if (!Active) {
		return;
	}
	DX11Texture_Update(Device.Get_Device(), Device.Get_Context(), destination, source,
		source_rectangle, destination_point);
}

void Direct3D11_Texture_Statistics(unsigned & textures_made, unsigned & textures_reused,
	unsigned & textures_refused)
{
	DX11Texture_Statistics(textures_made, textures_reused, textures_refused);
}

const char * Direct3D11_Texture_First_Refusal()
{
	return DX11Texture_First_Refusal();
}

unsigned Direct3D11_Texture_Note_Count()
{
	return DX11Texture_Note_Count();
}

const char * Direct3D11_Texture_Note(unsigned index)
{
	return DX11Texture_Note(index);
}

unsigned Direct3D11_Texture_Copy_Shape_Count()
{
	return DX11Texture_Copy_Shape_Count();
}

const char * Direct3D11_Texture_Copy_Shape(unsigned index)
{
	return DX11Texture_Copy_Shape(index);
}

void Direct3D11_Dump_Programs_To(const char * directory)
{
	DumpDirectory = directory == NULL ? "" : directory;
	if (Active) {
		Backend.Set_Dump_Directory(DumpDirectory.c_str());
	}
}

void Direct3D11_Present_Enable(bool enabled)
{
	PresentRequested = enabled;
}

void Direct3D11_Set_VSync(bool enabled)
{
	VSyncRequested = enabled;
}

bool Direct3D11_Present_Is_Enabled()
{
	return PresentRequested && Active;
}

bool Direct3D11_Post_Chain(const char * chain)
{
	const bool understood = DX11Post_Parse_Chain(chain, PostChain, PostChainLength);
	if (Active) {
		Post.Set_Chain(PostChain, PostChainLength);
	}
	return understood;
}

const char * Direct3D11_Post_Diagnostic()
{
	return Active ? Post.Diagnostic_Line() : "post-process off";
}

// Whatever put the frame in front of the player, the swap chain is where everything after it has to
// be drawn.  Rebinding through the backend rather than by hand is what gets the depth buffer and the
// scene's own viewport offset back at the same time.
static void take_the_frame_to_the_screen()
{
	Device.Set_Scene_View(NULL);
	Backend.Begin_Scene();
}

void Direct3D11_Finish_Scene()
{
	if (Active && Post.Run_Chain()) {
		take_the_frame_to_the_screen();
	}
}

void Direct3D11_Finish_Frame()
{
	if (Active && Post.Copy_Through()) {
		take_the_frame_to_the_screen();
	}
}

void Direct3D11_Begin_Scene()
{
	if (Active) {
		// Before the backend binds anything: the scene goes wherever this says it goes, and a frame
		// that asked for a chain and could not have one falls back to the swap chain here rather
		// than half way through.
		Post.Begin_Frame();
		Device.Set_Scene_View(Post.Scene_View());
		Backend.Begin_Scene();
	}
}

void Direct3D11_Mirror_Clear(bool colour, bool depth, float red, float green, float blue,
	float alpha)
{
	if (Active) {
		const float value[4] = { red, green, blue, alpha };
		Backend.Clear(colour, depth, value);
	}
}

unsigned char * Direct3D11_Capture_Back_Buffer(unsigned & width, unsigned & height,
	unsigned & pitch)
{
	width = 0;
	height = 0;
	pitch = 0;
	if (!Active || Device.Get_Back_Buffer_View() == NULL) {
		return NULL;
	}

	// A screenshot is of what the player sees, so the chain runs first.  The present path asks for
	// the same thing a moment later and gets it for nothing, because the second Resolve of a frame
	// does not run.
	Direct3D11_Finish_Frame();

	ID3D11Resource * back_buffer = NULL;
	Device.Get_Back_Buffer_View()->GetResource(&back_buffer);

	D3D11_TEXTURE2D_DESC description;
	((ID3D11Texture2D *)back_buffer)->GetDesc(&description);
	description.Usage = D3D11_USAGE_STAGING;
	description.BindFlags = 0;
	description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	description.MiscFlags = 0;
	// A multisampled back buffer cannot be copied into a single-sample staging texture, and this
	// swap chain never asks for one, so the copy below is the whole of it.
	description.SampleDesc.Count = 1;
	description.SampleDesc.Quality = 0;

	ID3D11Texture2D * staging = NULL;
	if (FAILED(Device.Get_Device()->CreateTexture2D(&description, NULL, &staging))) {
		back_buffer->Release();
		return NULL;
	}

	Device.Get_Context()->CopyResource(staging, back_buffer);

	D3D11_MAPPED_SUBRESOURCE mapped;
	unsigned char * pixels = NULL;
	if (SUCCEEDED(Device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
		width = description.Width;
		height = description.Height;
		pitch = description.Width * 4;
		pixels = new unsigned char[pitch * height];
		for (unsigned row = 0; row < height; ++row) {
			memcpy(pixels + row * pitch,
				(const unsigned char *)mapped.pData + row * mapped.RowPitch, pitch);
		}
		Device.Get_Context()->Unmap(staging, 0);
	}

	staging->Release();
	back_buffer->Release();
	return pixels;
}

void Direct3D11_Release_Capture(unsigned char * pixels)
{
	delete [] pixels;
}

void Direct3D11_End_Scene(bool flip_frames)
{
	if (Active && flip_frames && PresentRequested) {
		Direct3D11_Finish_Frame();
		Device.Present(VSyncRequested ? 1 : 0);
	}
}

void Direct3D11_Mirror_Stream_Source(DX11BufferTwinClass * twin, unsigned stride, unsigned offset)
{
	if (Active) {
		Backend.Set_Stream_Source(twin ? twin->Buffer() : NULL, stride, offset);
	}
}

void Direct3D11_Mirror_Indices(DX11BufferTwinClass * twin)
{
	if (Active) {
		Backend.Set_Indices(twin ? twin->Buffer() : NULL, DXGI_FORMAT_R16_UINT);
	}
}

void Direct3D11_Mirror_Vertex_Format(unsigned fvf)
{
	if (Active) {
		Backend.Set_Vertex_Format(fvf);
	}
}

void Direct3D11_Mirror_Pixel_Shader(const void * shader)
{
	if (Active) {
		const EngineShaderRecord & record = engine_shader_record(shader);
		Backend.Set_Pixel_Program(record.Program, shader != NULL, record.FileName.c_str());
	}
}

void Direct3D11_Register_Engine_Shader(const void * shader, const char * file_path)
{
	if (shader == NULL || file_path == NULL) {
		return;
	}

	// The whole path, not the file name alone, so a report line reads the way the loader's own
	// argument does and can be grepped for as it stands.
	EngineShaderRecord record;
	record.Program = EngineShader_From_File(file_path);
	record.FileName = file_path;
	EngineShaders[shader] = record;
}

void Direct3D11_Mirror_Vertex_Shader(const void * shader)
{
	if (Active) {
		const EngineShaderRecord & record = engine_shader_record(shader);
		Backend.Set_Vertex_Program(record.Program, shader != NULL, record.FileName.c_str());
	}
}

void Direct3D11_Mirror_Vertex_Shader_Constant(unsigned first_register, const float * values,
	unsigned count)
{
	if (Active) {
		Backend.Set_Vertex_Program_Constant(first_register, values, count);
	}
}

bool Direct3D11_Draw_Indexed_Triangles(unsigned index_count, unsigned start_index,
	unsigned base_vertex)
{
	if (!Active) {
		return false;
	}
	return Backend.Draw_Indexed_Triangles(index_count, start_index, base_vertex);
}

bool Direct3D11_Draw_Indexed_Strip(unsigned index_count, unsigned start_index,
	unsigned base_vertex)
{
	if (!Active) {
		return false;
	}
	return Backend.Draw_Indexed_Strip(index_count, start_index, base_vertex);
}

bool Direct3D11_Draw_User_Strip(const void * vertices, unsigned primitive_count, unsigned stride)
{
	if (!Active) {
		return false;
	}
	return Backend.Draw_User_Strip(vertices, primitive_count, stride);
}

void Direct3D11_Twin_Statistics(unsigned & buffers_made, unsigned long long & bytes_made)
{
	buffers_made = TwinBuffers;
	bytes_made = TwinBytes;
}

void Direct3D11_Refusals(unsigned long long & no_buffer, unsigned long long & no_stage,
	unsigned long long & no_layout, unsigned long long & no_program,
	unsigned long long & no_object, unsigned long long & foreign_shader,
	unsigned long long & no_texture)
{
	no_buffer = 0;
	no_stage = 0;
	no_layout = 0;
	no_program = 0;
	no_object = 0;
	foreign_shader = 0;
	no_texture = 0;
	if (Active) {
		Backend.Refusals(no_buffer, no_stage, no_layout, no_program, no_object, foreign_shader,
			no_texture);
	}
}

unsigned Direct3D11_Refused_Description_Count()
{
	return Active ? Backend.Refused_Description_Count() : 0;
}

const char * Direct3D11_Refused_Description(unsigned index)
{
	return Active ? Backend.Refused_Description(index) : "";
}

void Direct3D11_Target_Statistics(unsigned long long & bound, unsigned long long & restored,
	unsigned long long & draws)
{
	bound = 0;
	restored = 0;
	draws = 0;
	if (Active) {
		Backend.Target_Statistics(bound, restored, draws);
	}
}

const char * Direct3D11_Diagnostic()
{
	return Active ? Backend.Diagnostic_Line() : "";
}

unsigned Direct3D11_Target_Trace_Count()
{
	return Active ? Backend.Target_Trace_Count() : 0;
}

const char * Direct3D11_Target_Trace(unsigned index)
{
	return Active ? Backend.Target_Trace(index) : "";
}

unsigned Direct3D11_Pipeline_Report_Count()
{
	return Active ? Backend.Pipeline_Report_Count() : 0;
}

const char * Direct3D11_Pipeline_Report(unsigned index)
{
	return Active ? Backend.Pipeline_Report(index) : "";
}

unsigned Direct3D11_Foreign_Report_Count()
{
	return Active ? Backend.Foreign_Report_Count() : 0;
}

const char * Direct3D11_Foreign_Report(unsigned index)
{
	return Active ? Backend.Foreign_Report(index) : "";
}

const char * Direct3D11_First_Compiler_Error()
{
	return Active ? Backend.First_Compiler_Error() : "";
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

void Direct3D11_Take_Frame_Cost(double & pipeline_milliseconds, unsigned & pipelines,
	double & texture_milliseconds, unsigned & textures)
{
	pipeline_milliseconds = 0.0;
	pipelines = 0;
	if (Active) {
		Backend.Take_Frame_Build_Cost(pipeline_milliseconds, pipelines);
	}
	DX11Texture_Take_Frame_Cost(texture_milliseconds, textures);
}
