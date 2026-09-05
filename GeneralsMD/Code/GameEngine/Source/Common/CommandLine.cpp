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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////


#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ArchiveFileSystem.h"
#include "Common/CommandLine.h"
#include "Common/CRCDebug.h"
#include "Common/LocalFileSystem.h"
#include "Common/OptionsCatalog.h"
#include "Common/Version.h"
#include "GameClient/TerrainVisual.h" // for TERRAIN_LOD_MIN definition
#include "GameClient/GameText.h"
#include "GameNetwork/GameInfo.h" // for the SlotState -autoskirmish hands the AI slots
#include "Common/FileSystem.h"
#include "Common/RandomMapGenerator.h" // for -randommap

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif



Bool TheDebugIgnoreSyncErrors = FALSE;
extern Int DX8Wrapper_PreserveFPU;

#ifdef DEBUG_CRC
Int TheCRCFirstFrameToLog = -1;
UnsignedInt TheCRCLastFrameToLog = 0xffffffff;
Bool g_keepCRCSaves = FALSE;
Bool g_crcModuleDataFromLogic = FALSE;
Bool g_crcModuleDataFromClient = FALSE;
Bool g_verifyClientCRC = FALSE; // verify that GameLogic CRC doesn't change from client
Bool g_clientDeepCRC = FALSE;
Bool g_logObjectCRCs = FALSE;
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
extern Bool g_useStringFile;
#endif

// Retval is number of cmd-line args eaten
typedef Int (*FuncPtr)( char *args[], int num );

static const UnsignedByte F_NOCASE = 1; // Case-insensitive

struct CommandLineParam
{
	const char *name;
	FuncPtr func;
};

static void ConvertShortMapPathToLongMapPath(AsciiString &mapName)
{
	AsciiString path = mapName;
	AsciiString token;
	AsciiString actualpath;

	if ((path.find('\\') == NULL) && (path.find('/') == NULL))
	{
		DEBUG_CRASH(("Invalid map name %s", mapName.str()));
		return;
	}
	path.nextToken(&token, "\\/");
	while (!token.endsWithNoCase(".map") && (token.getLength() > 0))
	{
		actualpath.concat(token);
		actualpath.concat('\\');
		path.nextToken(&token, "\\/");
	}

	if (!token.endsWithNoCase(".map"))
	{
		DEBUG_CRASH(("Invalid map name %s", mapName.str()));
	}
	// remove the .map from the end.
	token.removeLastChar();
	token.removeLastChar();
	token.removeLastChar();
	token.removeLastChar();

	// "maps\foo.map" is the short form and grows the directory the map lives in; a path that
	// already names that directory ("maps\foo\foo.map") is left alone instead of growing a third.
	AsciiString dir = token;
	dir.concat('\\');
	if (!actualpath.endsWithNoCase(dir.str()))
	{
		actualpath.concat(token);
		actualpath.concat('\\');
	}
	actualpath.concat(token);
	actualpath.concat(".map");

	mapName = actualpath;
}

//=============================================================================
//=============================================================================
Int parseNoLogOrCrash(char *args[], int)
{
#ifdef ALLOW_DEBUG_UTILS
	DEBUG_CRASH(("-NoLogOrCrash not supported in this build\n"));
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseWin(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_windowMode = WINDOW_MODE_WINDOWED;
		applyWindowMode();
	}
	return 1;
}

//=============================================================================
/* -borderless is fullscreen without the display mode change: a windowed device the size of the
	 desktop, in a window with no caption and no frame, so it covers the screen exactly the way an
	 exclusive fullscreen device does but alt-tabs instantly, never loses the device on focus change,
	 and leaves a second monitor usable.  The window style and size are decided in WinMain, which
	 preparses -borderless off the raw command line long before this parser runs - the window has to
	 exist before the engine does.  What is left here is the back buffer, which has to match the
	 window or the picture is a stretched blit and the cursor no longer lands where it is drawn, and
	 applyWindowMode is what sizes it - the same code an Options.ini that says Borderless goes
	 through, so the switch and the setting cannot drift apart.

	 -xres/-yres after -borderless still win, for a smaller back buffer in a borderless window.

	 Edge scrolling goes back on because retail turns it off for windowed play - the cursor can
	 legitimately sit on the border while you reach for another window - and a window covering the
	 whole display has no such border.  Options.ini's EdgeScrollInWindowedMode is read before the
	 command line, so a player who set it either way is overridden here on purpose. */
//=============================================================================
Int parseBorderless(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_windowMode = WINDOW_MODE_BORDERLESS;
		applyWindowMode();
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoMusic(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_musicOn = false;
	}
	return 1;
}


