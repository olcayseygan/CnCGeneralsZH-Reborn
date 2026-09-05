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

// FILE: GUIUtil.h //////////////////////////////////////////////////////
// Author: Matthew D. Campbell, Sept 2002

#pragma once

#ifndef __GUIUTIL_H__
#define __GUIUTIL_H__

class GameWindow;
class GameInfo;

void ShowUnderlyingGUIElements( Bool show, const char *layoutFilename, const char *parentName,
															 const char **gadgetsToHide, const char **perPlayerGadgetsToHide );

// Every seat a lobby can offer - Open, Closed and the six rungs of the AI ladder - in one list,
// each entry tagged with the SlotState it stands for.  allowTakeover adds the empty seat you
// take over in game, which only the skirmish lobby offers.
void PopulatePlayerSlotComboBox(GameWindow *comboBox, Int color, Bool allowTakeover);
void PopulateColorComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool isObserver = FALSE);
void PopulatePlayerTemplateComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool allowObservers );
void PopulateTeamComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool isObserver = FALSE);
void PopulateStartingCashComboBox(GameWindow *comboBox, GameInfo *myGame);

// The peace time rungs both network lobbies offer, from one list so they cannot drift apart.
// UpdatePeaceTimeComboBox puts the box back on whatever the host last sent and greys it out when
// nobody may touch it - a client, or any lobby with a computer player in it, which has no peace
// time.  PeaceTimeFromComboBox reads the minutes back out of the entry the user picked.
void PopulatePeaceTimeComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
void UpdatePeaceTimeComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
Int PeaceTimeFromComboBox(GameWindow *comboBox);

void EnableSlotListUpdates( Bool val );
Bool AreSlotListUpdatesEnabled( void );

void UpdateSlotList( GameInfo *myGame, GameWindow *comboPlayer[],
										GameWindow *comboColor[], GameWindow *comboPlayerTemplate[],
										GameWindow *comboTeam[], GameWindow *buttonAccept[], 
										GameWindow *buttonStart, GameWindow *buttonMapStartPosition[] );

void EnableAcceptControls(Bool Enabled, GameInfo *myGame, GameWindow *comboPlayer[],
										GameWindow *comboColor[], GameWindow *comboPlayerTemplate[],
										GameWindow *comboTeam[], GameWindow *buttonAccept[], GameWindow *buttonStart,
										GameWindow *buttonMapStartPosition[], Int slotNum = -1);

#endif // __GUIUTIL_H__
