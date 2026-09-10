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
**
**	The assembly repairs below are taken from crosire's d3d8to9
**	(https://github.com/crosire/d3d8to9, BSD 3-clause), vendored in this tree at
**	Libraries/Source/d3d8to9, with its proxy-object plumbing removed.
*/

#include "d3d8shadertranslate.h"
#include "d3dx9runtime.h"

#include <regex>
#include <string>

// The register file a vs_1_1 shader can name, which bounds both the initialisation pass
// and the search for a spare register when an m*x* has to be unaliased.
static const size_t TEMPORARY_REGISTER_COUNT = 12;
static const size_t TEXTURE_OUTPUT_REGISTER_COUNT = 8;
static const size_t COLOUR_OUTPUT_REGISTER_COUNT = 2;
// vs_1_1's instruction budget.  A repair that would push the shader past it is dropped
// rather than making the shader fail to assemble.
static const size_t MAX_VERTEX_INSTRUCTIONS = 128;
static const size_t MAX_DECLARATION_ELEMENTS = 32;

// The D3D8 data type of a declaration token, and the number of bytes it advances the
// stream offset by.
struct DeclarationType
{
	BYTE Type;
	BYTE Size;
};

static const DeclarationType DECLARATION_TYPES[] =
{
	{ D3DDECLTYPE_FLOAT1,   4 },
	{ D3DDECLTYPE_FLOAT2,   8 },
	{ D3DDECLTYPE_FLOAT3,  12 },
	{ D3DDECLTYPE_FLOAT4,  16 },
	{ D3DDECLTYPE_D3DCOLOR, 4 },
	{ D3DDECLTYPE_UBYTE4,   4 },
	{ D3DDECLTYPE_SHORT2,   4 },
	{ D3DDECLTYPE_SHORT4,   8 }
};

// D3D8 addressed vertex inputs by a fixed register number whose meaning was baked into
// the API; D3D9 names a usage and an index instead.  This is that table.
struct DeclarationUsage
{
	BYTE Usage;
	BYTE UsageIndex;
};

static const DeclarationUsage DECLARATION_USAGES[] =
{
	{ D3DDECLUSAGE_POSITION,     0 },
	{ D3DDECLUSAGE_BLENDWEIGHT,  0 },
	{ D3DDECLUSAGE_BLENDINDICES, 0 },
	{ D3DDECLUSAGE_NORMAL,       0 },
	{ D3DDECLUSAGE_PSIZE,        0 },
	{ D3DDECLUSAGE_COLOR,        0 },
	{ D3DDECLUSAGE_COLOR,        1 },
	{ D3DDECLUSAGE_TEXCOORD,     0 },
	{ D3DDECLUSAGE_TEXCOORD,     1 },
	{ D3DDECLUSAGE_TEXCOORD,     2 },
	{ D3DDECLUSAGE_TEXCOORD,     3 },
	{ D3DDECLUSAGE_TEXCOORD,     4 },
	{ D3DDECLUSAGE_TEXCOORD,     5 },
	{ D3DDECLUSAGE_TEXCOORD,     6 },
	{ D3DDECLUSAGE_TEXCOORD,     7 },
	{ D3DDECLUSAGE_POSITION,     1 },
	{ D3DDECLUSAGE_NORMAL,       1 }
};

static const size_t DECLARATION_TYPE_COUNT = sizeof(DECLARATION_TYPES)/sizeof(DECLARATION_TYPES[0]);
static const size_t DECLARATION_USAGE_COUNT = sizeof(DECLARATION_USAGES)/sizeof(DECLARATION_USAGES[0]);

// D3DXDisassembleShader's output carries the shader's own name and comment bytes, which
// can be anything at all; the assembler will not take a control character.
static std::string readable_disassembly(ID3DXBuffer * disassembly)
{
	const char * const raw = static_cast<const char *>(disassembly->GetBufferPointer());
	const size_t size = disassembly->GetBufferSize();

	std::string text;
	text.reserve(size);
	for (size_t index = 0; index < size; ++index) {
		const unsigned char character = static_cast<unsigned char>(raw[index]);
		const bool is_printable = character == '\t' || character == '\n' || character == '\r'
			|| (character >= ' ' && character <= '~');
		if (is_printable) {
			text.push_back(static_cast<char>(character));
		}
	}
	return text;
}

static const char * usage_declaration_keyword(BYTE usage)
{
	switch (usage) {
	case D3DDECLUSAGE_POSITION:     return "dcl_position";
	case D3DDECLUSAGE_BLENDWEIGHT:  return "dcl_blendweight";
	case D3DDECLUSAGE_BLENDINDICES: return "dcl_blendindices";
	case D3DDECLUSAGE_NORMAL:       return "dcl_normal";
	case D3DDECLUSAGE_PSIZE:        return "dcl_psize";
	case D3DDECLUSAGE_COLOR:        return "dcl_color";
	case D3DDECLUSAGE_TEXCOORD:     return "dcl_texcoord";
	default:                        return NULL;
	}
}

// How many arithmetic instructions the disassembly says it used, which bounds how many
// repair instructions can still be inserted.
static size_t instruction_count_of(const std::string & source, const char * counter_word,
	size_t digit_count, size_t default_count)
{
	const size_t position = source.find(counter_word);
	if (position <= digit_count || position >= source.size()) {
		return default_count;
	}
	return strtoul(source.substr(position - digit_count - 1, digit_count).c_str(), NULL, 10);
}

HRESULT Create_Translated_Pixel_Shader(IDirect3DDevice9 * device, const DWORD * function,
	IDirect3DPixelShader9 ** shader, std::string * translated_source)
{
	if (device == NULL || function == NULL || shader == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*shader = NULL;

	if (D3DXDisassembleShader == NULL || D3DXAssembleShader == NULL) {
		return D3DERR_INVALIDCALL;
	}
	if (*function < D3DPS_VERSION(1, 0) || *function > D3DPS_VERSION(1, 4)) {
		return D3DERR_INVALIDCALL;
	}

	ID3DXBuffer * disassembly = NULL;
	HRESULT result = D3DXDisassembleShader(function, FALSE, NULL, &disassembly);
	if (FAILED(result)) {
		return result;
	}

	std::string source = readable_disassembly(disassembly);
	disassembly->Release();

	const size_t version_position = source.find("ps_1_");
	if (version_position == std::string::npos) {
		return D3DERR_INVALIDCALL;
	}
	if (source.at(version_position + 5) == '0') {
		source.replace(version_position, 6, "ps_1_1");	// D3D9 dropped ps_1_0
	}

	// The disassembler writes the original D3D8 source as a comment block and #line
	// directives; neither survives reassembly.
	source = std::regex_replace(source, std::regex("    \\/\\/ ps\\.1\\.[1-4]\\n((?! ).+\\n)+"), "");
	source = std::regex_replace(source, std::regex("([^\\n]\\n)[\\s]*#line [0123456789]+.*\\n"), "$1");

	// D3D9 refuses a negation modifier on a constant register in an add; the same
	// arithmetic written as a sub is accepted.
	source = std::regex_replace(source,
		std::regex("(add)([_satxd248]*) (r[0-9][\\.wxyz]*), ((1-|)[crtv][0-9][\\.wxyz_abdis2]*), (-)(c[0-9][\\.wxyz]*)(_bx2|_bias|_x2|_d[zbwa]|)(?![_\\.wxyz])"),
		"sub$2 $3, $4, $7$8");

	if (translated_source != NULL) {
		*translated_source = source;
	}

	ID3DXBuffer * assembly = NULL;
	ID3DXBuffer * errors = NULL;
	result = D3DXAssembleShader(source.data(), static_cast<UINT>(source.size()), NULL, NULL, 0,
		&assembly, &errors);
	if (errors != NULL) {
		errors->Release();
	}
	if (FAILED(result)) {
		return result;
	}

	result = device->CreatePixelShader(static_cast<const DWORD *>(assembly->GetBufferPointer()), shader);
	assembly->Release();
	return result;
}

HRESULT Create_Translated_Vertex_Shader(IDirect3DDevice9 * device, const DWORD * d3d8_declaration,
	const DWORD * function, IDirect3DVertexShader9 ** shader,
	IDirect3DVertexDeclaration9 ** vertex_declaration, std::string * translated_source)
{
	if (device == NULL || d3d8_declaration == NULL || function == NULL
		|| shader == NULL || vertex_declaration == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*shader = NULL;
	*vertex_declaration = NULL;

	if (D3DXDisassembleShader == NULL || D3DXAssembleShader == NULL) {
		return D3DERR_INVALIDCALL;
	}
	if (*function < D3DVS_VERSION(1, 0) || *function > D3DVS_VERSION(1, 1)) {
		return D3DERR_INVALIDCALL;
	}

	// Decode the D3D8 declaration into D3D9 elements, remembering which vN register each
	// element feeds so the dcl_ lines can name it.
	D3DVERTEXELEMENT9 elements[MAX_DECLARATION_ELEMENTS];
	DWORD input_registers[MAX_DECLARATION_ELEMENTS];
	size_t element_count = 0;
	WORD stream = 0;
	WORD offset = 0;

	for (const DWORD * token = d3d8_declaration; *token != D3DVSD_END(); ++token) {
		if (element_count + 1 >= MAX_DECLARATION_ELEMENTS) {
			return D3DERR_INVALIDCALL;
		}

		const DWORD token_type = (*token & D3DVSD_TOKENTYPEMASK) >> D3DVSD_TOKENTYPESHIFT;
		if (token_type == D3DVSD_TOKEN_STREAM) {
			stream = static_cast<WORD>((*token & D3DVSD_STREAMNUMBERMASK) >> D3DVSD_STREAMNUMBERSHIFT);
			offset = 0;
		}
		else if (token_type == D3DVSD_TOKEN_STREAMDATA && (*token & 0x10000000) != 0) {
			offset = static_cast<WORD>(offset
				+ ((*token & D3DVSD_SKIPCOUNTMASK) >> D3DVSD_SKIPCOUNTSHIFT) * sizeof(DWORD));
		}
		else if (token_type == D3DVSD_TOKEN_STREAMDATA) {
			const DWORD type = (*token & D3DVSD_DATATYPEMASK) >> D3DVSD_DATATYPESHIFT;
			const DWORD address = (*token & D3DVSD_VERTEXREGMASK) >> D3DVSD_VERTEXREGSHIFT;
			if (type >= DECLARATION_TYPE_COUNT || address >= DECLARATION_USAGE_COUNT) {
				return D3DERR_INVALIDCALL;
			}

			elements[element_count].Stream = stream;
			elements[element_count].Offset = offset;
			elements[element_count].Type = DECLARATION_TYPES[type].Type;
			elements[element_count].Method = D3DDECLMETHOD_DEFAULT;
			elements[element_count].Usage = DECLARATION_USAGES[address].Usage;
			elements[element_count].UsageIndex = DECLARATION_USAGES[address].UsageIndex;
			offset = static_cast<WORD>(offset + DECLARATION_TYPES[type].Size);

			input_registers[element_count] = address;
			++element_count;
		}
		else {
			// Tessellator and constant-memory tokens; no shader this game ships uses one.
			return D3DERR_INVALIDCALL;
		}
	}

	const D3DVERTEXELEMENT9 terminator = D3DDECL_END();
	elements[element_count] = terminator;

	ID3DXBuffer * disassembly = NULL;
	HRESULT result = D3DXDisassembleShader(function, FALSE, NULL, &disassembly);
	if (FAILED(result)) {
		return result;
	}

	std::string source = readable_disassembly(disassembly);
	disassembly->Release();

	const size_t version_position = source.find("vs_1_");
	if (version_position == std::string::npos) {
		return D3DERR_INVALIDCALL;
	}
	if (source.at(version_position + 5) == '0') {
		source.replace(version_position, 6, "vs_1_1");	// D3D9 dropped vs_1_0
	}

	// D3D9 wants the inputs declared inside the shader.
	size_t insert_position = version_position + 7;
	for (size_t index = 0; index < element_count; ++index) {
		const char * const keyword = usage_declaration_keyword(elements[index].Usage);
		if (keyword == NULL) {
			return D3DERR_INVALIDCALL;
		}

		std::string declaration_line = "    ";
		declaration_line += keyword;
		if (elements[index].UsageIndex > 0) {
			declaration_line += std::to_string(elements[index].UsageIndex);
		}
		declaration_line += " v" + std::to_string(input_registers[index]) + '\n';

		source.insert(insert_position, declaration_line);
		insert_position += declaration_line.length();
	}

	size_t instruction_count = instruction_count_of(source, "instruction", 3, 0);

	// D3D9 refuses to read a register that was never written.  Writing c0 into every
	// register the shader mentions costs an instruction each and changes nothing the
	// shader goes on to compute, because anything it reads it also writes first.
	for (size_t index = 0; index < TEXTURE_OUTPUT_REGISTER_COUNT; ++index) {
		const std::string name = "oT" + std::to_string(index);
		if (source.find(name) != std::string::npos && instruction_count < MAX_VERTEX_INSTRUCTIONS) {
			++instruction_count;
			source.insert(insert_position, "    mov " + name + ", c0\n");
		}
	}
	for (size_t index = 0; index < COLOUR_OUTPUT_REGISTER_COUNT; ++index) {
		const std::string name = "oD" + std::to_string(index);
		if (source.find(name) != std::string::npos && instruction_count < MAX_VERTEX_INSTRUCTIONS) {
			++instruction_count;
			source.insert(insert_position, "    mov " + name + ", c0\n");
		}
	}
	for (size_t index = 0; index < TEMPORARY_REGISTER_COUNT; ++index) {
		const std::string name = "r" + std::to_string(index);
		if (source.find(name) != std::string::npos && instruction_count < MAX_VERTEX_INSTRUCTIONS) {
			++instruction_count;
			source.insert(insert_position, "    mov " + name + ", c0\n");
		}
	}

	source = std::regex_replace(source, std::regex("    \\/\\/ vs\\.1\\.1\\n((?! ).+\\n)+"), "");
	source = std::regex_replace(source, std::regex("([^\\n]\\n)[\\s]*#line [0123456789]+.*\\n"), "$1");

	// oFog and oPts take one component in D3D9 and took four in D3D8.
	source = std::regex_replace(source, std::regex("(oFog|oPts)\\.x"), "$1");
	source = std::regex_replace(source,
		std::regex("(add|sub|mul|min|max) (oFog|oPts), ([cr][0-9]+), (.+)\\n"), "$1 $2, $3.x, $4\n");
	source = std::regex_replace(source,
		std::regex("(add|sub|mul|min|max) (oFog|oPts), (.+), ([cr][0-9]+)\\n"), "$1 $2, $3, $4.x\n");
	source = std::regex_replace(source,
		std::regex("(mov|mad) (oFog|oPts)(.*), (-?)([crv][0-9]+(?![\\.0-9]))"), "$1 $2$3, $4$5.x");

	// An m3x3 or m4x4 may not read its own destination as its first source.  Copy that
	// source into a register the shader does not use, and read it from there.
	if (std::regex_search(source, std::regex("m.x."))) {
		size_t spare_register = 0;
		while (spare_register < TEMPORARY_REGISTER_COUNT
			&& source.find("r" + std::to_string(spare_register)) != std::string::npos) {
			++spare_register;
		}

		for (size_t index = 0; index < TEMPORARY_REGISTER_COUNT; ++index) {
			const std::string aliased =
				"(m.x.) (r" + std::to_string(index) + "), ((-?)r" + std::to_string(index) + "([\\.xyzw]*))(?![0-9])";

			while (std::regex_search(source, std::regex(aliased))) {
				if (spare_register < TEMPORARY_REGISTER_COUNT && instruction_count < MAX_VERTEX_INSTRUCTIONS) {
					++instruction_count;
					source = std::regex_replace(source, std::regex(aliased),
						"mov r" + std::to_string(spare_register) + ", $2\n    $1 $2, $4r"
							+ std::to_string(spare_register) + "$5",
						std::regex_constants::format_first_only);
				}
				else {
					// Nowhere to put the copy.  Comment the instruction out rather than
					// leave an assembly the assembler will reject outright.
					source = std::regex_replace(source, std::regex("(.*" + aliased + ".*)"), "/*$1*/");
					break;
				}
			}
		}
	}

	// oPos must be written in full.
	if (std::regex_search(source, std::regex("    ([a-z2-4]*) oPos\\."))
		&& !std::regex_search(source, std::regex("    ([a-z2-4]*) oPos,"))) {
		const bool writes_x = std::regex_search(source, std::regex("    ([a-z2-4]*) oPos\\.[y|z|w]*x"));
		const bool writes_y = std::regex_search(source, std::regex("    ([a-z2-4]*) oPos\\.[x|z|w]*y"));
		const bool writes_z = std::regex_search(source, std::regex("    ([a-z2-4]*) oPos\\.[x|y|w]*z"));
		const bool writes_w = std::regex_search(source, std::regex("    ([a-z2-4]*) oPos\\.[x|y|z]*w"));
		if (!writes_x || !writes_y || !writes_z || !writes_w) {
			source = std::regex_replace(source,
				std::regex("    ([a-z2-4]*) (oPos\\.[x|y|z|w]*,) ([^\\n]*)\\n"), "    $1 oPos, $3\n");
		}
	}

	if (translated_source != NULL) {
		*translated_source = source;
	}

	ID3DXBuffer * assembly = NULL;
	ID3DXBuffer * errors = NULL;
	result = D3DXAssembleShader(source.data(), static_cast<UINT>(source.size()), NULL, NULL, 0,
		&assembly, &errors);
	if (errors != NULL) {
		errors->Release();
	}
	if (FAILED(result)) {
		return result;
	}

	result = device->CreateVertexShader(static_cast<const DWORD *>(assembly->GetBufferPointer()), shader);
	assembly->Release();
	if (FAILED(result)) {
		return result;
	}

	result = device->CreateVertexDeclaration(elements, vertex_declaration);
	if (FAILED(result)) {
		(*shader)->Release();
		*shader = NULL;
	}
	return result;
}