//=============================================================================
//=============================================================================
Int parseNoVideo(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_videoOn = false;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseFPUPreserve(char *args[], int argc)
{
	if (argc > 1)
	{
		DX8Wrapper_PreserveFPU = atoi(args[1]);
	}
	return 2;
}

#if defined(_DEBUG) || defined(_INTERNAL)
//=============================================================================
//=============================================================================
Int parseUseCSF(char *args[], int)
{
	g_useStringFile = FALSE;
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoInputDisable(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_disableScriptedInputDisabling = true;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoFade(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_disableCameraFade = true;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoMilCap(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_disableMilitaryCaption = true;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseDebugCRCFromFrame(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		TheCRCFirstFrameToLog = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseDebugCRCUntilFrame(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		TheCRCLastFrameToLog = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseKeepCRCSave(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_keepCRCSaves = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseCRCLogicModuleData(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_crcModuleDataFromLogic = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseCRCClientModuleData(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_crcModuleDataFromClient = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseClientDeepCRC(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_clientDeepCRC = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseVerifyClientCRC(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_verifyClientCRC = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseLogObjectCRCs(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_logObjectCRCs = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNetCRCInterval(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		NET_CRC_INTERVAL = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseReplayCRCInterval(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		REPLAY_CRC_INTERVAL = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseNoDraw(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_noDraw = TRUE;
	}
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseLogToConsole(char *args[], int)
{
	DebugSetFlags(DebugGetFlags() | DEBUG_FLAG_LOG_TO_CONSOLE);
	return 1;
}

#endif // _DEBUG || _INTERNAL

//=============================================================================
//=============================================================================
Int parseNoAudio(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_audioOn = false;
		TheWritableGlobalData->m_speechOn = false;
		TheWritableGlobalData->m_soundsOn = false;
		TheWritableGlobalData->m_musicOn = false;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoWin(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_windowMode = WINDOW_MODE_FULLSCREEN;
		applyWindowMode();
	}
	return 1;
}

Int parseFullVersion(char *args[], int num)
{
	if (TheVersion && num > 1)
	{
		TheVersion->setShowFullVersion(atoi(args[1]) != 0);
	}
	return 1;
}

Int parseNoShadows(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_useShadowVolumes = false;
		TheWritableGlobalData->m_useShadowDecals = false;
	}
	return 1;
}

Int parseMapName(char *args[], int num)
{
	// num is what is *left* on the command line, so the original "== 2" quietly ignored the map
	// whenever another option followed it, and returning 1 left the file name to be re-parsed.
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_mapName.set( args[ 1 ] );
		ConvertShortMapPathToLongMapPath(TheWritableGlobalData->m_mapName);
		return 2;
	}
	return 1;
}

// parseRandomMap =============================================================
/** -randommap <seed> [players] [cells]: generate a skirmish map from the seed and write it into
	the user map directory as "RMG_<seed>", then point -map at it.  Nothing downstream has to know
	it was generated: the map cache walks that directory on startup, so it shows up in the skirmish
	map list like any hand-made map, and -autoskirmish starts on it.  The same seed gives the same
	map on every machine, so both sides of a network game can be handed the same command line. */
//=============================================================================
Int parseRandomMap(char *args[], int num)
{
	if (TheWritableGlobalData == NULL || num < 2)
		return 1;

	RandomMapSettings settings;
	settings.m_seed = atoi( args[1] );
	// Generate all the start positions the map is allowed to hold, so any -autoskirmish count
	// fits regardless of which of the two options came first on the command line.
	settings.m_numPlayers = RandomMapGenerator::MAX_PLAYERS;

	// players and cells are optional and positional, so only eat what still looks like a number.
	Int eaten = 2;
	if (num > eaten && isdigit((UnsignedByte)args[eaten][0]))
		settings.m_numPlayers = atoi( args[eaten++] );
	if (num > eaten && isdigit((UnsignedByte)args[eaten][0]))
		settings.m_playableCells = atoi( args[eaten++] );

	std::vector<char> mapBytes;
	RandomMapGenerator::generate( settings, mapBytes );

	// The map cache expects "<user maps>\<name>\<name>.map" - the directory carries the name.
	AsciiString mapsDir, dir, path;
	mapsDir.format( "%sMaps", TheGlobalData->getPath_UserData().str() );
	dir.format( "%s\\RMG_%d", mapsDir.str(), settings.m_seed );
	path.format( "%s\\RMG_%d.map", dir.str(), settings.m_seed );
	TheFileSystem->createDirectory( mapsDir );		// createDirectory is one level at a time
	TheFileSystem->createDirectory( dir );

	FILE *fp = fopen( path.str(), "wb" );
	if (fp == NULL)
	{
		DEBUG_LOG(("-randommap: could not write '%s'\n", path.str()));
		return eaten;
	}
	fwrite( &mapBytes[0], 1, mapBytes.size(), fp );
	fclose( fp );

	TheWritableGlobalData->m_mapName = path;
	DEBUG_LOG(("-randommap: wrote '%s' - %d players, %d cells, %d bytes\n",
		path.str(), settings.m_numPlayers, settings.m_playableCells, mapBytes.size()));
	return eaten;
}

Int parseXRes(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_xResolution = atoi(args[1]);
		return 2;
	}
	return 1;
}

Int parseYRes(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_yResolution = atoi(args[1]);
		return 2;
	}
	return 1;
}

/* The five synthetic link conditions below used to be debug-build-only, and this fork only ever
	 ships Release, so the switch they drive did not exist in any build anyone could run.  They are
	 the cheapest way to reproduce a lossy or slow link without a second machine on a bad line. */
//=============================================================================
//=============================================================================
Int parseLatencyAverage(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_latencyAverage = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parseLatencyAmplitude(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_latencyAmplitude = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parseLatencyPeriod(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_latencyPeriod = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parseLatencyNoise(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_latencyNoise = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parsePacketLoss(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_packetLoss = atoi(args[1]);
	}
	return 2;
}

#if defined(_DEBUG) || defined(_INTERNAL)
//=============================================================================
//=============================================================================
Int parseLowDetail(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_terrainLOD = TERRAIN_LOD_MIN;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoDynamicLOD(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_enableDynamicLOD = FALSE;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoStaticLOD(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_enableStaticLOD = FALSE;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseUseWaveEditor(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_usingWaterTrackEditor = TRUE;
	}
	return 1;
}

//=============================================================================
Int parseNoViewLimit(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_useCameraConstraints = FALSE;
	}
	return 1;
}

Int parseWireframe(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_wireframe = TRUE;
	}
	return 1;
}

Int parseShowCollision(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_showCollisionExtents = TRUE;
	}
	return 1;
}

Int parseNoShowClientPhysics(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_showClientPhysics = FALSE;
	}
	return 1;
}

Int parseShowTerrainNormals(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_showTerrainNormals = TRUE;
	}
	return 1;
}

Int parseStateMachineDebug(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_stateMachineDebug = TRUE;
	}
	return 1;
}

Int parseJabber(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_jabberOn = TRUE;
	}
	return 1;
}

Int parseMunkee(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_munkeeOn = TRUE;
	}
	return 1;
}
#endif // defined(_DEBUG) || defined(_INTERNAL)

Int parseScriptDebug(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_scriptDebug = TRUE;
		TheWritableGlobalData->m_winCursors = TRUE;
	}
	return 1;
}

Int parseParticleEdit(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_particleEdit = TRUE;
		TheWritableGlobalData->m_winCursors = TRUE;
		TheWritableGlobalData->m_windowMode = WINDOW_MODE_WINDOWED;
		applyWindowMode();
	}
	return 1;
}


Int parseBuildMapCache(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_buildMapCache = true;
	}
	return 1;
}


#if defined(_DEBUG) || defined(_INTERNAL) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
Int parsePreload( char *args[], int num )
{
	if( TheWritableGlobalData )
		TheWritableGlobalData->m_preloadAssets = TRUE;
	return 1;
}
#endif


#if defined(_DEBUG) || defined(_INTERNAL) 
Int parseDisplayDebug(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_displayDebug = TRUE;
	}
	return 1;
}

Int parseFile(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_initialFile = args[1];
		ConvertShortMapPathToLongMapPath(TheWritableGlobalData->m_initialFile);
	}
	return 2;
}


Int parsePreloadEverything( char *args[], int num )
{
	if( TheWritableGlobalData )
	{
		TheWritableGlobalData->m_preloadAssets = TRUE;
		TheWritableGlobalData->m_preloadEverything = TRUE;
	}
	return 1;
}

Int parseLogAssets( char *args[], int num )
{
	if( TheWritableGlobalData )
	{
		FILE *logfile=fopen("PreloadedAssets.txt","w");
		if (logfile)	//clear the file
			fclose(logfile);
		TheWritableGlobalData->m_preloadReport = TRUE;
	}
	return 1;
}

/// begin stuff for VTUNE
Int parseVTune ( char *args[], int num )
{
	if( TheWritableGlobalData )
		TheWritableGlobalData->m_vTune = TRUE;
	return 1;
}
/// end stuff for VTUNE

#endif // defined(_DEBUG) || defined(_INTERNAL)

//=============================================================================
//=============================================================================

Int parseNoFX(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_useFX = FALSE;
	}
	return 1;
}

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseNoShroud(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_shroudOn = FALSE;
	}
	return 1;
}
#endif

Int parseForceBenchmark(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_forceBenchmark = TRUE;
	}
	return 1;
}

