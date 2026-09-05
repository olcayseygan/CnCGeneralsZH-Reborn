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

// GameEngine.cpp /////////////////////////////////////////////////////////////////////////////////
// Implementation of the Game Engine singleton
// Author: Michael S. Booth, April 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ActionManager.h"
#include "Common/AudioAffect.h"
#include "Common/BuildAssistant.h"
#include "Common/CRCDebug.h"
#include "Common/Radar.h"
#include "Common/PlayerTemplate.h"
#include "Common/Team.h"
#include "Common/PlayerList.h"
#include "Common/Player.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/MessageStream.h"
#include "Common/ThingFactory.h"
#include "Common/File.h"
#include "Common/FileSystem.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/LocalFileSystem.h"
#include "Common/CDManager.h"
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/JobSystem.h"
#include "GameLogic/TerrainLogic.h"		// -autocamera needs the map extent and the ground height
#include "Common/RandomValue.h"
#include "Common/NameKeyGenerator.h"
#include "Common/ModuleFactory.h"
#include "Common/Debug.h"
#include "Common/GameState.h"
#include "Common/GameStateMap.h"
#include "Common/Science.h"
#include "Common/FunctionLexicon.h"
#include "Common/CommandLine.h"
#include "Common/DamageFX.h"
#include "Common/MultiplayerSettings.h"
#include "Common/Recorder.h"
#include "Common/SpecialPower.h"
#include "Common/TerrainTypes.h"
#include "Common/Upgrade.h"
#include "Common/UserPreferences.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"
#include "Common/GameLOD.h"
#include "Common/Registry.h"
#include "Common/GameCommon.h"	// FOR THE ALLOW_DEBUG_CHEATS_IN_RELEASE #define

#include "GameLogic/Armor.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"		// the headless run summary reports the match's pathfinder totals
#include "GameLogic/CaveSystem.h"
#include "GameLogic/CrateSystem.h"
#include "GameLogic/Damage.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/RankInfo.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/SidesList.h"

#include "GameClient/Display.h"
#include "GameClient/FXList.h"
#include "GameClient/GameClient.h"
#include "GameClient/Keyboard.h"
#include "GameClient/Shell.h"
#include "GameClient/GameText.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/Water.h"
#include "GameClient/TerrainRoads.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/MapUtil.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/Drawable.h"
#include "GameClient/GUICallbacks.h"

#include "GameNetwork/GameInfo.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/WOLBrowser/WebBrowser.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/NetworkUtil.h"
#include "GameNetwork/GameSpy/GameResultsThread.h"

#include "Common/Version.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-------------------------------------------------------------------------------------------------

#ifdef DEBUG_CRC
class DeepCRCSanityCheck : public SubsystemInterface
{
public:
	DeepCRCSanityCheck() {}
	virtual ~DeepCRCSanityCheck() {}

	virtual void init(void) {}
	virtual void reset(void);
	virtual void update(void) {}

protected:
};

DeepCRCSanityCheck *TheDeepCRCSanityCheck = NULL;

void DeepCRCSanityCheck::reset(void)
{
	static Int timesThrough = 0;
	static UnsignedInt lastCRC = 0;

	AsciiString fname;
	fname.format("%sCRCAfter%dMaps.dat", TheGlobalData->getPath_UserData().str(), timesThrough);
	UnsignedInt thisCRC = TheGameLogic->getCRC( CRC_RECALC, fname );

	DEBUG_LOG(("DeepCRCSanityCheck: CRC is %X\n", thisCRC));
	DEBUG_ASSERTCRASH(timesThrough == 0 || thisCRC == lastCRC,
		("CRC after reset did not match beginning CRC!\nNetwork games won't work after this.\nOld: 0x%8.8X, New: 0x%8.8X",
		lastCRC, thisCRC));
	lastCRC = thisCRC;

	timesThrough++;
}
#endif // DEBUG_CRC

//-------------------------------------------------------------------------------------------------
/// The GameEngine singleton instance
GameEngine *TheGameEngine = NULL;

//-------------------------------------------------------------------------------------------------
SubsystemInterfaceList* TheSubsystemList = NULL;

//-------------------------------------------------------------------------------------------------
template<class SUBSYSTEM>
void initSubsystem(SUBSYSTEM*& sysref, AsciiString name, SUBSYSTEM* sys, Xfer *pXfer,  const char* path1 = NULL, 
									 const char* path2 = NULL, const char* dirpath = NULL)
{
	sysref = sys;
	TheSubsystemList->initSubsystem(sys, path1, path2, dirpath, pXfer, name);
}

//-------------------------------------------------------------------------------------------------
extern HINSTANCE ApplicationHInstance;  ///< our application instance
extern CComModule _Module;

//-------------------------------------------------------------------------------------------------
static void updateTGAtoDDS();

Int GameEngine::getFramesPerSecondLimit( void )
{
	return m_maxFPS;
}

//-------------------------------------------------------------------------------------------------
GameEngine::GameEngine( void )
{
	// Set the time slice size to 1 ms.
	timeBeginPeriod(1);

	// initialize to non garbage values
	m_maxFPS = 0;
	m_quitting = FALSE;
	m_isActive = FALSE;

	_Module.Init(NULL, ApplicationHInstance);
}

//-------------------------------------------------------------------------------------------------
GameEngine::~GameEngine()
{
	//extern std::vector<std::string>	preloadTextureNamesGlobalHack;
	//preloadTextureNamesGlobalHack.clear();

	// Debug.cpp puts an assertion up as a modal dialog only while the game is windowed, because a
	// dialog over a fullscreen device deadlocks. Teardown is neither: the device is on its way out
	// under us, and a dialog raised here hangs the process on exit with nothing on screen. Say we
	// are not windowed and every assertion from here on goes to the log instead.
	extern bool DX8Wrapper_IsWindowed;
	DX8Wrapper_IsWindowed = false;

	delete TheMapCache;
	TheMapCache = NULL;

//	delete TheShell;
//	TheShell = NULL;

	TheGameResultsQueue->endThreads();

	// Tear the game world down while every subsystem can still see every other one.
	// shutdownAll deletes in reverse registration order, which kills ThePlayerList and
	// TheRadar before TheTeamFactory and TheGameLogic - and Player::~Player NULLs the
	// owning player of each team prototype on its way out, so TeamFactory's own teardown
	// then walked dead pointers and faulted twice on every exit.  Each fault cost a full
	// dbghelp symbolization of an 80MB pdb: that was the several-second stall on quit.
	// resetAll() is the same call made between games, and PlayerList::reset() clears the
	// teams before the players, so afterwards shutdownAll has an empty world to free.
	TheSubsystemList->resetAll();

	TheSubsystemList->shutdownAll();
	delete TheSubsystemList;
	TheSubsystemList = NULL;

	delete TheNetwork;
	TheNetwork = NULL;

	delete TheCommandList;
	TheCommandList = NULL;

	delete TheNameKeyGenerator;
	TheNameKeyGenerator = NULL;

	delete TheFileSystem;
	TheFileSystem = NULL;

	if (TheGameLODManager)
		delete TheGameLODManager;

	Drawable::killStaticImages();

	_Module.Term();

	/* After everything that could still fork.  parallel_for never returns with work in flight, so
		 there is nothing to drain here - but a worker parked on the semaphore still has to be told
		 to leave, or the process waits on it at exit. */
	JobSystem::shutdown();

#ifdef PERF_TIMERS
	PerfGather::termPerfDump();
#endif

	// Restore the previous time slice for Windows.
	timeEndPeriod(1);
}

void GameEngine::setFramesPerSecondLimit( Int fps )
{
	DEBUG_LOG(("GameEngine::setFramesPerSecondLimit() - setting max fps to %d (TheGlobalData->m_useFpsLimit == %d)\n", fps, TheGlobalData->m_useFpsLimit));
	m_maxFPS = fps;
}

/* -replay <file>: the name the command line asked for, opened after init()'s resetAll().  See
	 the .rep branch in init() for why it cannot be opened where it is parsed. */
static AsciiString thePendingReplayFile;

/* -loadsave <file>: same deferral, same reason. A save game rebuilds the whole world, and doing
	 that before init()'s resetAll() would have resetAll tear it straight back down again. */
static AsciiString thePendingSaveFile;

/** Open the save game -loadsave named, once every subsystem has been reset.
	*
	* This is doLoadGame() in PopupSaveLoad.cpp without the menu: prepare a single player game, load,
	* and fall back to the shell if the load fails. */
static void startPendingSaveGame( void )
{
	AvailableGameInfo gameInfo;
	gameInfo.next = NULL;
	gameInfo.prev = NULL;
	// A leaf name, not a path: everything downstream calls getFilePathInSaveDirectory on it, so a
	// full path here gets the save directory glued in front of it a second time and loadGame then
	// answers SC_FILE_NOT_FOUND for a file that is plainly there.
	gameInfo.filename = thePendingSaveFile;

	/* Ask first. Neither getSaveGameInfoFromFile nor loadGame survives a name that is not there -
		 the first reads out of an xfer that never opened - and a file name typed on a command line is
		 exactly the sort of thing that is not there. Without this the switch faulted on a typo. */
	if (!TheGameState->doesSaveGameExist( thePendingSaveFile ))
	{
		DEBUG_LOG(("-loadsave: '%s' does not exist\n", gameInfo.filename.str()));
		// A run driven entirely from the command line has nobody at the keyboard, so dropping it
		// into the main menu means a process that never ends. Say what went wrong and stop.
		if (TheGlobalData->m_headless)
			TheGameEngine->setQuitting( TRUE );
		else
			TheWritableGlobalData->m_shellMapOn = TRUE;
		return;
	}

	// getSaveGameInfoFromFile opens the name it is handed as-is - the menu path gets away with a
	// leaf only because it is called from inside iterateSaveFiles, which has chdir'd into the save
	// directory first. Give it the path.
	TheGameState->getSaveGameInfoFromFile(
		TheGameState->getFilePathInSaveDirectory( thePendingSaveFile ), &gameInfo.saveGameInfo );

	TheGameLogic->prepareNewGame( GAME_SINGLE_PLAYER, DIFFICULTY_NORMAL, 0 );

	if (TheGameState->loadGame( gameInfo ) != SC_OK)
	{
		DEBUG_LOG(("-loadsave: '%s' could not be loaded\n", gameInfo.filename.str()));
		if (TheGameLogic->isInGame())
			TheGameLogic->clearGameData( FALSE );
		if (TheGlobalData->m_headless)
			TheGameEngine->setQuitting( TRUE );
		else
			TheWritableGlobalData->m_shellMapOn = TRUE;
	}
}

/** -----------------------------------------------------------------------------------------------
 * -autoskirmish <n>: build a skirmish slot list from the command line and launch it without going
 * through the menus.  This is reallyDoStart() in SkirmishGameOptionsMenu.cpp minus the GUI: the
 * slots only have to say who is occupied and who is an AI, because GameLogic::startNewGame resolves
 * a -1 faction, colour and start position itself (populateRandomSideAndColor,
 * populateRandomStartPosition).  Meant for unattended runs - eight AI players fighting at whatever
 * frame rate the machine gives, with the local slot watching.
 */
