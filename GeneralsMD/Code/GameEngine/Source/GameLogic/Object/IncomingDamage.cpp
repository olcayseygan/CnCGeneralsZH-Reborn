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

// IncomingDamage.cpp /////////////////////////////////////////////////////////////////////////////
// The ledger of damage in flight. See IncomingDamage.h for what it is for.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameLogic/IncomingDamage.h"

#include "GameLogic/Object.h"
#include "GameLogic/Module/BodyModule.h"

#include <map>
#include <vector>

//-------------------------------------------------------------------------------------------------
struct BookedShot
{
	ObjectID			m_shooter;			///< who fired it, so the landing damage can release its own booking
	Real					m_amount;				///< damage it is expected to take off the victim, after armor
	UnsignedInt		m_expireFrame;	///< when the booking lapses even though nothing landed
	Bool					m_isClaim;			///< an announced shot rather than one in the air: invisible to its own shooter
	UnsignedInt		m_claimFrame;		///< when the claim was first made, which is what decides who gets the target
};

typedef std::vector<BookedShot> BookedShotVec;

// std::map, keyed by ObjectID: the sweep below walks it in ID order on every client.
typedef std::map<ObjectID, BookedShotVec> BookedShotMap;

static BookedShotMap theBookings;

//
// How long past the expected impact a booking is honored before it lapses.  A missile that is shot
// down or lured away by countermeasures never lands, and this is how long the target stays wrongly
// reserved: long enough to absorb the slop in the flight-time estimate below, short enough that the
// squad's pause is not noticeable.
//
static const UnsignedInt BOOKING_SLACK_FRAMES = 15;

// Flight time is estimated from the weapon speed, which for a guided projectile is only a hint --
// the projectile flies on its own locomotor. Clamp the estimate rather than trust it outright.
static const UnsignedInt MIN_FLIGHT_FRAMES = 1;
static const UnsignedInt MAX_FLIGHT_FRAMES = 90;

//
// How long an announced shot stands without being repeated.  A unit that is still lining the shot up
// says so again every frame, so this only has to outlast one frame; the margin is there so a unit
// whose update is skipped for a frame does not have to start over, and it is short enough that a
// unit which gives up on the target frees it again within a fifth of a second.
//
static const UnsignedInt CLAIM_HOLD_FRAMES = 6;

//-------------------------------------------------------------------------------------------------
/** Damage genuinely in the air against a victim.  Announcements are not counted. */
//-------------------------------------------------------------------------------------------------
static Real sumShotsInFlight(ObjectID victim)
{
	BookedShotMap::const_iterator vic = theBookings.find(victim);
	if (vic == theBookings.end())
		return 0.0f;

	Real total = 0.0f;
	const BookedShotVec& shots = vic->second;
	for (BookedShotVec::const_iterator s = shots.begin(); s != shots.end(); ++s)
	{
		if (!s->m_isClaim)
			total += s->m_amount;
	}

	return total;
}

//-------------------------------------------------------------------------------------------------
/** Take back this shooter's announcement against this victim, if it has one standing. */
//-------------------------------------------------------------------------------------------------
static void releaseClaim(ObjectID victim, ObjectID shooter)
{
	BookedShotMap::iterator vic = theBookings.find(victim);
	if (vic == theBookings.end())
		return;

	BookedShotVec& shots = vic->second;
	for (BookedShotVec::iterator s = shots.begin(); s != shots.end(); ++s)
	{
		if (s->m_isClaim && s->m_shooter == shooter)
		{
			shots.erase(s);
			break;
		}
	}

	if (shots.empty())
		theBookings.erase(vic);
}

//-------------------------------------------------------------------------------------------------
void IncomingDamageTracker::reset()
{
	theBookings.clear();
}

