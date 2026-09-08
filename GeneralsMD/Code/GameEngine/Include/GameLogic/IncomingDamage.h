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

// IncomingDamage.h ///////////////////////////////////////////////////////////////////////////////
// The ledger of damage that is already on its way to a target but has not landed yet.
//
// A shot whose damage is delayed - a projectile in flight, or a hitscan weapon far enough away that
// the damage is scheduled for a later frame - leaves the shooter's barrel with nothing anywhere
// recording that the victim is, for practical purposes, already dead.  Every other unit still sees
// full health and keeps firing, so a squad routinely spends a whole volley on a target the first
// two shots had already killed.
//
// Every delayed shot is booked here against its victim, with the frame its damage is expected to
// land.  Auto-targeting reads the ledger back: a target whose booked damage already covers its
// remaining health is skipped when acquiring, and a unit that is auto-attacking one stops firing
// and looks for something else.  Nothing here touches an explicit player attack order.
//
// Entries expire on their own.  A booking is dropped when its damage lands, and otherwise a few
// frames after the impact was due, so a missile that is shot down or diverted by countermeasures
// costs at most that long a pause rather than a permanent reservation.
//
// The ledger is logic-side and iterated in ObjectID order, so it stays deterministic across clients
// and replays.  It is deliberately not saved: bookings live under a second of game time, and a
// loaded game simply re-books as its units fire again.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#ifndef __INCOMINGDAMAGE_H__
#define __INCOMINGDAMAGE_H__

#include "Lib/BaseType.h"
#include "Common/GameCommon.h"

class Object;

//-------------------------------------------------------------------------------------------------
class IncomingDamageTracker
{
public:

	/// Drop every booking. Called when the game logic resets.
	static void reset();

	/// Drop the bookings whose impact frame has come and gone. Called once per logic frame.
	static void update(UnsignedInt currentFrame);

	/**
		Book a shot's damage against its victim, expected to land on impactFrame. The amount is what
		the shot is estimated to actually take off the victim, i.e. after armor.
	*/
	static void bookShot(ObjectID victim, ObjectID shooter, Real amount,
											 UnsignedInt currentFrame, UnsignedInt impactFrame);

	/// A shot from this shooter has landed on this victim: release the oldest booking of the pair.
	static void shotLanded(ObjectID victim, ObjectID shooter);

	/// Total damage booked against this victim that has not landed yet.
	static Real getBookedDamage(ObjectID victim);

	/**
		TRUE if the damage already on its way to this victim covers remainingHealth, i.e. shooting it
		again would be wasted. Always FALSE for an object nothing has booked damage against, so the
		common case costs one lookup in an almost always tiny table.
	*/
	static Bool isAlreadyDoomed(ObjectID victim, Real remainingHealth);

	/// The same question asked about a live object, whose health it reads for you.
	static Bool isAlreadyDoomed(const Object *victim);

	/**
		Say, before the round leaves the barrel, that this shooter means to put this much damage into
		this victim. A shot that is still being aimed or is inside its wind-up is worth announcing:
		until it is fired there is nothing in the ledger, so a whole flight commits to the same target
		and only finds out on the frame the first missile launches. Re-announcing refreshes the claim
		and it lapses within a few frames of the last one, so a unit that changes its mind releases it
		by falling silent.
	*/
	static void claimShot(ObjectID victim, ObjectID shooter, Real amount, UnsignedInt currentFrame);

	/**
		TRUE if somebody else has this victim covered: damage in flight plus the shots other units
		have announced already exceed its health. The asker's own claim is left out, so a unit is
		never talked out of the shot it is lining up. This is the question a unit asks before
		committing; isAlreadyDoomed, which counts only fire that is genuinely in the air, is the
		question to ask when an announcement is not good enough.
	*/
	static Bool isSpokenFor(const Object *victim, ObjectID asker);

};

#endif // __INCOMINGDAMAGE_H__