Int parseNoMoveCamera(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_disableCameraMovement = true;
	}
	return 1;
}

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseNoCinematic(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_disableCameraMovement = true;
		TheWritableGlobalData->m_disableMilitaryCaption = true;
		TheWritableGlobalData->m_disableCameraFade = true;
		TheWritableGlobalData->m_disableScriptedInputDisabling = true;
	}
	return 1;
}
#endif

Int parseSync(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheDebugIgnoreSyncErrors = true;
	}
	return 1;
}

Int parseNoShellMap(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_shellMapOn = FALSE;
	}
	return 1;
}

// Turn terrain collision on for every particle system at once.  The shipped
// ParticleSystem.ini lives inside INIZH.big and a loose copy would have to replace the whole
// file, so this is how the effect gets seen before any INI is written.
// ponytail: a preview switch - the real answer is GroundCollision per system in the INI
Int parseParticleBounce(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_particleGroundBounce = TRUE;
	}
	return 1;
}

Int parseNoShaders(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_chipSetType = 1;	//force to a voodoo card which uses least amount of features.
	}
	return 1;
}

#if (defined(_DEBUG) || defined(_INTERNAL))
Int parseNoLogo(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_playIntro = FALSE;
		TheWritableGlobalData->m_afterIntro = TRUE;
		TheWritableGlobalData->m_playSizzle = FALSE;
	}
	return 1;
}
#endif

Int parseNoSizzle( char *args[], int )
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_playSizzle = FALSE;
	}
	return 1;
}

Int parseShellMap(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_shellMapName = args[1];
	}
	return 2;
}

Int parseNoWindowAnimation(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_animateWindows = FALSE;
	}
	return 1;
}

Int parseWinCursors(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_winCursors = TRUE;
	}
	return 1;
}

Int parseQuickStart( char *args[], int num )
{
#if (defined(_DEBUG) || defined(_INTERNAL))
  parseNoLogo( args, num );
#else
	//Kris: Patch 1.01 -- Allow release builds to skip the sizzle video, but still force the EA logo to show up.
	//This is for legal reasons.
	parseNoSizzle( args, num );
#endif
	parseNoShellMap( args, num );
	parseNoWindowAnimation( args, num );
	return 1;
}

Int parseConstantDebug( char *args[], int num )
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_constantDebugUpdate = TRUE;
	}
	return 1;
}

#if (defined(_DEBUG) || defined(_INTERNAL))
Int parseExtraLogging( char *args[], int num )
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_extraLogging = TRUE;
	}
	return 1;
}
#endif

//-allAdvice feature
/*
Int parseAllAdvice( char *args[], int num )
{
	if( TheWritableGlobalData )
	{
		TheWritableGlobalData->m_allAdvice = TRUE;
	}
	return 1;
}
*/

Int parseShowTeamDot( char *args[], int num )
{
	if( TheWritableGlobalData )
	{
		TheWritableGlobalData->m_showTeamDot = TRUE;
	}
	return 1;
}


#if defined(_DEBUG) || defined(_INTERNAL)
Int parseSelectAll( char *args[], int num )
{
	if( TheWritableGlobalData )
	{
		TheWritableGlobalData->m_allowUnselectableSelection = TRUE;
	}
	return 1;
}

Int parseRunAhead( char *args[], Int num )
{
	if (num > 2)
	{
		MIN_RUNAHEAD = atoi(args[1]);
		MAX_FRAMES_AHEAD = atoi(args[2]);
		FRAME_DATA_LENGTH = (MAX_FRAMES_AHEAD + 1)*2;
	}
	return 3;
}
#endif


Int parseSeed(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_fixedSeed = atoi(args[1]);
	}
	return 2;
}

Int parseAutoSkirmish(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		Int players = atoi(args[1]);
		if (players < 2)
			players = 2;
		if (players > MAX_SLOTS)
			players = MAX_SLOTS;
		TheWritableGlobalData->m_autoSkirmishPlayers = players;
	}
	return 2;
}

Int parseAIDifficulty(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		//
		// The three rungs of the ladder. Anything unrecognised is still the top rung, so a batch
		// script naming a rung that no longer exists gets the hardest AI rather than the easiest.
		//
		AsciiString difficulty = args[1];
		if (difficulty.compareNoCase("easy") == 0)
			TheWritableGlobalData->m_autoSkirmishAIState = SLOT_EASY_AI;
		else if (difficulty.compareNoCase("medium") == 0 || difficulty.compareNoCase("med") == 0)
			TheWritableGlobalData->m_autoSkirmishAIState = SLOT_MED_AI;
		else
			TheWritableGlobalData->m_autoSkirmishAIState = SLOT_BRUTAL_AI;
	}
	return 2;
}

