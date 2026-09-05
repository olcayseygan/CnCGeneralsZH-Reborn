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

// FILE: KeyboardOptionsMenu.cpp /////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:   Command & Conquer: Generals
//
// File name: KeyboardOptionsMenu.cpp
//
// Created:   Chris Brue, July 2002
//
// Desc:      the Keyboard options window control
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/UserPreferences.h"

#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetSlider.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/IMEManager.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Mouse.h"
#include "GameClient/GameText.h"
#include "GameClient/MetaEvent.h"

#include "GameNetwork/FirewallHelper.h"
#include "GameNetwork/IPEnumeration.h"

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
WindowMsgHandledType KeyboardTextEntryInput( GameWindow *window, UnsignedInt msg,
													 WindowMsgData mData1, WindowMsgData mData2 );

static NameKeyType buttonBackID = NAMEKEY_INVALID;
static GameWindow *buttonBack = NULL;

static NameKeyType parentKeyboardOptionsMenuID = NAMEKEY_INVALID;
static GameWindow *parentKeyboardOptionsMenu = NULL;

static NameKeyType comboBoxCategoryListID = NAMEKEY_INVALID;
static GameWindow *comboBoxCategoryList = NULL;

static NameKeyType listBoxCommandListID = NAMEKEY_INVALID;
static GameWindow *listBoxCommandList   = NULL;

static NameKeyType staticTextDescriptionID = NAMEKEY_INVALID;
static GameWindow *staticTextDescription   = NULL;

static NameKeyType staticTextCurrentHotkeyID = NAMEKEY_INVALID;
static GameWindow *staticTextCurrentHotkey     = NULL;

static NameKeyType buttonResetAllID = NAMEKEY_INVALID;
static GameWindow *buttonResetAll   = NULL;

static NameKeyType textEntryAssignHotkeyID = NAMEKEY_INVALID;
static GameWindow *textEntryAssignHotkey   = NULL;

static NameKeyType buttonAssignID = NAMEKEY_INVALID;
static GameWindow *buttonAssign = NULL;

//use Bools to test if modifiers are used

Bool shiftDown = false;
Bool altDown = false;
Bool ctrlDown = false;

// shows whether or not a correctly formatted hotkey assignment is in the text area
Bool absolute = false;

//
// What the screen is working on: the command whose row is selected, and the key the player has
// pressed into the box but not yet accepted.  The box captures a keystroke whole - key and
// modifiers together, on the way down - rather than assembling a string out of the letters typed
// into it, which is what it used to do and which could never produce a binding at the end.
//
static MetaMapRec *theSelectedRec = NULL;
static MappableKeyType thePendingKey = MK_NONE;
static MappableKeyModState thePendingMods = NONE;

static UnicodeString bindingText( const MetaMapRec *rec );
static UnicodeString bindingTextFor( MappableKeyType key, MappableKeyModState mods );
static Bool catchBindingKey( WindowMsgData mData1, WindowMsgData mData2 );

// initialize these, they will be used a lot
UnicodeString alt;
UnicodeString ctrl;
UnicodeString shift;



//-------------------------------------------------------------------------------------------------
/** There is no category dropdown any more.
	*
	* It was a filter over a list, on a screen whose whole job is to answer "what is bound to what" -
	* so it hid seven eighths of the answer behind a control you had to work.  Every command is in
	* the one list now, in category order, with the category written across the row above its
	* commands.  This is kept as a no-op because the screen's message handler still names it. */
//-------------------------------------------------------------------------------------------------
void populateCategoryBox()
{
}

// keeps track of whether or not each text modifier is being currently displayed in the text entry field
void setKeyDown( UnicodeString mod, Bool b )
{
	if( mod == TheGameText->fetch( "KEYBOARD:Shift+" ) )
		shiftDown = b;
	else if( mod == TheGameText->fetch( "KEYBOARD:Ctrl+" ) )
		ctrlDown = b;
	else
		altDown = b;
}

//-------------------------------------------------------------------------------------------------
/** A binding as a player reads it: "Ctrl+A", "F9", or nothing at all. */
//-------------------------------------------------------------------------------------------------
static UnicodeString bindingTextFor( MappableKeyType key, MappableKeyModState mods )
{
	if( key == MK_NONE )
		return TheGameText->fetch( "GUI:NULL" );

	UnicodeString out;
	if( mods & CTRL )
		out.concat( TheGameText->fetch( "KEYBOARD:Ctrl+" ) );
	if( mods & ALT )
		out.concat( TheGameText->fetch( "KEYBOARD:Alt+" ) );
	if( mods & SHIFT )
		out.concat( TheGameText->fetch( "KEYBOARD:Shift+" ) );

	// the KEY_ names are what the INI spells, and the "KEY_" in front of them is noise on a screen
	for( const LookupListRec *keyName = KeyNames; keyName->name; keyName++ )
	{
		if( keyName->value != (Int)key )
			continue;

		const char *bare = keyName->name;
		if( strncmp( bare, "KEY_", 4 ) == 0 )
			bare += 4;

		UnicodeString uStr;
		uStr.translate( AsciiString( bare ) );
		out.concat( uStr );
		break;
	}

	return out;
}

