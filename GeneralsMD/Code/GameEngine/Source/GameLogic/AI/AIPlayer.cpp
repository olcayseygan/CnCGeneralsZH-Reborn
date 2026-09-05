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

// AIPlayer.cpp ///////////////////////////////////////////////////////////////////////////////////
// Computerized opponent
// Author: Michael S. Booth, January 2002
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/GameMemory.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h" 
#include "Common/ThingFactory.h"
#include "Common/PlayerList.h"
#include "Common/BuildAssistant.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/WellKnownKeys.h"
#include "Common/Xfer.h"
#include "GameClient/ControlBar.h"
#include "GameClient/TerrainVisual.h"	
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/Weapon.h"				// the anti-masks B1 reads off a team's weapons
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/RebuildHoleBehavior.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"
#include "GameLogic/Module/SupplyWarehouseDockUpdate.h"
#include "GameLogic/PartitionManager.h"
#include "Common/ActionManager.h"				// canCaptureBuilding, for the tech buildings
#include "GameLogic/Module/SpecialPowerModule.h"	// ... and the module that does it

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

#define SUPPLY_CENTER_CLOSE_DIST (20*PATHFIND_CELL_SIZE_F)

#define USE_DOZER 1

/** Does 'observerNdx' know this thing is there?
	*
	* The partition manager already draws exactly the line wanted here.  A player's shroud status for
	* an object is SHROUDED until that player has seen it; once seen, an *immobile* object stays
	* FOGGED when the vision leaves it (PartitionData::friend_calcActualShroudedStatus) while anything
	* that can move goes back to SHROUDED.  So "not SHROUDED" already means "I can see it now, or it
	* is a building I have seen and buildings do not walk away" - which is the whole information model
	* the AI needs, with no memory of its own to keep, save or desync.
	*
	* observerNdx < 0 is the old omniscient answer, for callers that are not one player's thinking. */
static Bool observerKnowsAbout( const Object *obj, Int observerNdx )
{
	if( obj == NULL )
		return FALSE;
	if( observerNdx < 0 )
		return TRUE;
	return obj->getShroudedStatus( observerNdx ) != OBJECTSHROUD_SHROUDED;
}

/** What a unit is worth in a fight, for every decision here that has to weigh one force against
	* another - what to build against (B1), when to break off (C1), when there is enough to attack
	* with (C2).
	*
	* ThreatValue is the data's own answer to exactly this question, and it is what the engine's own
	* threat map is built from.  It also defaults to zero and the shipped Object INI almost never
	* sets it, so a first cut of all three of those decisions quietly weighed every force in the game
	* at nothing and did nothing at all.  Build cost is the fallback: always present, and a fair
	* proxy - the data already prices a tank above a rifleman.
	*/
static Real aiCombatPower( const Object *obj )
{
	if( obj == NULL )
		return 0.0f;

	const ThingTemplate *tmpl = obj->getTemplate();
	if( tmpl == NULL )
		return 0.0f;

	//
	// Only what can shoot has any power in an exchange.  Cost as a proxy is only fair among things
	// that fight: a war factory costs as much as a tank column and cannot fire a shot, and the
	// first cut of this counted every building in range as part of the enemy's firepower - so every
	// team that came within sight of a base decided it was losing and went home, and 20 measured
	// matches ended with zero kills on both sides.
	//
	if( !tmpl->isKindOf( KINDOF_CAN_ATTACK ) )
		return 0.0f;

	const Real threat = INT_TO_REAL( tmpl->getThreatValue() );
	if( threat > 0.0f )
		return threat;

	return INT_TO_REAL( tmpl->calcCostToBuild( obj->getControllingPlayer() ) );
}

/** One of the map's own Player_N_Start waypoints, by 0-based position index.  Where the start
	* positions *are* is public - the map has them, every human can read them off the preview.  Which
	* of them the enemy is standing on is not, and that is what the AI has to find out. */
static Bool startPositionLoc( Int startNdx, Coord3D *pos )
{
	if( startNdx < 0 || startNdx >= MAX_PLAYER_COUNT || TheTerrainLogic == NULL )
		return FALSE;

	AsciiString name;
	name.format( "Player_%d_Start", startNdx + 1 );		// the waypoints are 1-based
	Waypoint *way = TheTerrainLogic->getWaypointByName( name );
	if( way == NULL )
		return FALSE;

	*pos = *way->getLocation();
	return TRUE;
}

/** Where a player actually started.  This is ground truth, and the AI is only allowed to ask it
	* where it has earned the answer: standing next to the position with a scout, for its own and its
	* allies' positions, or once elimination has left nowhere else for an enemy to be.  Everything
	* that wants an enemy's address goes through enemyStartGuess instead. */
static Bool playerStartPosition( Int playerNdx, Coord3D *pos )
{
	Player *p = ThePlayerList ? ThePlayerList->getNthPlayer( playerNdx ) : NULL;
	if( p == NULL )
		return FALSE;

	return startPositionLoc( p->getMpStartIndex(), pos );
}


/* How often each of the periodic jobs re-checks.  Up here rather than beside the function that uses
	 each one, because the constructor needs them: it spreads the players across each cycle, and it
	 cannot do that without knowing how long the cycle is.  See the staggering block below. */

/** How often the AI looks for somewhere to expand to.  Long, because an expansion is a building
	* order and the base builder has its own rhythm to keep. */
static const Int EXPANSION_CHECK_SECONDS = 60;

/** How often the scout is looked at.  It only ever gets an order when it has arrived, so this is a
	* check, not a repath. */
static const Int SCOUT_CHECK_RATE = 2 * LOGICFRAMES_PER_SECOND;

/** How often the AI looks for something to capture.  It only ever gets an order when it is idle, so
	* this is a check, not a re-path. */
static const Int CAPTURE_CHECK_RATE = 5 * LOGICFRAMES_PER_SECOND;

#ifdef DEBUG_LOGGING
/** Milliseconds between two performance counter readings.  Used by the per-job AI profile below and
	* by the sub-timers inside processBaseBuilding, which is why it lives up here. */
static Real aiPlayerElapsedMS( const Int64 &from, const Int64 &to )
{
	static Int64 freq = 0;
	if( freq == 0 )
		QueryPerformanceFrequency( (LARGE_INTEGER *)&freq );
	if( freq == 0 )
		return 0.0f;
	return (Real)( (double)(to - from) * 1000.0 / (double)freq );
}

/* Sub-timers inside the "base" job, which is the one that got expensive once the expansion convoy
	 was out of the way.  They nest inside it, so they keep their own start variable rather than
	 sharing the per-job one, and the report prints them as a breakdown of base rather than beside
	 it.  Reached through AIPlayer statics because AISkirmishPlayer overrides processBaseBuilding and
	 the version that actually runs in a skirmish lives in the other file. */
static const char *theAIBaseSubName[ AIPlayer::BASE_SUB_COUNT ] =
	{ "hole", "dozerfix", "safe", "finddozer", "canmake", "build" };
static Real theAIBaseSubMS[ AIPlayer::BASE_SUB_COUNT ];
static Int theAIBaseSubCalls[ AIPlayer::BASE_SUB_COUNT ];
static Int64 theAIBaseSubStart;
#endif

/*static*/ void AIPlayer::profileBaseSubBegin( void )
{
#ifdef DEBUG_LOGGING
	QueryPerformanceCounter( (LARGE_INTEGER *)&theAIBaseSubStart );
#endif
}

/*static*/ void AIPlayer::profileBaseSubEnd( Int slot )
{
#ifdef DEBUG_LOGGING
	if( slot < 0 || slot >= BASE_SUB_COUNT )
		return;
	Int64 now;
	QueryPerformanceCounter( (LARGE_INTEGER *)&now );
	theAIBaseSubMS[ slot ] += aiPlayerElapsedMS( theAIBaseSubStart, now );
	++theAIBaseSubCalls[ slot ];
#endif
}

/*static*/ void AIPlayer::profileBaseSubCount( Int slot )
{
#ifdef DEBUG_LOGGING
	if( slot >= 0 && slot < BASE_SUB_COUNT )
		++theAIBaseSubCalls[ slot ];
#endif
}

#ifdef DEBUG_LOGGING
#define AI_BASE_SUB_BEGIN()		AIPlayer::profileBaseSubBegin()
#define AI_BASE_SUB_END(slot)	AIPlayer::profileBaseSubEnd(slot)
#else
#define AI_BASE_SUB_BEGIN()
#define AI_BASE_SUB_END(slot)
#endif

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
AIPlayer::AIPlayer( Player *p ) :
m_player(p),
m_buildDelay(0), 
m_teamDelay(0),	
m_teamTimer(2),	// Important - don't start building teams until frame 1.
m_structureTimer(2), // Important - don't start building structures until frame 1.
m_readyToBuildTeam(false),
m_readyToBuildStructure(false),
m_structuresInQueue(0),
m_repairDozer(INVALID_ID),
m_skillsetSelector(INVALID_SKILLSET_SELECTION),
m_dozerQueuedForRepair(false),
m_supplySourceAttackCheckFrame(0),
m_attackedSupplyCenter(INVALID_ID),
m_teamSeconds(10),
m_curWarehouseID(INVALID_ID),
m_buildProbeOffset(0.0f),
m_scoutTimer(1),
m_retreatTimer(1),
m_expandTimer(1),
m_skillLevel(AISKILL_BRUTAL),
m_role(AIROLE_AGGRESSIVE)
{
	for (Int scoutSlot = 0; scoutSlot < MAX_AI_SCOUTS; ++scoutSlot) {
		m_scoutID[scoutSlot] = INVALID_ID;
		m_scoutTargetFor[scoutSlot] = -1;	// nothing looked at yet; the score picks the first target
	}
	for (Int startNdx = 0; startNdx < MAX_PLAYER_COUNT; ++startNdx) {
		m_scoutSeenFrame[startNdx] = 0;		// 0 == never seen, which outranks every real age below
		m_startChecked[startNdx] = FALSE;
		m_startOccupied[startNdx] = FALSE;
		m_playerStartNdx[startNdx] = -1;	// nobody located yet, not even us - updateStartIntel seeds it
	}
	m_startIntelFrame = 0;
	m_capturerID = INVALID_ID;
	m_captureTimer = 1;

	m_frameLastBuildingBuilt = TheGameLogic->getFrame();
	p->setCanBuildUnits(false); // turn off ai production by default.

	Int i;
	for (i=0; i<MAX_STRUCTURES_TO_REPAIR; i++) {
		m_structuresToRepair[i] = INVALID_ID;
	}
	m_repairDozerOrigin.zero();
	m_buildProbePos.zero();
	m_baseCenter.zero();
	m_baseCenterSet = false;
	m_difficulty = TheScriptEngine->getGlobalDifficulty(); 
	m_skillLevel = skillLevelForDifficulty( m_difficulty );

	//
	// Role is rolled once and kept for the whole match.  Sins is explicit that its AI does not flip
	// personality mid-game, and that consistency is what makes an opponent readable and therefore
	// counterable - an AI that turtles for ten minutes and then rushes is not deep, it is noise.
	// GameLogicRandomValue, so a replay rolls the same one.
	//
	m_role = (GameLogicRandomValue( 0, AIROLE_COUNT - 1 ) == 0) ? AIROLE_AGGRESSIVE : AIROLE_DEFENSIVE;

	m_teamSeconds = TheAI->getAiData()->m_teamSeconds;

	//
	// Stagger this player's repeating checks.  doBaseBuilding re-arms itself for 2 seconds,
	// doTeamBuilding for 5 and updateBridgeRepair for 1, all from constants, and every AIPlayer is
	// built with the same timers on the same frame, so a lobby full of them checks in unison from
	// the first frame to the last and the whole cost of it lands on one logic frame.  The offset
	// moves each player to its own slot in the cycle; nothing runs less often.
	//
	// The two "ready" timers get the offset as well: they are what anchors the cadence that follows
	// (both hand their delay a zero on the frame they expire, and both expire on frame 2 for
	// everyone), so leaving them alone would re-align the players two frames in.  The price is that
	// the last player's first base-building check comes up to 2 seconds late and its first team
	// check up to 5 - once, at the start of the match.
	//
	const Int playerIndex = p->getPlayerIndex();
	m_buildDelay = computeUpdatePhase( playerIndex, 2*LOGICFRAMES_PER_SECOND );
	m_teamDelay = computeUpdatePhase( playerIndex, 5*LOGICFRAMES_PER_SECOND );
	m_structureTimer += m_buildDelay;
	m_teamTimer += m_teamDelay;
	// ...and EA never initialized this one at all.  updateBridgeRepair returns before reading it
	// while the repair queue is empty, which it is here, so it has been benign - but only just.
	m_bridgeTimer = computeUpdatePhase( playerIndex, LOGICFRAMES_PER_SECOND );

	/* The four jobs this fork added start their timers at 1, so every computer player ran every one
		 of them on the same logic frame - and kept doing so for the rest of the match, because each
		 re-arms with a fixed interval and so never drifts apart again.

		 Measured on Twilight Flame with eight top-rung AI: doExpansion alone cost 15.4ms of a
		 16.7ms frame, all of it landing on one frame a minute. Spread across the cycle it is under
		 6ms, and nothing runs any less often than it did. */
	/* What is needed is only that the players land on *different* frames, not that they are spread
		 evenly over the cycle - and the difference matters for the slow ones. Spreading the
		 once-a-minute expansion check over its whole minute delays the last player's first expansion
		 by nearly half of one, which showed up as a smaller army over six matches. A two-second
		 window puts eight players seven frames apart, which is all the convoy needs broken, and
		 costs nobody anything measurable. The offset is permanent either way: each timer re-arms
		 with a fixed interval, so players that start apart stay apart. */
	const Int SPREAD = 2 * LOGICFRAMES_PER_SECOND;
	m_expandTimer += computeUpdatePhase( playerIndex, SPREAD );
	m_captureTimer += computeUpdatePhase( playerIndex, SPREAD );
	m_retreatTimer += computeUpdatePhase( playerIndex, SPREAD );
	// The scout check's own cycle is already only two seconds, so its full cycle is the window.
	m_scoutTimer += computeUpdatePhase( playerIndex, SCOUT_CHECK_RATE );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
AIPlayer::~AIPlayer()
{
	clearTeamsInQueue();
}

// ------------------------------------------------------------------------------------------------
/** Invoked when a structure I am building is finished building. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::onStructureProduced( Object *factory, Object *bldg )
{
	m_teamDelay = 0; // Cause the update queues & selection to happen immediately.
	m_buildDelay = 0; // Cause 
	/* Find the info building this. */
	BuildListInfo *info;
	for( info = m_player->getBuildList(); info; info = info->getNext() )
	{
		if (info->getObjectID() != bldg->getID()) continue;
		Dict d;
		d.setAsciiString(TheKey_objectName, info->getBuildingName());
		d.setAsciiString(TheKey_objectScriptAttachment, info->getScript());
		d.setInt(TheKey_objectInitialHealth, info->getHealth());
		d.setBool(TheKey_objectUnsellable, info->getUnsellable());
		
		info->setUnderConstruction(false);
		bldg->updateObjValuesFromMapProperties(&d);
		// clear the under construction status
		bldg->clearStatus( MAKE_OBJECT_STATUS_MASK2( OBJECT_STATUS_UNDER_CONSTRUCTION, OBJECT_STATUS_RECONSTRUCTING ) );

		// UnderConstruction just cleared, so update our upgrades
		bldg->updateUpgradeModules();

		TheScriptEngine->addObjectToCache(bldg);
		TheScriptEngine->runObjectScript(info->getScript(), bldg);
		if (TheGlobalData->m_debugAI) {
			AsciiString bldgName = bldg->getTemplate()->getName();
			bldgName.concat(" - Building completed.");
			TheScriptEngine->AppendDebugMessage(bldgName, false);
		}
		checkForSupplyCenter(info, bldg);
		return;
	}

	// Look in build list & see if this is spawned from a hole.
	for( info = m_player->getBuildList(); info; info = info->getNext() )
	{
		const ThingTemplate *bldgPlan = TheThingFactory->findTemplate( info->getTemplateName() );
		if (!bldgPlan) {																											 
			continue;
		}		
		if (!bldgPlan->isEquivalentTo(bldg->getTemplate())) {
			continue; // not the same kind of building we're looking for.
		}
		// check for hole.
		if (info->getObjectID() != INVALID_ID) {
			// used to have a building.
			Object *obj = TheGameLogic->findObjectByID( info->getObjectID() );
			if (obj!=NULL) {
				if (obj->isKindOf(KINDOF_REBUILD_HOLE)) {
					RebuildHoleBehaviorInterface *rhbi = RebuildHoleBehavior::getRebuildHoleBehaviorInterfaceFromObject( obj );
					if( rhbi ) {
						ObjectID spawnedID = rhbi->getReconstructedBuildingID();
						if (bldg->getID() == spawnedID) {
							DEBUG_LOG(("AI got rebuilt %s\n", bldgPlan->getName().str()));
							info->setObjectID(bldg->getID());
							return;
						}
					}
				}
			} 
		}
	}

	if (TheGameLogic->getFrame()>0) {
		DEBUG_LOG(("***AI PLAYER-Structure not found in production queue.\n"));
	}
}

// ------------------------------------------------------------------------------------------------
/** See if the building is a supply center, and see how many supply trucks we want. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::checkForSupplyCenter( BuildListInfo *info, Object *bldg )
{
	class SupplyCenterDockUpdate;
	// if it is a supply center, I must have boxes
	static const NameKeyType key_centerUpdate = NAMEKEY("SupplyCenterDockUpdate");
	SupplyCenterDockUpdate *centerModule = (SupplyCenterDockUpdate*)bldg->findUpdateModule( key_centerUpdate );
	if( centerModule  )
	{
		info->setSupplyBuilding(true);
		Int desiredGatherers = 0;
		const AISideInfo *resInfo = TheAI->getAiData()->m_sideInfo;
		while (resInfo) {
			if (resInfo->m_side == m_player->getSide()) {
				GameDifficulty difficulty = m_difficulty;
				if (difficulty == DIFFICULTY_EASY) {
					desiredGatherers = resInfo->m_easy;
				}
				if (difficulty == DIFFICULTY_NORMAL) {
					desiredGatherers = resInfo->m_normal;
				}
				if (difficulty == DIFFICULTY_HARD) {
					desiredGatherers = resInfo->m_hard;	 
				}
			}
			resInfo = resInfo->m_next;
		}
		//
		// Adaptive harvesters (B6): the three INI numbers do not know how much supply is actually
		// parked next to this centre.  One extra gatherer per warehouse beyond the first within the
		// safe radius, capped at two extra - a centre with three piles beside it can keep more
		// trucks busy, and one with a single pile cannot.
		//
		if( getSkillProfile()->m_adaptiveHarvesters )
		{
			Int warehouses = 0;
			PartitionFilterAcceptByKindOf supplyOnly( MAKE_KINDOF_MASK( KINDOF_SUPPLY_SOURCE ), KINDOFMASK_NONE );
			PartitionFilterAlive filterAlive;
			PartitionFilter *filters[] = { &supplyOnly, &filterAlive, 0 };

			MemoryPoolObjectHolder hold;
			SimpleObjectIterator *nearby = ThePartitionManager->iterateObjectsInRange(
					bldg->getPosition(), TheAI->getAiData()->m_supplyCenterSafeRadius, FROM_CENTER_2D, filters );
			hold.hold( nearby );
			for( Object *w = nearby->first(); w; w = nearby->next() )
				++warehouses;

			Int extra = warehouses - 1;
			if( extra < 0 ) extra = 0;
			if( extra > 2 ) extra = 2;
			desiredGatherers += extra;
		}

		info->setSupplyBuilding(true);
		info->setCurrentGatherers(-1);
		info->setDesiredGatherers(desiredGatherers+1); // get a freebie with the supply depots.
	}
}

// ------------------------------------------------------------------------------------------------
/** Queue up a supply truck to be built. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::queueSupplyTruck( void )
{			
	Bool truckInQueue = false;
	for ( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance())
	{
		TeamInQueue *team = iter.cur();
		WorkOrder *order;
		for( order = team->m_workOrders; order; order = order->m_next )
		{
			// GLA dozers (workers) are also resource gatherers, so make sure it isn't a worker. jba.
			if (order->m_isResourceGatherer) {
				truckInQueue = true;
			}
		}
	}	
	
	if (truckInQueue) {
		return; // already building a supply truck.
	}
	Int totalHarvesters = 0;

	// See how many harvesters we have servicing this supply src.
	// Scan my units.
	Player::PlayerTeamList::const_iterator it;
	for (it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it) {
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance()) {
			Team *team = iter.cur();
			if (!team) {
				continue;
			}			
			for (DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance()) {
				Object *obj = objIter.cur();
				if (!obj)  continue;
				if (!obj->isKindOf(KINDOF_HARVESTER)) continue;
				if (!obj->getAI()) continue;

				SupplyTruckAIInterface* supplyTruckAI = obj->getAI()->getSupplyTruckAIInterface();
				if( supplyTruckAI )	{
					totalHarvesters++;
				}
			}
		}
	}

	/* Find the info building this. */
	for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
	{
		if (info->isSupplyBuilding() == false) continue;
		Int desiredGatherers = info->getDesiredGatherers();
		Int curGatherers = info->getCurrentGatherers();

		if (curGatherers>=desiredGatherers) {
			// Check & see if any have died.
			Object *supplyCenter = TheGameLogic->findObjectByID(info->getObjectID());
			// Check for supplies.
			if (supplyCenter) {
				if (supplyCenter->isKindOf(KINDOF_REBUILD_HOLE)) {
					continue; // don't consider rebuild holes.
				}
				// Make sure we have a supplies near it.
				Coord3D center = *supplyCenter->getPosition();
				Real radius = SUPPLY_CENTER_CLOSE_DIST + supplyCenter->getGeometryInfo().getBoundingCircleRadius();

				PartitionFilterAcceptByKindOf f1(MAKE_KINDOF_MASK(KINDOF_SUPPLY_SOURCE), KINDOFMASK_NONE);
				PartitionFilterPlayer f2(m_player, false);	// Only find other.
				PartitionFilterOnMap filterMapStatus;

				PartitionFilter *filters[] = { &f1, &f2, &filterMapStatus, 0 };

				Object *supplySource = ThePartitionManager->getClosestObject(&center, radius, FROM_BOUNDINGSPHERE_2D, filters);
				if (!supplySource) {
					// No supplies.
					continue;
				}
				static const NameKeyType key_warehouseUpdate = NAMEKEY("SupplyWarehouseDockUpdate");
				SupplyWarehouseDockUpdate *warehouseModule = (SupplyWarehouseDockUpdate*)supplySource->findUpdateModule( key_warehouseUpdate );
				if( warehouseModule )	{	 
					Int availableCash = warehouseModule->getBoxesStored()*TheGlobalData->m_baseValuePerSupplyBox;
					if (availableCash<=0) continue;
					if( m_player->getRelationship(supplySource->getTeam()) == ENEMIES ) {
						continue;
					}
				}
				// Ok, it has supplies available near it.
				checkForSupplyCenter(info, supplyCenter);
				Int curGatherers = 0;
				// See how many harvesters we have servicing this supply src.
				// Scan my units.
				Player::PlayerTeamList::const_iterator it;
				for (it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it) {
					for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance()) {
						Team *team = iter.cur();
						if (!team) {
							continue;
						}			
						for (DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance()) {
							Object *obj = objIter.cur();
							if (!obj)  continue;
							if (!obj->isKindOf(KINDOF_HARVESTER)) continue;
							if (!obj->getAI()) continue;

							SupplyTruckAIInterface* supplyTruckAI = obj->getAI()->getSupplyTruckAIInterface();
							if( supplyTruckAI )	{
								ObjectID dock = supplyTruckAI->getPreferredDockID();
								if (dock == supplyCenter->getID()) {
									curGatherers++;
									if (!supplyTruckAI->isCurrentlyFerryingSupplies()) {
										// Note - although this is the ai, we are sending in CMD_FROM_PLAYER.
										// This causes the dock object to stick in the docking interface.
										// The supply truck ai issues dock commands, and they become confused.
										// Thus, player.  jba.  ;(
										obj->getAI()->aiDock(supplyCenter, CMD_FROM_PLAYER);
									}
								}
							}
						}
					}
				}
				//DEBUG_LOG(("Expected %d harvesters, found %d, need %d\n", info->getDesiredGatherers(), 
				//	curGatherers, info->getDesiredGatherers()-curGatherers) );
				info->setCurrentGatherers(curGatherers);
			}
		} else {
			/* See if we have any "loose" harvesters (cause my supply center got nuked.) */
			Player::PlayerTeamList::const_iterator it;
			for (it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it) {
				for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance()) {
					Team *team = iter.cur();
					if (!team) continue;
					for (DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance()) {
						Object *obj = objIter.cur();
						if (!obj)  continue;
						if (!obj->isKindOf(KINDOF_HARVESTER)) continue;
						if (!obj->getAI()) continue;

						SupplyTruckAIInterface* supplyTruckAI = obj->getAI()->getSupplyTruckAIInterface();
						if( supplyTruckAI )	{
							ObjectID dock = supplyTruckAI->getPreferredDockID();
							if (TheGameLogic->findObjectByID(dock)!=NULL) continue;
							if (supplyTruckAI->isCurrentlyFerryingSupplies() || supplyTruckAI->isForcedIntoWantingState()) 
							{
								// This thinks he is a gatherer, but doesn't have a preferred dock id.
								Object *center = TheGameLogic->findObjectByID(info->getObjectID());
								if (center) {
									info->setCurrentGatherers(info->getCurrentGatherers()+1);
									// Note - although this is the ai, we are sending in CMD_FROM_PLAYER.
									// This causes the dock object to stick in the docking interface.
									// The supply truck ai issues dock commands, and they become confused.
									// Thus, player.  jba.  ;(
									obj->getAI()->aiDock(center, CMD_FROM_PLAYER);
									DEBUG_LOG(("Re-attaching supply truck to supply center.\n"));
									return;
								}
							}
						}
					}
				}
			}
			if (totalHarvesters >= desiredGatherers*3) {
				continue; // we got lotsa gatherers.
			}
			Bool canBuildUnits = m_player->getCanBuildUnits();
			// If we need a supply truck thingy, turn on unit building for a moment.
			m_player->setCanBuildUnits(true);
			const ThingTemplate *tTemplate = TheThingFactory->firstTemplate();
			while (tTemplate) {	 
				Bool isSupplyTruck = tTemplate->isKindOf(KINDOF_HARVESTER);;
				if (isSupplyTruck) {
					Object *factory = findFactory(tTemplate, false);
					if (factory) {
						// we can build one.
						WorkOrder *order = newInstance(WorkOrder);
						order->m_thing = tTemplate;
						order->m_factoryID = INVALID_ID;
						order->m_numRequired = 1;
						order->m_required = true;
						order->m_isResourceGatherer =true;
						// prepend to head of list
						order->m_next = NULL;
						TeamInQueue *team = newInstance(TeamInQueue);
						// Put in front of queue.
						prependTo_TeamBuildQueue(team);
						team->m_priorityBuild = true;
						team->m_workOrders = order;
						team->m_frameStarted = TheGameLogic->getFrame();
						// Stick it on the default team
						team->m_team = m_player->getDefaultTeam(); 
						AsciiString teamName = "Supply truck - building one at the ";
						teamName.concat(factory->getTemplate()->getName());
						TheScriptEngine->AppendDebugMessage(teamName, false);
						m_teamDelay = 0;
						if (info->getCurrentGatherers()==-1) {
							// First one is automatic. jba.
							order->m_factoryID = factory->getID();
							info->setCurrentGatherers(0);
						}	else {
							startTraining( order, team->m_priorityBuild, team->m_team->getName());
						}
						break;
					}
				}
				tTemplate = tTemplate->friend_getNextTemplate();
			}
			m_player->setCanBuildUnits(canBuildUnits);
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
static void deleteQueue(TeamInQueue* o)
{
	if (o)
	{
		o->deleteInstance();
	}
}