/** -aidiff2 <name>: give the odd-numbered skirmish slots a different rung from -aidiff.
	*
	* Without this the batch runner can only play a rung against itself, which says nothing about
	* whether the ladder is a ladder.  The whole no-cheat design (AI-ROADMAP.md D6) rests on higher
	* rungs actually beating lower ones through better decisions, and that is not a claim to make
	* without measuring it. */
Int parseAIDifficulty2(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		AsciiString difficulty = args[1];
		if (difficulty.compareNoCase("easy") == 0)
			TheWritableGlobalData->m_autoSkirmishAIStateOdd = SLOT_EASY_AI;
		else if (difficulty.compareNoCase("medium") == 0 || difficulty.compareNoCase("med") == 0)
			TheWritableGlobalData->m_autoSkirmishAIStateOdd = SLOT_MED_AI;
		else
			TheWritableGlobalData->m_autoSkirmishAIStateOdd = SLOT_BRUTAL_AI;
	}
	return 2;
}

Int parseObserver(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_autoSkirmishObserver = TRUE;
	}
	return 1;
}

Int parseIncrAGPBuf(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_incrementalAGPBuf = TRUE;
	}
	return 1;
}

Int parseNetMinPlayers(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_netMinPlayers = atoi(args[1]);
	}
	return 2;
}

Int parsePlayStats(char *args[], int num)
{
	if (TheWritableGlobalData  && num > 1)
	{
		TheWritableGlobalData->m_playStats  = atoi(args[1]);
	}
	return 2;
}

Int parseDemoLoadScreen(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_loadScreenDemo = TRUE;
	}
	return 1;
}

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseSaveStats(char *args[], int num)
{
	if (TheWritableGlobalData  && num > 1)
	{
		TheWritableGlobalData->m_saveStats = TRUE;
		TheWritableGlobalData->m_baseStatsDir = args[1];
	}
	return 2;
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseSaveAllStats(char *args[], int num)
{
	if (TheWritableGlobalData  && num > 1)
	{
		TheWritableGlobalData->m_saveStats = TRUE;
		TheWritableGlobalData->m_baseStatsDir = args[1];
		TheWritableGlobalData->m_saveAllStats = TRUE;
	}
	return 2;
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseLocalMOTD(char *args[], int num)
{
	if (TheWritableGlobalData  && num > 1)
	{
		TheWritableGlobalData->m_useLocalMOTD = TRUE;
		TheWritableGlobalData->m_MOTDPath = args[1];
	}
	return 2;
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseCameraDebug(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_debugCamera = TRUE;
	}
	return 1;
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseBenchmark(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_benchmarkTimer = atoi(args[1]);
		TheWritableGlobalData->m_playStats  = atoi(args[1]);
	}
	return 2;
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
#ifdef DUMP_PERF_STATS
Int parseStats(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_dumpStatsAtInterval = TRUE;
		TheWritableGlobalData->m_statsInterval  = atoi(args[1]);
	}
	return 2;
}
#endif
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseIgnoreAsserts(char *args[], int num)
{
	if (TheWritableGlobalData && num > 0)
	{
		TheWritableGlobalData->m_debugIgnoreAsserts = true;
	}
	return 1;
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseIgnoreStackTrace(char *args[], int num)
{
	if (TheWritableGlobalData && num > 0)
	{
		TheWritableGlobalData->m_debugIgnoreStackTrace = true;
	}
	return 1;
}
#endif

/* -fps <n> is the game speed itself: the rate the logic tick is paced at, the command-line twin of
	 the skirmish menu's game speed slider.  MSG_NEW_GAME only honours 1..1000. */

Int parseFPSLimit(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_framesPerSecondLimit = atoi(args[1]);
	}
	return 2;
}

/* -noFPSLimit uncaps the *renderer*, not the simulation.  It used to raise
	 m_framesPerSecondLimit to 30000 because the old loop ran one logic frame per pass, so the only
	 way to render freely was to let the logic run freely too - and the game then played at whatever
	 speed the machine managed.  GameEngine::update() now paces the logic tick against wall clock
	 and GameEngine::execute() never sleeps for a frame budget, so rendering is already uncapped
	 unconditionally and m_framesPerSecondLimit means game speed and nothing else.  Raising it here
	 would just run the match in fast-forward: superweapon and build timers count real seconds off a
	 clock ticking a thousand times too fast.  Use -fps to ask for that on purpose. */
Int parseNoFPSLimit(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_useFpsLimit = false;
	}
	return 1;
}

/* -headless is the unattended-run switch: no frame is ever drawn, the logic tick runs flat out
	 instead of being paced against wall clock, audio is silenced, and the process quits by itself
	 the moment the match is decided.  A window and a D3D device are still created - W3D reaches into
	 drawables and the asset manager throughout, so a true null display is a much larger change than
	 skipping the one draw call is worth.  That window is forced windowed and tiny: nothing is ever
	 drawn into it, and a fullscreen device would take the desktop's display mode away from whoever
	 is using the machine while the batch runs.  -xres/-yres after -headless still win if a run
	 wants a real back buffer.  Pair it with -autoskirmish and -observer for a soak run.

	 Audio goes off because at several hundred logic frames a second the game hands the mixer a few
	 thousand events a second that nobody will hear; -headless is for a machine, not a listener. */
enum { HEADLESS_RESOLUTION = 100 };	// big enough for a legal back buffer, small enough to ignore

Int parseHeadless(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_headless = TRUE;
		// windowed, and plainly so: a borderless Options.ini would otherwise hand a batch run the
		// whole display to draw a picture nobody is looking at
		TheWritableGlobalData->m_windowMode = WINDOW_MODE_WINDOWED;
		applyWindowMode();
		TheWritableGlobalData->m_xResolution = HEADLESS_RESOLUTION;
		TheWritableGlobalData->m_yResolution = HEADLESS_RESOLUTION;
		TheWritableGlobalData->m_audioOn = FALSE;
		TheWritableGlobalData->m_musicOn = FALSE;
		TheWritableGlobalData->m_soundsOn = FALSE;
		TheWritableGlobalData->m_speechOn = FALSE;
		TheWritableGlobalData->m_videoOn = FALSE;
	}
	return 1;
}