//-------------------------------------------------------------------------------------------------
static UnicodeString bindingText( const MetaMapRec *rec )
{
	if( rec == NULL )
		return TheGameText->fetch( "GUI:NULL" );
	return bindingTextFor( rec->m_key, rec->m_modState );
}

//-------------------------------------------------------------------------------------------------
/** Take one keystroke as the binding the player wants, and show it in the box.
	*
	* Whole, on the way down: the key and whatever of ctrl, alt and shift was held with it.  Both the
	* box and the screen behind it offer their keystrokes here, because which of the two has the
	* keyboard focus is not something a player should have to know - clicking a row is the whole
	* interaction, and the next key you press is the answer to it. */
//-------------------------------------------------------------------------------------------------
static Bool catchBindingKey( WindowMsgData mData1, WindowMsgData mData2 )
{
	if( theSelectedRec == NULL || textEntryAssignHotkey == NULL )
		return FALSE;

	const UnsignedByte key = (UnsignedByte)mData1;
	const UnsignedShort state = (UnsignedShort)mData2;

	if( !BitTest( state, KEY_STATE_DOWN ) || BitTest( state, KEY_STATE_AUTOREPEAT ) )
		return FALSE;

	// escape leaves the screen; the modifier keys are what you hold, not what you bind
	if( key == KEY_ESC )
		return FALSE;
	if( key == KEY_LCTRL || key == KEY_RCTRL || key == KEY_LALT || key == KEY_RALT ||
			key == KEY_LSHIFT || key == KEY_RSHIFT )
		return TRUE;

	Int mods = 0;
	if( BitTest( state, KEY_STATE_CONTROL ) )	mods |= CTRL;
	if( BitTest( state, KEY_STATE_ALT ) )			mods |= ALT;
	if( BitTest( state, KEY_STATE_SHIFT ) )		mods |= SHIFT;

	thePendingKey = (MappableKeyType)key;
	thePendingMods = (MappableKeyModState)mods;

	EntryData *e = (EntryData *)textEntryAssignHotkey->winGetUserData();
	if( e && e->text )
	{
		e->text->setText( bindingTextFor( thePendingKey, thePendingMods ) );
		e->charPos = e->text->getTextLength();
	}

	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** The category the combo box is showing.  Its rows are the CategoryListName table in order, and
	* the row's *value* is the category - not its index, which is only the same by luck. */
//-------------------------------------------------------------------------------------------------
static MappableKeyCategories currentCategory( void )
{
	// every category is in the one list; the argument is only there because the signature is
	return CATEGORY_CONTROL;
}

//-------------------------------------------------------------------------------------------------
/** Every command in this category, each with the key it is on right now.
	*
	* The key is in the row rather than only in the box below, so the screen answers "what is bound
	* to what" at a glance - which is the question somebody opens it with - and the record itself is
	* the row's data, so a selection is exact instead of matched back by display name. */
//-------------------------------------------------------------------------------------------------
void fillCommandListBox( MappableKeyCategories cat )
{
	if(!listBoxCommandList)
		return;

	GadgetListBoxReset(listBoxCommandList);

	const Color color = GameMakeColor( 255, 255, 255, 255 );
	const Color heading = GameMakeColor( 255, 220, 120, 255 );

	for( Int c = 0; c < CATEGORY_NUM_CATEGORIES; ++c )
	{
		const MappableKeyCategories thisCat = (MappableKeyCategories)CategoryListName[ c ].value;

		//
		// The debug commands are compiled out of a shipping build, so binding one would be binding a
		// key to nothing at all.  Everything else the game answers to is here.
		//
		if( thisCat == CATEGORY_DEBUG )
			continue;

		Bool wroteHeading = FALSE;

		for(const MetaMapRec *rec = TheMetaMap->getFirstMetaMapRec(); rec; rec = rec->m_next)
		{
			if( rec->m_category != thisCat || rec->m_displayName.isEmpty() )
				continue;

			if( !wroteHeading )
			{
				AsciiString key;
				key.format( "GUI:%s", CategoryListName[ c ].name );
				Int head = GadgetListBoxAddEntryText( listBoxCommandList,
																							TheGameText->fetch( key ), heading, -1, -1 );
				// a heading is not a command: no record, so a click on it selects nothing
				if( head >= 0 )
					GadgetListBoxSetItemData( listBoxCommandList, NULL, head );
				wroteHeading = TRUE;
			}

			UnicodeString row( L"    " );
			row.concat( rec->m_displayName );
			row.concat( UnicodeString( L"  -  " ) );
			row.concat( bindingText( rec ) );

			Int at = GadgetListBoxAddEntryText(listBoxCommandList, row, color, -1, -1 );
			if( at >= 0 )
				GadgetListBoxSetItemData( listBoxCommandList, (void *)rec, at );
		}
	}
}

void doKeyUp(EntryData *e, UnicodeString mod )
{
	char c = e->text->getText().getCharAt( e->text->getTextLength() - 1);
	// if there are modifiers, check which ones exist and act accordingly
	if( c == '+' )
	{
		// if all of the mods are down, make string out of other two
		if( altDown && ctrlDown && shiftDown )
		{
			if( mod == shift )
			{
				UnicodeString temp = alt;
				temp.concat( ctrl );
				e->text->setText( temp );
				e->charPos = e->text->getTextLength();
				setKeyDown( mod, false );
			}
			else if( mod == alt )
			{
				UnicodeString temp = ctrl;
				temp.concat( shift );
				e->text->setText( temp );
				e->charPos = e->text->getTextLength();
				setKeyDown( mod, false );
			}
			else if( mod == ctrl )
			{
				UnicodeString temp = alt;
				temp.concat( shift );
				e->text->setText( temp );
				e->charPos = e->text->getTextLength();
				setKeyDown( mod, false );
			}
		}
		// if alt and ctrl are both down
		else if( altDown && ctrlDown )
		{
			if( mod == alt )
			{
				e->text->setText( ctrl );
				e->charPos = e->text->getTextLength();
				setKeyDown( mod, false );
			}
			else if( mod == ctrl )
			{
				e->text->setText( ctrl );
				e->charPos = e->text->getTextLength();
				setKeyDown( mod, false );
			}
		}
		// if alt and shift are both down
		else if( altDown && shiftDown )
		{
			if( mod == alt )
			{
				e->text->setText( shift );
				e->charPos = e->text->getTextLength();
				setKeyDown( mod, false );
			}
			else if( mod == shift )
			{
				e->text->setText( alt );
				e->charPos = e->text->getTextLength();
				setKeyDown( mod, false );
			}
		}
		// if ctrl and shift are both down
		else if( ctrlDown && shiftDown )
		{
			if( mod == ctrl )
			{
				e->text->setText( shift );
				e->charPos = e->text->getTextLength();
				setKeyDown( mod, false );
			}
			else if( mod == shift )
			{
				e->text->setText( ctrl );
				e->charPos = e->text->getTextLength();
				setKeyDown( mod, false );
			}
		}
		// else only one mod, just clear everything
		else
		{
			e->text->setText( UnicodeString::TheEmptyString );
			e->sText->setText( UnicodeString::TheEmptyString );
			e->charPos = e->text->getTextLength();
			setKeyDown( mod, false );
		}
	}
	else
	{
		// this absolute thang will/might need more than one test
		absolute = true;
	}
}

// preforms the correct action when a modifier key is pressed down
void doKeyDown(EntryData *e, UnicodeString mod )
{
	// simple cases if there are no mods present
	//sanity check
	if( e->text->getTextLength() <= 1 )
	{
		// reset text
		e->text->setText( mod );
		e->sText->setText( mod );
		e->charPos = e->text->getTextLength();
		setKeyDown( mod, true );
	}

	else //if( e->text->getTextLength() )
	{
		char c = e->text->getText().getCharAt( e->text->getTextLength() - 1);
		if( c != '+' && absolute)
		{
				e->text->setText( mod );
				e->sText->setText( mod );
				e->charPos = e->text->getTextLength();
				// try reseting all mods first
				setKeyDown( shift, false );
				setKeyDown( alt, false );
				setKeyDown( ctrl, false );

				setKeyDown( mod, true );
				absolute = false;

		}
		//else only allow modifiers are present
		else
		{
			if( mod == shift && shiftDown )
			{
			}
			else if( mod == ctrl && ctrlDown )
			{
			}
			else if( mod == alt && altDown )
			{
			}
			else
			{
				//figure out the cases for which mod goes first

				// puts shift at the end of the mods
				if( altDown && ctrlDown)
				{
					UnicodeString temp = alt;
					temp.concat( ctrl );
					temp.concat( mod );
					e->text->setText(temp);
					e->charPos = e->text->getTextLength();
					setKeyDown( mod, true );
				}
				// if alt and shift are down, puts ctrl in the middle
				else if( altDown && shiftDown )
				{
					UnicodeString temp = alt;
					temp.concat( ctrl );
					temp.concat( shift );
					e->text->setText( temp );
					e->charPos = e->text->getTextLength();
					setKeyDown( mod, true );
				}
				// puts either shift or ctrl after alt
				else if( altDown )
				{
					UnicodeString temp = alt;
					temp.concat( mod );
					e->text->setText(temp);
					e->charPos = e->text->getTextLength();
					setKeyDown( mod, true );
				}
				// puts alt infront of these two
				else if( ctrlDown && shiftDown )
				{
					UnicodeString temp = alt;
					temp.concat( ctrl );
					temp.concat( shift );
					e->text->setText( temp );
					e->charPos = e->text->getTextLength();
					setKeyDown( mod, true );
				}
				// if only ctrl+ is currently being displayed
				else if( ctrlDown )
				{
					// if it's alt, put it in front
					if( mod == alt )
					{
						UnicodeString temp = mod;
						temp.concat( ctrl );
						e->text->setText( temp );
						e->charPos = e->text->getTextLength();
						setKeyDown( mod, true );
					}
					//else put shift after ctrl
					else
					{
						UnicodeString temp = ctrl;
						temp.concat( mod );
						e->text->setText( temp );
						e->charPos = e->text->getTextLength();
						setKeyDown( mod, true );
					}
				}
				// else put alt or ctrl in front of shift
				else if( shiftDown )
				{
					UnicodeString temp = mod;
					temp.concat( shift );
					e->text->setText( temp );
					e->charPos = e->text->getTextLength();
					setKeyDown( mod, true );
				}

			}
					
		}
	}
}


//-------------------------------------------------------------------------------------------------
/** Initialize the options menu */
//-------------------------------------------------------------------------------------------------
void KeyboardOptionsMenuInit( WindowLayout *layout, void *userData )
{

	//set keyboard focus to main parent
	parentKeyboardOptionsMenuID = TheNameKeyGenerator->nameToKey("KeyboardOptionsMenu.wnd:ParentKeyboardOptionsMenu");
	parentKeyboardOptionsMenu = TheWindowManager->winGetWindowFromId( NULL, parentKeyboardOptionsMenuID );

	// get ids for our children controls
	buttonBackID = TheNameKeyGenerator->nameToKey( AsciiString("KeyboardOptionsMenu.wnd:ButtonBack") );
	buttonBack = TheWindowManager->winGetWindowFromId( parentKeyboardOptionsMenu, buttonBackID );

	comboBoxCategoryListID = TheNameKeyGenerator->nameToKey( "KeyboardOptionsMenu.wnd:ComboBoxCategoryList" );
	comboBoxCategoryList   = TheWindowManager->winGetWindowFromId( /*parentKeyboardOptionsMenu*/NULL, comboBoxCategoryListID );

	listBoxCommandListID   = TheNameKeyGenerator->nameToKey( "KeyboardOptionsMenu.wnd:ListBoxCommandList" );
	listBoxCommandList     = TheWindowManager->winGetWindowFromId( NULL, listBoxCommandListID );

	staticTextDescriptionID = TheNameKeyGenerator->nameToKey( "KeyboardOptionsMenu.wnd:StaticTextDescription" );
	staticTextDescription   = TheWindowManager->winGetWindowFromId( NULL, staticTextDescriptionID );

	staticTextCurrentHotkeyID = TheNameKeyGenerator->nameToKey( "KeyboardOptionsMenu.wnd:StaticTextCurrentHotkey" );
	staticTextCurrentHotkey   = TheWindowManager->winGetWindowFromId( NULL, staticTextCurrentHotkeyID );

	buttonResetAllID        = TheNameKeyGenerator->nameToKey( "KeyboardOptionsMenu.wnd:ButtonResetAll" );
	buttonResetAll          = TheWindowManager->winGetWindowFromId( NULL, buttonResetAllID );

	textEntryAssignHotkeyID = TheNameKeyGenerator->nameToKey( "KeyboardOptionsMenu.wnd:TextEntryAssignHotkey" );
	textEntryAssignHotkey   = TheWindowManager->winGetWindowFromId( NULL, textEntryAssignHotkeyID );

	buttonAssignID          = TheNameKeyGenerator->nameToKey( "KeyboardOptionsMenu.wnd:ButtonAssign" );
	buttonAssign            = TheWindowManager->winGetWindowFromId( NULL, buttonAssignID );



	//special text entry box that needs its own function
	textEntryAssignHotkey->winSetInputFunc( KeyboardTextEntryInput );

	// populate category combo box
	populateCategoryBox();

	// populate command list
	fillCommandListBox(CATEGORY_CONTROL);

	//disable textEntry until specific command is chosen
	textEntryAssignHotkey->winEnable( false );

	// nothing is selected yet, so say what to do rather than showing an empty panel
	GadgetStaticTextSetText( staticTextDescription, TheGameText->fetch( "GUI:PressAKey" ) );
	GadgetStaticTextSetText( staticTextCurrentHotkey, TheGameText->fetch( "GUI:NULL" ) );
	theSelectedRec = NULL;
	thePendingKey = MK_NONE;
	thePendingMods = NONE;

	//clear textEntry field
	EntryData *e = (EntryData *)textEntryAssignHotkey->winGetUserData();
	e->text->setText( UnicodeString::TheEmptyString );
	e->charPos = e->text->getTextLength();

	// set up these strings because they will be called a lot
	alt   = TheGameText->fetch( "KEYBOARD:Alt+" );
	ctrl = TheGameText->fetch( "KEYBOARD:Ctrl+" );
	shift = TheGameText->fetch( "KEYBOARD:Shift+" );

	// show menu
	layout->hide( FALSE );

	// set keyboard focus to main parent
	TheWindowManager->winSetFocus( parentKeyboardOptionsMenu );
}

//-------------------------------------------------------------------------------------------------
/** options menu shutdown method */
//-------------------------------------------------------------------------------------------------
void KeyboardOptionsMenuShutdown( WindowLayout *layout, void *userData )
{
		// hide menu
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout );
}