// ------------------------------------------------------------------------------------------------
/** Clear the current work order */
// ------------------------------------------------------------------------------------------------
void AIPlayer::clearTeamsInQueue( void )
{
	removeAll_TeamBuildQueue(deleteQueue);
	removeAll_TeamReadyQueue(deleteQueue);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Object *AIPlayer::buildStructureNow(const ThingTemplate *bldgPlan, BuildListInfo *info)
{

	// inst-construct the building
	Object *bldg = TheBuildAssistant->buildObjectNow( NULL, 
																						bldgPlan,
																						info->getLocation(),
																						info->getAngle(),
																						m_player );

	// store the object with the build order  
	if (bldg)
	{
		Dict d;
		d.setAsciiString(TheKey_objectName, info->getBuildingName());
		d.setAsciiString(TheKey_objectScriptAttachment, info->getScript());
		d.setInt(TheKey_objectInitialHealth, info->getHealth());
		d.setBool(TheKey_objectUnsellable, info->getUnsellable());
		
		bldg->updateObjValuesFromMapProperties(&d);

		info->setObjectID( bldg->getID() );
		info->setObjectTimestamp( TheGameLogic->getFrame()+1 );	// has to be non-zero, so just add 1.

		// clear the under construction status
		bldg->clearStatus( MAKE_OBJECT_STATUS_MASK2( OBJECT_STATUS_UNDER_CONSTRUCTION, OBJECT_STATUS_RECONSTRUCTING ) );

		// UnderConstruction just cleared, so update our upgrades
		bldg->updateUpgradeModules();

		if (TheGlobalData->m_debugAI) {
			AsciiString bldgName = bldgPlan->getName();
			bldgName.concat(" - Building completed.");
			TheScriptEngine->AppendDebugMessage(bldgName, false);
		}
		TheScriptEngine->addObjectToCache(bldg);
		TheScriptEngine->runObjectScript(info->getScript(), bldg);
		checkForSupplyCenter(info, bldg);
		ExitInterface *exitInterface = bldg->getObjectExitInterface();
		if( exitInterface )
		{
			Coord3D rallyPoint;
			Bool gotOffset = false;
			if (fabs(info->getRallyOffset()->x) > 1.0f || fabs(info->getRallyOffset()->y)>1.0f) {
				gotOffset = true;	// was a bare statement, so RallyPointOffset was never applied
			}
			if (!exitInterface->getNaturalRallyPoint(rallyPoint)) {
				rallyPoint = *info->getLocation();
			}
			if (gotOffset) {
				rallyPoint.x += info->getRallyOffset()->x;
				rallyPoint.y += info->getRallyOffset()->y;
				exitInterface->setRallyPoint(&rallyPoint);
			}
		}
	} // bldg built
	return bldg;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Object *AIPlayer::buildStructureWithDozer(const ThingTemplate *bldgPlan, BuildListInfo *info)
{
	// Find a dozer.
	Object *dozer = findDozer(info->getLocation());
	if (dozer==NULL) {
		return NULL;
	}
	// Check available funds.
	Money *money = m_player->getMoney();
	if (money->countMoney()<bldgPlan->calcCostToBuild(m_player)) {
		return NULL;
	}

	/* One building placement per logic frame, across every computer player.
		 Placing a building is the expensive half of base building: when the chosen spot is taken,
		 isLocationLegalToBuild is called over a ring of candidate positions reaching 120 pathfind
		 cells for a skirmish AI, and finding a spot out at the edge of that costs 10ms - measured.
		 One of those is a frame nobody notices; the slow frames in an eight-player match were two
		 landing together, because a completed building shortcuts the timer that was meant to keep
		 the players apart.

		 The loser waits exactly one frame - a thirtieth of a second on a building that takes half a
		 minute to put up. The order is the player list order, so every machine defers the same
		 player on the same frame and a replay still matches. */
	static UnsignedInt s_lastPlacementFrame = 0;
	const UnsignedInt nowFrame = TheGameLogic->getFrame();
	if (s_lastPlacementFrame == nowFrame && nowFrame != 0) {
		m_buildDelay = 1;		// try again next frame; doBaseBuilding leaves any value >= 1 alone
		return NULL;
	}
	s_lastPlacementFrame = nowFrame;
	// construct the building
	Coord3D pos = *info->getLocation();
	pos.z += TheTerrainLogic->getGroundHeight(pos.x, pos.y);
	if( !dozer->getAIUpdateInterface() )
	{
		return NULL;
	}
	Real angle = info->getAngle();
 	if( TheBuildAssistant->isLocationLegalToBuild( &pos, bldgPlan, angle,
																								 BuildAssistant::NO_ENEMY_OBJECT_OVERLAP,
																								 dozer, m_player ) != LBC_OK ) {
		// If there's enemy units or structures, don't build/rebuild.
		TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback, turn it off.  jba.
		return NULL;
	}

	// validate the the position to build at is valid
	if( TheBuildAssistant->isLocationLegalToBuild( &pos, bldgPlan, angle,
																								 BuildAssistant::CLEAR_PATH |
																								 BuildAssistant::TERRAIN_RESTRICTIONS |
																								 BuildAssistant::NO_OBJECT_OVERLAP,
																								 dozer, m_player ) != LBC_OK ) {
			// Warn.
			AsciiString bldgName = bldgPlan->getName();
			bldgName.concat(" - Dozer unable to place.  Attempting to adjust position.");
			TheScriptEngine->AppendDebugMessage(bldgName, false);

			// try to fix.
			Real posOffset;
			Bool valid = false;
			// Wiggle it a little :)
			Real limit = 10*PATHFIND_CELL_SIZE_F;
			if (isSkirmishAI()) {
				limit = 120*PATHFIND_CELL_SIZE_F;
			}
			Coord3D newPos = pos;

			/* One ring of that wiggle per logic frame.

				 The spot in the build list is taken, so this walks a square ring outwards looking for
				 one that is not, and for a skirmish AI it walks it 120 pathfind cells out - most of a
				 generated map. Every position costs a call to isLocationLegalToBuild, which is a
				 partition query for overlapping objects and a zone check for a route to it, and the
				 rings get longer the further out they go: 3,720 of them by the outermost, 46ms in one
				 logic frame, measured, and the worst frame of a four-player Twilight Flame match.
				 It is rare - four such frames in 55,876 - and that is exactly what a stutter is.

				 So the scan gets a budget and remembers where it was. It stops at the end of whichever
				 ring takes it past BUILD_PROBES_PER_FRAME positions, asks to be called again next
				 frame, and carries on from that ring; the positions are tried in the same order they
				 always were, so the spot it settles on is the spot it would have found in one go. The
				 budget counts positions rather than milliseconds on purpose: a stopwatch would test a
				 different number of them on a slower machine, and the two would desync.

				 A ring is finished once started rather than resumed part way through, which keeps the
				 whole of the state in one number.

				 The budget is small because the positions are not equally expensive. The rings near
				 the base are the dear ones - the partition query there comes back full of the
				 player's own buildings, and a position that gets past it pays for the terrain
				 sampling as well - while the outer rings are mostly off the map and are rejected on
				 the first line. A budget of 100 left a 19.7ms frame made of about 120 inner
				 positions; at 32 the first frame walks three rings and the worst frame this can cost
				 is either those, or one whole outer ring of 240 cheap ones. */
			const Int BUILD_PROBES_PER_FRAME = 32;
			Int probes = 0;
			Bool outOfBudget = false;
			Real firstOffset = 0;
			if (m_buildProbeOffset > 0 &&
					m_buildProbePos.x == pos.x && m_buildProbePos.y == pos.y) {
				firstOffset = m_buildProbeOffset;		// same spot as last frame: carry on from there
			}
			m_buildProbePos = pos;
			m_buildProbeOffset = 0;

			for (posOffset = firstOffset; posOffset<limit; posOffset += 2*PATHFIND_CELL_SIZE_F) {
				if (probes >= BUILD_PROBES_PER_FRAME) {
					// out of budget with rings left to walk: pick this one up again next frame
					m_buildProbeOffset = posOffset;
					outOfBudget = true;
					break;
				}
				if (isSkirmishAI()) {
					posOffset += 2*PATHFIND_CELL_SIZE_F;
				}
				Real offset = posOffset/2;
				Real xPos, yPos;
				yPos = pos.y-offset;
				for (xPos = pos.x-offset; xPos <= pos.x+offset; xPos+=PATHFIND_CELL_SIZE_F) {
					if (isSkirmishAI()) xPos += PATHFIND_CELL_SIZE_F;
					probes += 2;
					newPos.x = xPos;
					newPos.y = yPos;
					valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, bldgPlan, angle,
																							 BuildAssistant::CLEAR_PATH |
																							 BuildAssistant::TERRAIN_RESTRICTIONS |
																							 BuildAssistant::NO_OBJECT_OVERLAP,
																							 dozer, m_player ) == LBC_OK;
					if (valid) break;
					newPos.y = yPos+posOffset;
					valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, bldgPlan, angle,
																							 BuildAssistant::CLEAR_PATH |
																							 BuildAssistant::TERRAIN_RESTRICTIONS |
																							 BuildAssistant::NO_OBJECT_OVERLAP,
																							 dozer, m_player ) == LBC_OK;
				}
				if (valid) break;
				xPos = pos.x-offset;
				for (yPos = pos.y-offset; yPos <= pos.y+offset; yPos+=PATHFIND_CELL_SIZE_F) {
					if (isSkirmishAI()) yPos += PATHFIND_CELL_SIZE_F;
					probes += 2;
					newPos.x = xPos;
					newPos.y = yPos;
					valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, bldgPlan, angle,
																							 BuildAssistant::CLEAR_PATH |
																							 BuildAssistant::TERRAIN_RESTRICTIONS |
																							 BuildAssistant::NO_OBJECT_OVERLAP,
																							 dozer, m_player ) == LBC_OK;
					if (valid) break;
					newPos.x = xPos+posOffset;
					valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, bldgPlan, angle,
																							 BuildAssistant::CLEAR_PATH |
																							 BuildAssistant::TERRAIN_RESTRICTIONS |
																							 BuildAssistant::NO_OBJECT_OVERLAP,
																							 dozer, m_player ) == LBC_OK;
				}
				if (valid) break;
			}
			if (valid) pos = newPos;
			if (!valid && outOfBudget) {
				/* Out of budget with the search unfinished. The fallback below settles for the
					 original spot, and taking it here would be answering a question this frame has not
					 finished asking. */
				TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback
				m_buildDelay = 1;		// try again next frame; doBaseBuilding leaves any value >= 1 alone
				return NULL;
			}
			if (!valid) {
				valid = TheBuildAssistant->isLocationLegalToBuild( &pos, bldgPlan, angle,
																						 BuildAssistant::NO_ENEMY_OBJECT_OVERLAP,
																						 dozer, m_player ) == LBC_OK;
				if (!valid) {
					return NULL;
				}
			}

	}

	TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback, turn it off.  jba.
	if (!TheAI->pathfinder()->clientSafeQuickDoesPathExist(dozer->getAI()->getLocomotorSet(),
		dozer->getPosition(), &pos)) {
		AsciiString bldgName = bldgPlan->getName();
		bldgName.concat(" - Dozer unable to reach building.  Teleporting.");
		TheScriptEngine->AppendDebugMessage(bldgName, false);
		dozer->setPosition(&pos);
	}

	Object *bldg = TheBuildAssistant->buildObjectNow( dozer, 
																						bldgPlan,
																						&pos,
																						angle,
																						m_player );



#if defined _DEBUG || defined _INTERNAL
	if (TheGlobalData->m_debugAI == AI_DEBUG_PATHS)
	{
		extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
 		RGBColor color;
		color.blue = 0;
		color.red = 1;
		color.green = 0;
		Coord3D myPos;
		myPos = *dozer->getPosition();
		myPos.z = TheTerrainLogic->getGroundHeight( myPos.x, myPos.y ) + 0.5f;
		addIcon(&myPos, 2*PATHFIND_CELL_SIZE_F, 120, color);
		myPos = pos;
		myPos.z = TheTerrainLogic->getGroundHeight( myPos.x, myPos.y ) + 0.5f;
		addIcon(&myPos, 2*PATHFIND_CELL_SIZE_F, 120, color);	
		Real dx, dy;
		dx = dozer->getPosition()->x - pos.x;
		dy = dozer->getPosition()->y - pos.y;

		Int count = sqrt(dx*dx+dy*dy)/(PATHFIND_CELL_SIZE_F/2);
		if (count<2) count = 2;
		Int i;
		color.green = 1;
		for (i=1; i<count; i++) {
			myPos.x = dozer->getPosition()->x + (pos.x-dozer->getPosition()->x)*i/count;
			myPos.y = dozer->getPosition()->y + (pos.y-dozer->getPosition()->y)*i/count;
			myPos.z = TheTerrainLogic->getGroundHeight( myPos.x, myPos.y ) + 0.5f;
			addIcon(&myPos, PATHFIND_CELL_SIZE_F/2, 120, color);

		}
	}	
#endif

	// store the object with the build order  
	if (bldg)
	{
		ExitInterface *exitInterface = bldg->getObjectExitInterface();
		if( exitInterface )
		{
			Coord3D rallyPoint;
			Bool gotOffset = false;
			if (fabs(info->getRallyOffset()->x) > 1.0f || fabs(info->getRallyOffset()->y)>1.0f) {
				gotOffset = true;	// was a bare statement, so RallyPointOffset was never applied
			}
			if (!exitInterface->getNaturalRallyPoint(rallyPoint)) {
				rallyPoint = *info->getLocation();
			}
			if (gotOffset) {
				rallyPoint.x += info->getRallyOffset()->x;
				rallyPoint.y += info->getRallyOffset()->y;
				exitInterface->setRallyPoint(&rallyPoint);
			}
		}
		info->setObjectID( bldg->getID() );
		info->setObjectTimestamp( TheGameLogic->getFrame()+1 );	// Has to be non-zero, so add 1.
		info->setUnderConstruction(true);

		if (TheGlobalData->m_debugAI) {
			AsciiString bldgName = bldgPlan->getName();
			bldgName.concat(" - Building started.");
			TheScriptEngine->AppendDebugMessage(bldgName, false);
		}
	} // bldg built
	TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback, turn it off.  jba.
	return bldg;
}

