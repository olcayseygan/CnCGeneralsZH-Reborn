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

// FILE: UiAnimClock.h ////////////////////////////////////////////////////////
//
// A wall clock for the fixed-step client animations.
//
// The renderer is not frame-rate capped any more. Everything that used to
// advance one step per rendered frame - the menu window animations, the window
// transitions, the control bar scheme, the screen filter fades - was authored
// against 30fps and now runs at whatever the machine manages, so a menu
// transition covers its whole travel in a few milliseconds and reads as a snap.
// Each of them asks the function below whether enough real time has gone by to
// take its next step.
//
// This lives in its own header, deliberately: its callers are spread across
// GameEngine and GameEngineDevice, and the natural home
// (AnimateWindowManager.h) names std::list without including it, so it does not
// parse everywhere this is needed.
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __UIANIMCLOCK_H_
#define __UIANIMCLOCK_H_

#include "Lib/BaseType.h"

//-----------------------------------------------------------------------------
/**
	* Pace a fixed-step UI animation off the wall clock.
	*
	* 'lastMs' and 'accumMs' are the caller's state, zeroed when the animation is
	* (re)started - a zero 'lastMs' seeds itself from 'nowMs' and returns FALSE,
	* so a freshly started animation holds its first frame rather than taking two
	* steps on the frame it starts.
	*
	* Elapsed time is clamped to a single step, so a stall - a level load, a
	* dragged window - cannot bank up a burst of steps and jump the animation
	* forward when it ends.
	*
	* Returns TRUE at most once per step period.
	*/
Bool GameClient_isUiAnimStepDue( UnsignedInt &lastMs, Real &accumMs, UnsignedInt nowMs,
																 Real stepsPerSec );

/// what the menus were authored against, and what everything using the helper above steps at
const Real UI_ANIM_STEPS_PER_SEC = 30.0f;

/** The same rate with the player's `MenuTransitionSpeed` percent applied. Menu slides and fades ask
	* for this; the control bar and the shader manager keep the authored rate, because those are the
	* game's own animations rather than the shell's ceremony. Kept out of the pure helper above so
	* that stays testable without a GlobalData. */
Real GameClient_menuAnimStepsPerSec( void );

#endif // __UIANIMCLOCK_H_