//-------------------------------------------------------------------------------------------------
/** options menu update method */
//-------------------------------------------------------------------------------------------------
void KeyboardOptionsMenuUpdate( WindowLayout *layout, void *userData )
{

}  // end OptionsMenuUpdate

//-------------------------------------------------------------------------------------------------
/** Options menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType KeyboardOptionsMenuInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{

	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;

			switch( key )
			{

				// ----------------------------------------------------------------------------------------
				case KEY_ESC:
				{
					
					//
					// send a simulated selected event to the parent window of the
					// back/exit button
					//
					if( BitTest( state, KEY_STATE_UP ) )
					{
						AsciiString buttonName( "KeyboardOptionsMenu.wnd:ButtonBack" );
						NameKeyType buttonID = TheNameKeyGenerator->nameToKey( buttonName );
						GameWindow *button = TheWindowManager->winGetWindowFromId( window, buttonID );

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																								(WindowMsgData)button, buttonID );

					}  // end if

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}  // end escape

			}  // end switch( key )

			// with a command picked, the next key you press is the one you want it on
			if( catchBindingKey( mData1, mData2 ) )
				return MSG_HANDLED;

		}  // end char

	}  // end switch( msg )

	return MSG_IGNORED;

}  // end KeyboardOptionsMenuInput

//-------------------------------------------------------------------------------------------------
/** options menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType KeyboardOptionsMenuSystem( GameWindow *window, UnsignedInt msg, 
																				WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CREATE:
		{
			
			break;

		}  // end create

		//---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{

			break;

		}  // end case

		// --------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			return MSG_HANDLED;

		}  // end input

		//---------------------------------------------------------------------------------------------
		case GCM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

      if( comboBoxCategoryList != NULL && controlID == comboBoxCategoryListID )
      {
				// the category dropdown is gone; if a layout still carries one, it just refills the list
				fillCommandListBox( currentCategory() );

				//reset current hotkey description
				GadgetStaticTextSetText( staticTextDescription, TheGameText->fetch( "GUI:NULL" ) );

				//reset current hotkey text
				GadgetStaticTextSetText( staticTextCurrentHotkey, TheGameText->fetch( "GUI:NULL" ) );

				//clear textEntry field
				EntryData *e = (EntryData *)textEntryAssignHotkey->winGetUserData();
				e->text->setText( UnicodeString::TheEmptyString );
				e->charPos = e->text->getTextLength();

				//disable textEntry until specific command is chosen
				textEntryAssignHotkey->winEnable( false );

      }
			break;

		}  // end selected

		// ---------------------------------------------------------------------------------------------
		case GLM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

			if( controlID == listBoxCommandListID )
			{
				Int selected = -1;
				GadgetListBoxGetSelected( listBoxCommandList,  &selected );

				// the record is the row's own data, so this is exact rather than matched by name -
				// two commands sharing a display name used to both answer to the first one's row
				theSelectedRec = ( selected >= 0 )
													 ? (MetaMapRec *)GadgetListBoxGetItemData( listBoxCommandList, selected )
													 : NULL;

				thePendingKey = MK_NONE;
				thePendingMods = NONE;

				EntryData *entry = (EntryData *)textEntryAssignHotkey->winGetUserData();
				if( entry )
				{
					entry->text->setText( UnicodeString::TheEmptyString );
					entry->charPos = 0;
				}

				if( theSelectedRec )
				{
					GadgetStaticTextSetText( staticTextDescription, theSelectedRec->m_description );
					GadgetStaticTextSetText( staticTextCurrentHotkey, bindingText( theSelectedRec ) );
					textEntryAssignHotkey->winEnable( true );
					TheWindowManager->winSetFocus( textEntryAssignHotkey );
				}
				else
				{
					textEntryAssignHotkey->winEnable( false );
				}

			} // end selected

			break;

		} // end case

		// ---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

			if( controlID == buttonBackID )
			{

				// go back one screen
				TheShell->pop();

			}  // end if
			else if( controlID == buttonAssignID )
			{
				//
				// Put the command on the key the box caught.  Whatever else was on that key gives it
				// up - a key can only mean one thing - and the list is rebuilt so both the command
				// that moved and the one that was displaced show what they answer to now.
				//
				if( theSelectedRec && thePendingKey != MK_NONE )
				{
					MetaMapRec *displaced = TheMetaMap->rebind( theSelectedRec, thePendingKey, thePendingMods );

					fillCommandListBox( currentCategory() );

					GadgetStaticTextSetText( staticTextCurrentHotkey, bindingText( theSelectedRec ) );

					// say which command just went quiet, rather than leaving it to be found in a game
					if( displaced )
					{
						UnicodeString note = displaced->m_displayName;
						note.concat( UnicodeString( L"  -  " ) );
						note.concat( TheGameText->fetch( "GUI:NULL" ) );
						GadgetStaticTextSetText( staticTextDescription, note );
					}

					thePendingKey = MK_NONE;
					thePendingMods = NONE;
					EntryData *entry = (EntryData *)textEntryAssignHotkey->winGetUserData();
					if( entry )
					{
						entry->text->setText( UnicodeString::TheEmptyString );
						entry->charPos = 0;
					}
				}
			}
			else if( controlID == buttonResetAllID )
			{
				// every key back to what the game shipped with, and the file written out empty
				TheMetaMap->resetBindingsToDefault();

				fillCommandListBox( currentCategory() );

				theSelectedRec = NULL;
				thePendingKey = MK_NONE;
				thePendingMods = NONE;

				//reset current hotkey text
				GadgetStaticTextSetText( staticTextCurrentHotkey, TheGameText->fetch( "GUI:NULL" ) );

				//clear textEntry field
				EntryData *e = (EntryData *)textEntryAssignHotkey->winGetUserData();
				e->text->setText( UnicodeString::TheEmptyString );
				e->charPos = e->text->getTextLength();

				//disable text entry
				textEntryAssignHotkey->winEnable( false );

			}

			break;

		}	// end selected

		default:
			return MSG_IGNORED;

	}	// end switch

	return MSG_HANDLED;

}  // end KeyboardOptionsMenuSystem

// KeyboardTextEntryInput =======================================================
/** Handle input for text entry field */
//=============================================================================
WindowMsgHandledType KeyboardTextEntryInput( GameWindow *window, UnsignedInt msg,
													 WindowMsgData mData1, WindowMsgData mData2 )
{
	EntryData *e = (EntryData *)window->winGetUserData();

	WinInstanceData *instData = window->winGetInstanceData();

	if ( TheIMEManager && TheIMEManager->isAttachedTo( window) && TheIMEManager->isComposing())
	{
		// ignore input while IME has focus
		return MSG_HANDLED;
	}

	//
	// This box does not take text.  It takes one keystroke, whole: the key and whatever of ctrl,
	// alt and shift were held with it, on the way down, and it shows what it caught.  The several
	// hundred lines below used to assemble a display string out of the characters typed into it,
	// character by character, and never arrived at a key and a modifier that anything could be
	// bound to - which is why the Assign button had nothing to do and said so in a comment.
	//
	// The mouse cases further down still matter (the box highlights and takes focus like any
	// other), so this sits in front of the keyboard ones rather than replacing the function.
	//
	if( msg == GWM_CHAR )
		return catchBindingKey( mData1, mData2 ) ? MSG_HANDLED : MSG_IGNORED;

	if( msg == GWM_IME_CHAR )
		return MSG_HANDLED;			// a keystroke is not a character here

	switch( msg )
	{
		// ------------------------------------------------------------------------
		case GWM_IME_CHAR:
		{
			WideChar ch = (WideChar) mData1;

			// --------------------------------------------------------------------
			if ( ch == VK_RETURN )
			{
				// Done with this edit
			 		TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
			 																				GEM_EDIT_DONE,
			 																				(WindowMsgData)window, 
			 																				0 );
				return MSG_HANDLED;
			};

			if( ch )
			{
				// Constrain keys based on rules for entry box.
				if( e->numericalOnly )
				{
					if( TheWindowManager->winIsDigit( ch ) == 0 )
						return MSG_HANDLED;
				}
				else if( e->alphaNumericalOnly )
				{
					if( TheWindowManager->winIsAlNum( ch ) == 0 )
						return MSG_HANDLED;
				}
				else if ( e->aSCIIOnly )
				{
					if ( TheWindowManager->winIsAscii( ch ) == 0 )
					{
						return MSG_HANDLED;
					}
				}

				if( e->text->getTextLength() <= 1 )
				{
					e->text->setText( UnicodeString::TheEmptyString );
					e->text->appendChar( ch );
					e->charPos = e->text->getTextLength();
					TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																					GEM_UPDATE_TEXT,
																					(WindowMsgData)window, 
																					0 );
					return MSG_HANDLED;
				}
				//else check is modifiers are persent
				else
				{
					char c = e->text->getText().getCharAt(e->text->getTextLength() - 1 );
					if(c == '+' )
					{
						e->text->appendChar( ch );
						e->charPos++;
						TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																						GEM_UPDATE_TEXT,
																						(WindowMsgData)window, 
																						0 );
						return MSG_HANDLED;
					}
					// if not, reset textEntry
					else
					{
						//if any of the modifiers are down, just replace letter
						if( ( shiftDown | ctrlDown | altDown ) && ( !absolute ) )
						{
							char test = e->text->getText().getCharAt(e->text->getTextLength() - 1);
							// only replace letter if not the same as last char of string (removes flickering)
							if( test != ch )
							{
								e->text->removeLastChar();
								e->text->appendChar( ch );
								TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																								GEM_UPDATE_TEXT,
																								(WindowMsgData)window, 
																								0 );
							}
						}
						//else reset textEntry
						else
						{
							e->text->setText( UnicodeString::TheEmptyString );
							e->text->appendChar( ch );
							e->charPos = 1;
							TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																							GEM_UPDATE_TEXT,
																							(WindowMsgData)window, 
																							0 );
						}
						return MSG_HANDLED;
					}
				}
				

			}
			break;
		}
		// ------------------------------------------------------------------------
		case GWM_CHAR:

			switch( mData1 )
			{
				/*
				// --------------------------------------------------------------------
				case KEY_KPENTER:
				case KEY_ENTER:
					// Done with this edit
					if( BitTest( mData2, KEY_STATE_DOWN ) )
					{
						if( e->receivedUnichar == FALSE )
						{
							TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																									GEM_EDIT_DONE,
																									(WindowMsgData)window, 
																									0 );
						}
					}

					break;
				 */

				// -------------------------------------------------------------------------------------------
				// modifier cases

				case KEY_LCTRL:
				{
					if( BitTest( mData2, KEY_STATE_DOWN ) )
					{
						UnicodeString mod = ctrl;
						doKeyDown( e, mod );
						TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																GEM_UPDATE_TEXT,
																(WindowMsgData)window, 
																0 );

						return MSG_HANDLED;
					}
					if( BitTest(mData2, KEY_STATE_UP ) )
					{
							UnicodeString mod = ctrl;
							doKeyUp( e, mod );
							TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																						GEM_UPDATE_TEXT,
																						(WindowMsgData)window, 
																						0 );

							return MSG_HANDLED;
					}
					break;
				}

				case KEY_RSHIFT:
				case KEY_LSHIFT:
				{
					if( BitTest( mData2, KEY_STATE_DOWN ) )
					{
						UnicodeString mod = shift;
						doKeyDown( e, mod );
						TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																GEM_UPDATE_TEXT,
																(WindowMsgData)window, 
																0 );

						return MSG_HANDLED;

					}
					if( BitTest( mData2, KEY_STATE_UP ) )
					{
						UnicodeString mod = shift;
						doKeyUp(e, mod );

						TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																						GEM_UPDATE_TEXT,
																						(WindowMsgData)window, 
																						0 );


						return MSG_HANDLED;
					}
					break;
				}

				case KEY_LALT:
				{
					if( BitTest( mData2, KEY_STATE_DOWN ) )
					{
						UnicodeString mod = alt;
						doKeyDown( e, mod );

						TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																GEM_UPDATE_TEXT,
																(WindowMsgData)window, 
																0 );

						return MSG_HANDLED;

					}
					if( BitTest(mData2, KEY_STATE_UP ) )
					{
						UnicodeString mod = alt;
						doKeyUp( e, mod );
						TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																GEM_UPDATE_TEXT,
																(WindowMsgData)window, 
																0 );

						return MSG_HANDLED;
					}
					break;
				}

				// -------------------------------------------------------------------------------------------


				// --------------------------------------------------------------------
				// Don't process these keys
				case KEY_ESC:
				case KEY_PGUP:
				case KEY_PGDN:
				case KEY_HOME:
				case KEY_END:
				case KEY_F1:
				case KEY_F2:
				case KEY_F3:
				case KEY_F4:
				case KEY_F5:
				case KEY_F6:
				case KEY_F7:
				case KEY_F8:
				case KEY_F9:
				case KEY_F10:
				case KEY_F11:
				case KEY_F12:
				case KEY_CAPS:
					return MSG_IGNORED;

				// --------------------------------------------------------------------
				case KEY_DOWN:
				case KEY_RIGHT:
				case KEY_TAB:

					if( BitTest( mData2, KEY_STATE_DOWN ) )
						window->winNextTab();
					break;

				// --------------------------------------------------------------------
				case KEY_UP:
				case KEY_LEFT:

					if( BitTest( mData2, KEY_STATE_DOWN ) )
						window->winPrevTab();
					break;

				// --------------------------------------------------------------------
				case KEY_BACKSPACE:
				{
					e->text->setText( UnicodeString::TheEmptyString );
					e->charPos = e->text->getTextLength();
					TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																					GEM_UPDATE_TEXT,
																					(WindowMsgData)window, 
																					0 );
					setKeyDown(shift, false );
					setKeyDown(ctrl, false );
					setKeyDown(alt, false );
					return MSG_HANDLED;
					
					break;
				}
				case KEY_DEL:
				{

					if( BitTest( mData2, KEY_STATE_DOWN ) )
					{
						// if conCharPos != 0 this will fall through to next case.
						// it should be noted that conCharPos can only != 0 in Jap & Kor
						if( e->conCharPos == 0 )
						{
							if( e->charPos > 0 )
							{

								e->text->removeLastChar();
								e->sText->removeLastChar();
								e->charPos--;
								TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																								GEM_UPDATE_TEXT,
																								(WindowMsgData)window, 
																								0 );
							}  // end if
						}
					}
					break;
				}

				// ----------------------------------------------------------------------------------------
				// doing research to see if this will fix the keyboard stuff
				/*default:
				{
					char ch = mData1;
					if( ch && ( BitTest( mData2, KEY_STATE_DOWN ) ) )
					{
						// Constrain keys based on rules for entry box.
						if( e->numericalOnly )
						{
							if( TheWindowManager->winIsDigit( ch ) == 0 )
								return MSG_HANDLED;
						}
						else if( e->alphaNumericalOnly )
						{
							if( TheWindowManager->winIsAlNum( ch ) == 0 )
								return MSG_HANDLED;
						}
						else if ( e->aSCIIOnly )
						{
							if ( TheWindowManager->winIsAscii( ch ) == 0 )
							{
								return MSG_HANDLED;
							}
						}

						if( e->text->getTextLength() <= 1 )
						{
							e->text->setText( UnicodeString::TheEmptyString );
							e->text->appendChar( ch );
							e->charPos = e->text->getTextLength();
							TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																							GEM_UPDATE_TEXT,
																							(WindowMsgData)window, 
																							0 );
							return MSG_HANDLED;
						}
						//else check is modifiers are persent
						else
						{
							char c = e->text->getText().getCharAt(e->text->getTextLength() - 1 );
							if(c == '+' )
							{
								e->text->appendChar( ch );
								e->charPos++;
								TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																								GEM_UPDATE_TEXT,
																								(WindowMsgData)window, 
																								0 );
								return MSG_HANDLED;
							}
							// if not, reset textEntry
							else
							{
								//if any of the modifiers are down, just replace letter
								if( ( shiftDown | ctrlDown | altDown ) && ( !absolute ) )
								{
									char test = e->text->getText().getCharAt(e->text->getTextLength() - 1);
									// only replace letter if not the same as last char of string (removes flickering)
									if( test != ch )
									{
										e->text->removeLastChar();
										e->text->appendChar( ch );
										TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																										GEM_UPDATE_TEXT,
																										(WindowMsgData)window, 
																										0 );
									}
								}
								//else reset textEntry
								else
								{
									e->text->setText( UnicodeString::TheEmptyString );
									e->text->appendChar( ch );
									e->charPos = 1;
									TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																									GEM_UPDATE_TEXT,
																									(WindowMsgData)window, 
																									0 );
								}
								return MSG_HANDLED;
							}
						}
						

					}
				}*/


			}  // end switch( mData1 )

			break;

		// ------------------------------------------------------------------------
		case GWM_LEFT_DOWN:
			BitSet( instData->m_state, WIN_STATE_HILITED );
			TheWindowManager->winSetFocus( window );
			break;

		// ------------------------------------------------------------------------
		case GWM_MOUSE_ENTERING:

			if (BitTest( instData->getStyle(), GWS_MOUSE_TRACK ) )
			{

				BitSet( instData->m_state, WIN_STATE_HILITED );
				TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																						GBM_MOUSE_ENTERING,
																						(WindowMsgData)window, 0 );
				TheWindowManager->winSetFocus( window );
			}

			break;

		// ------------------------------------------------------------------------
		case GWM_MOUSE_LEAVING:

			if( BitTest( instData->getStyle(), GWS_MOUSE_TRACK ) )
			{

				BitClear( instData->m_state, WIN_STATE_HILITED );
				TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																						GBM_MOUSE_LEAVING,
																						(WindowMsgData)window, 0 );
			}
			break;

		// ------------------------------------------------------------------------
		case GWM_LEFT_DRAG:

			if( BitTest( instData->getStyle(), GWS_MOUSE_TRACK ) )
				TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																						GGM_LEFT_DRAG,
																						(WindowMsgData)window, 0 );
			break;

		// ------------------------------------------------------------------------
		default:
			return MSG_IGNORED;

	}  // end switch( msg )

	return MSG_HANDLED;

}  // end GadgetTextEntryInput