// ------------------------------------------------------------------------------------------------
/** Build our base. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::processBaseBuilding( void )
{
	//
	// Refresh base buildings. Scan through list, if a building is missing,
	// rebuild it, unless it's rebuild count is zero.
	//
	if (m_readyToBuildStructure)
	{

		for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
		{
			AsciiString name = info->getTemplateName();
			if (name.isEmpty()) continue;
			const ThingTemplate *bldgPlan = TheThingFactory->findTemplate( name );
			if (!bldgPlan) {																											 
				DEBUG_LOG(("*** ERROR - Build list building '%s' doesn't exist.\n", name.str()));
				continue;
			}
			// check for hole.
			if (info->getObjectID() != INVALID_ID) {
				// used to have a building.
				Object *bldg = TheGameLogic->findObjectByID( info->getObjectID() );
				if (bldg==NULL) {
					// got destroyed.
					ObjectID priorID;
					priorID = info->getObjectID();
					info->setObjectID(INVALID_ID);
					info->setObjectTimestamp(TheGameLogic->getFrame()+1);
					// Scan for a GLA hole.	KINDOF_REBUILD_HOLE
					AI_BASE_SUB_BEGIN();
					Object *obj;
					for( obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() ) {
						if (!obj->isKindOf(KINDOF_REBUILD_HOLE)) continue;
						RebuildHoleBehaviorInterface *rhbi = RebuildHoleBehavior::getRebuildHoleBehaviorInterfaceFromObject( obj );
						if( rhbi ) {
							ObjectID spawnerID = rhbi->getSpawnerID();
							if (priorID == spawnerID) {
								DEBUG_LOG(("AI Found hole to rebuild %s\n", bldgPlan->getName().str()));
								info->setObjectID(obj->getID());
							}
						}
 					}
					AI_BASE_SUB_END( BASE_SUB_HOLE );
				}	else {
					if (bldg->getControllingPlayer() == m_player) {
						// Check for built or dozer missing.
						if( bldg->getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
						{
							AI_BASE_SUB_BEGIN();
							// make sure dozer is working on him.
							ObjectID builder = bldg->getBuilderID();
							Object* myDozer = TheGameLogic->findObjectByID(builder);
							if (myDozer==NULL) {
								DEBUG_LOG(("AI's Dozer got killed.  Find another dozer.\n"));
 								myDozer = findDozer(bldg->getPosition());
								if (myDozer==NULL || myDozer->getAI()==NULL) {
									AI_BASE_SUB_END( BASE_SUB_DOZERFIX );
									continue;
								}
								myDozer->getAI()->aiResumeConstruction(bldg, CMD_FROM_AI);
							}	else {
								// make sure he is building.
								myDozer->getAI()->aiResumeConstruction(bldg, CMD_FROM_AI);
							}
							AI_BASE_SUB_END( BASE_SUB_DOZERFIX );
						}
					} else {
						// oops, got captured.
						info->setObjectID(INVALID_ID);
						info->setObjectTimestamp(TheGameLogic->getFrame()+1);
					}	
				}
			}
			if (info->getObjectID()==INVALID_ID && info->getObjectTimestamp()>0) {
				// this object was built at some time, and got destroyed at or near objectTimestamp.
				// Wait a few seconds before initiating a rebuild.
				if (info->getObjectTimestamp()+TheAI->getAiData()->m_rebuildDelaySeconds*LOGICFRAMES_PER_SECOND > TheGameLogic->getFrame()) {
					continue;
				}	else {
					DEBUG_LOG(("Enabling rebuild for %s\n", info->getTemplateName().str()));
					info->setObjectTimestamp(0); // ready to build.
				}
			}
			// check if this building has any "rebuilds" left
			if (info->isBuildable())
			{
				Object *bldg = TheGameLogic->findObjectByID( info->getObjectID() );

				if (bldg == NULL)
				{


#ifdef USE_DOZER
					// dozer-construct the building
					AI_BASE_SUB_BEGIN();
					bldg = buildStructureWithDozer(bldgPlan, info);
					AI_BASE_SUB_END( BASE_SUB_BUILD );
					// store the object with the build order
					if (bldg)
					{
						info->setObjectID( bldg->getID() );
						info->decrementNumRebuilds();

						m_readyToBuildStructure = false;
						m_structureTimer = computeStructureDelay();
						m_frameLastBuildingBuilt = TheGameLogic->getFrame();
						// only build one building per delay loop
						break;
					} // bldg built

#else
					// force delay between rebuilds
					if (TheGameLogic->getFrame() - m_frameLastBuildingBuilt < framesToBuild) 
					{
						m_buildDelay = framesToBuild - (TheGameLogic->getFrame() - m_frameLastBuildingBuilt); 
						return;
					}	else {
						// building is missing, (re)build it
						// deduct money to build, if we have it
						Int cost = bldgPlan->calcCostToBuild( m_player );
						if (m_player->getMoney()->countMoney() >= cost)
						{
							// we have the money, deduct it
							m_player->getMoney()->withdraw( cost );

							// inst-construct the building
							bldg = buildStructureNow(bldgPlan, info, NULL);
							// store the object with the build order
							if (bldg)
							{
								info->setObjectID( bldg->getID() );
								info->decrementNumRebuilds();

								m_readyToBuildStructure = false;
								m_structureTimer = computeStructureDelay();
								m_frameLastBuildingBuilt = TheGameLogic->getFrame();
								// only build one building per delay loop
								break;
							} // bldg built
						} // have money
					} // rebuild delay ok
#endif
				} // building missing
			} // is buildable
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** A team is about to be destroyed */
//-------------------------------------------------------------------------------------------------
void AIPlayer::aiPreTeamDestroy( const Team *deletedTeam )
{
	{
		for (DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance())
		{
			TeamInQueue *team = iter.cur();
			if (team->m_team == deletedTeam) {
				// The members of the team all got killed before we could finish building the team.
				removeFrom_TeamBuildQueue(team);
				team->deleteInstance();
				iter = iterate_TeamBuildQueue();
			}
		}
	}
	{
		for ( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamReadyQueue(); !iter.done(); iter.advance())
		{
			TeamInQueue *team = iter.cur();
			if (team->m_team == deletedTeam) {
				// The members of the team all got killed before we could activate the team.
				removeFrom_TeamReadyQueue(team);
				team->deleteInstance();
				iter = iterate_TeamReadyQueue();
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Guard supply center */
//-------------------------------------------------------------------------------------------------
void AIPlayer::guardSupplyCenter( Team *team, Int minSupplies )
{
	m_supplySourceAttackCheckFrame = 0; // force check.
	Object *warehouse = NULL;
	if (isSupplySourceAttacked()) {
		warehouse = TheGameLogic->findObjectByID(m_attackedSupplyCenter);
	}
	if (warehouse==NULL) {
		warehouse = findSupplyCenter(minSupplies);
	}
	if (warehouse) {

		AIGroup* theGroup = TheAI->createGroup();
		if (!theGroup) {
			return;
		}
		team->getTeamAsAIGroup(theGroup);
		Coord3D location = *warehouse->getPosition();
		// It's probably a defensive move - position towards the enemy.
		Region2D bounds;
		// getSkirmishEnemyPlayer() can be NULL (no resolvable human enemy, e.g. an all-AI game);
		// every caller in ScriptActions checks it, these two did not.
		Player *skirmishEnemy = TheScriptEngine->getSkirmishEnemyPlayer();
		Coord3D offset;
		offset.zero();
		if (skirmishEnemy)
		{
			getPlayerStructureBounds(&bounds, skirmishEnemy->getPlayerIndex(), FALSE, m_player->getPlayerIndex());
			offset.x = location.x - (bounds.lo.x+bounds.hi.x)*0.5f;
			offset.y = location.y - (bounds.lo.y+bounds.hi.y)*0.5f;
			offset.normalize();
		}
		Real radius = warehouse->getGeometryInfo().getBoundingCircleRadius()*0.8f;

		location.x -= offset.x*radius;
		location.y -= offset.y*radius;
		theGroup->groupGuardPosition( &location, GUARDMODE_NORMAL, CMD_FROM_SCRIPT );

	}
}

//-------------------------------------------------------------------------------------------------
/** Is a supply source attacked? */
//-------------------------------------------------------------------------------------------------
Bool AIPlayer::isSupplySourceAttacked( void )
{
	// don't scan more often than every 10 seconds. This was a bare 10 used as a FRAME count, so
	// the throttle was a third of a second and, worse, the "have I been attacked recently" test
	// below only saw damage that landed in the last 10 frames.
	const Int SCAN_RATE = 10 * LOGICFRAMES_PER_SECOND;
	UnsignedInt curFrame = TheGameLogic->getFrame();
	if (curFrame==0) {
		m_supplySourceAttackCheckFrame = curFrame+SCAN_RATE;
		return false; // can't be attacked on first frame.
	}
	m_attackedSupplyCenter = INVALID_ID;
	if (curFrame < m_supplySourceAttackCheckFrame) {
		return false;
	}
	if (m_player->getAttackedFrame()+SCAN_RATE < curFrame) {
		return false; // haven't been attacked recently.
	}
	m_supplySourceAttackCheckFrame = curFrame+SCAN_RATE;

	// Scan my units.
	Player::PlayerTeamList::const_iterator it;
	for (it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it) {
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance()) {
			Team *team = iter.cur();
			if (!team) {
				continue;
			}			
			for (DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance()) {
				Object *obj = objIter.cur();
				if (!obj) {
					continue;
				}
				if (!obj->isKindOf(KINDOF_CASH_GENERATOR) && !obj->isKindOf(KINDOF_DOZER) &&
					!obj->isKindOf(KINDOF_HARVESTER)) {
					continue;
				}
				// check for attacked.
				BodyModuleInterface *body = obj->getBodyModule();
				if (body) {
					const DamageInfo *info = body->getLastDamageInfo();
					if (info) {
						if (info->out.m_noEffect) {
							continue;
						}
						if (body->getLastDamageTimestamp() + SCAN_RATE > curFrame) {
							// winner.
							m_attackedSupplyCenter = obj->getID();
							return true;
						}
					}
				}
			}
		}
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
/** Is the nearest supply source safe? */
//-------------------------------------------------------------------------------------------------
Bool AIPlayer::isSupplySourceSafe( Int minSupplies )
{
	Object *warehouse = findSupplyCenter(minSupplies);
	if (warehouse==NULL) return true; // it's safe cause it doesn't exist.
	return (isLocationSafe(warehouse->getPosition(), warehouse->getTemplate()));
}

//-------------------------------------------------------------------------------------------------
/** Is this location safe for building this thing? */
//-------------------------------------------------------------------------------------------------
Bool AIPlayer::isLocationSafe(const Coord3D *pos, const ThingTemplate *tthing )
{
	if (tthing == NULL) return 0;

	// See if we have enemies.
	Real radius = TheAI->getAiData()->m_supplyCenterSafeRadius;
	radius += tthing->getTemplateGeometryInfo().getBoundingCircleRadius();

	// only consider enemies.
	PartitionFilterPlayerAffiliation	filterTeam(m_player, (ALLOW_ALLIES|ALLOW_NEUTRAL), false);

	// and only stuff that is not dead
	PartitionFilterAlive filterAlive;

	// and only stuff that isn't stealthed (and not detected)
	// (note that stealthed allies aren't hidden from us, but we're only looking for enemies here)
	PartitionFilterRejectByObjectStatus filterStealth( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_STEALTHED ), 
																										 MAKE_OBJECT_STATUS_MASK2( OBJECT_STATUS_DETECTED, OBJECT_STATUS_DISGUISED ) );

	// (optional) only stuff that is significant
	PartitionFilterInsignificantBuildings filterInsignificant(true, false);

	PartitionFilterRejectByKindOf	filterHarvesters(MAKE_KINDOF_MASK(KINDOF_HARVESTER), KINDOFMASK_NONE);

	PartitionFilterRejectByKindOf	filterDozer(MAKE_KINDOF_MASK(KINDOF_DOZER), KINDOFMASK_NONE);

	PartitionFilter *filters[16];
	Int numFilters = 0;

	filters[numFilters++] = &filterTeam;
	filters[numFilters++] = &filterAlive;
	filters[numFilters++] = &filterStealth;

	filters[numFilters++] = &filterInsignificant;
	filters[numFilters++] = &filterHarvesters;
	filters[numFilters++] = &filterDozer;
	filters[numFilters] = NULL;

	Object *enemy = ThePartitionManager->getClosestObject(  pos, radius, FROM_BOUNDINGSPHERE_2D, filters );
	if (enemy!=NULL) {
		return false;
	}
	return true;

}  // isSupplySourceSafe


// ------------------------------------------------------------------------------------------------
/** Invoked when a unit I am training comes into existence */
// ------------------------------------------------------------------------------------------------
void AIPlayer::onUnitProduced( Object *factory, Object *unit )
{
	Bool found = false;
	// this was uninitialised and is only assigned for units that actually have a SupplyTruck AI
	// (USA/China dozers do not), so stack garbage decided whether m_repairDozer got bound - and
	// that made an AI game non-deterministic.
	Bool supplyTruck = false;

	// factory could be NULL at the start of the game.
	if (factory == NULL) {
		return;
	}

	for (DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance())
	{
		TeamInQueue *team = iter.cur();
		// find work order entry and delete it
		WorkOrder *order;
		if (found) break;
		for( order = team->m_workOrders; order; order = order->m_next )
		{
			if (order->m_factoryID == factory->getID() && order->m_numCompleted < order->m_numRequired && unit->getTemplate()->isEquivalentTo(order->m_thing))
			{
				// found associated order, mark it complete.
				order->m_numCompleted++;
				// put new unit into the team under construction
				if (team->m_team)
					unit->setTeam( team->m_team );
				if (team->m_reinforcement) {
					team->m_reinforcementID = unit->getID();
				}
				AIUpdateInterface *ai = unit->getAIUpdateInterface();
				if (team->m_team->getPrototype()->getTemplateInfo()->m_hasHomeLocation) {
					if (ai) {
						std::vector<Coord3D> path;
						path.push_back( *ai->getGoalPosition() );
						path.push_back(team->m_team->getPrototype()->getTemplateInfo()->m_homeLocation);
						ai->aiFollowExitProductionPath(&path, NULL, CMD_FROM_AI);
					}
				}

				order->m_factoryID = INVALID_ID; // no longer using this factory.
				if (ai) {
					// tell it to start gathering resources.
					// Here is the special bit for this exit style, force wanting on SupplyTruck types
					SupplyTruckAIInterface* supplyTruckAI = ai->getSupplyTruckAIInterface();
					if( supplyTruckAI )	{
						if (order->m_isResourceGatherer) {
							supplyTruck = true;
						} else {
							supplyTruck = false;
						}
						supplyTruckAI->setForceWantingState(supplyTruck);
						if (supplyTruck) {
							// assign to a supply depot.
							for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
							{
								if (info->isSupplyBuilding() && info->getDesiredGatherers()>0 &&
									info->getDesiredGatherers()>info->getCurrentGatherers()) {
										Object *obj = TheGameLogic->findObjectByID(info->getObjectID());
										if (obj) {
											info->setCurrentGatherers(info->getCurrentGatherers()+1);
											// Note - although this is the ai, we are sending in CMD_FROM_PLAYER.
											// This causes the dock object to stick in the docking interface.
											// The supply truck ai issues dock commands, and they become confused.
											// Thus, player.  jba.  ;(
											ai->aiDock(obj, CMD_FROM_PLAYER);
										}
									}
							}

						}
					}
				}
				found = true;
				break;
			}
		}
	}
	if (!supplyTruck && unit->isKindOf(KINDOF_DOZER)) {
		if (m_dozerQueuedForRepair) {
			m_repairDozer = unit->getID();
			m_dozerQueuedForRepair =false;
		} else {
			m_buildDelay = 0;
			m_structureTimer = 1;
		}
	}
	if (!found) {
		DEBUG_LOG(("***AI PLAYER-Unit not found in production queue.\n"));
	}

	m_teamDelay = 0; // Cause the update queues & selection to happen immediately.
}

//----------------------------------------------------------------------------------------------------------
/**
 * Find a good spot to fire a superweapon.
 */
Bool AIPlayer::computeSuperweaponTarget(const SpecialPowerTemplate *power, Coord3D *retPos, Int playerNdx, Real weaponRadius)
{

  Bool success = FALSE;

	Region2D bounds;
	getPlayerStructureBounds(&bounds, playerNdx, FALSE, m_player->getPlayerIndex());

	if( bounds.hi.x == 0 
		&& bounds.lo.x == 0 
		&& bounds.hi.y == 0 
		&& bounds.lo.y == 0 
		)
	{
		Region3D bounds3D;
		// Degenerate bounds because he has no buildings.  Don't give up, scan the whole map for the leftovers.
		TheTerrainLogic->getExtent(&bounds3D);
		bounds.hi.x = bounds3D.hi.x;
		bounds.lo.x = bounds3D.lo.x;
		bounds.hi.y = bounds3D.hi.y;
		bounds.lo.y = bounds3D.lo.y;
	}

	if (weaponRadius<1.0f) {
		weaponRadius = 1.0f; // sanity to avoid divide by 0.
	}

	Int xCount, yCount;
	bounds.lo.x += weaponRadius;
	bounds.hi.x -= weaponRadius;
	if (bounds.hi.x<bounds.lo.x) {
		bounds.hi.x = bounds.lo.x = (bounds.hi.x+bounds.lo.x)/2.0f;
	}
	if (bounds.hi.y<bounds.lo.y) {
		bounds.hi.y = bounds.lo.y = (bounds.hi.y+bounds.lo.y)/2.0f;
	}
	
	xCount = REAL_TO_INT_CEIL(bounds.width()/weaponRadius)+1;
	yCount = REAL_TO_INT_CEIL(bounds.height()/weaponRadius)+1;

	if (xCount>10) xCount = 10;
	if (yCount>10) yCount = 10;

	Int cash = -1;
	Coord3D pos;
	Coord3D bestPos;
	Int x, y, xDelta, yDelta, xIndex, yIndex, xStart, yStart;

	Bool targetMilitaryUnits = TRUE;
	if( power->getSpecialPowerType() == SPECIAL_SNEAK_ATTACK )
	{
		//Military units will have a negative effect on where to drop the special power.
		//We want to target rich areas that are poorly defended or undefended.
		targetMilitaryUnits = FALSE;
	}

	//Randomize which way we iterate the grid. We don't always want to start in the bottom left corner incase
	//of a bad calculation, it'll would always end up there.
	switch( GameLogicRandomValue( 1, 4 ) )
	{
		case 1:
			//x min to max, y min to max
			xDelta = 1,	yDelta = 1;
			xStart = 0,	yStart = 0;
			break;
		case 2:
			//x max to min, y min to max
			xDelta = -1, yDelta = 1;
			xStart = xCount, yStart = 0;
			break;
		case 3:
			//x min to max, y max to min
			xDelta = 1, yDelta = -1;
			xStart = 0, yStart = yCount;
			break;
		case 4:
		default:
			//x max to min, y max to min
			xDelta = -1, yDelta = -1;
			xStart = xCount, yStart = yCount;
			break;
	}

	//Calculate the generally best position
	xIndex = xStart;
	for( x = 0; x < xCount; x++, xIndex += xDelta ) 
	{
		yIndex = yStart;
		for( y = 0; y < yCount; y++, yIndex += yDelta ) 
		{
			pos.x = bounds.lo.x + ( bounds.width() * xIndex ) / xCount;
			pos.y = bounds.lo.y + ( bounds.height() * yIndex ) / yCount;
			pos.z = 0;
			Int curCash = getPlayerSuperweaponValue( &pos, playerNdx, 2*weaponRadius, targetMilitaryUnits, m_player->getPlayerIndex() );
			if ( curCash > cash) 
			{
				cash = curCash;
				bestPos = pos;
			}
		}
	}

	//Fine tune that position by looking at a even smaller radius.
	Coord3D veryBestPos;
	xCount = 11;
	yCount = 11;
	cash = -1;
	Int count = 0;
	for( x = 0; x < xCount; x++ ) 
	{
		for( y = 0; y < yCount; y++ ) 
		{
			pos.x = bestPos.x + (x-5)*(weaponRadius/10);
			pos.y = bestPos.y + (y-5)*(weaponRadius/10);	// was (x-5): only the diagonal was scanned
			pos.z = 0;
			Int curCash = getPlayerSuperweaponValue( &pos, playerNdx, weaponRadius, targetMilitaryUnits, m_player->getPlayerIndex() );
			if ( curCash > cash) 
			{
				cash = curCash;
				veryBestPos = pos;
				count = 1;
			}	
			else if (curCash==cash) 
			{
				veryBestPos.x += pos.x;
				veryBestPos.y += pos.y;
				count++;
			}
		}
	}
	if (count>1) {
		veryBestPos.x /= count;
		veryBestPos.y /= count;
	}
	veryBestPos.z = TheTerrainLogic->getGroundHeight(veryBestPos.x, veryBestPos.y);
	*retPos = veryBestPos;

  success = ( cash > -1 );


  return success;


}

//----------------------------------------------------------------------------------------------------------
/**
 * Get the target value for structures in an area.
 */
Int AIPlayer::getPlayerSuperweaponValue(Coord3D *center, Int playerNdx, Real radius, Bool includeMilitaryUnits, Int observerNdx )
{
	if (radius < 4*PATHFIND_CELL_SIZE_F) 
	{
		radius = 4*PATHFIND_CELL_SIZE_F;
	}
	Player::PlayerTeamList::const_iterator it;
	Real cash = 0;
	Real radSqr = sqr(radius);

	Player* pPlayer = ThePlayerList->getNthPlayer(playerNdx);
	if (pPlayer == NULL) 
		return 0;
	for (it = pPlayer->getPlayerTeams()->begin(); it != pPlayer->getPlayerTeams()->end(); ++it) 
	{
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance()) 
		{
			Team *team = iter.cur();
			if (!team) continue;
			for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance()) 
			{
				Object *pObj = iter.cur();
				if (!pObj) 
					continue;
				if (!observerKnowsAbout(pObj, observerNdx))
					continue;			// a superweapon is aimed at what has been scouted, not at the object list

				Bool applyNegValue = FALSE;
				if( !includeMilitaryUnits )
				{
					if( pObj->isKindOf( KINDOF_FS_BASE_DEFENSE ) || pObj->isKindOf( KINDOF_TECH_BASE_DEFENSE ) )
					{
						//Hostile structure
						applyNegValue = TRUE;
					}
					else if( pObj->isKindOf( KINDOF_VEHICLE ) || pObj->isKindOf( KINDOF_INFANTRY ) )
					{
						if( !pObj->isKindOf( KINDOF_DOZER ) && !pObj->isKindOf( KINDOF_HARVESTER ) )
						{
							//Hostile unit.
							applyNegValue = TRUE;
						}
					}
				}
				else if (pObj->isKindOf(KINDOF_AIRCRAFT)) 
				{
					if (pObj->isSignificantlyAboveTerrain()) 
					{
						continue; // Don't target flying aircraft.  OK if in the airstrip.
					}
				}
				Coord3D pos = *pObj->getPosition();
				Real dx = center->x - pos.x;
				Real dy = center->y - pos.y;
				if (dx*dx+dy*dy<radSqr) 
				{
					Real dist = sqrt(dx*dx+dy*dy);
					Real factor = 1.0f - (dist/(2*radius)); // 1.0 in center, 0.5 on edges.
					Real value = pObj->getTemplate()->calcCostToBuild(pPlayer);
					if (pObj->isKindOf(KINDOF_COMMANDCENTER)) 
					{
						if( !includeMilitaryUnits )
							value = value * 5.0f; //Command centers are prime targets for sneak attacks.
						else
							value = value / 10; // Command centers cannot be killed by any superweapon, so we don't want to target them as highly. jba.
					}
					if (pObj->isKindOf( KINDOF_FS_SUPERWEAPON ) ) 
					{
						if( !includeMilitaryUnits )
							value = value * 5.0f; //Superweapons are prime targets for sneak attacks.
						else
							value = value / 10; // Superweapons cannot be killed by any superweapon, so we don't want to target them as highly. jba.
					}
					if( applyNegValue )
					{
						cash -= factor * value * 5.0f; //Extremely undesired 
					}
					else
					{
						cash += factor * value;
					}
				}
			}
		}
	}
	return cash;
}
// ------------------------------------------------------------------------------------------------
/** Search the computer player's buildings for one that can build the given request 
	* and start training the unit.
	* If busyOK is true, it will queue a unit even if one is building.  This lets 
	* script invoked teams "push" to the front of the queue. */
// ------------------------------------------------------------------------------------------------
Bool AIPlayer::startTraining( WorkOrder *order, Bool busyOK, AsciiString teamName)
{
	Object *factory = findFactory(order->m_thing, busyOK);
	if( factory )
	{
		ProductionUpdateInterface *pu = factory->getProductionUpdateInterface();
		if (pu && pu->queueCreateUnit( order->m_thing, pu->requestUniqueUnitID() )) {
			order->m_factoryID = factory->getID(); 
			if (TheGlobalData->m_debugAI) {
				AsciiString teamStr = "Queuing ";
				teamStr.concat(order->m_thing->getName());
				teamStr.concat(" for ");
				teamStr.concat(teamName);
				TheScriptEngine->AppendDebugMessage(teamStr, false);
			}
			return true;
		}
	}  // end if

	return FALSE;

}

// ------------------------------------------------------------------------------------------------
/** Search the computer player's buildings for one that can build the given request.
	* If busyOK is true, it will return a busy factory if there are no idle ones.  This is 
	* used for script invoked teams "push" to the front of the queue. */
// ------------------------------------------------------------------------------------------------
Object *AIPlayer::findFactory(const ThingTemplate *thing, Bool busyOK) 
{
	Object *busyFactory = NULL; // We prefer a factory that isn't busy.
	for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
	{
		Object *factory = TheGameLogic->findObjectByID( info->getObjectID() );
		if( factory )
		{
			if (factory->getControllingPlayer() != m_player) {
				// captured. Stamp the timestamp too, exactly as processBaseBuilding does - without
				// it the RebuildDelay gate below is skipped and the AI keeps trying to rebuild on
				// top of the enemy-owned building, every structure cycle.
				info->setObjectID(INVALID_ID);
				info->setObjectTimestamp(TheGameLogic->getFrame()+1);
				continue;
			}
			// ignore buildings that are under construction.
			if (factory->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
				continue;
			// also ignore buildings that are being sold.
			if (factory->testStatus(OBJECT_STATUS_SOLD))
				continue;
			ProductionUpdateInterface *pu = factory->getProductionUpdateInterface();
			// If it doesn't produce, continue.
			if (!pu) continue;
			// if we can't create the unit do nothing
			if( TheBuildAssistant->isPossibleToMakeUnit( factory, thing ) == FALSE )
				continue;
			// If the factory is not busy, return it.
			Bool busy = pu->getProductionCount()>0;
			if (!busy) return factory; // found a not busy factory.
			if (busyOK) busyFactory = factory;
		}  // end if

	}  // end for
	// We didn't find an idle factory, so return the busy one.
	if (busyOK) return busyFactory;
	return NULL;
}

// ------------------------------------------------------------------------------------------------
/** Return true if team can be considered for building */
// ------------------------------------------------------------------------------------------------
Bool AIPlayer::isPossibleToBuildTeam( TeamPrototype *proto, Bool requireIdleFactory, Bool &notEnoughMoney)
{
	/* Make sure we have at least one idle factory, and factories for all unit types. */
	Bool anyIdle = false;
	Int cost=0;
	notEnoughMoney = false;
	for( int i=0; i<proto->getTemplateInfo()->m_numUnitsInfo; i++ )
	{
		const TCreateUnitsInfo *unitInfo = &proto->getTemplateInfo()->m_unitsInfo[0];
		const ThingTemplate *thing = TheThingFactory->findTemplate( unitInfo[i].unitThingName );
		if (thing) {
			Int thingCost = thing->calcCostToBuild(m_player);
			if (NULL == findFactory(thing, true)) {
				// Couldn't find a factory.
				return false;
			}
			if (NULL != findFactory(thing, false)) {
				// Found an idle factory.
				anyIdle = true;
			}
			cost += thingCost * ((unitInfo[i].maxUnits+unitInfo[i].minUnits)/2.0f);
		}
	}
	cost *= TheAI->getAiData()->m_teamResourcesToBuild;
	if (m_player->getMoney()->countMoney() < cost)	{
		notEnoughMoney = true;
		return false; // too expensive
	}
	if (anyIdle) 
	{
		return true;
	}
	if (!requireIdleFactory) 
	{
		// Doesn't require an idle factory, so we're ok.
		return true;
	}
	return false;
}

// ------------------------------------------------------------------------------------------------
/** Check if this team is buildable, doesn't exceed maximum limits, meets conditions, 
	* and isn't under construction. */
// ------------------------------------------------------------------------------------------------
Bool AIPlayer::isAGoodIdeaToBuildTeam( TeamPrototype *proto )
{
	// Check condition.
	if (!proto->evaluateProductionCondition()) {
		return false;
	}
	// check build limit
	if (proto->countTeamInstances() >= proto->getTemplateInfo()->m_maxInstances){
		if (TheGlobalData->m_debugAI) {	
			AsciiString str;
			str.format("Team %s not chosen - %d already exist.", proto->getName().str(), proto->countTeamInstances());
			TheScriptEngine->AppendDebugMessage(str, false);
		}
		return false;	// Max already built.
	}

	for ( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance())
	{
		TeamInQueue *team = iter.cur();
		if (team->m_team->getPrototype() == proto) {
			return false; // currently building one of these.
		}
	}
	Bool needMoney;
	if (!isPossibleToBuildTeam( proto, true, needMoney)) {
		if (TheGlobalData->m_debugAI) {	
			AsciiString str;
			if (needMoney) {
				str.format("Team %s not chosen - Not enough money.", proto->getName().str());
			} else {
				str.format("Team %s not chosen - Factory/tech missing or busy.", proto->getName().str());
			}
			TheScriptEngine->AppendDebugMessage(str, false);
		}
		return false;
	}
	return true;
}

// ------------------------------------------------------------------------------------------------
/** See if any existing teams need reinforcements, and have higher priority. */
// ------------------------------------------------------------------------------------------------
Bool AIPlayer::selectTeamToReinforce( Int minPriority )
{
	// Find a high production priority team that needs reinforcements.
	Player::PlayerTeamList::const_iterator t;
	Team *curTeam = NULL;
	Int curPriority = minPriority; // Don't reinforce a team unless it is above min priority.
	const ThingTemplate *curThing = NULL;
	for (t = m_player->getPlayerTeams()->begin(); t != m_player->getPlayerTeams()->end(); ++t)
	{
		TeamPrototype *proto = (*t);
		Bool busy = false;
		for ( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance())
		{
			TeamInQueue *team = iter.cur();
			if (team->m_team->getPrototype() == proto) {
				busy = true; // currently building one of these.
			}
		}
		if (busy) continue;
		if (proto->getTemplateInfo()->m_automaticallyReinforce && proto->getTemplateInfo()->m_productionPriority>curPriority) {
			// Check the team instances.
			for (DLINK_ITERATOR<Team> iter = proto->iterate_TeamInstanceList(); !iter.done(); iter.advance())
			{
				Team *team = iter.cur();
				if (team->hasAnyUnits() == false) 
				{
					continue; // empty.
				}
				const TCreateUnitsInfo *unitInfo = &team->getPrototype()->getTemplateInfo()->m_unitsInfo[0];
				for( int i=0; i<team->getPrototype()->getTemplateInfo()->m_numUnitsInfo; i++ )
				{
					if (unitInfo[i].maxUnits < 1) continue;
					const ThingTemplate *thing = TheThingFactory->findTemplate( unitInfo[i].unitThingName );
					if (thing==NULL) continue;
					Int count=0;
					team->countObjectsByThingTemplate(1, &thing, false, &count);
					if (count < unitInfo[i].maxUnits) 
					{
						// See if there is a factory available.
						if (NULL != findFactory(thing, false)) 
						{
							curTeam = team;
							curPriority = proto->getTemplateInfo()->m_productionPriority;
							curThing = thing;
						}
					}
				}				
			}
		}
	}
	if (curTeam && curThing) 
	{
		/* We have something to build. */
		TeamInQueue *teamQ = newInstance(TeamInQueue);
		// Put in front of queue.
		prependTo_TeamBuildQueue(teamQ);
		teamQ->m_priorityBuild = false;
		teamQ->m_reinforcement = true;

		WorkOrder *order = newInstance(WorkOrder);
		order->m_thing = curThing;
		order->m_factoryID = INVALID_ID;
		order->m_numRequired = 1;
		order->m_required = true;
		// prepend to head of list
		order->m_next = NULL;
		teamQ->m_workOrders = order;
		teamQ->m_frameStarted = TheGameLogic->getFrame();
		teamQ->m_team = curTeam; 

		AsciiString teamName = curTeam->getPrototype()->getName();
		teamName.concat(" - AutoReinforcing one ");
		teamName.concat(curThing->getName());
		TheScriptEngine->AppendDebugMessage(teamName, false);

		// start the creation of a new unit
		Coord3D origin;
		origin = curTeam->getPrototype()->getTemplateInfo()->m_homeLocation;
		if (curTeam->getFirstItemIn_TeamMemberList()) 
		{
			origin = *curTeam->getFirstItemIn_TeamMemberList()->getPosition();
		}
		Object *unit = curTeam->tryToRecruit(curThing, &origin, TheAI->getAiData()->m_maxRecruitDistance);
		if (unit) 
		{
			order->m_numCompleted = 1;

			AsciiString teamStr = "Team '";
			teamStr.concat(curTeam->getPrototype()->getName());
			teamStr.concat("' recruits ");
			teamStr.concat(curThing->getName());
			teamStr.concat(" from team '");
			teamStr.concat(unit->getTeam()->getPrototype()->getName());
			teamStr.concat("'");
			TheScriptEngine->AppendDebugMessage(teamStr, false);

			unit->setTeam(curTeam);

			teamQ->m_reinforcementID = unit->getID();

			AIUpdateInterface *ai = unit->getAIUpdateInterface();
			if (ai) 
			{
				ai->aiIdle(CMD_FROM_AI);
			}
		} else {
			startTraining( order, teamQ->m_priorityBuild, teamQ->m_team->getName());
		}
		m_teamDelay = 0;
		return true;
	}
	return false;
}

// ------------------------------------------------------------------------------------------------
/** Determine the next team to build.  Return true if one was selected. */
// ------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/** What one unit template can answer.  Read off its weapons: what they are allowed to shoot at,
	* and what the data says they are the preferred answer to. */
//-------------------------------------------------------------------------------------------------
static void addTemplateCapability( const ThingTemplate *tmpl, AITeamCapability *cap )
{
	if( tmpl == NULL )
		return;

	//
	// "Can it see stealth" is not a KindOf - it is a module the data hangs on the unit, so ask the
	// template's module list by name.  That is the same question the game itself answers when it
	// builds the object, one INI edit away from being right for a mod's own detector.
	//
	const ModuleInfo &modules = tmpl->getBehaviorModuleInfo();
	for( Int m = 0; m < modules.getCount(); ++m )
	{
		if( modules.getNthName( m ).compareNoCase( "StealthDetectorUpdate" ) == 0 )
		{
			cap->m_detectsStealth = TRUE;
			break;
		}
	}

	const WeaponTemplateSetVector &sets = tmpl->getWeaponTemplateSets();
	for( WeaponTemplateSetVector::const_iterator it = sets.begin(); it != sets.end(); ++it )
	{
		for( Int slot = 0; slot < WEAPONSLOT_COUNT; ++slot )
		{
			const WeaponTemplate *w = it->getNth( (WeaponSlotType)slot );
			if( w == NULL )
				continue;

			const Int anti = w->getAntiMask();
			if( anti & (WEAPON_ANTI_AIRBORNE_VEHICLE | WEAPON_ANTI_AIRBORNE_INFANTRY) )
				cap->m_hitsAir = TRUE;
			if( anti & WEAPON_ANTI_GROUND )
				cap->m_hitsGround = TRUE;

			const KindOfMaskType &preferred = it->getNthPreferredAgainstMask( (WeaponSlotType)slot );
			if( preferred.test( KINDOF_VEHICLE ) )
				cap->m_prefersVehicles = TRUE;
			if( preferred.test( KINDOF_INFANTRY ) )
				cap->m_prefersInfantry = TRUE;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** What a team prototype can answer, over every unit it is built from. */
//-------------------------------------------------------------------------------------------------
static AITeamCapability teamCapability( const TeamPrototype *proto )
{
	AITeamCapability cap;
	const TeamTemplateInfo *info = proto ? proto->getTemplateInfo() : NULL;
	if( info == NULL )
		return cap;

	for( Int i = 0; i < info->m_numUnitsInfo; ++i )
		addTemplateCapability( TheThingFactory->findTemplate( info->m_unitsInfo[ i ].unitThingName, FALSE ), &cap );

	return cap;
}

//-------------------------------------------------------------------------------------------------
/** What this AI can see of what its enemies are fielding, weighted by threat value so a tank
	* counts for more than a rifleman.  Fog-aware by construction - it asks observerKnowsAbout of
	* every object, which is what makes B1 A2's first customer. */
//-------------------------------------------------------------------------------------------------
void AIPlayer::computeEnemyComposition( AIEnemyComposition *out )
{
	Real air = 0.0f, armour = 0.0f, infantry = 0.0f, stealth = 0.0f, total = 0.0f;
	const Int me = m_player->getPlayerIndex();

	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		if( p == NULL || p == m_player )
			continue;
		if( m_player->getRelationship( p->getDefaultTeam() ) != ENEMIES )
			continue;

		for( Player::PlayerTeamList::const_iterator t = p->getPlayerTeams()->begin();
				 t != p->getPlayerTeams()->end(); ++t )
		{
			for( DLINK_ITERATOR<Team> iter = (*t)->iterate_TeamInstanceList(); !iter.done(); iter.advance() )
			{
				Team *team = iter.cur();
				if( team == NULL )
					continue;
				for( DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance() )
				{
					Object *obj = objIter.cur();
					if( obj == NULL || obj->isEffectivelyDead() )
						continue;
					if( obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_PROJECTILE ) )
						continue;			// this is about the army, not the base
					if( !observerKnowsAbout( obj, me ) )
						continue;

					const Real threat = aiCombatPower( obj );
					if( threat <= 0.0f )
						continue;

					total += threat;
					if( obj->isKindOf( KINDOF_AIRCRAFT ) )				air += threat;
					if( obj->isKindOf( KINDOF_VEHICLE ) )					armour += threat;
					if( obj->isKindOf( KINDOF_INFANTRY ) )				infantry += threat;
					if( obj->getStatusBits().test( OBJECT_STATUS_CAN_STEALTH ) )	stealth += threat;
				}
			}
		}
	}

	out->m_totalThreat = total;

	if( total <= 0.0f )
		return;			// nothing seen: the caller's zeroed composition means "no opinion"

	out->m_air = air / total;
	out->m_armour = armour / total;
	out->m_infantry = infantry / total;
	out->m_stealth = stealth / total;
}

//-------------------------------------------------------------------------------------------------
/** What this AI can see another player is worth, priced in build cost - his army and his base.
	* Fog-aware, so an enemy nobody has scouted is worth nothing to attack rather than everything. */
//-------------------------------------------------------------------------------------------------
Real AIPlayer::visibleEstateValue( Int playerNdx )
{
	Player *p = ThePlayerList->getNthPlayer( playerNdx );
	if( p == NULL )
		return 0.0f;

	const Int me = m_player->getPlayerIndex();
	Real value = 0.0f;

	for( Player::PlayerTeamList::const_iterator t = p->getPlayerTeams()->begin();
			 t != p->getPlayerTeams()->end(); ++t )
	{
		for( DLINK_ITERATOR<Team> iter = (*t)->iterate_TeamInstanceList(); !iter.done(); iter.advance() )
		{
			Team *team = iter.cur();
			if( team == NULL )
				continue;
			for( DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance() )
			{
				Object *obj = objIter.cur();
				if( obj == NULL || obj->isEffectivelyDead() || obj->isKindOf( KINDOF_PROJECTILE ) )
					continue;
				if( !observerKnowsAbout( obj, me ) )
					continue;
				value += INT_TO_REAL( obj->getTemplate()->calcCostToBuild( p ) );
			}
		}
	}
	return value;
}

Bool AIPlayer::selectTeamToBuild( void )
{

	// find the highest priority of all teams
	Player::PlayerTeamList::const_iterator t;
	const Int invalidPri = -99999;
	Int hiPri = invalidPri;
	// collect all teams that are possible to build, and are at the highest priority
	Player::PlayerTeamList candidateList1;
	for (t = m_player->getPlayerTeams()->begin(); t != m_player->getPlayerTeams()->end(); ++t)
	{
		if (isAGoodIdeaToBuildTeam(*t))
		{
			candidateList1.push_back( (*t) );
			Int pri = (*t)->getTemplateInfo()->m_productionPriority;

			if (pri > hiPri)
			{
				hiPri = pri;
			}
		}
	}

	if (selectTeamToReinforce(hiPri)) {
		return true;
	}

	// check if no team prototypes are valid for production
	if (hiPri == invalidPri)
		return false;

	if (TheGlobalData->m_debugAI) {
		TheScriptEngine->AppendDebugMessage("**AI** Selecting team to build", false);
	}

	//
	// EA's version stopped here: take the highest static productionPriority and flip a coin among
	// the ties.  Not one line of it looked at what the enemy was fielding, which is why an AI facing
	// nothing but aircraft would go on building tanks.
	//
	// So the static priority is the base of a score rather than the whole of it, and two situational
	// terms ride on top:
	//
	//   - how well the team answers what this AI can *see* the enemy fielding (B1), scaled by the
	//     rung's counterCompositionWeight - zero on the bottom two rungs, which is EA's behaviour
	//     exactly, up to one at the top;
	//   - what this AI is trying to do (D8): an aggressive one leans towards attack teams, a
	//     defensive one towards the teams the data flags as base or perimeter defence.  A
	//     preference, not a bonus - both spend the same money.
	//
	// The coin flip stays, for the ties that remain. Determinism: GameLogicRandomValue, as before.
	//
	const AIDifficultyProfile *profile = getSkillProfile();
	AIEnemyComposition enemy;
	if (profile->m_counterCompositionWeight > 0.0f)
		computeEnemyComposition( &enemy );

	// what a perfect counter, and the role preference, are worth in units of production priority
	const Real COUNTER_SPAN = 20.0f;
	const Real ROLE_SPAN = 10.0f;

	Player::PlayerTeamList candidateList;
	Int count = 0;
	Real bestScore = 0.0f;
	Bool haveBest = FALSE;
	for (t = candidateList1.begin(); t != candidateList1.end(); ++t)
	{
		const TeamTemplateInfo *info = (*t)->getTemplateInfo();
		const Bool isDefenceTeam = info->m_isBaseDefense || info->m_isPerimeterDefense;

		Real score = INT_TO_REAL( info->m_productionPriority );
		score += profile->m_counterCompositionWeight * COUNTER_SPAN *
						 aiCounterScore( enemy, teamCapability( *t ) );
		if( (m_role == AIROLE_DEFENSIVE) == (isDefenceTeam != FALSE) )
			score += ROLE_SPAN;

		if( !haveBest || score > bestScore )
		{
			bestScore = score;
			haveBest = TRUE;
			candidateList.clear();
			count = 0;
		}
		if( score >= bestScore )
		{
			candidateList.push_back( (*t) );
			count++;
		}
	}

	// pick a random team from the hi-priority set
	Int which = GameLogicRandomValue( 0, count-1 );

	TeamPrototype *teamProto = NULL;
	Int i = 0;
	for (t = candidateList.begin(); t != candidateList.end(); ++t)
	{
		if (i == which)
		{
			teamProto = (*t);
			break;
		}

		i++;
	}
	if (teamProto) {
		if (!teamProto->getTemplateInfo()->m_hasHomeLocation && !isSkirmishAI()) {
			AsciiString teamStr = "Error : team '";
			teamStr.concat(teamProto->getName());
			teamStr.concat("' has no Home Position (or Origin).");
			TheScriptEngine->AppendDebugMessage(teamStr, false);
		}
		// Build it at low priority, as we have selected it automagically. 
		buildSpecificAITeam(teamProto, false);
		m_readyToBuildTeam = false;
		m_teamTimer = computeTeamDelay();
		return true;
	}
	return false;
}

// ------------------------------------------------------------------------------------------------
/** Build a specific team.  If priorityBuild, put at front of queue with priority set. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::buildSpecificAIBuilding(const AsciiString &thingName)
{
	//
	AsciiString teamStr = "Error : Solo ai doesn't support BuildSpecificBuilding. '";
	teamStr.concat(thingName);
	teamStr.concat("' not built.");
	TheScriptEngine->AppendDebugMessage(teamStr, false);
}

// ------------------------------------------------------------------------------------------------
/** Build an upgrade. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::buildUpgrade(const AsciiString &upgrade)
{
	const UpgradeTemplate *curUpgrade = TheUpgradeCenter->findUpgrade(upgrade);
	if (curUpgrade==NULL) {
		AsciiString msg = "Upgrade ";
		msg.concat(upgrade);
		msg.concat(" does not exist.  Ignoring request.");
		TheScriptEngine->AppendDebugMessage( msg, false);
		return;
	}
 	if (curUpgrade->getUpgradeType()==UPGRADE_TYPE_OBJECT) {
		AsciiString msg = "Player build upgrade: Upgrade ";
		msg.concat(upgrade);
		msg.concat(" is an object, not a player upgrade.  Ignoring request.");
		TheScriptEngine->AppendDebugMessage( msg, false);
		return;
	}
	// See if it is in progress.
	if (m_player->hasUpgradeInProduction(curUpgrade)) {
		AsciiString msg = TheNameKeyGenerator->keyToName(m_player->getPlayerNameKey());
		msg.concat(" already has upgrade ");
		msg.concat(upgrade);
		msg.concat(" queued.  Ignoring request.");
		TheScriptEngine->AppendDebugMessage( msg, false);
		return;
	}
	// See if it is in progress.
	if (m_player->hasUpgradeComplete(curUpgrade)) {
		AsciiString msg = TheNameKeyGenerator->keyToName(m_player->getPlayerNameKey());
		msg.concat(" already has upgrade ");
		msg.concat(upgrade);
		msg.concat(" completed.  Ignoring request.");
		TheScriptEngine->AppendDebugMessage( msg, false);
		return;
	}


	// No money.
	if( TheUpgradeCenter->canAffordUpgrade( m_player, curUpgrade ) == FALSE ) {
		AsciiString msg = TheNameKeyGenerator->keyToName(m_player->getPlayerNameKey());
		msg.concat(" lacks money to build upgrade ");
		msg.concat(upgrade);
		msg.concat(" at this time.  Ignoring request.");
		TheScriptEngine->AppendDebugMessage( msg, false);
		return;
	}
	// Find a production queue.
	for( const BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
	{
		Object *factory = TheGameLogic->findObjectByID( info->getObjectID() );
		if( factory )
		{
			if( factory->getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
				continue;
			if( factory->getStatusBits().test( OBJECT_STATUS_SOLD ) )
				continue;
			Bool canUpgradeHere = false;
			const CommandSet *commandSet = TheControlBar->findCommandSet( factory->getCommandSetString() );
			if( commandSet == NULL) continue;
			for( Int j = 0; j < MAX_COMMANDS_PER_SET; j++ )
			{
				//Get the command button.
				const CommandButton *commandButton = commandSet->getCommandButton(j);
				if (commandButton==NULL) continue;
				if (commandButton->getName().isEmpty() )	continue;	
				if (commandButton->getUpgradeTemplate() == NULL )	continue;	
 				if (commandButton->getUpgradeTemplate()->getUpgradeName() == curUpgrade->getUpgradeName()) {
					canUpgradeHere = true;
				}
			}
			if (!canUpgradeHere) continue;
			ProductionUpdateInterface *pu = factory->getProductionUpdateInterface();
			// If it doesn't produce, continue.
			if (!pu) continue;
			// Try to queue it.
			if (pu->queueUpgrade(curUpgrade)) {
				AsciiString msg = TheNameKeyGenerator->keyToName(m_player->getPlayerNameKey());
				msg.concat(" queues ");
				msg.concat(curUpgrade->getUpgradeName());
				msg.concat(" at ");
				msg.concat(factory->getTemplate()->getName());
				TheScriptEngine->AppendDebugMessage( msg, false);
				return;
			}
		}  // end if
	}  // end for

	AsciiString msg = TheNameKeyGenerator->keyToName(m_player->getPlayerNameKey());
	msg.concat(" lacks factory to build upgrade ");
	msg.concat(upgrade);
	msg.concat(" at this time.  Ignoring request.");
	TheScriptEngine->AppendDebugMessage( msg, false);
	return;
}

// ------------------------------------------------------------------------------------------------
/** Build a supply center near a supply source with minimumCash or more resources. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::buildBySupplies(Int minimumCash, const AsciiString& thingName)
{
	Object *bestSupplyWarehouse = findSupplyCenter(minimumCash);
	const ThingTemplate* tTemplate = TheThingFactory->findTemplate(thingName);
	// tTemplate was dereferenced here and only null-checked a few lines further down, so a bad
	// building name in the "Build supply center" script action faulted.
	if (tTemplate && !tTemplate->isKindOf(KINDOF_CASH_GENERATOR)) {
		// Build by the current warehouse.
		Object *curWarehouse = TheGameLogic->findObjectByID(m_curWarehouseID);
		if (curWarehouse) {
			bestSupplyWarehouse = curWarehouse;
		}
	}


	if (bestSupplyWarehouse && tTemplate) {
		Coord3D location;
		location = *bestSupplyWarehouse->getPosition();
		// offset back towards the base.
		Coord2D offset;
		offset.x = location.x - m_baseCenter.x;
		offset.y = location.y - m_baseCenter.y;
		offset.normalize();
		Real radius = 3*PATHFIND_CELL_SIZE_F;
		if (!tTemplate->isKindOf(KINDOF_CASH_GENERATOR)) {
			// It's probably a defensive structure - build towards the enemy.
			Region2D bounds;
			Player *skirmishEnemy = TheScriptEngine->getSkirmishEnemyPlayer();	// can be NULL
			if (skirmishEnemy)
			{
				getPlayerStructureBounds(&bounds, skirmishEnemy->getPlayerIndex(), FALSE, m_player->getPlayerIndex());
				offset.x = location.x - (bounds.lo.x+bounds.hi.x)*0.5f;
				offset.y = location.y - (bounds.lo.y+bounds.hi.y)*0.5f;
				offset.normalize();
			}
			radius = bestSupplyWarehouse->getGeometryInfo().getBoundingCircleRadius();
		}
		location.x -= offset.x*radius;
		location.y -= offset.y*radius;
		Real angle = tTemplate->getPlacementViewAngle();

 		// validate the the position to build at is valid
		Bool valid=false;
		Coord3D newPos = location;
		if( TheBuildAssistant->isLocationLegalToBuild( &location, tTemplate, angle,
																									 BuildAssistant::NO_OBJECT_OVERLAP,
																									 NULL, m_player ) != LBC_OK ) {
			// Warn. 
			const Coord3D *warehouseLocation = bestSupplyWarehouse->getPosition();
			AsciiString debugMessage;
			debugMessage.format(" %s - buildBySupplies unable to place near dock at (%.2f,%.2f).  Attempting to adjust position.",
													tTemplate->getName().str(),
													warehouseLocation->x,
													warehouseLocation->y
													);
			TheScriptEngine->AppendDebugMessage(debugMessage, false);
			if( TheGlobalData->m_debugSupplyCenterPlacement )
				DEBUG_LOG(("%s", debugMessage.str()));
			// try to fix.
			Real posOffset;
			// Wiggle it a little :)
			for (posOffset = 0; posOffset<2*SUPPLY_CENTER_CLOSE_DIST; posOffset += 2*PATHFIND_CELL_SIZE_F) {
				Real offset = posOffset/2;
				Real xPos, yPos;
				yPos = location.y-offset;
				for (xPos = location.x-offset; xPos <= location.x+offset; xPos+=PATHFIND_CELL_SIZE_F) {
					newPos.x = xPos;
					newPos.y = yPos;
					valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, tTemplate, angle,
																							 BuildAssistant::CLEAR_PATH |
																							 BuildAssistant::TERRAIN_RESTRICTIONS |
																							 BuildAssistant::NO_OBJECT_OVERLAP,
																							 NULL, m_player ) == LBC_OK;
					if (valid) break;
					if( TheGlobalData->m_debugSupplyCenterPlacement )
						DEBUG_LOG(("buildBySupplies -- Fail at (%.2f,%.2f)\n", newPos.x, newPos.y));
					newPos.y = yPos+posOffset;
					valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, tTemplate, angle,
																							 BuildAssistant::CLEAR_PATH |
																							 BuildAssistant::TERRAIN_RESTRICTIONS |
																							 BuildAssistant::NO_OBJECT_OVERLAP,
																							 NULL, m_player ) == LBC_OK;
					if (valid) break;
					if( TheGlobalData->m_debugSupplyCenterPlacement )
						DEBUG_LOG(("buildBySupplies -- Fail at (%.2f,%.2f)\n", newPos.x, newPos.y));
				}
				if (valid) break;
				xPos = location.x-offset;
				for (yPos = location.y-offset; yPos <= location.y+offset; yPos+=PATHFIND_CELL_SIZE_F) {
					newPos.x = xPos;
					newPos.y = yPos;
					valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, tTemplate, angle,
																							 BuildAssistant::CLEAR_PATH |
																							 BuildAssistant::TERRAIN_RESTRICTIONS |
																							 BuildAssistant::NO_OBJECT_OVERLAP,
																							 NULL, m_player ) == LBC_OK;
					if (valid) break;
					if( TheGlobalData->m_debugSupplyCenterPlacement )
						DEBUG_LOG(("buildBySupplies -- Fail at (%.2f,%.2f)\n", newPos.x, newPos.y));
					newPos.x = xPos+posOffset;
					valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, tTemplate, angle,
																							 BuildAssistant::CLEAR_PATH |
																							 BuildAssistant::TERRAIN_RESTRICTIONS |
																							 BuildAssistant::NO_OBJECT_OVERLAP,
																							 NULL, m_player ) == LBC_OK;
					if (valid) break;
					if( TheGlobalData->m_debugSupplyCenterPlacement )
						DEBUG_LOG(("buildBySupplies -- Fail at (%.2f,%.2f)\n", newPos.x, newPos.y));
				}
				if (valid) break;
			}
		}
		if (valid) 
		{
			if( TheGlobalData->m_debugSupplyCenterPlacement )
				DEBUG_LOG(("buildAISupplyCenter -- SUCCESS at (%.2f,%.2f)\n", newPos.x, newPos.y));
			location = newPos;
		}
		TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback, turn it off.  jba.
		location.z = 0; // All build list locations are ground relative.
		m_player->addToPriorityBuildList(thingName, &location, angle);
		m_curWarehouseID = bestSupplyWarehouse->getID();
	}
}

// ------------------------------------------------------------------------------------------------
/** Calculates the closest construction zone location based on a template. */
// ------------------------------------------------------------------------------------------------
Bool AIPlayer::calcClosestConstructionZoneLocation( const ThingTemplate *constructTemplate, Coord3D *location )
{
	if( !constructTemplate || !location )
	{
		return FALSE;
	}

  Bool success = FALSE;

	// offset back towards the base.
	Coord2D offset;
	offset.x = location->x - m_baseCenter.x;
	offset.y = location->y - m_baseCenter.y;
	offset.normalize();

	Real angle = constructTemplate->getPlacementViewAngle();

 	// validate the the position to build at is valid
	Bool valid=false;
	Coord3D newPos = *location;
	if( TheBuildAssistant->isLocationLegalToBuild( location, constructTemplate, angle, BuildAssistant::NO_OBJECT_OVERLAP, NULL, m_player ) != LBC_OK )
	{
		// Warn. 
		AsciiString bldgName = constructTemplate->getName();
		bldgName.concat(" - calcClosestConstructionZoneLocation unable to place.  Attempting to adjust position.");
		TheScriptEngine->AppendDebugMessage( bldgName, false );
		// try to fix.
		Real posOffset;
		// Wiggle it a little :)
		for( posOffset = 0; posOffset < 2 * SUPPLY_CENTER_CLOSE_DIST; posOffset += 2 * PATHFIND_CELL_SIZE_F ) 
		{
			Real offset = posOffset / 2;
			Real xPos, yPos;
			yPos = location->y - offset;
			for( xPos = location->x - offset; xPos <= location->x + offset; xPos += PATHFIND_CELL_SIZE_F ) 
			{
				newPos.x = xPos;
				newPos.y = yPos;
				valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, constructTemplate, angle,
																						 BuildAssistant::CLEAR_PATH |
																						 BuildAssistant::TERRAIN_RESTRICTIONS |
																						 BuildAssistant::NO_OBJECT_OVERLAP,
																						 NULL, m_player ) == LBC_OK;
				if( valid ) 
					break;
				
				newPos.y = yPos + posOffset;
				valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, constructTemplate, angle,
																						 BuildAssistant::CLEAR_PATH |
																						 BuildAssistant::TERRAIN_RESTRICTIONS |
																						 BuildAssistant::NO_OBJECT_OVERLAP,
																						 NULL, m_player ) == LBC_OK;
			}
			
			if( valid ) 
				break;

			xPos = location->x - offset;
			for( yPos = location->y - offset; yPos <= location->y + offset; yPos += PATHFIND_CELL_SIZE_F ) 
			{
				newPos.x = xPos;
				newPos.y = yPos;
				valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, constructTemplate, angle,
																						 BuildAssistant::CLEAR_PATH |
																						 BuildAssistant::TERRAIN_RESTRICTIONS |
																						 BuildAssistant::NO_OBJECT_OVERLAP,
																						 NULL, m_player ) == LBC_OK;
				if( valid ) 
					break;

				newPos.x = xPos + posOffset;
				valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, constructTemplate, angle,
																						 BuildAssistant::CLEAR_PATH |
																						 BuildAssistant::TERRAIN_RESTRICTIONS |
																						 BuildAssistant::NO_OBJECT_OVERLAP,
																						 NULL, m_player ) == LBC_OK;
			}

			if( valid ) 
				break;
		}
	}
	if( valid )
	{
		//We succeeded in calculating the best position.
		location->set( &newPos );
    success = TRUE;
	}
	else
	{
		//We failed to calculate a position, so zero out the position.
		location->zero();
    success = FALSE;
	}

	TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback, turn it off.  jba.

  return success;

}

