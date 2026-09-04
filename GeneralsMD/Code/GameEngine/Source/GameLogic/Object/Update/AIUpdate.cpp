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

// AIUpdate.cpp //
// Implementation of generic AI mechanisms
// Author: Michael S. Booth, 2001-2002
// Subsequently : John Ahlquist 2002 and a cast of thousands.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#define DEFINE_LOCOMOTORSET_NAMES					// for TheLocomotorSetNames[]
#define DEFINE_AUTOACQUIRE_NAMES

#include "Common/ActionManager.h"
#include "Common/GameState.h"
#include "Common/CRCDebug.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/RandomValue.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/PerfTimer.h"
#include "Common/UnitTimings.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"
#include "Lib/Trig.h"

#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/InGameUI.h"  // useful for printing quick debug strings when we need to

#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/CrowdModel.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/ProneUpdate.h"
#include "GameLogic/Module/DeliverPayloadAIUpdate.h"
#include "GameLogic/Module/HackInternetAIUpdate.h"
#include "GameLogic/Module/HordeUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/TurretAI.h"
#include "GameLogic/Weapon.h"
#include "Common/Radar.h"									// For TheRadar

#define SLEEPY_AI

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-------------------------------------------------------------------------------------------------
AIUpdateModuleData::AIUpdateModuleData()
{
	//m_locomotorTemplates	-- nothing to do
	for (int i = 0; i < MAX_TURRETS; i++)
		m_turretData[i] = NULL;
	m_autoAcquireEnemiesWhenIdle = 0;
	m_moodAttackCheckRate = LOGICFRAMES_PER_SECOND * 2;
#ifdef ALLOW_SURRENDER
	m_surrenderDuration = LOGICFRAMES_PER_SECOND * 120;
#endif

  m_forbidPlayerCommands = FALSE;
	m_turretsLinked = FALSE;
}

//-------------------------------------------------------------------------------------------------
AIUpdateModuleData::~AIUpdateModuleData()
{
	for (int i = 0; i < MAX_TURRETS; i++)
	{
		if (m_turretData[i])
		{
			TurretAIData* td = const_cast<TurretAIData*>(m_turretData[i]);
			if (td)
				td->deleteInstance();
		}
	}
}

//-------------------------------------------------------------------------------------------------
const LocomotorTemplateVector* AIUpdateModuleData::findLocomotorTemplateVector(LocomotorSetType t) const
{
	if (m_locomotorTemplates.empty())
		return NULL;

  LocomotorTemplateMap::const_iterator it = m_locomotorTemplates.find(t);
  if (it == m_locomotorTemplates.end()) 
	{
		return NULL;
	}
	else
	{
		return &(*it).second;
	}
}

//
// -tracemove [id]: one line a frame for one unit.  A jam is an argument between the speed the unit
// wants, the ceiling its last collision put on it, the decaying bump limit and the frames it has
// spent blocked, and from outside the object none of those is visible - the run-level counters
// can say 40000 blocked frames and still not say which of them zeroed the tank.  The columns are
// in the order the code applies them, so the first one that goes to zero is the culprit.
//
// With no id the trace latches onto the first unit that gets blocked and follows it for the rest of
// the run: in a batch nobody knows an object id in advance, and the interesting unit is by
// definition one that is stuck.  Output only - nothing here is read back by any logic.
//
static ObjectID theTracedObjectID = INVALID_ID;

void AIUpdate_resetMoveTrace( void )
{
	theTracedObjectID = INVALID_ID;
}

static void AIUpdate_traceMove( const Object *obj, Bool blocked, Int blockedFrames,
																Real desiredSpeed, Real maxSpeed, Real maxBlockedSpeed,
																Real bumpSpeedLimit, Bool waitingForPath, Bool hasPath,
																Bool stuck )
{
	const Int wanted = TheGlobalData ? TheGlobalData->m_traceMoveID : 0;
	if (wanted == 0)
		return;

	if (wanted > 0)
	{
		if (obj->getID() != (ObjectID)wanted)
			return;
	}
	else
	{
		// no id given: the first unit to get blocked is the one we follow from then on
		if (theTracedObjectID == INVALID_ID)
		{
			if (!blocked)
				return;
			theTracedObjectID = obj->getID();
		}
		else if (obj->getID() != theTracedObjectID)
		{
			return;
		}
	}

	const Coord3D *pos = obj->getPosition();
	const PhysicsBehavior *physics = obj->getPhysics();
	const Real actualSpeed = physics ? physics->getVelocityMagnitude() : 0.0f;
	DEBUG_LOG(("MOVETRACE %d,%d,%.2f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d\n",
		TheGameLogic->getFrame(), (Int)obj->getID(), pos->x, pos->y,
		actualSpeed, desiredSpeed, maxSpeed, maxBlockedSpeed, bumpSpeedLimit,
		blocked ? 1 : 0, blockedFrames, waitingForPath ? 1 : 0,
		hasPath ? 1 : 0));
	if (stuck)
	{
		DEBUG_LOG(("MOVETRACE %d,%d,stuck\n", TheGameLogic->getFrame(), (Int)obj->getID()));
	}
}

//-------------------------------------------------------------------------------------------------
/*static*/ void AIUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
  ModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] = 
	{
		{ "Turret",											AIUpdateModuleData::parseTurret,	NULL, offsetof(AIUpdateModuleData, m_turretData[0]) },
		{ "AltTurret",									AIUpdateModuleData::parseTurret,	NULL, offsetof(AIUpdateModuleData, m_turretData[1]) },
		{ "AutoAcquireEnemiesWhenIdle", INI::parseBitString32, TheAutoAcquireEnemiesNames, offsetof(AIUpdateModuleData, m_autoAcquireEnemiesWhenIdle) },
		{ "MoodAttackCheckRate",				INI::parseDurationUnsignedInt,		NULL, offsetof(AIUpdateModuleData, m_moodAttackCheckRate) },
#ifdef ALLOW_SURRENDER
		{ "SurrenderDuration",					INI::parseDurationUnsignedInt,		NULL, offsetof(AIUpdateModuleData, m_surrenderDuration) },
#endif
    { "ForbidPlayerCommands",				INI::parseBool,										NULL, offsetof(AIUpdateModuleData, m_forbidPlayerCommands) },
    { "TurretsLinked",							INI::parseBool,										NULL, offsetof( AIUpdateModuleData, m_turretsLinked ) },
		{ 0, 0, 0, 0 }
	};
  p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