/* -maxframes <n> bounds a headless run in logic frames rather than in seconds, so the cutoff is
	 the same on every machine and in every replay.  A stalemate between eight brutal AIs is a real
	 outcome and it does not end on its own. */
Int parseMaxGameFrames(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_maxGameFrames = atoi(args[1]);
	}
	return 2;
}

/* -screenshot <n>: save one picture of the running game at logic frame n.
	 *
	 * F12 has always taken one, and a key is no use to anything that runs on its own. A renderer
	 * change with no picture to compare against can only be argued about, which is why a row of
	 * graphics work in the upstream ledger sits unclosed - not because the code is hard, because
	 * nobody could see the result. Combine with -autoskirmish, -map and -maxframes; -headless draws
	 * nothing and says so. The file goes next to the save games, as sshotNNN.bmp. */
Int parseScreenShot(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		Int frame = atoi(args[1]);
		if (frame < 1)
			frame = 1;
		TheWritableGlobalData->m_screenShotFrame = frame;
	}
	return 2;
}

/* -autocamera [seconds]: every so often, put the camera wherever the fighting is.
	 *
	 * A soak run watches from a free camera that never moves, and a camera that never moves is the
	 * one thing a real player's never is.  Everything a moving camera drags in behind it - the
	 * terrain window scrolling, shroud updates, models and textures loading the first time they come
	 * on screen - is invisible to a run that stares at one spot, which is exactly the blind spot a
	 * stutter likes to live in.  Default 5 seconds if no number is given. */
/* -msaa [N]: multisampled back buffer.  Bare means 4x, a number means N (2..16), and anything the
	 device will not give is degraded on its own inside DX8Wrapper.  It is stored as the same index
	 the options menu writes to Options.ini, so the switch and the setting are one value; the switch
	 wins because the command line is parsed after the preferences file. */
Int parseMSAA(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		unsigned samples = 4;	// a bare -msaa means 4x
		Int consumed = 1;
		if (num > 1 && args[1] && args[1][0] >= '0' && args[1][0] <= '9')
		{
			samples = (unsigned)atoi(args[1]);
			consumed = 2;
		}
		TheWritableGlobalData->m_msaaLevel = msaaLevelForSamples(samples);
		return consumed;
	}
	return 1;
}

/* -camera <x> <y>: point the camera at one map position and leave it there.
	 *
	 * -screenshot only made a picture; it could not say of what.  The camera starts at the local
	 * player's own base, so anything the player does not own - a river, a bridge, a piece of terrain
	 * a shader change is about - cannot be got into the frame from a script at all, and -autocamera
	 * follows the fighting, which is somewhere else again.  Map coordinates, the same ones the log
	 * prints; the ground height is looked up. */
Int parseCameraLook(char *args[], int num)
{
	if (TheWritableGlobalData && num > 2 && args[1] && args[2])
	{
		TheWritableGlobalData->m_cameraLook.x = (Real)atof(args[1]);
		TheWritableGlobalData->m_cameraLook.y = (Real)atof(args[2]);
		TheWritableGlobalData->m_cameraLookSet = TRUE;
		return 3;
	}
	return 1;
}

Int parseAutoCamera(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		Int seconds = 5;
		Int consumed = 1;
		// The value is optional, so only take the next word if it is actually a number.
		if (num > 1 && args[1] && args[1][0] >= '0' && args[1][0] <= '9')
		{
			seconds = atoi(args[1]);
			consumed = 2;
		}
		if (seconds < 1)
			seconds = 1;
		TheWritableGlobalData->m_autoCameraSeconds = seconds;
		return consumed;
	}
	return 1;
}

/* -tracemove [id]: one line a frame, for one unit, naming every value that can zero its speed.
	 *
	 * A jam is an argument between four numbers - the speed the unit wants, the ceiling a collision
	 * put on it, the decaying bump limit and the frames it has spent blocked - and none of them can
	 * be seen from outside the object.  The aggregate counters say a run had 40000 blocked frames;
	 * they cannot say which line of code stopped the tank.  With no id the trace attaches itself to
	 * the first unit that gets blocked and follows that one for the rest of the run, which is what
	 * you want when the jam is somewhere in a batch and nobody knows any object's id in advance. */
Int parseTraceMove(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		Int id = -1; // no number: follow the first unit that gets blocked
		Int consumed = 1;
		// The value is optional, so only take the next word if it is actually a number.
		if (num > 1 && args[1] && args[1][0] >= '0' && args[1][0] <= '9')
		{
			id = atoi(args[1]);
			consumed = 2;
		}
		if (id == 0)
			id = -1; // 0 is not a valid object id, and it is how the feature is switched off
		TheWritableGlobalData->m_traceMoveID = id;
		return consumed;
	}
	return 1;
}

/* -aislice <n> lets a unit's AI decide once every n logic frames instead of every one.

	 This is the only lever left that reduces what the AI does rather than how fast it does it, and
	 it is off by default because what it spends is reaction time, not milliseconds: a unit acquires
	 a target and obeys an order up to n-1 frames late. Movement is not sliced - the locomotor still
	 runs every frame, so units keep driving smoothly along the route they already have; it is the
	 deciding that waits. The offset is the object id, so the units do not all think on the same
	 frame, and an id is assigned in creation order and identical on every machine, which keeps this
	 inside the CRC. */
Int parseAISlice(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1 && args[1])
	{
		Int slice = atoi(args[1]);
		if (slice < 1)
			slice = 1;
		if (slice > 8)
			slice = 8;			// past this a unit is visibly asleep, and it is a measuring tool anyway
		TheWritableGlobalData->m_aiSliceFrames = slice;
		return 2;
	}
	return 1;
}