// ------------------------------------------------------------------------------------------------
/** Build a specific building nearest specified team. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::buildSpecificBuildingNearestTeam( const AsciiString &thingName, const Team *team )
{
	const ThingTemplate *tTemplate = TheThingFactory->findTemplate( thingName );

	if( !tTemplate || !team )
	{
		//Assert will already happen in the failed findTemplate call.
		return;
	}

	//From the team's location, find the most valid build location.
	const Coord3D *location = team->getEstimateTeamPosition();
	if( !location )
	{
		return;
	}

	// offset back towards the base.
	Coord2D offset;
	offset.x = location->x - m_baseCenter.x;
	offset.y = location->y - m_baseCenter.y;
	offset.normalize();

	Real angle = tTemplate->getPlacementViewAngle();

 	// validate the the position to build at is valid
	Bool valid=false;
	Coord3D newPos = *location;
	if( TheBuildAssistant->isLocationLegalToBuild( location, tTemplate, angle, BuildAssistant::NO_OBJECT_OVERLAP, NULL, m_player ) != LBC_OK )
	{
		// Warn. 
		AsciiString bldgName = tTemplate->getName();
		bldgName.concat(" - buildSpecificBuildingNearestTeam unable to place.  Attempting to adjust position.");
		TheScriptEngine->AppendDebugMessage( bldgName, false );
		// try to fix.
		Real posOffset;
		// Wiggle it a little :)
		for( posOffset = 0; posOffset < 2 * SUPPLY_CENTER_CLOSE_DIST; posOffset += 2 * PATHFIND_CELL_SIZE_F ) 
		{
			Real offset = posOffset / 2;
			Real xPos, yPos;
			yPos = location->y-offset;
			for( xPos = location->x - offset; xPos <= location->x + offset; xPos += PATHFIND_CELL_SIZE_F ) 
			{
				newPos.x = xPos;
				newPos.y = yPos;
				valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, tTemplate, angle,
																						 BuildAssistant::CLEAR_PATH |
																						 BuildAssistant::TERRAIN_RESTRICTIONS |
																						 BuildAssistant::NO_OBJECT_OVERLAP,
																						 NULL, m_player ) == LBC_OK;
				if( valid ) 
					break;
				
				newPos.y = yPos + posOffset;
				valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, tTemplate, angle,
																						 BuildAssistant::CLEAR_PATH |
																						 BuildAssistant::TERRAIN_RESTRICTIONS |
																						 BuildAssistant::NO_OBJECT_OVERLAP,
																						 NULL, m_player ) == LBC_OK;
			}
			
			if( valid ) 
				break;

			xPos = location->x - offset;
			for( yPos = location->y - offset; yPos <= location->y + offset; yPos += PATHFIND_CELL_SIZE_F ) 
			{
				newPos.x = xPos;
				newPos.y = yPos;
				valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, tTemplate, angle,
																						 BuildAssistant::CLEAR_PATH |
																						 BuildAssistant::TERRAIN_RESTRICTIONS |
																						 BuildAssistant::NO_OBJECT_OVERLAP,
																						 NULL, m_player ) == LBC_OK;
				if( valid ) 
					break;

				newPos.x = xPos + posOffset;
				valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, tTemplate, angle,
																						 BuildAssistant::CLEAR_PATH |
																						 BuildAssistant::TERRAIN_RESTRICTIONS |
																						 BuildAssistant::NO_OBJECT_OVERLAP,
																						 NULL, m_player ) == LBC_OK;
			}

			if( valid ) 
				break;
		}
	}
	if( valid ) 
	{
		newPos.z = 0; // All build list locations are ground relative.
		m_player->addToPriorityBuildList( thingName, &newPos, angle );
	}

	TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback, turn it off.  jba.

}

// ------------------------------------------------------------------------------------------------
/** Find a supply center we haven't built a supply depot near yet. */
// ------------------------------------------------------------------------------------------------
Object *AIPlayer::findSupplyCenter(Int minimumCash)
{
	Object *bestSupplyWarehouse = NULL;
	Real bestDistSqr = 0;
	Object *obj;
	Coord3D enemyCenter;
	enemyCenter.zero();
	Region2D bounds;
	Player *enemy = getAiEnemy();
	if (enemy) {
		getPlayerStructureBounds(&bounds, enemy->getPlayerIndex(), FALSE, m_player->getPlayerIndex());
		enemyCenter.set( (bounds.lo.x+bounds.hi.x)*0.5f, (bounds.lo.y+bounds.hi.y)*0.5f, 0);
	}

	do {
		for( obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			if (!obj->isKindOf(KINDOF_STRUCTURE)) continue;
			if (!obj->isKindOf(KINDOF_SUPPLY_SOURCE)) continue;
			// ... and one this AI has laid eyes on. This loop walks every object on the map, so
			// without the test the AI expands to docks in shroud it has never been near.
			if (!observerKnowsAbout(obj, m_player->getPlayerIndex())) continue;
			static const NameKeyType key_warehouseUpdate = NAMEKEY("SupplyWarehouseDockUpdate");
			SupplyWarehouseDockUpdate *warehouseModule = (SupplyWarehouseDockUpdate*)obj->findUpdateModule( key_warehouseUpdate );
			if( warehouseModule )	{	 
				Int availableCash = warehouseModule->getBoxesStored()*TheGlobalData->m_baseValuePerSupplyBox;
				if (availableCash<minimumCash) continue;
				if( m_player->getRelationship(obj->getTeam()) == ENEMIES ) {
					continue;
				}

				// Make sure we don't have a supply center near it.
				Coord3D center = *obj->getPosition();
				Real radius = SUPPLY_CENTER_CLOSE_DIST + obj->getGeometryInfo().getBoundingCircleRadius();

				PartitionFilterAcceptByKindOf f1(MAKE_KINDOF_MASK(KINDOF_CASH_GENERATOR), KINDOFMASK_NONE);
				//
				// "Do I already have a centre here" - and, for a supportive AI, "does my ally".  Two
				// allied AIs racing each other to the same warehouse is one of the most visibly
				// stupid things AI teammates do, and this filter is the whole of the fix.
				//
				PartitionFilterPlayer f2(m_player, true);	// Only find your own units.
				PartitionFilterPlayerAffiliation f2Ally(m_player, ALLOW_SAME_PLAYER | ALLOW_ALLIES, true);
				PartitionFilterOnMap filterMapStatus;

				PartitionFilter *mine[] = { &f1, &f2, &filterMapStatus, 0 };
				PartitionFilter *ours[] = { &f1, &f2Ally, &filterMapStatus, 0 };
				PartitionFilter **filters = (m_role == AIROLE_SUPPORTIVE) ? ours : mine;

				Object *supplyCenter = ThePartitionManager->getClosestObject(&center, radius, FROM_BOUNDINGSPHERE_2D, filters);
				if (supplyCenter) {
					// We already have a supply center.
					continue;
				}

				Real dx, dy;
				dx = obj->getPosition()->x - m_baseCenter.x;
				dy = obj->getPosition()->y - m_baseCenter.y;
				Real distSqr = dx*dx + dy*dy;
				if (enemy) {
					// make sure this isn't closer to our enemy than us.
					dx = obj->getPosition()->x - enemyCenter.x;
					dy = obj->getPosition()->y - enemyCenter.y;
					if (distSqr*0.4>(dx*dx+dy*dy)*0.6f) {
						// closer than 60/40 to enemy than to us, probably not a good candidate for expansion.
						continue;
					}
				}

				if (bestSupplyWarehouse==NULL) {
					bestSupplyWarehouse = obj;
					bestDistSqr = distSqr;
				} else if (bestDistSqr>distSqr) {
					bestSupplyWarehouse = obj;
					bestDistSqr = distSqr;
				}
			}
		}
		if (bestSupplyWarehouse) break;
		minimumCash /= 2;
 	} while (minimumCash > 100);

	return bestSupplyWarehouse;
}