static void startAutoSkirmish( void )
{
	AsciiString mapName = TheGlobalData->m_mapName;
	if (mapName.isEmpty())
	{
		DEBUG_LOG(("-autoskirmish: no -map was given\n"));
		return;
	}

	const MapMetaData *md = TheMapCache->findMap( mapName );
	if (md == NULL)
	{
		DEBUG_LOG(("-autoskirmish: '%s' is not in the map cache\n", mapName.str()));
		return;
	}
	if (!md->m_isMultiplayer)
	{
		DEBUG_LOG(("-autoskirmish: '%s' is not a multiplayer map\n", mapName.str()));
		return;
	}

	Int numPlayers = TheGlobalData->m_autoSkirmishPlayers;
	if (numPlayers > md->m_numPlayers)
	{
		DEBUG_LOG(("-autoskirmish: '%s' holds %d players, not %d\n", mapName.str(), md->m_numPlayers, numPlayers));
		numPlayers = md->m_numPlayers;
	}

	if (TheSkirmishGameInfo == NULL)
	{
		TheSkirmishGameInfo = NEW SkirmishGameInfo;
	}
	TheSkirmishGameInfo->init();
	TheSkirmishGameInfo->clearSlotList();
	TheSkirmishGameInfo->reset();
	TheSkirmishGameInfo->enterGame();

	/* Watching costs no seat.  GameLogic::startNewGame always adds a "ReplayObserver" side after
		 the slots, and PlayerList::newGame makes the first human side the local player - so a slot
		 list with nothing but AI in it leaves that observer holding the camera, exactly the way a
		 replay does.  Spending a slot on the observer instead would cost a bot, because MAX_SLOTS is
		 8 and that is also the most start positions a map has. */
	const Bool observing = TheGlobalData->m_autoSkirmishObserver;
	UnicodeString localName;
	localName.translate( AsciiString( "Player" ) );
	for( Int i = 0; i < numPlayers; i++ )
	{
		GameSlot slot;
		if (i == 0 && !observing)
		{
			slot.setState( SLOT_PLAYER, localName );
			slot.setName( localName );
			slot.setPlayerTemplate( PLAYERTEMPLATE_RANDOM );
		}
		else
		{
			// -aidiff2 gives the odd slots a different rung, so a batch can play one against another
			Int state = TheGlobalData->m_autoSkirmishAIState;
			if ((i & 1) && TheGlobalData->m_autoSkirmishAIStateOdd != 0)
				state = TheGlobalData->m_autoSkirmishAIStateOdd;
			slot.setState( (SlotState)state );
			slot.setPlayerTemplate( PLAYERTEMPLATE_RANDOM );
		}
		slot.setColor( -1 );			// -1 is "random" to populateRandomSideAndColor
		slot.setStartPos( -1 );		// and to populateRandomStartPosition
		/* -teams splits the lobby into allied blocks: with eight players and two teams the first
			 four are team 0 and the rest team 1, the way the lobby numbers them. GameLogic's own
			 alliance pass reads the slot's team number and does the rest. Without it every slot is
			 -1, which is "no team", and everybody fights everybody. */
		Int teamNumber = -1;
		const Int teams = TheGlobalData->m_autoSkirmishTeams;
		if (teams > 1 && numPlayers >= teams)
		{
			const Int perTeam = (numPlayers + teams - 1) / teams;
			teamNumber = i / perTeam;
			if (teamNumber >= teams)
				teamNumber = teams - 1;		// an uneven split puts the remainder on the last team
		}
		slot.setTeamNumber( teamNumber );
		TheSkirmishGameInfo->setSlot( i, slot );
	}
	TheSkirmishGameInfo->setLocalIP( TheSkirmishGameInfo->getSlot(0)->getIP() );
	TheSkirmishGameInfo->setMap( mapName );

	/* -seed makes the whole run repeatable: the seed drives the factions, the colours, the start
		 positions and every logic random draw after them, so the same command line replays the same
		 match. */
	const Int seed = (TheGlobalData->m_fixedSeed >= 0) ? TheGlobalData->m_fixedSeed : GetTickCount();
	TheSkirmishGameInfo->setSeed( seed );
	TheSkirmishGameInfo->startGame( 0 );

	TheWritableGlobalData->m_shellMapOn = FALSE;
	TheWritableGlobalData->m_playIntro = FALSE;

	/* The menu passes the game speed slider's position here, so pass the same thing and clamp it
		 the same way reallyDoStart() does: MSG_NEW_GAME rejects anything outside 1..1000 by falling
		 back to m_framesPerSecondLimit *unclamped*, which is how a stray -fps would still get through.
		 -noFPSLimit deliberately does not appear here - it uncaps the renderer, not the simulation. */
	Int maxFPS = TheGlobalData->m_framesPerSecondLimit;
	if (maxFPS < 15)
		maxFPS = DEFAULT_MAX_FPS;
	if (maxFPS > 1000)
		maxFPS = 1000;

	InitRandom( seed );
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
	msg->appendIntegerArgument( GAME_SKIRMISH );
	msg->appendIntegerArgument( DIFFICULTY_NORMAL );
	msg->appendIntegerArgument( 0 );
	msg->appendIntegerArgument( maxFPS );

	DEBUG_LOG(("-autoskirmish: %d slots on '%s', seed %d, up to %d fps, %s\n",
		numPlayers, mapName.str(), seed, maxFPS,
		observing ? "every slot AI, watching from the free camera" : "slot 0 is the local player"));
}

/** -----------------------------------------------------------------------------------------------
 * -netgame <ip>[,<ip>...] -netslot <n>: play a LAN game against those addresses, with the slot
 * list, the map and the seed coming from the command line instead of from the lobby.  Same job as
 * startAutoSkirmish() one level up, and the same restriction: the map has to be a multiplayer map
 * that is already in the cache on every machine, because nothing is transferred.
 *
 * Every copy has to be given the same list in the same order and its own -netslot, and the seed
 * has to match too - the factions, the colours and the start positions are all drawn from it, and
 * two machines that disagree about them desync on the first frame.
 */
static void startAutoNetGame( void )
{
	AsciiString mapName = TheGlobalData->m_mapName;
	if (mapName.isEmpty())
	{
		DEBUG_LOG(("-netgame: no -map was given\n"));
		return;
	}

	const MapMetaData *md = TheMapCache->findMap( mapName );
	if (md == NULL)
	{
		DEBUG_LOG(("-netgame: '%s' is not in the map cache\n", mapName.str()));
		return;
	}
	if (!md->m_isMultiplayer)
	{
		DEBUG_LOG(("-netgame: '%s' is not a multiplayer map\n", mapName.str()));
		return;
	}

	UnsignedInt slotIPs[ MAX_SLOTS ];
	Int numSlots = ResolveHostList( TheGlobalData->m_netGameHosts, slotIPs, MAX_SLOTS );
	if (numSlots < 2)
	{
		DEBUG_LOG(("-netgame: '%s' is not a list of 2 to %d addresses\n",
			TheGlobalData->m_netGameHosts.str(), MAX_SLOTS));
		return;
	}

	if (numSlots > md->m_numPlayers)
	{
		DEBUG_LOG(("-netgame: '%s' holds %d players, not %d\n", mapName.str(), md->m_numPlayers, numSlots));
		return;
	}

	/* The seed is what the host would have picked and sent round, so it has to be given here - an
		 unseeded network game is one that disagrees with itself. */
	if (TheGlobalData->m_fixedSeed < 0)
	{
		DEBUG_LOG(("-netgame: needs a -seed, and the same one on every machine\n"));
		return;
	}

	if (TheLAN == NULL)
		TheLAN = NEW LANAPI();

	TheLAN->StartAutomatedGame( mapName, TheGlobalData->m_fixedSeed, slotIPs, numSlots,
		TheGlobalData->m_netGameLocalSlot );
}

/** -----------------------------------------------------------------------------------------------
 * Initialize the game engine by initializing the GameLogic and GameClient.
 */
void GameEngine::init( void ) {} /// @todo: I changed this to take argc & argv so we can parse those after the GDF is loaded.  We need to rethink this immediately as it is a nasty hack
void GameEngine::init( int argc, char *argv[] )
{
	try {
		//create an INI object to use for loading stuff
		INI ini;

#ifdef DEBUG_LOGGING
		if (TheVersion)
		{
			DEBUG_LOG(("================================================================================\n"));
	#if defined _DEBUG
			const char *buildType = "Debug";
	#elif defined _INTERNAL
			const char *buildType = "Internal";
	#else
			const char *buildType = "Release";
	#endif
			DEBUG_LOG(("Generals version %s (%s)\n", TheVersion->getAsciiVersion().str(), buildType));
			DEBUG_LOG(("Build date: %s\n", TheVersion->getAsciiBuildTime().str()));
			DEBUG_LOG(("Build location: %s\n", TheVersion->getAsciiBuildLocation().str()));
			DEBUG_LOG(("Built by: %s\n", TheVersion->getAsciiBuildUser().str()));
			DEBUG_LOG(("================================================================================\n"));
		}
#endif

	#if defined(PERF_TIMERS) || defined(DUMP_PERF_STATS)
		DEBUG_LOG(("Calculating CPU frequency for performance timers.\n"));
		InitPrecisionTimer();
	#endif
	#ifdef PERF_TIMERS
		PerfGather::initPerfDump("AAAPerfStats", PerfGather::PERF_NETTIME);
	#endif




	#ifdef DUMP_PERF_STATS////////////////////////////////////////////////////////////
	__int64 startTime64;//////////////////////////////////////////////////////////////
	__int64 endTime64,freq64;///////////////////////////////////////////////////////////
	GetPrecisionTimerTicksPerSec(&freq64);///////////////////////////////////////////////
	GetPrecisionTimer(&startTime64);////////////////////////////////////////////////////
  char Buf[256];//////////////////////////////////////////////////////////////////////
	#endif//////////////////////////////////////////////////////////////////////////////
		
		m_maxFPS = DEFAULT_MAX_FPS;

		/* THREADING-ROADMAP.md 3.1.  Started before anything can fork and joined in ~GameEngine.
			 Costs one thread per spare core sitting on a semaphore until something uses it. */
		JobSystem::init();

		TheSubsystemList = MSGNEW("GameEngineSubsystem") SubsystemInterfaceList;
		
		TheSubsystemList->addSubsystem(this);

		// initialize the random number system
		InitRandom();

		// Create the low-level file system interface
		TheFileSystem = createFileSystem();

		//Kris: Patch 1.01 - November 17, 2003
		//I was unable to resolve the RTPatch method of deleting a shipped file. English, Chinese, and Korean
		//SKU's shipped with two INIZH.big files. One properly in the Run directory and the other in Run\INI\Data.
		//We need to toast the latter in order for the game to patch properly.
		DeleteFile( "Data\\INI\\INIZH.big" );

		// not part of the subsystem list, because it should normally never be reset!
		TheNameKeyGenerator = MSGNEW("GameEngineSubsystem") NameKeyGenerator;
		TheNameKeyGenerator->init();


    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheNameKeyGenerator  = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		// not part of the subsystem list, because it should normally never be reset!
		TheCommandList = MSGNEW("GameEngineSubsystem") CommandList;
		TheCommandList->init();

    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheCommandList  = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		XferCRC xferCRC;
		xferCRC.open("lightCRC");


		initSubsystem(TheLocalFileSystem, "TheLocalFileSystem", createLocalFileSystem(), NULL);


    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheLocalFileSystem  = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheArchiveFileSystem, "TheArchiveFileSystem", createArchiveFileSystem(), NULL); // this MUST come after TheLocalFileSystem creation

    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheArchiveFileSystem  = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheWritableGlobalData, "TheWritableGlobalData", MSGNEW("GameEngineSubsystem") GlobalData(), &xferCRC, "Data\\INI\\Default\\GameData.ini", "Data\\INI\\GameData.ini");


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After  TheWritableGlobalData = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////



	#if defined(_DEBUG) || defined(_INTERNAL)
		// If we're in Debug or Internal, load the Debug info as well.
		ini.load( AsciiString( "Data\\INI\\GameDataDebug.ini" ), INI_LOAD_OVERWRITE, NULL );
	#endif
		
		// special-case: parse command-line parameters after loading global data
		parseCommandLine(argc, argv);

		// doesn't require resets so just create a single instance here.
		TheGameLODManager = MSGNEW("GameEngineSubsystem") GameLODManager;
		TheGameLODManager->init();
		
		// after parsing the command line, we may want to perform dds stuff. Do that here.
		if (TheGlobalData->m_shouldUpdateTGAToDDS) {
			// update any out of date targas here.
			updateTGAtoDDS();
		}

		// read the water settings from INI (must do prior to initing GameClient, apparently)
		ini.load( AsciiString( "Data\\INI\\Default\\Water.ini" ), INI_LOAD_OVERWRITE, &xferCRC );
		ini.load( AsciiString( "Data\\INI\\Water.ini" ), INI_LOAD_OVERWRITE, &xferCRC );
		ini.load( AsciiString( "Data\\INI\\Default\\Weather.ini" ), INI_LOAD_OVERWRITE, &xferCRC );
		ini.load( AsciiString( "Data\\INI\\Weather.ini" ), INI_LOAD_OVERWRITE, &xferCRC );



	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After water INI's = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


