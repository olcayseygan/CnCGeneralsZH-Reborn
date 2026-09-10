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

// Does the generated text compile.
//
// test_ffvertex.cpp checks the shape of the program - which term is there, which register it
// reads - and every one of those checks passes on text that no compiler would accept.  A missing
// declaration, a swizzle off the end of a float3, a semantic the profile does not have: all of
// them look like correct strings.  So this hands the generator's output to the real HLSL compiler,
// both profiles, every description the game is known to ask for, and reports what it says.
//
// It compiles with D3DCompile out of d3dcompiler_47.dll, which ships with Windows.  A machine
// without it skips rather than fails: the check is worth having where it can run and it is not
// worth making the suite unrunnable somewhere else.

#include "test_harness.h"

#include "ffvertex.h"

#include <d3dcommon.h>
#include <string.h>
#include <windows.h>

typedef HRESULT (WINAPI *D3DCompileFunction)(LPCVOID source_data, SIZE_T source_size,
	LPCSTR source_name, const D3D_SHADER_MACRO * defines, ID3DInclude * include,
	LPCSTR entry_point, LPCSTR target, UINT flags1, UINT flags2, ID3DBlob ** code,
	ID3DBlob ** error_messages);

static const char * const COMPILER_MODULE = "d3dcompiler_47.dll";
static const char * const ENTRY_POINT = "main";
static const char * const D3D9_PROFILE = "vs_3_0";
static const char * const D3D11_PROFILE = "vs_4_0";

static D3DCompileFunction load_compiler()
{
	HMODULE module = LoadLibraryA(COMPILER_MODULE);
	if (module == NULL) {
		return NULL;
	}
	return reinterpret_cast<D3DCompileFunction>(GetProcAddress(module, "D3DCompile"));
}

// Compiles and, when the compiler refuses, prints what it said.  A test that only reports "did not
// compile" leaves the next person running fxc by hand to find out why.
static bool compiles(D3DCompileFunction compile, const std::string & hlsl, const char * profile)
{
	ID3DBlob * code = NULL;
	ID3DBlob * errors = NULL;
	const HRESULT result = compile(hlsl.c_str(), hlsl.size(), "ffvertex", NULL, NULL,
		ENTRY_POINT, profile, 0, 0, &code, &errors);

	if (FAILED(result) && errors != NULL) {
		printf("  %s: %s\n", profile, static_cast<const char *>(errors->GetBufferPointer()));
	}
	if (code != NULL) {
		code->Release();
	}
	if (errors != NULL) {
		errors->Release();
	}
	return SUCCEEDED(result);
}

static VertexPipelineDescription plain_description()
{
	VertexPipelineDescription description;
	memset(&description, 0, sizeof(description));
	description.FVF = D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX2|D3DFVF_DIFFUSE;
	description.ColourVertexEnabled = true;
	description.DiffuseMaterialSource = D3DMCS_COLOR1;
	description.AmbientMaterialSource = D3DMCS_MATERIAL;
	description.EmissiveMaterialSource = D3DMCS_MATERIAL;
	description.SpecularMaterialSource = D3DMCS_MATERIAL;
	description.StageCount = 1;
	description.Stages[0].TextureCoordinateIndex = D3DTSS_TCI_PASSTHRU;
	description.Stages[0].TextureTransformFlags = D3DTTFF_DISABLE;
	description.FogVertexMode = D3DFOG_NONE;
	return description;
}

// One entry per shape the game is known to produce, so a refusal names the shape that broke.
struct CompileCase
{
	const char * Name;
	bool LightingEnabled;
	bool SpecularEnabled;
	unsigned LightCount;
	DWORD LightType;
	DWORD TextureCoordinateIndex;
	DWORD TextureTransformFlags;
	bool FogEnabled;
	DWORD FogVertexMode;
};