// ------------------------------------------------------------------------------------------------
/**  Build a base defense. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::buildAIBaseDefense(Bool flank)
{
	//
	AsciiString teamStr = "Error : Solo ai doesn't support buildAIBaseDefense. '";
	TheScriptEngine->AppendDebugMessage(teamStr, false);
}

// ------------------------------------------------------------------------------------------------
/** Build a base defense. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::buildAIBaseDefenseStructure(const AsciiString &thingName, Bool flank)
{
	//
	AsciiString teamStr = "Error : Solo ai doesn't support buildAIBaseDefenseStructure. '";
	TheScriptEngine->AppendDebugMessage(teamStr, false);
}

// ------------------------------------------------------------------------------------------------
/** Repair a bridge or other structure. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::repairStructure(ObjectID structure)
{
	Object *structureObj = TheGameLogic->findObjectByID(structure);
	if (structureObj==NULL) return;
	if (structureObj->getBodyModule()==NULL) return;
	// If the structure is not noticably damaged, don't bother.
	enum BodyDamageType structureState = structureObj->getBodyModule()->getDamageState(); 
	if (structureState==BODY_PRISTINE) {
		return; 
	}
	if (structureObj->isKindOf(KINDOF_BRIDGE)) {
		// Locate the correct post to repair.
	}
	Int i;
	for (i=0; i<m_structuresInQueue; i++) {
		if (m_structuresToRepair[i] == structureObj->getID()) {
			DEBUG_LOG(("info - Bridge already queued for repair.\n"));
			return;
		}
	}
	if (m_structuresInQueue==MAX_STRUCTURES_TO_REPAIR) {
		DEBUG_LOG(("Structure repair queue is full, ignoring repair request. JBA\n"));
		return;
	}
	m_structuresToRepair[m_structuresInQueue] = structureObj->getID();
	m_structuresInQueue++;
}

// ------------------------------------------------------------------------------------------------
/** select a skillset for the player. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::selectSkillset(Int skillset)
{
	DEBUG_ASSERTCRASH(m_skillsetSelector == INVALID_SKILLSET_SELECTION, 
		("Selecting a skill set (%d) after one has already been chosen (%d) means some points have been incorrectly spent.\n", skillset + 1, m_skillsetSelector + 1));

	m_skillsetSelector = skillset;
}

// ------------------------------------------------------------------------------------------------
/** Do per frame work (if any) repairing bridges. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::updateBridgeRepair(void)
{
	if (m_structuresInQueue == 0) return;
	// Check once a second.
	m_bridgeTimer--;
	if (m_bridgeTimer>0) return;
	m_bridgeTimer = LOGICFRAMES_PER_SECOND;
	Object *bridgeObj=NULL;
	while (bridgeObj==NULL && m_structuresInQueue>0) {
		bridgeObj = TheGameLogic->findObjectByID(m_structuresToRepair[0]);
		if (bridgeObj==NULL) {
			Int i;
			for (i=0; i<m_structuresInQueue-1; i++) {
				m_structuresToRepair[i] = m_structuresToRepair[i+1];
			}
			m_structuresInQueue--;
		}
	}
	if (m_structuresInQueue == 0) return;

	// Got a bridge to repair.
	Object *dozer = NULL;
	Coord3D bridgePos = *bridgeObj->getPosition();
	enum BodyDamageType bridgeState = bridgeObj->getBodyModule()->getDamageState(); 
	if (m_repairDozer==INVALID_ID) {
		m_dozerIsRepairing = false;
		// Need a dozer.
		if (m_dozerQueuedForRepair) {
			return; // we're waiting for one.
		}
		dozer = findDozer(&bridgePos);
		if (dozer) {
			m_repairDozer = dozer->getID();
			m_repairDozerOrigin = *dozer->getPosition();
			dozer->getAI()->aiRepair(bridgeObj, CMD_FROM_AI);
			DEBUG_LOG(("Telling dozer to repair\n"));
			m_dozerIsRepairing = true;
			return;
		}
		queueDozer();
		m_dozerQueuedForRepair = true;
		return;
	}

	dozer = TheGameLogic->findObjectByID(m_repairDozer);
	if (dozer==NULL) {
		m_repairDozer=INVALID_ID; // we got killed.
		m_bridgeTimer=0;
		return; // Just try to find a dozer next frame.
	}

	DozerAIInterface* dozerAI = dozer->getAI()->getDozerAIInterface();
	if (dozerAI==NULL) {
		DEBUG_CRASH(("Unexpected - dozer doesn't have dozer interface."));
		return;
	}
	if (m_dozerIsRepairing) {
		if (!dozerAI->isAnyTaskPending())	{
			// should be done repairing.
			if (bridgeState==BODY_PRISTINE) {
				DEBUG_LOG(("Dozer finished repairing structure.\n"));
				// we're done.
				Int i;
				for (i=0; i<m_structuresInQueue-1; i++) {
					m_structuresToRepair[i] = m_structuresToRepair[i+1];
				}
				m_structuresInQueue--;
				m_dozerIsRepairing = false;
				if (m_structuresInQueue==0) {
					// Go home.  
					Coord3D pos = m_baseCenter;
					if (!m_baseCenterSet) {
						pos = m_repairDozerOrigin;
					}
					AIUpdateInterface *ai=dozer->getAI();
					TheAI->pathfinder()->adjustToPossibleDestination(dozer, ai->getLocomotorSet(), &pos);
					dozer->getAI()->aiMoveToPosition(&pos, CMD_FROM_AI);
					return;
				}
			}
		}	else {
			// dozer should be working on the bridge.
			return;
		}
	}	
	dozer->getAI()->aiRepair(bridgeObj, CMD_FROM_AI);
	m_dozerIsRepairing = true;
	DEBUG_LOG(("Telling dozer to repair\n"));
}

// ------------------------------------------------------------------------------------------------
/** Build a specific team.  If priorityBuild, put at front of queue with priority set. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::buildSpecificAITeam( TeamPrototype *teamProto, Bool priorityBuild)
{
	//
	// Create "Team in queue" based on team population
	//
	if (teamProto)
	{	
		if (!m_player->getCanBuildUnits()) {
			AsciiString teamStr = "Can't build team '";
			teamStr.concat(teamProto->getName());
			teamStr.concat("' because build units is disabled.");
			TheScriptEngine->AppendDebugMessage(teamStr, false);
			return;
		}
		if (priorityBuild && teamProto->getIsSingleton()) {
			Team *singletonTeam = TheTeamFactory->findTeam( teamProto->getName() );
			if (singletonTeam && singletonTeam->hasAnyObjects()) {
				AsciiString teamStr = "Unable to build singleton team '";
				teamStr.concat("' because team already exists.");
				TheScriptEngine->AppendDebugMessage(teamStr, false); 
				return;
			}
		}
		// Check & make sure we have factories.
		Bool needMoney;
		if (!isPossibleToBuildTeam(teamProto, false, needMoney)) {
			if (needMoney) {
				// Queue it up anyway.
				AsciiString teamStr = "Note - queueing team '";
				teamStr.concat(teamProto->getName());
				teamStr.concat("' but there is enough money.");
				TheScriptEngine->AppendDebugMessage(teamStr, false);
			} else {	
				// Tech tree doesn't work.
				AsciiString teamStr = "Unable to build team '";
				teamStr.concat(teamProto->getName());
				if (needMoney) {
					teamStr.concat("' - Not enough money.");
				} else {
					teamStr.concat("' because required factories/tech don't exist.");
				}
				TheScriptEngine->AppendDebugMessage(teamStr, false);
				return;
			}
		}
		const TCreateUnitsInfo *unitInfo = &teamProto->getTemplateInfo()->m_unitsInfo[0];
		WorkOrder *orders = NULL;
		Int i;
		// Queue up optional units.
		for( i=0; i<teamProto->getTemplateInfo()->m_numUnitsInfo; i++ )
		{
			const ThingTemplate *thing = TheThingFactory->findTemplate( unitInfo[i].unitThingName );
			if (thing)
			{
				int count = unitInfo[i].maxUnits-unitInfo[i].minUnits;
				if (count>0) {
					WorkOrder *order = newInstance(WorkOrder);
					order->m_thing = thing;
					order->m_factoryID = INVALID_ID;
					order->m_numRequired = count;
					// prepend to head of list
					order->m_next = orders;
					orders = order;
				}
			}
		}
		// Queue up required units.
		for( i=0; i<teamProto->getTemplateInfo()->m_numUnitsInfo; i++ )
		{
			const ThingTemplate *thing = TheThingFactory->findTemplate( unitInfo[i].unitThingName );
			if (thing)
			{
				int count = unitInfo[i].minUnits;
				WorkOrder *order = newInstance(WorkOrder);
				order->m_thing = thing;
				order->m_factoryID = INVALID_ID;
				order->m_numRequired = count;
				order->m_required = true;
				// prepend to head of list
				order->m_next = orders;
				orders = order;
			}
		}
		if (orders) 
		{
			/* We have something to build. */
			TeamInQueue *team = newInstance(TeamInQueue);
			if (priorityBuild) {
				// Put in front of queue.
				prependTo_TeamBuildQueue(team);
				team->m_priorityBuild = true;
			}	else {
				// Put in back of queue.
				reverse_TeamBuildQueue();
				prependTo_TeamBuildQueue(team);
				reverse_TeamBuildQueue();
				team->m_priorityBuild = false;
			}
			team->m_workOrders = orders;
			team->m_frameStarted = TheGameLogic->getFrame();
			// create inactive team to place members into as they are built
			// when team is complete, the team is activated
			team->m_team = TheTeamFactory->createInactiveTeam( teamProto->getName() ); 
			AsciiString teamName = teamProto->getName();
			teamName.concat(" - starting team build.");
			TheScriptEngine->AppendDebugMessage(teamName, false);
			m_teamDelay = 0;
			if (team->m_team->getPrototype()->getTemplateInfo()->m_executeActions) {
				const Script *script = TheScriptEngine->findScriptByName(team->m_team->getPrototype()->getTemplateInfo()->m_productionCondition);
				if (script && script->getAction()) {
					TheScriptEngine->friend_executeAction(script->getAction(), team->m_team);
				}
			}
		} else {
			if (TheGlobalData->m_debugAI) {
				AsciiString teamName = teamProto->getName();
				teamName.concat(" - contains 0 buildable units.");
				TheScriptEngine->AppendDebugMessage(teamName, false);
			}
		}

	}
}