#ifdef DEBUG_CRC
		initSubsystem(TheDeepCRCSanityCheck, "TheDeepCRCSanityCheck", MSGNEW("GameEngineSubystem") DeepCRCSanityCheck, NULL, NULL, NULL, NULL);
#endif // DEBUG_CRC
		initSubsystem(TheGameText, "TheGameText", CreateGameTextInterface(), NULL);

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheGameText = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheScienceStore,"TheScienceStore", MSGNEW("GameEngineSubsystem") ScienceStore(), &xferCRC, "Data\\INI\\Default\\Science.ini", "Data\\INI\\Science.ini");
		initSubsystem(TheMultiplayerSettings,"TheMultiplayerSettings", MSGNEW("GameEngineSubsystem") MultiplayerSettings(), &xferCRC, "Data\\INI\\Default\\Multiplayer.ini", "Data\\INI\\Multiplayer.ini");
		initSubsystem(TheTerrainTypes,"TheTerrainTypes", MSGNEW("GameEngineSubsystem") TerrainTypeCollection(), &xferCRC, "Data\\INI\\Default\\Terrain.ini", "Data\\INI\\Terrain.ini");
		initSubsystem(TheTerrainRoads,"TheTerrainRoads", MSGNEW("GameEngineSubsystem") TerrainRoadCollection(), &xferCRC, "Data\\INI\\Default\\Roads.ini", "Data\\INI\\Roads.ini");
		initSubsystem(TheGlobalLanguageData,"TheGlobalLanguageData",MSGNEW("GameEngineSubsystem") GlobalLanguage, NULL); // must be before the game text
		initSubsystem(TheCDManager,"TheCDManager", CreateCDManager(), NULL);
	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheCDManager = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////
		initSubsystem(TheAudio,"TheAudio", createAudioManager(), NULL);
		//
		// Missing music used to end the process here, with setQuitting and not one word anywhere: the
		// game started, the window appeared for a moment and it closed again.  A player who deleted
		// Music.big to save space, or a mod that ships without music, got that and no way to find out
		// why.  Music is not a thing the game needs to run - the audio manager already plays nothing
		// when it has nothing - so say so in the log and carry on.
		//
		if (!TheAudio->isMusicAlreadyLoaded())
			DEBUG_LOG(("No music track was found - the game runs without music. Check that Music.big is next to the exe.\n"));

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheAudio = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheFunctionLexicon,"TheFunctionLexicon", createFunctionLexicon(), NULL);
		initSubsystem(TheModuleFactory,"TheModuleFactory", createModuleFactory(), NULL);
		initSubsystem(TheMessageStream,"TheMessageStream", createMessageStream(), NULL);
		initSubsystem(TheSidesList,"TheSidesList", MSGNEW("GameEngineSubsystem") SidesList(), NULL);
		initSubsystem(TheCaveSystem,"TheCaveSystem", MSGNEW("GameEngineSubsystem") CaveSystem(), NULL);
		initSubsystem(TheRankInfoStore,"TheRankInfoStore", MSGNEW("GameEngineSubsystem") RankInfoStore(), &xferCRC, NULL, "Data\\INI\\Rank.ini");
		initSubsystem(ThePlayerTemplateStore,"ThePlayerTemplateStore", MSGNEW("GameEngineSubsystem") PlayerTemplateStore(), &xferCRC, "Data\\INI\\Default\\PlayerTemplate.ini", "Data\\INI\\PlayerTemplate.ini");
		initSubsystem(TheParticleSystemManager,"TheParticleSystemManager", createParticleSystemManager(), NULL);

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheParticleSystemManager = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////
    
    
		initSubsystem(TheFXListStore,"TheFXListStore", MSGNEW("GameEngineSubsystem") FXListStore(), &xferCRC, "Data\\INI\\Default\\FXList.ini", "Data\\INI\\FXList.ini");
		/* The fork's own detonation light, on top of EA's list.  It is a separate file rather than a
			 loose copy of FXList.ini because a loose copy shadows the whole 190K shipped file: it goes
			 stale against every patch, it cannot be reviewed, and - since it lands in the INI CRC below -
			 it silently refuses every multiplayer join from a machine that does not have the same one. */
		ini.load( AsciiString( "Data\\INI\\FXListReborn.ini" ), INI_LOAD_OVERWRITE, &xferCRC );
		initSubsystem(TheWeaponStore,"TheWeaponStore", MSGNEW("GameEngineSubsystem") WeaponStore(), &xferCRC, NULL, "Data\\INI\\Weapon.ini");
		initSubsystem(TheObjectCreationListStore,"TheObjectCreationListStore", MSGNEW("GameEngineSubsystem") ObjectCreationListStore(), &xferCRC, "Data\\INI\\Default\\ObjectCreationList.ini", "Data\\INI\\ObjectCreationList.ini");
		initSubsystem(TheLocomotorStore,"TheLocomotorStore", MSGNEW("GameEngineSubsystem") LocomotorStore(), &xferCRC, NULL, "Data\\INI\\Locomotor.ini");
		initSubsystem(TheSpecialPowerStore,"TheSpecialPowerStore", MSGNEW("GameEngineSubsystem") SpecialPowerStore(), &xferCRC, "Data\\INI\\Default\\SpecialPower.ini", "Data\\INI\\SpecialPower.ini");
		initSubsystem(TheDamageFXStore,"TheDamageFXStore", MSGNEW("GameEngineSubsystem") DamageFXStore(), &xferCRC, NULL, "Data\\INI\\DamageFX.ini");
		initSubsystem(TheArmorStore,"TheArmorStore", MSGNEW("GameEngineSubsystem") ArmorStore(), &xferCRC, NULL, "Data\\INI\\Armor.ini");
		initSubsystem(TheBuildAssistant,"TheBuildAssistant", MSGNEW("GameEngineSubsystem") BuildAssistant, NULL);


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheBuildAssistant = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////



		initSubsystem(TheThingFactory,"TheThingFactory", createThingFactory(), &xferCRC, "Data\\INI\\Default\\Object.ini", NULL, "Data\\INI\\Object");

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheThingFactory = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////
    
    
		initSubsystem(TheUpgradeCenter,"TheUpgradeCenter", MSGNEW("GameEngineSubsystem") UpgradeCenter, &xferCRC, "Data\\INI\\Default\\Upgrade.ini", "Data\\INI\\Upgrade.ini");
		initSubsystem(TheGameClient,"TheGameClient", createGameClient(), NULL);


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheGameClient = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////

	
		initSubsystem(TheAI,"TheAI", MSGNEW("GameEngineSubsystem") AI(), &xferCRC,  "Data\\INI\\Default\\AIData.ini", "Data\\INI\\AIData.ini");
		initSubsystem(TheGameLogic,"TheGameLogic", createGameLogic(), NULL);
		initSubsystem(TheTeamFactory,"TheTeamFactory", MSGNEW("GameEngineSubsystem") TeamFactory(), NULL);
		initSubsystem(TheCrateSystem,"TheCrateSystem", MSGNEW("GameEngineSubsystem") CrateSystem(), &xferCRC, "Data\\INI\\Default\\Crate.ini", "Data\\INI\\Crate.ini");
		initSubsystem(ThePlayerList,"ThePlayerList", MSGNEW("GameEngineSubsystem") PlayerList(), NULL);
		initSubsystem(TheRecorder,"TheRecorder", createRecorder(), NULL);
		initSubsystem(TheRadar,"TheRadar", createRadar(), NULL);
		initSubsystem(TheVictoryConditions,"TheVictoryConditions", createVictoryConditions(), NULL);



	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheVictoryConditions = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		AsciiString fname;
		fname.format("Data\\%s\\CommandMap.ini", GetRegistryLanguage().str());
		initSubsystem(TheMetaMap,"TheMetaMap", MSGNEW("GameEngineSubsystem") MetaMap(), NULL, fname.str(), "Data\\INI\\CommandMap.ini");

#if defined(_DEBUG) || defined(_INTERNAL)
		ini.load("Data\\INI\\CommandMapDebug.ini", INI_LOAD_MULTIFILE, NULL);
#endif

#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
		ini.load("Data\\INI\\CommandMapDemo.ini", INI_LOAD_MULTIFILE, NULL);