static const CompileCase COMPILE_CASES[] = {
	{ "unlit pass-through",      false, false, 0, 0,
	  D3DTSS_TCI_PASSTHRU, D3DTTFF_DISABLE, false, D3DFOG_NONE },
	{ "one directional light",   true,  false, 1, D3DLIGHT_DIRECTIONAL,
	  D3DTSS_TCI_PASSTHRU, D3DTTFF_DISABLE, false, D3DFOG_NONE },
	{ "three directional lights",true,  false, 3, D3DLIGHT_DIRECTIONAL,
	  D3DTSS_TCI_PASSTHRU, D3DTTFF_DISABLE, false, D3DFOG_NONE },
	{ "point light",             true,  false, 1, D3DLIGHT_POINT,
	  D3DTSS_TCI_PASSTHRU, D3DTTFF_DISABLE, false, D3DFOG_NONE },
	{ "spot light",              true,  false, 1, D3DLIGHT_SPOT,
	  D3DTSS_TCI_PASSTHRU, D3DTTFF_DISABLE, false, D3DFOG_NONE },
	{ "specular highlight",      true,  true,  1, D3DLIGHT_DIRECTIONAL,
	  D3DTSS_TCI_PASSTHRU, D3DTTFF_DISABLE, false, D3DFOG_NONE },
	{ "camera space position",   false, false, 0, 0,
	  D3DTSS_TCI_CAMERASPACEPOSITION, D3DTTFF_COUNT2, false, D3DFOG_NONE },
	{ "camera space normal",     false, false, 0, 0,
	  D3DTSS_TCI_CAMERASPACENORMAL, D3DTTFF_DISABLE, false, D3DFOG_NONE },
	{ "reflection vector",       false, false, 0, 0,
	  D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR, D3DTTFF_DISABLE, false, D3DFOG_NONE },
	{ "projected transform",     false, false, 0, 0,
	  D3DTSS_TCI_CAMERASPACEPOSITION, D3DTTFF_COUNT3|D3DTTFF_PROJECTED, false, D3DFOG_NONE },
	{ "linear fog",              false, false, 0, 0,
	  D3DTSS_TCI_PASSTHRU, D3DTTFF_DISABLE, true, D3DFOG_LINEAR },
	{ "exponential fog",         false, false, 0, 0,
	  D3DTSS_TCI_PASSTHRU, D3DTTFF_DISABLE, true, D3DFOG_EXP },
	{ "squared exponential fog", false, false, 0, 0,
	  D3DTSS_TCI_PASSTHRU, D3DTTFF_DISABLE, true, D3DFOG_EXP2 }
};

static VertexPipelineDescription description_for(const CompileCase & compile_case)
{
	VertexPipelineDescription description = plain_description();
	description.LightingEnabled = compile_case.LightingEnabled;
	description.SpecularEnabled = compile_case.SpecularEnabled;
	description.LightCount = compile_case.LightCount;
	for (unsigned index = 0; index < compile_case.LightCount; ++index) {
		description.Lights[index].Type = compile_case.LightType;
	}
	description.Stages[0].TextureCoordinateIndex = compile_case.TextureCoordinateIndex;
	description.Stages[0].TextureTransformFlags = compile_case.TextureTransformFlags;
	description.FogEnabled = compile_case.FogEnabled;
	description.FogVertexMode = compile_case.FogVertexMode;
	return description;
}

TEST(ffvertexcompile_every_shape_the_game_produces_compiles_on_both_profiles)
{
	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping\n");
		return;
	}

	const size_t case_count = sizeof(COMPILE_CASES)/sizeof(COMPILE_CASES[0]);
	for (size_t index = 0; index < case_count; ++index) {
		const CompileCase & compile_case = COMPILE_CASES[index];
		const VertexPipelineDescription description = description_for(compile_case);

		std::string nine;
		std::string eleven;
		CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D9, nine));
		CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D11, eleven));

		if (!compiles(compile, nine, D3D9_PROFILE)) {
			printf("  case refused on %s: %s\n", D3D9_PROFILE, compile_case.Name);
			CHECK(false);
		}
		if (!compiles(compile, eleven, D3D11_PROFILE)) {
			printf("  case refused on %s: %s\n", D3D11_PROFILE, compile_case.Name);
			CHECK(false);
		}
	}
}

// Two stages, each generating a different way, which is the terrain's shape and the widest program
// the generator writes.
TEST(ffvertexcompile_a_two_stage_program_compiles)
{
	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping\n");
		return;
	}

	VertexPipelineDescription description = plain_description();
	description.LightingEnabled = true;
	description.LightCount = 2;
	description.Lights[0].Type = D3DLIGHT_DIRECTIONAL;
	description.Lights[1].Type = D3DLIGHT_POINT;
	description.StageCount = 2;
	description.Stages[0].TextureCoordinateIndex = D3DTSS_TCI_PASSTHRU;
	description.Stages[0].TextureTransformFlags = D3DTTFF_DISABLE;
	description.Stages[1].TextureCoordinateIndex = D3DTSS_TCI_CAMERASPACENORMAL;
	description.Stages[1].TextureTransformFlags = D3DTTFF_COUNT2;

	std::string nine;
	std::string eleven;
	CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D9, nine));
	CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D11, eleven));
	CHECK(compiles(compile, nine, D3D9_PROFILE));
	CHECK(compiles(compile, eleven, D3D11_PROFILE));
}