/*static*/ void AIUpdateModuleData::parseTurret(INI* ini, void *instance, void * store, const void* /*userData*/)
{
	if (*(TurretAIData**)store)
	{
		DEBUG_CRASH(("Only one turret to a customer, for now"));
		throw INI_INVALID_DATA;
	}

	TurretAIData* td = newInstance(TurretAIData);
	ini->initFromINIMultiProc(td, td->buildFieldParse);
	*(TurretAIData**)store = td;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void AIUpdateModuleData::parseLocomotorSet(INI* ini, void *instance, void * /*store*/, const void* /*userData*/)
{
	ThingTemplate *tt = (ThingTemplate *)instance;
	AIUpdateModuleData *self = tt->friend_getAIModuleInfo();
	if (!self) 
	{
		DEBUG_CRASH( ("Attempted to specify a locomotor for object %s without an AIUpdate block.", tt->getName().str() ) );
		throw INI_INVALID_DATA;
	}

	LocomotorSetType set = (LocomotorSetType)INI::scanIndexList(ini->getNextToken(), TheLocomotorSetNames);
	if (!self->m_locomotorTemplates[set].empty())
	{
		if (ini->getLoadType() != INI_LOAD_CREATE_OVERRIDES)
		{
			DEBUG_CRASH(("re-specifying a LocomotorSet is no longer allowed\n"));
			throw INI_INVALID_DATA;
		}
	}

	self->m_locomotorTemplates[set].clear();
	for (const char* locoName = ini->getNextToken(); locoName; locoName = ini->getNextTokenOrNull())
	{
		if (!*locoName || !stricmp(locoName, "None"))
			continue;

		NameKeyType locoKey = NAMEKEY(locoName);
		const LocomotorTemplate* lt = TheLocomotorStore->findLocomotorTemplate(locoKey);
		if (!lt)
		{
			DEBUG_CRASH(("Locomotor %s not found!\n",locoName));
			throw INI_INVALID_DATA;
		}
		self->m_locomotorTemplates[set].push_back(lt);
	}
}

//-------------------------------------------------------------------------------------------------
// subclasses may want to override this, to use a subclass of AIStateMachine.
AIStateMachine* AIUpdateInterface::makeStateMachine()
{
	return newInstance(AIStateMachine)( getObject(), "AIUpdateInterfaceMachine");
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AIUpdateInterface::AIUpdateInterface( Thing *thing, const ModuleData* moduleData ) : 
	UpdateModule( thing, moduleData )
{
	int i;

	m_priorWaypointID = 0xfacade;
	m_currentWaypointID	= 0xfacade;
	m_stateMachine = NULL;
	m_nextEnemyScanTime = 0;
	m_currentVictimID = INVALID_ID;
	m_desiredSpeed = FAST_AS_POSSIBLE;
	m_lastCommandSource = CMD_FROM_AI;
	m_guardMode = GUARDMODE_NORMAL;
	m_guardTargetType[0] = m_guardTargetType[1] = GUARDTARGET_NONE;
	m_locationToGuard.zero();
	m_objectToGuard = INVALID_ID;
	m_areaToGuard = NULL;
	m_attackInfo = NULL;
	m_waypointCount = 0;
	m_waypointIndex = 0;
	m_completedWaypoint = NULL;
	m_path = NULL;
	m_requestedVictimID = INVALID_ID;
	m_requestedDestination.zero();
	m_requestedDestination2.zero();
	m_pathTimestamp = 0;
	m_ignoreObstacleID = INVALID_ID;
	m_pathExtraDistance = 0;
	m_pathfindGoalCell.x = m_pathfindGoalCell.y = -1;
	m_pathfindCurCell.x = m_pathfindCurCell.y = -1;
	m_blockedFrames = 0;
	m_curMaxBlockedSpeed = 0;
	m_laneFraction = 0.5f;
	m_laneFractionValid = FALSE;
	m_laneHoldFrame = 0;
	m_pendingLane = 0.5f;
	m_hasPendingLane = FALSE;
	m_corridor = NULL;
	m_crowdLat = 0.0f;
	m_pendingCrowdLat = 0.0f;
	m_hasPendingCrowdLat = FALSE;
	m_crowdLatValid = FALSE;
	m_crowdHoldFrame = 0;
	m_crowdSample = 0;
	m_crowdQueued = 0;
	m_crowdSide = 1;
	m_crowdSepSmooth = 0.0f;
	m_crowdAim = 0.0f;
	m_crowdAimValid = FALSE;
	m_crowdStuck = 0;
	m_crowdLastPos.zero();
	m_crowdEscape.zero();
	m_crowdEscapeUntil = 0;
	m_crowdEscapeCool = 0;
	m_bumpSpeedLimit = FAST_AS_POSSIBLE;
	m_ignoreCollisionsUntil = 0;
	m_queueForPathFrame = 0;
	m_finalPosition.zero();
	m_repulsor1 = INVALID_ID;
	m_repulsor2 = INVALID_ID;
	m_nextGoalPathIndex = -1;
	m_moveOutOfWay1 = INVALID_ID;
	m_moveOutOfWay2 = INVALID_ID;
	m_exitProductionRallyPoint.zero();
	m_hasExitProductionRallyPoint = FALSE;
	m_locomotorSet.clear();
	m_curLocomotor = NULL;
	m_curLocomotorSet = LOCOMOTORSET_INVALID;
	m_locomotorGoalType = NONE;
	m_locomotorGoalData.zero();
	for (i = 0; i < MAX_TURRETS; i++)
		m_turretAI[i] = NULL;
	m_turretSyncFlag = TURRET_INVALID;
	m_attitude = AI_NORMAL;
	m_nextMoodCheckTime = 0;
#ifdef ALLOW_DEMORALIZE
	m_demoralizedFramesLeft = 0;
#endif
#ifdef ALLOW_SURRENDER
	m_surrenderedFramesLeft = 0;
	m_surrenderedPlayerIndex = -1;
#endif
	m_crateCreated = INVALID_ID;
	m_tmpInt = 0;
	m_doFinalPosition = FALSE;
	m_waitingForPath = FALSE;
	m_isAttackPath = FALSE;
	m_isFinalGoal = FALSE;
	m_isApproachPath = FALSE;
	m_isSafePath = FALSE;
	m_movementComplete = FALSE;
	m_isMoving = FALSE;
	m_isBlocked = FALSE;
	m_isBlockedAndStuck = FALSE;
	m_upgradedLocomotors = FALSE;
	m_canPathThroughUnits = FALSE;
	m_randomlyOffsetMoodCheck = FALSE;
	m_isAiDead = FALSE;
	m_isRecruitable = TRUE; // Things default to being recruitable.
	m_executingWaypointQueue = FALSE;
	m_retryPath = FALSE;
	m_isInUpdate = FALSE;
	m_fixLocoInPostProcess = FALSE;

	// ---------------------------------------------

	for (i = 0; i < MAX_TURRETS; i++)
	{
		if (getAIUpdateModuleData()->m_turretData[i])
		{
			m_turretAI[i] = newInstance(TurretAI)(getObject(), getAIUpdateModuleData()->m_turretData[i], (WhichTurretType)i);
		}
	}

	chooseLocomotorSet(LOCOMOTORSET_NORMAL);

#ifdef SLEEPY_AI
	setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
#endif
}

#ifdef ALLOW_SURRENDER
//=============================================================================
// Object::setSurrendered, and related methods ================================
//=============================================================================
void AIUpdateInterface::setSurrendered( const Object *objWeSurrenderedTo, Bool surrendered )
{
	if (surrendered)
	{
		Bool wasSurrendered = isSurrendered();

		const AIUpdateModuleData* d = getAIUpdateModuleData();

		if (m_surrenderedFramesLeft < d->m_surrenderDuration)
			m_surrenderedFramesLeft = d->m_surrenderDuration;
		
		const Player* playerWeSurrenderedTo = objWeSurrenderedTo ? objWeSurrenderedTo->getControllingPlayer() : NULL;
		m_surrenderedPlayerIndex = playerWeSurrenderedTo ? playerWeSurrenderedTo->getPlayerIndex() : -1;

		if (!wasSurrendered)
		{
			//aiIdle(CMD_FROM_AI);
			// srj sez: calling aiIdle() won't work, since we are probably "effectivelyDead"...
			// meaning we won't respong to aiDoCommand! so go straight to the metal here:
			getStateMachine()->clear();
			getStateMachine()->setState( AI_IDLE );
			setLastCommandSource(CMD_FROM_AI);

			// Play our sound surrendered
			AudioEventRTS surrenderSound = *getObject()->getTemplate()->getVoiceSurrender();
			surrenderSound.setObjectID(getObject()->getID());
			TheAudio->addAudioEvent(&surrenderSound);		
		}
	}
	else
	{
		// GS During the act of surrendering, we dipped to 0 and then were manually set to have hit points.  
		// That made us alive but marked as Dead.  Gotta undo that.

		getObject()->setEffectivelyDead( FALSE );

		m_surrenderedFramesLeft = 0;
		m_surrenderedPlayerIndex = -1;
	}

}
#endif

//=============================================================================
void AIUpdateInterface::setGoalPositionClipped(const Coord3D* in, CommandSourceType cmdSource)
{
	if (in)
	{
		Coord3D tmp  = *in;
		if (cmdSource == CMD_FROM_PLAYER)
		{
			Real fudge = TheGlobalData->m_partitionCellSize * 0.5f;
			if (getObject()->isKindOf(KINDOF_AIRCRAFT) && getObject()->isSignificantlyAboveTerrain() && m_curLocomotor != NULL)
			{
				// aircraft must stay further away from the map edges, to prevent getting "lost"
				fudge = max(fudge, m_curLocomotor->getPreferredHeight());
			}
			Region3D mapRegion;
			TheTerrainLogic->getExtent( &mapRegion );
			if (tmp.x < mapRegion.lo.x + fudge)
			{
				tmp.x = mapRegion.lo.x + fudge;
			}
			if (tmp.x > mapRegion.hi.x - fudge)
			{
				tmp.x = mapRegion.hi.x - fudge;
			}
			if (tmp.y < mapRegion.lo.y + fudge)
			{
				tmp.y = mapRegion.lo.y + fudge;
			}
			if (tmp.y > mapRegion.hi.y - fudge)
			{
				tmp.y = mapRegion.hi.y - fudge;
			}
		}
		getStateMachine()->setGoalPosition(&tmp);
	}
	else
	{
		getStateMachine()->setGoalPosition(NULL);
	}
}

/* Called by the pathfinder when it processes the pathfind queue.  Basically, it's our turn
to call use the PathfindServicesInterface to do a pathfind operation.  This shouldn't be called
(and in fact is very hard to do because PathfindServicesInterace is private to the pathfinder)
except by the pathfinder during pathfind queue processing.  jba */
//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::doPathfind( PathfindServicesInterface *pathfinder )
{
	if (!m_waitingForPath) {
		return;
	}
	//CRCDEBUG_LOG(("AIUpdateInterface::doPathfind() for object %d\n", getObject()->getID()));
	m_waitingForPath = FALSE;	 
	if (m_isSafePath) {
		destroyPath();
		Coord3D pos1, pos2;
		pos1.set(-1000,-1000,0);
		Object *repulsor = TheGameLogic->findObjectByID(m_repulsor1);
		if (repulsor) {
			pos1 = *repulsor->getPosition();
		}
		pos2 = pos1;
		repulsor = TheGameLogic->findObjectByID(m_repulsor2);
		if (repulsor) {
			pos2 = *repulsor->getPosition();
		}
		m_path = pathfinder->findSafePath(getObject(), m_locomotorSet, 
			getObject()->getPosition(), 
			&pos1, 	&pos2, 
			getObject()->getVisionRange() + TheAI->getAiData()->m_repulsedDistance);
		return;
	}
	if (m_isApproachPath & !isDoingGroundMovement()) {
		m_isApproachPath = false;
	}
	if (m_isApproachPath) {
		destroyPath();
		m_path = pathfinder->findClosestPath(getObject(), m_locomotorSet, getObject()->getPosition(), 
			&m_requestedDestination, m_isBlockedAndStuck, 0.2f, FALSE );
		if (isDoingGroundMovement() && getPath()) {
			TheAI->pathfinder()->updateGoal(getObject(), getPath()->getLastNode()->getPosition(),
				getPath()->getLastNode()->getLayer());
		}
		return;
	}
	if (m_isAttackPath) {
		Object *victim = NULL;
		if (m_requestedVictimID != INVALID_ID) { 
			victim = TheGameLogic->findObjectByID(m_requestedVictimID);
		}
		if (computeAttackPath(pathfinder, victim, &m_requestedDestination))	{	
			if (getPath()) {
				TheAI->pathfinder()->updateGoal(getObject(), getPath()->getLastNode()->getPosition(),
					getPath()->getLastNode()->getLayer());
			}
			//CRCDEBUG_LOG(("AIUpdateInterface::doPathfind() - m_isAttackPath = TRUE after computeAttackPath\n"));
			m_isAttackPath = TRUE; 
			return;
		}
		//CRCDEBUG_LOG(("AIUpdateInterface::doPathfind() - m_isAttackPath = FALSE after computeAttackPath()\n"));
		m_isAttackPath = FALSE;
		if (victim) {
			m_requestedDestination = *victim->getPosition();
			/* find a pathable destination near the victim.*/
			TheAI->pathfinder()->adjustToPossibleDestination(getObject(), getLocomotorSet(), &m_requestedDestination);
			ignoreObstacle(victim); 
		}
	} 
	computePath(pathfinder, &m_requestedDestination);
	if (m_isFinalGoal && isDoingGroundMovement() && getPath()) {
		TheAI->pathfinder()->updateGoal(getObject(), getPath()->getLastNode()->getPosition(),
			getPath()->getLastNode()->getLayer());
	}
	if (m_queueForPathFrame > TheGameLogic->getFrame()) {
		m_waitingForPath = TRUE;
	}
#ifdef SLEEPY_AI
	// if we're no longer waiting for a path, make sure we wake up right away!
	if (!m_waitingForPath)
	{
		wakeUpNow();
	}
#endif
}

/* Requests a path to be found.  Note that if it is possible to do it without having to use the 
pathfinder (air units just move point to point) it generates the path immediately.  Otherwise the path
will be processed when we get to the front of the pathfind queue. jba */
//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::requestPath( Coord3D *destination, Bool isFinalGoal ) 
{

	if (m_locomotorSet.getValidSurfaces() == 0) {
		DEBUG_CRASH(("Attempting to path immobile unit."));
	}

	//DEBUG_LOG(("Request Frame %d, obj %s %x\n", TheGameLogic->getFrame(), getObject()->getTemplate()->getName().str(), getObject()));
	m_requestedDestination = *destination;
	m_isFinalGoal = isFinalGoal;
	CRCDEBUG_LOG(("AIUpdateInterface::requestPath() - m_isAttackPath = FALSE for object %d\n", getObject()->getID()));
	m_isAttackPath = FALSE;	
	m_requestedVictimID = INVALID_ID;	
	m_isApproachPath = FALSE;
	m_isSafePath = FALSE;
	if (canComputeQuickPath()) {
		computeQuickPath(destination);
		return;
	}
	m_waitingForPath = TRUE;
	if (m_pathTimestamp > TheGameLogic->getFrame()-3) {
		/* Requesting path very quickly.  Can cause a spin. */
		//DEBUG_LOG(("%d Pathfind - repathing in less than 3 frames.  Waiting 1 second\n",
			//TheGameLogic->getFrame()));
		setQueueForPathTime(LOGICFRAMES_PER_SECOND);
		// See if it has been too soon.
		// jba intense debug
		//DEBUG_LOG(("Info - RePathing very quickly %d, %d.\n", m_pathTimestamp, TheGameLogic->getFrame()));
		if (m_path && m_isBlockedAndStuck) {
			setIgnoreCollisionTime(2*LOGICFRAMES_PER_SECOND);
			m_blockedFrames = 0;
			m_isBlocked = FALSE;
			m_isBlockedAndStuck = FALSE;
		}
		return;
	}
	TheAI->pathfinder()->queueForPath(getObject()->getID());

}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::requestAttackPath( ObjectID victimID, const Coord3D* victimPos ) 
{
	if (m_locomotorSet.getValidSurfaces() == 0) {
		DEBUG_CRASH(("Attempting to path immobile unit."));
	}
	CRCDEBUG_LOG(("AIUpdateInterface::requestAttackPath() - m_isAttackPath = TRUE for object %d\n", getObject()->getID()));
	m_requestedDestination = *victimPos;
	m_requestedVictimID = victimID;	
	m_isAttackPath = TRUE;
	m_isApproachPath = FALSE;
	m_isSafePath = FALSE;
	m_waitingForPath = TRUE;
	if (m_pathTimestamp > TheGameLogic->getFrame()-3) {
		/* Requesting path very quickly.  Can cause a spin. */
		//DEBUG_LOG(("%d Pathfind - repathing in less than 3 frames.  Waiting 2 second\n",TheGameLogic->getFrame()));
		setQueueForPathTime(2*LOGICFRAMES_PER_SECOND);
		setLocomotorGoalNone();
		return;
	}
	TheAI->pathfinder()->queueForPath(getObject()->getID());
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::requestApproachPath( Coord3D *destination ) 
{
	if (m_locomotorSet.getValidSurfaces() == 0) {
		DEBUG_CRASH(("Attempting to path immobile unit."));
	}
	m_requestedDestination = *destination;
	m_isFinalGoal = TRUE;
	CRCDEBUG_LOG(("AIUpdateInterface::requestApproachPath() - m_isAttackPath = FALSE for object %d\n", getObject()->getID()));
	m_isAttackPath = FALSE;	
	m_requestedVictimID = INVALID_ID;	
	m_isApproachPath = TRUE;
	m_isSafePath = FALSE;
	m_waitingForPath = TRUE;
	if (m_pathTimestamp > TheGameLogic->getFrame()-3) {
		/* Requesting path very quickly.  Can cause a spin. */
		//DEBUG_LOG(("%d Pathfind - repathing in less than 3 frames.  Waiting 2 second\n",TheGameLogic->getFrame()));
		setQueueForPathTime(2*LOGICFRAMES_PER_SECOND);
		return;
	}
	TheAI->pathfinder()->queueForPath(getObject()->getID());
}

//-------------------------------------------------------------------------------------------------
// Requests a safe path away from the repulsor.
void AIUpdateInterface::requestSafePath( ObjectID repulsor ) 
{
	if (repulsor != m_repulsor1) {
		m_repulsor2 = m_repulsor1; // save the prior repulsor.
	}
	m_repulsor1 = repulsor;	
	m_isFinalGoal = FALSE;
	CRCDEBUG_LOG(("AIUpdateInterface::requestSafePath() - m_isAttackPath = FALSE for object %d\n", getObject()->getID()));
	m_isAttackPath = FALSE;	
	m_requestedVictimID = INVALID_ID;	
	m_isApproachPath = FALSE;
	m_isSafePath = TRUE;
	m_waitingForPath = TRUE;
	if (m_pathTimestamp > TheGameLogic->getFrame()-3) {
		/* Requesting path very quickly.  Can cause a spin. */
		//DEBUG_LOG(("%d Pathfind - repathing in less than 3 frames.  Waiting 2 second\n",TheGameLogic->getFrame()));
		setQueueForPathTime(2*LOGICFRAMES_PER_SECOND);
		return;
	}
	TheAI->pathfinder()->queueForPath(getObject()->getID());
}

enum {WAYPOINT_PATH_LIMIT=1024};
//-------------------------------------------------------------------------------------------------
// 
void AIUpdateInterface::setPathFromWaypoint(const Waypoint *way, const Coord2D *offset) 
{
	destroyPath();
	m_path = newInstance(Path);
	Coord3D pos = *getObject()->getPosition();
	m_path->prependNode( &pos, LAYER_GROUND );
	m_path->markOptimized();
	int count = 0;
	while (way) {
		Coord3D wayPos = *way->getLocation();
		wayPos.x += offset->x;
		wayPos.y += offset->y;
		if (way->getLink(0) == NULL) {
			TheAI->pathfinder()->snapPosition(getObject(), &wayPos);
		}
		m_path->appendNode( &wayPos, LAYER_GROUND );
		way = way->getLink(0);
		count++;
		if (count>WAYPOINT_PATH_LIMIT) break;
	}
	m_waitingForPath = FALSE;	 
	TheAI->pathfinder()->setDebugPath(m_path);
#ifdef SLEEPY_AI
	// if we're no longer waiting for a path, make sure we wake up right away!
	wakeUpNow();
#endif
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::onObjectCreated()
{
	// create the behavior state machine.
	// can't do this in the ctor because makeStateMachine is a protected virtual func,
	// and overrides to virtual funcs don't exist in our ctor. (look it up.)
	if (m_stateMachine == NULL)
	{
		m_stateMachine = makeStateMachine();
		m_stateMachine->initDefaultState();
	}
}

//-------------------------------------------------------------------------------------------------
AIUpdateInterface::~AIUpdateInterface( void )
{
	m_locomotorSet.clear();
	m_curLocomotor = NULL;

	if( m_stateMachine ) {
		m_stateMachine->halt();
		m_stateMachine->deleteInstance();
	}

	for (int i = 0; i < MAX_TURRETS; i++)
	{
		if (m_turretAI[i])
			m_turretAI[i]->deleteInstance();
		m_turretAI[i] = NULL;
	}
	m_stateMachine = NULL;

	// destroy the current path. (destroyPath is NULL savvy)
	destroyPath();

}

//=============================================================================
void AIUpdateInterface::setTurretTargetObject(WhichTurretType tur, Object* o, Bool forceAttacking)
{
	if (m_turretAI[tur])
	{
		m_turretAI[tur]->setTurretTargetObject(o, forceAttacking);
	}
}

//=============================================================================
Object* AIUpdateInterface::getTurretTargetObject( WhichTurretType tur, Bool clearDeadTargets )
{
	if( m_turretAI[ tur ] )
	{
		Object *obj;
		Coord3D pos;
		if( m_turretAI[ tur ]->friend_getTurretTarget( obj, pos, clearDeadTargets ) == TARGET_OBJECT )
		{
			return obj;
		}
	}
	return NULL;
}

//=============================================================================
void AIUpdateInterface::setTurretTargetPosition(WhichTurretType tur, const Coord3D* pos)
{
	if (m_turretAI[tur])
	{
		m_turretAI[tur]->setTurretTargetPosition(pos);
	}
}

//=============================================================================
void AIUpdateInterface::setTurretEnabled(WhichTurretType tur, Bool enabled)
{
	if (m_turretAI[tur])
	{
		m_turretAI[tur]->setTurretEnabled( enabled );
	}
}

//=============================================================================
void AIUpdateInterface::recenterTurret(WhichTurretType tur)
{
	if (m_turretAI[tur])
	{
		m_turretAI[tur]->recenterTurret();
	}
}

//=============================================================================
Bool AIUpdateInterface::isTurretEnabled( WhichTurretType tur ) const
{
	if( m_turretAI[ tur ] )
	{
		return m_turretAI[ tur ]->isTurretEnabled();
	}
	return FALSE;
}

//=============================================================================
Bool AIUpdateInterface::isTurretInNaturalPosition(WhichTurretType tur) const
{
	if (m_turretAI[tur])
	{
		return m_turretAI[tur]->isTurretInNaturalPosition();
	}
	return FALSE;
}

//=============================================================================
Bool AIUpdateInterface::isWeaponSlotOnTurretAndAimingAtTarget(WeaponSlotType wslot, const Object* victim) const
{
	for (int i = 0; i < MAX_TURRETS; i++)
	{
		if (m_turretAI[i] && m_turretAI[i]->isWeaponSlotOnTurret(wslot))
		{
			return m_turretAI[i]->isTryingToAimAtTarget(victim);
		}
	}
	return FALSE;
}

//=============================================================================
Bool AIUpdateInterface::getTurretRotAndPitch(WhichTurretType tur, Real* turretAngle, Real* turretPitch) const
{
	if (m_turretAI[tur])
	{
		if (turretAngle)
			*turretAngle = m_turretAI[tur]->getTurretAngle();
		if (turretPitch)
			*turretPitch = m_turretAI[tur]->getTurretPitch();
		return TRUE;
	}
	return FALSE;
}

//=============================================================================
Real AIUpdateInterface::getTurretTurnRate(WhichTurretType tur) const
{
	return (tur != TURRET_INVALID && m_turretAI[tur] != NULL) ?
					m_turretAI[tur]->getTurnRate() :
					0.0f;
}

//=============================================================================
WhichTurretType AIUpdateInterface::getWhichTurretForCurWeapon() const
{
	for (int i = 0; i < MAX_TURRETS; ++i)
		if (m_turretAI[i] && m_turretAI[i]->isOwnersCurWeaponOnTurret())
			return (WhichTurretType)i;

	return TURRET_INVALID;
}

//=============================================================================
WhichTurretType AIUpdateInterface::getWhichTurretForWeaponSlot(WeaponSlotType wslot, Real* turretAngle, Real* turretPitch) const
{
	for (int i = 0; i < MAX_TURRETS; ++i)
	{
		if (m_turretAI[i] && m_turretAI[i]->isWeaponSlotOnTurret(wslot))
		{
			if (turretAngle)
				*turretAngle = m_turretAI[i]->getTurretAngle();
			if (turretPitch)
				*turretPitch = m_turretAI[i]->getTurretPitch();

			return (WhichTurretType)i;
		}
	}
	return TURRET_INVALID;
}

//=============================================================================
Real AIUpdateInterface::getCurLocomotorSpeed() const
{
	if (m_curLocomotor != NULL)
		return m_curLocomotor->getMaxSpeedForCondition(getObject()->getBodyModule()->getDamageState());

	DEBUG_LOG(("no current locomotor!"));
	return 0.0f;
}

//=============================================================================
void AIUpdateInterface::setLocomotorUpgrade(Bool set)
{
	m_upgradedLocomotors = set;
	if (m_curLocomotorSet == LOCOMOTORSET_NORMAL || m_curLocomotorSet == LOCOMOTORSET_NORMAL_UPGRADED)
		chooseLocomotorSet(LOCOMOTORSET_NORMAL);
}

//=============================================================================
Bool AIUpdateInterface::chooseLocomotorSet(LocomotorSetType wst)
{
	DEBUG_ASSERTCRASH(wst != LOCOMOTORSET_NORMAL_UPGRADED, ("never pass LOCOMOTORSET_NORMAL_UPGRADED here"));
	if (wst == LOCOMOTORSET_NORMAL && m_upgradedLocomotors)
		wst = LOCOMOTORSET_NORMAL_UPGRADED;

	if (wst == m_curLocomotorSet)
		return TRUE;

	if (chooseLocomotorSetExplicit(wst))
	{
		chooseGoodLocomotorFromCurrentSet();
		return TRUE;
	}

	return FALSE;
}

//=============================================================================
// this should only be called by load/save, or by chooseLocomotorSet.
// it does no sanity checking; it just jams it in.
Bool AIUpdateInterface::chooseLocomotorSetExplicit(LocomotorSetType wst)
{
	const LocomotorTemplateVector* set = getAIUpdateModuleData()->findLocomotorTemplateVector(wst);
	if (set)
	{
		m_locomotorSet.clear();
		m_curLocomotor = NULL;
		for (Int i = 0; i < set->size(); ++i)
		{
			const LocomotorTemplate* lt = set->at(i);
			if (lt)
				m_locomotorSet.addLocomotor(lt);
		}
		m_curLocomotorSet = wst;
		return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::chooseGoodLocomotorFromCurrentSet( void )
{
	Locomotor* prevLoco = m_curLocomotor;

	Locomotor* newLoco = TheAI->pathfinder()->chooseBestLocomotorForPosition(getObject()->getLayer(), &m_locomotorSet, getObject()->getPosition());

	if (newLoco == NULL)
	{
		if (prevLoco != NULL)
		{
		/* due to physics, we might slight into a cell for which we have no loco
			(eg, cliff) and get stuck. this is bad. as a solution, we do this.
			this may look a little funny, but as a practical matter, it works well, 
			since the pathfinder will prevent us from doing any significant "wrong" terrain. */
			newLoco = prevLoco;
		}
		else
		{
			/* this can happen for a newly-created object, which might come into being in 
				the middle of an obstacle. for now, we just fake it and choose a ground locomotor. */
			newLoco = m_locomotorSet.findLocomotor(LOCOMOTORSURFACE_GROUND);
		}
	}

	m_curLocomotor = newLoco;

	if (prevLoco != m_curLocomotor)
	{
		// make sure the group's speed will be recalculated
		if (getGroup())
			getGroup()->recomputeGroupSpeed();

		// turn off precision-z-pos anytime the loco changes, just in case,
		// since it should only be enabled in very special cases
		m_curLocomotor->setUsePreciseZPos(FALSE);
		// ditto for no-slow-down.
		m_curLocomotor->setNoSlowDownAsApproachingDest(FALSE);
		// ditto for ultra-accuracy.
		m_curLocomotor->setUltraAccurate(FALSE);
	}
}

//----------------------------------------------------------------------------------------------------------
Object* AIUpdateInterface::checkForCrateToPickup()
{
	if (m_crateCreated != INVALID_ID) 
	{
		m_crateCreated = INVALID_ID; // we have processed it, so clear it.
		Object* crate = TheGameLogic->findObjectByID(m_crateCreated);
		if (crate) 
		{
			for (BehaviorModule** m = crate->getBehaviorModules(); *m; ++m)
			{
				CollideModuleInterface* collide = (*m)->getCollide();
				if (!collide)
					continue;

				if( collide->wouldLikeToCollideWith(getObject()))
				{
					return crate;
				}
			}
		}
	}
	return NULL;
}

#ifdef ALLOW_SURRENDER
//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::doSurrenderUpdateStuff()
{
	RELEASE_CRASH(("Read the comment in doSurrenderUpdateStuff"));

	/*
		If you ever re-enable this code, you must convert it to be 
		properly sleepy. It is crucial that we avoid requiring a call
		to AIUpdate every frame just to support this. (srj)
	*/

	UnsignedInt prevSurrenderedFrames = m_surrenderedFramesLeft;

	if (m_surrenderedFramesLeft > 0)
		--m_surrenderedFramesLeft;

	if (m_surrenderedFramesLeft > 0)
		getObject()->setModelConditionState( MODELCONDITION_SURRENDER );
	else
		getObject()->clearModelConditionState( MODELCONDITION_SURRENDER );

	//
	// when we leave a surrendered state we give ourselves an idle command ... why you ask? Well
	// during the surrender sequence we might have started moving towards a POW truck come
	// to pick us up, but now we should stop and be all normal again
	//
	if( prevSurrenderedFrames > 0 && m_surrenderedFramesLeft == 0 )
	{
		m_surrenderedPlayerIndex = -1;
		aiIdle( CMD_FROM_AI );
	}
}
#endif

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::setQueueForPathTime(Int frames)
{
#ifdef SLEEPY_AI
	if (frames >= UPDATE_SLEEP_NONE && getWakeFrame() > UPDATE_SLEEP(frames))
	{
		if (m_isInUpdate)
		{
			// we're changing this while in our own update (probably via a move state).
			// just do nothing, since update will calculate the correct sleep behavior at the end.
		}
		else
		{
			setWakeFrame(getObject(), UPDATE_SLEEP(frames));
		}
	}
#endif
	m_queueForPathFrame = frames ? (TheGameLogic->getFrame() + frames) : 0;
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::wakeUpNow()
{
#ifdef SLEEPY_AI
	if (getWakeFrame() > UPDATE_SLEEP_NONE)
	{
		if (m_isInUpdate)
		{
			// we're changing this while in our own update (probably via a move state).
			// just do nothing, since update will calculate the correct sleep behavior at the end.
		}
		else
		{
			setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
		}
	}
#endif
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::friend_notifyStateMachineChanged()
{
	wakeUpNow();
}

//-------------------------------------------------------------------------------------------------
/**
 * The "main loop" of the AI subsystem
 */
DECLARE_PERF_TIMER(AIUpdateInterface_update)
UpdateSleepTime AIUpdateInterface::update( void )	 
{
	//DEBUG_LOG(("AIUpdateInterface frame %d: %08lx\n",TheGameLogic->getFrame(),getObject()));

	USE_PERF_TIMER(AIUpdateInterface_update)
	
	m_isInUpdate = TRUE;

	m_completedWaypoint = NULL; // Reset so state machine update can set it if we just completed the path.

	// assume we can sleep forever, unless the state machine (or turret, etc) demand otherwise
	UpdateSleepTime subMachineSleep = UPDATE_SLEEP_FOREVER;

	/* -aislice: think every n-th frame, drive every frame.

		 The state machine below is where a unit decides - what to attack, where the next waypoint is,
		 whether to give up and repath - and it is the expensive half of the object walk. The
		 locomotor at the bottom of this function is what actually moves it, and that is not skipped,
		 so a sliced-out frame is a frame the unit keeps driving along the route it already has
		 without asking any new questions. The stagger is the object id: creation order, identical on
		 every machine, so this stays inside the CRC. Off by default (n = 1). */
	const Int aiSlice = TheGlobalData ? TheGlobalData->m_aiSliceFrames : 1;
	const Bool thinkThisFrame = (aiSlice <= 1) ||
		(((TheGameLogic->getFrame() + (UnsignedInt)getObject()->getID()) % (UnsignedInt)aiSlice) == 0);

	StateReturnType stRet = thinkThisFrame ? getStateMachine()->updateStateMachine() : STATE_CONTINUE;

	// A unit that was just built walks a short exit path out of its producer and then, if the player
	// set a rally point, attack moves to it - so it stops and fights whatever it runs into on the way
	// instead of taking the shots and walking on.  This is checked right after the machine ran,
	// because finishing (or failing) the exit path is what drops us into idle in the first place.
	if (m_hasExitProductionRallyPoint && getAIStateType() == AI_IDLE)
	{
		Coord3D rallyPoint = m_exitProductionRallyPoint;
		m_hasExitProductionRallyPoint = FALSE;
		privateAttackMoveToPosition( &rallyPoint, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
		stRet = STATE_CONTINUE;
	}

	if (IS_STATE_SLEEP(stRet))
	{
		Int frames = GET_STATE_SLEEP_FRAMES(stRet);
		if (frames < subMachineSleep)
			subMachineSleep = UPDATE_SLEEP(frames);
	}
	else
	{
		// it's STATE_CONTINUE, STATE_SUCCESS, or STATE_FAILURE, 
		// any of which will probably require next frame
		subMachineSleep = UPDATE_SLEEP_NONE;
	}

	// note that this is all OK with sleepiness, since m_movementComplete can
	// only be set via our statemachine (via friend_startingMove or friend_endMove),
	// which we just called. thus we should
	// never have worry about waking ourselves up when this changes, since
	// if it changes the code will always flow thru here anyway. (srj)
	if (m_movementComplete) 
	{
		setQueueForPathTime(0);

		// destroy path
		destroyPath();
		setLocomotorGoalNone();

		getObject()->clearModelConditionState(MODELCONDITION_MOVING);

		Coord3D goalPos;
		if (TheAI->pathfinder()->goalPosition(getObject(), &goalPos)) 
		{
			// Pop to goal - This shouldn't happen (often), but make sure we got to where we're going.
			Real dx = goalPos.x-getObject()->getPosition()->x;
			Real dy = goalPos.y-getObject()->getPosition()->y;
			if (dx*dx+dy*dy>=PATHFIND_CELL_SIZE_F*PATHFIND_CELL_SIZE_F) 
			{
				// Too far, so just grid current pos.
				goalPos = *getObject()->getPosition();
				TheAI->pathfinder()->snapPosition(getObject(), &goalPos);
			}
			setFinalPosition(&goalPos);
			TheAI->pathfinder()->updateGoal(getObject(), &goalPos, getObject()->getLayer());
		}
		m_movementComplete = FALSE;
		ignoreObstacle(NULL);
	}

	UnsignedInt now = TheGameLogic->getFrame();
	if (m_queueForPathFrame != 0)
	{
		if (now >= m_queueForPathFrame) 
		{
			TheAI->pathfinder()->queueForPath(getObject()->getID());
			setQueueForPathTime(0);
		}
		else
		{
			UnsignedInt sleepForPathDelta = m_queueForPathFrame - now;
			if (sleepForPathDelta < subMachineSleep)
				subMachineSleep = UPDATE_SLEEP(sleepForPathDelta);
		}
	}

	Object *obj = getObject();

	if (! obj->isEffectivelyDead() &&
			! obj->isDisabledByType( DISABLED_PARALYZED ) &&
			! obj->isDisabledByType( DISABLED_UNMANNED ) &&
			! obj->isDisabledByType( DISABLED_EMP ) &&
			! obj->isDisabledByType( DISABLED_SUBDUED ) &&
			! obj->isDisabledByType( DISABLED_HACKED ) )
	{
		// If we are dead, don't let the turrets do anything anymore, or else they will keep attacking
		// (a turret is deciding too, so it waits with the rest of the unit's thinking under -aislice)
		for (int i = 0; thinkThisFrame && i < MAX_TURRETS; ++i)
		{
			if (m_turretAI[i])
			{
				UpdateSleepTime tmp = m_turretAI[i]->updateTurretAI();
				if (tmp < subMachineSleep)
					subMachineSleep = tmp;
			}
		}
	}

	// must do death check outside of the state machine update, to avoid corruption
	if (isAiInDeadState() && !(getStateMachine()->getCurrentStateID() == AI_DEAD) )
	{
		/// @todo Yikes! If we are not interruptable, and we die, what do we do? (MSB)
		getStateMachine()->clear();
		getStateMachine()->setState( AI_DEAD );
		getStateMachine()->lock("AIUpdateInterface::update");
		// strangely, dead things need to NOT sleep at all. (but they don't stay dead for long,
		// so this is not too bad.)
		subMachineSleep = UPDATE_SLEEP_NONE;
	}

	// do this objects movement
	UpdateSleepTime tmp = doLocomotor();
	if (tmp < subMachineSleep)
		subMachineSleep = tmp;

#ifdef ALLOW_DEMORALIZE
	RELEASE_CRASH(("If ALLOW_DEMORALIZE is ever defined, this code must be redone to do proper SLEEPY updates. (srj)"));
	// update the demoralized frames if present
	if( m_demoralizedFramesLeft > 0 )
	{
		setDemoralized( m_demoralizedFramesLeft - 1 );
	}
#endif

#ifdef ALLOW_SURRENDER
	RELEASE_CRASH(("If ALLOW_SURRENDER is ever defined, this code must be redone to do proper SLEEPY updates. (srj)"));
	doSurrenderUpdateStuff();
#endif

	m_isInUpdate = FALSE;

	if (m_completedWaypoint != NULL)
	{
		// sleep NONE here so that it will get reset next frame.
		// this happen infrequently, so it shouldn't be an issue.
		return UPDATE_SLEEP_NONE;
	}
	else
	{
#ifdef SLEEPY_AI
		return subMachineSleep;
#else
		return UPDATE_SLEEP_NONE;
#endif
	}
} 



//-------------------------------------------------------------------------------------------------
/**
 * Append waypoint to queue for later movement
 */
Bool AIUpdateInterface::queueWaypoint( const Coord3D *pos )
{
	if (m_waypointCount < MAX_WAYPOINTS)
	{
		m_waypointQueue[ m_waypointCount++ ] = *pos;
		return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/**
 * Start moving along the waypoint path in the queue
 */
void AIUpdateInterface::executeWaypointQueue( void )
{
	// the dead don't listen very well
	if (isAiInDeadState())
		return;

//	m_actionStack->clear();

	if (m_waypointCount > 0)
	{
		m_waypointIndex = 0;
		m_executingWaypointQueue = TRUE;
	}
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::clearWaypointQueue( void )
{
	m_waypointCount = 0;
	m_executingWaypointQueue = FALSE;
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::markAsDead()
{
	m_isAiDead = TRUE;
	getObject()->setEffectivelyDead(TRUE);
	wakeUpNow();	// wake us up immediately so that our anim plays promptly!
}

//-------------------------------------------------------------------------------------------------
/* Returns TRUE if this ai has a higher path priority than the other one.
The way to have a higher priority is:
1. If the paths were assigned when both units were in the same ai group, we use the path priority assigned.
2. If not, the unit that is in front has the higher priority.
3. If exactly tied (usually beacause both units got unfortunately snapped to the same location), ObjectID is used
to break the tie. 
*/
Bool AIUpdateInterface::hasHigherPathPriority(AIUpdateInterface *otherAI) const
{
	Object *other = otherAI->getObject();

	// Dozers have highest priority.
	if (getObject()->isKindOf(KINDOF_DOZER) && !other->isKindOf(KINDOF_DOZER)) {
		return TRUE;
	}
	if (!getObject()->isKindOf(KINDOF_DOZER) && other->isKindOf(KINDOF_DOZER)) {
		return FALSE;
	}

	// Vehicles always have higher priority than infantry.
	if (getObject()->isKindOf(KINDOF_VEHICLE) && other->isKindOf(KINDOF_INFANTRY)) {
		return TRUE;
	}
	if (getObject()->isKindOf(KINDOF_INFANTRY) && other->isKindOf(KINDOF_VEHICLE)) {
		return FALSE;
	}

	// The paths aren't of the same group, so see which unit is in front.
	Coord3D ourDir = *getObject()->getUnitDirectionVector2D();
	Coord3D otherDir = *other->getUnitDirectionVector2D();
	if (ourDir.x*otherDir.x + ourDir.y*otherDir.y <= 0) {
		return getObject()->getID() < other->getID();
	}
	Coord2D	combinedDir; 
	combinedDir.x = ourDir.x + otherDir.x;
	combinedDir.y = ourDir.y + otherDir.y;
	Coord2D vectorToOther;
	vectorToOther.x = other->getPosition()->x - getObject()->getPosition()->x;
	vectorToOther.y = other->getPosition()->y - getObject()->getPosition()->y;
	// Dot product is our directions projected onto each other.
	Real dotProduct = combinedDir.x*vectorToOther.x	+ combinedDir.y*vectorToOther.y;
	if (dotProduct>0) return FALSE;  // other is ahead of us along our directional vector.
	if (dotProduct<0) return TRUE; // We are ahead of other.
	// Exactly equal.  Use object id's to break the tie.  
	return getObject()->getID() < other->getID();
}

//-------------------------------------------------------------------------------------------------
/* Returns max speed we can have and not run into unit that is blocking us.
*/
Real AIUpdateInterface::calculateMaxBlockedSpeed(Object *other) const
{
	Coord3D ourDir = *getObject()->getUnitDirectionVector2D();
	Coord3D otherDir = *other->getUnitDirectionVector2D();
	// Dot product is our directions projected onto each other.
	Coord2D vectorToOther;
	vectorToOther.x = other->getPosition()->x - getObject()->getPosition()->x;
	vectorToOther.y = other->getPosition()->y - getObject()->getPosition()->y;
	vectorToOther.normalize();
	Real dotProduct = vectorToOther.x*otherDir.x	+ vectorToOther.y*otherDir.y;
	if (dotProduct<0) return 0; // They are running into us.

	Real speedFactor = dotProduct;
	PhysicsBehavior *otherPhysics = other->getPhysics();
	if (!otherPhysics) {
		return m_curMaxBlockedSpeed;
	}	
	Coord3D otherVel = *otherPhysics->getVelocity();
	otherVel.z = 0;
	// Calculate how fast other is moving away from us...
	Real awaySpeed = otherVel.length() * speedFactor;				 

	// Now calculate the amount we are moving relative to towards them...
	dotProduct = vectorToOther.x*ourDir.x	+ vectorToOther.y*ourDir.y;
	if (dotProduct<=0) {
		// Unexpected - we are moving away.  Shouldn't be blocked...
		return m_curMaxBlockedSpeed;
	}
	Real maxSpeed = awaySpeed / dotProduct;
	if (other->getFormationID()!=NO_FORMATION_ID && getObject()->getFormationID()==other->getFormationID()) {
		maxSpeed *= 0.55f; // don't let formations crowd each other.
	}
	if (maxSpeed>m_curMaxBlockedSpeed) return m_curMaxBlockedSpeed;
	return maxSpeed;
}


//-------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::blockedBy(Object *other)
/* Returns TRUE if we are blocked from moving by the other object.*/
{
	Coord3D goalPos = *getStateMachine()->getGoalPosition();
	Object *obj = getObject();
	Coord3D pos = *obj->getPosition();
	ICoord2D goalCell = *getPathfindGoalCell();

	// If we are near our final goal, don't get stuck.
	if (goalCell.x>0 && goalCell.y>0) {
		Real dx = fabs(goalPos.x-pos.x);
		Real dy = fabs(goalPos.y-pos.y);
		if (dx<PATHFIND_CELL_SIZE_F && dy<PATHFIND_CELL_SIZE_F) {
			return FALSE; // If we're approaching our goal, ignore obstacles.
		}
	}

	Bool canCrush = obj->canCrushOrSquish(other, TEST_CRUSH_OR_SQUISH);
	if (canCrush) return FALSE; // just run over them.

	AIUpdateInterface* aiOther = other->getAI();

	if (!aiOther) return FALSE; // Ignore it.
	if (!aiOther->isDoingGroundMovement()) {
		return FALSE; // Can't be blocked if the other is airborne.
	}

	if (getCurLocomotor() && getCurLocomotor()->isMovingBackwards()) {
		return false; // don't collide.
	}
	Bool otherMoving = ( aiOther->m_locomotorGoalType != NONE );
	Coord3D otherPos = *other->getPosition();
	Real dx = pos.x-otherPos.x;
	Real dy = pos.y-otherPos.y;
	Real curDSqr = dx*dx+dy*dy;

	if (obj->isKindOf(KINDOF_INFANTRY) && other->isKindOf(KINDOF_INFANTRY)) {
		// Infantry doesn't tend to impede other infantry...
#ifdef INFANTRY_MOVES_THROUGH_INFANTRY
		if (!otherMoving) {
			return FALSE;  // Infantry can run through other infantry.
		}
		return FALSE; 
#else
		// If we are crossing, just pass through.
		Coord3D ourDir = *obj->getUnitDirectionVector2D();
		Coord3D theirDir = *other->getUnitDirectionVector2D();
		// Dot product is our directions projected onto each other.
		Real dotProduct = ourDir.x*theirDir.x	+ ourDir.y*theirDir.y;
		if (dotProduct<=0.25) return FALSE;  // we are not moving in the same direction.
#endif
	}

	if (curDSqr < PATHFIND_CELL_SIZE_F*PATHFIND_CELL_SIZE_F*0.0001f) {
		// Somehow 2 units ended up on the same grid.
		// Lowest path priority wins.
		return (hasHigherPathPriority(aiOther));
	}

	// we've been blocked for a while.  If we're crossing, just move through.
	Coord3D ourDir = *obj->getUnitDirectionVector2D();
	Coord3D theirDir = *other->getUnitDirectionVector2D();
	// Dot product is our directions projected onto each other.
	Real dotProduct = ourDir.x*theirDir.x	+ ourDir.y*theirDir.y;
	if (getNumFramesBlocked()>LOGICFRAMES_PER_SECOND) {
		if (dotProduct<=0.0f) return FALSE;  // we are not moving in the same direction.
	}

	Real collisionAngle = ThePartitionManager->getRelativeAngle2D( obj, &otherPos );
	Real otherAngle = ThePartitionManager->getRelativeAngle2D( other, &pos );
	//DEBUG_LOG(("Collision angle %.2f, %.2f, %s, %x %s\n", collisionAngle*180/PI, otherAngle*180/PI, obj->getTemplate()->getName().str(), obj, other->getTemplate()->getName().str()));
	Real angleLimit = PI/4; // 45 degrees.
	if (collisionAngle>PI/2 || collisionAngle<-PI/2) {
		return FALSE; // we're moving away.
	}
	if (!otherMoving) angleLimit *= 0.75f;
	if (collisionAngle>angleLimit || collisionAngle<-angleLimit) {
		if (dotProduct<=0.0f) return FALSE;  // we are not moving in the same direction.
		if (otherMoving && (otherAngle>angleLimit || otherAngle<-angleLimit) ) {
			// See if we're running into each other.
			Coord3D ourDir = *obj->getUnitDirectionVector2D();
			Coord3D theirDir = *other->getUnitDirectionVector2D();
			dx += ourDir.x - theirDir.x;
			dy += ourDir.y - theirDir.y;
			if (curDSqr>dx*dx+dy*dy) {
				if (hasHigherPathPriority(aiOther)) {
					// Lowest path priority wins.
					return FALSE;
				}
			}	else {
				//DEBUG_LOG(("Moving Away From EachOther\n"));
				return FALSE;  // moving away, so no need for corrective action.
			}
		} else {
			return FALSE;	 // Off angle, and they're not moving, so we aren't moving into each other.
		}
	}


	if (!aiOther->isAiInDeadState())	
	{
		return TRUE;
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::needToRotate(void)
/* Returns TRUE if we need to rotate to point in our path's direcion.*/
{
	if (isWaitingForPath()) 
		return TRUE; // new path will probably require rotation.

	if (this->getCurLocomotor() && this->getCurLocomotor()->getWanderWidthFactor()>0.0f) 
		return FALSE; // wanderers don't need to rotate.

	Real deltaAngle = 0;
	if (getPath())
	{
		ClosestPointOnPathInfo info;
		CRCDEBUG_LOG(("AIUpdateInterface::needToRotate() - calling computePointOnPath() for object %d\n", getObject()->getID()));
		getPath()->computePointOnPath(getObject(), m_locomotorSet, *getObject()->getPosition(), info);
		deltaAngle = ThePartitionManager->getRelativeAngle2D( getObject(), &info.posOnPath );
	}	

	if (fabs(deltaAngle)>PI/30) 
	{
		return TRUE;
	}

	return FALSE;
}


//-------------------------------------------------------------------------------------------------
// -crowd: the constants the steering stack is made of, all in world units (a pathfind cell is 10).
//-------------------------------------------------------------------------------------------------
static const Real CROWD_AIR					= 4.0f;		///< air a body wants round it, on top of both radii
static const Real CROWD_SEP_STEP		= 1.5f;		///< most one frame of separation may move a lane
static const Real CROWD_PASS_CLEAR	= 6.0f;		///< room a passing lane leaves beside the blocker
static const Real CROWD_TAPER_DIST	= PATHFIND_CELL_SIZE_F * 8.0f;	///< the band closes over the last of the route
static const Int  CROWD_BRAKE_FRAMES	= 8;		///< frames of closing time a brake is allowed to read
static const Int  CROWD_FAN_FRAMES	= 30;			///< held up this long before spreading out
static const Real CROWD_FAN_RATE		= 0.8f;		///< and then sideways at this much a frame
static const Real CROWD_SEP_FILTER	= 0.13f;	///< how much of a frame's sideways push is believed (~4/sec)
static const Real CROWD_AIM_CRUISE	= 0.23f;	///< how fast the aim follows the route while driving (~7/sec)
static const Real CROWD_AIM_URGENT	= 0.67f;	///< and while manoeuvring (~20/sec), where lag is worse than twitch
static const Real CROWD_AIM_DEAD		= 0.035f;	///< two degrees: hold the wheel still rather than chase the noise
static const Int  CROWD_STUCK_PRESS	= 10;			///< a third of a second of getting nowhere: stop being polite
static const Int  CROWD_STUCK_GIVEUP	= 60;		///< two seconds of it: back out of wherever this is
static const Int  CROWD_ESCAPE_FRAMES	= 66;		///< and spend this long doing it before giving the route another go
static const Int  CROWD_ESCAPE_COOL	= 90;			///< no second attempt before this, or a wedged pair rock forever
static const Int  CROWD_MERGE_FRAMES	= 45;		///< a second and a half: how far ahead a merge is worth noticing
static const Real CROWD_LOOK_FRAMES	= 16.0f;	///< half a second: the travel the aim filter is damped against
static const Real CROWD_AIM_SLOWEST	= 0.08f;	///< however fast the chassis, the aim still follows this much of the error

//-------------------------------------------------------------------------------------------------
/* Returns TRUE if the physics collide should apply the force.  Normally not.
Also determines whether objects are blocked, and if so, if they are stuck.  jba.*/
Bool AIUpdateInterface::processCollision(PhysicsBehavior *physics, Object *other)
{

#ifdef DO_UNIT_TIMINGS
	return false;
#endif

	if (m_ignoreCollisionsUntil > TheGameLogic->getFrame()) 
		return FALSE;

	if (m_canPathThroughUnits) 
		return FALSE;

	AIUpdateInterface* aiOther = other->getAI();
	if (aiOther == NULL) 
		return FALSE;

	Bool selfMoving = isMoving();
	Bool otherMoving = ( aiOther && aiOther->isMoving() );
	if (!isDoingGroundMovement()) return FALSE;
	if (!aiOther->isDoingGroundMovement()) return FALSE;
	if (selfMoving) 
	{
		Bool blocked = blockedBy(other);
		if (blocked) 
		{
			if (getObject()->isKindOf(KINDOF_INFANTRY)) 
			{
				// Panic bounces around.
				if (getStateMachine()->getCurrentStateID() == AI_PANIC) 
				{
					return TRUE; // just bounce off of other humans.
				}
			}
			m_isBlocked = TRUE; // we are blocked.
 			if (otherMoving && aiOther->isWaitingForPath()) 
			{
				return FALSE; // let them get their path;
			}

			Real maxSpeed = calculateMaxBlockedSpeed(other);
			if (maxSpeed < m_curMaxBlockedSpeed)
			{
				m_curMaxBlockedSpeed = maxSpeed;
			}

			// before settling in behind him, see whether the route is wide enough to go round.
			// under -crowd the same decision is made every frame in crowdSteer, off the band and the
			// whole neighbourhood rather than off this one collision, so the old rule stands down
			if (!TheGlobalData->m_crowdModel)
				tryLaneChangeAround(other);

			if (!aiOther->isMovingAwayFrom(getObject())) {

				if (other->isKindOf(KINDOF_INFANTRY) && !getObject()->isKindOf(KINDOF_INFANTRY)) 
				{
					//Kris: Patch 1.01 -- November 5, 2003
					//Prevent busy units from being told to move out of the way!
					if( other->testStatus( OBJECT_STATUS_IS_USING_ABILITY ) || other->getAI() && other->getAI()->isBusy() )
					{
						return FALSE;
					}
					aiOther->aiMoveAwayFromUnit(getObject(), CMD_FROM_AI);
					return FALSE;
				}
#define dont_MOVE_AROUND // It just causes more problems than it fixes. jba.
#ifdef MOVE_AROUND 
				if (m_curLocomotor!=NULL && (other->isKindOf(KINDOF_INFANTRY)==getObject()->isKindOf(KINDOF_INFANTRY))) {
					Real myMaxSpeed = m_curLocomotor->getMaxSpeedForCondition(getObject()->getBodyModule()->getDamageState());
					Locomotor *hisLoco = aiOther->getCurLocomotor();
					if (hisLoco) {
						Real hisMaxSpeed = hisLoco->getMaxSpeedForCondition(other->getBodyModule()->getDamageState());
						if (hisMaxSpeed > 0.05 && hisMaxSpeed < 0.6f*myMaxSpeed)	{
							aiOther->aiMoveAwayFromUnit(getObject(), CMD_FROM_AI);
							return FALSE;
						}
					}
				}
#endif
			}

			//DEBUG_LOG(("Blocked %s, %x, %s\n", getObject()->getTemplate()->getName().str(), getObject(), other->getTemplate()->getName().str()));
			if (m_blockedFrames==0) m_blockedFrames = 1;
			if (!needToRotate()) 
			{
				// If we are already pointing in the right direction, we may be stuck.
				if (!otherMoving)
				{
					// (asking the idle allied blocker to step aside was tried here and reverted:
					// at a group's destination every arriving unit shoved the parked ones, which
					// shoved others - the group milled about and repathed without end)
					//
					/* -crowd asks again, under the three conditions that revert was missing.  We have
						 to have been held up for a while, so an arrival that clears on its own is left
						 alone; we have to still have somewhere to be, which is what stops the whole thing
						 at a destination where nobody does; and the parked unit has to be the smaller of
						 the two, so a mob of infantry cannot pass a tank around by taking turns to shove
						 it. */
					if (TheGlobalData->m_crowdModel
								&& m_crowdQueued > CROWD_FAN_FRAMES * 2
								&& Crowd_remaining(getObject()) > PATHFIND_CELL_SIZE_F * 3.0f
								&& !crowdOutranksMe(other)
								&& !aiOther->isMovingAwayFrom(getObject())
								&& !aiOther->isBusy()
								&& !other->testStatus(OBJECT_STATUS_IS_USING_ABILITY))
					{
						aiOther->aiMoveAwayFromUnit(getObject(), CMD_FROM_AI);
						m_crowdQueued = 0;			// he has been asked; give him time to answer
						return FALSE;
					}
					// Intense logging jba
					// DEBUG_LOG(("Blocked&Stuck !otherMoving\n"));
					m_isBlockedAndStuck = TRUE;
					return FALSE;
				}

				// See if other is blocked by us.
				if (aiOther->blockedBy(getObject())) 
				{
					if (!aiOther->needToRotate()) 
					{
						// Deadlocked.  -crowd settles it by size and by who is nearer the end of his
						// route, which is the same order the steering used all the way here; retail
						// settles it by who is carrying the more urgent kind of order
						const Bool yield = TheGlobalData->m_crowdModel
																	? crowdOutranksMe(other)
																	: !hasHigherPathPriority(aiOther);
						if (yield)
						{
							// get out of his way.
							aiMoveAwayFromUnit(aiOther->getObject(), CMD_FROM_AI);
							//m_isBlockedAndStuck = TRUE;
							// Intense logging jba.
							// DEBUG_LOG(("Blocked&Stuck other is blockedByUs, has higher priority\n"));
						}
					}
				}	
				else 
				{
					// Just wait.
				}
			}	
			else 
			{
				// We are rotating, so don't accumulate blocked frames.
				m_blockedFrames = 1;
			}
		}
	}	
	else 
	{
		if (isAiInDeadState()) 
		{
			// Dead infantry get pushed around by crushers.
			if (getObject()->isKindOf(KINDOF_INFANTRY) && other->canCrushOrSquish(getObject(), TEST_SQUISH_ONLY)) 
			{
				return TRUE;
			}
		}

		Coord3D otherPos = *other->getPosition();
		Real dx = getObject()->getPosition()->x - otherPos.x;
		Real dy = getObject()->getPosition()->y - otherPos.y;
		Real curDSqr = dx*dx+dy*dy;
		// (a footprint-scaled threshold was tried here and reverted: tanks parked closer than
		// it by the group's own formation kept being pulled apart and never came to rest)
		if (!otherMoving && curDSqr < PATHFIND_CELL_SIZE_F*PATHFIND_CELL_SIZE_F*0.25f)
		{
			if (this->getCurrentStateID() == AI_BUSY) {
				return false;
			}
			if (getObject()->testStatus(OBJECT_STATUS_IS_USING_ABILITY)) {
				return false;  // we are doing a special ability.  Shouldn't move at this time.  jba.
			}
			// jba intense debug
			//DEBUG_LOG(("*****Units ended up on top of each other.  Shouldn't happen.\n"));
			if (isIdle()) {
				Coord3D safePosition = *getObject()->getPosition();
				
				TheAI->pathfinder()->adjustToPossibleDestination(getObject(), getLocomotorSet(), &safePosition);
				aiMoveToPosition( &safePosition, CMD_FROM_AI ); 
			}
			if (aiOther->isIdle()) {
				TheAI->pathfinder()->adjustToPossibleDestination(other, aiOther->getLocomotorSet(), 
					&otherPos);
				aiOther->aiMoveToPosition( &otherPos, CMD_FROM_AI);
			}
		}
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/**
 * See if we can do a quick path without pathfinding.
 */
Bool AIUpdateInterface::canComputeQuickPath( void )
{
	/* Basically, if a unit is moving through the air, we can quick path.  jba. */
	Bool landBound = FALSE;
	// Note - if a truck happens to pop into the air and gets a move to command, it still
	// needs to pathfind.  So only skip pathfinding for airborne things that can fly... jba.
	if (!(m_locomotorSet.getValidSurfaces() & LOCOMOTORSURFACE_AIR))
  {
		landBound = TRUE;
	}

	Bool unitIsFlyingThroughTheAir = FALSE;
	if (landBound) {
		unitIsFlyingThroughTheAir = FALSE; // Land bound units never fly.
	}	else {
		if (!isDoingGroundMovement()) {
			// If it can fly, and it isn't moving on the ground, we're flying.
			unitIsFlyingThroughTheAir = TRUE;
		}
	}
	return unitIsFlyingThroughTheAir;
}

//-------------------------------------------------------------------------------------------------
/**
 * Create a quick path.  (Just places the start & end point as the path). jba.
 */
Bool AIUpdateInterface::computeQuickPath( const Coord3D *destination )
{
	// for now, quick path objects don't pathfind, generally airborne units
	// build a trivial one-node path containing destination

	
	// First, see if our path already goes to the destination.
	if (m_path) {
		PathNode *closeNode = NULL;
		closeNode = m_path->getLastNode();
		if (closeNode && closeNode->getNextOptimized()==NULL) {
			Real dxSqr = destination->x - closeNode->getPosition()->x;
			dxSqr *= dxSqr;
			Real dySqr = destination->y - closeNode->getPosition()->y;
			dySqr *= dySqr;
			Real dzSqr = destination->z - closeNode->getPosition()->z;
			dzSqr *= dzSqr;
			if (dxSqr+dySqr+dzSqr<0.25f) {
				return TRUE;
			}
		}
	}
	// destroy previous path
	destroyPath();
	if (getObject()->isKindOf(KINDOF_AIRCRAFT) && !getObject()->isKindOf(KINDOF_PROJECTILE)) {	
		m_path = TheAI->pathfinder()->getAircraftPath(getObject(), destination);
	} else {
		m_path = newInstance(Path);
		m_path->prependNode( destination, LAYER_GROUND );
		Coord3D pos = *getObject()->getPosition();
		pos.z = destination->z;
		m_path->prependNode( &pos, getObject()->getLayer() );
		m_path->getFirstNode()->setNextOptimized(m_path->getFirstNode()->getNext());

		if (TheGlobalData->m_debugAI==AI_DEBUG_PATHS) 
		{
			TheAI->pathfinder()->setDebugPath(m_path);
		}
	}


	// timestamp when the path was created
	m_pathTimestamp = TheGameLogic->getFrame();

	m_blockedFrames = 0;
	m_isBlockedAndStuck = FALSE;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/**
 * Invoke the pathfinder to compute a path to the desired location.
 */
Bool AIUpdateInterface::computePath( PathfindServicesInterface *pathServices, Coord3D *destination )
{

	if (!m_isBlockedAndStuck)	{
		destroyPath();
	}

	if (canComputeQuickPath())
	{
		return computeQuickPath(destination);
	}
	m_retryPath = false;
	Region3D extent;
	TheTerrainLogic->getMaximumPathfindExtent(&extent);
	if (!extent.isInRegionNoZ(destination)) {
		// We're going off the map.
		Coord3D pos = *getObject()->getPosition();
		if (!extent.isInRegionNoZ(&pos))	{
			// We're starting off the map.  Since we're off the map, we can't pathfind so just build a path.
			return computeQuickPath(destination);
		}
	}

	// Special case of exit factory. jba.
	if ((m_stateMachine->getCurrentStateID() == AI_FOLLOW_EXITPRODUCTION_PATH) && canPathThroughUnits()) {
		Bool ok = computeQuickPath(destination);
		if (ok) {
			TheAI->pathfinder()->moveAlliesAwayFromDestination(getObject(), *destination);
			setCanPathThroughUnits(false);
			setGoalPositionClipped(destination, CMD_FROM_AI);
			return ok;
		}
	}

	Path *theNewPath = NULL;
	TheAI->pathfinder()->setIgnoreObstacleID( getIgnoredObstacleID() );
	TheAI->pathfinder()->setIgnoreUnderConstruction( getDozerAIInterface() != NULL );

	Coord3D originalDestination = *destination;
	// sanity check - if destination cell is invalid, don't bother pathing

	LocomotorSurfaceTypeMask surfaces = m_locomotorSet.getValidSurfaces();
	if (!m_isFinalGoal && TheAI->pathfinder()->isLinePassable( getObject(), surfaces,
			getObject()->getLayer(), *getObject()->getPosition(), originalDestination, false, true)) {
		// this way out skips the reset at the bottom, and a flag left on would answer for whoever
		// asks the pathfinder next
		TheAI->pathfinder()->setIgnoreUnderConstruction( FALSE );
		return computeQuickPath(destination);
	}

	PathfindLayerEnum destinationLayer = TheTerrainLogic->getLayerForDestination(destination);
	if (TheAI->pathfinder()->validMovementPosition( getObject()->getCrusherLevel()>0, destinationLayer, m_locomotorSet, destination ) == FALSE)
	{
		theNewPath = NULL;
	}
	else
	{
		// compute a ground-based path
		if (m_isBlockedAndStuck) {
			theNewPath = pathServices->patchPath( getObject(), m_locomotorSet, 
				getPath(), m_isBlockedAndStuck);
		}	else {
			theNewPath = pathServices->findPath( getObject(), m_locomotorSet, getObject()->getPosition(), 
				destination);
		}
	}
	if (theNewPath==NULL && m_path==NULL) {
		Real pathCostFactor = 0.0f;	
		theNewPath = pathServices->findClosestPath( getObject(), m_locomotorSet, getObject()->getPosition(), 
			destination, m_isBlockedAndStuck, pathCostFactor, FALSE );
		m_retryPath = true;
	}
	TheAI->pathfinder()->setIgnoreObstacleID( INVALID_ID );
	TheAI->pathfinder()->setIgnoreUnderConstruction( FALSE );
	// after both the direct path and the closest-path fallback: nothing came back, so this unit
	// is not going anywhere this frame.  The count is what says whether a cost change made the
	// search fail rather than merely route differently.
	if (theNewPath == NULL)
		Pathfinder::bumpNoPath();
	if (theNewPath) {
		// destroy previous path
		destroyPath();
		m_path = theNewPath;
		if (getCurLocomotor() && getCurLocomotor()->isUltraAccurate()) {
			// Move exactly to the destination.  Normal ground pathfinding moves to a gridded location.
			theNewPath->updateLastNode(&originalDestination);
		}
		setLocomotorGoalPositionOnPath();
 		if( !getObject()->isKindOf(KINDOF_NO_COLLIDE))// If I don't collide with things, I don't need to tell them to get out of the way
			TheAI->pathfinder()->moveAllies(getObject(), theNewPath);
	} else {
		// Keep using the old path.
 		if (m_path && m_isBlockedAndStuck) {
			destroyPath();
			// Stop and wait one second.

			setQueueForPathTime(LOGICFRAMES_PER_SECOND);
			Coord3D goalPos;
			Object *obj = getObject();
			goalPos = *obj->getPosition();
			TheAI->pathfinder()->snapPosition(obj, &goalPos);
			setFinalPosition(&goalPos);
			setLocomotorGoalNone();

			m_blockedFrames = 0;
			m_isBlocked = FALSE;
			m_isBlockedAndStuck = FALSE;
		}
	}
	// timestamp when the path was created
	m_pathTimestamp = TheGameLogic->getFrame();

	m_blockedFrames = 0;
	m_isBlockedAndStuck = FALSE;
	if (m_path)
		return TRUE;

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/**
 * Invoke the pathfinder to compute a path to attack the current victim.
 */
Bool AIUpdateInterface::computeAttackPath( PathfindServicesInterface *pathServices, const Object *victim, const Coord3D* victimPos )
{
	//CRCDEBUG_LOG(("AIUpdateInterface::computeAttackPath() for object %d\n", getObject()->getID()));
	// See if it has been too soon.
	if (m_pathTimestamp >= TheGameLogic->getFrame()-2) 
	{
		// jba intense debug
		//CRCDEBUG_LOG(("Info - RePathing very quickly %d, %d.\n", m_pathTimestamp, TheGameLogic->getFrame()));
		if (m_path && m_isBlockedAndStuck) 
		{
			setIgnoreCollisionTime(2*LOGICFRAMES_PER_SECOND);
			m_blockedFrames = 0;
			m_isBlocked = FALSE;
			m_isBlockedAndStuck = FALSE;
			return TRUE;
		}
	}
	Bool landBound = FALSE;
	// Note - if a truck happens to pop into the air and gets a move to command, it still
	// needs to pathfind.  So only skip pathfinding for airborne things that can fly... jba.
	if (!(m_locomotorSet.getValidSurfaces() & LOCOMOTORSURFACE_AIR))
  {
		landBound = TRUE;
	}

	Object* source = getObject();
	if (!victim && !victimPos) 
	{
		//CRCDEBUG_LOG(("AIUpdateInterface::computeAttackPath() - victim is NULL\n"));
		return FALSE;
	}

	PathfindLayerEnum victimLayer = LAYER_GROUND;
	if (victim) {
		victimLayer = victim->getLayer();
	}

	Weapon *weapon = source->getCurrentWeapon();
	if (!weapon)
	{
		DEBUG_CRASH(("no weapon in AIUpdateInterface::computeAttackPath"));
		return FALSE;
	}

	// is our weapon within attack range?
	// if so, just return TRUE with no path.
	if (victim != NULL)
	{
		if (weapon->isWithinAttackRange(source, victim))
		{
			Bool viewBlocked = FALSE;
			if (isDoingGroundMovement() && !victim->isSignificantlyAboveTerrain()) 
			{
				viewBlocked = TheAI->pathfinder()->isAttackViewBlockedByObstacle(source, *source->getPosition(), victim, *victim->getPosition());
			}
			if (!viewBlocked) 
			{
				destroyPath();
				//CRCDEBUG_LOG(("AIUpdateInterface::computeAttackPath() - target is in range and visible\n"));
				return TRUE;
			}
			
		}
	}
	else if (victimPos != NULL)
	{
		if (weapon->isWithinAttackRange(source, victimPos))
		{
			Bool viewBlocked = FALSE;
			if (isDoingGroundMovement()) 
			{
				viewBlocked = TheAI->pathfinder()->isAttackViewBlockedByObstacle(source, *source->getPosition(), NULL, *victimPos);
			}
			if (!viewBlocked) {
				destroyPath();
				//CRCDEBUG_LOG(("AIUpdateInterface::computeAttackPath() target pos is in range and visible\n"));
				return TRUE;
			}
		}
	}

	// Contact weapon
	if (weapon->isContactWeapon()) 
	{
		// Weapon is basically a contact weapon, like a car bomb.  The approach target logic
		// has been modified to let it approach the object, so just approach the target position.	jba.
		Coord3D tmp = *victimPos;
		destroyPath();
		if (this->getCurLocomotor()) 
		{
			getCurLocomotor()->setNoSlowDownAsApproachingDest(TRUE);
		}
		Bool ok = computePath(pathServices, &tmp);
		if (m_path==NULL) return false;
		Real dx, dy;
		dx = victimPos->x - m_path->getLastNode()->getPosition()->x;
		dy = victimPos->y - m_path->getLastNode()->getPosition()->y;
		if (sqr(dx)+sqr(dy) < sqr(PATHFIND_CELL_SIZE_F*3)) {
			if (m_path) 
			{
				m_path->updateLastNode(victimPos); // jam in the coordinates of the target.
			}
		}
		dx = source->getPosition()->x - m_path->getLastNode()->getPosition()->x;
		dy = source->getPosition()->y - m_path->getLastNode()->getPosition()->y;
		if (sqr(dx)+sqr(dy) < sqr(PATHFIND_CELL_SIZE_F)) {
			// Very short path - we can't get to the goal.
			destroyPath();
			return false;
		}
		//CRCDEBUG_LOG(("AIUpdateInterface::computeAttackPath() is contact weapon\n"));
		return ok;
	}


	Coord3D localVictimPos;
	if (victim != NULL)
	{
		if (victim->isKindOf(KINDOF_BRIDGE)) 
		{
			TBridgeAttackInfo info;
			TheTerrainLogic->getBridgeAttackPoints(victim, &info);
			Real distSqr1 = ThePartitionManager->getDistanceSquared( source, &info.attackPoint1, FROM_BOUNDINGSPHERE_3D );
			Real distSqr2 = ThePartitionManager->getDistanceSquared( source, &info.attackPoint2, FROM_BOUNDINGSPHERE_3D );
			if (distSqr2<distSqr1) {
 				localVictimPos = info.attackPoint2;
			} else {
 				localVictimPos = info.attackPoint1;
			}
		}
		else
		{
			localVictimPos = *victim->getPosition();
		}
	}
	else
	{
		localVictimPos = *victimPos;
	}

	localVictimPos.z = TheTerrainLogic->getLayerHeight( localVictimPos.x, localVictimPos.y, victimLayer );

	if (getObject()->isAboveTerrain() && !landBound)
	{
		// for now, airborne objects don't pathfind
		// build a trivial one-node path containing destination

		weapon->computeApproachTarget(getObject(), victim, &localVictimPos, 0, localVictimPos);
		//DEBUG_ASSERTCRASH(weapon->isGoalPosWithinAttackRange(getObject(), &localVictimPos, victim, victimPos, NULL),
		//	("position we just calced is not acceptable\n"));
		
		// First, see if our path already goes to the destination.
		if (m_path) 
		{
			PathNode *startNode, *closeNode = NULL;
			startNode = m_path->getFirstNode();
			closeNode = startNode->getNextOptimized();
			if (closeNode && closeNode->getNextOptimized()==NULL) {
				Real dxSqr = localVictimPos.x - closeNode->getPosition()->x;
				dxSqr *= dxSqr;
				Real dySqr = localVictimPos.y - closeNode->getPosition()->y;
				dySqr *= dySqr;
				if (dxSqr+dySqr<0.25f) 
				{
					return TRUE;
				}
			}
		}
		// destroy previous path
		destroyPath();
		m_path = newInstance(Path);
		m_path->prependNode( &localVictimPos, LAYER_GROUND );
		Coord3D pos = *getObject()->getPosition();
		pos.z = localVictimPos.z;
		m_path->prependNode( &pos, LAYER_GROUND );
		m_path->getFirstNode()->setNextOptimized(m_path->getFirstNode()->getNext());
		if (TheGlobalData->m_debugAI==AI_DEBUG_PATHS) 
		{
			TheAI->pathfinder()->setDebugPath(m_path);
		}
	}
	else
	{
		// destroy previous path
		destroyPath();

		TheAI->pathfinder()->setIgnoreObstacleID( getIgnoredObstacleID() );

		// compute a ground-based path
		m_path = pathServices->findAttackPath( getObject(), m_locomotorSet, getObject()->getPosition(), 
			victim, &localVictimPos, weapon);
		if (m_path) {
			Coord3D goal = *m_path->getLastNode()->getPosition();
			if (!weapon->isGoalPosWithinAttackRange(getObject(), &goal, victim, &localVictimPos)) {
				// We didn't actually find a path we can attack from. [8/14/2003]
				// If the move is a short distance, just do a find closest path to our current
				// position.  This will unstack us if we are on top of another unit. jba.
				Coord3D objPos = *getObject()->getPosition();
				goal.sub(&objPos);
				if (goal.length()<3*PATHFIND_CELL_SIZE_F) {
					destroyPath();
					TheAI->pathfinder()->adjustDestination(getObject(), m_locomotorSet, &objPos);
					m_path = pathServices->findClosestPath(getObject(), m_locomotorSet, getObject()->getPosition(), 
								&objPos, false, 0.2f, true );
				}
				if (m_path==NULL) {
					return false;
				}
			}
			goal = *m_path->getLastNode()->getPosition();
			TheAI->pathfinder()->updateGoal(getObject(), &goal, TheTerrainLogic->getLayerForDestination(&goal));
			if (m_path->getBlockedByAlly()) 
			{
	 			if( !getObject()->isKindOf(KINDOF_NO_COLLIDE))// If I don't collide with things, I don't need to tell them to get out of the way
					TheAI->pathfinder()->moveAllies(getObject(), m_path);
			}
		}
		TheAI->pathfinder()->setIgnoreObstacleID( INVALID_ID );
	}

	// timestamp when the path was created
	m_pathTimestamp = TheGameLogic->getFrame();

	m_blockedFrames = 0;
	m_isBlockedAndStuck = FALSE;
	//CRCDEBUG_LOG(("AIUpdateInterface::computeAttackPath() done\n"));
	if (m_path)
		return TRUE;

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/**
 * Destroy the current path, and set it to NULL
 */
void AIUpdateInterface::destroyPath( void )
{
	// destroy previous path
	if (m_path)
		m_path->deleteInstance();

	m_path = NULL;
	m_waitingForPath = FALSE; // we no longer need it.
	//CRCDEBUG_LOG(("AIUpdateInterface::destroyPath() - m_isAttackPath = FALSE for object %d\n", getObject()->getID()));
	m_isAttackPath = FALSE;
	// the lane belongs to the route, not to the unit: a new route gets a new one, seeded off
	// wherever the unit is standing when it is handed
	m_laneFractionValid = FALSE;
	crowdReleaseCorridor();
	setLocomotorGoalNone();
}

//-------------------------------------------------------------------------------------------------
/** The band was measured against one route and means nothing against another, so it dies with the
		path.  m_crowdSample going back to 0 also clears the "this route has no band" latch, which is
		what stops a unit on a two-cell path from re-probing every frame for the rest of its life. */
//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::crowdReleaseCorridor( void )
{
	if (m_corridor)
	{
		delete m_corridor;
		m_corridor = NULL;
	}
	m_crowdLatValid = FALSE;
	m_crowdSample = 0;
	m_crowdQueued = 0;
	m_crowdHoldFrame = 0;
	m_crowdSepSmooth = 0.0f;
	m_crowdAimValid = FALSE;
	m_crowdStuck = 0;
	/* The backing-out manoeuvre survives the route.  Being wedged is the one thing a new route does
		 not cure - the new one starts in the same hole - and a unit that repaths every second while
		 stuck would throw away the only rule that gets it out, once a second, forever. */
}

//-------------------------------------------------------------------------------------------------
/**
 * Pick where across the route's width this unit rides.
 *
 * The lane has to be handed down by whoever issued the order, and the first version of this
 * measured it here instead, which was worth nothing: a unit's route starts under its own tracks,
 * so its sideways distance from that route is zero and every member of a group came out at 0.5.
 * A group therefore has to say, at order time, where each member sat across the group - see
 * AIGroup::groupMoveToPosition.  A unit ordered on its own has no group to sit across and rides
 * the centre, which is what retail does anyway.
 */
//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::seedLaneFraction( void )
{
	Bool hadPending = m_hasPendingLane;

	m_laneFraction = m_hasPendingLane ? m_pendingLane : 0.5f;
	m_laneFractionValid = TRUE;
	m_laneHoldFrame = 0;
	m_hasPendingLane = FALSE;

	if (TheGlobalData->m_noLanePath)
		m_laneFraction = 0.5f;

	// the other end of the SHOWLANES trail: a lane handed out at order time is worth nothing if the
	// path arrives after something else has already seeded this unit at the centre.
	if (TheGlobalData->m_showLanes)
	{
		DEBUG_LOG(("SHOWLANES seed: unit %d pending=%d lane=%.2f\n", getObject()->getID(),
			hadPending ? 1 : 0, m_laneFraction));
	}
}

//-------------------------------------------------------------------------------------------------
/**
 * Take a different lane to get past a unit that is in the way and not going anywhere.
 *
 * The alternative retail offers is to wait, count blocked frames, and eventually spend a search on
 * a route that starts from the middle of the queue - which is how a column of tanks turns into a
 * column of stopped tanks.  Sliding across the band costs no search at all: the route is unchanged,
 * only the point on it the unit steers at moves.  The lane is held for a while so it does not flap
 * between the two sides of a blocker who is himself drifting, and it is paid for in a little speed.
 */
//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::tryLaneChangeAround( Object *other )
{
	if (TheGlobalData->m_noLanePath)
		return;
	if (!m_laneFractionValid || getPath() == NULL)
		return;
	if (TheGameLogic->getFrame() < m_laneHoldFrame)
		return;
	if (m_curLocomotor == NULL)
		return;

	// only worth going round somebody slower than us; anybody keeping up will clear on his own
	Real mySpeed = m_curLocomotor->getMaxSpeedForCondition(getObject()->getBodyModule()->getDamageState());
	if (mySpeed < 0.05f)
		return;
	/* How fast the blocker is actually going, not how fast his engine could.  Comparing top speeds
		 means a column of identical tanks never passes anything: every one of them reports full
		 speed while crawling nose to tail behind the same jam, so the one mechanism that could
		 break the queue up refused to fire in exactly the case it was written for. */
	AIUpdateInterface *aiOther = other->getAI();
	Real hisSpeed = 0.0f;
	if (aiOther != NULL && aiOther->isMoving() && other->getPhysics() != NULL)
		hisSpeed = other->getPhysics()->getVelocityMagnitude();
	if (hisSpeed > mySpeed * 0.8f)
		return;

	Coord3D onPath;
	Coord2D dir;
	if (!getPath()->closestPointAndDir(*getObject()->getPosition(), &onPath, &dir))
		return;

	Coord2D left, right;
	left.x = -dir.y;	left.y = dir.x;
	right.x = dir.y;	right.y = -dir.x;

	Pathfinder *pf = TheAI->pathfinder();
	PathfindLayerEnum layer = getObject()->getLayer();
	Real leftRoom = pf->laneExtent(getObject(), m_locomotorSet, layer, &onPath, &left);
	Real rightRoom = pf->laneExtent(getObject(), m_locomotorSet, layer, &onPath, &right);
	Real width = leftRoom + rightRoom;
	if (width < 1.0f)
		return;		// a doorway has no room to pass in, and pretending otherwise just wedges both units

	// where the blocker sits across the same band, and how far past him we have to be to clear
	Real hisLateral = (other->getPosition()->x - onPath.x) * left.x
									+ (other->getPosition()->y - onPath.y) * left.y;
	Real hisFraction = Pathfinder_laneFraction(hisLateral, leftRoom, rightRoom);
	Real gap = (getObject()->getGeometryInfo().getBoundingCircleRadius()
						+ other->getGeometryInfo().getBoundingCircleRadius()) / width;

	// prefer the side we are already on, and take the other one if that side has no room
	Real want = (m_laneFraction >= hisFraction) ? (hisFraction + gap) : (hisFraction - gap);
	if (want > 0.95f || want < 0.05f)
		want = (m_laneFraction >= hisFraction) ? (hisFraction - gap) : (hisFraction + gap);
	if (want > 0.95f || want < 0.05f)
		return;
	if (fabs(want - m_laneFraction) < 0.02f)
		return;

	m_laneFraction = want;
	m_laneHoldFrame = TheGameLogic->getFrame() + PF_LANE_HOLD_FRAMES;
	getPath()->invalidateCachedPointOnPath();		// or we steer at the old lane for another twenty frames

	// a unit crabbing sideways is not driving forwards at full speed
	Real cap = mySpeed * 0.85f;
	if (cap < m_curMaxBlockedSpeed)
		m_curMaxBlockedSpeed = cap;
}

//-------------------------------------------------------------------------------------------------
/**
 * Right of way between two units, decided the same way by both of them.
 *
 * Size first, because a bigger body has less room to be squeezed and shoving it is what wedges a
 * doorway.  Then how much route is left, so the unit nearly there is let out rather than made to
 * wait behind one that has half a map to cross.  The inside of a bend counts as being further along
 * than it is: it is the position with no room, and letting it out first is what lets the rest flow
 * round the corner instead of all four of them arriving at the apex together.  Id last, which
 * decides nothing on the ground but guarantees the two answers are opposites.
 */
//-------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::crowdOutranksMe( Object *other ) const
{
	const Object *self = getObject();
	if (other == NULL)
		return FALSE;

	if (Crowd_outranks( other, self ))
		return TRUE;
	if (Crowd_outranks( self, other ))
		return FALSE;

	Real mine = Crowd_remaining( self ) - Crowd_bendBonus( m_corridor, m_crowdSample, m_crowdLat );
	Real his = Crowd_remaining( other );
	const AIUpdateInterface *ai = other->getAIUpdateInterface();
	if (ai != NULL)
		his -= Crowd_bendBonus( ai->getCrowdCorridor(), ai->getCrowdSample(), ai->getCrowdLat() );

	if (fabs( mine - his ) > 1.0f)
		return his < mine;

	return other->getID() < self->getID();
}

//-------------------------------------------------------------------------------------------------
/**
 * Where across the route to drive this frame, and how fast.
 *
 * This is the whole -crowd model, and it is one function on purpose: every rule in it reads the
 * same neighbour scan and writes the same two numbers, and the only two numbers the engine's
 * steering will accept are the point to aim at and the speed to aim at it with.  Retail has one
 * reactive rule (a collision, once it has already happened, caps the speed) and consequently a
 * group of twenty crosses a map in single file and stops dead in a doorway.
 *
 * Order matters and is the sandbox's: give way to something bigger coming up behind, then deal with
 * whatever is directly ahead - pass it if there is room, brake if there is not - then spread out,
 * but only while actually held up, then push apart from whoever is too close.  Separation last
 * because it is the smallest correction and has to win the tie; fanning out before it, because a
 * unit that has been stopped for a second wants a different lane and not a nudge.
 */
//-------------------------------------------------------------------------------------------------
/** Somewhere a wedged unit can back out to, or FALSE if it is walled in on every side.
		Sideways first and backwards second: the ground ahead is what it is already failing to drive
		through, and a unit that has been stationary for two seconds is in a hole its own route made. */
//-------------------------------------------------------------------------------------------------
static Bool crowdFindEscape( Object *self, const LocomotorSet& locoSet, const Coord2D& tan,
														 Int firstSide, Coord3D *out )
{
	const Coord3D *pos = self->getPosition();
	const Real myR = self->getGeometryInfo().getBoundingCircleRadius();
	const Real across = myR * 2.0f + PATHFIND_CELL_SIZE_F;
	const Real behind = myR + PATHFIND_CELL_SIZE_F * 0.5f;
	const Bool crusher = self->getCrusherLevel() > 0;
	const PathfindLayerEnum layer = self->getLayer();

	const Real sx = -tan.y, sy = tan.x;			// left of the route
	for (Int t = 0; t < 4; t++)
	{
		// out and back to the roomier side, out and back the other way, straight out, straight back
		const Real side = (t == 1) ? -(Real)firstSide : (Real)firstSide;
		const Real lat = (t < 3) ? across : 0.0f;
		const Real back = (t < 2) ? behind : ((t == 2) ? 0.0f : across);

		Coord3D p;
		p.x = pos->x + sx * lat * side - tan.x * back;
		p.y = pos->y + sy * lat * side - tan.y * back;
		p.z = TheTerrainLogic->getGroundHeight( p.x, p.y );
		if (TheAI->pathfinder()->validMovementPosition( crusher, layer, locoSet, &p ))
		{
			*out = p;
			return TRUE;
		}
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::crowdSteer( Coord3D& goalPos, Real& speed )
{
	Object *self = getObject();
	Path *path = getPath();
	if (path == NULL || m_curLocomotor == NULL)
		return;

	/* Only units that were sent somewhere as a group.  A group order hands every member a lane
		 before the paths exist, and that hand-out is the flag: no lane handed down, no band, and the
		 unit drives retail's centre line.  A unit crossing a map on its own has nobody to share the
		 road with, and the rules below then cost it real time - they are written to resolve traffic,
		 and a single vehicle repositioning inside a firefight is not traffic. */
	if (!m_hasPendingCrowdLat && !m_crowdLatValid)
		return;

	/* The band is measured once per route.  m_crowdSample = -1 is the latch for a route too short
		 to have one, without which a unit driving the last two cells of its path re-probes two
		 hundred cells every frame for the rest of its life. */
	if (m_corridor == NULL)
	{
		if (m_crowdSample < 0)
			return;
		CrowdCorridor *corr = new CrowdCorridor;
		if (!corr->build( self, m_locomotorSet, path ))
		{
			delete corr;
			m_crowdSample = -1;
			return;
		}
		m_corridor = corr;
		m_crowdSample = 0;
		// the lane is not cleared here: destroyPath already did that for a new route, and a save
		// reloaded mid-drive has to come back in the lane it was saved in rather than at the centre
	}

	const Coord3D *myPos = self->getPosition();
	const Int i = m_corridor->nearest( *myPos, m_crowdSample );
	m_crowdSample = i;

	const CrowdCorridor::Sample& here = m_corridor->at( i );
	const Real myR = self->getGeometryInfo().getBoundingCircleRadius();

	/* The last cell of the route is arrival, and arrival is not a formation problem.  Only the last
		 cell: standing the rules down over the last four instead - on the theory that a short hop
		 inside a firefight is not a march - measures three times worse (298 blocked unit-frames per
		 1000 against 108).  Short hops are exactly where units are packed tightest. */
	if (m_corridor->length() - here.along < PATHFIND_CELL_SIZE_F)
		return;

	if (!m_crowdLatValid)
	{
		/* A unit ordered on its own rides wherever it is already standing, which for a route that
			 starts under its own tracks is the centre line - retail's answer, and the right one.  A
			 unit ordered as part of a group was told where to sit before the path existed. */
		m_crowdLat = m_hasPendingCrowdLat ? m_pendingCrowdLat : m_corridor->latOf( i, *myPos );
		m_hasPendingCrowdLat = FALSE;
		m_crowdLatValid = TRUE;
		m_crowdSide = (here.left >= here.right) ? 1 : -1;
	}

	const UnsignedInt now = TheGameLogic->getFrame();

	/* ---- how long this unit has been asking to move and not moving ----
		 Not the same question as m_blockedFrames, which is about collisions: a unit facing the wrong
		 way, one whose speed cap has been talked down to nothing by the rules below, and one wedged
		 between two allies it never quite touches are all standing still without a collision to show
		 for it.  This counts the ground covered against the speed asked for, which catches all three
		 and is the number the whole ladder below is hung off. */
	const Real dxMoved = myPos->x - m_crowdLastPos.x;
	const Real dyMoved = myPos->y - m_crowdLastPos.y;
	const Real moved = (Real)sqrt( dxMoved * dxMoved + dyMoved * dyMoved );
	if (speed > 0.01f && moved < speed * 0.2f)
		++m_crowdStuck;
	else
		m_crowdStuck = 0;
	m_crowdLastPos = *myPos;

	/* ---- and what to do about it ----
		 Two rungs.  The first stands the courtesies down: a unit that is getting nowhere has already
		 given way, braked and queued for a third of a second, and every one of those rules is now
		 costing it the speed it needs to push through.  The second backs it out.  Two seconds of
		 nothing is not traffic, it is a wedge - between two allies, in the corner of a cliff, against
		 the one tank in the column that is never going to move - and no amount of steering along a
		 route that leads through the wedge gets out of it.  Retail's answer is to keep driving into
		 whatever it is until the pathfinder notices; that works when there is a collision to count,
		 and this is the case where there is not. */
	const Bool pressing = m_crowdStuck > CROWD_STUCK_PRESS;

	if (m_crowdEscapeUntil > 0)
	{
		const Real edx = m_crowdEscape.x - myPos->x;
		const Real edy = m_crowdEscape.y - myPos->y;
		const Bool arrived = (Real)sqrt( edx * edx + edy * edy ) < myR + 2.0f;
		if (arrived || now >= m_crowdEscapeUntil)
		{
			m_crowdEscapeUntil = 0;
			m_crowdEscapeCool = now + CROWD_ESCAPE_COOL;
			m_crowdStuck = 0;
		}
		else
		{
			// nothing else applies: the route is what it is wedged against
			goalPos = m_crowdEscape;
			goalPos.z = TheTerrainLogic->getGroundHeight( goalPos.x, goalPos.y );
			m_crowdQueued = 0;
			m_crowdAimValid = FALSE;			// the aim filter is for following a route, not for getting out of a hole
			if (TheGlobalData->m_showLanes && (now % LOGICFRAMES_PER_SECOND) == 0)
				DEBUG_LOG(("SHOWLANES crowd: unit %d escape to %.0f,%.0f\n", self->getID(), goalPos.x, goalPos.y));
			return;
		}
	}
	else if (m_crowdStuck > CROWD_STUCK_GIVEUP && now >= m_crowdEscapeCool)
	{
		const Int firstSide = (here.left >= here.right) ? 1 : -1;
		if (crowdFindEscape( self, m_locomotorSet, here.tan, firstSide, &m_crowdEscape ))
		{
			m_crowdEscapeUntil = now + CROWD_ESCAPE_FRAMES;
			m_crowdStuck = 0;
			return;
		}
		// walled in on all four sides: keep trying the route, and ask again in a second
		m_crowdStuck = CROWD_STUCK_GIVEUP - LOGICFRAMES_PER_SECOND;
	}

	//--- one scan, every rule reads it -------------------------------------------------------------
	const Real scanRange = PATHFIND_CELL_SIZE_F * 4.0f + myR;
	const Real lookAhead = PATHFIND_CELL_SIZE_F * (Real)CROWD_LOOKAHEAD_CELLS + myR;

	PartitionFilterRelationship		fRel( self, PartitionFilterRelationship::ALLOW_ALLIES );
	PartitionFilterAlive					fAlive;
	PartitionFilterSameMapStatus	fMap( self );
	PartitionFilter *filters[] = { &fRel, &fAlive, &fMap, NULL };
	SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( self, scanRange, FROM_CENTER_2D, filters );
	MemoryPoolObjectHolder hold( iter );

	Real sep = 0.0f;					// how far sideways the crowd is pushing us
	Object *blocker = NULL;		// nearest thing directly in the way
	Real blockerFwd = 0.0f;
	Real blockerGap = 0.0f;
	Real blockerSpeed = 0.0f;
	Bool blockerMoving = FALSE;
	Bool giveWay = FALSE;
	Real giveWayLat = 0.0f;
	Int rank = 0;							// how many held-up neighbours are ahead of us in the queue
	Bool touching = FALSE;		// somebody's body is inside ours right now
	Object *joiner = NULL;		// somebody coming in from the side who will cross our line
	Real joinerSide = 0.0f;
	Real joinerR = 0.0f;
	Real joinerWhen = 1.0e9f;

	PhysicsBehavior *myPhys = self->getPhysics();
	Coord3D myVel;
	myVel.zero();
	if (myPhys != NULL)
		myVel = *myPhys->getVelocity();

	for (Object *o = iter->first(); o; o = iter->next())
	{
		if (o == self)
			continue;
		AIUpdateInterface *ai = o->getAI();
		if (ai == NULL || !ai->isDoingGroundMovement())
			continue;

		/* Infantry walks through infantry, and the band is not allowed to take that back.  Retail's
			 own rule is in blockedBy: two foot soldiers block each other only while they are going the
			 same way, and two squads crossing pass straight through one another.  Left in the scan
			 below, a crossing squad reads as a wall of blockers - every man of it brakes, gives way and
			 shuffles sideways for a crowd he is supposed to walk into.  Same dot product as blockedBy's
			 so the two agree; a column following a column is still traffic and still gets the rules. */
		if (self->isKindOf( KINDOF_INFANTRY ) && o->isKindOf( KINDOF_INFANTRY ))
		{
			const Coord3D *myDir = self->getUnitDirectionVector2D();
			const Coord3D *hisDir = o->getUnitDirectionVector2D();
			if (myDir->x * hisDir->x + myDir->y * hisDir->y <= 0.25f)
				continue;
		}

		const Coord3D *hp = o->getPosition();
		const Real dx = hp->x - myPos->x;
		const Real dy = hp->y - myPos->y;
		const Real fwd = dx * here.tan.x + dy * here.tan.y;			// along our route
		const Real side = -dx * here.tan.y + dy * here.tan.x;		// across it, left positive
		const Real hisR = o->getGeometryInfo().getBoundingCircleRadius();
		const Real gap = (Real)sqrt( dx * dx + dy * dy ) - myR - hisR;
		const Real comfort = myR + hisR + CROWD_AIR;

		PhysicsBehavior *hisPhys = o->getPhysics();
		const Bool hisMoving = ai->isMoving() && hisPhys != NULL && hisPhys->getVelocityMagnitude() > 0.05f;

		if (gap < 0.0f)
			touching = TRUE;

		/* Somebody joining our road from the side, which is the case braking handles worst: he is not
			 in front of us yet, so nothing slows down, and by the time he is, both of us are in the same
			 square.  Moving over for him costs nothing and turns a queue into a zipper.

			 The test is a real closest approach on the two current courses, not a cone.  Anything looser
			 and a column shuffles sideways for every unit driving vaguely alongside it, which costs more
			 speed than the braking it replaces. */
		if (hisMoving && fabs( side ) > (myR + hisR) * 0.5f && hisPhys != NULL)
		{
			const Coord3D *hv = hisPhys->getVelocity();
			const Real hs = (Real)sqrt( hv->x * hv->x + hv->y * hv->y );
			if (hs > 0.01f)
			{
				const Real align = (hv->x * here.tan.x + hv->y * here.tan.y) / hs;
				// a merge comes in at an angle: parallel traffic is the stream itself, head-on is not a merge
				if (align > 0.35f && align < 0.8f)
				{
					const Real rvx = hv->x - myVel.x;
					const Real rvy = hv->y - myVel.y;
					const Real rvv = rvx * rvx + rvy * rvy;
					if (rvv > 1.0e-6f)
					{
						const Real when = -(dx * rvx + dy * rvy) / rvv;			// frames to closest approach
						if (when >= 0.0f && when < (Real)CROWD_MERGE_FRAMES && when < joinerWhen)
						{
							const Real mx = dx + rvx * when;
							const Real my = dy + rvy * when;
							if ((Real)sqrt( mx * mx + my * my ) < myR + hisR + 0.8f && crowdOutranksMe( o ))
							{
								joiner = o;
								joinerSide = side;
								joinerR = hisR;
								joinerWhen = when;
							}
						}
					}
				}
			}
		}

		// abreast and too close: push apart
		if (gap < comfort && fabs( fwd ) < myR + hisR)
		{
			Real dir;
			if (fabs( side ) > 0.01f)
				dir = (side > 0.0f) ? -1.0f : 1.0f;
			else
				dir = (o->getID() < self->getID()) ? -1.0f : 1.0f;	// exactly abreast; somebody has to pick
			sep += dir * (comfort - gap);
		}

		/* Something bigger coming up behind in our lane.  It cannot go round us in the room it has
			 and it will not stop, so the courtesy is ours to extend, and extending it costs us a lane
			 rather than a stop. */
		if (!giveWay && hisMoving && fwd < 0.0f && Crowd_outranks( o, self )
					&& fabs( side ) < myR + hisR + CROWD_AIR)
		{
			const Real away = myR + hisR + CROWD_AIR;
			Real dir;
			if (fabs( side ) > 0.01f)
				dir = (side > 0.0f) ? -1.0f : 1.0f;
			else
				dir = (Real)m_crowdSide;
			/* Clear of *his* line, not of wherever we happen to be.  Stepping aside from our own
				 current lane looks the same for one frame and is not the same thing at all: the lane it
				 produces is the input to the next frame's step, so a unit given way to for a second walks
				 itself out to the edge of the band a body width at a time. */
			giveWayLat = m_corridor->latOf( i, *hp ) + dir * away;
			giveWay = TRUE;
		}

		// directly ahead, near enough to matter
		if (fwd > 0.0f && fwd < lookAhead + hisR && fabs( side ) < myR + hisR + 1.0f)
		{
			if (blocker == NULL || fwd < blockerFwd)
			{
				blocker = o;
				blockerFwd = fwd;
				blockerGap = gap;
				blockerSpeed = 0.0f;
				blockerMoving = hisMoving;
				if (hisPhys != NULL)
				{
					const Coord3D *v = hisPhys->getVelocity();
					blockerSpeed = v->x * here.tan.x + v->y * here.tan.y;		// his speed our way, not his speed
					if (blockerSpeed < 0.0f)
						blockerSpeed = 0.0f;
				}
			}
		}

		// our place in the queue, which is how far out we fan when it stops moving
		if (gap < comfort * 2.0f && crowdOutranksMe( o ))
			++rank;
	}

	//--- the rules, in order -----------------------------------------------------------------------
	Real lat = m_crowdLat;
	Real cap = speed;
	Bool queued = FALSE;
	Bool merged = FALSE;

	if (giveWay && !pressing)
	{
		lat = giveWayLat;
		m_crowdHoldFrame = now + CROWD_HOLD_FRAMES;
	}

	/* Make room for the joiner rather than braking for him, and only for one we would have had to
		 brake for anyway - crowdOutranksMe already asked that question.  A unit that is itself getting
		 nowhere extends no courtesies: it has none to spare. */
	if (joiner != NULL && !pressing)
	{
		const Real shift = myR + joinerR + 0.5f;
		const Real want = lat - ((joinerSide > 0.0f) ? shift : -shift);
		if (fabs( m_corridor->clampLat( i, want ) - want ) < 0.5f)
		{
			lat = want;
			m_crowdHoldFrame = now + CROWD_HOLD_FRAMES;
			merged = TRUE;
		}
	}

	if (blocker != NULL && !pressing)
	{
		if (!crowdOutranksMe( blocker ))
		{
			// we have right of way; he is the one who has to move, and we only avoid rear-ending him
			if (blockerMoving && blockerGap < myR && blockerSpeed < cap)
				cap = blockerSpeed;
		}
		else
		{
			const Real hisR = blocker->getGeometryInfo().getBoundingCircleRadius();
			const Real hisLat = m_corridor->latOf( i, *blocker->getPosition() );
			const Real shift = myR + hisR + CROWD_PASS_CLEAR;

			/* Pass on the outside of a bend.  The inside is where the road runs out, and a unit that
				 dives up the inside of a turn to get past somebody arrives at the apex with a wall on
				 one side and the unit it just passed on the other. */
			const Real curv = m_corridor->curvature( i );
			Int first = (fabs( curv ) > 0.08f) ? ((curv > 0.0f) ? -1 : 1) : m_crowdSide;

			Bool took = FALSE;
			if (now >= m_crowdHoldFrame)
			{
				for (Int t = 0; t < 2 && !took; t++)
				{
					const Real want = hisLat + (Real)((t == 0) ? first : -first) * shift;
					if (fabs( m_corridor->clampLat( i, want ) - want ) < 0.5f)
					{
						lat = want;
						m_crowdSide = (want >= hisLat) ? 1 : -1;
						m_crowdHoldFrame = now + CROWD_HOLD_FRAMES;
						took = TRUE;
					}
				}
			}

			if (!took)
			{
				queued = TRUE;

				/* Brake behind a unit that is going somewhere, and never behind one that is not.
					 A parked ally on the route is not traffic, it is an obstacle, and the engine already
					 knows what to do about obstacles: drive into it, count the blocked frames, repath.
					 Braking short of it instead means the collision never happens, m_blockedFrames never
					 rises, and every piece of retail's stuck machinery sits idle while the column dies
					 politely a metre behind a tank that is never going to move.  That one line was worth
					 16593 blocked unit-frames against a baseline of 507. */
				/* And then only when we are actually going to hit him.  Braking by distance slows the
					 whole march: a unit ten units behind traffic moving at its own speed is not catching
					 anybody up, and pricing that gap costs 60% of the column's speed for nothing.  Closing
					 speed and the time it leaves is the only thing worth reading. */
				if (blockerMoving)
				{
					const Real want = Crowd_brakeSpeed( speed, blockerSpeed, blockerGap, CROWD_BRAKE_FRAMES );
					if (want < cap)
						cap = want;
				}
			}
		}
	}

	if (queued)
		++m_crowdQueued;
	else if (m_crowdQueued > 0)
		--m_crowdQueued;

	/* Fanning out is only worth anything while stopped.  Doing it all the time is a formation, and
		 a formation held across a map is what drives a group into every obstacle sideways-on; the
		 sandbox spreads out at the back of a jam and closes up again the moment it clears. */
	if (m_crowdQueued > CROWD_FAN_FRAMES && !pressing)
	{
		/* Two bodies off the line is a wide road.  Multiplying by the whole queue rank is how the
			 eighth unit in a jam ends up eighty units into the scenery, still politely queueing. */
		Int step = rank + 1;
		if (step > 2) step = 2;
		const Real target = (Real)m_crowdSide * (2.0f * myR + CROWD_PASS_CLEAR) * (Real)step;
		if (target > lat + 0.1f)
			lat += CROWD_FAN_RATE;
		else if (target < lat - 0.1f)
			lat -= CROWD_FAN_RATE;
	}

	/* Separation goes into the lane and stays there.  Steering with it frame by frame and leaving
		 the lane where it was reads better on paper - a shove is not a decision - and measures worse:
		 3312 blocked unit-frames against 1236 over the same eight seeds.  A shove that is forgotten
		 has to be paid again every frame, and two units abreast in a narrow band spend the whole
		 drive rediscovering each other. */
	/* Through a filter on the way in, though.  The raw push is the sum over whoever happens to be
		 inside the comfort radius this frame, and that set changes every frame: one neighbour drifting
		 in and out of range flips the push by a body width and back again, and the lane - and with it
		 the point the unit is aiming at - shimmers. */
	m_crowdSepSmooth += (sep - m_crowdSepSmooth) * CROWD_SEP_FILTER;
	sep = m_crowdSepSmooth;
	if (sep > CROWD_SEP_STEP) sep = CROWD_SEP_STEP;
	if (sep < -CROWD_SEP_STEP) sep = -CROWD_SEP_STEP;
	lat += sep;

	/* Everything above chose a lane; this is the only place the unit is allowed to move into one,
		 and never faster than it drives.  Giving way and passing both name a lane a full body width
		 away, and taking it in one frame swings the aim point twenty-odd feet sideways two cells in
		 front of the tracks - which is a turn no tank can make, so the unit stops and rotates on the
		 spot, and by the time it is facing the new lane the rule that asked for it has moved on.  The
		 spinning and the rocking back and forth are both this.  A quarter of forward speed sideways
		 is about twenty degrees of steering, which a tank takes without stopping. */
	const Real laneRate = (speed * 0.25f > 0.4f) ? speed * 0.25f : 0.4f;
	Real move = m_corridor->clampLat( i, lat ) - m_crowdLat;
	if (move > laneRate) move = laneRate;
	if (move < -laneRate) move = -laneRate;
	m_crowdLat = m_corridor->clampLat( i, m_crowdLat + move );

	// ease off through a bend, or the outside of the group is asked for a speed it cannot turn at
	const Real bend = (Real)fabs( m_corridor->curvature( i ) );
	if (bend > 0.15f)
	{
		Real f = 1.0f - bend * 0.5f;
		if (f < 0.55f) f = 0.55f;
		cap *= f;
	}

	//--- and finally, the two numbers the locomotor takes ------------------------------------------
	/* How far ahead to steer.  The distance is the old one, two cells and a body; what changed is
		 where it is measured from.  Taking the point at a sample index quantises it: the aim jumped a
		 whole cell forward every time the unit crossed a sample boundary, and on anything but a
		 straight the direction handed to the locomotor stepped with it.  It runs off the unit's own
		 unquantised distance along the route now, and the point is taken between samples, so it slides.

		 Making the distance itself a travel time was tried and reverted.  It cures the same wobble and
		 costs four times the stuck units: on forty maps, 4667 blocked unit-frames a match and 1.4 stuck
		 became 5159 and 5.4, and clamping the horizon where the route bends recovered none of it (29 of
		 39 seeds came back bit-identical).  A point fifty feet up the road is measured against ground
		 fifty feet up the road, and the narrow bit in between is not consulted by anybody.  The chassis
		 difference is dealt with in the aim filter below instead, where being wrong only costs lag. */
	const Real myAlong = m_corridor->alongOf( i, *myPos );
	const Real routeLen = m_corridor->length();

	Real wantAlong = myAlong + lookAhead;
	if (wantAlong > routeLen) wantAlong = routeLen;

	/* The point has to be in front of the unit, and being in front along the route is not the same
		 thing.  A unit shoved sideways out of a queue, or one carried past its own lookahead through a
		 corner, gets handed a point behind its own tracks, and the locomotor turns round and drives at
		 it: that is the rocking back and forth, and at a corner, where the route is already round the
		 bend, it is the rotating on the spot.  Walk forward until the point is ahead. */
	for (;;)
	{
		Real outLat = m_crowdLat;
		const Real leftToRun = routeLen - wantAlong;
		if (leftToRun < CROWD_TAPER_DIST)
			outLat *= leftToRun / CROWD_TAPER_DIST;	// the band closes on the destination, so the group arrives together

		m_corridor->pointAt( wantAlong, m_corridor->clampLatAt( wantAlong, outLat ), &goalPos );

		const Real ahead = (goalPos.x - myPos->x) * here.tan.x + (goalPos.y - myPos->y) * here.tan.y;
		if (ahead >= myR || wantAlong >= routeLen)
			break;

		wantAlong += PATHFIND_CELL_SIZE_F;
		if (wantAlong > routeLen) wantAlong = routeLen;
	}

	/* ---- and the aim goes through a filter of its own ----
		 Every rule above moves the point a little, the sample the point is taken from hops forward a
		 cell at a time, and the band's own width wobbles: the direction handed to the locomotor is
		 never still, and a locomotor steered at a direction that is never still is a metronome.  So
		 the direction is low-passed and small errors are ignored outright.

		 Only while cruising.  In a jam, behind somebody, giving way, moving over, or when the route
		 genuinely turns hard, the filter opens right up - lag costs more than twitch the moment the
		 unit is actually manoeuvring, and a smoothed answer to "there is a tank in front of you" is
		 the wrong kind of calm. */
	const Real aimDx = goalPos.x - myPos->x;
	const Real aimDy = goalPos.y - myPos->y;
	const Real aimDist = (Real)sqrt( aimDx * aimDx + aimDy * aimDy );
	if (aimDist > 0.01f)
	{
		const Real wantAim = ATan2( aimDy, aimDx );		// the table, not the runtime: this decides a position
		if (!m_crowdAimValid)
		{
			m_crowdAim = wantAim;
			m_crowdAimValid = TRUE;
		}

		const Real err = normalizeAngle( wantAim - m_crowdAim );
		const Bool urgent = pressing || touching || queued || giveWay || merged
												|| (blocker != NULL && blockerGap < myR)
												|| fabs( err ) > 1.0f;

		/* One gain is the wrong gain for two chassis.  The steering point sits a fixed distance in
			 front - two cells and a body, about thirty feet - which a heavy tank at 25 covers in forty
			 frames and a scout at 90 in ten.  Pure pursuit goes unstable when the unit eats its own
			 lookahead faster than the wheel settles: the light thing overshoots, the error changes sign,
			 it oversteers back, and that is the head shaking players see on fast vehicles and never on
			 heavy ones.  So the cruising gain is damped by how much of the aim distance the chassis
			 covers in half a second.  It is a lag, not a different point to drive at: nothing aims
			 anywhere it was not already aiming, which is what the horizon experiment got wrong. */
		Real gain = urgent ? CROWD_AIM_URGENT : CROWD_AIM_CRUISE;
		if (!urgent)
		{
			const Real travel = speed * CROWD_LOOK_FRAMES;
			if (travel > aimDist)
			{
				gain *= aimDist / travel;
				if (gain < CROWD_AIM_SLOWEST) gain = CROWD_AIM_SLOWEST;
			}
		}
		m_crowdAim = normalizeAngle( m_crowdAim + err * gain );

		if (!urgent && fabs( normalizeAngle( m_crowdAim - self->getOrientation() ) ) < CROWD_AIM_DEAD)
			m_crowdAim = self->getOrientation();		// two degrees is not worth a steering input

		goalPos.x = myPos->x + Cos( m_crowdAim ) * aimDist;
		goalPos.y = myPos->y + Sin( m_crowdAim ) * aimDist;
	}

	goalPos.z = TheTerrainLogic->getGroundHeight( goalPos.x, goalPos.y );

	if (cap < speed)
		speed = cap;

	if (TheGlobalData->m_showLanes && (now % LOGICFRAMES_PER_SECOND) == 0)
	{
		const char *mode = pressing ? "press" : (giveWay ? "yield" : (merged ? "merge"
											 : (queued ? "brake" : (blocker != NULL ? "pass" : "free"))));
		DEBUG_LOG(("SHOWLANES crowd: unit %d %s sample %d lat %.1f band %.1f/%.1f queued %d stuck %d rank %d speed %.2f\n",
			self->getID(), mode, i, m_crowdLat, here.left, here.right, m_crowdQueued, m_crowdStuck, rank, speed));
	}
}

//-------------------------------------------------------------------------------------------------
/**
 * This is used by the internal move to state to indicate that a move started.
 */
void AIUpdateInterface::friend_startingMove(void) 
{
	m_movementComplete = FALSE; // we aren't finished moving.
	m_isMoving = TRUE;
	m_blockedFrames = 0;
	m_isBlockedAndStuck = FALSE;
}

//-------------------------------------------------------------------------------------------------
/**
 * This is used by the internal move to state to indicate that a move completed.
 */
void AIUpdateInterface::friend_endingMove()
{
	m_movementComplete = TRUE;
	m_isMoving = FALSE;
}

//-------------------------------------------------------------------------------------------------
/**
 * This is used by the jetai to set a specific path.
 */
void AIUpdateInterface::friend_setPath(Path *path)
{
	destroyPath();
	m_path = path;
}

//-------------------------------------------------------------------------------------------------
/**
 * This is used by the guard tunnel network state to set a target object.
 */
void AIUpdateInterface::friend_setGoalObject(Object *obj)
{
	Bool locked = getStateMachine()->isLocked();
	getStateMachine()->unlock();
	getStateMachine()->setGoalObject(obj);
	if (locked) {
		getStateMachine()->lock("Friend_setGlobalObject re-locking");
	}
}

//-------------------------------------------------------------------------------------------------
/** Is there a path at all that exists from us to the destination location */
//-------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::isPathAvailable( const Coord3D *destination ) const
{
	
	// sanity
	if( destination == NULL )
		return FALSE;

	const Coord3D *myPos = getObject()->getPosition();

	return TheAI->pathfinder()->clientSafeQuickDoesPathExist( m_locomotorSet, myPos, destination );

}  // end isPathAvailable

//-------------------------------------------------------------------------------------------------
/** Is there a path (computed using the less accurate but quick method )
	* at all that exists from us to the destination location */
//-------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::isQuickPathAvailable( const Coord3D *destination ) const
{
	
	// sanity
	if( destination == NULL )
		return FALSE;

	const Coord3D *myPos = getObject()->getPosition();

	return TheAI->pathfinder()->clientSafeQuickDoesPathExistForUI( m_locomotorSet, myPos, destination );

}  // end isQuickPathAvailable




//-------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::isValidLocomotorPosition(const Coord3D* pos) const
{
	return TheAI->pathfinder()->validMovementPosition( getObject()->getCrusherLevel()>0, getObject()->getLayer(), m_locomotorSet, pos );
}

//-------------------------------------------------------------------------------------------------
DECLARE_PERF_TIMER(doLocomotor)
/**
 * Compute drive forces
 */
UpdateSleepTime AIUpdateInterface::doLocomotor( void )
{
	USE_PERF_TIMER(doLocomotor)

	if (getObject()->isKindOf(KINDOF_IMMOBILE))
		return UPDATE_SLEEP_FOREVER;

	chooseGoodLocomotorFromCurrentSet();

	if (m_isBlocked)
	{
		++m_blockedFrames;
		// this is the only place a blocked unit is counted once per frame rather than once per
		// collision pair, so it is where the traffic-jam number for the headless run comes from
		Pathfinder::bumpBlockedFrame( m_isBlockedAndStuck );

		/* And it is where the traffic map is written.  A unit that is stopped is the definition of
			 traffic; a unit that is merely slow is not, and neither is a unit parked because it has
			 arrived.  The cell it is standing in gets more expensive for about a second, which is
			 long enough for whoever repaths next to be handed a way round the line instead of a
			 place in it. */
		if (!TheGlobalData->m_noFlowPath)
		{
			Int trafficRadius = 0;
			Bool trafficCenter = true;
			TheAI->pathfinder()->getRadiusAndCenter(getObject(), trafficRadius, trafficCenter);
			TheAI->pathfinder()->noteTraffic(getObject()->getPosition(), trafficRadius);
		}
	}
	else
	{
		m_blockedFrames = 0;
	}

	/* Re-stamp where this unit expects to be over the next few seconds, once a second, offset by
		 its own id so the whole army does not do it on one frame.  Always from where the unit
		 actually is - a claim made a second ago against a plan that has since been held up is
		 exactly the claim that would send somebody else round a crossing that is no longer there. */
	if (!TheGlobalData->m_noFlowPath && getPath() != NULL)
	{
		UnsignedInt stagger = TheGameLogic->getFrame() + (UnsignedInt)getObject()->getID();
		if ((stagger % PF_CLAIM_REFRESH_FRAMES) == 0)
			TheAI->pathfinder()->claimPathTiming(getObject(), getPath());
	}

	/* The lane across the route is taken the first frame the unit drives on a new route, not when
		 the route is built: a path can be handed over several frames ahead of the move actually
		 starting, and the group that ordered it may still be handing out lanes.

		 There is no drift back to the middle.  The first version had one, on the theory that a lane
		 should be a response to traffic and not a permanent kink, and it was the whole reason a
		 group kept collapsing back into single file - it pulled every unit at 0.008 a frame towards
		 the same line.  The taper over the last PF_LANE_TAPER_CELLS closes the band on arrival,
		 which is the only place the spread actually has to go away. */
	if (!m_laneFractionValid && getPath() != NULL)
		seedLaneFraction();

	const Bool traceWasBlocked = m_isBlocked;	// -tracemove: the flag is cleared on the next line
	m_isBlocked = FALSE;

	Bool blocked = m_blockedFrames > 0;
	Bool requiresConstantCalling = TRUE;	// assume the worst.

	if (m_curLocomotor)
	{
		m_curLocomotor->setPhysicsOptions(getObject());

		if (isAiInDeadState() && !m_curLocomotor->getLocomotorWorksWhenDead())
		{
			// now it's over, I'm dead, and I haven't done anything that I want,
			// or, I'm still alive, and there's nothing I want to do
		}
		else
		{
			switch (m_locomotorGoalType)
			{
				case POSITION_EXPLICIT:
					{
						Real speed = m_desiredSpeed;
						Real myMaxSpeed = m_curLocomotor->getMaxSpeedForCondition(getObject()->getBodyModule()->getDamageState());
						if( speed == FAST_AS_POSSIBLE || speed > myMaxSpeed )
							speed = myMaxSpeed;
						m_curLocomotor->locoUpdate_moveTowardsPosition(getObject(), 
							m_locomotorGoalData, 0.0f, speed, &blocked);
						m_doFinalPosition = FALSE;
					}
					break;

				case POSITION_ON_PATH:
					{	 
						if (!getPath())
						{
							if (m_waitingForPath) 
							{
								return UPDATE_SLEEP_FOREVER;  // Can't move till we get our path.
							}
							DEBUG_LOG(("Dead %d, obj %s %x\n", isAiInDeadState(), getObject()->getTemplate()->getName().str(), getObject()));
#ifdef STATE_MACHINE_DEBUG
							DEBUG_LOG(("Waiting %d, state %s\n", m_waitingForPath, getStateMachine()->getCurrentStateName().str()));
							m_stateMachine->setDebugOutput(1);
#endif
							DEBUG_CRASH(("must have a path here (doLocomotor)"));
							break;
						}
						Coord3D goalPos;
						Real onPathDistToGoal;
						if (!isDoingGroundMovement()) 
						{
							// airborne locomotor.  Get the goal and distance direct to the goal, don't consider obstacles.
							onPathDistToGoal = getPath()->computeFlightDistToGoal(getObject()->getPosition(), goalPos);
						} 
						else 
						{
							// Compute the actual goal position along the path to move towards.  Consider
							// obstacles, and follow the intermediate path points.
							ClosestPointOnPathInfo info;
							CRCDEBUG_LOG(("AIUpdateInterface::doLocomotor() - calling computePointOnPath() for %s\n",
								DescribeObject(getObject()).str()));
							getPath()->computePointOnPath(getObject(), m_locomotorSet, *getObject()->getPosition(), info);
							onPathDistToGoal = info.distAlongPath;
							goalPos = info.posOnPath;
							// layer is a possible bridge in the path.  Check & set the layer if applicable.
							TheAI->pathfinder()->updateLayer(getObject(), info.layer);
						}
				 
						Real speed = m_desiredSpeed;
						Real myMaxSpeed = m_curLocomotor->getMaxSpeedForCondition(getObject()->getBodyModule()->getDamageState());
						if( speed == FAST_AS_POSSIBLE || speed > myMaxSpeed )
							speed = myMaxSpeed;

						if (blocked && speed>m_curMaxBlockedSpeed) 
						{
							speed = m_curMaxBlockedSpeed;
							if (m_bumpSpeedLimit>speed) {
								m_bumpSpeedLimit = speed;
							}
							m_bumpSpeedLimit *= 0.95f;
							speed = m_bumpSpeedLimit;
						} 
						else 
						{
							blocked = FALSE;
							if (m_bumpSpeedLimit<FAST_AS_POSSIBLE) {
								if (m_bumpSpeedLimit<speed*0.2f) {
									m_bumpSpeedLimit = speed*0.2f;
								}
								m_bumpSpeedLimit *= 1.05f;
							}
							if (speed>m_bumpSpeedLimit) {
								speed = m_bumpSpeedLimit;
							}
						}

						/* -crowd hangs here, on the two numbers about to be handed to the locomotor.  The
							 point on the route has already been worked out; the crowd model moves it sideways
							 across the width of the road and takes speed off for whatever is in the way.
							 Ground movement only - an aircraft has no road and no traffic. */
						if (TheGlobalData->m_crowdModel && isDoingGroundMovement())
							crowdSteer(goalPos, speed);

						m_curLocomotor->locoUpdate_moveTowardsPosition(getObject(), goalPos,
							onPathDistToGoal+getPathExtraDistance(), speed, &blocked);

						m_doFinalPosition = FALSE;
					}
					break;

				case ANGLE:
					{
						m_curLocomotor->locoUpdate_moveTowardsAngle(getObject(), m_locomotorGoalData.x);
						m_doFinalPosition = FALSE;
					}
					break;

				case NONE:
					{
						if (m_doFinalPosition) 
						{
							Coord3D pos = *getObject()->getPosition();
							Bool onGround = !getObject()->isAboveTerrain() && getObject()->getLayer() == LAYER_GROUND;
							Real dx = m_finalPosition.x - pos.x;
							Real dy = m_finalPosition.y - pos.y;
							Real dSqr = dx*dx+dy*dy;
							const Real DARN_CLOSE = 0.25f;
							if (dSqr < DARN_CLOSE) 
							{
								m_doFinalPosition = FALSE; 
								if (onGround)
									m_finalPosition.z = TheTerrainLogic->getGroundHeight( m_finalPosition.x, m_finalPosition.y );
								else
									m_finalPosition.z = pos.z;
								getObject()->setPosition(&m_finalPosition);
							} 
							else 
							{
								Real dist = sqrtf(dSqr);
								if (dist<1) dist = 1;
								pos.x += 2*PATHFIND_CELL_SIZE_F*dx/(dist*LOGICFRAMES_PER_SECOND);
								pos.y += 2*PATHFIND_CELL_SIZE_F*dy/(dist*LOGICFRAMES_PER_SECOND);
								if (onGround)
									pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
								getObject()->setPosition(&pos);
							}
						}
						requiresConstantCalling = m_curLocomotor->locoUpdate_maintainCurrentPosition(getObject());
					}
					break;
			}
		}
		
		if (!blocked && m_blockedFrames>1) 
		{
			m_blockedFrames = 1;
		}

		// After our movement for the frame, update our AirborneTarget flag.
		if(getObject()->getHeightAboveTerrain() > m_curLocomotor->getAirborneTargetingHeight() )
			getObject()->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_AIRBORNE_TARGET ) );
		else
			getObject()->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_AIRBORNE_TARGET ) );

		// before the ceiling is thrown away for the frame - it is the value the trace is about
		AIUpdate_traceMove( getObject(), traceWasBlocked, m_blockedFrames,
			m_desiredSpeed,
			m_curLocomotor->getMaxSpeedForCondition(getObject()->getBodyModule()->getDamageState()),
			m_curMaxBlockedSpeed, m_bumpSpeedLimit, isWaitingForPath(), getPath() != NULL,
			m_isBlockedAndStuck );

		m_curMaxBlockedSpeed = FAST_AS_POSSIBLE;
	}

	if (m_curLocomotor != NULL
			&& m_locomotorGoalType == NONE
			&& m_doFinalPosition == FALSE
			&& m_isBlocked == FALSE
			&& requiresConstantCalling == FALSE)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	else
	{
		return UPDATE_SLEEP_NONE;
	}

}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::setLocomotorGoalPositionOnPath()
{
	m_locomotorGoalType = POSITION_ON_PATH;
	m_locomotorGoalData.zero();
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::setLocomotorGoalPositionExplicit(const Coord3D& newPos)
{
	m_locomotorGoalType = POSITION_EXPLICIT;
	m_locomotorGoalData = newPos;
#ifdef _DEBUG
if (_isnan(m_locomotorGoalData.x) || _isnan(m_locomotorGoalData.y) || _isnan(m_locomotorGoalData.z))
{
	DEBUG_CRASH(("NAN in setLocomotorGoalPositionExplicit"));
}
#endif
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::setLocomotorGoalOrientation(Real angle)
{
	m_locomotorGoalType = ANGLE;
	m_locomotorGoalData.x = angle;
#ifdef _DEBUG
if (_isnan(m_locomotorGoalData.x) || _isnan(m_locomotorGoalData.y) || _isnan(m_locomotorGoalData.z))
{
	DEBUG_CRASH(("NAN in setLocomotorGoalOrientation"));
}
#endif
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::setLocomotorGoalNone()
{
	m_locomotorGoalType = NONE;
}

//-------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::isDoingGroundMovement(void) const
{
  
  if (getObject()->isDisabledByType( DISABLED_UNMANNED ) 
   && getObject()->isKindOf( KINDOF_PRODUCED_AT_HELIPAD ) )
  {
    return TRUE; // an unmanned helicopter gets grounded, eventually.
  }

	if (m_locomotorSet.getValidSurfaces() == LOCOMOTORSURFACE_AIR) 
	{
		return FALSE;  // air only loco.
	}

	if (m_curLocomotor == NULL) 
	{
		return FALSE;	// No loco, so we aren't moving.
	}

	// Cur loco is air, so not ground.
	if (m_curLocomotor->getLegalSurfaces() & LOCOMOTORSURFACE_AIR) 
	{
		return FALSE; 
	}

	// We are held, so not moving on ground.
	if( getObject()->isDisabledByType( DISABLED_HELD ) ) 
	{
		return FALSE;
	}

	// if we're airborne and "allowed to fall", we are probably deliberately in midair
	// due to rappel or accident...
	const PhysicsBehavior* physics = getObject()->getPhysics();
	if (getObject()->isAboveTerrain() && physics != NULL && physics->getAllowToFall())
	{
		return FALSE;
	}

	// After all exceptions, we must be doing ground movement.
	//DEBUG_ASSERTLOG(getObject()->isSignificantlyAboveTerrain(), ("Object %s is significantly airborne but also doing ground movement. What?\n",getObject()->getTemplate()->getName().str()));
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Some aircraft (comanche in particular, which hover) shouldn't stack destinations.
Others, like missles, should stack destinations.  AdjustDestination in pathfinder unstacks
destinations, and this routine identifies non-ground units that should unstack. */

Bool AIUpdateInterface::isAircraftThatAdjustsDestination(void) const
{
	if (m_curLocomotor == NULL) 
	{
		return FALSE;	// No loco, so we aren't moving.
	}

	if (m_curLocomotor->getAppearance() == LOCO_HOVER) 
	{
		return TRUE;	// Hover adjusts.
	}
	if (m_curLocomotor->getAppearance() == LOCO_WINGS)
	{
		return TRUE; // wings adjusts.
	}
	if (m_curLocomotor->getAppearance() == LOCO_THRUST)
	{
		return FALSE; // thrust doesn't adjust.
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::getTreatAsAircraftForLocoDistToGoal() const
{
	Bool treatAsAircraft = !isDoingGroundMovement();
	if (getPathExtraDistance() > PATHFIND_CLOSE_ENOUGH) 
	{
		// We are following a waypoint or other multiple point path, so use the "easy" success criteria.
		treatAsAircraft = TRUE;
	}
	if (m_curLocomotor && m_curLocomotor->getAppearance() == LOCO_HOVER) 
	{
		// Hovercrafts are very sloppy.  So use aircraft tests for distance to goal.  jba.
		treatAsAircraft = TRUE;
	}
	return treatAsAircraft;
}

//-------------------------------------------------------------------------------------------------
Real AIUpdateInterface::getLocomotorDistanceToGoal() 
{
	switch (m_locomotorGoalType)
	{
		case POSITION_EXPLICIT:
			DEBUG_CRASH(("not yet implemented"));
			return 0.0f;

		case POSITION_ON_PATH:
			if (!getPath()) 
			{
				DEBUG_CRASH(("must have a path here (getLocomotorDistanceToGoal)"));
				return 0.0f;
			}
			else if (!m_curLocomotor) 
			{
				//DEBUG_LOG(("no locomotor here, so no dist. (this is ok.)\n"));
				return 0.0f;
			}	
			else if( m_curLocomotor->isCloseEnoughDist3D() || getObject()->isKindOf(KINDOF_PROJECTILE))
			{
				const Object *me = getObject();
				const Coord3D *dest = getGoalPosition();
				if (m_path->getLastNode()) {
					dest = m_path->getLastNode()->getPosition();
				}
				Real distance = ThePartitionManager->getDistanceSquared( me, dest, FROM_CENTER_3D );
				return sqrt( distance );// Other paths return dots of normalized vectors, so one sqrt ain't so bad
			}
			else 
			{
				Coord3D goalPos;
				Bool treatAsAircraft = getTreatAsAircraftForLocoDistToGoal();
				Real dist;
				if (treatAsAircraft) 
				{
					// airborne locomotor.  Get the goal and distance direct to the goal, don't consider obstacles.
					dist =  getPath()->computeFlightDistToGoal(getObject()->getPosition(), goalPos);
				}	else {
					// Ground based locomotor.
					ClosestPointOnPathInfo info;
					CRCDEBUG_LOG(("AIUpdateInterface::getLocomotorDistanceToGoal() - calling computePointOnPath() for object %d\n", getObject()->getID()));
					getPath()->computePointOnPath(getObject(), m_locomotorSet, *getObject()->getPosition(), info);
					goalPos = info.posOnPath;	 
					dist = info.distAlongPath;
				}
				if (m_path->getLastNode()) {
					goalPos = *m_path->getLastNode()->getPosition();
				}
				// We are trying to get to goal.  So, 
				// If the actual distance is farther, then use the actual distance so we get there.
				Real dx = goalPos.x - getObject()->getPosition()->x;
				Real dy = goalPos.y - getObject()->getPosition()->y;
				Real distSqr = dx*dx + dy*dy;
				
				if (treatAsAircraft) 
				{
					if (sqr(dist) > distSqr) 
					{
						return sqrt(distSqr);
					}
					else
					{
						return dist; 
					}
				}

				if (dist<PATHFIND_CELL_SIZE_F || sqr(dist) < distSqr)
					return sqrtf(distSqr);
				else
					return dist;			 

			}

		case ANGLE:
		case NONE:
			// If it isn't a positional goal, we are there already.
			return 0.0f;
	}

	return 0.0f;
}
 

/**
 * Catch up with the rest of the team.
 */
void AIUpdateInterface::joinTeam( void )
{
	// the dead don't listen very well
	if (isAiInDeadState())
		return;

	if (getObject()->isMobile() == FALSE)
		return;

	chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	getStateMachine()->clear();
	getStateMachine()->setGoalWaypoint(NULL);
	Object *obj = getObject();
	Object *other = NULL;
	Team *team = obj->getTeam();
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *anObj = iter.cur();
		if (!anObj) 
		{
			continue;
		}
		if (obj == anObj) 
		{
			// it's us.
			continue;
		}	
		else if (anObj->getAI()) 
		{
			if( !anObj->isDisabledByType( DISABLED_HELD ) ) 
			{
				other = anObj;
				break;
			}
		}
	}
	if (other) {
		AIUpdateInterface* ai = other->getAI();
		if (ai->isIdle()) {
			aiMoveToPosition(other->getPosition(), CMD_FROM_AI);
			return;
		}
		if (ai->getGoalObject()) {
			getStateMachine()->setGoalObject(ai->getGoalObject());
		} else {
			getStateMachine()->setGoalPosition(ai->getGoalPosition());
		}
		StateID	state = getCurrentStateID();
		setLastCommandSource( CMD_FROM_AI );
		// Match the state.
		getStateMachine()->setState( state );
	}

}  // end joinTeam

//-------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::isAllowedToRespondToAiCommands(const AICommandParms* parms) const
{
	// the dead don't listen very well
	// (unless they are seeking to feed on the brains of the living)
	// [urrr, need brains]
	if (getObject()->isEffectivelyDead())
		return FALSE;

	// We're catching the sleep mood here. AI Units that are asleep actually ignore all commands.
	// (See the AI Mood matrix for more info)
	UnsignedInt moodParms = getMoodMatrixValue();
	if ((moodParms & MM_Controller_AI) && (moodParms & MM_Mood_Sleep) && (parms->m_cmd != AICMD_MOVE_TO_POSITION_EVEN_IF_SLEEPING))
		return FALSE;

  const AIUpdateModuleData *data = getAIUpdateModuleData();

  Bool forbidden = data->m_forbidPlayerCommands;

  if ( parms->m_cmdSource == CMD_FROM_PLAYER && forbidden )
    return FALSE; 
  // THIS IS JUST FOR THE SPECTREGUNSHIP FOR NOW... 
  // IT LOCKS OUT USER INPUT, 
  // ALLOWING ONLY THE SPECTREUPDATE TO COMMAND IT VIA CMD_FROM_AI
  // AUTHOR, LORENZEN... 5/15/03

	//
	// A unit that is still walking the exit path out of the thing that produced it finishes that
	// step before it will listen to anyone.  Taking an order mid-doorway leaves it turning around
	// inside the building's footprint, which blocks the next unit off the line and, with
	// setCanPathThroughUnits on for the exit path, lets it be shoved back through the wall.
	// Aircraft are left alone: their "exit path" is the taxi and takeoff run off a helipad or
	// airfield, which the player is expected to be able to redirect.
	//
	if ( parms->m_cmdSource == CMD_FROM_PLAYER
			 && getStateMachine()->getCurrentStateID() == AI_FOLLOW_EXITPRODUCTION_PATH
			 && isDoingGroundMovement() )
		return FALSE;

	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::aiDoCommand(const AICommandParms* parms)
{
	if (!isAllowedToRespondToAiCommands(parms))
		return;

	// Any order at all replaces the trip to the producer's rally point - except the exit path itself,
	// which is the leg that precedes it.
	if (parms->m_cmd != AICMD_FOLLOW_EXITPRODUCTION_PATH)
		m_hasExitProductionRallyPoint = FALSE;

#ifdef ALLOW_SURRENDER
	// surrendered items have very limited options, and only via AI cmds
	if (isSurrendered())
	{
		if (parms->m_cmdSource != CMD_FROM_AI)
			return;

		switch (parms->m_cmd)
		{
			case AICMD_MOVE_TO_POSITION:
			case AICMD_MOVE_TO_OBJECT:
			case AICMD_IDLE:
			case AICMD_ENTER:
			case AICMD_EXIT:
				break;

			default:
				DEBUG_LOG(("ignoring ai cmd due to surrender condition"));
				return;
		}
	}
#endif

  
	switch (parms->m_cmd)
	{
		case AICMD_MOVE_TO_POSITION:
		case AICMD_MOVE_TO_POSITION_EVEN_IF_SLEEPING:
			privateMoveToPosition(&parms->m_pos, parms->m_cmdSource);
			break;
		case AICMD_MOVE_TO_OBJECT:
			privateMoveToObject(parms->m_obj, parms->m_cmdSource);
			break;
		case AICMD_TIGHTEN_TO_POSITION:
			privateTightenToPosition(&parms->m_pos, parms->m_cmdSource);
			break;
		case AICMD_MOVE_TO_POSITION_AND_EVACUATE:
			privateMoveToAndEvacuate(&parms->m_pos, parms->m_cmdSource);
			break;
		case AICMD_MOVE_TO_POSITION_AND_EVACUATE_AND_EXIT:
			privateMoveToAndEvacuateAndExit(&parms->m_pos, parms->m_cmdSource);
			break;
		case AICMD_IDLE:
			privateIdle(parms->m_cmdSource);
			break;
		case AICMD_FOLLOW_WAYPOINT_PATH:
			privateFollowWaypointPath(parms->m_waypoint, parms->m_cmdSource);
			break;
		case AICMD_FOLLOW_WAYPOINT_PATH_AS_TEAM:
			privateFollowWaypointPathAsTeam(parms->m_waypoint, parms->m_cmdSource);
			break;
		case AICMD_FOLLOW_WAYPOINT_PATH_EXACT:
			privateFollowWaypointPathExact(parms->m_waypoint, parms->m_cmdSource);
			break;
		case AICMD_FOLLOW_WAYPOINT_PATH_AS_TEAM_EXACT:
			privateFollowWaypointPathAsTeamExact(parms->m_waypoint, parms->m_cmdSource);
			break;
		case AICMD_FOLLOW_PATH:
			privateFollowPath(&parms->m_coords, parms->m_obj, parms->m_cmdSource, FALSE);
			break;
		case AICMD_FOLLOW_PATH_APPEND:
			privateFollowPathAppend(&parms->m_pos, parms->m_cmdSource);
			break;
		case AICMD_FOLLOW_EXITPRODUCTION_PATH:
			privateFollowPath(&parms->m_coords, parms->m_obj, parms->m_cmdSource, TRUE);
			break;
		case AICMD_ATTACK_OBJECT:
			privateAttackObject(parms->m_obj, parms->m_intValue, parms->m_cmdSource);
			break;
		case AICMD_FORCE_ATTACK_OBJECT:
			privateForceAttackObject(parms->m_obj, parms->m_intValue, parms->m_cmdSource);
			break;
		case AICMD_GUARD_RETALIATE:
			privateGuardRetaliate( parms->m_obj, &parms->m_pos, parms->m_intValue, parms->m_cmdSource );
			break;
		case AICMD_ATTACK_TEAM:
			privateAttackTeam(parms->m_team, parms->m_intValue, parms->m_cmdSource);
			break;
		case AICMD_ATTACK_POSITION:
			privateAttackPosition(&parms->m_pos, parms->m_intValue, parms->m_cmdSource);
			break;
		case AICMD_ATTACKMOVE_TO_POSITION:
			privateAttackMoveToPosition(&parms->m_pos, parms->m_intValue, parms->m_cmdSource);
			break;
		case AICMD_ATTACKFOLLOW_WAYPOINT_PATH:
			privateAttackFollowWaypointPath(parms->m_waypoint, parms->m_intValue, FALSE, parms->m_cmdSource);
			break;
		case AICMD_ATTACKFOLLOW_WAYPOINT_PATH_AS_TEAM:
			privateAttackFollowWaypointPath(parms->m_waypoint, parms->m_intValue, TRUE, parms->m_cmdSource);
			break;
		case AICMD_HUNT:
			privateHunt(parms->m_cmdSource);
			break;
		case AICMD_ATTACK_AREA:
			privateAttackArea(parms->m_polygon, parms->m_cmdSource);
			break;
		case AICMD_REPAIR:
			privateRepair(parms->m_obj, parms->m_cmdSource);
			break;
#ifdef ALLOW_SURRENDER
		case AICMD_PICK_UP_PRISONER:
			privatePickUpPrisoner( parms->m_obj, parms->m_cmdSource );
			break;
		case AICMD_RETURN_PRISONERS:
			privateReturnPrisoners( parms->m_obj, parms->m_cmdSource );
			break;
#endif
		case AICMD_RESUME_CONSTRUCTION:
			privateResumeConstruction(parms->m_obj, parms->m_cmdSource);
			break;
		case AICMD_GET_HEALED:
			privateGetHealed(parms->m_obj, parms->m_cmdSource);
			break;
		case AICMD_GET_REPAIRED:
			privateGetRepaired(parms->m_obj, parms->m_cmdSource);
			break;
		case AICMD_ENTER://///////////////////////////////////////////////////////////////
			privateEnter(parms->m_obj, parms->m_cmdSource);
			break;
		case AICMD_DOCK:
			privateDock(parms->m_obj, parms->m_cmdSource);
			break;
		case AICMD_EXIT:////////////////////////////////////////////////////////////////////
			privateExit(parms->m_obj, parms->m_cmdSource);
			break;
		case AICMD_EXIT_INSTANTLY://///////////////////////////////////////////////////////
			privateExitInstantly( parms->m_obj, parms->m_cmdSource );
			break;
		case AICMD_EVACUATE://///////////////////////////////////////////////////////////
			privateEvacuate(parms->m_intValue, parms->m_cmdSource);
			break;
		case AICMD_EVACUATE_INSTANTLY:////////////////////////////////////////////////////
			privateEvacuateInstantly( parms->m_intValue, parms->m_cmdSource );
			break;
		case AICMD_EXECUTE_RAILED_TRANSPORT:
			privateExecuteRailedTransport( parms->m_cmdSource );
			break;
		case AICMD_GO_PRONE:
			privateGoProne(&parms->m_damage, parms->m_cmdSource);
			break;
		case AICMD_GUARD_POSITION:
		{
			//Kris: Aug 18, 2003 -- If you were retaliating and ordered to enter guard mode, 
			//the state needs to be cleared before doing so or else we leave the state too
			//late and clear data AFTER we go into the new guard mode causing units to 
			//move to zero (bottom left corner).
			AIStateMachine *state = getStateMachine();
			if( state && state->getCurrentStateID() == AI_GUARD_RETALIATE )
			{
				state->clear();
			}
			//end

			privateGuardPosition(&parms->m_pos, (GuardMode)parms->m_intValue, parms->m_cmdSource);
			break;
		}
		case AICMD_GUARD_OBJECT:
		{
			//Kris: Aug 18, 2003 -- If you were retaliating and ordered to enter guard mode, 
			//the state needs to be cleared before doing so or else we leave the state too
			//late and clear data AFTER we go into the new guard mode causing units to 
			//move to zero (bottom left corner).
			AIStateMachine *state = getStateMachine();
			if( state && state->getCurrentStateID() == AI_GUARD_RETALIATE )
			{
				state->clear();
			}
			//end

			privateGuardObject(parms->m_obj, (GuardMode)parms->m_intValue, parms->m_cmdSource);
			break;
		}
		case AICMD_GUARD_TUNNEL_NETWORK:
		{
			//Kris: Aug 18, 2003 -- If you were retaliating and ordered to enter guard mode, 
			//the state needs to be cleared before doing so or else we leave the state too
			//late and clear data AFTER we go into the new guard mode causing units to 
			//move to zero (bottom left corner).
			AIStateMachine *state = getStateMachine();
			if( state && state->getCurrentStateID() == AI_GUARD_RETALIATE )
			{
				state->clear();
			}
			//end

			privateGuardTunnelNetwork((GuardMode)parms->m_intValue, parms->m_cmdSource);
			break;
		}
		case AICMD_GUARD_AREA:
		{
			//Kris: Aug 18, 2003 -- If you were retaliating and ordered to enter guard mode, 
			//the state needs to be cleared before doing so or else we leave the state too
			//late and clear data AFTER we go into the new guard mode causing units to 
			//move to zero (bottom left corner).
			AIStateMachine *state = getStateMachine();
			if( state && state->getCurrentStateID() == AI_GUARD_RETALIATE )
			{
				state->clear();
			}
			//end

			privateGuardArea(parms->m_polygon, (GuardMode)parms->m_intValue, parms->m_cmdSource);
			break;
		}
		case AICMD_HACK_INTERNET:
			privateHackInternet( parms->m_cmdSource );
			break;
		case AICMD_FACE_OBJECT:
			privateFaceObject( parms->m_obj, parms->m_cmdSource );
			break;
		case AICMD_FACE_POSITION:
			privateFacePosition( &parms->m_pos, parms->m_cmdSource );
			break;
		case AICMD_RAPPEL_INTO:
			privateRappelInto( parms->m_obj, parms->m_pos, parms->m_cmdSource );
			break;
		case AICMD_COMBATDROP:
			privateCombatDrop( parms->m_obj, parms->m_pos, parms->m_cmdSource );
			break;
		case AICMD_COMMANDBUTTON:
			privateCommandButton( parms->m_commandButton, parms->m_cmdSource );
			break;
		case AICMD_COMMANDBUTTON_OBJ:
			privateCommandButtonObject( parms->m_commandButton, parms->m_obj, parms->m_cmdSource );
			break;
		case AICMD_COMMANDBUTTON_POS:
			privateCommandButtonPosition( parms->m_commandButton, &parms->m_pos, parms->m_cmdSource );
			break;
		case AICMD_WANDER:
			privateWander( parms->m_waypoint, parms->m_cmdSource );
			break;
		case AICMD_WANDER_IN_PLACE:
			privateWanderInPlace(parms->m_cmdSource);
			break;
		case AICMD_PANIC:
			privatePanic( parms->m_waypoint, parms->m_cmdSource );
			break;
		case AICMD_BUSY:
			privateBusy( parms->m_cmdSource );
			break;
		case AICMD_MOVE_AWAY_FROM_UNIT:
			privateMoveAwayFromUnit( parms->m_obj, parms->m_cmdSource );
			break;
		default:
			DEBUG_CRASH(("unhandled AI command!"));
			break;
	}
}


//-------------------------------------------------------------------------------------------------
// AI Command Interface implementation for AIUpdateInterface
//

/**
 * Move to given position(s)
 */
void AIUpdateInterface::privateMoveToPosition( const Coord3D *pos, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE) 
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	if (!isIdle() && cmdSource == CMD_FROM_AI) {
		// This is an internally generated move to, and we are in a non-idle state. [8/19/2003]
		// Our state could be the source of this command, so 
		// Move for 20 seconds [8/19/2003]
		// Things like attack state don't take kindly to being booted out unceremoniously. jba. [8/19/2003]
		setGoalPositionClipped(pos, cmdSource);
		m_blockedFrames = 0;
		m_isBlocked = FALSE;
		m_isBlockedAndStuck = FALSE;
		getStateMachine()->setTemporaryState(AI_MOVE_TO, LOGICFRAMES_PER_SECOND * 20);
	} else {
		// Normal user or script command, just do it. [8/19/2003]
		getStateMachine()->clear();
		setGoalPositionClipped(pos, cmdSource);
		m_blockedFrames = 0;
		m_isBlocked = FALSE;
		m_isBlockedAndStuck = FALSE;
		setLastCommandSource( cmdSource );
		getStateMachine()->setState( AI_MOVE_TO );
	}

}

//-------------------------------------------------------------------------------------------------
/**
 * Move to given object
 */
void AIUpdateInterface::privateMoveToObject( Object *obj, CommandSourceType cmdSource ) 
{
	// the dead don't listen very well
	if (m_isAiDead)
		return;

	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	
	getStateMachine()->clear();
	getStateMachine()->setGoalObject( obj );
	m_blockedFrames = 0;
	m_isBlocked = FALSE;
	m_isBlockedAndStuck = FALSE;
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_MOVE_TO );

}

//----------------------------------------------------------------------------------------
// Face a specified object -- succeed when facing
//----------------------------------------------------------------------------------------
void AIUpdateInterface::privateFaceObject( Object *obj, CommandSourceType cmdSource )
{
	if( !getObject()->isMobile() )
	{
		return;
	}

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	getStateMachine()->setGoalObject( obj );
	m_blockedFrames = 0;
	m_isBlocked = FALSE;
	m_isBlockedAndStuck = FALSE;
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_FACE_OBJECT );
}

//----------------------------------------------------------------------------------------
// Face a specified position -- succeed when facing
//----------------------------------------------------------------------------------------
void AIUpdateInterface::privateFacePosition( const Coord3D *pos, CommandSourceType cmdSource )
{
	if( !getObject()->isMobile() )
	{
		return;
	}

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	setGoalPositionClipped(pos, cmdSource);
	m_blockedFrames = 0;
	m_isBlocked = FALSE;
	m_isBlockedAndStuck = FALSE;
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_FACE_POSITION );
}

//----------------------------------------------------------------------------------------
// Rappel into target and devastate contents (if not empty).
// If target is null, rappel to ground.
//----------------------------------------------------------------------------------------
void AIUpdateInterface::privateRappelInto( Object *target, const Coord3D& pos, CommandSourceType cmdSource )
{

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	getStateMachine()->setGoalObject( target );
	setGoalPositionClipped(&pos, cmdSource);
	m_blockedFrames = 0;
	m_isBlocked = FALSE;
	m_isBlockedAndStuck = FALSE;
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_RAPPEL_INTO );
}


//----------------------------------------------------------------------------------------
/**
 * Move to given position(s)
 * If transportExits, transport returns and deletes itself.
 */
void AIUpdateInterface::privateMoveToAndEvacuate( const Coord3D *pos, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	setGoalPositionClipped(pos, cmdSource);
	m_blockedFrames = 0;
	m_isBlocked = FALSE;
	m_isBlockedAndStuck = FALSE;
	setLastCommandSource( cmdSource );

	m_stateMachine->setState( AI_MOVE_AND_EVACUATE );
}

//----------------------------------------------------------------------------------------
/**
 * Move to given position(s)
 * If transportExits, transport returns and deletes itself.
 */
void AIUpdateInterface::privateMoveToAndEvacuateAndExit( const Coord3D *pos, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	setGoalPositionClipped(pos, cmdSource);
	m_blockedFrames = 0;
	m_isBlocked = FALSE;
	m_isBlockedAndStuck = FALSE;
	setLastCommandSource( cmdSource );

	static NameKeyType key_DeliverPayloadAIUpdate = NAMEKEY("DeliverPayloadAIUpdate");
	DeliverPayloadAIUpdate *dp = (DeliverPayloadAIUpdate*)getObject()->findUpdateModule( key_DeliverPayloadAIUpdate );
	if( dp )
	{
		dp->deliverPayloadViaModuleData( pos );
	}
	else
	{
		getStateMachine()->setState( AI_MOVE_AND_EVACUATE_AND_EXIT);
	}

}

//----------------------------------------------------------------------------------------
/**
 * Enter idle state.
 */
void AIUpdateInterface::privateIdle(CommandSourceType cmdSource)
{
	if (getObject()->isKindOf(KINDOF_PROJECTILE))
		return;

	getStateMachine()->clear();
	getStateMachine()->setState( AI_IDLE );
	setLastCommandSource( cmdSource );

	ContainModuleInterface *contain = getObject()->getContain();
	if (contain)
	{
		const ContainedItemsList* items = contain->getContainedItemsList();
		if (items)
		{
			for (ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it)
			{
				Object* obj = *it;
				AIUpdateInterface* ai = obj ? obj->getAI() : NULL;
				if (ai)
					ai->aiIdle(cmdSource);
			}
		}
	}

}

//----------------------------------------------------------------------------------------
Bool AIUpdateInterface::isIdle() const
{
	const AIStateMachine *state = getStateMachine();
	if( state->getCurrentStateID() == AI_IDLE )
	{
		return TRUE;
	}
	return state->isInIdleState();
}

//----------------------------------------------------------------------------------------
Bool AIUpdateInterface::isAttacking() const
{
	return getStateMachine()->isInAttackState();
}

//----------------------------------------------------------------------------------------
//Definition of busy -- when explicitly in the busy state. Moving or attacking is not considered busy!
//----------------------------------------------------------------------------------------
Bool AIUpdateInterface::isBusy() const
{
	return getStateMachine()->isInBusyState();
}

//----------------------------------------------------------------------------------------
Bool AIUpdateInterface::isClearingMines() const
{
	// if we are attacking with an anti-mine weapon, we are clearing mines, regardless
	// of our target.

	if (!getObject()->testStatus(OBJECT_STATUS_IS_ATTACKING))
		return FALSE;

	const Weapon* weapon = getObject()->getCurrentWeapon();
	if (!weapon)
		return FALSE;

	if ((weapon->getAntiMask() & WEAPON_ANTI_MINE) == 0)
		return FALSE;

	return TRUE;
}

//----------------------------------------------------------------------------------------
/**
 * Take the shortest path towards pos in order to tighten up a formation
 */
void AIUpdateInterface::privateTightenToPosition( const Coord3D *pos, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;
	getStateMachine()->clear();
	getStateMachine()->setGoalObject( NULL );
	setGoalPositionClipped(pos, cmdSource);
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_MOVE_AND_TIGHTEN );
}
//----------------------------------------------------------------------------------------
/**
 * Is this moving out of the way of another unit.
 */
Bool AIUpdateInterface::isMovingAwayFrom(Object *obj)	 const
{
	ObjectID id = obj->getID();
	if (m_stateMachine->getTemporaryState() == AI_MOVE_OUT_OF_THE_WAY) {
		if (m_moveOutOfWay1 == id) return TRUE;
		if (m_moveOutOfWay2 == id) return TRUE; 
	}
	return FALSE;
}
//----------------------------------------------------------------------------------------
/**
 * Is this moving out of the way of another unit.
 */
Bool AIUpdateInterface::isMoving() const
{
	if (isIdle()) {
		return false;
	}
	if (m_locomotorGoalType != NONE) {
		return TRUE;
	}
	if (m_isMoving) {
		return TRUE;
	}
	return FALSE;
}

//----------------------------------------------------------------------------------------
/**
 * Move out of the way of another unit.
 */
void AIUpdateInterface::privateMoveAwayFromUnit( Object *unit, CommandSourceType cmdSource )
{
	// the dead don't listen very well
	if (isAiInDeadState() || (getObject()->isMobile() == FALSE) || !isAllowedToMoveAwayFromUnit()) 
	{
		return;
	}

	//
	// A queued AI command is re-issued a frame or more after it was made, and the object it names
	// can be gone by then - a hacker told to step aside while it is coming out of its hacking state
	// is the reproducible case.
	//
	if (unit == NULL)
		return;

	ObjectID id = unit->getID();
	if (m_stateMachine->getTemporaryState() == AI_MOVE_OUT_OF_THE_WAY) {
		if (m_moveOutOfWay1 == id) {
			if (m_isBlocked) {
				setIgnoreCollisionTime(LOGICFRAMES_PER_SECOND*2); // cheat for 2 seconds.
			}
			return;
		}
		if (m_moveOutOfWay2 == id) {
			if (m_isBlocked) {
				setIgnoreCollisionTime(LOGICFRAMES_PER_SECOND*2); // cheat for 2 seconds.
			}
			return;
		}
	}
	m_moveOutOfWay2 = m_moveOutOfWay1;
	m_moveOutOfWay1 = id;
	Object *obj2 = TheGameLogic->findObjectByID(m_moveOutOfWay2);
	Path *path2 = NULL;
	if (obj2 && obj2->getAI()) {
		path2 = obj2->getAI()->getPath();
	}

	Path* unitPath = NULL;
	if (unit && unit->getAI()) {
		unitPath = unit->getAI()->getPath();
	}
	if (unitPath == NULL) return;
	Path *newPath = TheAI->pathfinder()->getMoveAwayFromPath(getObject(), unit, unitPath, obj2, path2);
	if (newPath==NULL && !canPathThroughUnits())	{
		setCanPathThroughUnits(TRUE);
		newPath = TheAI->pathfinder()->getMoveAwayFromPath(getObject(), unit, unitPath, obj2, path2);
	}
		
	if (newPath) {
		destroyPath();
		m_path = newPath;
		wakeUpNow();
		m_stateMachine->setTemporaryState(AI_MOVE_OUT_OF_THE_WAY, 10*LOGICFRAMES_PER_SECOND);
		if (m_path) 
		{
	 		if( !getObject()->isKindOf(KINDOF_NO_COLLIDE))// If I don't collide with things, I don't need to tell them to get out of the way
				TheAI->pathfinder()->moveAllies(getObject(), m_path);
		}
	}

}

//----------------------------------------------------------------------------------------
/**
 * Start following the path from the given point
 */
void AIUpdateInterface::privateFollowWaypointPath( const Waypoint *way, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	getStateMachine()->setGoalWaypoint( way );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS );
}

//----------------------------------------------------------------------------------------
/**
 * Start following the path from the given point
 */
void AIUpdateInterface::privateFollowWaypointPathExact( const Waypoint *way, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	getStateMachine()->setGoalWaypoint( way );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS_EXACT );
}

//----------------------------------------------------------------------------------------
/**
 * Start following the path from the given point
 */
void AIUpdateInterface::privateFollowWaypointPathAsTeam( const Waypoint *way, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	getStateMachine()->setGoalWaypoint( way );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_FOLLOW_WAYPOINT_PATH_AS_TEAM );
}

//----------------------------------------------------------------------------------------
/**
 * Start following the path from the given point
 */
void AIUpdateInterface::privateFollowWaypointPathAsTeamExact( const Waypoint *way, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	getStateMachine()->setGoalWaypoint( way );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_FOLLOW_WAYPOINT_PATH_AS_TEAM_EXACT );
}

//----------------------------------------------------------------------------------------
void AIUpdateInterface::privateFollowPathAppend( const Coord3D *pos, CommandSourceType cmdSource )
{
	// We're adding a dynamic waypoint!
	Bool effectivelyMoving = isMoving() || isWaitingForPath();

	if (getAIStateType() == AI_FOLLOW_PATH && getStateMachine()->getGoalPathSize() > 0 && effectivelyMoving)
	{
		//We already have a path, so simply add the point to the end of it!
		getStateMachine()->addToGoalPath(pos);
	}
	else if (effectivelyMoving)
	{
		//Our unit is moving to a point already so simply add our waypoint after that point
		//and convert it to a waypoint command!
		std::vector<Coord3D> path;
		path.push_back( *getGoalPosition() );
		path.push_back( *pos );
		privateFollowPath( &path, NULL, cmdSource, false );
	}
	else
	{
		//Hopefully we're idle or doing something that doesn't require movement.
		std::vector<Coord3D> path;
		path.push_back( *pos );
		privateFollowPath( &path, NULL, cmdSource, false );
	}
}

//----------------------------------------------------------------------------------------
/**
 * Remember the rally point of the producer that just built us.  The exit path is only the step out
 * of the door; update() turns this into an attack move once that step is done.
 */
void AIUpdateInterface::friend_setExitProductionRallyPoint( const Coord3D *pos )
{
	m_exitProductionRallyPoint = *pos;
	m_hasExitProductionRallyPoint = TRUE;
}

//----------------------------------------------------------------------------------------
/**
 * Follow the path defined by the given array of points
 */
void AIUpdateInterface::privateFollowPath( const std::vector<Coord3D>* path, Object *ignoreObject, CommandSourceType cmdSource, Bool exitProduction )
{
	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	// clear current state machine
	getStateMachine()->clear();

	if (path->size()>0) {
		const Coord3D goal = (*path)[path->size()-1];
		getStateMachine()->setGoalPosition(&goal);
	}
	// set path info
	getStateMachine()->setGoalPath( path );


	// set the command source
	setLastCommandSource( cmdSource );

	ignoreObstacle(ignoreObject);

	// start us following
	getStateMachine()->setState( exitProduction ? AI_FOLLOW_EXITPRODUCTION_PATH : AI_FOLLOW_PATH );

}

//----------------------------------------------------------------------------------------
/**
 * Attack given object
 */
void AIUpdateInterface::privateAttackObject( Object *victim, Int maxShotsToFire, CommandSourceType cmdSource )
{
	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	if (!victim) 
	{
		// Hard to kill em if they're already dead.  jba
		return;
	}

	getStateMachine()->clear();
	getStateMachine()->setGoalObject( victim );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_ATTACK_OBJECT );

	// do this after setting it as the current state, as the max-shots-to-fire is reset in AttackState::onEnter()
	Weapon* weapon = getObject()->getCurrentWeapon();
	if (weapon)
		weapon->setMaxShotCount(maxShotsToFire);
}

//-----------------------------------------------------------------------------------------
void AIUpdateInterface::privateForceAttackObject( Object *victim, Int maxShotsToFire, CommandSourceType cmdSource )
{
	if (!victim) {
		return;
	}

	getStateMachine()->clear();
	getStateMachine()->setGoalObject( victim );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_FORCE_ATTACK_OBJECT );

	// do this after setting it as the current state, as the max-shots-to-fire is reset in AttackState::onEnter()
	Weapon* weapon = getObject()->getCurrentWeapon();
	if (weapon)
		weapon->setMaxShotCount(maxShotsToFire);
}

//-----------------------------------------------------------------------------------------
void AIUpdateInterface::privateGuardRetaliate( Object *victim, const Coord3D *pos, Int maxShotsToFire, CommandSourceType cmdSource )
{
	if (!victim) {
		return;
	}

	getStateMachine()->clear();
	getStateMachine()->setGoalObject( victim );
	setGoalPositionClipped( pos, cmdSource );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_GUARD_RETALIATE );

	// do this after setting it as the current state, as the max-shots-to-fire is reset in AttackState::onEnter()
	Weapon* weapon = getObject()->getCurrentWeapon();
	if (weapon)
		weapon->setMaxShotCount(maxShotsToFire);
}

//----------------------------------------------------------------------------------------
/**
 * Attack the given team
 */
void AIUpdateInterface::privateAttackTeam( const Team *team, Int maxShotsToFire, CommandSourceType cmdSource )
{
	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	getStateMachine()->setGoalTeam( team );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_ATTACK_SQUAD );

	// do this after setting it as the current state, as the max-shots-to-fire is reset in AttackState::onEnter()
	Weapon* weapon = getObject()->getCurrentWeapon();
	if (weapon)
		weapon->setMaxShotCount(maxShotsToFire);
}

//----------------------------------------------------------------------------------------
/**
 * Attack given spot
 */
void AIUpdateInterface::privateAttackPosition( const Coord3D *pos, Int maxShotsToFire, CommandSourceType cmdSource )
{
	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	Coord3D localPos = *pos;
	pos = NULL;

	// ick... rather grody hack for disarming stuff. if we attack a position,
	// but have a "continue range" for the weapon, try to find a suitable object
	// to attack first.
	Weapon* weapon = getObject()->getCurrentWeapon();
	Real continueRange = weapon ? weapon->getContinueAttackRange() : 0.0f;
	if (continueRange > 0.0f)
	{
		// ick. set this bit so we can find the mine to go target, even if stealthed. (srj)
		getObject()->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_IGNORING_STEALTH ) );
		PartitionFilterPossibleToAttack filterAttack(ATTACK_NEW_TARGET, getObject(), cmdSource);
		PartitionFilterSameMapStatus filterMapStatus(getObject());
		PartitionFilter *filters[] = { &filterAttack, &filterMapStatus, NULL };
		Object* victim = ThePartitionManager->getClosestObject(&localPos, continueRange, FROM_CENTER_2D, filters);
		getObject()->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_IGNORING_STEALTH ) );

		if (victim)
		{
 			aiAttackObject(victim, maxShotsToFire, cmdSource);
			return;
		}
		else
		{
			// limit 'em to one shot, and fall thru.
			maxShotsToFire = 1;
		}
	}

	// if it's a contact weapon, we must be able to path to the target pos. if not, find a spot close by.
	// this fixes an obscure bug with mine-clearing: if you tell someone to clear mines and put the centerpoint
	// inside a building, the dozer/worker will just go thru the building to that spot. ick. so if you find that
	// this clause (below) is problematic, you'll probbaly have to find another way to fix this mine-clearing bug. (srj)
	if (weapon && weapon->isContactWeapon() && !isPathAvailable(&localPos))
	{
		FindPositionOptions fpOptions;
		fpOptions.minRadius = 0.0f;
		fpOptions.maxRadius = 100.0f;
		fpOptions.sourceToPathToDest = getObject();// This makes it find a place forWhom can get to.
		Coord3D tmp;
		if (ThePartitionManager->findPositionAround(&localPos, &fpOptions, &tmp))
			localPos = tmp;
	}

	getStateMachine()->clear();
	destroyPath();
	setGoalPositionClipped(&localPos, cmdSource);
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_ATTACK_POSITION );


	//Set the goal object to NULL because if we are attacking a location, we need to be able to move up to it properly.
	//When this isn't set, the move aborts before getting into firing range, thus deadlocks.
	getStateMachine()->setGoalObject( NULL );

	// do this after setting it as the current state, as the max-shots-to-fire is reset in AttackState::onEnter()
	weapon = getObject()->getCurrentWeapon();
	if (weapon)
		weapon->setMaxShotCount(maxShotsToFire);
}

//----------------------------------------------------------------------------------------
/**
 * Attack move to the given location
 */
void AIUpdateInterface::privateAttackMoveToPosition( const Coord3D *pos, Int maxShotsToFire, CommandSourceType cmdSource )
{
	if (m_isAiDead || getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	setGoalPositionClipped(pos, cmdSource);
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_ATTACK_MOVE_TO );

	// do this after setting it as the current state, as the max-shots-to-fire is reset in AttackState::onEnter()
	Weapon* weapon = getObject()->getCurrentWeapon();
	if (weapon)
		weapon->setMaxShotCount(maxShotsToFire);
}

//----------------------------------------------------------------------------------------
/**
 * Attack move down a given waypoint path. If asTeam is TRUE, do so as a team.
 */
void AIUpdateInterface::privateAttackFollowWaypointPath( const Waypoint *way, Int maxShotsToFire, Bool asTeam, CommandSourceType cmdSource )
{
	if (m_isAiDead || getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	getStateMachine()->setGoalWaypoint( way );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( (asTeam ? AI_ATTACKFOLLOW_WAYPOINT_PATH_AS_TEAM : AI_ATTACKFOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS) );

	// do this after setting it as the current state, as the max-shots-to-fire is reset in AttackState::onEnter()
	Weapon* weapon = getObject()->getCurrentWeapon();
	if (weapon)
		weapon->setMaxShotCount(maxShotsToFire);
}


//----------------------------------------------------------------------------------------
/**
 * Begin "seek and destroy"
 */
void AIUpdateInterface::privateHunt( CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	if (getObject()->isKindOf(KINDOF_PROJECTILE))
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_HUNT );
}

//----------------------------------------------------------------------------------------
/**
 * Begin "seek and destroy"
 */
void AIUpdateInterface::privateAttackArea( const PolygonTrigger *areaToGuard, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	if (getObject()->isKindOf(KINDOF_PROJECTILE))
		return;

	m_areaToGuard = areaToGuard;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_ATTACK_AREA);
}

//----------------------------------------------------------------------------------------
/**
 * Repair the given object
 */
void AIUpdateInterface::privateRepair( Object *obj, CommandSourceType cmdSource )
{

	// there is no "default" way for generic objects to repair each other
	return;
				
}

#ifdef ALLOW_SURRENDER
//----------------------------------------------------------------------------------------
/**
	* Pick up prisoner
	*/
void AIUpdateInterface::privatePickUpPrisoner( Object *prisoner, CommandSourceType cmdSource )
{

	// there is no "default" way for generic units to pick up prisoners
	return;

}
#endif

#ifdef ALLOW_SURRENDER
//----------------------------------------------------------------------------------------
/**
	* Return prisoners
	*/
void AIUpdateInterface::privateReturnPrisoners( Object *prison, CommandSourceType cmdSource )
{

	// there is no "default" way for generic units to return prisoners
	return;

}
#endif

//----------------------------------------------------------------------------------------
/**
	* Resume construction of object
	*/
void AIUpdateInterface::privateResumeConstruction( Object *obj, CommandSourceType cmdSource )
{

	// there is no "default" way for generic objects to resume construction
	return;

}

//----------------------------------------------------------------------------------------
/**
 * Get healed at the heal depot
 */
void AIUpdateInterface::privateGetHealed( Object *healDepot, CommandSourceType cmdSource )
{

  // sanity, if we can't get healed from here get outta here
	if( TheActionManager->canGetHealedAt( getObject(), healDepot, cmdSource ) == FALSE )
		return;

	// enter the heal dest for healing
	aiEnter( healDepot, cmdSource );

}

//----------------------------------------------------------------------------------------
/**
 * Get repaired at the repair depot
 */
void AIUpdateInterface::privateGetRepaired( Object *repairDepot, CommandSourceType cmdSource )
{

	// sanity, if we can't get repaired from here get out of here
	if( TheActionManager->canGetRepairedAt( getObject(), repairDepot, cmdSource ) == FALSE )
		return;

	// dock with the repair depot
	aiDock( repairDepot, cmdSource );

}

//----------------------------------------------------------------------------------------
/**
 * Enter the given object
 */
void AIUpdateInterface::privateEnter( Object *obj, CommandSourceType cmdSource )
{
	Object *me = getObject();
	if( me->isMobile() == FALSE )
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	if( TheActionManager->canEnterObject( me, obj, cmdSource, DONT_CHECK_CAPACITY ) )
	{
		getStateMachine()->clear();
		getStateMachine()->setGoalObject( obj );
		setLastCommandSource( cmdSource );
		getStateMachine()->setState( AI_ENTER );
	}
}

//----------------------------------------------------------------------------------------
/**
 * Dock with the given object
 */
void AIUpdateInterface::privateDock( Object *obj, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	getStateMachine()->clear();
	getStateMachine()->setGoalObject( obj );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_DOCK );
}

//----------------------------------------------------------------------------------------
void AIUpdateInterface::privateCombatDrop( Object *target, const Coord3D& pos, CommandSourceType cmdSource )
{
	DEBUG_CRASH(("default implementation, should never be called"));
	if( getObject()->getContain() )
	{
		getObject()->getContain()->removeAllContained(FALSE);
	}
}

//----------------------------------------------------------------------------------------
/**
 * Get out of whatever it is inside of
 */
void AIUpdateInterface::privateExit( Object *objectToExit, CommandSourceType cmdSource )
{
	Object *us = getObject();
	if (!objectToExit)
	{
		objectToExit = us->getContainedBy();
	}

	if (!objectToExit)
		return;

  if ( objectToExit->isDisabledByType( DISABLED_SUBDUED ) )
    return;


	// we must go thru this state (rather than calling exitObjectViaDoor directly!), 
	// because a few containers might need to delay to allow
	// us to exit (eg, Chinooks must land), meaning we might have to wait a bit, and coordinate
	// with the container by actually NOTIFYING it that we want to exit...
	getStateMachine()->clear();
	getStateMachine()->setGoalObject( objectToExit );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_EXIT );
}

//----------------------------------------------------------------------------------------
/**
 * Get out of whatever it is inside of this frame
 */
void AIUpdateInterface::privateExitInstantly( Object *objectToExit, CommandSourceType cmdSource )
{
	Object *us = getObject();
	if (!objectToExit)
	{
		objectToExit = us->getContainedBy();
	}

	if (!objectToExit)
		return;

  if ( objectToExit->isDisabledByType( DISABLED_SUBDUED ) )
    return;

	// we must go thru this state (rather than calling exitObjectViaDoor directly!), 
	// because a few containers might need to delay to allow
	// us to exit (eg, Chinooks must land), meaning we might have to wait a bit, and coordinate
	// with the container by actually NOTIFYING it that we want to exit...
	getStateMachine()->clear();
	getStateMachine()->setGoalObject( objectToExit );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_EXIT_INSTANTLY );
}


//----------------------------------------------------------------------------------------
/**
 * Get out of whatever it is inside of
 */
void AIUpdateInterface::doQuickExit( const std::vector<Coord3D>* path )
{

	Bool locked = getStateMachine()->isLocked();
	getStateMachine()->unlock();

	// set path info
	getStateMachine()->setGoalPath( path );

	getStateMachine()->setTemporaryState( AI_FOLLOW_EXITPRODUCTION_PATH, 10*LOGICFRAMES_PER_SECOND);
	if (locked) {
		getStateMachine()->lock("Relocking in doQuickExit.");
	}
}

//----------------------------------------------------------------------------------------
/**
 * Empty its contents
 */
void AIUpdateInterface::privateEvacuate( Int exposeStealthUnits, CommandSourceType cmdSource )
{

  if ( getObject()->isDisabledByType( DISABLED_SUBDUED ) )
    return;


	ContainModuleInterface *contain = getObject()->getContain();
	if( contain )
	{
		if( exposeStealthUnits )
		{
			contain->markAllPassengersDetected();
		}
		contain->orderAllPassengersToExit( cmdSource, FALSE );
	}
}

//----------------------------------------------------------------------------------------
/**
 * Empty its contents this frame
 */
void AIUpdateInterface::privateEvacuateInstantly( Int exposeStealthUnits, CommandSourceType cmdSource )
{

  if ( getObject()->isDisabledByType( DISABLED_SUBDUED ) )
    return;


	ContainModuleInterface *contain = getObject()->getContain();
	if( contain )
	{
		if( exposeStealthUnits )
		{
			contain->markAllPassengersDetected();
		}
		contain->orderAllPassengersToExit( cmdSource, TRUE );
	}
}

// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::privateExecuteRailedTransport( CommandSourceType cmdSource )
{

	// there is no default implementation for this

}

//----------------------------------------------------------------------------------------
///< life altering state change, if this AI can do it
void AIUpdateInterface::privateGoProne( const DamageInfo *damageInfo, CommandSourceType )
{
	static NameKeyType proneModuleKey = TheNameKeyGenerator->nameToKey( "ProneUpdate" );
	ProneUpdate *proneModule = (ProneUpdate *)getObject()->findUpdateModule( proneModuleKey );

	if( proneModule )
		proneModule->goProne( damageInfo );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/**
 * Wander around
 */
void AIUpdateInterface::privateWander( const Waypoint *way, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_WANDER);

	getStateMachine()->clear();
	setLastCommandSource( cmdSource );
	getStateMachine()->setGoalWaypoint( way );
	getStateMachine()->setState( AI_WANDER );
	
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/**
 * Wander around
 */
void AIUpdateInterface::privateWanderInPlace( CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_WANDER);

	getStateMachine()->clear();
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_WANDER_IN_PLACE );
	
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/**
 * Panic
 */
void AIUpdateInterface::privatePanic( const Waypoint *way, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_PANIC);
	
	getStateMachine()->clear();
	setLastCommandSource( cmdSource );
	getStateMachine()->setGoalWaypoint( way );
	getStateMachine()->setState( AI_PANIC );
	
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/**
 * Busy
 */
void AIUpdateInterface::privateBusy( CommandSourceType cmdSource )
{
	getStateMachine()->clear();
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_BUSY );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/**
 * Guard the given spot
 */
void AIUpdateInterface::privateGuardPosition( const Coord3D *pos, GuardMode guardMode, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	if (getObject()->isKindOf(KINDOF_PROJECTILE))
		return;

	if (m_guardTargetType[1] == GUARDTARGET_NONE) {
		m_guardTargetType[1] = GUARDTARGET_LOCATION;
	} else {
		m_guardTargetType[0] = GUARDTARGET_LOCATION;
	}
	Coord3D adjPos = *pos;
	if (cmdSource==CMD_FROM_PLAYER) {
		// Clip to playable area.
		Region3D r;
		TheTerrainLogic->getExtent(&r);
		if (!r.isInRegionNoZ(&adjPos))
			adjPos = TheTerrainLogic->findClosestEdgePoint(&adjPos);
	}
	m_locationToGuard = adjPos;
	m_guardMode = guardMode;

	getStateMachine()->clear();
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_GUARD );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/**
 * Guard the given spot
 */
void AIUpdateInterface::privateGuardTunnelNetwork( GuardMode guardMode, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	if (getObject()->isKindOf(KINDOF_PROJECTILE))
		return;

	m_guardMode = guardMode;

	getStateMachine()->clear();
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_GUARD_TUNNEL_NETWORK );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/**
 * Guard the given spot
 */
void AIUpdateInterface::privateGuardObject( Object *objectToGuard, GuardMode guardMode, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	if (getObject()->isKindOf(KINDOF_PROJECTILE))
		return;

	if (m_guardTargetType[1] == GUARDTARGET_NONE) {
		m_guardTargetType[1] = GUARDTARGET_OBJECT;
	} else {
		m_guardTargetType[0] = GUARDTARGET_OBJECT;
	}
	m_guardMode = guardMode;
	m_objectToGuard = objectToGuard->getID();

	getStateMachine()->clear();
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_GUARD );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/**
 * Guard the given spot
 */
void AIUpdateInterface::privateGuardArea( const PolygonTrigger *areaToGuard, GuardMode guardMode, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	if (getObject()->isKindOf(KINDOF_PROJECTILE))
		return;

	if (m_guardTargetType[1] == GUARDTARGET_NONE) {
		m_guardTargetType[1] = GUARDTARGET_AREA;
	} else {
		m_guardTargetType[0] = GUARDTARGET_AREA;
	}
	m_areaToGuard = areaToGuard;
	m_guardMode = guardMode;

	Coord3D pos;
	m_areaToGuard->getCenterPoint(&pos);
	m_locationToGuard = pos;
	m_objectToGuard = INVALID_ID; //just in case.
	getStateMachine()->clear();
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_GUARD );
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::privateHackInternet( CommandSourceType cmdSource )
{
	// We need to be able to hack in containers
//	if (getObject()->isMobile() == FALSE)
//		return;

	getStateMachine()->clear();
	setLastCommandSource( cmdSource );

	static NameKeyType key_HackInternetAIUpdate = NAMEKEY("HackInternetAIUpdate");
	HackInternetAIUpdate *ai = (HackInternetAIUpdate*)getObject()->findUpdateModule( key_HackInternetAIUpdate );
	if( ai )
	{
		ai->hackInternet();
	}
	else
	{
		DEBUG_CRASH(("Unit %s is expecting a 'Update = HackInternetAIUpdate' entry in FactionUnit.ini", getObject()->getTemplate()->getName().str() ) );
	}
}

/// if we are attacking "fromID", stop that and attack "toID" instead
void AIUpdateInterface::transferAttack(ObjectID fromID, ObjectID toID)
{
	Object *newTarget = TheGameLogic->findObjectByID( toID );

	if (m_currentVictimID == fromID)
		m_currentVictimID = toID;

	Object* goalObj = getStateMachine()->getGoalObject();
	if (goalObj && goalObj->getID() == fromID)
		getStateMachine()->setGoalObject( newTarget );

	//Transfer the turrets too this frame.
	for( Int i = 0; i < MAX_TURRETS; i++ )
	{
		goalObj = getTurretTargetObject( (WhichTurretType)i, FALSE );
		if( goalObj && goalObj->getID() == fromID )
		{
			setTurretTargetObject( (WhichTurretType)i, newTarget, TRUE );
		}
	}

}

//----------------------------------------------------------------------------------------------------------
/**
 * Indicate who we are attacking.
 */
void AIUpdateInterface::setCurrentVictim( const Object *victim )
{
	if (victim == NULL)
	{
		// be paranoid, in case we are called from dtors, etc.
		if (m_currentVictimID != INVALID_ID)
		{
			Object* self = getObject();
			Object* target = TheGameLogic->findObjectByID(m_currentVictimID);
			if (self != NULL && target != NULL)
			{
				AIUpdateInterface* targetAI = target->getAI();
				if (targetAI)
				{
					targetAI->addTargeter(self->getID(), FALSE);
				}
			}
		}

		m_currentVictimID = INVALID_ID;
	}
	else
	{
		// we don't add a targeter here, since we usually want to defer
		// that until we are actually aiming (as opposed to, say, approaching)
		// the victim.
		m_currentVictimID = victim->getID();
	}
}

/**
 * Who is our current victim?
 */
Object *AIUpdateInterface::getCurrentVictim( void ) const
{
	if (m_currentVictimID != INVALID_ID)
		return TheGameLogic->findObjectByID( m_currentVictimID );

	return NULL;
}

// if we are attacking a position (and NOT an object), return it. otherwise return null.
const Coord3D *AIUpdateInterface::getCurrentVictimPos( void ) const
{
	if (getObject()->testStatus(OBJECT_STATUS_IS_ATTACKING))
	{
		if (m_currentVictimID == INVALID_ID)
		{
			return getStateMachine()->getGoalPosition();
		}
	}

	return NULL;
}


/**
 * Set the behavior modifier for this agent
 */
void AIUpdateInterface::setAttitude( AttitudeType tude )
{
	m_attitude = tude;
}

/**
 * Get the current behavior modifier state	
 */
AttitudeType AIUpdateInterface::getAttitude( void ) const
{
	return m_attitude;
}

/**
 * Return the current state the AI is in.
 */
AIStateType AIUpdateInterface::getAIStateType() const
{
	return (AIStateType)getStateMachine()->getCurrentStateID();
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::ignoreObstacle( const Object *obj )
{
	m_ignoreObstacleID = obj ? obj->getID() : INVALID_ID;
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::ignoreObstacleID( ObjectID id )
{
	m_ignoreObstacleID = id;
}

//-------------------------------------------------------------------------------------------------
ObjectID AIUpdateInterface::getIgnoredObstacleID( void ) const
{ 
	return m_ignoreObstacleID; 
}

//-------------------------------------------------------------------------------------------------
Object* AIUpdateInterface::getEnterTarget()
{
	AIStateType stateType = getAIStateType();

	if( stateType != AI_ENTER && 
			stateType != AI_GUARD_TUNNEL_NETWORK &&
			stateType != AI_GET_REPAIRED )
		return NULL;

	return getStateMachine()->getGoalObject();
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::setLastCommandSource( CommandSourceType source )
{
	m_lastCommandSource = source; 
}

//-------------------------------------------------------------------------------------------------
UnsignedInt AIUpdateInterface::getMoodMatrixValue( void ) const
{
	UnsignedInt returnVal = 0;
	// seems like a weird way to get my controlling object, but I don't see another
	if (!getStateMachine()) 
	{
		return returnVal;
	}
	
	const Object *owner = getObject();
	Player *player = owner->getControllingPlayer();

	if (!player) 
	{
		return returnVal;
	}
	
	if (player->getPlayerType() == PLAYER_HUMAN) 
	{
		returnVal |= MM_Controller_Player;
		// Human units don't have a mood.

	} 
	else 
	{
		returnVal |= MM_Controller_AI;
		switch (getAttitude())
		{
			case AI_SLEEP:			returnVal |= MM_Mood_Sleep; break;
			case AI_PASSIVE:		returnVal |= MM_Mood_Passive; break;
			case AI_NORMAL:			returnVal |= MM_Mood_Normal; break;
			case AI_ALERT:			returnVal |= MM_Mood_Alert; break;
			case AI_AGGRESSIVE:	returnVal |= MM_Mood_Aggressive; break;
			default: 
				DEBUG_CRASH(("Unknown mood '%d' in getMoodMatrixValue. (Team '%s'). Using normal. (jkmcd)", getAttitude(), getObject()->getTeam()->getName().str() ));
				returnVal |= MM_Mood_Normal;
				break;
		}
	}

	if (getLocomotorSet().getValidSurfaces() & LOCOMOTORSURFACE_AIR) 
	{
		returnVal |= MM_UnitType_Air;
	} 
	else 
	{
		if (m_turretAI[0] != NULL) 
		{
			returnVal |= MM_UnitType_Turreted;
		} 
		else 
		{
			returnVal |= MM_UnitType_NonTurreted;
		}
	}

	return returnVal;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt AIUpdateInterface::getMoodMatrixActionAdjustment( MoodMatrixAction action ) const
{
	// Angry Mob Members (but not Nexi) are never subject to moods. In particular,
	// they must never, ever, ever convert a move into an attack move, or Bad Things
	// will happend, since MobMemberSlavedUpdate expects a moveto to remain a moveto.
	// Mark L sez that members do not, in fact, need any mood adjustment whatsoever,
	// since the mood of the nexus wants to control all this anyway. Unfortunately, there
	// is no KINDOF_MOB_MEMBER, and we don't want to add one at the eleventh hour...
	// this, however, is a unique and safe combination that applies only to mob members. (srj)
	if (getObject()->isKindOf(KINDOF_INFANTRY) && getObject()->isKindOf(KINDOF_IGNORED_IN_GUI))
	{
		return MAA_Action_Ok;
	}

	UnsignedInt moodMatrix = getMoodMatrixValue();
	UnsignedInt returnVal = 0;

	if (moodMatrix & MM_Controller_Player) 
	{
		// Player-controlled units can always do actions (from a mood perspective, at any rate)
		returnVal = MAA_Action_Ok;
		return returnVal;
	}

	returnVal = MAA_Action_Ok;
	switch (action)
	{
		case MM_Action_Idle: 
		{
			switch( moodMatrix & MM_Mood_Bitmask )
			{
				case MM_Mood_Sleep:				returnVal = MAA_Action_Ok | MAA_Affect_Range_IgnoreAll; break;
				case MM_Mood_Passive:			returnVal = MAA_Action_Ok | MAA_Affect_Range_WaitForAttack; break;
				case MM_Mood_Normal:			returnVal = MAA_Action_Ok; break;
				case MM_Mood_Alert:				returnVal = MAA_Action_Ok | MAA_Affect_Range_Alert; break;
				case MM_Mood_Aggressive:	returnVal = MAA_Action_Ok | MAA_Affect_Range_Aggressive; break;
			}
			break;
		}
		case MM_Action_Move:
		{
			switch( moodMatrix & MM_Mood_Bitmask )
			{
				case MM_Mood_Sleep:				returnVal = MAA_Action_To_Idle | MAA_Affect_Range_IgnoreAll; break;
				case MM_Mood_Passive:			returnVal = MAA_Action_Ok | MAA_Affect_Range_WaitForAttack; break;
				case MM_Mood_Normal:			returnVal = MAA_Action_Ok; break;
				case MM_Mood_Alert:				returnVal = MAA_Action_To_AttackMove | MAA_Affect_Range_Alert; break;
				case MM_Mood_Aggressive:	returnVal = MAA_Action_To_AttackMove | MAA_Affect_Range_Aggressive; break;
			}
			break;
		}
		case MM_Action_Attack:
		{
			switch( moodMatrix & MM_Mood_Bitmask )
			{
				case MM_Mood_Sleep:				returnVal = MAA_Action_To_Idle | MAA_Affect_Range_IgnoreAll; break;
				case MM_Mood_Passive:			returnVal = MAA_Action_Ok; break;
				case MM_Mood_Normal:			returnVal = MAA_Action_Ok; break;
				case MM_Mood_Alert:				returnVal = MAA_Action_Ok; break;
				case MM_Mood_Aggressive:	returnVal = MAA_Action_Ok; break;
			}
			break;
		}
		case MM_Action_AttackMove:
		{
			switch( moodMatrix & MM_Mood_Bitmask )
			{
				case MM_Mood_Sleep:				returnVal = MAA_Action_To_Idle | MAA_Affect_Range_IgnoreAll; break;
				case MM_Mood_Passive:			returnVal = MAA_Action_Ok; break;
				case MM_Mood_Normal:			returnVal = MAA_Action_Ok; break;
				case MM_Mood_Alert:				returnVal = MAA_Action_Ok | MAA_Affect_Range_Alert; break;
				case MM_Mood_Aggressive:	returnVal = MAA_Action_Ok | MAA_Affect_Range_Aggressive; break;
			}
			break;
		}
	};

	return returnVal;
}

//----------------------------------------------------------------------------------------------
void AIUpdateInterface::wakeUpAndAttemptToTarget( void )
{
	if (!isIdle()) {
		return;
	}

	UnsignedInt now = TheGameLogic->getFrame();
	m_nextMoodCheckTime = now;
	m_randomlyOffsetMoodCheck = TRUE;
}

//----------------------------------------------------------------------------------------------
/**
 * Reset when we should next look for a target. Usually called by *Idle::onEnter
 */
void AIUpdateInterface::resetNextMoodCheckTime()
{
	UnsignedInt now = TheGameLogic->getFrame();
	m_nextMoodCheckTime = now + TheAI->getAiData()->m_forceIdleFramesCount;
	m_randomlyOffsetMoodCheck = TRUE;
}

//----------------------------------------------------------------------------------------------
void AIUpdateInterface::setNextMoodCheckTime( UnsignedInt frame )
{
	m_nextMoodCheckTime = frame;
	m_randomlyOffsetMoodCheck = false;
}



Bool AIUpdateInterface::canAutoAcquireWhileStealthed() const 
{ 
  if ( getObject() && getObject()->getStealth() && getObject()->getStealth()->isGrantedBySpecialPower() )
    return TRUE;
  return getAIUpdateModuleData()->m_autoAcquireEnemiesWhenIdle & AAS_Idle_Stealthed;
}


//----------------------------------------------------------------------------------------------
/**
 * Return the next object that our mood suggests we should attack.
 */
Object* AIUpdateInterface::getNextMoodTarget( Bool calledByAI, Bool calledDuringIdle, Bool allowOutOfWeaponRangeTargets )
{
	Object *obj = getObject();

	// if we're dead, we can't attack
	if (obj->isEffectivelyDead()) 
		return NULL;

	if (obj->testStatus(OBJECT_STATUS_IS_USING_ABILITY)) {
		return NULL;  // we are doing a special ability.  Shouldn't auto-acquire a target at this time.  jba.
	}

	const AIUpdateModuleData* d = getAIUpdateModuleData();
	
	if (calledDuringIdle)
	{
		if ((d->m_autoAcquireEnemiesWhenIdle & AAS_Idle) == 0) 
		{
			return NULL;
		}
	}

// srj sez: this should ignore calledDuringIdle, despite what the name of the bit implies.
	if (isAttacking() && BitTest(d->m_autoAcquireEnemiesWhenIdle, AAS_Idle_Not_While_Attacking))
	{
		return NULL;
	}

	//Check if unit is stealthed... is so we won't acquire targets unless he has
	//AutoAcquireWhenIdle = Yes Stealthed.
	if ( calledDuringIdle )
	{
		if( obj->getStatusBits().test( OBJECT_STATUS_STEALTHED ) ) 
		{
			if( !canAutoAcquireWhileStealthed() ) 
			{
  			const Object *container = obj->getContainedBy();
  			if( ! (container && container->getContain()->isPassengerAllowedToFire()) )
  			{
					// Sorry, stealthed and not allowed to idle fire when stealthed.
					// Being in a firing container is an exception to this veto.
  				return NULL;
  			}
			}
		}
	}

	UnsignedInt now = TheGameLogic->getFrame();

	// Check if team auto targets same victim.
	Object *teamVictim = NULL;
	if (calledByAI && obj->getTeam()->getPrototype()->getTemplateInfo()->m_attackCommonTarget) 
	{
		teamVictim = obj->getTeam()->getTeamTargetObject();
		if (teamVictim) {
			// Make sure we can attack the team victim.  Mixed teams can acquire aircraft, and units
			// like toxin tractors shouldn't acquire aircraft. jba. [8/27/2003]
			CanAttackResult result = obj->getAbleToAttackSpecificObject( ATTACK_NEW_TARGET, teamVictim, CMD_FROM_AI );
			if( result != ATTACKRESULT_POSSIBLE && result != ATTACKRESULT_POSSIBLE_AFTER_MOVING ) {
				teamVictim = NULL; // Can't attack him. jba [8/27/2003]
			}
		}
		
		if (teamVictim && getAttitude()>=AI_NORMAL) 
			return teamVictim;
	}

	DEBUG_ASSERTCRASH(m_nextMoodCheckTime != 0, ("m_nextMoodCheckTime should never be zero here."));

	if (calledByAI)
	{
		// make sure it's time to check again.
		if (now < m_nextMoodCheckTime)
			return NULL;

		Int checkRate = d->m_moodAttackCheckRate;
		m_nextMoodCheckTime = now + checkRate;
		if (m_randomlyOffsetMoodCheck)
		{
			Int halfRate = checkRate >> 1;
			m_nextMoodCheckTime = (UnsignedInt)((Int)m_nextMoodCheckTime + GameLogicRandomValue(-halfRate, halfRate));
			m_randomlyOffsetMoodCheck = FALSE;
		}
	}

	// Use Guard Outer, which typically corresponds to the total range
	Real rangeToFindWithin = TheAI->getAdjustedVisionRangeForObject(obj, AI_VISIONFACTOR_OWNERTYPE | AI_VISIONFACTOR_MOOD);

	// A caller that closes with what it finds (attack move) is not limited by what the unit can see:
	// a unit whose weapon outranges its vision - artillery, rocket infantry, a Scud launcher - found
	// nothing at all this way and walked past everything it was ordered to kill.
	if (allowOutOfWeaponRangeTargets)
	{
		Real weaponRange = obj->getLargestWeaponRange();
		if (weaponRange > rangeToFindWithin)
			rangeToFindWithin = weaponRange;
	}

	if (rangeToFindWithin <= 0.0f)
		return NULL;

	//If we are contained by an object, add it's bounding radius so that large buildings can auto acquire everything in
	//outer ranges. Calculating this from the center is bad... although this code makes it possible to acquire a target
	//outside of range, but in that case, it'll just fail and continue.
	const Object *container = obj->getContainedBy();
	if( container )
	{
		rangeToFindWithin += container->getGeometryInfo().getBoundingCircleRadius();
	}

	UnsignedInt moodMatrixVal = getMoodMatrixValue();
	if ((moodMatrixVal & MM_Controller_AI) && (moodMatrixVal & MM_Mood_Passive)) 
	{
		BodyModuleInterface *bmi = obj->getBodyModule();
		if (!bmi)
			return NULL;

		//Kris: August 26, 2003
		//Do not allow units that healed me to get acquired! They are our friends!!!
		if( bmi->getLastDamageInfo()->in.m_damageType != DAMAGE_HEALING )
		{
			return TheGameLogic->findObjectByID(bmi->getLastDamageInfo()->in.m_sourceID);
		}
	}
	UnsignedInt flags = AI::CAN_ATTACK;
	if (TheAI->getAiData()->m_attackUsesLineOfSight) {
		if (obj->isKindOf(KINDOF_ATTACK_NEEDS_LINE_OF_SIGHT)) {
			flags |= AI::CAN_SEE;
		}
	}

	if (TheAI->getAiData()->m_attackIgnoreInsignificantBuildings) {
		flags |= AI::IGNORE_INSIGNIFICANT_BUILDINGS; 
	}
	
	//
	// Idle auto-acquire ignores buildings unless the unit is one of the few with AttackBuildings in
	// its AutoAcquireEnemiesWhenIdle, which is right for a unit standing around, and wrong for an
	// attack move: the player pointed at the enemy base and the group walked through it without
	// firing at a single structure.  A caller that closes with what it finds takes buildings too;
	// the CAN_ATTACK filter still rejects the ones this unit's weapons cannot hurt.
	//
	if( (d->m_autoAcquireEnemiesWhenIdle & AAS_Idle_Attack_Buildings) || allowOutOfWeaponRangeTargets )
	{
		flags |= AI::ATTACK_BUILDINGS;
	}

	// if we're called by AI, and are human controlled, then our AI will not
	// allow us to pursue the target. therefore, we should ensure that we only
	// look for targets that are already within attack range (as opposed to vision range).
	// The caller can lift that restriction (attack move does) when it will actually close
	// with what it finds instead of driving past it.
	if (calledByAI && !allowOutOfWeaponRangeTargets && obj->getControllingPlayer()->getPlayerType() == PLAYER_HUMAN)
	{
		flags |= AI::WITHIN_ATTACK_RANGE;
	}
	
	//
	// Instead of shroud affecting the ability to attack, it affects the ability to target.
	// The same checks apply as the old WeaponSet check (now commented out, search for getShroudedStatus)
	//
	// This used to carry "&& getPlayerType() == PLAYER_HUMAN", which is not a balance knob but a
	// hard if on who is playing: a computer player's units auto-acquired targets standing in fog it
	// could not see, and a human player's could not.  UNFOGGED is the only thing that engages
	// PartitionFilterFreeOfFog, and getNextMoodTarget is the shared path - attack states, guard, mob
	// members and base defence turrets all route through here - so the exemption covered every one
	// of them.  The AI now sees what a player sees, which is also what makes stealth work against it.
	//
	if( calledByAI && obj->getControllingPlayer() )
	{
		flags |= AI::UNFOGGED;
	}

	//
	// A caller that closes with what it finds picks the biggest threat in range instead of the
	// nearest thing (see AI_threatScore).  Walking up to a dozer while the artillery next to it
	// keeps firing is only sensible for a unit that cannot move to a better target - and this
	// caller can.
	//
	if (allowOutOfWeaponRangeTargets)
	{
		flags |= AI::PREFER_HIGH_THREAT;
	}

	Object *newVictim = TheAI->findClosestEnemy(obj, rangeToFindWithin, flags, getAttackInfo());

/*
DEBUG_LOG(("GNMT frame %d: %s %08lx (con %s %08lx) uses range %f, flags %08lx, %s finds %s %08lx\n",
	now,
	obj->getTemplate()->getName().str(),
	obj,
	container ? container->getTemplate()->getName().str() : "",
	container,
	rangeToFindWithin,
	flags,
	getAttackInfo() != NULL && getAttackInfo() != TheScriptEngine->getDefaultAttackInfo() ? "ATTACKINFO," : "",
	newVictim ? newVictim->getTemplate()->getName().str() : "",
	newVictim
));
*/

	if (newVictim)
	{
		CRCDEBUG_LOG(("AIUpdateInterface::getNextMoodTarget() - %d is attacking %d\n", obj->getID(), newVictim->getID()));
/*
srj debug hack. ignore.
Int ot = getTmpValue();
if (ot!=0&&now>ot&&now-ot<=4)
ot=ot;
setTmpValue(now);
*/
	}

	return newVictim;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::hasNationalism() const
{
	const Player *player = getObject()->getControllingPlayer();
	if( player )
	{
		///@todo Find a better way to represent nationalism without hardcoding here (CBD)
		static const UpgradeTemplate *nationalismTemplate = TheUpgradeCenter->findUpgrade( "Upgrade_Nationalism" );
		DEBUG_ASSERTCRASH( nationalismTemplate != NULL, ("AIUpdateInterface::hasNationalism - Nationalism upgrade not found\n") );
		if( nationalismTemplate )
			return player->hasUpgradeComplete( nationalismTemplate );
	}
	return FALSE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::hasFanaticism() const
{
	const Player *player = getObject()->getControllingPlayer();
	if( player )
	{
		///@todo Find a better way to represent fanaticism without hardcoding here (MAL)
		static const UpgradeTemplate *fanaticismTemplate = TheUpgradeCenter->findUpgrade( "Upgrade_Fanaticism" );
		DEBUG_ASSERTCRASH( fanaticismTemplate != NULL, ("AIUpdateInterface::hasFanaticism - Fanaticism upgrade not found\n") );
		if( fanaticismTemplate )
			return player->hasUpgradeComplete( fanaticismTemplate );
	}
	return FALSE;
}

// ------------------------------------------------------------------------------------------------
// the horde module that owns the bonus tells us what it sees, instead of us walking every behavior
// module of the object to work it out again.
// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::evaluateMoraleBonus( Bool inHorde, Bool allowNationalism, HordeActionType type )
{
#ifdef ALLOW_DEMORALIZE
	if( isDemoralized() )
	{
		Object *us = getObject();

		us->setWeaponBonusCondition( WEAPONBONUSCONDITION_DEMORALIZED );

		// a demoralized unit gets none of the three bonuses
		us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_HORDE );
		us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_NATIONALISM );
		us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_FANATICISM );

		Drawable *draw = us->getDrawable();
		if( draw && !us->isKindOf( KINDOF_PORTABLE_STRUCTURE ) )
			draw->setTerrainDecal( TERRAIN_DECAL_DEMORALIZED );

		return;
	}

	getObject()->clearWeaponBonusCondition( WEAPONBONUSCONDITION_DEMORALIZED );
#endif

	//Lorenzen temporarily disabled, since it fights with the horde buff
	//Drawable *draw = getObject()->getDrawable();
	//if ( draw && !getObject()->isKindOf( KINDOF_PORTABLE_STRUCTURE ) )
	//	draw->setTerrainDecal(TERRAIN_DECAL_NONE);

	switch( type )
	{
		case HORDEACTION_HORDE:
			evaluateNationalismBonusClassic( inHorde, allowNationalism );
			break;

		case HORDEACTION_HORDE_FIXED:
			evaluateNationalismBonus( inHorde, allowNationalism );
			break;
	}
}

// ------------------------------------------------------------------------------------------------
// the classic rule: nationalism and fanaticism are granted while the upgrades are owned and are
// never taken away again, even when the horde breaks up.
// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::evaluateNationalismBonusClassic( Bool inHorde, Bool allowNationalism )
{
	Object *us = getObject();

	if( inHorde )
		us->setWeaponBonusCondition( WEAPONBONUSCONDITION_HORDE );
	else
		us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_HORDE );

	if( allowNationalism && hasNationalism() )
	{
		us->setWeaponBonusCondition( WEAPONBONUSCONDITION_NATIONALISM );

		// FOR THE NEW GC INFANTRY GENERAL
		if( hasFanaticism() )
			us->setWeaponBonusCondition( WEAPONBONUSCONDITION_FANATICISM );
		else
			us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_FANATICISM );
	}
	else
		us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_NATIONALISM );
}

// ------------------------------------------------------------------------------------------------
// the fixed rule: all three bonuses follow the horde status, and fanaticism no longer needs
// nationalism to be owned as well.
// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::evaluateNationalismBonus( Bool inHorde, Bool allowNationalism )
{
	Object *us = getObject();

	if( inHorde )
	{
		us->setWeaponBonusCondition( WEAPONBONUSCONDITION_HORDE );

		if( allowNationalism && hasNationalism() )
			us->setWeaponBonusCondition( WEAPONBONUSCONDITION_NATIONALISM );
		else
			us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_NATIONALISM );

		if( allowNationalism && hasFanaticism() )
			us->setWeaponBonusCondition( WEAPONBONUSCONDITION_FANATICISM );
		else
			us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_FANATICISM );
	}
	else
	{
		us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_HORDE );
		us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_NATIONALISM );
		us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_FANATICISM );
	}
}