/* -noflowpath takes the flow model back out and prices terrain only, the way retail does.

	 It exists to be measured against. A change to how a route is costed is argued with a batch of
	 matches, and a batch is only an argument if both halves of it are the same binary: two builds
	 differ in the compiler's mood as well as in the change. So the clearance charge, the traffic
	 charge and the crossing charge all hang off this one switch, and ai-batch.ps1 runs the same
	 exe twice. The maps are still built and still maintained under it - they cost almost nothing
	 to keep and turning them off as well would measure two changes at once. */
Int parseNoFlowPath(char *args[], int num)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_noFlowPath = TRUE;
	return 1;
}

/* -nolanes puts every unit back on the centre line of its route.

	 The flow model decides where a route goes; lanes decide where across it a unit drives, which
	 is a separate question and needs its own baseline. With this on, computePointOnPath steers at
	 the centre the way retail always did, nothing measures the width of the ground beside the
	 route, and a unit stuck behind a slower one waits instead of sliding past. Same reasoning as
	 -noflowpath: one binary, run twice. */
Int parseNoLanePath(char *args[], int num)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_noLanePath = TRUE;
	return 1;
}

/* -nomomentum prices a turn the way retail does: 4, 8 or 16 whatever is turning.

	 The search charges what the swing actually costs this hull instead - its own speed over its own
	 turn rate - and charges the first step against the direction the unit is already pointing, which
	 retail charged not at all. That is the difference between a route and a route a tank can drive.
	 Same reasoning as the two switches above: the baseline has to be the same binary. */
Int parseNoMomentumPath(char *args[], int num)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_noMomentumPath = TRUE;
	return 1;
}

/* -showlanes draws the band model on top of the world.

	 A movement change that measures well in a batch and cannot be seen in a game is a change nobody
	 has any reason to believe, and two different failures - a lane that was never handed out, and a
	 lane that was handed out and then refused - look exactly alike from the camera. So the overlay
	 draws both halves separately: what the ordering group asked for, and what the unit ended up
	 steering at. Release, because the machine that has the complaint is the one running Release. */
Int parseShowLanes(char *args[], int num)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_showLanes = TRUE;
	return 1;
}

/* -crowd turns on the crowd model, in one switch, so that it can be argued with.

	 It is not one rule but a stack of them - the corridor with its measured width, the lane handed
	 out as a distance rather than a share, right of way by body size, giving way to something bigger
	 closing on you, passing a unit that is actually slower rather than one whose engine is, fanning
	 out only while held up, and easing off through a bend. Every one of those changes what a group
	 looks like crossing a map, and shipping them one at a time means eight batches and eight
	 opinions about which of them did the damage. So they land together behind one flag, off by
	 default: the same exe run twice is the before and the after, and the argument is about the whole
	 model rather than about any single constant inside it. */
Int parseCrowdModel(char *args[], int num)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_crowdModel = TRUE;
	return 1;
}

/* -groupdrill <n> gives every player's army a group order every n frames.

	 A skirmish AI moves its teams one unit at a time, so a self-play batch contains no group orders
	 at all and measures nothing the crowd model does. This puts them in: the same call a right-click
	 makes, over the whole army, corner to corner. The match becomes meaningless - the win rate under
	 this switch says nothing about anything - and the blocked unit-frames become the first honest
	 measurement of group movement this fork has. 600 frames, twenty seconds, is long enough for an
	 army to cross a generated map. */
Int parseGroupDrill(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		Int frames = 600;
		Int eaten = 1;
		if (num > 1 && args[1] && args[1][0] != '-')
		{
			frames = atoi(args[1]);
			eaten = 2;
		}
		if (frames < LOGICFRAMES_PER_SECOND)
			frames = LOGICFRAMES_PER_SECOND;		// an order a frame is not a drill, it is a stutter
		TheWritableGlobalData->m_groupDrill = frames;
		return eaten;
	}
	return 1;
}

/* -teams <n> splits an -autoskirmish lobby into n allied teams instead of a free-for-all.

	 Free-for-all and 4v4 are not the same load and not the same game. Eight players each fighting
	 seven others spread the fighting over the whole map; two sides of four put every unit on one of
	 two fronts, which is where units bunch up, where the pathfinder earns its money, and where a
	 player says the game is chugging. Slots are handed out in blocks - the first n-th of them are
	 team 0, the next team 1 - which is how the lobby numbers them, and GameLogic's own alliance
	 setup does the rest from each slot's team number. */
Int parseTeams(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1 && args[1])
	{
		Int teams = atoi(args[1]);
		if (teams < 1)
			teams = 1;								// one team is a free-for-all, same as not asking
		if (teams > MAX_SLOTS)
			teams = MAX_SLOTS;
		TheWritableGlobalData->m_autoSkirmishTeams = teams;
		return 2;
	}
	return 1;
}

/* -peacetime <minutes> gives an -autoskirmish match the lobby's peace time without a lobby.

	 The option itself is a host setting picked from a combo box in the three lobby screens, and none
	 of those exist in an unattended run - so the only way to watch what the truce does to a match, or
	 to prove a change to it did not desync a replay, is to hand the same number to the slot list the
	 command line builds. Clamped the same way GameInfo::setPeaceTime clamps the wire value. */
Int parsePeaceTime(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1 && args[1])
	{
		Int minutes = atoi(args[1]);
		if (minutes < 0)
			minutes = 0;
		if (minutes > 60)
			minutes = 60;
		TheWritableGlobalData->m_peaceTime = minutes;
		return 2;
	}
	return 1;
}

/* -slowframe <ms> lowers the bar a logic frame has to clear before it logs its own breakdown.

	 The default of 20ms is a stutter hunt: it catches the frames a player would notice. Chasing a
	 subsystem's cost is a different search - the question is not "which frames were terrible" but
	 "which frames did this cost anything at all" - and for that the bar wants to be a few
	 milliseconds. Every frame over it writes a line and a flush, so a low bar on a long run is a
	 large log and a slower run; it is a measuring tool, not a setting. */
Int parseSlowFrame(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1 && args[1])
	{
		const Real ms = (Real)atof(args[1]);
		if (ms > 0.0f)
			TheWritableGlobalData->m_slowFrameMS = ms;
		return 2;
	}
	return 1;
}