#endif

		//
		// ... and last, whatever the player has moved.  Keybinds.ini sits next to Options.ini and
		// holds only the commands that are not on the key the data gave them, so it survives a patch
		// that adds commands and it is not part of the INI checksum: two players in one match can
		// hold completely different keyboards.
		//
		TheMetaMap->loadUserBindings();


		initSubsystem(TheActionManager,"TheActionManager", MSGNEW("GameEngineSubsystem") ActionManager(), NULL);
		//initSubsystem((CComObject<WebBrowser> *)TheWebBrowser,"(CComObject<WebBrowser> *)TheWebBrowser", (CComObject<WebBrowser> *)createWebBrowser(), NULL);
		initSubsystem(TheGameStateMap,"TheGameStateMap", MSGNEW("GameEngineSubsystem") GameStateMap, NULL, NULL, NULL );
		initSubsystem(TheGameState,"TheGameState", MSGNEW("GameEngineSubsystem") GameState, NULL, NULL, NULL );

		// Create the interface for sending game results
		initSubsystem(TheGameResultsQueue,"TheGameResultsQueue", GameResultsInterface::createNewGameResultsInterface(), NULL, NULL, NULL, NULL);


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheGameResultsQueue = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		xferCRC.close();
		TheWritableGlobalData->m_iniCRC = xferCRC.getCRC();
		DEBUG_LOG(("INI CRC is 0x%8.8X\n", TheGlobalData->m_iniCRC));

		TheSubsystemList->postProcessLoadAll();

		setFramesPerSecondLimit(TheGlobalData->m_framesPerSecondLimit);

		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_musicOn, AudioAffect_Music);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_soundsOn, AudioAffect_Sound);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_sounds3DOn, AudioAffect_Sound3D);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_speechOn, AudioAffect_Speech);
			
		// We're not in a network game yet, so set the network singleton to NULL.
		TheNetwork = NULL;

		//Create a default ini file for options if it doesn't already exist.
		//OptionPreferences prefs( TRUE );

		// If we turn m_quitting to FALSE here, then we throw away any requests to quit that
		// took place during loading. :-\ - jkmcd
		// If this really needs to take place, please make sure that pressing cancel on the audio 
		// load music dialog will still cause the game to quit.
		// m_quitting = FALSE;

		// for fingerprinting, we need to ensure the presence of these files


#if !defined(_INTERNAL) && !defined(_DEBUG)
		AsciiString dirName;
    dirName = TheArchiveFileSystem->getArchiveFilenameForFile("generalsbzh.sec");

    if (dirName.compareNoCase("genseczh.big") != 0)
		{
			DEBUG_LOG(("generalsbzh.sec was not found in genseczh.big - it was in '%s'\n", dirName.str()));
			m_quitting = TRUE;
		}
		
		dirName = TheArchiveFileSystem->getArchiveFilenameForFile("generalsazh.sec");
		const char *noPath = dirName.reverseFind('\\');
		if (noPath) {
			dirName = noPath + 1;
		}

		if (dirName.compareNoCase("musiczh.big") != 0)
		{
			DEBUG_LOG(("generalsazh.sec was not found in musiczh.big - it was in '%s'\n", dirName.str()));
			m_quitting = TRUE;
		}
#endif


		// initialize the MapCache
		TheMapCache = MSGNEW("GameEngineSubsystem") MapCache;
		TheMapCache->updateCache();


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheMapCache->updateCache = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		if (TheGlobalData->m_buildMapCache)
		{
			// just quit, since the map cache has already updated
			//populateMapListbox(NULL, true, true);
			m_quitting = TRUE;
		}
		
		// load the initial shell screen
		//TheShell->push( AsciiString("Menus/MainMenu.wnd") );
		
		// This allows us to run a map/reply from the command line
		if (TheGlobalData->m_initialFile.isEmpty() == FALSE)
		{
			AsciiString fname = TheGlobalData->m_initialFile;
			fname.toLower();

			if (fname.endsWithNoCase(".map"))
			{
				TheWritableGlobalData->m_shellMapOn = FALSE;
				TheWritableGlobalData->m_playIntro = FALSE;
				TheWritableGlobalData->m_pendingFile = TheGlobalData->m_initialFile;

				// shutdown the top, but do not pop it off the stack
	//			TheShell->hideShell();

				// send a message to the logic for a new game
				GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
				msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
				msg->appendIntegerArgument(DIFFICULTY_NORMAL);
				msg->appendIntegerArgument(0);
				InitRandom(0);
			}
			else if (fname.endsWithNoCase(".rep"))
			{
				/* Playback cannot start here.  init() ends with TheSubsystemList->resetAll(), and
					 RecorderClass::reset() closes the file it is playing back and drops the mode to
					 NONE - after which the queued MSG_NEW_GAME starts a replay game with no command
					 stream behind it and the recorder, seeing mode NONE, starts *recording* that empty
					 game over the replay it was asked to play.  The menu path is unaffected because by
					 then the reset is long past.  So remember the name and open it below.

					 The shell map goes off for the same reason the .map branch turns it off: it is a
					 live game of its own, and a replay that fails to open would otherwise leave the
					 process sitting in it, which from the outside looks exactly like a hung playback. */
				TheWritableGlobalData->m_shellMapOn = FALSE;
				TheWritableGlobalData->m_playIntro = FALSE;
				thePendingReplayFile = fname;
			}
			else if (fname.endsWithNoCase(".sav"))
			{
				TheWritableGlobalData->m_shellMapOn = FALSE;
				TheWritableGlobalData->m_playIntro = FALSE;
				thePendingSaveFile = TheGlobalData->m_initialFile;	// the name as given, not lowercased
			}
		}
		else if (TheGlobalData->m_netGameHosts.isNotEmpty())
		{
			/* Deferred for the same reason the replay is: the network game has to survive the
				 resetAll() that ends init(), and TheRecorder has to be past its reset before the
				 MSG_NEW_GAME that makes it start recording is queued. */
			TheWritableGlobalData->m_shellMapOn = FALSE;
			TheWritableGlobalData->m_playIntro = FALSE;
		}
		else if (TheGlobalData->m_autoSkirmishPlayers > 0)
		{
			startAutoSkirmish();
		}

		// 
		if (TheMapCache && TheGlobalData->m_shellMapOn)
		{
			AsciiString lowerName = TheGlobalData->m_shellMapName;
			lowerName.toLower();

			MapCache::const_iterator it = TheMapCache->find(lowerName);
			if (it == TheMapCache->end())
			{
				TheWritableGlobalData->m_shellMapOn = FALSE;
			}
		}

		if(!TheGlobalData->m_playIntro)
			TheWritableGlobalData->m_afterIntro = TRUE;

		//initDisabledMasks();
		
	}
	catch (ErrorCode ec)
	{
		/* Every ErrorCode but ERROR_INVALID_D3D used to leave here without a word, and init then ran
			 on to its tail with half its subsystems missing - the first frame faulted on a NULL
			 TheGameLogic and the crash log blamed GameEngine::update() for a failure that happened
			 during startup.  Say which code it was and where it left the engine. */
		DEBUG_LOG(("GameEngine::init - ErrorCode %d escaped initialization (TheGameLogic=%p, TheGameClient=%p)\n",
			(Int)ec, TheGameLogic, TheGameClient));
		if (ec == ERROR_INVALID_D3D)
		{
			RELEASE_CRASHLOCALIZED("ERROR:D3DFailurePrompt", "ERROR:D3DFailureMessage");
		}
	}
	catch (INIException e)
	{
		if (e.mFailureMessage)
			RELEASE_CRASH((e.mFailureMessage));
		else
			RELEASE_CRASH(("Uncaught Exception during initialization."));

	}
	catch (...)
	{
		RELEASE_CRASH(("Uncaught Exception during initialization."));
	}

	if(!TheGlobalData->m_playIntro)
		TheWritableGlobalData->m_afterIntro = TRUE;

	initKindOfMasks();
	initDisabledMasks();
	initDamageTypeFlags();

	TheSubsystemList->resetAll();
	HideControlBar();

	/* Now that every subsystem has been reset, the recorder can be handed the replay and keep
		 it: the file stays open, the mode stays PLAYBACK, and the MSG_NEW_GAME appended here is
		 the one the logic acts on. */
	if (thePendingReplayFile.isNotEmpty())
	{
		if (!TheRecorder->playbackFile( thePendingReplayFile ))
		{
			DEBUG_LOG(("-replay: '%s' could not be played back\n", thePendingReplayFile.str()));
			TheWritableGlobalData->m_shellMapOn = TRUE;
		}
		thePendingReplayFile.clear();
	}

	/* Same point in the sequence, same reason: the world a save builds has to survive resetAll().
		 The menu path (doLoadGame in PopupSaveLoad.cpp) prepares a single player game first and falls
		 back to the shell if the load fails; do both here. */
	if (thePendingSaveFile.isNotEmpty())
	{
		startPendingSaveGame();
		thePendingSaveFile.clear();
	}

	if (TheGlobalData->m_netGameHosts.isNotEmpty() && TheGlobalData->m_initialFile.isEmpty())
	{
		startAutoNetGame();
	}
}  // end init

/** -----------------------------------------------------------------------------------------------
	* Reset all necessary parts of the game engine to be ready to accept new game data 
	*/
void GameEngine::reset( void )
{

	WindowLayout *background = TheWindowManager->winCreateLayout("Menus/BlankWindow.wnd");
	DEBUG_ASSERTCRASH(background,("We Couldn't Load Menus/BlankWindow.wnd"));
	background->hide(FALSE);
	background->bringForward();
	background->getFirstWindow()->winClearStatus(WIN_STATUS_IMAGE);
	Bool deleteNetwork = false;
	if (TheGameLogic->isInMultiplayerGame())
		deleteNetwork = true;

	TheSubsystemList->resetAll();

	if (deleteNetwork)
	{
		DEBUG_ASSERTCRASH(TheNetwork, ("Deleting NULL TheNetwork!"));
		if (TheNetwork)
			delete TheNetwork;
		TheNetwork = NULL;
	}
	if(background)
	{
		background->destroyWindows();
		background->deleteInstance();
		background = NULL;
	}
}

/// -----------------------------------------------------------------------------------------------
DECLARE_PERF_TIMER(GameEngine_update)

/** -----------------------------------------------------------------------------------------------
 * Wall-clock pacing for the logic tick: update() may be called at any render rate, but a logic
 * frame is only "due" every 1000/logicFps milliseconds.
 *
 * A render pass longer than one logic frame owes the simulation more than one tick, and the debt
 * has to be payable or game speed silently becomes render speed - at 20fps a 30Hz match runs a
 * third slow. So the accumulator carries up to LOGIC_CATCHUP_MAX_FRAMES frames of debt and the
 * caller drains it in a loop, skipping the client half while it catches up.
 *
 * The cap is what keeps that from spiralling: when the logic frame itself is what blew the budget,
 * paying the debt would just queue more of the same work. Past the cap the surplus is dropped and
 * the match honestly runs slow - which is what the HUD's 'hz' readout reports.
 *
 * Free function (not a member, not static) so test_gameengine can link straight to it.
 */
