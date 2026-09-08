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

// KeyDownInfo.h //////////////////////////////////////////////////////////////////////////////////
// Which modifier combinations a key is currently held down with.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _KEY_DOWN_INFO_H_
#define _KEY_DOWN_INFO_H_

#include "Lib/BaseType.h"
#include "GameClient/KeyDefs.h"

/**
	* A command bound to Ctrl+X with an UP transition only fired when X came up while Ctrl was still
	* held.  Let go of Ctrl first and the key's own release arrived with no Ctrl in the modifier
	* state, the record no longer matched, and whatever the DOWN had switched on stayed on.
	*
	* This remembers, for one key, every modifier combination it was pressed with, so the release
	* can be recognised whichever of the two the player lets go of first.  Ctrl, Alt and Shift make
	* seven combinations with at least one of them set, and one bit each is all this needs.
	*
	* The modifier values are the KEY_STATE_L* bits, which are not consecutive, so slots are
	* numbered by a packed Ctrl/Alt/Shift triple rather than by the mask itself.
	*/
class KeyDownInfo
{
public:

	enum { MOD_STATE_COUNT = 7 };

	KeyDownInfo() : m_modStateBits( 0 ) {}

	/// Slot number for a modifier mask, or -1 when the mask holds none of the three.
	static Int indexOfModState( Int modState )
	{
		const Int packed = ( ( modState & KEY_STATE_LCONTROL ) ? 1 : 0 )
										 | ( ( modState & KEY_STATE_LSHIFT   ) ? 2 : 0 )
										 | ( ( modState & KEY_STATE_LALT     ) ? 4 : 0 );
		return packed ? packed - 1 : -1;
	}

	/// The modifier mask a slot stands for, or zero when the slot number is out of range.
	static Int modStateAtIndex( Int index )
	{
		if( index < 0 || index >= MOD_STATE_COUNT )
			return 0;

		const Int packed = index + 1;
		return ( ( packed & 1 ) ? KEY_STATE_LCONTROL : 0 )
				 | ( ( packed & 2 ) ? KEY_STATE_LSHIFT   : 0 )
				 | ( ( packed & 4 ) ? KEY_STATE_LALT     : 0 );
	}

	Bool isKeyDown() const { return m_modStateBits != 0; }

	Bool hasModStateAtIndex( Int index ) const
	{
		return index >= 0 && index < MOD_STATE_COUNT && ( m_modStateBits & ( 1 << index ) ) != 0;
	}

	Bool hasModState( Int modState ) const
	{
		return hasModStateAtIndex( indexOfModState( modState ) );
	}

	void setModState( Int modState )
	{
		const Int index = indexOfModState( modState );
		if( index >= 0 )
			m_modStateBits |= (UnsignedByte)( 1 << index );
	}

	void clearModStateAtIndex( Int index )
	{
		if( index >= 0 && index < MOD_STATE_COUNT )
			m_modStateBits &= (UnsignedByte)~( 1 << index );
	}

	void clearModState( Int modState )
	{
		clearModStateAtIndex( indexOfModState( modState ) );
	}

	void clear() { m_modStateBits = 0; }

private:

	UnsignedByte m_modStateBits;	///< one bit per Ctrl/Alt/Shift combination the key is held with
};

#endif // _KEY_DOWN_INFO_H_
