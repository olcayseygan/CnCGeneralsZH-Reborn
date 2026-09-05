cmake_minimum_required(VERSION 3.20)
project(GeneralsMD CXX C)

# Win32 only: the code is full of 32-bit inline __asm, which MSVC x64 rejects outright.
if(MSVC AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Configure for Win32: cmake -A Win32")
endif()

set(WW ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/WWVegas)
set(COMP ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/Compression)

add_compile_definitions(WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)

# EA's own switch for exactly this situation ("Good for startup problems", Debug.h):
# it turns DEBUG_LOG back on in a Release build so the hundreds of existing log
# calls in GameEngine::init land in Run/generals.exe-<machine>.log.  Without it a
# quiet failure in init leaves nothing behind at all - GameEngine::init's
# catch (ErrorCode) swallows everything that is not ERROR_INVALID_D3D, and WinMain's
# outer catch (...) swallows the rest, so the exe just returns 0.  Applied per target
# at the bottom of this file, not globally: WWVegas' wwdebug.h includes GameEngine's
# Debug.h and routes WWDEBUG_SAY into DEBUG_LOG, so a global define makes wwmath and
# friends call DebugLog(), which only exists in gameengine - every leaf test exe then
# fails to link.  The WW libraries' own chatter is not what we are after anyway.
option(RELEASE_DEBUG_LOGGING "Write the startup debug log in Release builds" ON)
if(MSVC)
  # /permissive- is off on purpose: this is 2003-era code and templates here rely on
  # pre-two-phase-lookup name binding (simplevec.h et al).
  # /EHa, not /EHsc: WinMain installs _set_se_translator(DumpExceptionInfo) to turn an
  # access violation into a C++ exception and write ReleaseCrashInfo.txt on the way.  A
  # translator only runs in code compiled with asynchronous EH, so under /EHsc the hook
  # is dead (MSVC says so: "warning C4535: _set_se_translator() requires /EHa") and a
  # faulting frame just kills the process with 0xC0000005 and no log at all.
  add_compile_options(/W3 /permissive /EHa)
  # There is no debugger on the porting machine, so ReleaseCrashInfo.txt's stack
  # dump is the only stack we get.  StackDump.cpp walks the EBP chain by hand and
  # /O2 implies /Oy (no frame pointer), which leaves that dump empty; /Zi + /DEBUG
  # then give the frames names instead of bare addresses.  Costs a PDB, buys Phase 5.
  # /FS goes with /Zi: MSBuild compiles a project's sources in parallel and they all write
  # the one PDB.  Without it the writes go through mspdbsrv.exe, and if that dies or is
  # killed - which happens when a build is interrupted - every following compile fails with
  # C1041, or worse, only some of them do and the library is left half-built against an old
  # header.  /FS serializes the writes in the compilers themselves instead.
  add_compile_options(/Oy- /Zi /FS)
  # /MP: build a project's own sources in parallel too, not just one project at a
  # time.  MSBuild already runs projects concurrently, but gameengine alone is ~580
  # sources in a single project and was the long pole.  /FS above is what makes this
  # safe - the PDB writes are serialized in the compilers.
  add_compile_options(/MP)
  # /arch:SSE2 is already the 32-bit default on this compiler; it is written down because
  # the simulation now depends on it.  x87 carries 80-bit intermediates whose spills round
  # a second time, and FPUControl.cpp pins the control word to 24-bit precision, which would
  # cut the double-precision argument reduction in WWMath/dettrig.cpp down to float and give
  # a different angle on a machine that spilled at a different point.  SSE2 arithmetic is
  # done at the declared width, so every machine gets the same bits.  See dettrig.h.
  add_compile_options(/arch:SSE2)
  add_link_options(/DEBUG)
endif()

# ---- wwdebug --------------------------------------------------------------
add_library(wwdebug STATIC
  ${WW}/WWDebug/wwdebug.cpp
  ${WW}/WWDebug/wwmemlog.cpp
  ${WW}/WWDebug/wwprofile.cpp)
target_include_directories(wwdebug PUBLIC ${WW}/WWDebug PRIVATE ${WW}/WWLib ${WW}/Wwutil)

# ---- wwutil ---------------------------------------------------------------
add_library(wwutil STATIC
  ${WW}/Wwutil/mathutil.cpp
  ${WW}/Wwutil/miscutil.cpp)
target_include_directories(wwutil PUBLIC ${WW}/Wwutil PRIVATE ${WW}/WWLib ${WW}/WWMath)
target_link_libraries(wwutil PRIVATE wwdebug)

# ---- wwmath ---------------------------------------------------------------
add_library(wwmath STATIC
  ${WW}/WWMath/aabox.cpp          ${WW}/WWMath/aabtreecull.cpp
  ${WW}/WWMath/cardinalspline.cpp ${WW}/WWMath/catmullromspline.cpp
  ${WW}/WWMath/colmath.cpp        ${WW}/WWMath/colmathaabox.cpp
  ${WW}/WWMath/colmathaabtri.cpp  ${WW}/WWMath/colmathfrustum.cpp
  ${WW}/WWMath/colmathline.cpp    ${WW}/WWMath/colmathobbobb.cpp
  ${WW}/WWMath/colmathobbox.cpp   ${WW}/WWMath/colmathobbtri.cpp
  ${WW}/WWMath/colmathplane.cpp   ${WW}/WWMath/colmathsphere.cpp
  ${WW}/WWMath/cullsys.cpp        ${WW}/WWMath/curve.cpp
  ${WW}/WWMath/dettrig.cpp
  ${WW}/WWMath/euler.cpp          ${WW}/WWMath/frustum.cpp
  ${WW}/WWMath/gridcull.cpp       ${WW}/WWMath/hermitespline.cpp
  ${WW}/WWMath/lineseg.cpp        ${WW}/WWMath/lookuptable.cpp
  ${WW}/WWMath/matrix3.cpp        ${WW}/WWMath/matrix3d.cpp
  ${WW}/WWMath/matrix4.cpp        ${WW}/WWMath/obbox.cpp
  ${WW}/WWMath/ode.cpp            ${WW}/WWMath/pot.cpp
  ${WW}/WWMath/quat.cpp           ${WW}/WWMath/tcbspline.cpp
  ${WW}/WWMath/tri.cpp            ${WW}/WWMath/v3_rnd.cpp
  ${WW}/WWMath/vehiclecurve.cpp   ${WW}/WWMath/vp.cpp
  ${WW}/WWMath/wwmath.cpp)
target_include_directories(wwmath PUBLIC ${WW}/WWMath PRIVATE ${WW}/WWLib ${WW}/WWSaveLoad ${WW}/Wwutil)
target_link_libraries(wwmath PRIVATE wwdebug)

# ---- compression ----------------------------------------------------------
# zlib 1.1.4 (vendored, per README) minus maketree.c - that one is a table
# generator with its own main().  Lzhl_tcp.cpp is also skipped (socket layer,
# nothing in the game calls it).
add_library(compression STATIC
  ${COMP}/CompressionManager.cpp
  ${COMP}/EAC/btreeabout.cpp ${COMP}/EAC/btreedecode.cpp ${COMP}/EAC/btreeencode.cpp
  ${COMP}/EAC/huffabout.cpp  ${COMP}/EAC/huffdecode.cpp  ${COMP}/EAC/huffencode.cpp
  ${COMP}/EAC/refabout.cpp   ${COMP}/EAC/refdecode.cpp   ${COMP}/EAC/refencode.cpp
  ${COMP}/LZHCompress/NoxCompress.cpp
  ${COMP}/LZHCompress/CompLibSource/Huff.cpp
  ${COMP}/LZHCompress/CompLibSource/Lz.cpp
  ${COMP}/LZHCompress/CompLibSource/Lzhl.cpp
  ${COMP}/ZLib/adler32.c  ${COMP}/ZLib/compress.c ${COMP}/ZLib/crc32.c
  ${COMP}/ZLib/deflate.c  ${COMP}/ZLib/gzio.c     ${COMP}/ZLib/infblock.c
  ${COMP}/ZLib/infcodes.c ${COMP}/ZLib/inffast.c  ${COMP}/ZLib/inflate.c
  ${COMP}/ZLib/inftrees.c ${COMP}/ZLib/infutil.c  ${COMP}/ZLib/trees.c
  ${COMP}/ZLib/uncompr.c  ${COMP}/ZLib/zutil.c)
target_include_directories(compression
  PUBLIC ${COMP} ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include
  PRIVATE ${COMP}/LZHCompress/CompLibHeader)
# CompressionManager calls z_compress2/z_uncompress and BaseType.h owns 'Byte',
# so zlib must be built with its Z_PREFIX renames on.
target_compile_definitions(compression PRIVATE Z_PREFIX)

# Round-trip check over every codec CompressionManager exposes.
add_executable(compression_selfcheck ${COMP}/test_compression.cpp)
target_link_libraries(compression_selfcheck PRIVATE compression)

# Guards the hand-rolled Matrix3D::Get_Inverse that replaced D3DXMatrixInverse.
add_executable(wwmath_selfcheck ${WW}/WWMath/test_inverse.cpp)
target_include_directories(wwmath_selfcheck PRIVATE ${WW}/WWLib ${WW}/WWSaveLoad ${WW}/Wwutil)
target_link_libraries(wwmath_selfcheck PRIVATE wwmath wwsaveload wwlib wwdebug)

# ---- wwlib ----------------------------------------------------------------
add_library(wwlib STATIC
  ${WW}/WWLib/_convert.cpp   ${WW}/WWLib/_mono.cpp      ${WW}/WWLib/argv.cpp
  ${WW}/WWLib/b64pipe.cpp    ${WW}/WWLib/b64straw.cpp   ${WW}/WWLib/base64.cpp
  ${WW}/WWLib/blit.cpp       ${WW}/WWLib/blowfish.cpp   ${WW}/WWLib/buff.cpp
  ${WW}/WWLib/bufffile.cpp   ${WW}/WWLib/chunkio.cpp    ${WW}/WWLib/convert.cpp
  ${WW}/WWLib/cpudetect.cpp  ${WW}/WWLib/crc.cpp        ${WW}/WWLib/cstraw.cpp
  ${WW}/WWLib/data.cpp       ${WW}/WWLib/ddraw.cpp      ${WW}/WWLib/dsurface.cpp
  ${WW}/WWLib/Except.cpp     ${WW}/WWLib/FastAllocator.cpp
  ${WW}/WWLib/ffactory.cpp   ${WW}/WWLib/fixed.cpp      ${WW}/WWLib/md5.cpp
  ${WW}/WWLib/gcd_lcm.cpp    ${WW}/WWLib/hash.cpp
  ${WW}/WWLib/hsv.cpp        ${WW}/WWLib/ini.cpp        ${WW}/WWLib/int.cpp
  ${WW}/WWLib/jshell.cpp     ${WW}/WWLib/keyboard.cpp   ${WW}/WWLib/LaunchWeb.cpp
  ${WW}/WWLib/lcw.cpp        ${WW}/WWLib/load.cpp       ${WW}/WWLib/mixfile.cpp
  ${WW}/WWLib/mono.cpp       ${WW}/WWLib/mpmath.cpp     ${WW}/WWLib/mpu.cpp
  ${WW}/WWLib/msgloop.cpp    ${WW}/WWLib/multilist.cpp  ${WW}/WWLib/mutex.cpp
  ${WW}/WWLib/nstrdup.cpp    ${WW}/WWLib/palette.cpp    ${WW}/WWLib/pcx.cpp
  ${WW}/WWLib/pipe.cpp       ${WW}/WWLib/pk.cpp         ${WW}/WWLib/ramfile.cpp
  ${WW}/WWLib/random.cpp     ${WW}/WWLib/rawfile.cpp    ${WW}/WWLib/rc4.cpp
  ${WW}/WWLib/rcfile.cpp     ${WW}/WWLib/readline.cpp   ${WW}/WWLib/realcrc.cpp
  ${WW}/WWLib/refcount.cpp   ${WW}/WWLib/registry.cpp   ${WW}/WWLib/rgb.cpp
  ${WW}/WWLib/rle.cpp        ${WW}/WWLib/rndstrng.cpp   ${WW}/WWLib/sampler.cpp
  ${WW}/WWLib/sha.cpp        ${WW}/WWLib/slnode.cpp     ${WW}/WWLib/srandom.cpp
  ${WW}/WWLib/stimer.cpp     ${WW}/WWLib/straw.cpp      ${WW}/WWLib/strtok_r.cpp
  ${WW}/WWLib/systimer.cpp   ${WW}/WWLib/tagblock.cpp   ${WW}/WWLib/TARGA.CPP
  ${WW}/WWLib/textfile.cpp   ${WW}/WWLib/tgatodxt.cpp   ${WW}/WWLib/thread.cpp
  ${WW}/WWLib/trim.cpp       ${WW}/WWLib/vector.cpp     ${WW}/WWLib/verchk.cpp
  ${WW}/WWLib/widestring.cpp ${WW}/WWLib/win.cpp        ${WW}/WWLib/WWCOMUtil.cpp
  ${WW}/WWLib/wwfile.cpp     ${WW}/WWLib/wwfont.cpp     ${WW}/WWLib/wwstring.cpp
  ${WW}/WWLib/xpipe.cpp      ${WW}/WWLib/xstraw.cpp     ${WW}/WWLib/xsurface.cpp)
target_include_directories(wwlib PUBLIC ${WW}/WWLib PRIVATE ${WW}/WWMath ${WW}/Wwutil)
# winmm: timeGetTime/timeBeginPeriod.  version: verchk.cpp's GetFileVersionInfo,
# which Wwutil drags in for Get_File_Id_String.
target_link_libraries(wwlib PRIVATE wwdebug PUBLIC winmm version)

# ---- wwsaveload -----------------------------------------------------------
add_library(wwsaveload STATIC
  ${WW}/WWSaveLoad/definition.cpp          ${WW}/WWSaveLoad/definitionfactory.cpp
  ${WW}/WWSaveLoad/definitionfactorymgr.cpp ${WW}/WWSaveLoad/definitionmgr.cpp
  ${WW}/WWSaveLoad/parameter.cpp           ${WW}/WWSaveLoad/persistfactory.cpp
  ${WW}/WWSaveLoad/pointerremap.cpp        ${WW}/WWSaveLoad/saveload.cpp
  ${WW}/WWSaveLoad/saveloadstatus.cpp      ${WW}/WWSaveLoad/saveloadsubsystem.cpp
  ${WW}/WWSaveLoad/twiddler.cpp            ${WW}/WWSaveLoad/wwsaveload.cpp)
target_include_directories(wwsaveload PUBLIC ${WW}/WWSaveLoad PRIVATE ${WW}/WWLib ${WW}/WWMath ${WW}/Wwutil)
target_link_libraries(wwsaveload PRIVATE wwdebug wwlib wwmath)

# ---- ww3d2 (renderer core) ------------------------------------------------
# wwshade is header-only here on purpose: no .dsp links wwshade.lib and its
# shd*bump* sources need shdpp/NVASM-generated headers.  Revisit at exe link.
# dx8webbrowser.cpp stays inert (ENABLE_EMBEDDED_BROWSER unset).
add_library(ww3d2 STATIC

  ${WW}/WW3D2/FramGrab.cpp ${WW}/WW3D2/aabtree.cpp ${WW}/WW3D2/aabtreebuilder.cpp 
  ${WW}/WW3D2/agg_def.cpp ${WW}/WW3D2/animatedsoundmgr.cpp ${WW}/WW3D2/animobj.cpp 
  ${WW}/WW3D2/assetmgr.cpp ${WW}/WW3D2/assetstatus.cpp ${WW}/WW3D2/bitmaphandler.cpp 
  ${WW}/WW3D2/bmp2d.cpp ${WW}/WW3D2/boxrobj.cpp ${WW}/WW3D2/bwrender.cpp 
  ${WW}/WW3D2/camera.cpp ${WW}/WW3D2/collect.cpp ${WW}/WW3D2/coltest.cpp 
  ${WW}/WW3D2/composite.cpp ${WW}/WW3D2/dazzle.cpp ${WW}/WW3D2/ddsfile.cpp 
  ${WW}/WW3D2/decalmsh.cpp ${WW}/WW3D2/decalsys.cpp ${WW}/WW3D2/distlod.cpp 
  ${WW}/WW3D2/dx8caps.cpp ${WW}/WW3D2/dx8fvf.cpp ${WW}/WW3D2/dx8indexbuffer.cpp 
  ${WW}/WW3D2/dx8polygonrenderer.cpp ${WW}/WW3D2/dx8renderer.cpp ${WW}/WW3D2/dx8rendererdebugger.cpp 
  ${WW}/WW3D2/dx8texman.cpp ${WW}/WW3D2/dx8vertexbuffer.cpp ${WW}/WW3D2/dx8webbrowser.cpp 
  ${WW}/WW3D2/dx8wrapper.cpp ${WW}/WW3D2/dynamesh.cpp ${WW}/WW3D2/font3d.cpp 
  ${WW}/WW3D2/formconv.cpp ${WW}/WW3D2/hanim.cpp ${WW}/WW3D2/hanimmgr.cpp 
  ${WW}/WW3D2/hcanim.cpp ${WW}/WW3D2/hlod.cpp ${WW}/WW3D2/hmdldef.cpp 
  ${WW}/WW3D2/hmorphanim.cpp ${WW}/WW3D2/hrawanim.cpp ${WW}/WW3D2/htree.cpp 
  ${WW}/WW3D2/htreemgr.cpp ${WW}/WW3D2/intersec.cpp ${WW}/WW3D2/layer.cpp 
  ${WW}/WW3D2/light.cpp ${WW}/WW3D2/lightenvironment.cpp ${WW}/WW3D2/line3d.cpp 
  ${WW}/WW3D2/linegrp.cpp ${WW}/WW3D2/mapper.cpp ${WW}/WW3D2/matinfo.cpp 
  ${WW}/WW3D2/matpass.cpp ${WW}/WW3D2/matrixmapper.cpp ${WW}/WW3D2/mesh.cpp 
  ${WW}/WW3D2/meshbuild.cpp ${WW}/WW3D2/meshdam.cpp ${WW}/WW3D2/meshgeometry.cpp 
  ${WW}/WW3D2/meshmatdesc.cpp ${WW}/WW3D2/meshmdl.cpp ${WW}/WW3D2/meshmdlio.cpp 
  ${WW}/WW3D2/metalmap.cpp ${WW}/WW3D2/missingtexture.cpp ${WW}/WW3D2/motchan.cpp 
  ${WW}/WW3D2/nullrobj.cpp ${WW}/WW3D2/part_buf.cpp ${WW}/WW3D2/part_emt.cpp 
  ${WW}/WW3D2/part_ldr.cpp ${WW}/WW3D2/pivot.cpp ${WW}/WW3D2/pointgr.cpp 
  ${WW}/WW3D2/polyinfo.cpp ${WW}/WW3D2/predlod.cpp ${WW}/WW3D2/prim_anim.cpp 
  ${WW}/WW3D2/projector.cpp ${WW}/WW3D2/proto.cpp ${WW}/WW3D2/render2d.cpp 
  ${WW}/WW3D2/render2dsentence.cpp ${WW}/WW3D2/renderobjectrecycler.cpp ${WW}/WW3D2/rendobj.cpp 
  ${WW}/WW3D2/rinfo.cpp ${WW}/WW3D2/ringobj.cpp ${WW}/WW3D2/scene.cpp 
  ${WW}/WW3D2/segline.cpp ${WW}/WW3D2/seglinerenderer.cpp ${WW}/WW3D2/shader.cpp 
  ${WW}/WW3D2/shattersystem.cpp ${WW}/WW3D2/snappts.cpp ${WW}/WW3D2/sortingrenderer.cpp 
  ${WW}/WW3D2/soundrobj.cpp ${WW}/WW3D2/sphereobj.cpp ${WW}/WW3D2/static_sort_list.cpp 
  ${WW}/WW3D2/statistics.cpp ${WW}/WW3D2/streak.cpp ${WW}/WW3D2/streakRender.cpp 
  ${WW}/WW3D2/stripoptimizer.cpp ${WW}/WW3D2/surfaceclass.cpp ${WW}/WW3D2/texfcach.cpp 
  ${WW}/WW3D2/texproject.cpp ${WW}/WW3D2/texture.cpp ${WW}/WW3D2/texturefilter.cpp 
  ${WW}/WW3D2/textureloader.cpp ${WW}/WW3D2/texturethumbnail.cpp ${WW}/WW3D2/txt.cpp 
  ${WW}/WW3D2/vertmaterial.cpp ${WW}/WW3D2/visrasterizer.cpp ${WW}/WW3D2/w3d_dep.cpp 
  ${WW}/WW3D2/w3d_util.cpp ${WW}/WW3D2/w3dexclusionlist.cpp ${WW}/WW3D2/ww3d.cpp 
  ${WW}/WW3D2/ww3dformat.cpp 
)
target_include_directories(ww3d2
  PUBLIC ${WW}/WW3D2
  PRIVATE ${WW}/WWLib ${WW}/WWMath ${WW}/WWDebug ${WW}/WWSaveLoad ${WW}/Wwutil
          ${WW}/wwshade ${WW}/WWAudio ${WW}/Miles6/include
          ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/DirectX/Include)
target_link_directories(ww3d2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/DirectX/Lib)
# vfw32: FramGrab.cpp writes AVIs.  legacy_stdio_definitions: d3dx8.lib calls
# _vsnprintf, which the modern CRT only provides as an inline.
target_link_libraries(ww3d2
  PUBLIC d3d8 d3dx8 dxguid vfw32 legacy_stdio_definitions
  PRIVATE wwlib wwmath wwdebug wwsaveload)
# The 2001 d3dx8.lib predates SAFESEH and was built against the long-gone
# libci.lib CRT, so anything linking it has to opt out of both.
target_link_options(ww3d2 INTERFACE /SAFESEH:NO /NODEFAULTLIB:libci.lib)
# dx8webbrowser.cpp is the one file /MP cannot have: MSVC rejects #import under
# multi-process compilation (C2813) and finds the directive by a lexical scan, so
# even though ENABLE_EMBEDDED_BROWSER's block is what holds it, disabling the block
# would not help.  /MP1 puts just this file back on a single process.
set_source_files_properties(${WW}/WW3D2/dx8webbrowser.cpp PROPERTIES COMPILE_OPTIONS /MP1)

# Phase 8: 1 kHz EIP sampler that symbolizes against the linker map - the
# machine has no debugger, so this is how hotspots get names.
add_executable(eip_sampler ${CMAKE_CURRENT_SOURCE_DIR}/Tools/ProfileSampler/eip_sampler.cpp)
set_target_properties(eip_sampler PROPERTIES CXX_STANDARD 17)

# Phase 1 checkpoint: vendored DX8 SDK + a d3d8.dll create a real device.  Our d3d8to9
# d3d8.dll is copied next to the exe (see below, after d3d8to9 is defined) so the test
# exercises the game's real path: Direct3D 9 by default, Direct3D 9On12 / Direct3D 12 with -d3d12.
add_executable(dx8_smoke ${WW}/WW3D2/test_dx8_smoke.cpp)
target_include_directories(dx8_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/DirectX/Include)
target_link_directories(dx8_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/DirectX/Lib)
target_link_libraries(dx8_smoke PRIVATE d3d8 user32)

# ===========================================================================
# Phase 2 - remaining support libs.  None of these touch DX8.
# ===========================================================================
set(LIBSRC ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source)

# ---- debug ----------------------------------------------------------------
# EA's in-repo debug/logging lib (RTS.dsp links it as debug.lib).  Self-contained
# apart from the Win32 SDK.  The test1-6/, netserv/ and debug_dlg/ subdirs are
# separate .dsp projects, not part of the lib.
add_library(debug STATIC
  ${LIBSRC}/debug/debug.cpp        ${LIBSRC}/debug/debug_cmd.cpp
  ${LIBSRC}/debug/debug_debug.cpp  ${LIBSRC}/debug/debug_except.cpp
  ${LIBSRC}/debug/debug_getdefaultcommands.cpp
  ${LIBSRC}/debug/debug_internal.cpp
  ${LIBSRC}/debug/debug_io_con.cpp ${LIBSRC}/debug/debug_io_flat.cpp
  ${LIBSRC}/debug/debug_io_net.cpp ${LIBSRC}/debug/debug_io_ods.cpp
  ${LIBSRC}/debug/debug_purecall.cpp
  ${LIBSRC}/debug/debug_stack.cpp)
target_include_directories(debug PUBLIC ${LIBSRC}/debug)
target_link_libraries(debug PUBLIC dbghelp comctl32 ws2_32)
# "debug" is also a target_link_libraries config keyword, so it can never be
# passed as a plain library name.  Link this alias instead.
add_library(debuglib ALIAS debug)

# ---- profile --------------------------------------------------------------
# EA's in-repo sampling/function profiler.  GameEngine includes profile.h, whose
# #pragma comment(lib, "profile.lib") makes this a hard link dependency even
# though almost nothing calls into it.
add_library(profile STATIC
  ${LIBSRC}/profile/profile.cpp           ${LIBSRC}/profile/profile_cmd.cpp
  ${LIBSRC}/profile/profile_funclevel.cpp ${LIBSRC}/profile/profile_highlevel.cpp
  ${LIBSRC}/profile/profile_result.cpp)
target_include_directories(profile PUBLIC ${LIBSRC}/profile)
target_link_libraries(profile PUBLIC debuglib)

# ---- wwdownload -----------------------------------------------------------
# FTP patch downloader.  Its wlib/resource.h includes are all commented out in
# the shipped source, so it needs nothing but winsock.
add_library(wwdownload STATIC
  ${WW}/WWDownload/Download.cpp ${WW}/WWDownload/FTP.CPP
  ${WW}/WWDownload/registry.cpp ${WW}/WWDownload/urlBuilder.cpp)
target_include_directories(wwdownload PUBLIC ${WW}/WWDownload ${WW})
target_link_libraries(wwdownload PUBLIC wsock32 winmm)

# ---- benchmark ------------------------------------------------------------
# BYTEmark sources are missing; benchmark_stub.c stands in for them.  See the
# file header for why a constant is enough and when to revisit.
add_library(benchmark STATIC ${LIBSRC}/Benchmark/benchmark_stub.c)
target_include_directories(benchmark PUBLIC ${LIBSRC}/Benchmark)

# ---- eabrowserdispatch ----------------------------------------------------
# Only BrowserDispatch.idl shipped; the .h/_i.c that GameEngine's WebBrowser.h
# expects are MIDL output.  Generate them at build time (MSBuild puts cl.exe,
# MIDL's preprocessor, on PATH) into a dir laid out so the shipped
# #include "EABrowserDispatch/BrowserDispatch.h" resolves unchanged.
set(EABD_GEN ${CMAKE_CURRENT_BINARY_DIR}/generated/EABrowserDispatch)
file(MAKE_DIRECTORY ${EABD_GEN})
# midl.exe ships with the Windows SDK, not on PATH at configure time.
file(GLOB MIDL_HINTS "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/*/x86")
find_program(MIDL_EXECUTABLE midl HINTS ${MIDL_HINTS} REQUIRED)
add_custom_command(
  OUTPUT ${EABD_GEN}/BrowserDispatch.h ${EABD_GEN}/BrowserDispatch_i.c
  COMMAND ${MIDL_EXECUTABLE} /nologo /env win32 /win32
          /h ${EABD_GEN}/BrowserDispatch.h
          /iid ${EABD_GEN}/BrowserDispatch_i.c
          /proxy ${EABD_GEN}/BrowserDispatch_p.c
          /dlldata ${EABD_GEN}/dlldata.c
          /tlb ${EABD_GEN}/BrowserDispatch.tlb
          ${LIBSRC}/EABrowserDispatch/BrowserDispatch.idl
  DEPENDS ${LIBSRC}/EABrowserDispatch/BrowserDispatch.idl
  COMMENT "MIDL BrowserDispatch.idl")
add_library(eabrowserdispatch STATIC
  ${EABD_GEN}/BrowserDispatch_i.c ${EABD_GEN}/BrowserDispatch.h)
target_include_directories(eabrowserdispatch
  PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)

# ---- gamespy --------------------------------------------------------------
# PLAN.md called for hand-written stubs behind the five GameSpy* libs, but the
# real SDK is open source (TheSuperHackers/GamespySDK, itself CMake-based), so
# vendor and build it instead: correct signatures, structs and enum values
# rather than 73 guessed prototypes.  Multiplayer stays out of scope - this only
# has to compile and link; nothing needs to reach a live GameSpy server.
# OpenSSL/HTTPS off so the SDK does not FetchContent OpenSSL for services that
# have been dead since 2014; voice and tests off for the same reason.
set(GS_OPENSSL          OFF CACHE BOOL "" FORCE)
set(GS_SECURESECONNECT  OFF CACHE BOOL "" FORCE)
set(GS_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
set(GS_INCLUDE_VOICE    OFF CACHE BOOL "" FORCE)
add_subdirectory(${LIBSRC}/GameSpy ${CMAKE_CURRENT_BINARY_DIR}/gamespy)

# d3d8to9 (vendored, github.com/crosire/d3d8to9): builds our own d3d8.dll that
# translates every D3D8 call to Direct3D 9.  It sits next to generals.exe, the
# loader prefers the application directory over system32, and the whole
# renderer runs on the DX9 runtime without touching the ~5600 D3D8 call sites.
add_subdirectory(${LIBSRC}/d3d8to9 ${CMAKE_CURRENT_BINARY_DIR}/d3d8to9)

# ===========================================================================
# Phase 3 - GameEngine.  ~580 files of game logic; no DX8, no device code.
# ===========================================================================
set(GE ${CMAKE_CURRENT_SOURCE_DIR}/GameEngine)

# The source tree is a frozen 2003 snapshot, so a glob cannot go stale the way
# it would in a live project - and 580 explicit paths would drown this file.
file(GLOB_RECURSE GAMEENGINE_SOURCES ${GE}/Source/*.cpp)
# On disk but absent from GameEngine.dsp, i.e. never in the shipping build:
# dead experiments (SimplePlayer, urlLaunch, VideoStream), an older copy of
# DrawableManager that GameClient/Drawable/ supersedes, and GameSpeech, whose
# hash uses std::hash<const char*> on strings that outlive nothing.
list(REMOVE_ITEM GAMEENGINE_SOURCES
  ${GE}/Source/Common/Audio/GameSpeech.cpp
  ${GE}/Source/Common/Audio/simpleplayer.cpp
  ${GE}/Source/Common/Audio/urllaunch.cpp
  ${GE}/Source/GameClient/DrawableManager.cpp
  ${GE}/Source/GameClient/VideoStream.cpp)
# GameNetwork/GameSpy.h and the two .cpp files that owned it were stripped from
# the release, and these two are all that include it.  Multiplayer is out of
# scope (PLAN.md), so they stay out until the exe link says otherwise.
list(REMOVE_ITEM GAMEENGINE_SOURCES
  ${GE}/Source/GameNetwork/GameSpyChat.cpp
  ${GE}/Source/GameNetwork/GameSpyGP.cpp)
# EA's own header says "#error this file is obsolete", and this .cpp is its only
# includer - the live code went to GameNetwork/GameSpy/ instead.
list(REMOVE_ITEM GAMEENGINE_SOURCES
  ${GE}/Source/GameNetwork/GameSpyGameInfo.cpp)

add_library(gameengine STATIC ${GAMEENGINE_SOURCES})
target_include_directories(gameengine PUBLIC
  ${GE}/Include ${GE}/Include/Precompiled
  ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include
  ${WW} ${WW}/WWLib
  # The game includes "GameSpy/Peer/Peer.h", the SDK's own headers include each
  # other as "gscommon.h", so both levels have to be on the path.
  ${LIBSRC}/GameSpy/include ${LIBSRC}/GameSpy/include/gamespy ${COMP}
  ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/DirectX/Include)
# _RELEASE/_DEBUG pick the game's own build flavour; IG_DEBUG_STACKTRACE and
# Z_PREFIX are what GameEngine.dsp sets.
target_compile_definitions(gameengine PUBLIC
  IG_DEBUG_STACKTRACE Z_PREFIX
  $<$<CONFIG:Debug>:_DEBUG> $<$<NOT:$<CONFIG:Debug>>:_RELEASE>)
# C++17 for the std::hash_map -> unordered_map alias and string_view hashing in
# STLTypedefs.h.
set_target_properties(gameengine PROPERTIES CXX_STANDARD 17)
target_link_libraries(gameengine PUBLIC
  ww3d2 wwlib wwmath wwsaveload wwdebug wwutil compression debuglib wwdownload
  benchmark eabrowserdispatch profile gamespy
  dinput8 wininet imagehlp imm32)

# ===========================================================================
# Phase 4 - GameEngineDevice + Main -> generals.exe
# ===========================================================================
set(GED ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice)
set(STUBS ${CMAKE_CURRENT_SOURCE_DIR}/Stubs)

file(GLOB_RECURSE GAMEENGINEDEVICE_SOURCES ${GED}/Source/*.cpp)
# On disk but absent from GameEngineDevice.dsp: the DirectInput mouse (the
# shipping build uses the Win32 message mouse instead).
list(REMOVE_ITEM GAMEENGINEDEVICE_SOURCES
  ${GED}/Source/Win32Device/GameClient/Win32DIMouse.cpp)
# Miles is binary-only too, but the game ships mss32.dll and it exports
# decorated stdcall names (_AIL_startup@0, ...), so MilesAudioManager can bind
# to the real DLL through an import library - real audio, no SDK.  Thyme's
# recipe: build a no-op stub as mss32.dll from miles.c + miles.def (the def
# mirrors the shipped DLL's export table); linking against the stub's import
# lib makes the exe resolve those names from whatever mss32.dll sits next to
# it at runtime - the game's real one.  (`lib /def` alone can't produce this
# import lib: it prepends a second underscore to the already-decorated names.)
# Bink is binary-only too, and the same trick works: the game ships BINKW32.DLL
# and it exports decorated stdcall names (_BinkOpen@8, ...), so BinkVideoPlayer
# binds to the real DLL through the import library of a no-op stub built as
# binkw32.dll.  Real movies, no SDK.  The stub dll in build/Release/ must never
# be copied to Run/ - the loader has to find the game's own BINKW32.DLL there.
add_library(binkstub SHARED
  ${WW}/Bink/stub/bink.c ${WW}/Bink/stub/bink.def)
set_target_properties(binkstub PROPERTIES OUTPUT_NAME binkw32)
target_include_directories(binkstub PRIVATE ${WW}/Bink/include)

add_library(milesstub SHARED
  ${WW}/Miles6/stub/miles.c ${WW}/Miles6/stub/miles.def)
set_target_properties(milesstub PROPERTIES OUTPUT_NAME mss32)
target_include_directories(milesstub PRIVATE ${WW}/Miles6/stub ${WW}/Miles6/include)

add_library(gameenginedevice STATIC ${GAMEENGINEDEVICE_SOURCES})

target_include_directories(gameenginedevice PUBLIC
  ${GED}/Include ${STUBS} ${CMAKE_CURRENT_SOURCE_DIR}/Main
  ${WW}/Miles6/include ${WW}/Bink/include)
set_target_properties(gameenginedevice PROPERTIES CXX_STANDARD 17)
target_link_libraries(gameenginedevice PUBLIC gameengine ww3d2 milesstub binkstub)

# ---- generals.exe ---------------------------------------------------------
# BuildVersion.h and GeneratedVersion.h are normally written by Tools/
# buildVersionUpdate and Tools/versionUpdate as a pre-build step, from a build
# counter in the source tree.  Both are three-line headers, so write them at
# configure time instead - it keeps two more VC6 tools out of the port, and
# Windows wants UAC elevation for exe names containing "version" anyway.
set(GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(WRITE ${GENERATED}/BuildVersion.h
"// Generated by CMake, standing in for Tools/buildVersionUpdate.\n"
"#define VERSION_STRING \"v0.0.1-alpha\"\n"
"#define VERSION_MAJOR 0\n"
"#define VERSION_MINOR 0\n"
"#define VERSION_BUILDNUM 1\n")
file(WRITE ${GENERATED}/GeneratedVersion.h
"// Generated by CMake, standing in for Tools/versionUpdate.\n"
"#define LOCAL_BUILD_STRING \"0\"\n"
"#define VERSION_LOCALBUILDNUM 0\n"
"#define VERSION_BUILDUSER \"cmake\"\n"
"#define VERSION_BUILDLOC \"${CMAKE_HOST_SYSTEM_NAME}\"\n")

add_executable(generals WIN32 ${CMAKE_CURRENT_SOURCE_DIR}/Main/WinMain.cpp
                             ${CMAKE_CURRENT_SOURCE_DIR}/Main/RTS.RC)
target_include_directories(generals PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Main ${GENERATED})
# A .map turns the "fault offset" in the Windows Application Error event into a
# function name without a debugger: find the last entry whose RVA is <= the offset.
target_link_options(generals PRIVATE /MAP)
# A 32-bit process gets 2GB of user address space by default; this asks for the
# whole 4GB, which 64-bit Windows hands over with no /3GB switch and no reboot.
# 4GB is the ceiling and there is no way past it here - the pointer is 32 bits,
# and x64 is rejected by this port on purpose (the code is full of 32-bit __asm).
#
# Measured need: an eight-player Merciless match on Twilight Flame peaks at 978MB
# of virtual address space, so it is at half the old limit already, and a 2003
# engine walking into that wall does not report it - it stalls for a third of a
# second at a time and then dies inside ntdll's heap.
#
# The classic risk is code that treats a pointer as a signed 32-bit int, which
# only breaks once an allocation lands above 2GB. Nothing here is known to; if a
# pointer-signedness bug ever surfaces, this is the first line to suspect.
target_link_options(generals PRIVATE /LARGEADDRESSAWARE)
set_target_properties(generals PROPERTIES CXX_STANDARD 17)
target_link_libraries(generals PRIVATE gameenginedevice
  winmm dsound wsock32 ole32 oleaut32 uuid)
# The game reads its data relative to the working directory, so it has to sit
# beside Run/'s .big files to find anything (Phase 5).
add_custom_command(TARGET generals POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE:generals> ${CMAKE_CURRENT_SOURCE_DIR}/../Run/
  # The PDB has to sit next to the exe: SymInitialize searches the exe's directory,
  # and without it the crash log's stack is addresses with no names.
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_PDB_FILE:generals> ${CMAKE_CURRENT_SOURCE_DIR}/../Run/
  # d3d8to9's d3d8.dll next to the exe = the renderer runs on Direct3D 9.
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE:d3d8to9> ${CMAKE_CURRENT_SOURCE_DIR}/../Run/
  # The fork's key bindings and their labels.  Run/ is gitignored, so these two
  # used to be hand-made files that silently went stale every time a command was
  # added - the tracked copies under Code/Data are the masters and this puts them
  # where the game reads them.  A loose file beats every .big, and the game never
  # writes either one back, so overwriting them on each build is safe.
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/INI/CommandMap.ini
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Data/INI/
  # The fork's detonation light, loaded after EA's FXList.ini.  It is inside the
  # multiplayer INI CRC, so it has to be here, in the zip, and identical on every
  # machine in a game.
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/INI/FXListReborn.ini
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Data/INI/
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Patch.str
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Data/
  # The startup splash.  WinMain loads it from a loose file next to the exe, so
  # the fork's own artwork is a tracked master here rather than a hand-made file
  # in gitignored Run/.
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Install_Final.bmp
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/
  # The three command bar plates per side.  A loose texture beats every .big, so
  # these land in Run/Art/Textures where the W3D file system looks first.
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Art/Textures
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Art/Textures
  # The fork's window layouts, generated from the shipped ones by Tools/*.py and
  # tracked here.  They live outside Data/ in the archive (Window/Menus/...), and
  # a loose file wins over WindowZH.big.  Layouts are not in the multiplayer INI
  # checksum, so an out of date copy costs a wrong looking screen, not a refused
  # join.
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Window
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Window)
add_dependencies(generals d3d8to9)

# ===========================================================================
# Tests.  Run with:  ctest --test-dir build -C Release --output-on-failure
# ===========================================================================
enable_testing()

set(TESTS ${CMAKE_CURRENT_SOURCE_DIR}/Tests)

# Shared runner for the micro-harness in Tests/test_harness.h; every test
# binary links it instead of writing its own main().
add_library(test_harness STATIC ${TESTS}/test_main.cpp)
target_include_directories(test_harness PUBLIC ${TESTS})

# add_lib_test(<name> <libs...>) -> Tests/<name>.cpp, built and registered.
function(add_lib_test name)
  add_executable(${name} ${TESTS}/${name}.cpp)
  target_link_libraries(${name} PRIVATE test_harness ${ARGN})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_lib_test(test_wwmath wwmath wwlib wwsaveload wwdebug)
target_include_directories(test_wwmath PRIVATE ${WW}/WWLib ${WW}/WWSaveLoad ${WW}/Wwutil)

add_lib_test(test_wwlib wwlib wwmath wwsaveload wwdebug)
target_include_directories(test_wwlib PRIVATE ${WW}/WWLib ${WW}/WWMath ${WW}/WWSaveLoad ${WW}/Wwutil)
# generals.exe is linked /LARGEADDRESSAWARE, and that flag changes what
# GlobalMemoryStatus saturates its 32-bit fields to (0xFFFFFFFF, not 0x7FFFFFFF).
# cpudetect_memory_fits_in_the_signed_int_its_callers_use only means anything in a
# binary that sees the same value the game does.
target_link_options(test_wwlib PRIVATE /LARGEADDRESSAWARE)

add_lib_test(test_wwsaveload wwsaveload wwlib wwmath wwdebug)
target_include_directories(test_wwsaveload PRIVATE ${WW}/WWSaveLoad ${WW}/WWLib ${WW}/WWMath ${WW}/WWDebug ${WW}/Wwutil)

add_lib_test(test_wwutil wwutil wwmath wwlib wwdebug)
target_include_directories(test_wwutil PRIVATE ${WW}/Wwutil ${WW}/WWLib ${WW}/WWMath)

add_lib_test(test_compression compression)

add_lib_test(test_ww3d2 ww3d2 wwsaveload wwlib wwmath wwdebug)
target_include_directories(test_ww3d2 PRIVATE
  ${WW}/WW3D2 ${WW}/WWLib ${WW}/WWMath ${WW}/WWDebug ${WW}/WWSaveLoad ${WW}/Wwutil
  ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/DirectX/Include)

add_lib_test(test_wwdownload wwdownload)
target_include_directories(test_wwdownload PRIVATE ${WW}/WWDownload)

add_lib_test(test_debug debuglib)

add_lib_test(test_support benchmark eabrowserdispatch)

# Phase 3 checkpoint: boots the memory manager, name keys and file system, then
# runs a real INI file through the parser.
add_lib_test(test_gameengine gameengine)
# the well-known Dict keys and a handful of device/exe entry points live in
# GameEngineDevice, which Phase 4 ports; stand-ins until then.
target_sources(test_gameengine PRIVATE ${TESTS}/test_gameengine_stubs.cpp)
set_target_properties(test_gameengine PROPERTIES CXX_STANDARD 17)
# simulation_uses_no_runtime_trig reads the engine's own sources back off disk to prove nothing in
# GameLogic or Common has gone back to calling sinf().
# fxlist_reborn_ini_parses_and_keeps_its_light runs the fork's own FXList file through the real
# INI parser, so it needs the tracked master rather than whatever sits in Run/.
# the_options_menu_buttons_all_wear_one_font reads the tracked layout master, not Run/'s copy.
target_compile_definitions(test_gameengine PRIVATE
  GAMEENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/GameEngine"
  FXLIST_REBORN_INI="${CMAKE_CURRENT_SOURCE_DIR}/Data/INI/FXListReborn.ini"
  OPTIONS_MENU_WND="${CMAKE_CURRENT_SOURCE_DIR}/Data/Window/Menus/OptionsMenu.wnd")

# The three hand-written self-checks predate the harness; run them from ctest
# too so one command covers everything.
add_test(NAME wwmath_selfcheck COMMAND wwmath_selfcheck)
add_test(NAME compression_selfcheck COMMAND compression_selfcheck)
# dx8_smoke runs through our d3d8to9 d3d8.dll (copied next to it): once on its default,
# Direct3D 9, once opted in to Direct3D 9On12 / Direct3D 12 via -d3d12 (each run fails if
# it did not land on its runtime)
add_custom_command(TARGET dx8_smoke POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:d3d8to9> $<TARGET_FILE_DIR:dx8_smoke>)
add_dependencies(dx8_smoke d3d8to9)
add_test(NAME dx8_smoke COMMAND dx8_smoke)
add_test(NAME dx8_smoke_d3d12 COMMAND dx8_smoke -d3d12)

# bink_smoke validates the hand-reconstructed Bink ABI against the real
# BINKW32.DLL the game ships - it loads that DLL by path, so it does not link
# binkstub and it has to run from Run/.  Without game data it prints "skip".
add_executable(bink_smoke ${TESTS}/bink_smoke.c)
target_include_directories(bink_smoke PRIVATE ${WW}/Bink/include)
add_test(NAME bink_smoke COMMAND bink_smoke
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../Run)
# "-msaa": a multisampled back buffer, and the plain render target + non-multisampled depth
# buffer pairing that DX8Wrapper::_Get_Non_MultiSampled_Depth_Buffer hands the screen filters.
add_test(NAME dx8_smoke_msaa COMMAND dx8_smoke -msaa 4)

# Tools/bigfile.py reads and writes the game's BIG archives (that is how the shipped INIs
# come out for editing).  Its selfcheck round-trips synthetic files, so it needs no game
# data - but it does need an interpreter, so it is only registered when one is present.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME bigfile_selfcheck
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/bigfile.py selfcheck)
  # Tools/vfx_tune.py rewrites those INIs in place; its selfcheck runs both rule sets over a
  # synthetic one and checks the block parser did not eat an End or duplicate a key.
  add_test(NAME vfx_tune_selfcheck
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/vfx_tune.py selfcheck)
  # The options screen is three tracked files that have to agree: TheOptionCatalog names a widget,
  # Data/Window/Menus/OptionsMenu.wnd has to carry it, and Data/Patch.str has to carry its label,
  # its tooltip and every combo box entry.  Nothing at build time notices a typo in any of them -
  # the control is just never filled in, or the screen draws the key name - so it is checked here.
  add_test(NAME optionsmenu_selfcheck
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/Tools/optionsmenu_layout.py selfcheck)
  # The peace time combo box is the same three-file agreement, in the two network lobby layouts:
  # the menu code names ComboBoxPeaceTime, the layouts have to carry it, and Patch.str has to carry
  # the entry captions, the tooltip, the corner countdown and the announcements.  A missing control
  # is silent - the menu null checks it and the option just never appears - so it is checked here.
  add_test(NAME peacetime_selfcheck
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/Tools/peacetime_layout.py selfcheck)
endif()

if(RELEASE_DEBUG_LOGGING)
  target_compile_definitions(gameengine PRIVATE RELEASE_DEBUG_LOGGING)
  target_compile_definitions(gameenginedevice PRIVATE RELEASE_DEBUG_LOGGING)
  target_compile_definitions(generals PRIVATE RELEASE_DEBUG_LOGGING)
endif()

# THREADING-ROADMAP.md section 0: the measurement build.  EA's PerfGather call sites are all
# over the frame already, but PerfTimer.h defines NO_PERF_TIMERS in *both* branches of its
# #if, so PERF_TIMERS is off in every configuration and every USE_PERF_TIMER compiles to
# nothing.  This switch turns them on without touching the shipping default: the timers read
# the TSC on every scope entry and exit, which is not free, so OFF is the right default and
# any perf claim has to say which build it came from.
#
# Only the three targets that include PerfTimer.h get the define.  It must not go on globally:
# WWVegas' headers reach GameEngine's Debug.h, and a mismatched define across libraries is how
# you get a link error at best and a silently different struct at worst.
option(PERF_TIMERS "Compile EA's PerfGather scopes in (measurement build, not for shipping)" OFF)
if(PERF_TIMERS)
  target_compile_definitions(gameengine PRIVATE PERF_TIMERS)
  target_compile_definitions(gameenginedevice PRIVATE PERF_TIMERS)
  target_compile_definitions(generals PRIVATE PERF_TIMERS)
  # A measurement build is not the build anyone plays, and its post-build step copies into the
  # same Run/ directory the shipping one does.  Renaming the output keeps the two side by side -
  # which is also the only way to measure one against the other without rebuilding in between
  # (ai-batch.ps1 -Exe takes the name).
  set_target_properties(generals PROPERTIES OUTPUT_NAME generals_perf)
  message(STATUS "PERF_TIMERS ON - measurement build, timers cost real time; exe is generals_perf.exe")
endif()