Int GameEngine_logicCatchupMaxFrames( Int logicFps )
{
	if (logicFps <= 0)
		return 1;
	const Int frames = (Int)(logicFps * LOGIC_CATCHUP_MAX_MS / 1000.0f + 0.5f);
	return frames < 1 ? 1 : frames;
}

Bool GameEngine_mayStartAnotherCatchupTick( Int ticksSoFar, Int maxTicks, Real elapsedMsInLoop )
{
	if (ticksSoFar >= maxTicks)
		return FALSE;
	return elapsedMsInLoop < LOGIC_CATCHUP_BUDGET_MS;
}

Bool GameEngine_isLogicFrameDue( Real& accumMs, Real elapsedMs, Int logicFps )
{
	if (logicFps <= 0)
		return TRUE;
	const Real msPerLogicFrame = 1000.0f / logicFps;
	const Real maxAccumMs = msPerLogicFrame * GameEngine_logicCatchupMaxFrames(logicFps);
	if (elapsedMs > maxAccumMs)
		elapsedMs = maxAccumMs;
	accumMs += elapsedMs;
	if (accumMs > maxAccumMs)
		accumMs = maxAccumMs;
	if (accumMs < msPerLogicFrame)
		return FALSE;
	accumMs -= msPerLogicFrame;
	return TRUE;
}

/** -----------------------------------------------------------------------------------------------
 * Update the game engine by updating the GameClient and GameLogic singletons.
 * The client runs as fast as possible; TheGameLogic is limited to a fixed wall-clock
 * framerate (m_maxFPS, the game-speed setting), so game speed does not scale with the
 * render framerate.
 */
#ifdef DEBUG_LOGGING
//
// Frame rate watchdog.  A slow logic frame and a slow render both show up to the player as the same
// stutter, and the logic-side slow-frame log cannot tell them apart because it never sees the client
// half of the loop.  So time both halves of every pass and, once a second, say what the rate was and
// where the time went - but only when the rate actually dropped, so a smooth match logs nothing.
//
extern Real TheClientDrawMS;			///< GameClient.cpp: how long the last TheDisplay->DRAW() took
extern Real TheSceneDrawMS;				///< ...of which the 3D world
extern Real TheUIDrawMS;					///< ...and the interface over it
extern Real TheUIPostDrawMS;			///< ...of which the overlays and the HUD strips
extern Real TheWindowRepaintMS;		///< ...and the window system's repaint
extern Real TheStripGatherMS;			///< ...of the overlays, the production strip's sweep
extern Real TheStripDrawMS;				///< ...and the production strip's own drawing

#endif

// Outside the DEBUG_LOGGING block above on purpose: the frame-time histogram below is not debug
// instrumentation, it ships, and it needs this.
static Real engineElapsedMS( const Int64 &from, const Int64 &to )
{
	Int64 freq;
	QueryPerformanceFrequency( (LARGE_INTEGER *)&freq );
	if( freq < 1 )
		return 0.0f;
	return (Real)((double)(to - from) * 1000.0 / (double)freq);
}

/** -----------------------------------------------------------------------------------------------
 * Frame time as a distribution, not as an average.
 *
 * A mean is exactly the statistic that hides a stutter: a match that runs at 3ms and hitches to
 * 120ms four times a minute has a beautiful average and is horrible to play.  What a player feels
 * is the tail - so this keeps the tail and throws the mean in as an afterthought.
 *
 * A fixed histogram, because the alternative is keeping every sample: 0.25ms buckets out to 64ms
 * plus one overflow bucket, which is 257 Ints of static storage, no allocation, and one add per
 * frame.  Percentiles come out of the buckets (so they are accurate to a quarter of a millisecond,
 * which is far finer than anything that matters here); the worst frame is kept exactly, because the
 * worst frame is the one that gets complained about.
 */
class FrameTimeHistogram
{
public:
	enum { BUCKET_COUNT = 257, LAST_BUCKET = BUCKET_COUNT - 1 };
	static const Real BUCKET_MS;			// width of one bucket

	void reset( void )
	{
		m_count = 0;
		m_sumMS = 0.0;
		m_worstMS = 0.0f;
		m_worstAtFrame = 0;
		for( Int i = 0; i < BUCKET_COUNT; ++i )
			m_buckets[ i ] = 0;
	}

	void note( Real ms, UnsignedInt logicFrame )
	{
		if( ms < 0.0f )
			return;										// a clock that went backwards is not a frame time

		++m_count;
		m_sumMS += ms;
		if( ms > m_worstMS )
		{
			m_worstMS = ms;
			m_worstAtFrame = logicFrame;
		}

		Int bucket = (Int)(ms / BUCKET_MS);
		if( bucket > LAST_BUCKET )
			bucket = LAST_BUCKET;		// everything past 64ms lands together; it is all "terrible"
		++m_buckets[ bucket ];
	}

	Int count( void ) const { return m_count; }
	Real worstMS( void ) const { return m_worstMS; }
	UnsignedInt worstAtFrame( void ) const { return m_worstAtFrame; }
	Real meanMS( void ) const { return m_count ? (Real)(m_sumMS / m_count) : 0.0f; }

	/** The bucket boundary at or below which `fraction` of the frames fall.  Reported as the top of
		the bucket, so it never claims a frame was faster than it was. */
	Real percentileMS( Real fraction ) const
	{
		if( m_count <= 0 )
			return 0.0f;
		const Int target = (Int)(fraction * m_count);
		Int running = 0;
		for( Int i = 0; i < BUCKET_COUNT; ++i )
		{
			running += m_buckets[ i ];
			if( running > target )
				return (i == LAST_BUCKET) ? m_worstMS : (Real)(i + 1) * BUCKET_MS;
		}
		return m_worstMS;
	}

	/** How many frames took longer than `ms`.  This is the stutter count: pick the budget the
		frame is supposed to fit in and this says how often it did not. */
	Int countOver( Real ms ) const
	{
		Int over = 0;
		const Int firstBad = (Int)(ms / BUCKET_MS) + 1;
		for( Int i = firstBad; i < BUCKET_COUNT; ++i )
			over += m_buckets[ i ];
		return over;
	}

private:
	Int m_buckets[ BUCKET_COUNT ];
	Int m_count;
	double m_sumMS;
	Real m_worstMS;
	UnsignedInt m_worstAtFrame;
};

const Real FrameTimeHistogram::BUCKET_MS = 0.25f;

/* One for the whole loop pass - what the player's eye is on - and one for the logic tick alone,
	 because those are two different stutters with two different fixes: a long render frame drops a
	 picture, a logic tick over its 33ms budget drops the whole simulation behind the wall clock. */
static FrameTimeHistogram theFrameTimes;
static FrameTimeHistogram theLogicTimes;
static Bool theFrameTimesStarted = FALSE;

void GameEngine_noteFrameTime( Real ms, UnsignedInt logicFrame )
{
	if( !theFrameTimesStarted )
		return;
	theFrameTimes.note( ms, logicFrame );
}

void GameEngine_noteLogicTime( Real ms, UnsignedInt logicFrame )
{
	if( !theFrameTimesStarted )
		return;
	theLogicTimes.note( ms, logicFrame );
}

/** Start counting.  Called when an unattended run actually begins, so the map load, the first
	 asset pass and the settling second are not counted as stutters - they are, but they are not the
	 kind anybody can do anything about, and leaving them in buries the ones that matter. */
static void startFrameTimeStats( void )
{
	theFrameTimes.reset();
	theLogicTimes.reset();
	theFrameTimesStarted = TRUE;
}

static void reportFrameTimeStats( void )
{
	if( theFrameTimes.count() <= 0 )
		return;

	const Real over16 = 100.0f * theFrameTimes.countOver( 16.7f ) / theFrameTimes.count();
	const Real over33 = 100.0f * theFrameTimes.countOver( 33.3f ) / theFrameTimes.count();
	DEBUG_LOG(("HEADLESS FRAMETIME: %d frames | mean %.2f p50 %.2f p95 %.2f p99 %.2f p99.9 %.2f worst %.2f ms (frame %d) | over 16.7ms %d (%.2f%%) | over 33.3ms %d (%.2f%%)\n",
						 theFrameTimes.count(), theFrameTimes.meanMS(),
						 theFrameTimes.percentileMS( 0.50f ), theFrameTimes.percentileMS( 0.95f ),
						 theFrameTimes.percentileMS( 0.99f ), theFrameTimes.percentileMS( 0.999f ),
						 theFrameTimes.worstMS(), theFrameTimes.worstAtFrame(),
						 theFrameTimes.countOver( 16.7f ), over16,
						 theFrameTimes.countOver( 33.3f ), over33));

	if( theLogicTimes.count() > 0 )
	{
		DEBUG_LOG(("HEADLESS LOGICTIME: %d ticks | mean %.2f p50 %.2f p95 %.2f p99 %.2f p99.9 %.2f worst %.2f ms (frame %d) | over 33.3ms %d\n",
							 theLogicTimes.count(), theLogicTimes.meanMS(),
							 theLogicTimes.percentileMS( 0.50f ), theLogicTimes.percentileMS( 0.95f ),
							 theLogicTimes.percentileMS( 0.99f ), theLogicTimes.percentileMS( 0.999f ),
							 theLogicTimes.worstMS(), theLogicTimes.worstAtFrame(),
							 theLogicTimes.countOver( 33.3f )));
	}
}

/** -----------------------------------------------------------------------------------------------
 * -autocamera <n>: every n seconds, put the camera where the fighting is.
 *
 * A soak run watches from a free camera that never moves, and everything a moving camera drags in
 * behind it - the terrain window scrolling, the shroud, models and textures loading the first time
 * they come on screen - is invisible to a run that stares at one spot.  That is exactly the blind
 * spot a stutter likes to live in, so the measurement has to move the camera itself.
 *
 * "Where the fighting is" is decided the cheapest way that actually works: a coarse grid over the
 * map, one tally pass over every playable player's units, and the cell holding units from the most
 * sides wins.  A cell with two armies in it beats a cell with a bigger single army, which is the
 * difference between watching a battle and watching a base.  One walk of the object lists every n
 * seconds costs nothing next to the logic frame it rides on.
 */
enum { AUTOCAM_GRID = 16 };
static Int s_autoCamUnits[ AUTOCAM_GRID * AUTOCAM_GRID ];
static UnsignedInt s_autoCamSides[ AUTOCAM_GRID * AUTOCAM_GRID ];

struct AutoCamTally
{
	UnsignedInt sideBit;
	Real originX, originY, spanX, spanY;
};