// ------------------------------------------------------------------------------------------------
/** Recruit a specific team, within the specific radius of the home position. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::recruitSpecificAITeam(TeamPrototype *teamProto, Real recruitRadius)
{
	if (recruitRadius < 1) recruitRadius = 99999.0f;
	//
	// Create "Team in queue" based on team population
	//
	if (teamProto)
	{	
		if (teamProto->getIsSingleton()) {
			Team *singletonTeam = TheTeamFactory->findTeam( teamProto->getName() );
			if (singletonTeam && singletonTeam->hasAnyObjects()) {
				AsciiString teamStr = "Unable to recruit singleton team '";
				teamStr.concat("' because team already exists.");
				TheScriptEngine->AppendDebugMessage(teamStr, false); 
				return;
			}
		}
		if (!teamProto->getTemplateInfo()->m_hasHomeLocation && !isSkirmishAI()) 
		{
			AsciiString teamStr = "Error : team '";
			teamStr.concat(teamProto->getName());
			teamStr.concat("' has no Home Position (or Origin).");
			TheScriptEngine->AppendDebugMessage(teamStr, false);
		}
		// create inactive team to place members into as they are built
		// when team is complete, the team is activated
		Team *theTeam = TheTeamFactory->createInactiveTeam( teamProto->getName() ); 
		AsciiString teamName = teamProto->getName();
		teamName.concat(" - Recruiting.");
		TheScriptEngine->AppendDebugMessage(teamName, false);
		const TCreateUnitsInfo *unitInfo = &teamProto->getTemplateInfo()->m_unitsInfo[0];
//		WorkOrder *orders = NULL;
		Int i;
		Int unitsRecruited = 0;
		// Recruit.
		for( i=0; i<teamProto->getTemplateInfo()->m_numUnitsInfo; i++ )
		{
			const ThingTemplate *thing = TheThingFactory->findTemplate( unitInfo[i].unitThingName );
			if (thing)
			{
				int count = unitInfo[i].maxUnits;
				while (count>0) {
					Object *unit = theTeam->tryToRecruit(thing, &teamProto->getTemplateInfo()->m_homeLocation, recruitRadius);
					if (unit) 
					{
						unitsRecruited++;

						AsciiString teamStr = "Team '";
						teamStr.concat(theTeam->getPrototype()->getName());
						teamStr.concat("' recruits ");
						teamStr.concat(thing->getName());
						teamStr.concat(" from team '");
						teamStr.concat(unit->getTeam()->getPrototype()->getName());
						teamStr.concat("'");
						TheScriptEngine->AppendDebugMessage(teamStr, false);

						unit->setTeam(theTeam);

						AIUpdateInterface *ai = unit->getAIUpdateInterface();
						if (ai) 
						{
#if defined(_DEBUG) || defined(_INTERNAL)
							Coord3D pos = *unit->getPosition();
							Coord3D to = teamProto->getTemplateInfo()->m_homeLocation;
							DEBUG_LOG(("Moving unit from %f,%f to %f,%f\n", pos.x, pos.y , to.x, to.y ));
#endif
							ai->aiMoveToPosition( &teamProto->getTemplateInfo()->m_homeLocation, CMD_FROM_AI);
						}
					} else {
						break;
					}
					count--;
				}
			}
		}
		if (unitsRecruited>0) 
		{
			/* We have something to build. */
			TeamInQueue *team = newInstance(TeamInQueue);
			// Put in front of queue.
			prependTo_TeamReadyQueue(team);
			team->m_priorityBuild = false;
			team->m_workOrders = NULL;
			team->m_frameStarted = TheGameLogic->getFrame();
			team->m_team = theTeam; 
			AsciiString teamName = teamProto->getName();
			teamName.concat(" - Finished recruiting.");
			TheScriptEngine->AppendDebugMessage(teamName, false);
		}	else {
			//disband.
			if (!theTeam->getPrototype()->getIsSingleton()) {
				theTeam->deleteInstance();
				theTeam = NULL;
			}
			AsciiString teamName = teamProto->getName();
			teamName.concat(" - Recruited 0 units, disbanding.");
			TheScriptEngine->AppendDebugMessage(teamName, false);
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** Train our teams. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::processTeamBuilding( void )
{
	// select a new team
	if (selectTeamToBuild()) {
		queueUnits();
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void AIPlayer::queueUnits( void )
{

	queueSupplyTruck();

	// For each member of the current team to build, try to find a faction building to build it.
	//
	for ( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance())
	{
		TeamInQueue *team = iter.cur();
		for( WorkOrder *order = team->m_workOrders; order; order = order->m_next )
		{
			// check if there is a unit on the map that we can steal (recruit) instead of building
			// @todo: Should this try to alter the home location of the recruiting area to 
			// the center of the team, or to the home area of this player?
			Coord3D home = team->m_team->getPrototype()->getTemplateInfo()->m_homeLocation;
			Bool hasHome = false;
			if (team->m_team->getPrototype()->getTemplateInfo()->m_hasHomeLocation) {
				hasHome = true; 
			} else {
				hasHome = getBaseCenter(&home);
			}
			while (order->isWaitingToBuild()) {
				
				Object *unit = team->m_team->tryToRecruit(order->m_thing, &home, TheAI->getAiData()->m_maxRecruitDistance);
				if (unit) 
				{
					order->m_numCompleted++;

					AsciiString teamStr = "Team '";
					teamStr.concat(team->m_team->getPrototype()->getName());
					teamStr.concat("' recruits ");
					teamStr.concat(order->m_thing->getName());
					teamStr.concat(" from team '");
					teamStr.concat(unit->getTeam()->getPrototype()->getName());
					teamStr.concat("'");
					TheScriptEngine->AppendDebugMessage(teamStr, false);

					unit->setTeam(team->m_team);

					AIUpdateInterface *ai = unit->getAIUpdateInterface();
					if (hasHome) {
						ai->aiMoveToPosition( &home, CMD_FROM_AI);
					} else {
						ai->aiIdle(CMD_FROM_AI); // stop, you've been recruited.
					}
				}	else {
					break;
				}
			}
			if (order->isWaitingToBuild())
			{

				// start the creation of a new unit
				startTraining( order, team->m_priorityBuild, team->m_team->getName());
			}
			else
			{
				// we are under construction, verify our factory still exists
				order->validateFactory(m_player);
			}
		}
	}
}


//----------------------------------------------------------------------------------------------------------
/**
 * Frames to wait before trying the next structure, from AIData's StructureSeconds with the
 * rate modifier for a poor or a wealthy player applied.  This was four copies of the same
 * five lines; the skirmish AI needs to bend the result, so it lives in one place now.
 */
Int AIPlayer::computeStructureDelay( void )
{
	return aiHoardAdjustedDelay( computeBuildDelay( TheAI->getAiData()->m_structureSeconds,
													  m_player->getMoney()->countMoney(),
													  TheAI->getAiData()->m_resourcesPoor,
													  TheAI->getAiData()->m_resourcesWealthy,
													  TheAI->getAiData()->m_structuresPoorMod,
													  TheAI->getAiData()->m_structuresWealthyMod,
													  getBuildRateScale() ),
															 m_player->getMoney()->countMoney(),
															 getSkillProfile()->m_cashHoardThreshold );
}

//----------------------------------------------------------------------------------------------------------
/**
 * Where in a repeating check's cycle this player sits.  A player's slot is its index alone, so the
 * answer is the same on every machine and survives a save/load.
 */
Int AIPlayer::computeUpdatePhase( Int playerIndex, Int cycleFrames )
{
	if (playerIndex < 0 || cycleFrames < 1) {
		return 0;
	}
	return ((playerIndex % MAX_PLAYER_COUNT) * cycleFrames) / MAX_PLAYER_COUNT;
}

//----------------------------------------------------------------------------------------------------------
/**
 * The delay arithmetic, with nothing of the Player in it so a test can drive it: the written
 * delay, bent by whichever rate modifier the player's bank account selects, then divided by
 * how much faster than the data this kind of AI is meant to work.
 */
Int AIPlayer::computeBuildDelay( Real seconds, Int money, Int poorAt, Int wealthyAt,
																 Real poorMod, Real wealthyMod, Real rateScale )
{
	if (money < poorAt) {
		seconds = seconds/poorMod;
	}	else if (money > wealthyAt) {
		seconds = seconds/wealthyMod;
	}
	return (Int)(seconds*LOGICFRAMES_PER_SECOND/rateScale);
}

//----------------------------------------------------------------------------------------------------------
/**
 * Frames to wait before trying the next team.  Same as computeStructureDelay, except the
 * base delay is m_teamSeconds, which the SET_BASE_CONSTRUCTION_SPEED script action writes.
 */
Int AIPlayer::computeTeamDelay( void )
{
	return aiHoardAdjustedDelay( computeBuildDelay( (Real)m_teamSeconds,
													  m_player->getMoney()->countMoney(),
													  TheAI->getAiData()->m_resourcesPoor,
													  TheAI->getAiData()->m_resourcesWealthy,
													  TheAI->getAiData()->m_teamPoorMod,
													  TheAI->getAiData()->m_teamWealthyMod,
													  getBuildRateScale() ),
															 m_player->getMoney()->countMoney(),
															 getSkillProfile()->m_cashHoardThreshold );
}

//----------------------------------------------------------------------------------------------------------
/**
 * See if it's time to build another base building.
 */
void AIPlayer::doBaseBuilding( void )
{
	if (m_player->getCanBuildBase()) {
		// See if we are ready to start trying a structure.
		if (!m_readyToBuildStructure) {
			m_structureTimer--;
			if (m_structureTimer<=0) {
				m_readyToBuildStructure = true;
				m_buildDelay = 0;
			}
		}
		// This timer is to keep from banging on the logic each frame.  If something interesting
		// happens, like a building is added or a unit finished, the timers are shortcut.
		m_buildDelay--;		
		if (m_buildDelay<1) {
			if (m_readyToBuildStructure) {
				processBaseBuilding();
			}
			if (m_buildDelay<1) {	// processBaseBuilding may reset m_buildDelay.
				m_buildDelay = 2*LOGICFRAMES_PER_SECOND; // check again in 2 seconds.
			}
			// Note that this timer gets shortcut when a building is completed.
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * See if any ready teams have finished moving to the rally point.
 */
//----------------------------------------------------------------------------------------------------------
/** Is this team worth holding back for?
	*
	* Only attack waves: a reinforcement is going to an existing team and a base defence team is
	* needed where it stands.  The strength in hand is everything still waiting in the ready queue,
	* which is what would go out together if we released now.
	*/
//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::shouldHoldForMassing( TeamInQueue *team )
{
	const AIDifficultyProfile *profile = getSkillProfile();
	if( !profile->m_massBeforeAttacking )
		return FALSE;
	if( team == NULL || team->m_reinforcement )
		return FALSE;

	const TeamTemplateInfo *info = team->m_team ? team->m_team->getPrototype()->getTemplateInfo() : NULL;
	if( info == NULL || info->m_isBaseDefense || info->m_isPerimeterDefense )
		return FALSE;

	// there is a fight at home: nothing waits at a rally point while the base is being hit
	const UnsignedInt now = TheGameLogic->getFrame();
	const Bool baseUnderAttack =
			(m_player->getAttackedFrame() + 30 * LOGICFRAMES_PER_SECOND > now) && (m_player->getAttackedFrame() > 0);

	// the existing sixty-second valve stays the outer bound on any wait
	const Bool timeExpired = team->m_frameStarted + 60 * LOGICFRAMES_PER_SECOND < now;

	// what is in hand: everything still queued up behind the rally point
	Real waitingThreat = 0.0f;
	for( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamReadyQueue(); !iter.done(); iter.advance() )
	{
		TeamInQueue *t = iter.cur();
		if( t == NULL || t->m_team == NULL )
			continue;
		for( DLINK_ITERATOR<Object> objIter = t->m_team->iterate_TeamMemberList(); !objIter.done(); objIter.advance() )
		{
			Object *obj = objIter.cur();
			if( obj == NULL || obj->isEffectivelyDead() )
				continue;
			waitingThreat += aiCombatPower( obj );
		}
	}

	//
	// How much of what it can see of the enemy army it wants in hand first. The role's knob, not
	// the rung's: an aggressive AI commits earlier and with less, a defensive one waits for more
	// and would rather counter-attack after absorbing a hit. Same money, spent differently.
	//
	const Real massFraction = (m_role == AIROLE_AGGRESSIVE) ? 0.8f : 1.3f;

	AIEnemyComposition enemy;
	computeEnemyComposition( &enemy );

	return aiShouldMass( waitingThreat, enemy.m_totalThreat, massFraction, timeExpired, baseUnderAttack );
}

void AIPlayer::checkReadyTeams( void )
{
	// See if any ready teams are gathered at their rally point
	{	// needed to scope iter.  silly ms c++.
		for ( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamReadyQueue(); !iter.done(); iter.advance())
		{
			TeamInQueue *team = iter.cur();
			// If 60 seconds passed, start anyway.
			Bool timeExpired = team->m_frameStarted+60*LOGICFRAMES_PER_SECOND < TheGameLogic->getFrame();
			Bool allIdle=TRUE;	
			Bool anyIdle = FALSE;
			if (team->m_reinforcement) {
				Object *obj = TheGameLogic->findObjectByID(team->m_reinforcementID);
				if (obj && obj->getAIUpdateInterface()) {
					allIdle = obj->getAIUpdateInterface()->isIdle();
					anyIdle = allIdle;
				}
			} else {
				allIdle = team->m_team->isIdle();
				for (DLINK_ITERATOR<Object> iter = team->m_team->iterate_TeamMemberList(); !iter.done(); iter.advance()) {
					Object *obj = iter.cur();	
					if (obj->getAI() && obj->getAI()->isIdle()) {
						anyIdle = true;
					}
				}
			}
			if (anyIdle && team->m_team->getPrototype()->getTemplateInfo()->m_executeActions) {
				const Script *script = TheScriptEngine->findScriptByName(team->m_team->getPrototype()->getTemplateInfo()->m_productionCondition);
				if (script && script->getAction()) {
					// we have a start action.  So don't wait for allIdle as the team may be guarding.
					allIdle = true;
				}
			}
			if (timeExpired) allIdle = true;
			//
			// C2 (massing before attacking) was hooked in here and is deliberately not any more.
			// Holding a finished team at the rally point by leaving it in the ready queue jams the
			// whole production pipeline behind it - the team still counts as an instance, so no
			// replacement is selected, and 20 measured brutal-vs-brutal matches went from nine
			// decided to none, with zero kills and a quarter of the spending on both sides.
			//
			// The second half of the lesson is in the threshold rather than the plumbing: a wait
			// measured against the enemy's whole visible army deadlocks two symmetric AIs, because
			// neither ever has more than the other. Wherever this lands next, it has to let the
			// team activate (so production keeps moving) and hold it somewhere that is not the
			// queue, against a threshold that is not a fraction of the opponent.
			//
			// shouldHoldForMassing and aiShouldMass are kept, with their test, for that attempt.
			//
			if (allIdle) {
				if (!team->m_sentToStartLocation) {
					team->m_sentToStartLocation = true;
					/*
					if (team->m_team->getPrototype()->getTemplateInfo()->m_hasHomeLocation && 
							!team->m_reinforcement) {
 						AIGroup* theGroup = TheAI->createGroup();
						if (theGroup) {
							team->m_team->getTeamAsAIGroup(theGroup);
							Coord3D destination = team->m_team->getPrototype()->getTemplateInfo()->m_homeLocation;
							theGroup->groupTightenToPosition( &destination, false, CMD_FROM_AI );
							team->m_frameStarted = TheGameLogic->getFrame();
							continue;
						}
					}
					*/
				}
				// Start the team up.
				removeFrom_TeamReadyQueue(team);
				if (team->m_reinforcement) {
					Object *obj = TheGameLogic->findObjectByID(team->m_reinforcementID);
					if (obj&&obj->getAIUpdateInterface()) {
						obj->getAIUpdateInterface()->joinTeam();
					}
				} else {
					// mark our completed team as "active" - this will invoke any OnCreate scripts, etc.
					team->m_team->setActive();
					if (isSkirmishAI()) {
						TheScriptEngine->clearTeamFlags();
					}
					if (TheGlobalData->m_debugAI) {
						AsciiString teamName = team->m_team->getPrototype()->getName();
						teamName.concat(" - team activated.");
						TheScriptEngine->AppendDebugMessage(teamName, false);
					}
				}
				team->deleteInstance();
				iter = iterate_TeamReadyQueue();
			}																		 
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * See if any queued teams have finished building, or have run out of time.
 */
void AIPlayer::checkQueuedTeams( void )
{
	// See if any teams are expired.
	{	// needed to scope iter.  silly ms c++.
		for ( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance())
		{
			TeamInQueue *team = iter.cur();
			if (team && team->isBuildTimeExpired())	{
				if (team->isMinimumBuilt()) 
				{
					if (team->areBuildsComplete()) {
						// Move to ready queue
						removeFrom_TeamBuildQueue(team);
						prependTo_TeamReadyQueue(team);
					}	else {
						continue;
					}
				}	else {
					// Disband.
					removeFrom_TeamBuildQueue(team);
					team->disband();
					team->deleteInstance();
					if (isSkirmishAI()) {
						TheScriptEngine->clearTeamFlags();
					}
				}	 
				iter = iterate_TeamBuildQueue();
			}
		}
	}

	// See if any teams are ready.
	{	// needed to scope iter.  silly ms c++.
		for ( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance())
		{
			TeamInQueue *team = iter.cur();
			if (team && team->isAllBuilt())
			{
				// Move to ready queue
				removeFrom_TeamBuildQueue(team);
				prependTo_TeamReadyQueue(team);
				iter = iterate_TeamBuildQueue();
				continue;
			}
			Bool anyIdle = false;
			for (DLINK_ITERATOR<Object> iter = team->m_team->iterate_TeamMemberList(); !iter.done(); iter.advance()) {
				Object *obj = iter.cur();	
				if (obj && obj->getAI() && obj->getAI()->isIdle()) {
					anyIdle = true;
				}
			}
			if (anyIdle) {
				if (team->m_team->getPrototype()->getTemplateInfo()->m_executeActions) {
					const Script *script = TheScriptEngine->findScriptByName(team->m_team->getPrototype()->getTemplateInfo()->m_productionCondition);
					if (script) {
						TheScriptEngine->friend_executeAction(script->getAction(), team->m_team);
					}
				}
			}	
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * See if it is time to start another ai team building.
 */
void AIPlayer::doTeamBuilding( void )
{
	// See if any teams are expired.
	if (m_player->getCanBuildUnits()) {
		// See if we are ready to start trying a team.
		if (!m_readyToBuildTeam) {
			m_teamTimer--;
			if (m_teamTimer<=0) {
				m_readyToBuildTeam = true;
				m_teamDelay = 0;
			}
		}

		// This timer is to keep from banging on the logic each frame.  If something interesting
		// happens, like a building is added or a unit finished, the timers are shortcut.
		m_teamDelay--;
		if (m_teamDelay<1) {
			queueUnits(); // update the queues.
			if (m_readyToBuildTeam) {
				processTeamBuilding();
			}
			m_teamDelay = 5*LOGICFRAMES_PER_SECOND; // check again in 5 seconds.
			// Note that this timer gets shortcut when a unit or building is completed.
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * See if it is time to start another upgrade or skill building.
 */
void AIPlayer::doUpgradesAndSkills( void )
{
	if (TheGameLogic->getFrame() < 2) {
		// can't do updates on the first few frames
		return;
	}

	Bool checkScience = m_player->getSciencePurchasePoints()>0;
	if (!checkScience) {
		return;
	}
	const AISideInfo *sideInfo = TheAI->getAiData()->m_sideInfo;
	while (sideInfo) {
		if (sideInfo->m_side == m_player->getSide()) {
			break;
		}
		sideInfo = sideInfo->m_next;
	}
	if (sideInfo == NULL) return;

	if (m_skillsetSelector == INVALID_SKILLSET_SELECTION) {
		Int limit = 0;		
		// Pick randomly among the skillsets that have skills.
		// Designers sometimes only define skillset 1 & 2, or some such.  jba.
		if (sideInfo->m_skillSet2.m_numSkills>0) {
			limit = 1;
			if (sideInfo->m_skillSet3.m_numSkills>0) {
				limit = 2;
				if (sideInfo->m_skillSet4.m_numSkills>0) {
					limit = 3;
					if (sideInfo->m_skillSet5.m_numSkills>0) {
						limit = 4;
					}
				}
			}
		}
		if (isSkirmishAI()) {
			//
			// Split the designers' skill sets between the two roles rather than rolling across all
			// of them (D8).  This makes no claim about which set is which - only that an aggressive
			// AI and a defensive one draw from different halves, so the powers you see coming tell
			// you which one you are facing.  Still a roll, so repeat matches still differ.
			//
			if (limit > 0) {
				const Int half = limit / 2;
				if (m_role == AIROLE_AGGRESSIVE)
					m_skillsetSelector = GameLogicRandomValue(0, half);
				else
					m_skillsetSelector = GameLogicRandomValue(half, limit);
			} else {
				m_skillsetSelector = 0;
			}
		} else {
			m_skillsetSelector = 0; // Non-skirmish default to 0.  jba.
		}
	}

	// SKILLS
	if (m_player->getSciencePurchasePoints()>0) {
		const TSkillSet *skillset;
		switch(m_skillsetSelector) {
			default:
			case 0: skillset = &sideInfo->m_skillSet1; break;
			case 1: skillset = &sideInfo->m_skillSet2; break;
			case 2: skillset = &sideInfo->m_skillSet3; break;
			case 3: skillset = &sideInfo->m_skillSet4; break;
			case 4: skillset = &sideInfo->m_skillSet5; break;
		}
		const Bool saves = getSkillProfile()->m_savesSciencePoints;
		Int i;
		for (i=0; i<skillset->m_numSkills; i++) {
			ScienceType science = skillset->m_skills[i];

			//
			// Saving up (B5).  EA walked the whole set and bought the first thing it could afford,
			// so a point went on filler while the ability the set is actually built around sat two
			// points away for ever.  If the next thing in the designer's own order is only out of
			// reach on points, stop here and keep them; anything else - already owned, or blocked
			// by a rank we have not reached - is skipped as before.
			//
			if (saves && !m_player->hasScience(science)) {
				const Int cost = TheScienceStore->getSciencePurchaseCost(science);
				if (cost > m_player->getSciencePurchasePoints()) {
					break;
				}
			}

			if (m_player->isCapableOfPurchasingScience(science)) {
				if (m_player->attemptToPurchaseScience(science)) {
						AsciiString msg = TheNameKeyGenerator->keyToName(m_player->getPlayerNameKey());
						msg.concat(" purchases from SkillSet");
						msg.concat('1'+m_skillsetSelector);
						msg.concat(' ');
						msg.concat(TheScienceStore->getInternalNameForScience(science));
						msg.concat(".");
						TheScriptEngine->AppendDebugMessage( msg, false);
				}
			}
		}
	}


}

//----------------------------------------------------------------------------------------------------------
/**
 * Perform computer-controlled player AI
 */
//DECLARE_PERF_TIMER(AIPlayer_update)
#ifdef DEBUG_LOGGING
/* The slow-frame report in GameLogic.cpp could only say "players 16.2ms" - one number for every
	 computer player and all ten of the jobs each one does. That is enough to know the stutter is in
	 here and not enough to do anything about it, so this breaks it down the way ScriptEngine and
	 Pathfinder already break theirs down: per job, plus whichever single player cost the most.
	 Reset once per logic frame by AI::update. */
enum { AIP_BASE, AIP_READY, AIP_QUEUED, AIP_TEAM, AIP_UPGRADE,
			 AIP_BRIDGE, AIP_SCOUT, AIP_RETREAT, AIP_EXPAND, AIP_CAPTURE, AIP_PHASE_COUNT };
static const char *theAIPhaseName[ AIP_PHASE_COUNT ] =
	{ "base", "ready", "queued", "team", "upg", "bridge", "scout", "retreat", "expand", "capture" };
static Real theAIPhaseMS[ AIP_PHASE_COUNT ];
static Real theAIWorstPlayerMS = 0.0f;
static Int theAIWorstPlayer = -1;
static Int theAIPlayersUpdated = 0;
static Int64 theAIPhaseStart;

static void aiPhaseBegin( void )
{
	QueryPerformanceCounter( (LARGE_INTEGER *)&theAIPhaseStart );
}

static void aiPhaseEnd( Int phase )
{
	Int64 now;
	QueryPerformanceCounter( (LARGE_INTEGER *)&now );
	theAIPhaseMS[ phase ] += aiPlayerElapsedMS( theAIPhaseStart, now );
}


#define AI_PHASE( phase, call )	do { aiPhaseBegin(); call; aiPhaseEnd( phase ); } while(0)

/*static*/ void AIPlayer::resetFrameProfile( void )
{
	for( Int i = 0; i < AIP_PHASE_COUNT; ++i )
		theAIPhaseMS[ i ] = 0.0f;
	theAIWorstPlayerMS = 0.0f;
	theAIWorstPlayer = -1;
	theAIPlayersUpdated = 0;
	for( Int s = 0; s < BASE_SUB_COUNT; ++s )
	{
		theAIBaseSubMS[ s ] = 0.0f;
		theAIBaseSubCalls[ s ] = 0;
	}
}

/*static*/ const char *AIPlayer::getProfileReport( void )
{
	static char report[ 512 ];
	Int used = sprintf( report, "%d ai players, worst player %d %.1fms |",
											theAIPlayersUpdated, theAIWorstPlayer, theAIWorstPlayerMS );
	for( Int phase = 0; phase < AIP_PHASE_COUNT; ++phase )
		used += sprintf( report + used, " %s %.1f", theAIPhaseName[ phase ], theAIPhaseMS[ phase ] );
	used += sprintf( report + used, " || base:" );
	for( Int s = 0; s < BASE_SUB_COUNT; ++s )
		used += sprintf( report + used, " %s %.1f/%dx",
										 theAIBaseSubName[ s ], theAIBaseSubMS[ s ], theAIBaseSubCalls[ s ] );
	return report;
}
#else
/*static*/ void AIPlayer::resetFrameProfile( void ) { }
/*static*/ const char *AIPlayer::getProfileReport( void ) { return ""; }
#define AI_PHASE( phase, call )	call
#endif

void AIPlayer::update( void )
{
	//USE_PERF_TIMER(AIPlayer_update)
#ifdef DEBUG_LOGGING
	Int64 playerStart;
	QueryPerformanceCounter( (LARGE_INTEGER *)&playerStart );
#endif

	AI_PHASE( AIP_BASE,    doBaseBuilding() );			// See if it's time to build another building.
	AI_PHASE( AIP_READY,   checkReadyTeams() );			// See if any teams are ready to start.
	AI_PHASE( AIP_QUEUED,  checkQueuedTeams() );		// See if any teams are complete.
	AI_PHASE( AIP_TEAM,    doTeamBuilding() );			// See if it's time to start another team.
	AI_PHASE( AIP_UPGRADE, doUpgradesAndSkills() );	// See if it's time to build an upgrade or buy a skill.
	AI_PHASE( AIP_BRIDGE,  updateBridgeRepair() );	// Handle any bridge repairs.
	AI_PHASE( AIP_SCOUT,   doScouting() );					// Keep one unit looking at the map.
	AI_PHASE( AIP_RETREAT, doRetreats() );					// Break off the fights we are losing.
	AI_PHASE( AIP_EXPAND,  doExpansion() );					// Go and take the money that is lying around.
	AI_PHASE( AIP_CAPTURE, doCapture() );						// ... and the money that is standing around.

#ifdef DEBUG_LOGGING
	Int64 playerEnd;
	QueryPerformanceCounter( (LARGE_INTEGER *)&playerEnd );
	const Real playerMS = aiPlayerElapsedMS( playerStart, playerEnd );
	++theAIPlayersUpdated;
	if( playerMS > theAIWorstPlayerMS )
	{
		theAIWorstPlayerMS = playerMS;
		theAIWorstPlayer = m_player ? m_player->getPlayerIndex() : -1;
	}
#endif
}

//----------------------------------------------------------------------------------------------------------
/**
 * Find any things that build stuff & add them to the build list.  Then build any initially built
 * buildings.
 */
void AIPlayer::newMap( void )
{
	BuildListInfo *info = m_player->getBuildList();
	// Add any factories placed to the build list.
	Object *obj;
	for( obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{

		Player *owner = obj->getControllingPlayer();
		if (owner==m_player) {
			// See if it's a factory.
			ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();
			// If it doesn't produce, continue.
			if (!pu) continue;
			m_player->addToBuildList(obj);
		}

	}
	computeCenterAndRadiusOfBase(&m_baseCenter, &m_baseRadius);

	// Build any with the initially built flag.
	for( /* nothing */; info; info = info->getNext() )
	{
		AsciiString name = info->getTemplateName();
		if (name.isEmpty()) continue;
		const ThingTemplate *bldgPlan = TheThingFactory->findTemplate( name );
		if (!bldgPlan) {																											 
			DEBUG_LOG(("*** ERROR - Build list building '%s' doesn't exist.\n", name.str()));
			continue;
		}
		if (info->isInitiallyBuilt()) {
			buildStructureNow(bldgPlan, info);
		} else {
			info->incrementNumRebuilds(); // the initial build in the normal build list consumes a rebuild, so add one.
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** Find the center of the base and the radius of buildings.  */
// ------------------------------------------------------------------------------------------------
void AIPlayer::computeCenterAndRadiusOfBase(Coord3D *center, Real *radius)
{
	//
	BuildListInfo *info;
	Coord2D totalPos;
	totalPos.x = 0;
	totalPos.y = 0;
	Int numBldg=0;
	for( info = m_player->getBuildList(); info; info = info->getNext() )
	{
		AsciiString name = info->getTemplateName();
		if (name.isEmpty()) continue;
		const ThingTemplate *bldgPlan = TheThingFactory->findTemplate( name );
		if (!bldgPlan) {																											 
			continue;
		}
		Coord3D pos = *info->getLocation();
		totalPos.x += pos.x;
		totalPos.y += pos.y;
		numBldg++;
	}
	if (numBldg>0) {
		totalPos.x /= numBldg;
		totalPos.y /= numBldg;
	}

	m_baseCenterSet = numBldg>0;
	center->x = totalPos.x;
	center->y = totalPos.y;

	Real maxRadSqr = 0;
	//
	for( info = m_player->getBuildList(); info; info = info->getNext() )
	{
		AsciiString name = info->getTemplateName();
		if (name.isEmpty()) continue;
		const ThingTemplate *bldgPlan = TheThingFactory->findTemplate( name );
		if (!bldgPlan) {																											 
			continue;
		}
		Coord3D pos = *info->getLocation();
		Real dx = pos.x-center->x;
		Real dy = pos.y-center->y;
		if (dx<0) dx = -dx;
		if (dy<0) dy = -dy;
		Real bldgRadius = bldgPlan->getTemplateGeometryInfo().getBoundingCircleRadius()*0.4f;
		dx += bldgRadius;
		dy += bldgRadius; 
		Real radSqr = dx*dx+dy*dy;
		if (radSqr>maxRadSqr) maxRadSqr=radSqr;
	}
	*radius = sqrt(maxRadSqr);
}

//----------------------------------------------------------------------------------------------------------
/**
 * Checks to see if we're building a dozer.
 */
Bool AIPlayer::dozerInQueue( void )
{
	{	// needed to scope iter.  silly ms c++.
		for ( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance())
		{
			TeamInQueue *team = iter.cur();
			if (team && team->includesADozer() )
			{
				return true; // dozer is building already.
			}
		}
	}
	return false;
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
/** Notice that there is money to be had, and go and take it.
	*
	* Every piece of this already existed and only ever ran from a script: buildBySupplies places a
	* structure beside a warehouse, findSupplyCenter picks a warehouse that is worth taking and is
	* not already served, and guardSupplyCenter sends a team to sit on one.  What was missing was
	* any decision to call them - so an AI ran its starting piles dry and then simply stopped
	* earning.
	*
	* The defence goes down in the same job: an undefended expansion is a gift, and the same
	* buildBySupplies call places a base defence structure beside the warehouse facing the enemy
	* (that branch of it is written for exactly this and had no caller either).
	*/
//----------------------------------------------------------------------------------------------------------
void AIPlayer::doExpansion( void )
{
	const AIDifficultyProfile *profile = getSkillProfile();
	if( !profile->m_selfTriggeredExpansion )
		return;

	if( --m_expandTimer > 0 )
		return;
	m_expandTimer = EXPANSION_CHECK_SECONDS * LOGICFRAMES_PER_SECOND;

	if( !m_player->getCanBuildBase() || !m_baseCenterSet )
		return;

	//
	// What this faction calls a supply centre: whatever is already flagged as one on the build
	// list.  Faction-correct and mod-correct without a table, and if the AI has never had one it
	// has nothing to expand with anyway.
	//
	AsciiString supplyCenterName;
	for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
	{
		if( info->isSupplyBuilding() && info->getTemplateName().isNotEmpty() )
		{
			supplyCenterName = info->getTemplateName();
			break;
		}
	}
	if( supplyCenterName.isEmpty() )
		return;

	const ThingTemplate *tmpl = TheThingFactory->findTemplate( supplyCenterName, FALSE );
	if( tmpl == NULL )
		return;

	// pay for it out of what is spare, not out of the army's money
	const Int cost = tmpl->calcCostToBuild( m_player );
	if( m_player->getMoney()->countMoney() < 2 * cost )
		return;

	//
	// Somewhere worth going. findSupplyCenter already refuses a warehouse that is picked clean, one
	// this AI already has a centre beside, and one that is closer to the enemy than to us.
	//
	const Int WORTH_TAKING = 1000;
	if( findSupplyCenter( WORTH_TAKING ) == NULL )
		return;

	buildBySupplies( WORTH_TAKING, supplyCenterName );

	if( profile->m_defendExpansions )
	{
		const AISideInfo *resInfo = TheAI->getAiData()->m_sideInfo;
		while( resInfo )
		{
			if( resInfo->m_side == m_player->getSide() )
			{
				if( resInfo->m_baseDefenseStructure1.isNotEmpty() )
					buildBySupplies( WORTH_TAKING, resInfo->m_baseDefenseStructure1 );
				break;
			}
			resInfo = resInfo->m_next;
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/** How far around a fight to look for what is in it.  Wide enough to catch the base defences and
	* the second rank shooting into it, not so wide that a skirmish at the front counts the garrison
	* at the back as part of the same exchange. */
static const Real RETREAT_ENGAGEMENT_RADIUS = 300.0f;

/** A unit's contribution to an exchange: what it can still take, and what it can still deal.  The
	* threat value the data already carries stands in for damage per second - it is what
	* AI_threatScore uses for the same purpose. */
static void addToForce( const Object *obj, Real *health, Real *power )
{
	if( obj == NULL || obj->isEffectivelyDead() )
		return;

	const Real power2 = aiCombatPower( obj );
	if( power2 <= 0.0f )
		return;			// it is not in the exchange, so neither its health nor its damage counts

	const BodyModuleInterface *body = obj->getBodyModule();
	if( body )
		*health += body->getHealth();
	*power += power2;
}

//----------------------------------------------------------------------------------------------------------
/** Who a "fall back to the base" order can be handed to.
	*
	* Not aircraft.  A jet or a helicopter flies its own round trip - take off, spend the load, land
	* to reload and heal - and a move order is what starts that cycle.  Handing a parked aircraft one
	* every decision interval had it take off, fly at the base centre, go idle, land, and take off
	* again on the next tick, for as long as a fight near the base kept reading as lost: the
	* airfield emptied and filled every few seconds and nothing ever reached the enemy.  Everything
	* that sends an aircraft home is already in JetAIUpdate - out of ammo, out of special ammo, idle
	* for ReturnToBaseIdleTime - so the retreat has nothing to add and no business interrupting it.
	*
	* Split out with plain flags so the rule is testable without a running game.
	*/
Bool AIRetreat_canBeOrderedHome( Bool hasAI, Bool isStructureOrImmobile, Bool ownsItsOwnLanding )
{
	return hasAI && !isStructureOrImmobile && !ownsItsOwnLanding;
}

/** The same rule, asked of a real object. */
static Bool retreatCanOrderHome( const Object *obj )
{
	if( obj == NULL )
		return FALSE;
	const AIUpdateInterface *ai = obj->getAI();
	return AIRetreat_canBeOrderedHome( ai != NULL,
			obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_IMMOBILE ),
			ai != NULL && ai->getJetAIUpdate() != NULL );
}

//----------------------------------------------------------------------------------------------------------
/** Look at every fight this AI is in and break off the ones it is losing.
	*
	* Deliberately at the player level and on the rung's decision interval rather than inside the
	* unit update: it is one partition query per engaged team every few seconds, not a judgement
	* every unit makes every frame, and it keeps the whole of "when do we quit" in one readable
	* place.
	*/
//----------------------------------------------------------------------------------------------------------
void AIPlayer::doRetreats( void )
{
	const AIDifficultyProfile *profile = getSkillProfile();
	if( profile->m_retreatTtkRatio <= 0.0f )
		return;			// the bottom rung does not know how to quit, on purpose

	if( --m_retreatTimer > 0 )
		return;
	m_retreatTimer = REAL_TO_INT_CEIL( profile->m_decisionIntervalSeconds * LOGICFRAMES_PER_SECOND );
	if( m_retreatTimer < 1 )
		m_retreatTimer = 1;

	if( !m_baseCenterSet )
		return;			// nowhere to fall back to

	for( Player::PlayerTeamList::const_iterator t = m_player->getPlayerTeams()->begin();
			 t != m_player->getPlayerTeams()->end(); ++t )
	{
		const TeamTemplateInfo *info = (*t)->getTemplateInfo();
		if( info && (info->m_isBaseDefense || info->m_isPerimeterDefense) )
			continue;			// a base defence team that falls back has abandoned the thing it defends

		for( DLINK_ITERATOR<Team> iter = (*t)->iterate_TeamInstanceList(); !iter.done(); iter.advance() )
		{
			Team *team = iter.cur();
			if( team == NULL )
				continue;

			// where this team is
			Real count = 0.0f;
			Coord3D centre;
			centre.zero();
			for( DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance() )
			{
				Object *obj = objIter.cur();
				if( obj == NULL || obj->isEffectivelyDead() )
					continue;
				if( obj->isKindOf( KINDOF_PROJECTILE ) || !retreatCanOrderHome( obj ) )
					continue;			// only the units that could actually be pulled out place the fight
				centre.x += obj->getPosition()->x;
				centre.y += obj->getPosition()->y;
				count += 1.0f;
			}
			if( count < 1.0f )
				continue;
			centre.x /= count;
			centre.y /= count;
			centre.z = TheTerrainLogic->getGroundHeight( centre.x, centre.y );

			//
			// Both sides of the exchange, measured the same way: everything of ours in this fight
			// against everything of theirs in it.
			//
			// Not this team against everything of theirs.  An AI team is one to three units, and the
			// first cut of this compared one such team with every enemy within three hundred feet -
			// so a team of two tanks read a battle it was part of as 2,600 against 29,400, decided
			// it was losing by a hundred to one and walked home.  Every team did, every few seconds,
			// for the whole match: twenty measured matches ended with zero kills on both sides.
			//
			Real myHealth = 0.0f, myPower = 0.0f;
			Real enemyHealth = 0.0f, enemyPower = 0.0f;
			{
				PartitionFilterAlive filterAlive;
				PartitionFilterOnMap filterOnMap;
				PartitionFilter *filters[] = { &filterAlive, &filterOnMap, 0 };

				MemoryPoolObjectHolder hold;
				SimpleObjectIterator *nearby = ThePartitionManager->iterateObjectsInRange(
						&centre, RETREAT_ENGAGEMENT_RADIUS, FROM_CENTER_2D, filters );
				hold.hold( nearby );
				for( Object *e = nearby->first(); e; e = nearby->next() )
				{
					if( e->isKindOf( KINDOF_PROJECTILE ) )
						continue;
					//
					// Only the field battle. Base defences shoot, but they are what an attack goes
					// *through* - counting them makes every approach to a defended base read as a
					// lost fight, and the AI never attacks anything again. Picking a way in past
					// them is the influence map's job (B4), not the retreat's.
					//
					if( e->isKindOf( KINDOF_STRUCTURE ) || e->isKindOf( KINDOF_IMMOBILE ) )
						continue;

					if( e->getControllingPlayer() == m_player )
					{
						addToForce( e, &myHealth, &myPower );
					}
					else if( m_player->getRelationship( e->getTeam() ) == ENEMIES )
					{
						// only what it can see: an AI that pulls back from something it has not
						// found is reading the object list again (A2)
						if( observerKnowsAbout( e, m_player->getPlayerIndex() ) )
							addToForce( e, &enemyHealth, &enemyPower );
					}
				}
			}

			if( enemyPower <= 0.0f )
				continue;			// not in a fight

			const Real ratio = aiRetreatRatio( myHealth, myPower, enemyHealth, enemyPower );
			if( ratio >= profile->m_retreatTtkRatio )
				continue;			// holding, or winning

			//
			// Losing.  The whole team goes home if this rung knows how; otherwise the members that
			// are personally finished go, which saves the units that would otherwise die inside a
			// fight the team as a whole is still winning.
			//
			for( DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance() )
			{
				Object *obj = objIter.cur();
				if( obj == NULL || obj->isEffectivelyDead() || !retreatCanOrderHome( obj ) )
					continue;

				if( !profile->m_retreatTeams )
				{
					if( !profile->m_retreatIndividualUnits )
						continue;
					// this one's own exchange, against the same enemy force
					Real oneHealth = 0.0f, onePower = 0.0f;
					addToForce( obj, &oneHealth, &onePower );
					if( aiRetreatRatio( oneHealth, onePower, enemyHealth, enemyPower ) >= profile->m_retreatTtkRatio )
						continue;
				}

				obj->getAI()->aiMoveToPosition( &m_baseCenter, CMD_FROM_AI );
			}
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/** The cheapest thing this player could build that can walk somewhere and is not needed elsewhere.
	* Faction-agnostic on purpose - it lands on the Ranger, the Red Guard, the Rebel and the
	* Technical without a per-side table to keep, and on whatever a mod's cheapest unit is. */
static const ThingTemplate *cheapestScoutTemplate( Player *player )
{
	const ThingTemplate *best = NULL;
	Int bestCost = 0;

	for( const ThingTemplate *t = TheThingFactory->firstTemplate(); t; t = t->friend_getNextTemplate() )
	{
		if( !t->isKindOf( KINDOF_INFANTRY ) && !t->isKindOf( KINDOF_VEHICLE ) )
			continue;
		if( t->isKindOf( KINDOF_DOZER ) || t->isKindOf( KINDOF_HARVESTER ) )
			continue;		// those have a job
		if( t->isKindOf( KINDOF_AIRCRAFT ) || t->isKindOf( KINDOF_IMMOBILE ) )
			continue;
		if( !player->canBuild( t ) )
			continue;

		Int cost = t->calcCostToBuild( player );
		if( cost <= 0 )
			continue;
		if( best == NULL || cost < bestCost )
		{
			best = t;
			bestCost = cost;
		}
	}
	return best;
}

//----------------------------------------------------------------------------------------------------------
/**
 * A unit of ours that is free to go and look at things.  The default team is where anything built
 * outside the team system lands - the scout queued below, and the odd leftover - so nothing here is
 * taken off a team that has a mission.
 */
Object *AIPlayer::findScout( void )
{
	Team *team = m_player->getDefaultTeam();
	if( team == NULL )
		return NULL;

	for( DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance() )
	{
		Object *obj = iter.cur();
		if( obj == NULL || obj->isEffectivelyDead() )
			continue;
		//
		// A positive test, not a list of exclusions: the default team also holds everything the
		// player has in flight, and a missile is mobile, carries an AIUpdate and belongs to no
		// team - the first draft of this went scouting with a Patriot missile several times a
		// minute, and never with anything that could actually go and look.
		//
		if( !obj->isKindOf( KINDOF_INFANTRY ) && !obj->isKindOf( KINDOF_VEHICLE ) )
			continue;
		if( obj->isKindOf( KINDOF_PROJECTILE ) || obj->isKindOf( KINDOF_IMMOBILE ) )
			continue;
		if( obj->isKindOf( KINDOF_DOZER ) || obj->isKindOf( KINDOF_HARVESTER ) )
			continue;		// the economy is not a scout
		if( obj->isKindOf( KINDOF_AIRCRAFT ) )
			continue;
		if( obj->getAI() == NULL )
			continue;

		Bool alreadyScouting = FALSE;
		for( Int slot = 0; slot < MAX_AI_SCOUTS; ++slot )
			if( m_scoutID[ slot ] == obj->getID() )
				alreadyScouting = TRUE;
		if( alreadyScouting )
			continue;

		return obj;
	}
	return NULL;
}

//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::scoutInQueue( void )
{
	for( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance() )
	{
		TeamInQueue *team = iter.cur();
		if( team == NULL )
			continue;
		for( WorkOrder *order = team->m_workOrders; order; order = order->m_next )
			if( order->m_isScout )
				return true;
	}
	return false;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Put one cheap unit on order to do the looking.  Same shape as queueDozer: a one-order priority
 * team on the player's default team, so it never disturbs the team the AI is massing.
 */
void AIPlayer::queueScout( void )
{
	queueSupportUnit( cheapestScoutTemplate( m_player ), "SCOUT" );
}

//----------------------------------------------------------------------------------------------------------
/**
 * Put one cheap unit on order for a job outside the team system - the scout, the capturer.  Same
 * shape as queueDozer: a one-order priority team on the player's default team, so it never disturbs
 * the team the AI is massing, and it waits for spare change so it can never be what stops a base
 * going up.  One at a time, whichever job asked first.
 */
void AIPlayer::queueSupportUnit( const ThingTemplate *tTemplate, const char *what )
{
	if( tTemplate == NULL || scoutInQueue() )
		return;

	// scouting must never be what stops a base going up, so it waits for spare change
	if( m_player->getMoney()->countMoney() < 2 * tTemplate->calcCostToBuild( m_player ) )
		return;

	Bool canBuildUnits = m_player->getCanBuildUnits();
	m_player->setCanBuildUnits( true );

	Object *factory = findFactory( tTemplate, true );
	if( factory )
	{
		WorkOrder *order = newInstance(WorkOrder);
		order->m_thing = tTemplate;
		order->m_factoryID = INVALID_ID;
		order->m_numRequired = 1;
		order->m_required = true;
		order->m_isResourceGatherer = FALSE;
		order->m_isScout = TRUE;
		order->m_next = NULL;

		TeamInQueue *team = newInstance(TeamInQueue);
		prependTo_TeamBuildQueue( team );
		team->m_priorityBuild = true;
		team->m_workOrders = order;
		team->m_frameStarted = TheGameLogic->getFrame();
		team->m_team = m_player->getDefaultTeam();

		AsciiString msg = what;
		msg.concat( " - building one at the " );
		msg.concat( factory->getTemplate()->getName() );
		TheScriptEngine->AppendDebugMessage( msg, false );

		m_teamDelay = 0;
		startTraining( order, team->m_priorityBuild, team->m_team->getName() );
	}

	m_player->setCanBuildUnits( canBuildUnits );
}

//----------------------------------------------------------------------------------------------------------
/** How often the start-position picture is brought up to date.  doScouting is called every frame and
	* then sits behind its own timer for up to a rung's scouting interval, which would be far too slow
	* to notice a scout arriving somewhere. */
static const Int START_INTEL_RATE = LOGICFRAMES_PER_SECOND;

//----------------------------------------------------------------------------------------------------------
/** The three numbers the odds are made of: enemies still in the game, positions known to hold one,
	* and positions nobody has looked at yet. */
void AIPlayer::countStartIntel( Int *enemies, Int *occupied, Int *unchecked ) const
{
	*enemies = *occupied = *unchecked = 0;
	if( ThePlayerList == NULL )
		return;

	const Int playerCount = ThePlayerList->getPlayerCount();
	for( Int i = 0; i < playerCount && i < MAX_PLAYER_COUNT; ++i )
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		if( p == NULL || p == m_player || !p->isPlayerActive() )
			continue;
		if( m_player->getRelationship( p->getDefaultTeam() ) == ENEMIES )
			++(*enemies);
	}

	for( Int startNdx = 0; startNdx < MAX_PLAYER_COUNT; ++startNdx )
	{
		Coord3D there;
		if( !startPositionLoc( startNdx, &there ) )
			continue;						// the map does not have this position at all
		if( m_startOccupied[ startNdx ] )
			++(*occupied);
		else if( !m_startChecked[ startNdx ] )
			++(*unchecked);
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * Keep the picture of who is where up to date, and deduce whatever the picture already implies.
 *
 * Two things happen here.  A scout standing within sight of a start position answers that position:
 * an enemy is standing on it, or it is empty and crossed off for good.  And then the arithmetic:
 * every position known to hold an enemy accounts for one of them, so what is left is unlocated
 * enemies over unchecked positions.  1v3 on an eight-position map starts at 3/7 and rises to 3/6 and
 * 3/5 as the empty ones are crossed off.
 *
 * The moment it reaches 1 there is nothing left to find out - the remaining positions all hold an
 * enemy, and no walk can say anything a subtraction has not already said.  A two-player map is that
 * same case at its first step: one enemy, one position, 1/1, settled before a scout is even built.
 * The scouts are then free to go and look at what is standing there, which is the part that never
 * stops being worth knowing.
 */
void AIPlayer::updateStartIntel( void )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if( m_startIntelFrame != 0 && now < m_startIntelFrame + START_INTEL_RATE )
		return;
	m_startIntelFrame = now;

	if( ThePlayerList == NULL )
		return;

	const Int playerCount = ThePlayerList->getPlayerCount();

	//
	// Seed: where we are, and where our allies are, is not something anyone has to go and find out.
	// It also takes those positions out of the candidate list, which is what makes the 1v3 example
	// seven candidates rather than eight.
	//
	for( Int i = 0; i < playerCount && i < MAX_PLAYER_COUNT; ++i )
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		if( p == NULL || m_playerStartNdx[ i ] >= 0 )
			continue;
		//
		// ALLIES, not "anything that is not an enemy": the neutral and civilian players are neither,
		// and they report start index 0.  Letting them through crossed off the map's first start
		// position for every AI that was not itself standing on it - so whoever did start there was
		// permanently believed to be somewhere else.
		//
		if( p != m_player && m_player->getRelationship( p->getDefaultTeam() ) != ALLIES )
			continue;

		const Int startNdx = p->getMpStartIndex();
		if( startNdx >= 0 && startNdx < MAX_PLAYER_COUNT )
		{
			m_playerStartNdx[ i ] = startNdx;
			m_startChecked[ startNdx ] = TRUE;
		}
	}

	//
	// What the scouts can see from where they are standing.  This is the entire cost of the
	// information: a unit had to walk there.
	//
	for( Int slot = 0; slot < MAX_AI_SCOUTS; ++slot )
	{
		Object *scout = TheGameLogic->findObjectByID( m_scoutID[ slot ] );
		if( scout == NULL || scout->isEffectivelyDead() )
			continue;

		const Coord3D *at = scout->getPosition();
		Real see = scout->getVisionRange();
		if( see < 1.0f )
			see = 1.0f;

		for( Int startNdx = 0; startNdx < MAX_PLAYER_COUNT; ++startNdx )
		{
			//
			// A deduced position is checked without anyone knowing whose it is - the elimination says
			// an enemy is there, not which one.  So this keeps looking at positions already crossed
			// off until each has a name against it; only an empty one, or one already pinned to a
			// player, is finished with.
			//
			Bool pinned = FALSE;
			for( Int i = 0; i < MAX_PLAYER_COUNT; ++i )
				if( m_playerStartNdx[ i ] == startNdx )
					pinned = TRUE;
			if( m_startChecked[ startNdx ] && (pinned || !m_startOccupied[ startNdx ]) )
				continue;

			Coord3D there;
			if( !startPositionLoc( startNdx, &there ) )
				continue;

			const Real dx = there.x - at->x;
			const Real dy = there.y - at->y;
			if( dx*dx + dy*dy > see*see )
				continue;			// not close enough to say anything about it

			m_startChecked[ startNdx ] = TRUE;
			m_scoutSeenFrame[ startNdx ] = now;

			for( Int i = 0; i < playerCount && i < MAX_PLAYER_COUNT; ++i )
			{
				Player *p = ThePlayerList->getNthPlayer( i );
				if( p == NULL || p == m_player || p->getMpStartIndex() != startNdx )
					continue;
				if( m_player->getRelationship( p->getDefaultTeam() ) != ENEMIES )
					continue;

				m_startOccupied[ startNdx ] = TRUE;
				m_playerStartNdx[ i ] = startNdx;			// and now we know whose it is, not just that it is someone's
				DEBUG_LOG(("AI player %d found player %d at start position %d\n",
									 m_player->getPlayerIndex(), i, startNdx + 1));
			}
		}
	}

	// the elimination: as many enemies left to place as there are places left to put them
	Int enemies = 0, occupied = 0, unchecked = 0;
	countStartIntel( &enemies, &occupied, &unchecked );
	if( aiStartOccupiedOdds( enemies - occupied, unchecked ) < 1.0f )
		return;

	for( Int startNdx = 0; startNdx < MAX_PLAYER_COUNT; ++startNdx )
	{
		Coord3D there;
		if( m_startChecked[ startNdx ] || !startPositionLoc( startNdx, &there ) )
			continue;

		//
		// Deduced, not seen: we know an enemy is here without knowing which one, and that is enough
		// to attack it and enough to stop searching for it.  Which of them it is gets settled by the
		// first unit that arrives, above.
		//
		m_startChecked[ startNdx ] = TRUE;
		m_startOccupied[ startNdx ] = TRUE;
		DEBUG_LOG(("AI player %d deduced an enemy at start position %d without going there\n",
							 m_player->getPlayerIndex(), startNdx + 1));
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * The best address this AI has for an enemy, which is not always his real one.
 *
 * Found him: his position.  Not found him: the nearest position deduced to hold *an* enemy, and
 * failing that the nearest one nobody has looked at.  It never returns somewhere already crossed off
 * as empty, and it never returns nothing while there is anywhere left he could be - a fogged AI with
 * no coordinates at all sends every attack team to the map corner.
 *
 * Attacking the best guess is also how the guess gets corrected: the team that arrives can see.
 */
Bool AIPlayer::enemyStartGuess( Int playerNdx, Coord3D *pos )
{
	if( playerNdx >= 0 && playerNdx < MAX_PLAYER_COUNT && m_playerStartNdx[ playerNdx ] >= 0 )
		return startPositionLoc( m_playerStartNdx[ playerNdx ], pos );

	Coord3D from = m_baseCenter;
	if( !m_baseCenterSet )
		playerStartPosition( m_player->getPlayerIndex(), &from );

	Int best = -1;
	Real bestDistSqr = 0.0f;
	Bool bestOccupied = FALSE;

	for( Int startNdx = 0; startNdx < MAX_PLAYER_COUNT; ++startNdx )
	{
		Coord3D there;
		if( !startPositionLoc( startNdx, &there ) )
			continue;
		if( m_startChecked[ startNdx ] && !m_startOccupied[ startNdx ] )
			continue;			// looked, empty; he is not there

		Bool someoneElses = FALSE;
		for( Int i = 0; i < MAX_PLAYER_COUNT; ++i )
			if( i != playerNdx && m_playerStartNdx[ i ] == startNdx )
				someoneElses = TRUE;
		if( someoneElses )
			continue;

		const Real dx = there.x - from.x;
		const Real dy = there.y - from.y;
		const Real distSqr = dx*dx + dy*dy;
		const Bool occupied = m_startOccupied[ startNdx ];

		// a position known to hold someone beats a maybe, whatever the distances say
		if( best < 0 || (occupied && !bestOccupied) ||
				(occupied == bestOccupied && distSqr < bestDistSqr) )
		{
			best = startNdx;
			bestDistSqr = distSqr;
			bestOccupied = occupied;
			*pos = there;
		}
	}

	return (best >= 0);
}

//----------------------------------------------------------------------------------------------------------
/**
 * Where this scout goes next.  Two jobs, in that order.
 *
 * Searching: a start position nobody has looked at might be an enemy's, and updateStartIntel says
 * how likely that is - 3/7 in a 1v3 on an eight-position map.  Every unchecked position carries the
 * same odds, though, so the odds cancel out of the comparison and what is left is the nearest one:
 * the cheapest question to answer first.  What the odds are actually for is knowing when to stop.
 *
 * Touring: once every enemy is placed there is nothing left to search for, and the job becomes
 * keeping the picture of the bases current.  That is the stalest one per step walked -
 *
 *     score = frames since we last looked there / distance to walk there
 *
 * - recomputed at every arrival, so a scout drops a base the other scout has just refreshed and
 * takes the one nobody has been to in a while.  A picture younger than this rung's scouting interval
 * is not worth the walk at all: the scout stays where it is, keeping vision on what it can already
 * see, and is asked again in a couple of seconds.
 *
 * Somewhere crossed off as empty is never a target for either job.
 */
Bool AIPlayer::nextScoutTarget( Int slot, const Coord3D *from, Coord3D *pos )
{
	if( slot < 0 || slot >= MAX_AI_SCOUTS || from == NULL )
		return FALSE;

	const UnsignedInt now = TheGameLogic->getFrame();

	//
	// This is only ever called with an idle scout, so the last leg is over: whatever it was sent to
	// look at, it has now looked at.  (A scout that went idle because its path failed stamps a look it
	// never took - that costs one target staying stale for a cycle, not a wrong decision.)
	//
	if( m_scoutTargetFor[ slot ] >= 0 && m_scoutTargetFor[ slot ] < MAX_PLAYER_COUNT )
		m_scoutSeenFrame[ m_scoutTargetFor[ slot ] ] = now;

	const AIDifficultyProfile *profile = getSkillProfile();
	const UnsignedInt fresh = REAL_TO_INT_CEIL( profile->m_scoutIntervalSeconds * LOGICFRAMES_PER_SECOND );

	Int bestNdx = -1;
	Real bestScore = 0.0f;
	Bool bestSearching = FALSE;
	Coord3D bestPos;

	for( Int startNdx = 0; startNdx < MAX_PLAYER_COUNT; ++startNdx )
	{
		Coord3D there;
		if( !startPositionLoc( startNdx, &there ) )
			continue;

		const Bool searching = !m_startChecked[ startNdx ];
		if( !searching && !m_startOccupied[ startNdx ] )
			continue;			// crossed off, or ours: nothing there to go and see

		Bool taken = FALSE;
		for( Int other = 0; other < MAX_AI_SCOUTS; ++other )
			if( other != slot && m_scoutID[ other ] != INVALID_ID && m_scoutTargetFor[ other ] == startNdx )
				taken = TRUE;
		if( taken )
			continue;			// the other scout is already on its way there

		const Real dx = there.x - from->x;
		const Real dy = there.y - from->y;
		const Real dist = (Real)sqrt( dx*dx + dy*dy );

		const Real score = aiScoutScore( now, m_scoutSeenFrame[ startNdx ], dist, fresh );
		if( score <= 0.0f )
			continue;			// looked at recently enough that the walk would buy nothing

		// finding an enemy beats refreshing one already found, whatever the ages say
		if( bestNdx < 0 || (searching && !bestSearching) ||
				(searching == bestSearching && score > bestScore) )
		{
			bestNdx = startNdx;
			bestScore = score;
			bestSearching = searching;
			bestPos = there;
		}
	}

	if( bestNdx < 0 )
		return FALSE;			// everything worth seeing has been seen recently enough

	m_scoutTargetFor[ slot ] = bestNdx;
	*pos = bestPos;
	return TRUE;
}

//----------------------------------------------------------------------------------------------------------
/** The cheapest infantry this player can build that can walk into a building and own it.  Read off
	* the command set rather than a per-faction table, so it lands on the Ranger, the Rebel and the
	* Red Guard - and on whatever a mod gave the ability to. */
static const ThingTemplate *cheapestCapturerTemplate( Player *player )
{
	if( TheControlBar == NULL )
		return NULL;

	const ThingTemplate *best = NULL;
	Int bestCost = 0;

	for( const ThingTemplate *t = TheThingFactory->firstTemplate(); t; t = t->friend_getNextTemplate() )
	{
		if( !t->isKindOf( KINDOF_INFANTRY ) || t->isKindOf( KINDOF_DOZER ) )
			continue;
		if( !player->canBuild( t ) )
			continue;

		const Int cost = t->calcCostToBuild( player );
		if( cost <= 0 || (best != NULL && cost >= bestCost) )
			continue;

		const CommandSet *set = TheControlBar->findCommandSet( t->friend_getCommandSetString() );
		if( set == NULL )
			continue;

		Bool canCapture = FALSE;
		for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
		{
			const CommandButton *button = set->getCommandButton( i );
			if( button == NULL || button->getCommandType() != GUI_COMMAND_SPECIAL_POWER )
				continue;
			const SpecialPowerTemplate *power = button->getSpecialPowerTemplate();
			if( power && power->getSpecialPowerType() == SPECIAL_INFANTRY_CAPTURE_BUILDING )
				canCapture = TRUE;
		}
		if( !canCapture )
			continue;

		best = t;
		bestCost = cost;
	}
	return best;
}

//----------------------------------------------------------------------------------------------------------
/**
 * A unit of ours that can take a building.  Same rule as findScout: the default team only, so
 * nothing is pulled off a team that has a mission.
 */
Object *AIPlayer::findCapturer( void )
{
	Team *team = m_player->getDefaultTeam();
	if( team == NULL )
		return NULL;

	for( DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance() )
	{
		Object *obj = iter.cur();
		if( obj == NULL || obj->isEffectivelyDead() || obj->getAI() == NULL )
			continue;
		if( obj->findSpecialPowerModuleInterface( SPECIAL_INFANTRY_CAPTURE_BUILDING ) == NULL )
			continue;

		Bool busy = FALSE;
		for( Int slot = 0; slot < MAX_AI_SCOUTS; ++slot )
			if( m_scoutID[ slot ] == obj->getID() )
				busy = TRUE;			// the map still has to be looked at
		if( busy )
			continue;

		return obj;
	}
	return NULL;
}

//----------------------------------------------------------------------------------------------------------
/**
 * The closest tech building worth walking to: one to take (wantOurs FALSE) or one of ours to stand
 * on (wantOurs TRUE).
 *
 * Fog-aware like everything else here - a derrick nobody has seen yet is not a plan.  Uncaptured
 * ones belong to the neutral player, captured ones to whoever took them, so both are found by
 * walking the player list, which is the same shape getPlayerStructureBounds uses.
 */
Object *AIPlayer::nearestTechBuilding( const Coord3D *from, Bool wantOurs )
{
	if( ThePlayerList == NULL || from == NULL )
		return NULL;

	const Int myNdx = m_player->getPlayerIndex();
	Object *best = NULL;
	Real bestDistSqr = 0.0f;

	const Int playerCount = ThePlayerList->getPlayerCount();
	for( Int i = 0; i < playerCount; ++i )
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		if( p == NULL )
			continue;
		if( wantOurs != (p == m_player) )
			continue;
		if( !wantOurs && p != m_player && m_player->getRelationship( p->getDefaultTeam() ) == ALLIES )
			continue;			// it is not polite to capture an ally's buildings

		for( Player::PlayerTeamList::const_iterator it = p->getPlayerTeams()->begin();
				 it != p->getPlayerTeams()->end(); ++it )
		{
			for( DLINK_ITERATOR<Team> teamIter = (*it)->iterate_TeamInstanceList(); !teamIter.done(); teamIter.advance() )
			{
				Team *team = teamIter.cur();
				if( team == NULL )
					continue;

				for( DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance() )
				{
					Object *obj = objIter.cur();
					if( obj == NULL || obj->isEffectivelyDead() )
						continue;
					if( !obj->isKindOf( KINDOF_TECH_BUILDING ) )
						continue;
					if( !wantOurs && obj->isKindOf( KINDOF_IMMUNE_TO_CAPTURE ) )
						continue;
					if( !observerKnowsAbout( obj, myNdx ) )
						continue;			// not found yet, so not a plan

					const Coord3D *at = obj->getPosition();
					const Real dx = at->x - from->x;
					const Real dy = at->y - from->y;
					const Real distSqr = dx*dx + dy*dy;
					if( best == NULL || distSqr < bestDistSqr )
					{
						best = obj;
						bestDistSqr = distSqr;
					}
				}
			}
		}
	}
	return best;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Go and take the buildings that pay.
 *
 * An oil derrick is a supply pile that never runs out and costs one infantryman to own, and the AI
 * had no concept of one: it walked past the neutral ones and shot the enemy's.  One cheap unit now
 * does the rounds, and when there is nothing left to take it stands on what we own - an uncaptured
 * derrick is free money for whoever walks up to it next, and a captured one is free money for
 * whoever walks up to it after that.
 */
void AIPlayer::doCapture( void )
{
	if( --m_captureTimer > 0 )
		return;
	m_captureTimer = CAPTURE_CHECK_RATE;

	Object *capturer = TheGameLogic->findObjectByID( m_capturerID );
	if( capturer && (capturer->isEffectivelyDead() || capturer->getControllingPlayer() != m_player) )
		capturer = NULL;
	if( capturer == NULL )
	{
		m_capturerID = INVALID_ID;
		capturer = findCapturer();
		if( capturer )
			m_capturerID = capturer->getID();
	}

	Coord3D from = m_baseCenter;
	if( capturer )
		from = *capturer->getPosition();

	Object *target = nearestTechBuilding( &from, FALSE );

	if( capturer == NULL )
	{
		// only pay for one when there is something to spend it on
		if( target )
			queueCapturer();
		return;
	}

	AIUpdateInterface *ai = capturer->getAI();
	if( ai == NULL || !ai->isIdle() )
		return;			// still on its way

	//
	// A capture in progress reads as idle - the ability parks the unit's AI while it works - so
	// this check used to come round every five seconds and order the same capture again on the
	// same building, restarting it before it could finish.  On anything whose capture takes longer
	// than the check rate that is a loop with no way out: the derrick flashed and ticked forever
	// with a rifleman standing on it, and each restart left another copy of the ability's loop
	// sound playing.
	//
	if( capturer->testStatus( OBJECT_STATUS_IS_USING_ABILITY ) )
		return;			// already taking one

	if( target == NULL )
	{
		// nothing left to take: sit on what we own rather than wander home and let it be taken back
		Object *ours = nearestTechBuilding( &from, TRUE );
		if( ours )
			ai->aiGuardObject( ours, GUARDMODE_NORMAL, CMD_FROM_AI );
		return;
	}

	//
	// canCaptureBuilding refuses through the shroud and the capture itself has a range, so a target
	// we know about but cannot reach from here is walked to first and asked again next check.
	//
	SpecialPowerModuleInterface *mod = capturer->findSpecialPowerModuleInterface( SPECIAL_INFANTRY_CAPTURE_BUILDING );
	if( mod && TheActionManager->canCaptureBuilding( capturer, target, CMD_FROM_AI ) )
	{
		mod->doSpecialPowerAtObject( target, 0 );
		DEBUG_LOG(("AI player %d capturing a '%s'\n", m_player->getPlayerIndex(),
							 target->getTemplate()->getName().str()));
	}
	else
	{
		ai->aiMoveToObject( target, CMD_FROM_AI );
	}
}

//----------------------------------------------------------------------------------------------------------
void AIPlayer::queueCapturer( void )
{
	queueSupportUnit( cheapestCapturerTemplate( m_player ), "CAPTURE" );
}

//----------------------------------------------------------------------------------------------------------
/**
 * Keep one unit looking at the map.  Cheap by construction: one unit, ordered only when it has
 * arrived somewhere, replaced out of spare change when it dies.  Every difficulty scouts - an AI
 * that never looks reads as broken rather than as easy; what difficulty should change is how fast
 * it acts on what it finds.
 */
void AIPlayer::doScouting( void )
{
	//
	// Only a side that is actually playing goes looking.  A map's civilians are a computer player
	// too - that is how they are owned - and their cars and trucks sit on that player's default
	// team, which is exactly where findScout() looks.  So the civilian player was picking a parked
	// car out of the scenery and driving it to a start position: a stranger rolling into your base
	// in the first minute of every game, every game.  It has no faction, no build list and nothing
	// to learn from the map, so it does not scout.
	//
	if( m_player == NULL || !m_player->isPlayableSide() )
		return;

	// cheap, on its own one-second clock, and it has to run whether or not the scouts need an order:
	// this is where an arrival turns into an answer, and where the answers turn into a deduction
	updateStartIntel();

	if( --m_scoutTimer > 0 )
		return;
	m_scoutTimer = SCOUT_CHECK_RATE;

	const AIDifficultyProfile *profile = getSkillProfile();
	Int wanted = profile->m_maxScouts;
	if( wanted < 1 ) wanted = 1;
	if( wanted > MAX_AI_SCOUTS ) wanted = MAX_AI_SCOUTS;

	Bool ordered = FALSE;
	Bool short_ = FALSE;

	for( Int slot = 0; slot < wanted; ++slot )
	{
		Object *scout = TheGameLogic->findObjectByID( m_scoutID[ slot ] );
		if( scout && (scout->isEffectivelyDead() || scout->getControllingPlayer() != m_player) )
			scout = NULL;

		if( scout == NULL )
		{
			m_scoutID[ slot ] = INVALID_ID;
			scout = findScout();
			if( scout )
			{
				m_scoutID[ slot ] = scout->getID();
				// once per scout, so the log says whether this works at all without saying it twice
				DEBUG_LOG(("AI player %d scouting with a '%s'\n", m_player->getPlayerIndex(),
									 scout->getTemplate()->getName().str()));
			}
		}

		if( scout == NULL )
		{
			short_ = TRUE;
			continue;
		}

		AIUpdateInterface *ai = scout->getAI();
		if( ai == NULL || !ai->isIdle() )
			continue;			// still on its way

		Coord3D goal;
		if( nextScoutTarget( slot, scout->getPosition(), &goal ) )
		{
			ai->aiMoveToPosition( &goal, CMD_FROM_AI );
			ordered = TRUE;
		}
	}

	// one short of what this rung wants: put one on order, out of spare change
	if( short_ )
		queueScout();

	//
	// A scout that has just been sent somewhere is left alone for this rung's scouting interval.
	// This is the ladder's honest perception knob: every rung scouts, they differ in how diligently.
	// Ninety seconds between legs on Easy is a scout that wanders; twenty-five on Brutal is one
	// that keeps the map current.
	//
	if( ordered )
	{
		Int interval = REAL_TO_INT_CEIL( profile->m_scoutIntervalSeconds * LOGICFRAMES_PER_SECOND );
		if( interval > m_scoutTimer )
			m_scoutTimer = interval;
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * Queues up a dozer.
 */
void AIPlayer::queueDozer( void )
{

	if (dozerInQueue()) return;
	// Find a factory that can build a dozer.

	Bool canBuildUnits = m_player->getCanBuildUnits();
	// If we need a dozer, turn on unit building for a moment.
	m_player->setCanBuildUnits(true);
	const ThingTemplate *tTemplate = TheThingFactory->firstTemplate();
	while (tTemplate) {
		if (tTemplate->isKindOf(KINDOF_DOZER)) {
			Object *factory = findFactory(tTemplate, true);
			if (factory) {
				// we can build one.
				WorkOrder *order = newInstance(WorkOrder);
				order->m_thing = tTemplate;
				order->m_factoryID = INVALID_ID;
				order->m_numRequired = 1;
				order->m_required = true;
				order->m_isResourceGatherer = FALSE;
				// prepend to head of list
				order->m_next = NULL;
				TeamInQueue *team = newInstance(TeamInQueue);
				// Put in front of queue.
				prependTo_TeamBuildQueue(team);
				team->m_priorityBuild = true;
				team->m_workOrders = order;
				team->m_frameStarted = TheGameLogic->getFrame();
				// Stick it on the default team
				team->m_team = m_player->getDefaultTeam(); 
				AsciiString teamName = "DOZER - building one at the ";
				teamName.concat(factory->getTemplate()->getName());
				TheScriptEngine->AppendDebugMessage(teamName, false);
				m_teamDelay = 0;
				startTraining( order, team->m_priorityBuild, team->m_team->getName());
				break;
			}
		}
		tTemplate = tTemplate->friend_getNextTemplate();
	}
	// restore canbuildunits.
	m_player->setCanBuildUnits(canBuildUnits);
}


//-------------------------------------------------------------------------------------------------
/** Difficulty level for this player */
//-------------------------------------------------------------------------------------------------
enum GameDifficulty AIPlayer::getAIDifficulty(void) const
{
	return m_difficulty;
}

//-------------------------------------------------------------------------------------------------
/** The rung a seat that predates the ladder plays at - an old save, a replay, a scripted campaign
	* player.  The three that existed map to the three the lobby used to offer. */
//-------------------------------------------------------------------------------------------------
AISkillLevel AIPlayer::skillLevelForDifficulty( GameDifficulty difficulty )
{
	switch( difficulty )
	{
		case DIFFICULTY_EASY:		return AISKILL_EASY;
		case DIFFICULTY_NORMAL:	return AISKILL_MEDIUM;
		default:								return AISKILL_BRUTAL;
	}
}

//-------------------------------------------------------------------------------------------------
const AIDifficultyProfile *AIPlayer::getSkillProfile( void ) const
{
	return TheAI->getDifficultyProfile( m_skillLevel );
}


//----------------------------------------------------------------------------------------------------------
/**
 * Finds a dozer that isn't building or collecting resources.
 */
Object * AIPlayer::findDozer( const Coord3D *pos )
{
	// Add any factories placed to the build list.
	Object *obj;
	Object *dozer = NULL;
	Bool needDozer = true; 
	Object *closestDozer=NULL;
	Real closestDistSqr = 0;

	for( obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{

		Player *owner = obj->getControllingPlayer();
		if (owner==m_player) {
			// See if it's a dozer.
			if (obj->isKindOf(KINDOF_DOZER)) {

				AIUpdateInterface *ai = obj->getAIUpdateInterface();
				if (ai==NULL) {
					continue;										 
				}


				DozerAIInterface* dozerAI = ai->getDozerAIInterface();
				if (dozerAI) {
					// Since workers can be dozers, hmmm....
					SupplyTruckAIInterface* supplyTruckAI = ai->getSupplyTruckAIInterface();
					if( !dozerAI->isAnyTaskPending() && supplyTruckAI ) {
						// If it is gathering supplies, don't steal it.
						if (supplyTruckAI->isCurrentlyFerryingSupplies() || supplyTruckAI->isForcedIntoWantingState()) 
						{
							continue;
						}
					}
					if (obj->getID() == m_repairDozer) {
						continue; // don't steal the repair dozer.
					}
					needDozer = false; // dozer exists, may be busy.
					if (dozerAI->isTaskPending(DOZER_TASK_BUILD)) {
						continue; // already building.
					}
					if (!dozerAI->isAnyTaskPending()) {
						dozer = obj; // prefer an idle dozer
					}
					if (dozer==NULL) {
						dozer = obj; // but we'll take one doing stuff.
					}
					if (dozer && !dozerAI->isAnyTaskPending()) {
						// Got a good one, track closest.
						Real distSqr;
						Real dx, dy;
						dx = pos->x - dozer->getPosition()->x;
						dy = pos->y - dozer->getPosition()->y;
						distSqr = dx*dx+dy*dy;
						if (closestDozer == NULL) {
							closestDozer = dozer;
							closestDistSqr = distSqr;
						} else if (distSqr < closestDistSqr) {
							closestDozer = dozer;
							closestDistSqr = distSqr;
						}
					}
				}
			}
		}

	}
	if (needDozer) {
		queueDozer();
	}
	if (closestDozer) return closestDozer;
	return dozer;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIPlayer::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version 
	* 2: added m_teamSeconds delay.
	* 3: Added m_curWarehouseID.
	* 1: Reset back to 1 with major save file changes.
*/
// ------------------------------------------------------------------------------------------------
void AIPlayer::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 5;		// 2: scout  3: rung and role  4: scouting stamps  5: the capturer
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// team build queue count
	UnsignedShort teamBuildQueueCount = 0;
	for( DLINK_ITERATOR< TeamInQueue > teamInQueueIt = iterate_TeamBuildQueue();
			 teamInQueueIt.done() == FALSE;
			 teamInQueueIt.advance() )
		teamBuildQueueCount++;
	xfer->xferUnsignedShort( &teamBuildQueueCount );

	// team build queue data
	TeamInQueue *teamInQueue;
	if( xfer->getXferMode() == XFER_SAVE )
	{

		for( DLINK_ITERATOR< TeamInQueue > teamInQueueIt = iterate_TeamBuildQueue();
				 teamInQueueIt.done() == FALSE;
				 teamInQueueIt.advance() )
		{
		
			// get element data
			teamInQueue = teamInQueueIt.cur();

			// xfer it
			xfer->xferSnapshot( teamInQueue );

		}  // end for, iterate team build queue

	}  // end if, save
	else
	{

		// sanity, the list must be empty
		if( getFirstItemIn_TeamBuildQueue() != NULL )
		{
		
			DEBUG_CRASH(( "AIPlayer::xfer - TeamBuildQueue head is not NULL, you should delete it or something before loading a new list\n" ));
			throw SC_INVALID_DATA;

		}  // end if

		// ready all data
		for( UnsignedShort i = 0; i < teamBuildQueueCount; ++i )
		{

			// allocate new team in queue instance
			teamInQueue = newInstance(TeamInQueue);

			// attach to end of list
			prependTo_TeamBuildQueue( teamInQueue );

			// xfer data
			xfer->xferSnapshot( teamInQueue );

		}  // end for, i

		// the list was loaded in reverse order, reverse the list so it's in the same order as before
		reverse_TeamBuildQueue();

	}  // end else, load

	// team ready queue count
	UnsignedShort teamReadyQueueCount = 0;
	for( DLINK_ITERATOR< TeamInQueue > teamReadyQueueIt = iterate_TeamReadyQueue();
			 teamReadyQueueIt.done() == FALSE;
			 teamReadyQueueIt.advance() )
		teamReadyQueueCount++;
	xfer->xferUnsignedShort( &teamReadyQueueCount );

	// team Ready queue data
	TeamInQueue *teamReadyQueue;
	if( xfer->getXferMode() == XFER_SAVE )
	{

		for( DLINK_ITERATOR< TeamInQueue > teamReadyQueueIt = iterate_TeamReadyQueue();
				 teamReadyQueueIt.done() == FALSE;
				 teamReadyQueueIt.advance() )
		{
		
			// get element
			teamReadyQueue = teamReadyQueueIt.cur();
			
			// xfer data
			xfer->xferSnapshot( teamReadyQueue );

		}  // end for, iterate team ready queue

	}  // end if, save
	else
	{

		// sanity, the list must be empty
		if( getFirstItemIn_TeamReadyQueue() != NULL )
		{
		
			DEBUG_CRASH(( "AIPlayer::xfer - TeamReadyQueue head is not NULL, you should delete it or something before loading a new list\n" ));
			throw SC_INVALID_DATA;

		}  // end if

		// read all data
		for( UnsignedShort i = 0; i < teamReadyQueueCount; ++i )
		{

			// allocate new team in queue instance
			teamInQueue = newInstance(TeamInQueue);

			// attach to end of list
			prependTo_TeamReadyQueue( teamInQueue );

			// xfer data
			xfer->xferSnapshot( teamInQueue );

		}  // end for, i

		// reverse the list since it was loaded in reverse order due to the prepend
		reverse_TeamReadyQueue();

	}  // end else, load

	// xfer player index ... this is really just for sanity
	PlayerIndex playerIndex = m_player->getPlayerIndex();
	xfer->xferUser( &playerIndex, sizeof( PlayerIndex ) );
	if( playerIndex != m_player->getPlayerIndex() )
	{

		DEBUG_CRASH(( "AIPlayer::xfer - player index mismatch\n" ));
		throw SC_INVALID_DATA;

	}  // end if

	// xfer the rest of the ai player data (it's pretty straight forward)
	xfer->xferBool( &m_readyToBuildTeam );
	xfer->xferBool( &m_readyToBuildStructure );
	xfer->xferInt( &m_teamTimer );
	xfer->xferInt( &m_structureTimer );

	xfer->xferInt( &m_buildDelay );
	xfer->xferInt( &m_teamDelay );

	xfer->xferInt(&m_teamSeconds);
 	xfer->xferObjectID(&m_curWarehouseID);

	xfer->xferInt( &m_frameLastBuildingBuilt );

	xfer->xferUser( &m_difficulty, sizeof( GameDifficulty ) );
	xfer->xferInt( &m_skillsetSelector );

	xfer->xferCoord3D( &m_baseCenter );
	xfer->xferBool( &m_baseCenterSet );
	xfer->xferReal( &m_baseRadius );

	// the scout, so a loaded game does not go blind or start a second one
	if( version >= 2 )
	{
		xfer->xferInt( &m_scoutTimer );
		for( Int scoutSlot = 0; scoutSlot < MAX_AI_SCOUTS; ++scoutSlot )
		{
			xfer->xferObjectID( &m_scoutID[ scoutSlot ] );
			xfer->xferInt( &m_scoutTargetFor[ scoutSlot ] );
		}
	}
	// what has been looked at and what was found there, so a loaded game does not search a map it
	// has already searched - or forget an enemy it had located
	if( version >= 4 )
	{
		for( Int startNdx = 0; startNdx < MAX_PLAYER_COUNT; ++startNdx )
		{
			xfer->xferUnsignedInt( &m_scoutSeenFrame[ startNdx ] );
			xfer->xferBool( &m_startChecked[ startNdx ] );
			xfer->xferBool( &m_startOccupied[ startNdx ] );
			xfer->xferInt( &m_playerStartNdx[ startNdx ] );
		}
	}
	if( version >= 3 )
	{
		xfer->xferInt( &m_retreatTimer );
		xfer->xferInt( &m_expandTimer );
	}
	// the capturer, so a loaded game does not build a second one
	if( version >= 5 )
	{
		xfer->xferObjectID( &m_capturerID );
		xfer->xferInt( &m_captureTimer );
	}

	// the ladder rung and the role, which are rolled once and must come back the same way
	if( version >= 3 )
	{
		xfer->xferUser( &m_skillLevel, sizeof( AISkillLevel ) );
		xfer->xferUser( &m_role, sizeof( AIRole ) );
	}

	xfer->xferUser( m_structuresToRepair, sizeof( ObjectID ) * MAX_STRUCTURES_TO_REPAIR );
	xfer->xferObjectID( &m_repairDozer );
	xfer->xferInt( &m_structuresInQueue );
	xfer->xferBool( &m_dozerQueuedForRepair );
	xfer->xferBool( &m_dozerIsRepairing );
	xfer->xferInt( &m_bridgeTimer );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIPlayer::loadPostProcess( void )
{

}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
TeamInQueue::~TeamInQueue()
{
	WorkOrder *order, *next;

	for( order = m_workOrders; order; order = next )
	{
		next = order->m_next;
		order->deleteInstance();
	}
	// If we have a team, activate it.  If it is empty, Team.cpp will remove empty active teams.
	if (m_team) m_team->setActive();
	m_workOrders = NULL;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool TeamInQueue::isAllBuilt()
{
	WorkOrder *order;

	Bool stillBuilding = false;
	for( order = m_workOrders; order; order = order->m_next )
	{
		if (order->m_numRequired>order->m_numCompleted) 
		{
			stillBuilding = true;
		}
	}
	return !stillBuilding;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool TeamInQueue::isBuildTimeExpired()
{
	if (m_team->getPrototype()->getTemplateInfo()->m_initialIdleFrames<1) {
		return false; // Unlimited time.
	}
	if (TheGameLogic->getFrame() > m_frameStarted + m_team->getPrototype()->getTemplateInfo()->m_initialIdleFrames) {
		return true;
	}
	return false;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool TeamInQueue::isMinimumBuilt()
{
	WorkOrder *order;

	for( order = m_workOrders; order; order = order->m_next )
	{
		Int count = order->m_numCompleted;
		if (order->m_factoryID != INVALID_ID) {
			count++; // we have one building.
		}
		if (order->m_numRequired>count) 
		{
			if (order->m_required) {
				return false; // required units not built.
			}
		}
	}
	return true;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool TeamInQueue::includesADozer()
{
	WorkOrder *order;

	for( order = m_workOrders; order; order = order->m_next )
	{
		// GLA dozers (workers) are also resource gatherers, so make sure it isn't a gatherer. jba.
		if (order->m_thing->isKindOf(KINDOF_DOZER) && !order->m_isResourceGatherer) {
			return true;
		}
	}
	return false;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool TeamInQueue::areBuildsComplete()
{
	WorkOrder *order;

	for( order = m_workOrders; order; order = order->m_next )
	{
		if (order->m_factoryID != INVALID_ID) {
			return false; // we have one building.
		}
	}
	return true;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void TeamInQueue::disband()
{
	Team *newTeam = m_team->getPrototype()->getControllingPlayer()->getDefaultTeam();
	AsciiString teamName = m_team->getPrototype()->getName();
	teamName.concat(" - team disbanded, build time expired.");
	TheScriptEngine->AppendDebugMessage(teamName, false);
	if (m_team != newTeam) {
		m_team->transferUnitsTo(newTeam);
		if (!m_team->getPrototype()->getIsSingleton()) {
			m_team->deleteInstance();
		}
		m_team = NULL;
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void TeamInQueue::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method 
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void TeamInQueue::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;;
	xfer->xferVersion( &version, currentVersion );

	// xfer work order count
	UnsignedShort workOrderCount = 0;
	WorkOrder *workOrder;
	for( workOrder = m_workOrders; workOrder; workOrder = workOrder->m_next )
		workOrderCount++;
	xfer->xferUnsignedShort( &workOrderCount );

	// xfer work orders
	if( xfer->getXferMode() == XFER_SAVE )
	{

		// xfer each work order
		for( workOrder = m_workOrders; workOrder; workOrder = workOrder->m_next )
		{

			// xfer work order data
			xfer->xferSnapshot( workOrder );

		}  // end for

	}  // end if, save
	else
	{

		// sanity
		if( m_workOrders != NULL )
		{

			DEBUG_CRASH(( "TeamInQueue::xfer - m_workOrders should be NULL but isn't.  Perhaps you should blow it away before loading\n" ));
			throw SC_INVALID_DATA;

		}  // end if

		// load all work orders
		for( UnsignedShort i = 0; i < workOrderCount; ++i )
		{

			// allocate new work order
			workOrder = newInstance(WorkOrder);

			// attach to list at the end
			workOrder->m_next = NULL;
			if( m_workOrders == NULL )
				m_workOrders = workOrder;
			else
			{
				WorkOrder *last = m_workOrders;

				while( last->m_next != NULL )
					last = last->m_next;

				last->m_next = workOrder;

			}  // end else

			// load work order data
			xfer->xferSnapshot( workOrder );

		}  // end for, i

	}  // end else, load

	// xfer the rest of the team in queue data
	xfer->xferBool( &m_priorityBuild );
	TeamID teamID = m_team ? m_team->getID() : TEAM_ID_INVALID;
	xfer->xferUser( &teamID, sizeof( TeamID ) );
	if( xfer->getXferMode() == XFER_LOAD )
		m_team = TheTeamFactory->findTeamByID( teamID );
	xfer->xferInt( &m_frameStarted );
	xfer->xferBool( &m_sentToStartLocation );
	xfer->xferBool( &m_stopQueueing );
	xfer->xferBool( &m_reinforcement );
	xfer->xferObjectID( &m_reinforcementID );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void TeamInQueue::loadPostProcess( void )
{

}  // end loadPostProcess

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
WorkOrder::~WorkOrder()
{

}  // end WorkOrder

// ------------------------------------------------------------------------------------------------
/** Verify factoryID still refers to an active object */
// ------------------------------------------------------------------------------------------------
void WorkOrder::validateFactory( Player *thisPlayer )
{

	if (m_factoryID == INVALID_ID) 
		return;
	Object *factory = TheGameLogic->findObjectByID( m_factoryID );
	if ( factory == NULL) {
		m_factoryID = INVALID_ID;
		return;
	}
	if (factory->getControllingPlayer()!=thisPlayer) {
		m_factoryID = INVALID_ID;
	}

}  // end validateFactory

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void WorkOrder::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void WorkOrder::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;		// 2: the scout flag
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// thing template
	AsciiString thingTemplateName = m_thing ? m_thing->getName() : AsciiString::TheEmptyString;
	xfer->xferAsciiString( &thingTemplateName );
	if( xfer->getXferMode() == XFER_LOAD )
		m_thing = TheThingFactory->findTemplate( thingTemplateName );

	// factory id
	xfer->xferObjectID( &m_factoryID );

	// num completed
	xfer->xferInt( &m_numCompleted );

	// num required
	xfer->xferInt( &m_numRequired );

	// is required
	xfer->xferBool( &m_required );

	// is resource gatherer
	xfer->xferBool( &m_isResourceGatherer );

	// is the map-watching scout
	if( version >= 2 )
		xfer->xferBool( &m_isScout );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void WorkOrder::loadPostProcess( void )
{

}  // end loadPostProcess


//----------------------------------------------------------------------------------------------------------
/**
 * Get the bounds for a player's structure.
 */
void AIPlayer::getPlayerStructureBounds( Region2D *bounds, Int playerNdx, Bool conservative, Int observerNdx )
{
	Player::PlayerTeamList::const_iterator it;
	Bool firstObject = true;
	Bool firstStructure = true;
	bounds->hi.x = bounds->lo.x = bounds->hi.y = bounds->lo.y = 0;
	Region2D objBounds;
	objBounds.hi.x = objBounds.lo.x = objBounds.hi.y = objBounds.lo.y = 0;

	Player* pPlayer = ThePlayerList->getNthPlayer(playerNdx);
	if (pPlayer == NULL) 
		return;
	for (it = pPlayer->getPlayerTeams()->begin(); it != pPlayer->getPlayerTeams()->end(); ++it) 
	{
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance()) 
		{
			Team *team = iter.cur();
			if (!team) 
				continue;
			for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance()) 
			{
				Object *pObj = iter.cur();
				if (!pObj) 
					continue;
				if (!observerKnowsAbout(pObj, observerNdx))
					continue;			// this player has not found it yet
				const Bool isStructure = pObj->isKindOf(KINDOF_STRUCTURE);

				if( isStructure && conservative && pObj->isKindOf( KINDOF_CONSERVATIVE_BUILDING ) )
				{
					//Kris - Aug 14, 2003
					//Conservative buildings (captured tech buildings, sneak attack buildings are rejected).
					//This was added so base boundaries aren't potentially most of the map. Because sneak
					//attack uses an inverse cost calculation by avoiding defended areas, we need to keep
					//things close to the enemy base.
					continue;
				}

				{
					Coord3D pos = *pObj->getPosition();

					//
					// objBounds spans EVERY object, so it can stand in for a player who owns no
					// structures. It used to be updated only inside the structure test, which made
					// it a copy of the structure bounds and left the fallback below meaningless.
					//
					if (firstObject) {
						objBounds.lo.x = objBounds.hi.x = pos.x;
						objBounds.lo.y = objBounds.hi.y = pos.y;
						firstObject = false;
					}	
					else 
					{
						if (objBounds.lo.x>pos.x) objBounds.lo.x = pos.x;
						if (objBounds.lo.y>pos.y) objBounds.lo.y = pos.y;
						if (objBounds.hi.x<pos.x) objBounds.hi.x = pos.x;
						if (objBounds.hi.y<pos.y) objBounds.hi.y = pos.y;
					}

					if (!isStructure)
						continue;

					if (firstStructure) 
					{
						bounds->lo.x = bounds->hi.x = pos.x;
						bounds->lo.y = bounds->hi.y = pos.y;
						firstStructure = false;
					}	
					else 
					{
						if (bounds->lo.x>pos.x) bounds->lo.x = pos.x;
						if (bounds->lo.y>pos.y) bounds->lo.y = pos.y;
						if (bounds->hi.x<pos.x) bounds->hi.x = pos.x;
						if (bounds->hi.y<pos.y) bounds->hi.y = pos.y;
					}
				}
			}
		}
	}
	if (firstStructure && firstObject && observerNdx >= 0) {
		//
		// The observer has seen nothing of this player at all - the opening minutes, before anyone
		// has scouted.  Aim at the best address we have for him rather than at the (0,0) corner this
		// function would otherwise report: his real position if a scout has found him, and until then
		// whichever start position the elimination says is likeliest to be his.
		//
		Coord3D start;
		if (enemyStartGuess(playerNdx, &start)) {
			bounds->lo.x = bounds->hi.x = start.x;
			bounds->lo.y = bounds->hi.y = start.y;
		}
		return;
	}
	if (firstStructure && !firstObject) {
		// Player had no structures, so use unit bounds.
		// (the test used to be !firstStructure - i.e. it only "fell back" when structures HAD been
		// found, which was a no-op, and a structureless player was left with the (0,0,0,0) bounds
		// this function starts with. acquireEnemy, findSupplyCenter and guardSupplyCenter all then
		// aimed at the map corner.)
		*bounds = objBounds;
	}
}
