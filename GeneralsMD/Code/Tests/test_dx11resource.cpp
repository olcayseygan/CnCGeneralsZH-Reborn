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

// The buffers and textures, translated and then actually created.
//
// Half of this is a table and is checked as one.  The other half is D3D11 refusing combinations
// that look reasonable - a dynamic buffer with no CPU write access, a staging texture bound as a
// shader resource, a render target in anything but the default pool - and the only way to know the
// translation produces legal ones is to hand them to a device and see.

#include "test_harness.h"

#include "dx11device.h"
#include "dx11resource.h"

static const unsigned BUFFER_BYTES = 4096;
static const unsigned TEXTURE_SIZE = 32;
static const unsigned ONE_MIP_LEVEL = 1;

TEST(dx11resource_a_d3d9_colour_format_reverses_into_its_dxgi_name)
{
	// D3D9 names the channels most significant byte first, DXGI least significant first, so the
	// same four bytes are an A8R8G8B8 in one and a B8G8R8A8 in the other.
	CHECK_EQ(DX11Resource_Translate_Format(D3DFMT_A8R8G8B8), DXGI_FORMAT_B8G8R8A8_UNORM);
	CHECK_EQ(DX11Resource_Translate_Format(D3DFMT_X8R8G8B8), DXGI_FORMAT_B8G8R8A8_UNORM);
	CHECK_EQ(DX11Resource_Translate_Format(D3DFMT_R5G6B5), DXGI_FORMAT_B5G6R5_UNORM);
	CHECK_EQ(DX11Resource_Translate_Format(D3DFMT_A1R5G5B5), DXGI_FORMAT_B5G5R5A1_UNORM);
	CHECK_EQ(DX11Resource_Translate_Format(D3DFMT_A4R4G4B4), DXGI_FORMAT_B4G4R4A4_UNORM);
	CHECK_EQ(DX11Resource_Translate_Format(D3DFMT_A8), DXGI_FORMAT_A8_UNORM);
}

// DXT2 and DXT4 are DXT3 and DXT5 with the colour already multiplied by the alpha.  That is a
// property of the bytes, not of the layout, so they share a DXGI name with the format they
// premultiply and nothing about the blocks changes.
TEST(dx11resource_the_compressed_formats_map_onto_four_block_types)
{
	CHECK_EQ(DX11Resource_Translate_Format(D3DFMT_DXT1), DXGI_FORMAT_BC1_UNORM);
	CHECK_EQ(DX11Resource_Translate_Format(D3DFMT_DXT2), DXGI_FORMAT_BC2_UNORM);
	CHECK_EQ(DX11Resource_Translate_Format(D3DFMT_DXT3), DXGI_FORMAT_BC2_UNORM);
	CHECK_EQ(DX11Resource_Translate_Format(D3DFMT_DXT4), DXGI_FORMAT_BC3_UNORM);
	CHECK_EQ(DX11Resource_Translate_Format(D3DFMT_DXT5), DXGI_FORMAT_BC3_UNORM);
}

// D3D11 has no palette.  A palettised texture has to be expanded before it gets here, and saying
// so is better than handing back a format that would draw the indices as shades of grey.
TEST(dx11resource_a_palettised_format_has_no_translation)
{
	CHECK_EQ(DX11Resource_Translate_Format(D3DFMT_P8), DXGI_FORMAT_UNKNOWN);
	CHECK_EQ(DX11Resource_Translate_Format(D3DFMT_A8P8), DXGI_FORMAT_UNKNOWN);
}

// Two translations lose something the caller has to know about: an X format gains an alpha channel
// that used to be ignored, and a luminance texture arrives with its one channel in red.
TEST(dx11resource_the_lossy_translations_say_so)
{
	CHECK(DX11Resource_Format_Needs_Swizzle(D3DFMT_X8R8G8B8));
	CHECK(DX11Resource_Format_Needs_Swizzle(D3DFMT_L8));
	CHECK_EQ(DX11Resource_Translate_Format(D3DFMT_L8), DXGI_FORMAT_R8_UNORM);

	CHECK(!DX11Resource_Format_Needs_Swizzle(D3DFMT_A8R8G8B8));
	CHECK(!DX11Resource_Format_Needs_Swizzle(D3DFMT_DXT5));
}