static void autoCameraTallyObject( Object *obj, void *userData )
{
	AutoCamTally *t = (AutoCamTally *)userData;
	if( obj == NULL )
		return;
	// Units, not buildings: a base sits still and is not what anyone would look at.
	if( !obj->isKindOf( KINDOF_INFANTRY ) && !obj->isKindOf( KINDOF_VEHICLE ) &&
			!obj->isKindOf( KINDOF_AIRCRAFT ) )
		return;

	const Coord3D *pos = obj->getPosition();
	Int cx = (Int)(( pos->x - t->originX ) / t->spanX * AUTOCAM_GRID);
	Int cy = (Int)(( pos->y - t->originY ) / t->spanY * AUTOCAM_GRID);
	if( cx < 0 ) cx = 0;
	if( cy < 0 ) cy = 0;
	if( cx >= AUTOCAM_GRID ) cx = AUTOCAM_GRID - 1;
	if( cy >= AUTOCAM_GRID ) cy = AUTOCAM_GRID - 1;

	const Int cell = cy * AUTOCAM_GRID + cx;
	++s_autoCamUnits[ cell ];
	s_autoCamSides[ cell ] |= t->sideBit;
}

static Int autoCameraCountBits( UnsignedInt v )
{
	Int n = 0;
	while( v ) { n += (v & 1); v >>= 1; }
	return n;
}

static void updateAutoCamera( void )
{
	if( TheGlobalData->m_autoCameraSeconds <= 0 || TheTacticalView == NULL || TheTerrainLogic == NULL )
		return;
	if( !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;

	/* This runs once per *render* frame, and the renderer is uncapped - so a plain modulo of the
		 logic frame fires ten times over on the one frame it is due, each time walking every object
		 list in the game.  Remember the frame it last acted on instead. */
	static UnsignedInt lastMoveFrame = 0xffffffff;
	const UnsignedInt frame = TheGameLogic->getFrame();
	const UnsignedInt period = (UnsignedInt)TheGlobalData->m_autoCameraSeconds * LOGICFRAMES_PER_SECOND;
	if( frame == lastMoveFrame || (frame % period) != 0 )
		return;
	lastMoveFrame = frame;

	Region3D extent;
	TheTerrainLogic->getExtent( &extent );
	AutoCamTally tally;
	tally.originX = extent.lo.x;
	tally.originY = extent.lo.y;
	tally.spanX = extent.width();
	tally.spanY = extent.height();
	if( tally.spanX <= 1.0f || tally.spanY <= 1.0f )
		return;						// no map to speak of

	for( Int i = 0; i < AUTOCAM_GRID * AUTOCAM_GRID; ++i )
	{
		s_autoCamUnits[ i ] = 0;
		s_autoCamSides[ i ] = 0;
	}

	for( Int p = 0; p < ThePlayerList->getPlayerCount() && p < MAX_PLAYER_COUNT; ++p )
	{
		Player *player = ThePlayerList->getNthPlayer( p );
		if( !player->isPlayableSide() || player->isPlayerObserver() )
			continue;
		tally.sideBit = (UnsignedInt)1 << p;
		player->iterateObjects( autoCameraTallyObject, &tally );
	}

	/* Contested first, crowded second.  Squaring the number of sides present is enough to make any
		 two-sided cell outrank any one-sided one at realistic army sizes, without a special case. */
	Int bestCell = -1;
	Int bestScore = 0;
	for( Int c = 0; c < AUTOCAM_GRID * AUTOCAM_GRID; ++c )
	{
		if( s_autoCamUnits[ c ] == 0 )
			continue;
		const Int sides = autoCameraCountBits( s_autoCamSides[ c ] );
		const Int score = s_autoCamUnits[ c ] * sides * sides;
		if( score > bestScore )
		{
			bestScore = score;
			bestCell = c;
		}
	}
	if( bestCell < 0 )
		return;						// nobody has a unit anywhere; leave the camera alone

	Coord3D look;
	look.x = tally.originX + ( (bestCell % AUTOCAM_GRID) + 0.5f ) * tally.spanX / AUTOCAM_GRID;
	look.y = tally.originY + ( (bestCell / AUTOCAM_GRID) + 0.5f ) * tally.spanY / AUTOCAM_GRID;
	look.z = TheTerrainLogic->getGroundHeight( look.x, look.y );
	TheTacticalView->lookAt( &look );

	DEBUG_LOG(("AUTOCAMERA: frame %d -> (%.0f,%.0f), %d units from %d sides\n",
						 frame, look.x, look.y,
						 s_autoCamUnits[ bestCell ], autoCameraCountBits( s_autoCamSides[ bestCell ] )));
}

/** -----------------------------------------------------------------------------------------------
 * Why an unattended run is over, or NULL while it is still going.  The string is what the log line
 * says, so the reason and the report of it cannot drift apart.
 *
 * VictoryConditions publishes the end frame for a free-camera observer too, so nothing here
 * re-derives who won.  It has one trap: 'not decided yet' and 'decided on frame 0' are the same
 * stored zero, and the check starts running before a map has finished placing its objects - at
 * which point every player owns nothing and therefore looks eliminated.  Requiring the end frame to
 * be past the first second is what keeps an unpopulated map from reading as an instant draw.
 */
const char *GameEngine_headlessRunResult( UnsignedInt frame, UnsignedInt victoryEndFrame, Int maxGameFrames )
{
	const UnsignedInt SETTLE_FRAMES = 30;
	if (victoryEndFrame > SETTLE_FRAMES)
		return "decided";
	if (maxGameFrames > 0 && frame >= (UnsignedInt)maxGameFrames)
		return "frame limit reached";
	return NULL;
}

extern void AIUpdate_resetMoveTrace( void );	///< -tracemove: forget the unit the last match followed

/** -----------------------------------------------------------------------------------------------
 * -headless: decide whether the unattended run is finished, and if it is, write down how it went
 * and quit.  Two ways to finish: the match is decided, or -maxframes ran out.
 */
static void updateHeadlessRun( void )
{
	/* -autoskirmish counts as unattended even when it draws.  THREADING-ROADMAP.md section 0 step 4
		 asks for a heavy scenario under a real renderer, and a run that never ends and never writes
		 its numbers down is not a measurement.  This still cannot fire on a game a person started:
		 reaching here needs -headless or -autoskirmish, and neither is on a menu. */
	const Bool unattended = TheGlobalData->m_headless || TheGlobalData->m_autoSkirmishPlayers > 0;
	if (!unattended || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame())
		return;

	static DWORD runStartTime = 0;
	static UnsignedInt runStartFrame = 0;
	static Int peakUnits[ MAX_PLAYER_COUNT ];
	if (runStartTime == 0)
	{
		runStartTime = timeGetTime();
		runStartFrame = TheGameLogic->getFrame();
		for( Int i = 0; i < MAX_PLAYER_COUNT; ++i )
			peakUnits[ i ] = 0;
		// the shell map and the load did their own pathing; the run's numbers start here
		Pathfinder::resetMatchProfile();
		// and -tracemove with no id picks its unit out of the match, not out of the shell map
		AIUpdate_resetMoveTrace();
		extern void GroupDrill_reset( void );
		GroupDrill_reset();
	}

	const UnsignedInt frame = TheGameLogic->getFrame();

	/* -screenshot <n>: hand the renderer a shot request as the run passes frame n. W3DDisplay only
		 sets a pending flag here and writes the file out of the back buffer on its next draw, so this
		 does something only in a run that is drawing - -autoskirmish without -headless. It is the
		 only way to see what a renderer change did without somebody sitting in front of the machine,
		 which is what half the remaining graphics work needs. The file lands next to the save games
		 as sshotNNN.bmp. */
	static UnsignedInt screenShotRequestedOn = 0;
	if (TheGlobalData->m_screenShotFrame > 0 && screenShotRequestedOn == 0 &&
			frame >= (UnsignedInt)TheGlobalData->m_screenShotFrame)
	{
		screenShotRequestedOn = frame;
		if (TheGlobalData->m_headless)
		{
			DEBUG_LOG(("-screenshot: -headless draws nothing, so there is no picture to save\n"));
		}
		else if (TheDisplay != NULL)
		{
			TheDisplay->takeScreenShot();
			DEBUG_LOG(("-screenshot: asked for one at frame %d\n", frame));
		}
	}

	/* The frame times start a second in, not at frame 0.  The first counted pass carries the tail
		 of the map load and came out at two full seconds, which then *is* the worst frame of the
		 match and buries the 60ms hitch that somebody could actually do something about. */
	if( !theFrameTimesStarted && frame > runStartFrame + LOGICFRAMES_PER_SECOND )
		startFrameTimeStats();

	/* The biggest army each side ever fielded, sampled once a second.  The end-of-match counts are
		 cumulative and cannot tell an AI that massed and traded evenly from one that trickled units
		 out and lost them one at a time.  Once a second is one walk of each player's object list per
		 second, which is nothing next to the logic frame it rides on. */
	if ((frame % LOGICFRAMES_PER_SECOND) == 0)
	{
		KindOfMaskType nothing;
		nothing.clear();
		for( Int i = 0; i < ThePlayerList->getPlayerCount() && i < MAX_PLAYER_COUNT; ++i )
		{
			Player *p = ThePlayerList->getNthPlayer( i );
			if (!p->isPlayableSide() || p->isPlayerObserver())
				continue;
			Int units = p->countObjects( MAKE_KINDOF_MASK( KINDOF_INFANTRY ), nothing ) +
									p->countObjects( MAKE_KINDOF_MASK( KINDOF_VEHICLE ), nothing ) +
									p->countObjects( MAKE_KINDOF_MASK( KINDOF_AIRCRAFT ), nothing );
			if (units > peakUnits[ i ])
				peakUnits[ i ] = units;
		}
	}

	const char *why = GameEngine_headlessRunResult( frame, TheVictoryConditions->getEndFrame(),
																									TheGlobalData->m_maxGameFrames );
	if (why == NULL)
		return;

	const DWORD wallMs = timeGetTime() - runStartTime;
	const Real logicFps = wallMs ? (Real)(frame - runStartFrame) * 1000.0f / (Real)wallMs : 0.0f;

	DEBUG_LOG(("HEADLESS RESULT: %s on frame %d (%d frames in %.1fs wall, %.0f logic fps, %.1fx real time)\n",
						 why, frame,
						 frame - runStartFrame, wallMs / 1000.0f, logicFps,
						 logicFps / (Real)TheGameEngine->getFramesPerSecondLimit()));
	//
	// Client bookkeeping an unattended run still has to do: nothing draws, so nothing was retiring
	// particle systems and the count climbed all run (see GameClient::update).  Print it, because a
	// number that is supposed to stay flat is only worth anything if somebody would notice it move.
	//
	DEBUG_LOG(("HEADLESS PARTICLES: %d systems, %d particles\n",
						 TheParticleSystemManager ? TheParticleSystemManager->getParticleSystemCount() : -1,
						 TheParticleSystemManager ? TheParticleSystemManager->getParticleCount() : -1));

	/* The state of every object in the world, in one number, at the frame the run stopped.  Every
		 movement change in PATHFINDING-PLAN.md edits GameLogic, and a change that plays beautifully
		 and desyncs multiplayer is a change that has to be found before it ships, not after somebody
		 fails to join.  Record a fixed-seed match, play its replay back, compare this line: same
		 frame and same CRC means the replay took the same path through the logic, which is the same
		 property a second machine in a network game needs.  One walk of the object list, once, at
		 the end of a run nobody is watching.

		 It is deliberately the recalculated CRC and not the cached one - the cached value is only
		 refreshed on frames a network game asks for it, and a skirmish never asks. */
	DEBUG_LOG(("HEADLESS CRC: 0x%08X at frame %d\n", TheGameLogic->getCRC( CRC_RECALC ), frame));

	// What the pathfinder did over the whole match, not just the one frame that ran over budget.
	// A pathing change is argued with these: search count and time say what it cost, `nopath` and
	// `outofcells` say whether it broke anything, `blocked`/`stuck` say whether it actually
	// reduced the traffic jams it was supposed to reduce.
	DEBUG_LOG(("HEADLESS PATHFIND: %s\n", Pathfinder::getMatchProfileReport()));

	/* And under -groupdrill, the question the blocked counters cannot answer: of the units that were
		 marched across the map, how many were still going when the next order came, and how many had
		 stopped and stayed stopped.  Time spent in traffic is one thing; never getting there at all
		 is the thing anybody actually complains about. */
	if (TheGlobalData->m_groupDrill > 0)
	{
		extern const char *GroupDrill_report( void );
		DEBUG_LOG(("HEADLESS DRILL: %s\n", GroupDrill_report()));
	}

	/* Stability, which is a different question from speed and is answered by the tail rather than
		 by the average.  These two lines are the ones a stutter complaint is argued with. */
	reportFrameTimeStats();

	/* THREADING-ROADMAP.md 3.1's safety net: "no allocation inside a job" is a rule, and a rule
		 nothing checks is a comment.  allocs must read 0 - anything else means a job took the one
		 global pool lock and serialized the fork it was supposed to spread. */
	DEBUG_LOG(("HEADLESS JOBS: %d worker threads, %d allocations from a job\n",
						 JobSystem::workerCount(), JobSystem::workerAllocationCount()));

	// THREADING-ROADMAP.md section 0 step 3.  Only a PERF_TIMERS build has anything to say here.
#ifdef PERF_TIMERS
	PerfGather::dumpSummary();
#endif

	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		// The list also carries the civilian and neutral slots and, with -observer, the camera the
		// run is watched from.  None of them plays, so none of them has a result worth a line.
		if (!p->isPlayableSide() || p->isPlayerObserver())
			continue;

		ScoreKeeper *score = p->getScoreKeeper();
		DEBUG_LOG(("HEADLESS PLAYER %d '%ls': %s | score %d | money %d earned %d spent | units %d built %d lost %d killed peak %d | buildings %d built %d lost\n",
							 i, p->getPlayerDisplayName().str(),
							 TheVictoryConditions->hasAchievedVictory(p) ? "WON" :
								 (TheVictoryConditions->hasSinglePlayerBeenDefeated(p) ? "eliminated" : "alive"),
							 score->calculateScore(),
							 score->getTotalMoneyEarned(), score->getTotalMoneySpent(),
							 score->getTotalUnitsBuilt(), score->getTotalUnitsLost(), score->getTotalUnitsDestroyed(),
							 peakUnits[ i ],
							 score->getTotalBuildingsBuilt(), score->getTotalBuildingsLost()));
	}

	/* Tear the match down the way the benchmark timer does, so the replay of the run is closed and
		 written rather than left half-flushed by the process going away. */
	if (TheRecorder->getMode() == RECORDERMODETYPE_RECORD)
		TheRecorder->stopRecording();
	TheGameLogic->clearGameData();
	TheGameEngine->setQuitting( TRUE );
}