//-------------------------------------------------------------------------------------------------
void IncomingDamageTracker::update(UnsignedInt currentFrame)
{
	for (BookedShotMap::iterator vic = theBookings.begin(); vic != theBookings.end(); )
	{
		BookedShotVec& shots = vic->second;
		for (BookedShotVec::iterator s = shots.begin(); s != shots.end(); )
		{
			if (currentFrame >= s->m_expireFrame)
				s = shots.erase(s);
			else
				++s;
		}

		if (shots.empty())
		{
			// ObjectIDs are never reused, so an emptied entry is gone for good.
			BookedShotMap::iterator dead = vic;
			++vic;
			theBookings.erase(dead);
		}
		else
		{
			++vic;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void IncomingDamageTracker::bookShot(ObjectID victim, ObjectID shooter, Real amount,
																		 UnsignedInt currentFrame, UnsignedInt impactFrame)
{
	if (victim == INVALID_ID || amount <= 0.0f)
		return;

	UnsignedInt flight = (impactFrame > currentFrame) ? (impactFrame - currentFrame) : MIN_FLIGHT_FRAMES;
	if (flight < MIN_FLIGHT_FRAMES)
		flight = MIN_FLIGHT_FRAMES;
	else if (flight > MAX_FLIGHT_FRAMES)
		flight = MAX_FLIGHT_FRAMES;

	// the round is away, so the announcement that preceded it has done its job
	releaseClaim(victim, shooter);

	BookedShot shot;
	shot.m_shooter = shooter;
	shot.m_amount = amount;
	shot.m_expireFrame = currentFrame + flight + BOOKING_SLACK_FRAMES;
	shot.m_isClaim = FALSE;
	shot.m_claimFrame = 0;

	theBookings[victim].push_back(shot);
}

//-------------------------------------------------------------------------------------------------
void IncomingDamageTracker::claimShot(ObjectID victim, ObjectID shooter, Real amount,
																			UnsignedInt currentFrame)
{
	if (victim == INVALID_ID || shooter == INVALID_ID || amount <= 0.0f)
		return;

	const UnsignedInt expire = currentFrame + CLAIM_HOLD_FRAMES;

	BookedShotVec& shots = theBookings[victim];
	for (BookedShotVec::iterator s = shots.begin(); s != shots.end(); ++s)
	{
		if (s->m_isClaim && s->m_shooter == shooter)
		{
			// saying it again only stands the same claim up for longer: the frame it was first made on
			// is what holds this unit's place in the queue for the target, so it is left alone
			s->m_amount = amount;
			s->m_expireFrame = expire;
			return;
		}
	}

	BookedShot claim;
	claim.m_shooter = shooter;
	claim.m_amount = amount;
	claim.m_expireFrame = expire;
	claim.m_isClaim = TRUE;
	claim.m_claimFrame = currentFrame;

	shots.push_back(claim);
}

//-------------------------------------------------------------------------------------------------
void IncomingDamageTracker::shotLanded(ObjectID victim, ObjectID shooter)
{
	BookedShotMap::iterator vic = theBookings.find(victim);
	if (vic == theBookings.end())
		return;

	BookedShotVec& shots = vic->second;
	for (BookedShotVec::iterator s = shots.begin(); s != shots.end(); ++s)
	{
		if (s->m_shooter == shooter && !s->m_isClaim)
		{
			// oldest first: a shooter's shots land in the order they were fired
			shots.erase(s);
			break;
		}
	}

	if (shots.empty())
		theBookings.erase(vic);
}

//-------------------------------------------------------------------------------------------------
Real IncomingDamageTracker::getBookedDamage(ObjectID victim)
{
	return sumShotsInFlight(victim);
}

//-------------------------------------------------------------------------------------------------
Bool IncomingDamageTracker::isAlreadyDoomed(ObjectID victim, Real remainingHealth)
{
	const Real booked = getBookedDamage(victim);
	if (booked <= 0.0f)
		return FALSE;

	return booked >= remainingHealth;
}

//-------------------------------------------------------------------------------------------------
Bool IncomingDamageTracker::isAlreadyDoomed(const Object *victim)
{
	return isAlreadyDoomed(victim->getID(), victim->getBodyModule()->getHealth());
}

//-------------------------------------------------------------------------------------------------
Bool IncomingDamageTracker::isSpokenFor(const Object *victim, ObjectID asker)
{
	BookedShotMap::const_iterator vic = theBookings.find(victim->getID());
	if (vic == theBookings.end())
		return FALSE;

	const BookedShotVec& shots = vic->second;

	// where the asker stands in the queue for this victim, if it has announced anything at all
	Bool askerHasClaim = FALSE;
	UnsignedInt askerClaimFrame = 0;
	for (BookedShotVec::const_iterator s = shots.begin(); s != shots.end(); ++s)
	{
		if (s->m_isClaim && s->m_shooter == asker)
		{
			askerHasClaim = TRUE;
			askerClaimFrame = s->m_claimFrame;
			break;
		}
	}

	Real covered = 0.0f;
	for (BookedShotVec::const_iterator s = shots.begin(); s != shots.end(); ++s)
	{
		if (s->m_isClaim)
		{
			if (s->m_shooter == asker)
				continue;

			//
			// Only an announcement that got in ahead of ours stops us.  Two units that each defer to
			// the other both hold fire and the target is never shot at all, so the claims are ordered:
			// whoever spoke first keeps the target, and two that spoke on the same frame are settled by
			// object id, which every machine in a network game agrees on.  A unit that has announced
			// nothing - one scanning for something to acquire - is behind all of them.
			//
			const Bool spokeFirst = !askerHasClaim
														|| s->m_claimFrame < askerClaimFrame
														|| (s->m_claimFrame == askerClaimFrame && s->m_shooter < asker);
			if (!spokeFirst)
				continue;
		}

		covered += s->m_amount;
	}

	if (covered <= 0.0f)
		return FALSE;

	return covered >= victim->getBodyModule()->getHealth();
}