TEST(dx11resource_a_pool_and_a_usage_bit_become_a_usage_and_an_access_mask)
{
	D3D11_USAGE usage = D3D11_USAGE_DEFAULT;
	UINT cpu_access = 0;
	UINT bind = 0;

	// The managed pool is a resource the CPU fills once and the device reads from then on.
	CHECK(DX11Resource_Translate_Usage(D3DPOOL_MANAGED, 0, usage, cpu_access, bind));
	CHECK_EQ(usage, D3D11_USAGE_DEFAULT);
	CHECK_EQ(cpu_access, 0u);

	// The dynamic bit is the one the vertex buffers are refilled through every frame.
	CHECK(DX11Resource_Translate_Usage(D3DPOOL_DEFAULT, D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,
		usage, cpu_access, bind));
	CHECK_EQ(usage, D3D11_USAGE_DYNAMIC);
	CHECK_EQ(cpu_access, (UINT)D3D11_CPU_ACCESS_WRITE);

	// System memory is where a surface the CPU reads back out of lives, and staging is the only
	// D3D11 usage the CPU can both read and write.
	CHECK(DX11Resource_Translate_Usage(D3DPOOL_SYSTEMMEM, 0, usage, cpu_access, bind));
	CHECK_EQ(usage, D3D11_USAGE_STAGING);
	CHECK_EQ(cpu_access, (UINT)(D3D11_CPU_ACCESS_READ|D3D11_CPU_ACCESS_WRITE));

	// A render target is written by the device wherever D3D9 said to put it.
	CHECK(DX11Resource_Translate_Usage(D3DPOOL_DEFAULT, D3DUSAGE_RENDERTARGET,
		usage, cpu_access, bind));
	CHECK_EQ(usage, D3D11_USAGE_DEFAULT);
	CHECK_EQ(bind, (UINT)D3D11_BIND_RENDER_TARGET);

	// And it cannot live anywhere else, which D3D9 would have allowed and then ignored.
	CHECK(!DX11Resource_Translate_Usage(D3DPOOL_MANAGED, D3DUSAGE_RENDERTARGET,
		usage, cpu_access, bind));
}

// D3D9's lock flags say what the caller intends; D3D11's map types say what the resource allows,
// and the two only line up for some pairings.  D3DLOCK_DISCARD on a resource that is not dynamic
// is the one that would silently stall under D3D9.
TEST(dx11resource_a_lock_flag_is_only_a_map_type_where_the_usage_allows_it)
{
	D3D11_MAP map_type = D3D11_MAP_READ;

	CHECK(DX11Resource_Translate_Lock(D3DLOCK_DISCARD, D3D11_USAGE_DYNAMIC, map_type));
	CHECK_EQ(map_type, D3D11_MAP_WRITE_DISCARD);

	CHECK(DX11Resource_Translate_Lock(D3DLOCK_NOOVERWRITE, D3D11_USAGE_DYNAMIC, map_type));
	CHECK_EQ(map_type, D3D11_MAP_WRITE_NO_OVERWRITE);

	CHECK(DX11Resource_Translate_Lock(D3DLOCK_READONLY, D3D11_USAGE_STAGING, map_type));
	CHECK_EQ(map_type, D3D11_MAP_READ);

	CHECK(!DX11Resource_Translate_Lock(D3DLOCK_DISCARD, D3D11_USAGE_DEFAULT, map_type));
	CHECK(!DX11Resource_Translate_Lock(D3DLOCK_READONLY, D3D11_USAGE_DYNAMIC, map_type));
}

// D3D9's "no flags" means read and write on a surface that can be read and write only on one that
// cannot, so the same argument has two answers.
TEST(dx11resource_no_lock_flags_means_two_different_things)
{
	D3D11_MAP map_type = D3D11_MAP_READ;

	CHECK(DX11Resource_Translate_Lock(0, D3D11_USAGE_STAGING, map_type));
	CHECK_EQ(map_type, D3D11_MAP_READ_WRITE);

	CHECK(DX11Resource_Translate_Lock(0, D3D11_USAGE_DYNAMIC, map_type));
	CHECK_EQ(map_type, D3D11_MAP_WRITE_DISCARD);

	// A default-usage resource cannot be mapped at all.
	CHECK(!DX11Resource_Translate_Lock(0, D3D11_USAGE_DEFAULT, map_type));
}