void GameEngine::update( void )
{
	USE_PERF_TIMER(GameEngine_update)
	{
#ifdef DEBUG_LOGGING
		static Int fpsFrames = 0;
		static Real fpsClientTotal = 0.0f, fpsClientMax = 0.0f;
		static Real fpsLogicTotal = 0.0f, fpsLogicMax = 0.0f;
		static Int fpsLogicTicks = 0, fpsCatchupPasses = 0;
		static DWORD fpsWindowStart = timeGetTime();
		static Real fpsRadarTotal = 0.0f, fpsAudioTotal = 0.0f, fpsDrawTotal = 0.0f, fpsDrawMax = 0.0f;
		static Real fpsSceneTotal = 0.0f, fpsUITotal = 0.0f, fpsPostTotal = 0.0f, fpsWinTotal = 0.0f;
		static Real fpsStripGatherTotal = 0.0f, fpsStripDrawTotal = 0.0f;
		Int64 tClientStart, tClientEnd, tLogicStart, tLogicEnd, tRadarEnd, tAudioEnd;
		Real clientMS = 0.0f, logicMS = 0.0f, radarMS = 0.0f, audioMS = 0.0f;
		Int logicTicks = 0;
		TheClientDrawMS = TheSceneDrawMS = TheUIDrawMS = TheUIPostDrawMS = TheWindowRepaintMS = 0.0f;
		TheStripGatherMS = TheStripDrawMS = 0.0f;
		QueryPerformanceCounter( (LARGE_INTEGER *)&tClientStart );
#endif

		{

			// VERIFY CRC needs to be in this code block.  Please to not pull TheGameLogic->update() inside this block.
			VERIFY_CRC

			TheRadar->UPDATE();
#ifdef DEBUG_LOGGING
			QueryPerformanceCounter( (LARGE_INTEGER *)&tRadarEnd );
#endif

			/// @todo Move audio init, update, etc, into GameClient update

			TheAudio->UPDATE();
#ifdef DEBUG_LOGGING
			QueryPerformanceCounter( (LARGE_INTEGER *)&tAudioEnd );
#endif
			TheGameClient->UPDATE();
			TheMessageStream->propagateMessages();

			if (TheNetwork != NULL)
			{
				TheNetwork->UPDATE();
			}

			TheCDManager->UPDATE();
		}
#ifdef DEBUG_LOGGING
		QueryPerformanceCounter( (LARGE_INTEGER *)&tClientEnd );
		clientMS = engineElapsedMS( tClientStart, tClientEnd );
		radarMS = engineElapsedMS( tClientStart, tRadarEnd );
		audioMS = engineElapsedMS( tRadarEnd, tAudioEnd );
#endif


		// Fast-forward modes run a logic frame on every call; otherwise logic ticks at
		// m_maxFPS Hz of wall clock, however fast the client/renderer above is running.
#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
		Bool fastMode = TheGlobalData->m_TiVOFastMode;
#else
		Bool fastMode = TheGlobalData->m_TiVOFastMode && TheGameLogic->isInReplayGame();
#endif
		fastMode = fastMode || TheTacticalView->getTimeMultiplier() > 1 || TheScriptEngine->isTimeFast();

		/* -headless has no picture to pace the tick against and nobody watching it go by, so it runs
			 a logic frame every pass and the match plays out as fast as the machine manages.  Same
			 branch as fast-forward, different reason: this one is not a cheat, it is the whole point
			 of an unattended run. */
		fastMode = fastMode || TheGlobalData->m_headless;

		static DWORD prevLogicTime = timeGetTime();
		static Real logicAccumMs = 0.0f;
		DWORD now = timeGetTime();
		Real elapsedMs = (Real)(now - prevLogicTime);
		prevLogicTime = now;

		Bool logicFrameDue;
		Bool mayCatchUp = FALSE;
		if (fastMode)
		{
			logicAccumMs = 0.0f;
			logicFrameDue = TRUE;
		}
		else if (TheNetwork != NULL && TheNetwork->isPacingLogicFrames())
		{
			// A network game already has a clock: Network::timeForNewFrame() paces the tick against
			// the negotiated frame rate and only then publishes the frame's commands.  Gating a
			// second time here against m_maxFPS beats two independent clocks against each other -
			// a frame needs both to say yes, so the effective rate settles *below* either one and
			// drifts, which is a systematic multiplayer-only slowdown.  Let the network own it and
			// keep the accumulator clean for when the game drops back to single player.
			logicAccumMs = 0.0f;
			logicFrameDue = TRUE;
		}
		else
		{
			logicFrameDue = GameEngine_isLogicFrameDue(logicAccumMs, elapsedMs, m_maxFPS);
			// Only the wall-clock-paced path has a debt to pay back.  Fast mode and the network
			// clock above both mean exactly one logic frame per call, by their own definition.
			mayCatchUp = (m_maxFPS > 0);
		}

		const Bool logicMayRun =
				((TheNetwork == NULL && !TheGameLogic->isGamePaused()) || (TheNetwork && TheNetwork->isFrameDataReady()));

		if (!logicMayRun)
		{
			// A paused game, or one waiting on the network, is not falling behind - it is stopped.
			// Drop the debt so resuming does not open with a catch-up burst.
			logicAccumMs = 0.0f;
		}
		else if (logicFrameDue)
		{
#ifdef DEBUG_LOGGING
			QueryPerformanceCounter( (LARGE_INTEGER *)&tLogicStart );
#endif
			// Pay off the wall clock's debt.  Every pass after the first is a logic frame this call
			// already owes, and it runs without a client pass in front of it: a render frame is what
			// gets dropped so that game speed stays put when the frame rate does not.  Both the loop
			// count and the pacer's own accumulator cap stop at LOGIC_CATCHUP_MAX_FRAMES, so a logic
			// frame that is itself over budget cannot pull the loop into a spiral.
			Int logicTicksThisPass = 0;
			const Int maxTicksThisPass = GameEngine_logicCatchupMaxFrames(m_maxFPS);
			/* Bounded by the clock as well as by the count - see LOGIC_CATCHUP_BUDGET_MS.  Three
				 25ms ticks back to back with no picture in between is the 113ms freeze; one of them
				 plus the render is a dropped frame nobody files a bug about. */
			Int64 tCatchupStart, tCatchupNow;
			QueryPerformanceCounter( (LARGE_INTEGER *)&tCatchupStart );
			for( ;; )
			{
				TheGameLogic->UPDATE();
				++logicTicksThisPass;

				if (!mayCatchUp)
					break;
				QueryPerformanceCounter( (LARGE_INTEGER *)&tCatchupNow );
				if (!GameEngine_mayStartAnotherCatchupTick( logicTicksThisPass, maxTicksThisPass,
																									 engineElapsedMS( tCatchupStart, tCatchupNow ) ))
					break;
				// Asked last, because it is the one with a side effect: it spends the debt it reports.
				if (!GameEngine_isLogicFrameDue(logicAccumMs, 0.0f, m_maxFPS))
					break;
			}
#ifdef DEBUG_LOGGING
			QueryPerformanceCounter( (LARGE_INTEGER *)&tLogicEnd );
			logicMS = engineElapsedMS( tLogicStart, tLogicEnd );
			logicTicks = logicTicksThisPass;
			// One pass can pay off several ticks of debt; charge the histogram per tick, or a
			// catch-up burst reads as one enormous logic frame that never actually happened.
			GameEngine_noteLogicTime( logicMS / (Real)logicTicksThisPass, TheGameLogic->getFrame() );
#endif
		}

		updateHeadlessRun();
		updateAutoCamera();

#ifdef DEBUG_LOGGING
		fpsFrames++;
		fpsClientTotal += clientMS;
		fpsRadarTotal += radarMS;
		fpsAudioTotal += audioMS;
		fpsDrawTotal += TheClientDrawMS;
		fpsSceneTotal += TheSceneDrawMS;
		fpsUITotal += TheUIDrawMS;
		fpsPostTotal += TheUIPostDrawMS;
		fpsWinTotal += TheWindowRepaintMS;
		fpsStripGatherTotal += TheStripGatherMS;
		fpsStripDrawTotal += TheStripDrawMS;
		if( TheClientDrawMS > fpsDrawMax ) fpsDrawMax = TheClientDrawMS;
		fpsLogicTotal += logicMS;
		fpsLogicTicks += logicTicks;
		if( logicTicks > 1 ) fpsCatchupPasses++;
		if( clientMS > fpsClientMax ) fpsClientMax = clientMS;
		if( logicMS > fpsLogicMax ) fpsLogicMax = logicMS;
		{
			const DWORD nowMS = timeGetTime();
			const DWORD windowMS = nowMS - fpsWindowStart;
			if( windowMS >= 1000 )
			{
				const Real fps = (Real)fpsFrames * 1000.0f / (Real)windowMS;
				if( fps < 50.0f && TheGameLogic && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() )
				{
					// 'hz' is the rate the simulation actually achieved and 'catchup' how many render
					// frames were dropped to hold it there; hz short of m_maxFPS with catchup passes
					// present is the logic side being genuinely over budget, not the pacer.
					DEBUG_LOG(("FPS %.0f over %dms (%d frames, %d hz, %d catchup, logic frame %d) | client avg %.1f max %.1f (render avg %.1f max %.1f [scene %.1f, ui %.1f (post %.1f [strip gather %.1f, draw %.1f], win %.1f), present %.1f], radar %.1f, audio %.1f, rest %.1f) | logic avg %.1f max %.1f | unaccounted %.1fms\n",
										 fps, (Int)windowMS, fpsFrames,
										 (Int)((Real)fpsLogicTicks * 1000.0f / (Real)windowMS + 0.5f), fpsCatchupPasses,
										 TheGameLogic->getFrame(),
										 fpsClientTotal / fpsFrames, fpsClientMax,
										 fpsDrawTotal / fpsFrames, fpsDrawMax,
										 fpsSceneTotal / fpsFrames, fpsUITotal / fpsFrames,
										 fpsPostTotal / fpsFrames,
										 fpsStripGatherTotal / fpsFrames, fpsStripDrawTotal / fpsFrames,
										 fpsWinTotal / fpsFrames,
										 ( fpsDrawTotal - fpsSceneTotal - fpsUITotal ) / fpsFrames,
										 fpsRadarTotal / fpsFrames, fpsAudioTotal / fpsFrames,
										 ( fpsClientTotal - fpsDrawTotal - fpsRadarTotal - fpsAudioTotal ) / fpsFrames,
										 fpsLogicTotal / fpsFrames, fpsLogicMax,
										 (Real)windowMS - fpsClientTotal - fpsLogicTotal));
				}
				fpsFrames = 0;
				fpsLogicTicks = fpsCatchupPasses = 0;
				fpsClientTotal = fpsLogicTotal = 0.0f;
				fpsRadarTotal = fpsAudioTotal = fpsDrawTotal = fpsDrawMax = 0.0f;
				fpsSceneTotal = fpsUITotal = fpsPostTotal = fpsWinTotal = 0.0f;
				fpsStripGatherTotal = fpsStripDrawTotal = 0.0f;
				fpsClientMax = fpsLogicMax = 0.0f;
				fpsWindowStart = nowMS;
			}
		}
#endif

	}	// end perfGather

}

