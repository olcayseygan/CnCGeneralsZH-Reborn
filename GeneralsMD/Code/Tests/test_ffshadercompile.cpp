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

// Every combiner program the game asks for, compiled.
//
// The running game compiles these for ps_2_0 and refuses the draw when one will not build, so the
// D3D9 half is proved every time -ffshader runs.  ps_4_0 is not: nothing has ever handed one of
// these to the D3D11 compiler, and the two profiles disagree about more than the number after the
// underscore - sampler declarations, the COLOR output semantic, and what may be written to a
// register.  This finds that out here rather than on the day the D3D11 backend first draws.
//
// The descriptions are -ffprobe's, measured across Flash Effect, Golden Oasis, ForgottenForestZH
// and Alpine Assault.

#include "test_harness.h"

#include "ffshader.h"

#include <d3dcommon.h>
#include <string.h>
#include <windows.h>

typedef HRESULT (WINAPI *D3DCompileFunction)(LPCVOID source_data, SIZE_T source_size,
	LPCSTR source_name, const D3D_SHADER_MACRO * defines, ID3DInclude * include,
	LPCSTR entry_point, LPCSTR target, UINT flags1, UINT flags2, ID3DBlob ** code,
	ID3DBlob ** error_messages);

static const char * const COMPILER_MODULE = "d3dcompiler_47.dll";
static const char * const ENTRY_POINT = "main";
static const char * const D3D9_PROFILE = "ps_2_0";
static const char * const D3D11_PROFILE = "ps_4_0";

static D3DCompileFunction load_compiler()
{
	HMODULE module = LoadLibraryA(COMPILER_MODULE);
	if (module == NULL) {
		return NULL;
	}
	return reinterpret_cast<D3DCompileFunction>(GetProcAddress(module, "D3DCompile"));
}

static bool compiles(D3DCompileFunction compile, const std::string & hlsl, const char * profile)
{
	ID3DBlob * code = NULL;
	ID3DBlob * errors = NULL;
	const HRESULT result = compile(hlsl.c_str(), hlsl.size(), "ffshader", NULL, NULL,
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

static CombinerStage one_stage(DWORD colour_operation, DWORD colour_argument1,
	DWORD colour_argument2, DWORD alpha_operation, DWORD alpha_argument1, DWORD alpha_argument2,
	DWORD coordinate_index, bool texture_bound)
{
	CombinerStage stage;
	memset(&stage, 0, sizeof(stage));
	stage.ColourOperation = colour_operation;
	stage.ColourArgument1 = colour_argument1;
	stage.ColourArgument2 = colour_argument2;
	stage.AlphaOperation = alpha_operation;
	stage.AlphaArgument1 = alpha_argument1;
	stage.AlphaArgument2 = alpha_argument2;
	stage.TextureCoordinateIndex = coordinate_index;
	stage.TextureBound = texture_bound;
	return stage;
}

// The operations -ffprobe found, one program each, so a refusal names the operation that broke.
static const DWORD MEASURED_OPERATIONS[] = {
	D3DTOP_SELECTARG1, D3DTOP_SELECTARG2, D3DTOP_MODULATE, D3DTOP_MODULATE2X, D3DTOP_MODULATE4X,
	D3DTOP_ADD, D3DTOP_ADDSIGNED, D3DTOP_SUBTRACT, D3DTOP_DOTPRODUCT3
};

TEST(ffshadercompile_every_operation_compiles_on_both_profiles)
{
	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping\n");
		return;
	}

	const size_t operation_count = sizeof(MEASURED_OPERATIONS)/sizeof(MEASURED_OPERATIONS[0]);
	for (size_t index = 0; index < operation_count; ++index) {
		CombinerDescription description;
		description.StageCount = 1;
		description.Stages[0] = one_stage(MEASURED_OPERATIONS[index], D3DTA_TEXTURE, D3DTA_DIFFUSE,
			MEASURED_OPERATIONS[index], D3DTA_TEXTURE, D3DTA_DIFFUSE, 0, true);

		std::string hlsl;
		std::string eleven;
		CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, hlsl));
		CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, eleven));
		CHECK(compiles(compile, hlsl, D3D9_PROFILE));
		CHECK(compiles(compile, eleven, D3D11_PROFILE));
	}
}

// The two-stage shroud pass: MULTIPLYADD against an alpha-replicated texture factor, then
// DOTPRODUCT3.  It is the widest program the generator writes and the only one using either
// operation, so it is the one most likely to say something different on ps_4_0.
TEST(ffshadercompile_the_widest_measured_program_compiles)
{
	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping\n");
		return;
	}

	CombinerDescription description;
	description.StageCount = 2;
	description.Stages[0] = one_stage(D3DTOP_MULTIPLYADD, D3DTA_TEXTURE, D3DTA_DIFFUSE,
		D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT, 0, true);
	description.Stages[0].ColourArgument0 = D3DTA_TFACTOR | D3DTA_ALPHAREPLICATE;
	description.Stages[1] = one_stage(D3DTOP_DOTPRODUCT3, D3DTA_TEXTURE, D3DTA_CURRENT,
		D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT, 1, true);

	std::string hlsl;
	std::string eleven;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, hlsl));
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, eleven));
	CHECK(compiles(compile, hlsl, D3D9_PROFILE));
	CHECK(compiles(compile, eleven, D3D11_PROFILE));
}

// The alpha test and the fog exist only on the D3D11 profile, where they are shader instructions
// rather than pipeline state.  Every comparison is generated here, because a clip that will not
// compile is a pass that silently stays on the pipeline it was on.
TEST(ffshadercompile_every_alpha_comparison_and_the_fog_compile)
{
	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping\n");
		return;
	}

	static const DWORD COMPARISONS[] = {
		D3DCMP_NEVER, D3DCMP_LESS, D3DCMP_EQUAL, D3DCMP_LESSEQUAL,
		D3DCMP_GREATER, D3DCMP_NOTEQUAL, D3DCMP_GREATEREQUAL, D3DCMP_ALWAYS
	};
	const size_t comparison_count = sizeof(COMPARISONS)/sizeof(COMPARISONS[0]);

	for (size_t index = 0; index < comparison_count; ++index) {
		CombinerDescription description;
		memset(&description, 0, sizeof(description));
		description.StageCount = 1;
		description.Stages[0] = one_stage(D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE,
			D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE, 0, true);
		description.PixelPipeline.AlphaTestEnabled = true;
		description.PixelPipeline.AlphaFunction = COMPARISONS[index];
		description.PixelPipeline.FogEnabled = true;

		std::string hlsl;
		CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, hlsl));
		CHECK(compiles(compile, hlsl, D3D11_PROFILE));
	}
}

// The tree shadow pass reads the texture factor for its colour and its alpha both, and reads no
// varying but one coordinate set.  Under ps_4_0 an unread input is still a declared one.
TEST(ffshadercompile_the_tree_shadow_program_compiles)
{
	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping\n");
		return;
	}

	CombinerDescription description;
	description.StageCount = 1;
	description.Stages[0] = one_stage(D3DTOP_SELECTARG1, D3DTA_TFACTOR, D3DTA_DIFFUSE,
		D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_TFACTOR, 0, true);

	std::string hlsl;
	std::string eleven;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, hlsl));
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, eleven));
	CHECK(compiles(compile, hlsl, D3D9_PROFILE));
	CHECK(compiles(compile, eleven, D3D11_PROFILE));
}