// The half a table cannot answer: whether D3D11 accepts what the translation produced.  Each of
// these is a combination the engine asks for.
TEST(dx11resource_the_device_accepts_what_the_translation_produces)
{
	DX11DeviceClass device;
	device.Request_Debug_Layer();
	CHECK(device.Create_Offscreen());
	ID3D11Device * d3d = device.Get_Device();

	ID3D11Buffer * dynamic_vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(d3d, BUFFER_BYTES, D3DPOOL_DEFAULT,
		D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY, NULL, &dynamic_vertices));
	CHECK(dynamic_vertices != NULL);

	ID3D11Buffer * static_indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(d3d, BUFFER_BYTES, D3DPOOL_MANAGED, 0, NULL,
		&static_indices));
	CHECK(static_indices != NULL);

	ID3D11Texture2D * texture = NULL;
	ID3D11ShaderResourceView * view = NULL;
	CHECK(DX11Resource_Create_Texture(d3d, TEXTURE_SIZE, TEXTURE_SIZE, ONE_MIP_LEVEL,
		D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, 0, &texture, &view));
	CHECK(texture != NULL);

	// A texture the device can read needs the view as well.  D3D9 handed out one object for both
	// and a texture without a view here cannot be sampled however right its contents are.
	CHECK(view != NULL);

	ID3D11Texture2D * compressed = NULL;
	ID3D11ShaderResourceView * compressed_view = NULL;
	CHECK(DX11Resource_Create_Texture(d3d, TEXTURE_SIZE, TEXTURE_SIZE, ONE_MIP_LEVEL,
		D3DFMT_DXT5, D3DPOOL_MANAGED, 0, &compressed, &compressed_view));
	CHECK(compressed != NULL);
	CHECK(compressed_view != NULL);

	// A staging texture is the readback path and is bound to nothing, so it has no view.
	ID3D11Texture2D * staging = NULL;
	ID3D11ShaderResourceView * staging_view = NULL;
	CHECK(DX11Resource_Create_Texture(d3d, TEXTURE_SIZE, TEXTURE_SIZE, ONE_MIP_LEVEL,
		D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, 0, &staging, &staging_view));
	CHECK(staging != NULL);
	CHECK(staging_view == NULL);

	// A palettised texture is refused rather than created as something else.
	ID3D11Texture2D * palettised = NULL;
	ID3D11ShaderResourceView * palettised_view = NULL;
	CHECK(!DX11Resource_Create_Texture(d3d, TEXTURE_SIZE, TEXTURE_SIZE, ONE_MIP_LEVEL,
		D3DFMT_P8, D3DPOOL_MANAGED, 0, &palettised, &palettised_view));
	CHECK(palettised == NULL);

	staging->Release();
	compressed_view->Release();
	compressed->Release();
	view->Release();
	texture->Release();
	static_indices->Release();
	dynamic_vertices->Release();
}

// The thing the dynamic vertex buffers do every frame: throw the contents away and write the whole
// buffer again.  It is the one path in the engine where a wrong map type costs a stall a frame
// rather than a wrong picture, so nothing on screen would ever show it.
TEST(dx11resource_a_dynamic_buffer_takes_the_discarding_map)
{
	DX11DeviceClass device;
	device.Request_Debug_Layer();
	CHECK(device.Create_Offscreen());

	ID3D11Buffer * buffer = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(device.Get_Device(), BUFFER_BYTES, D3DPOOL_DEFAULT,
		D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY, NULL, &buffer));

	D3D11_MAP map_type = D3D11_MAP_READ;
	CHECK(DX11Resource_Translate_Lock(D3DLOCK_DISCARD, D3D11_USAGE_DYNAMIC, map_type));

	D3D11_MAPPED_SUBRESOURCE mapped;
	CHECK(SUCCEEDED(device.Get_Context()->Map(buffer, 0, map_type, 0, &mapped)));
	CHECK(mapped.pData != NULL);
	device.Get_Context()->Unmap(buffer, 0);

	buffer->Release();
}