// Horrible reference, but we really, really need to know if we are windowed.
extern bool DX8Wrapper_IsWindowed;
extern HWND ApplicationHWnd;

/** -----------------------------------------------------------------------------------------------
 * The "main loop" of the game engine. It will not return until the game exits. 
 */
void GameEngine::execute( void )
{
	DWORD prevLoopTime = timeGetTime();
#if defined(_DEBUG) || defined(_INTERNAL)
	DWORD startTime = timeGetTime() / 1000;
#endif

	// pretty basic for now
	while( !m_quitting )
	{
		Real thisFrameMS = 0.0f;		// how long this pass took, for the stutter report at the bottom

		//if (TheGlobalData->m_vTune)
		{
#ifdef PERF_TIMERS
			PerfGather::resetAll();
#endif
		}

		{

#if defined(_DEBUG) || defined(_INTERNAL)
			{
				// enter only if in benchmark mode
				if (TheGlobalData->m_benchmarkTimer > 0)
				{
					DWORD currentTime = timeGetTime() / 1000;
					if (TheGlobalData->m_benchmarkTimer < currentTime - startTime)
					{
						if (TheGameLogic->isInGame())
						{
							if (TheRecorder->getMode() == RECORDERMODETYPE_RECORD)
							{
								TheRecorder->stopRecording();
							}
							TheGameLogic->clearGameData();
						}
						TheGameEngine->setQuitting(TRUE);
					}
				}
			}
#endif
			
			{
				/* The whole pass, which is what the eye is on: everything between one picture and
					 the next, not just the parts somebody remembered to instrument.  Two
					 QueryPerformanceCounter reads a frame, which is why this is not behind a switch. */
				Int64 tFrameStart, tFrameEnd;
				QueryPerformanceCounter( (LARGE_INTEGER *)&tFrameStart );
				try
				{
					// compute a frame
					update();
				}
				catch (INIException e)
				{
					// Release CRASH doesn't return, so don't worry about executing additional code.
					if (e.mFailureMessage)
						RELEASE_CRASH((e.mFailureMessage));
					else
						RELEASE_CRASH(("Uncaught Exception in GameEngine::update"));
				}
				catch (...)
				{
					// try to save info off
					try 
					{
						if (TheRecorder && TheRecorder->getMode() == RECORDERMODETYPE_RECORD && TheRecorder->isMultiplayer())
							TheRecorder->cleanUpReplayFile();
					}
					catch (...)
					{
					}
					RELEASE_CRASH(("Uncaught Exception in GameEngine::update"));
				}	// catch
				QueryPerformanceCounter( (LARGE_INTEGER *)&tFrameEnd );
				thisFrameMS = engineElapsedMS( tFrameStart, tFrameEnd );
				GameEngine_noteFrameTime( thisFrameMS,
																	TheGameLogic ? TheGameLogic->getFrame() : 0 );
			}	// perf

			{
				// Rendering runs uncapped everywhere now, menus included. Game speed stays
				// constant because the logic tick is paced by wall clock inside update(), and the
				// menus no longer need a capped loop either: AnimateWindowManager::update paces
				// its own stepping off the wall clock, so the window animations keep the cadence
				// they were tuned for however fast the loop runs.
				prevLoopTime = timeGetTime();

		#if defined(_DEBUG) || defined(_INTERNAL)
				// I'm disabling this in internal because many people need alt-tab capability.  If you happen to be
				// doing performance tuning, please just change this on your local system. -MDC
				if (TheTacticalView->getTimeMultiplier()<=1 && !TheScriptEngine->isTimeFast())
					::Sleep(1); // give everyone else a tiny time slice.
		#endif
			}

		}	// perfgather for execute_loop

#ifdef PERF_TIMERS
		if (!m_quitting && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() && !TheGameLogic->isGamePaused())
		{
			PerfGather::accumulateFrame(thisFrameMS);
			PerfGather::dumpAll(TheGameLogic->getFrame());
			PerfGather::displayGraph(TheGameLogic->getFrame());
			PerfGather::resetAll();
		}
#endif

	}

}

/** -----------------------------------------------------------------------------------------------
	* Factory for the message stream
	*/
MessageStream *GameEngine::createMessageStream( void )
{
	// if you change this update the tools that use the engine systems
	// like GUIEdit, it creates a message stream to run in "test" mode
	return MSGNEW("GameEngineSubsystem") MessageStream;
}

//-------------------------------------------------------------------------------------------------
FileSystem *GameEngine::createFileSystem( void )
{
	return MSGNEW("GameEngineSubsystem") FileSystem;
}

//-------------------------------------------------------------------------------------------------
Bool GameEngine::isMultiplayerSession( void )
{
	return TheRecorder->isMultiplayer();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
#define CONVERT_EXEC1	"..\\Build\\nvdxt -list buildDDS.txt -dxt5 -full -outdir Art\\Textures > buildDDS.out"

void updateTGAtoDDS()
{
	// Here's the scoop. We're going to traverse through all of the files in the Art\Textures folder
	// and determine if there are any .tga files that are newer than associated .dds files. If there 
	// are, then we will re-run the compression tool on them.
	
	File *fp = TheLocalFileSystem->openFile("buildDDS.txt", File::WRITE | File::CREATE | File::TRUNCATE | File::TEXT);
	if (!fp) {
		return;
	}

	FilenameList files;
	TheLocalFileSystem->getFileListInDirectory("Art\\Textures\\", "", "*.tga", files, TRUE);
	FilenameList::iterator it;
	for (it = files.begin(); it != files.end(); ++it) {
		AsciiString filenameTGA = *it;
		AsciiString filenameDDS = *it;
		FileInfo infoTGA;
		TheLocalFileSystem->getFileInfo(filenameTGA, &infoTGA);

		// skip the water textures, since they need to be NOT compressed
		filenameTGA.toLower();
		if (strstr(filenameTGA.str(), "caust"))
		{
			continue;
		}
		// and the recolored stuff.
		if (strstr(filenameTGA.str(), "zhca"))
		{
			continue;
		}

		// replace tga with dds
		filenameDDS.removeLastChar();	// a
		filenameDDS.removeLastChar();	// g
		filenameDDS.removeLastChar();	// t
		filenameDDS.concat("dds");

		Bool needsToBeUpdated = FALSE;
		FileInfo infoDDS;
		if (TheFileSystem->doesFileExist(filenameDDS.str())) {
			TheFileSystem->getFileInfo(filenameDDS, &infoDDS);
			if (infoTGA.timestampHigh > infoDDS.timestampHigh || 
					(infoTGA.timestampHigh == infoDDS.timestampHigh && 
					 infoTGA.timestampLow > infoDDS.timestampLow)) {
				needsToBeUpdated = TRUE;
			}
		} else {
			needsToBeUpdated = TRUE;
		}

		if (!needsToBeUpdated) {
			continue;
		}

		filenameTGA.concat("\n");
		fp->write(filenameTGA.str(), filenameTGA.getLength());
	}

	fp->close();

	system(CONVERT_EXEC1);
}

//-------------------------------------------------------------------------------------------------
// System things

// If we're using the Wide character version of MessageBox, then there's no additional
// processing necessary. Please note that this is a sleazy way to get this information,
// but pending a better one, this'll have to do.
extern const Bool TheSystemIsUnicode = (((void*) (::MessageBox)) == ((void*) (::MessageBoxW)));