/* -netgame <ip>[,<ip>...] starts a LAN game against those addresses with no lobby in front of it,
	 and -netslot <n> says which of them this copy is.  Every machine is given the same slot list in
	 the same order, which is all the lobby ever agreed on: the slot list, the map and the seed.  A
	 network game is the only kind of game whose replay exercises the multiplayer paths (a CRC per
	 NET_CRC_INTERVAL frames, real remote players, a local slot that is not 0), so without this there
	 is no way to produce one unattended. */
Int parseNetGame(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_netGameHosts = args[1];
	}
	return 2;
}

Int parseNetSlot(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		Int slot = atoi(args[1]);
		if (slot < 0)
			slot = 0;
		if (slot >= MAX_SLOTS)
			slot = MAX_SLOTS - 1;
		TheWritableGlobalData->m_netGameLocalSlot = slot;
	}
	return 2;
}

/* -replay <file> plays a replay back without the menus, the way -autoskirmish starts a match
	 without them.  The only route into playback was ReplayMenu's list box and the _DEBUG/_INTERNAL
	 -file switch, so a Release build could record a game and then had no way to play it back
	 unattended - which is exactly what checking that a multiplayer replay still plays needs.

	 The name is resolved against the replay directory by RecorderClass::readReplayHeader, so pass
	 the bare file name; the extension is optional. */
Int parseReplay(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		AsciiString name = args[1];
		if (!name.endsWithNoCase(".rep"))
			name.concat(".rep");
		TheWritableGlobalData->m_initialFile = name;
	}
	return 2;
}

/* -loadsave <file> opens a save game without the menus, the same way -replay opens a replay. The
	 only route into a save was the Load menu, so there was no way to get a machine straight back
	 into a known world - which is what a bug that only shows up ten minutes into a mission needs.
	 The name is resolved against the save directory; the extension is optional. */
Int parseLoadSave(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		AsciiString name = args[1];
		if (!name.endsWithNoCase(".sav"))
			name.concat(".sav");
		TheWritableGlobalData->m_initialFile = name;
	}
	return 2;
}

Int parseDumpAssetUsage(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_dumpAssetUsage = true;
	}
	return 1;
}

Int parseJumpToFrame(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		parseNoFPSLimit(args, num);
		TheWritableGlobalData->m_noDraw = atoi(args[1]);
		return 2;
	}
	return 1;
}

Int parseUpdateImages(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_shouldUpdateTGAToDDS = TRUE;
	}
	return 1;
}

Int parseMod(char *args[], Int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		AsciiString modPath = args[1];
		if (strchr(modPath.str(), ':') || modPath.startsWith("/") || modPath.startsWith("\\"))
		{
			// full path passed in.  Don't append base path.
		}
		else
		{
			modPath.format("%s%s", TheGlobalData->getPath_UserData().str(), args[1]);
		}
		DEBUG_LOG(("Looking for mod '%s'\n", modPath.str()));

		if (!TheLocalFileSystem->doesFileExist(modPath.str()))
		{
			DEBUG_LOG(("Mod does not exist.\n"));
			return 2; // no such file/dir.
		}

		// now check for dir-ness
		struct _stat statBuf;
		if (_stat(modPath.str(), &statBuf) != 0)
		{
			DEBUG_LOG(("Could not _stat() mod.\n"));
			return 2; // could not stat the file/dir.
		}

		if (statBuf.st_mode & _S_IFDIR)
		{
			if (!modPath.endsWith("\\") && !modPath.endsWith("/"))
				modPath.concat('\\');
			DEBUG_LOG(("Mod dir is '%s'.\n", modPath.str()));
			TheWritableGlobalData->m_modDir = modPath;
		}
		else
		{
			DEBUG_LOG(("Mod file is '%s'.\n", modPath.str()));
			TheWritableGlobalData->m_modBIG = modPath;
		}

		return 2;
	}
	return 1;
}