#ifdef ALLOW_DEMORALIZE
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::setDemoralized( UnsignedInt durationInFrames )
{
	UnsignedInt prevDemoralizedFrames = m_demoralizedFramesLeft;

	// overwrite the previous demoralized time left
	m_demoralizedFramesLeft = durationInFrames;

	// if we turned on or turned off we need to re-evaluate our bonus conditions
	if( (prevDemoralizedFrames == 0 && m_demoralizedFramesLeft > 0) ||
			(prevDemoralizedFrames > 0 && m_demoralizedFramesLeft == 0) )
	{

		// evaluate demoralization, nationalism, and horde effect as they are all intertwined
		Object *us = getObject();
		for( BehaviorModule** u = us->getBehaviorModules(); *u; ++u )
		{
			HordeUpdateInterface *hui = (*u)->getHordeUpdateInterface();
			if( hui )
				evaluateMoraleBonus( hui->isInHorde(), hui->isAllowedNationalism(), hui->getHordeActionType() );
		}

	}  // end if

}
#endif

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::privateCommandButton( const CommandButton *commandButton, CommandSourceType cmdSource )
{
	if( !commandButton )
	{
		return;
	}

	if (getObject()->isKindOf(KINDOF_PROJECTILE))
		return;

	//First of all, it's quite possible to get this far with an object incapable of performing such a task. Scripts will have
	//entire teams of multiple unit types and want to order units to do something... if they can, great.. if not, ignore.
	Object *owner = getObject();
	if( owner )
	{
		AIUpdateInterface *ai = owner->getAI();
		if( ai )
		{
			//Make sure the owner has the same command button.
			const CommandSet *commandSet = TheControlBar->findCommandSet( owner->getCommandSetString() );
			if( commandSet )
			{
				for( int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
				{
					const CommandButton *aCommandButton = commandSet->getCommandButton(i);
					if( commandButton == aCommandButton )
					{
						//We found the matching command button so now order the unit to do what the button wants.
						switch( commandButton->getCommandType() )
						{
							//ONLY NO TARGET VIA AI BUTTONS NEED BE IMPLEMENTED HERE!
							case GUI_COMMAND_STOP:
								ai->aiIdle( cmdSource );
								break;
							default:
								if( owner->getName().isNotEmpty() )
								{
									DEBUG_ASSERTCRASH( 0, ("AIUpdate::privateCommandButton() -- unit %s ('%s'), command %s not implemented.",
										owner->getTemplate()->getName().str(), owner->getName().str(), commandButton->getTextLabel().str() ) );
								}
								else
								{
									DEBUG_ASSERTCRASH( 0, ("AIUpdate::privateCommandButton() -- unit %s, command %s not implemented.",
										owner->getTemplate()->getName().str(), commandButton->getTextLabel().str() ) );
								}
						}
					}
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::privateCommandButtonPosition( const CommandButton *commandButton, const Coord3D *pos, CommandSourceType cmdSource )
{
	if( !commandButton )
	{
		return;
	}

	if (getObject()->isKindOf(KINDOF_PROJECTILE))
		return;

	//First of all, it's quite possible to get this far with an object incapable of performing such a task. Scripts will have
	//entire teams of multiple unit types and want to order units to do something... if they can, great.. if not, ignore.
	Object *owner = getObject();
	if( owner )
	{
		AIUpdateInterface *ai = owner->getAI();
		if( ai )
		{
			//Make sure the owner has the same command button.
			const CommandSet *commandSet = TheControlBar->findCommandSet( owner->getCommandSetString() );
			if( commandSet )
			{
				for( int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
				{
					const CommandButton *aCommandButton = commandSet->getCommandButton(i);
					if( commandButton == aCommandButton )
					{
						//We found the matching command button so now order the unit to do what the button wants.
						switch( commandButton->getCommandType() )
						{
							//LOCATION BASED COMMANDS ONLY VIA AI
							case GUI_COMMAND_NONE:
							default:
								if( owner->getName().isNotEmpty() )
								{
									DEBUG_ASSERTCRASH( 0, ("AIUpdate::privateCommandButtonPosition() -- unit %s ('%s'), command %s not implemented.",
										owner->getTemplate()->getName().str(), owner->getName().str(), commandButton->getTextLabel().str() ) );
								}
								else
								{
									DEBUG_ASSERTCRASH( 0, ("AIUpdate::privateCommandButtonPosition() -- unit %s, command %s not implemented.",
										owner->getTemplate()->getName().str(), commandButton->getTextLabel().str() ) );
								}
								break;
						}
					}
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::privateCommandButtonObject( const CommandButton *commandButton, Object *obj, CommandSourceType cmdSource )
{
	if( !commandButton )
	{
		return;
	}

	if (getObject()->isKindOf(KINDOF_PROJECTILE))
		return;

	//First of all, it's quite possible to get this far with an object incapable of performing such a task. Scripts will have
	//entire teams of multiple unit types and want to order units to do something... if they can, great.. if not, ignore.
	Object *owner = getObject();
	if( owner )
	{
		AIUpdateInterface *ai = owner->getAI();
		//Make sure the owner has the same command button.
		const CommandSet *commandSet = TheControlBar->findCommandSet( owner->getCommandSetString() );
		if( commandSet )
		{
			for( int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
			{
				const CommandButton *aCommandButton = commandSet->getCommandButton(i);
				if( commandButton == aCommandButton )
				{
					//We found the matching command button so now order the unit to do what the button wants.
					switch( commandButton->getCommandType() )
					{
						//OBJECT BASED COMMANDS ONLY VIA AI
						case GUI_COMMAND_COMBATDROP:
							if( ai )
							{
								ai->aiCombatDrop( obj, *(obj->getPosition()), cmdSource );
							}
							break;
						default:
						{
							AsciiString myName = owner->getTemplate()->getName().str();
							AsciiString myNickname;
							AsciiString targetName = obj->getTemplate()->getName().str();
							AsciiString targetNickname;
							if( owner->getName().isNotEmpty() )
							{
								myNickname.format( "('%s')", owner->getName().str() );
							}
							if( obj->getName().isNotEmpty() )
							{
								targetNickname.format( "('%s')", obj->getName().str() );
							}

							DEBUG_ASSERTCRASH( 0, ("AIUpdate::privateCommandButtonPosition() -- unit %s %s, command %s at unit %s %s not implemented.",
								myName.str(), myNickname.str(), commandButton->getTextLabel().str(), targetName.str(), targetNickname.str() ) );
						}
					}
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
AIGroup *AIUpdateInterface::getGroup(void)
{
	return getObject()->getGroup();
}


///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::crc( Xfer *x )
{
	CRCGEN_LOG(("AIUpdateInterface::crc() begin - %8.8X\n", ((XferCRC *)x)->getCRC()));
	// extend base class
	UpdateModule::crc( x );

	xfer(x);

	CRCGEN_LOG(("AIUpdateInterface::crc() end - %8.8X\n", ((XferCRC *)x)->getCRC()));

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 5: the production rally point and its flag
	* 6: the out-of-bounds xfer of m_guardTargetType is fixed */
// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::xfer( Xfer *xfer )
{
  // version
  const XferVersion currentVersion = 9;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );
 
 // extend base class
  UpdateModule::xfer( xfer );
 
	xfer->xferUnsignedInt(&m_priorWaypointID);
	xfer->xferUnsignedInt(&m_currentWaypointID);
	xfer->xferSnapshot(m_stateMachine);
	xfer->xferBool(&m_isAiDead);
	xfer->xferBool(&m_isRecruitable);

	xfer->xferUnsignedInt(&m_nextEnemyScanTime);		
	xfer->xferObjectID(&m_currentVictimID);	
	xfer->xferReal(&m_desiredSpeed);
	xfer->xferUser(&m_lastCommandSource, sizeof(m_lastCommandSource));
	if (version < 6)
	{
		// the original wrote m_guardTargetType[0] and [1], then [1] and [2] - and [2] is one past the
		// end of a two-element array, landing on m_locationToGuard.  Read an old save exactly the way
		// it was written, so it still loads.
		xfer->xferUser(&m_guardTargetType[0], sizeof(m_guardTargetType));
		xfer->xferUser(&m_guardTargetType[1], sizeof(m_guardTargetType[1]));
		xfer->xferUser(&m_locationToGuard, sizeof(m_guardTargetType[1]));
	}
	else
	{
		xfer->xferUser(m_guardTargetType, sizeof(m_guardTargetType));
	}

	xfer->xferCoord3D(&m_locationToGuard);

	xfer->xferObjectID(&m_objectToGuard);

	AsciiString triggerName;
	if (m_areaToGuard) triggerName = m_areaToGuard->getTriggerName();
	xfer->xferAsciiString(&triggerName);
	if (xfer->getXferMode() == XFER_LOAD)
	{
		if (triggerName.isNotEmpty()) {
			m_areaToGuard = TheTerrainLogic->getTriggerAreaByName(triggerName);
		}
	} 

	AsciiString attackName;
	if (m_attackInfo) attackName = m_attackInfo->getName();
	xfer->xferAsciiString(&attackName);
	if (xfer->getXferMode() == XFER_LOAD)
	{
		if (attackName.isNotEmpty()) {
			m_attackInfo = TheScriptEngine->getAttackInfo(attackName);
		}
	}  

	xfer->xferInt(&m_waypointCount);
	if (m_waypointCount<0 || m_waypointCount>MAX_WAYPOINTS) {
		DEBUG_CRASH(("Invalid waypoint count %d, max = %d", m_waypointCount, MAX_WAYPOINTS));
		throw SC_INVALID_DATA;
	}
	Int i;
	for (i=0; i<m_waypointCount; i++) {
		xfer->xferCoord3D(&m_waypointQueue[i]);
	}
	xfer->xferInt(&m_waypointIndex);
	xfer->xferBool(&m_executingWaypointQueue);

	UnsignedInt id = INVALID_WAYPOINT_ID;
	if (m_completedWaypoint) {
		id = m_completedWaypoint->getID();
	}
	xfer->xferUnsignedInt(&id);
	if (xfer->getXferMode() == XFER_LOAD)
	{
		m_completedWaypoint = TheTerrainLogic->getWaypointByID(id);
	}

	xfer->xferBool(&m_waitingForPath);
	Bool gotPath = (m_path != NULL);
	xfer->xferBool(&gotPath);
	if (xfer->getXferMode() == XFER_LOAD)	{
		if (gotPath) {
			m_path = newInstance(Path);
		}
	}
	if (gotPath) {
		xfer->xferSnapshot(m_path);
	}
	xfer->xferObjectID(&m_requestedVictimID);
	xfer->xferCoord3D(&m_requestedDestination);
	xfer->xferCoord3D(&m_requestedDestination2);

	// Not needed - we will recompute paths on load.
	//xfer->xferUnsignedInt(&m_pathTimestamp);		
	
	xfer->xferObjectID(&m_ignoreObstacleID);
	xfer->xferReal(&m_pathExtraDistance);
	xfer->xferICoord2D(&m_pathfindGoalCell);
	xfer->xferICoord2D(&m_pathfindCurCell);

	// Not needed - jba.
	//Int					m_blockedFrames;						///< Number of frames we've been blocked.
	//Real				m_curMaxBlockedSpeed;				///< Max speed we can have and not run into blocking things.
	//Bool				m_isBlocked;
	//Bool				m_isBlockedAndStuck;				///< True if we are stuck & need to recompute path.
	//Bool				m_isInUpdate;
	//Bool				m_fixLocoInPostProcess;

	xfer->xferUnsignedInt(&m_ignoreCollisionsUntil);
	xfer->xferUnsignedInt(&m_queueForPathFrame);
	xfer->xferCoord3D(&m_finalPosition);
	xfer->xferBool(&m_doFinalPosition);
	xfer->xferBool(&m_isAttackPath);
	xfer->xferBool(&m_isFinalGoal);
	xfer->xferBool(&m_isApproachPath);
	xfer->xferBool(&m_isSafePath);
	xfer->xferBool(&m_movementComplete);
	xfer->xferBool(&m_isSafePath);
	xfer->xferBool(&m_upgradedLocomotors);
	xfer->xferBool(&m_canPathThroughUnits);
	xfer->xferBool(&m_randomlyOffsetMoodCheck);
	xfer->xferObjectID(&m_repulsor1);
	xfer->xferObjectID(&m_repulsor2);

	if (version < 3)
	{
		Int lastFrameMoved = 0;
		xfer->xferInt(&lastFrameMoved);
	}

	xfer->xferObjectID(&m_moveOutOfWay1);
	xfer->xferObjectID(&m_moveOutOfWay2);

	if (xfer->getXferMode() == XFER_LOAD && version < 4)
	{
		// Read in from .ini
		//LocomotorSet			m_locomotorSet;
		AsciiString setName;
		if (m_curLocomotorSet > LOCOMOTORSET_INVALID && m_curLocomotorSet < LOCOMOTORSET_COUNT) 
			setName = TheLocomotorSetNames[m_curLocomotorSet];

		xfer->xferAsciiString(&setName);

		if (setName.isNotEmpty()) 
			m_curLocomotorSet = (LocomotorSetType)INI::scanIndexList(setName.str(), TheLocomotorSetNames);

		m_fixLocoInPostProcess = TRUE;
	}
	else
	{
		if (xfer->getXferMode() == XFER_LOAD)
		{
			// our ctor choose a NORMAL set for us. it's simpler
			// to simply clear out whatever we have here and allow 
			// xferSelfAndCurLocoPtr() to continue to require a pristine,
			// empty set. (srj)
			m_locomotorSet.clear();
			m_curLocomotor = NULL;
		}
		m_locomotorSet.xferSelfAndCurLocoPtr(xfer, &m_curLocomotor);
		xfer->xferUser(&m_curLocomotorSet, sizeof(m_curLocomotorSet));
	}

	xfer->xferUser(&m_locomotorGoalType, sizeof(m_locomotorGoalType));
	xfer->xferCoord3D(&m_locomotorGoalData);

	for (i=0; i<MAX_TURRETS; i++) {
		if (m_turretAI[i]) {
			xfer->xferSnapshot(m_turretAI[i]);
		}
	}
	xfer->xferUser(&m_turretSyncFlag, sizeof(m_turretSyncFlag));
	xfer->xferUser(&m_attitude, sizeof(m_attitude));

	xfer->xferUnsignedInt(&m_nextMoodCheckTime);
	if (version == 1)	
	{
		// surrender + demoralize
#ifdef ALLOW_DEMORALIZE
		xfer->xferUnsignedInt(&m_demoralizedFramesLeft);
#else
		UnsignedInt demoralizedFramesLeft = 0;
		xfer->xferUnsignedInt(&demoralizedFramesLeft);
#endif
#ifdef ALLOW_SURRENDER
		xfer->xferUnsignedInt(&m_surrenderedFramesLeft);
		xfer->xferInt(&m_surrenderedPlayerIndex);
#else
		UnsignedInt surrenderedFramesLeft = 0;
		Int surrenderedPlayerIndex = 0;
		xfer->xferUnsignedInt(&surrenderedFramesLeft);
		xfer->xferInt(&surrenderedPlayerIndex);
#endif
	}
	else if (version == 2)
	{
#ifdef ALLOW_SURRENDER
		DEBUG_CRASH(("fix me ALLOW_SURRENDER"));	// should not happen
#endif
		// demoralize only
#ifdef ALLOW_DEMORALIZE
		xfer->xferUnsignedInt(&m_demoralizedFramesLeft);
#else
		UnsignedInt tmp0 = 0;
		xfer->xferUnsignedInt(&tmp0);
#endif
	}
	else
	{
		// else no surrender or demoralize
#ifdef ALLOW_SURRENDER
		DEBUG_CRASH(("fix me ALLOW_SURRENDER"));	// should not happen
#endif
#ifdef ALLOW_DEMORALIZE
		DEBUG_CRASH(("fix me ALLOW_DEMORALIZE"));	// should not happen
#endif
	}

	xfer->xferObjectID(&m_crateCreated);
	if (version < 3)
	{
		Int repulsorCountdown = 0;
		xfer->xferInt(&repulsorCountdown);
	}

	if (version >= 5)
	{
		xfer->xferCoord3D(&m_exitProductionRallyPoint);
		xfer->xferBool(&m_hasExitProductionRallyPoint);
	}

	if (version >= 7)
	{
		// the lane across the route.  An older save has none, and the ctor's 0.5 / not-valid means
		// every loaded unit re-seeds itself the first frame it drives, which is the right answer.
		xfer->xferReal(&m_laneFraction);
		xfer->xferBool(&m_laneFractionValid);
		xfer->xferUnsignedInt(&m_laneHoldFrame);
	}

	if (version >= 8)
	{
		// a lane handed down by the ordering group and not yet taken up.  Only a save made in the
		// few frames between the order and the path arriving carries one.
		xfer->xferReal(&m_pendingLane);
		xfer->xferBool(&m_hasPendingLane);
	}

	if (version >= 9)
	{
		/* -crowd's lane, which is a distance and not a share, plus how long the unit has been held
			 up - a save made in the middle of a jam that came back with everybody patient again would
			 restart the jam from the beginning.  The band itself is not saved: it is derived from the
			 route and is rebuilt the first frame after the load. */
		xfer->xferReal(&m_crowdLat);
		xfer->xferBool(&m_crowdLatValid);
		xfer->xferReal(&m_pendingCrowdLat);
		xfer->xferBool(&m_hasPendingCrowdLat);
		xfer->xferUnsignedInt(&m_crowdHoldFrame);
		xfer->xferInt(&m_crowdQueued);
		xfer->xferInt(&m_crowdSide);
	}


}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::loadPostProcess( void )
{
	UpdateModule::loadPostProcess();

	if (m_fixLocoInPostProcess && m_curLocomotorSet!=LOCOMOTORSET_INVALID) 
	{
		m_fixLocoInPostProcess = FALSE;

		LocomotorSetType lst = m_curLocomotorSet;
		// Set the current to invalid, because chooseLocomotorSet aborts if it is already set to the desired value.
		m_curLocomotorSet = LOCOMOTORSET_INVALID;
		chooseLocomotorSet(lst);
	}

	if (!isMoving()) {
		m_pathfindGoalCell.x = -1;
		m_pathfindGoalCell.y = -1;
		TheAI->pathfinder()->updateGoal(getObject(), getObject()->getPosition(), getObject()->getLayer());
		m_pathfindCurCell.x = -1;
		m_pathfindCurCell.y = -1;
		TheAI->pathfinder()->updatePos(getObject(), getObject()->getPosition());
	}	else {
		if (m_pathfindGoalCell.x >= 0 && m_pathfindGoalCell.y >= 0) {
			Coord3D goalPos;
			goalPos.x = m_pathfindGoalCell.x * PATHFIND_CELL_SIZE_F + PATHFIND_CELL_SIZE_F*0.5f;
			goalPos.y = m_pathfindGoalCell.y * PATHFIND_CELL_SIZE_F + PATHFIND_CELL_SIZE_F*0.5f;
			m_pathfindGoalCell.x = -1;
			m_pathfindGoalCell.y = -1;
			TheAI->pathfinder()->updateGoal(getObject(), &goalPos, getObject()->getLayer());
		}
	}

}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Int AIUpdateInterface::friend_getWaypointGoalPathSize() const 
{ 
			//
			// it is VERY IMPORTANT to check for the current state type as being follow-path, 
			// because "getGoalPath" and friends are used for other things (eg, jet takeoff and landing).
			// if you don't do this check, you will end up with really bizarre behavior in obscure jet-related
			// cases, and our users will all laugh at us.
			//
			// the goalpath should really be completely private, but at this point, this ugly scheme
			// has to be lived with. (srj)
			//
	if (getAIStateType() != AI_FOLLOW_PATH)
		return 0;

	return getStateMachine()->getGoalPathSize(); 
}

// ------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::hasLocomotorForSurface(LocomotorSurfaceType surfaceType)
{
	LocomotorSurfaceTypeMask surfaceMask = (LocomotorSurfaceTypeMask)surfaceType;
	if (m_locomotorSet.findLocomotor(surfaceMask))
		return TRUE;
	else
		return FALSE;
}

// ------------------------------------------------------------------------------------------------