static CommandLineParam params[] =
{
	{ "-noshellmap", parseNoShellMap },
	{ "-win", parseWin },
	{ "-borderless", parseBorderless },
	{ "-xres", parseXRes },
	{ "-yres", parseYRes },
	{ "-fullscreen", parseNoWin },
	{ "-fullVersion", parseFullVersion },
	{	"-particleEdit", parseParticleEdit },
	{ "-scriptDebug", parseScriptDebug },
	{ "-playStats", parsePlayStats },
	{ "-mod", parseMod },
	{ "-noshaders", parseNoShaders },
	{ "-particlebounce", parseParticleBounce },
	{ "-quickstart", parseQuickStart },

	{ "-packetloss", parsePacketLoss },
	{ "-latAvg", parseLatencyAverage },
	{ "-latAmp", parseLatencyAmplitude },
	{ "-latPeriod", parseLatencyPeriod },
	{ "-latNoise", parseLatencyNoise },

#if (defined(_DEBUG) || defined(_INTERNAL))
	{ "-noaudio", parseNoAudio },
	{ "-nomusic", parseNoMusic },
	{ "-novideo", parseNoVideo },
	{ "-noLogOrCrash", parseNoLogOrCrash },
	{ "-FPUPreserve", parseFPUPreserve },
	{ "-benchmark", parseBenchmark },
#ifdef DUMP_PERF_STATS
	{ "-stats", parseStats }, 
#endif
  { "-saveStats", parseSaveStats },
	{ "-localMOTD", parseLocalMOTD },
	{ "-UseCSF", parseUseCSF },
	{ "-NoInputDisable", parseNoInputDisable },
	{ "-DebugCRCFromFrame", parseDebugCRCFromFrame },
	{ "-DebugCRCUntilFrame", parseDebugCRCUntilFrame },
	{ "-KeepCRCSaves", parseKeepCRCSave },
	{ "-CRCLogicModuleData", parseCRCLogicModuleData },
	{ "-CRCClientModuleData", parseCRCClientModuleData },
	{ "-ClientDeepCRC", parseClientDeepCRC },
	{ "-VerifyClientCRC", parseVerifyClientCRC },
	{ "-LogObjectCRCs", parseLogObjectCRCs },
	{ "-saveAllStats", parseSaveAllStats },
	{ "-NetCRCInterval", parseNetCRCInterval },
	{ "-ReplayCRCInterval", parseReplayCRCInterval },
	{ "-noDraw", parseNoDraw },
	{ "-nomilcap", parseNoMilCap },
	{ "-nofade", parseNoFade },
	{ "-nomovecamera", parseNoMoveCamera },
	{ "-nocinematic", parseNoCinematic },
	{ "-noViewLimit", parseNoViewLimit },
	{ "-lowDetail", parseLowDetail },
	{ "-noDynamicLOD", parseNoDynamicLOD },
	{ "-noStaticLOD", parseNoStaticLOD },
	{ "-useWaveEditor", parseUseWaveEditor },
	{ "-wireframe", parseWireframe },
	{ "-showCollision", parseShowCollision },
	{ "-noShowClientPhysics", parseNoShowClientPhysics },
	{ "-showTerrainNormals", parseShowTerrainNormals },
	{ "-stateMachineDebug", parseStateMachineDebug },
	{ "-jabber", parseJabber },
	{ "-munkee", parseMunkee },
	{ "-displayDebug", parseDisplayDebug },
	{ "-file", parseFile },
  
//	{ "-preload", parsePreload },
	
  { "-preloadEverything", parsePreloadEverything },
	{ "-logAssets", parseLogAssets },
	{ "-netMinPlayers", parseNetMinPlayers },
	{ "-DemoLoadScreen", parseDemoLoadScreen },
	{ "-cameraDebug", parseCameraDebug },
	{ "-ignoreAsserts", parseIgnoreAsserts },
	{ "-ignoreStackTrace", parseIgnoreStackTrace },
	{ "-logToCon", parseLogToConsole },
	{ "-vTune", parseVTune },
	{ "-selectTheUnselectable", parseSelectAll },
	{ "-RunAhead", parseRunAhead },
	{ "-noshroud", parseNoShroud },
	{ "-forceBenchmark", parseForceBenchmark },
	{ "-buildmapcache", parseBuildMapCache },
	{ "-noshadowvolumes", parseNoShadows },
	{ "-nofx", parseNoFX },
	{ "-ignoresync", parseSync },
	{ "-nologo", parseNoLogo },
	{ "-shellmap", parseShellMap },
	{ "-noShellAnim", parseNoWindowAnimation },
	{ "-winCursors", parseWinCursors },
	{ "-constantDebug", parseConstantDebug },
	{ "-noagpfix", parseIncrAGPBuf },
	{ "-dumpAssetUsage", parseDumpAssetUsage },
	{ "-jumpToFrame", parseJumpToFrame },
	{ "-updateImages", parseUpdateImages },
	{ "-showTeamDot", parseShowTeamDot },
	{ "-extraLogging", parseExtraLogging },

#endif

	/* Outside the _DEBUG/_INTERNAL block on purpose: an unattended Release run is driven entirely
		 from the command line.  -autoskirmish needs a map to play, -seed makes the run repeatable,
		 -noFPSLimit lets the renderer run free, and -fps is the one knob that changes how fast the
		 match itself is simulated - the command-line twin of the skirmish menu's game speed slider. */
	{ "-map", parseMapName },
	{ "-randommap", parseRandomMap },
	{ "-seed", parseSeed },
	{ "-noFPSLimit", parseNoFPSLimit },
	{ "-fps", parseFPSLimit },
	{ "-autoskirmish", parseAutoSkirmish },
	{ "-aidiff", parseAIDifficulty },
	{ "-aidiff2", parseAIDifficulty2 },
	{ "-observer", parseObserver },
	{ "-headless", parseHeadless },
	{ "-maxframes", parseMaxGameFrames },
	{ "-screenshot", parseScreenShot },
	{ "-msaa", parseMSAA },
	{ "-autocamera", parseAutoCamera },
	{ "-camera", parseCameraLook },
	{ "-tracemove", parseTraceMove },
	{ "-slowframe", parseSlowFrame },
	{ "-teams", parseTeams },
	{ "-peacetime", parsePeaceTime },
	{ "-aislice", parseAISlice },
	{ "-noflowpath", parseNoFlowPath },
	{ "-nolanes", parseNoLanePath },
	{ "-nomomentum", parseNoMomentumPath },
	{ "-showlanes", parseShowLanes },
	{ "-crowd", parseCrowdModel },
	{ "-groupdrill", parseGroupDrill },
	{ "-replay", parseReplay },
	{ "-loadsave", parseLoadSave },
	{ "-netgame", parseNetGame },
	{ "-netslot", parseNetSlot },

	//-allAdvice feature
	//{ "-allAdvice", parseAllAdvice },

#if defined(_DEBUG) || defined(_INTERNAL) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
  { "-preload", parsePreload },
#endif


};

// parseCommandLine ===========================================================
/** Parse command-line parameters. */
//=============================================================================
void parseCommandLine(int argc, char *argv[])
{
	// To parse command-line parameters, we loop through a table holding arguments
	// and functions to handle them.  Comparisons can be case-(in)sensitive, and
	// can check the entire string (for testing the presence of a flag) or check
	// just the start (for a key=val argument).  The handling function can also
	// look at the next argument(s), to accomodate multi-arg parameters, e.g. "-p 1234".
	int arg=1, param;
	Bool found;

#ifdef DEBUG_LOGGING
	DEBUG_LOG(("Command-line args:"));
	int debugFlags = DebugGetFlags();
	DebugSetFlags(debugFlags & ~DEBUG_FLAG_PREPEND_TIME); // turn off timestamps
	for (arg=1; arg<argc; arg++)
	{
		DEBUG_LOG((" %s", argv[arg]));
	}
	DEBUG_LOG(("\n"));
	DebugSetFlags(debugFlags); // turn timestamps back on iff they were on before
	arg = 1;
#endif // DEBUG_LOGGING

	while (arg<argc)
	{
		// Look at arg #i
		found = false;
		for (param=0; !found && param<sizeof(params)/sizeof(params[0]); ++param)
		{
			int len = strlen(params[param].name);
			int len2 = strlen(argv[arg]);
			if (len2 != len)
				continue;
			if (!strnicmp(argv[arg], params[param].name, len))
			{
				arg += params[param].func(argv+arg, argc-arg);
				found = true;
			}
		}	// for
		if (!found)
		{
			arg++;
		}
	}

	TheArchiveFileSystem->loadMods();
}


