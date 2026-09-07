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

// FILE: ControlServer.cpp ///////////////////////////////////////////////////////////////////////
//
// -control [port]: a WebSocket on 127.0.0.1 that drives the game from outside it.  See
// ControlServer.h for the command grammar and for why world commands are queued rather than run
// where they arrive.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/ControlServer.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameClient/Display.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/ScenarioDrill.h"
#include "GameNetwork/GameInfo.h"		// MAX_SLOTS, for the skirmish command's player count

#include <vector>

// windows.h, which PreRTS.h already pulled in, brings Winsock 1.1 with it.  That is every call
// this file makes, so it takes the one already on the table rather than starting the winsock2
// header fight described in the hard constraints.

// ------------------------------------------------------------------------------------------------
// sizes and constants
// ------------------------------------------------------------------------------------------------

static const Int CONTROL_LOOPBACK_ONLY = INADDR_LOOPBACK;
static const Int CONTROL_BACKLOG = 1;
static const Int CONTROL_READ_CHUNK = 4096;
static const Int CONTROL_MAX_REQUEST = 16384;
static const Int CONTROL_MAX_COMMANDS_PER_FRAME = 64;

/// RFC 6455's magic string, appended to the client key before hashing
static const char *CONTROL_WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static const Int SHA1_DIGEST_BYTES = 20;
static const Int SHA1_BLOCK_BYTES = 64;

// WebSocket opcodes, from RFC 6455 section 5.2
static const unsigned char WS_OPCODE_TEXT = 0x1;
static const unsigned char WS_OPCODE_CLOSE = 0x8;
static const unsigned char WS_OPCODE_PING = 0x9;
static const unsigned char WS_OPCODE_PONG = 0xA;

// ------------------------------------------------------------------------------------------------
// SHA-1, because the handshake needs one and nothing in GameEngine has one
// ------------------------------------------------------------------------------------------------

struct Sha1State
{
	UnsignedInt digest[ 5 ];
	UnsignedInt byteCount;
	unsigned char block[ SHA1_BLOCK_BYTES ];
	Int blockLength;
};

static UnsignedInt sha1RotateLeft( UnsignedInt value, Int bits )
{
	return (value << bits) | (value >> (32 - bits));
}

static void sha1Init( Sha1State *state )
{
	state->digest[ 0 ] = 0x67452301;
	state->digest[ 1 ] = 0xEFCDAB89;
	state->digest[ 2 ] = 0x98BADCFE;
	state->digest[ 3 ] = 0x10325476;
	state->digest[ 4 ] = 0xC3D2E1F0;
	state->byteCount = 0;
	state->blockLength = 0;
}

static void sha1Transform( Sha1State *state )
{
	UnsignedInt w[ 80 ];
	Int i;

	for( i = 0; i < 16; ++i )
	{
		w[ i ] = ((UnsignedInt)state->block[ i * 4 ] << 24)
					 | ((UnsignedInt)state->block[ i * 4 + 1 ] << 16)
					 | ((UnsignedInt)state->block[ i * 4 + 2 ] << 8)
					 | ((UnsignedInt)state->block[ i * 4 + 3 ]);
	}
	for( i = 16; i < 80; ++i )
		w[ i ] = sha1RotateLeft( w[ i - 3 ] ^ w[ i - 8 ] ^ w[ i - 14 ] ^ w[ i - 16 ], 1 );

	UnsignedInt a = state->digest[ 0 ];
	UnsignedInt b = state->digest[ 1 ];
	UnsignedInt c = state->digest[ 2 ];
	UnsignedInt d = state->digest[ 3 ];
	UnsignedInt e = state->digest[ 4 ];

	for( i = 0; i < 80; ++i )
	{
		UnsignedInt f, k;
		if (i < 20)			{ f = (b & c) | ((~b) & d);				k = 0x5A827999; }
		else if (i < 40)	{ f = b ^ c ^ d;									k = 0x6ED9EBA1; }
		else if (i < 60)	{ f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
		else							{ f = b ^ c ^ d;									k = 0xCA62C1D6; }

		const UnsignedInt temp = sha1RotateLeft( a, 5 ) + f + e + k + w[ i ];
		e = d;
		d = c;
		c = sha1RotateLeft( b, 30 );
		b = a;
		a = temp;
	}

	state->digest[ 0 ] += a;
	state->digest[ 1 ] += b;
	state->digest[ 2 ] += c;
	state->digest[ 3 ] += d;
	state->digest[ 4 ] += e;
}

static void sha1Update( Sha1State *state, const unsigned char *data, Int length )
{
	for( Int i = 0; i < length; ++i )
	{
		state->block[ state->blockLength++ ] = data[ i ];
		++state->byteCount;
		if (state->blockLength == SHA1_BLOCK_BYTES)
		{
			sha1Transform( state );
			state->blockLength = 0;
		}
	}
}

static void sha1Finish( Sha1State *state, unsigned char *digestOut )
{
	const UnsignedInt bitCount = state->byteCount * 8;

	const unsigned char one = 0x80;
	sha1Update( state, &one, 1 );

	const unsigned char zero = 0x00;
	while (state->blockLength != 56)
		sha1Update( state, &zero, 1 );

	unsigned char lengthBytes[ 8 ];
	for( Int i = 0; i < 4; ++i )
		lengthBytes[ i ] = 0;
	lengthBytes[ 4 ] = (unsigned char)((bitCount >> 24) & 0xFF);
	lengthBytes[ 5 ] = (unsigned char)((bitCount >> 16) & 0xFF);
	lengthBytes[ 6 ] = (unsigned char)((bitCount >> 8) & 0xFF);
	lengthBytes[ 7 ] = (unsigned char)(bitCount & 0xFF);
	sha1Update( state, lengthBytes, 8 );

	for( Int i = 0; i < SHA1_DIGEST_BYTES; ++i )
		digestOut[ i ] = (unsigned char)((state->digest[ i / 4 ] >> (24 - 8 * (i % 4))) & 0xFF);
}

// ------------------------------------------------------------------------------------------------
// base64
// ------------------------------------------------------------------------------------------------

static const char *BASE64_ALPHABET =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static Int base64Encode( const unsigned char *data, Int length, char *out, Int outSize )
{
	const Int needed = ((length + 2) / 3) * 4 + 1;
	if (outSize < needed)
		return 0;

	Int written = 0;
	Int at = 0;
	while (at < length)
	{
		const UnsignedInt byte0 = data[ at ];
		const UnsignedInt byte1 = (at + 1 < length) ? data[ at + 1 ] : 0;
		const UnsignedInt byte2 = (at + 2 < length) ? data[ at + 2 ] : 0;
		const UnsignedInt triple = (byte0 << 16) | (byte1 << 8) | byte2;

		out[ written++ ] = BASE64_ALPHABET[ (triple >> 18) & 0x3F ];
		out[ written++ ] = BASE64_ALPHABET[ (triple >> 12) & 0x3F ];
		out[ written++ ] = (at + 1 < length) ? BASE64_ALPHABET[ (triple >> 6) & 0x3F ] : '=';
		out[ written++ ] = (at + 2 < length) ? BASE64_ALPHABET[ triple & 0x3F ] : '=';
		at += 3;
	}

	out[ written ] = 0;
	return written;
}

Bool ControlServer_computeAcceptKey( const char *clientKey, char *out, Int outSize )
{
	if (clientKey == NULL || out == NULL)
		return FALSE;

	Sha1State state;
	sha1Init( &state );
	sha1Update( &state, (const unsigned char *)clientKey, (Int)strlen( clientKey ) );
	sha1Update( &state, (const unsigned char *)CONTROL_WEBSOCKET_GUID,
							(Int)strlen( CONTROL_WEBSOCKET_GUID ) );

	unsigned char digest[ SHA1_DIGEST_BYTES ];
	sha1Finish( &state, digest );

	return base64Encode( digest, SHA1_DIGEST_BYTES, out, outSize ) > 0;
}

// ------------------------------------------------------------------------------------------------
// socket state
// ------------------------------------------------------------------------------------------------

static SOCKET theListenSocket = INVALID_SOCKET;
static SOCKET theClientSocket = INVALID_SOCKET;
static Bool theWinsockStarted = FALSE;
static Bool theHandshakeDone = FALSE;
static std::vector<char> theIncoming;
static std::vector<AsciiString> thePendingCommands;
static Bool theQuitRequested = FALSE;

static void closeClient( void )
{
	if (theClientSocket != INVALID_SOCKET)
	{
		closesocket( theClientSocket );
		theClientSocket = INVALID_SOCKET;
	}
	theHandshakeDone = FALSE;
	theIncoming.clear();
}

static Bool setNonBlocking( SOCKET s )
{
	unsigned long nonBlocking = 1;
	return ioctlsocket( s, FIONBIO, &nonBlocking ) == 0;
}

static Bool openListenSocket( Int port )
{
	WSADATA wsaData;
	if (!theWinsockStarted)
	{
		if (WSAStartup( MAKEWORD( 1, 1 ), &wsaData ) != 0)
		{
			DEBUG_LOG(("CONTROL: winsock would not start\n"));
			return FALSE;
		}
		theWinsockStarted = TRUE;
	}

	theListenSocket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	if (theListenSocket == INVALID_SOCKET)
	{
		DEBUG_LOG(("CONTROL: no socket\n"));
		return FALSE;
	}

	sockaddr_in address;
	memset( &address, 0, sizeof( address ) );
	address.sin_family = AF_INET;
	address.sin_port = htons( (unsigned short)port );
	address.sin_addr.s_addr = htonl( CONTROL_LOOPBACK_ONLY );

	if (bind( theListenSocket, (sockaddr *)&address, sizeof( address ) ) != 0
			|| listen( theListenSocket, CONTROL_BACKLOG ) != 0
			|| !setNonBlocking( theListenSocket ))
	{
		DEBUG_LOG(("CONTROL: cannot listen on 127.0.0.1:%d\n", port));
		closesocket( theListenSocket );
		theListenSocket = INVALID_SOCKET;
		return FALSE;
	}

	DEBUG_LOG(("CONTROL: listening on ws://127.0.0.1:%d\n", port));
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// websocket framing
// ------------------------------------------------------------------------------------------------

static void sendFrame( unsigned char opcode, const char *payload, Int length )
{
	if (theClientSocket == INVALID_SOCKET)
		return;

	std::vector<char> frame;
	frame.push_back( (char)(0x80 | opcode) );

	// a server frame is never masked, so the length is the whole second byte
	if (length < 126)
	{
		frame.push_back( (char)length );
	}
	else if (length < 65536)
	{
		frame.push_back( (char)126 );
		frame.push_back( (char)((length >> 8) & 0xFF) );
		frame.push_back( (char)(length & 0xFF) );
	}
	else
	{
		frame.push_back( (char)127 );
		for( Int i = 0; i < 4; ++i )
			frame.push_back( (char)0 );
		frame.push_back( (char)((length >> 24) & 0xFF) );
		frame.push_back( (char)((length >> 16) & 0xFF) );
		frame.push_back( (char)((length >> 8) & 0xFF) );
		frame.push_back( (char)(length & 0xFF) );
	}

	for( Int i = 0; i < length; ++i )
		frame.push_back( payload[ i ] );

	send( theClientSocket, &frame[ 0 ], (Int)frame.size(), 0 );
}

static void sendText( const char *text )
{
	sendFrame( WS_OPCODE_TEXT, text, (Int)strlen( text ) );
}

/** Pull one complete frame off the front of theIncoming.  Returns FALSE when there is not a whole
	  one there yet, which is the normal case on a non-blocking read. */
static Bool takeFrame( unsigned char *opcodeOut, AsciiString *payloadOut )
{
	const Int have = (Int)theIncoming.size();
	if (have < 2)
		return FALSE;

	const unsigned char byte0 = (unsigned char)theIncoming[ 0 ];
	const unsigned char byte1 = (unsigned char)theIncoming[ 1 ];
	const Bool masked = (byte1 & 0x80) != 0;
	Int payloadLength = byte1 & 0x7F;
	Int at = 2;

	if (payloadLength == 126)
	{
		if (have < at + 2)
			return FALSE;
		payloadLength = ((unsigned char)theIncoming[ at ] << 8) | (unsigned char)theIncoming[ at + 1 ];
		at += 2;
	}
	else if (payloadLength == 127)
	{
		// the four high bytes of a 64-bit length are refused rather than truncated: nothing this
		// socket accepts is four gigabytes long, so a frame claiming to be is a broken client
		if (have < at + 8)
			return FALSE;
		for( Int i = 0; i < 4; ++i )
		{
			if (theIncoming[ at + i ] != 0)
			{
				closeClient();
				return FALSE;
			}
		}
		payloadLength = ((unsigned char)theIncoming[ at + 4 ] << 24)
									| ((unsigned char)theIncoming[ at + 5 ] << 16)
									| ((unsigned char)theIncoming[ at + 6 ] << 8)
									| ((unsigned char)theIncoming[ at + 7 ]);
		at += 8;
	}

	unsigned char mask[ 4 ] = { 0, 0, 0, 0 };
	if (masked)
	{
		if (have < at + 4)
			return FALSE;
		for( Int i = 0; i < 4; ++i )
			mask[ i ] = (unsigned char)theIncoming[ at + i ];
		at += 4;
	}

	if (payloadLength < 0 || have < at + payloadLength)
		return FALSE;

	AsciiString payload;
	if (payloadLength > 0)
	{
		std::vector<char> text;
		text.resize( payloadLength + 1 );
		for( Int i = 0; i < payloadLength; ++i )
			text[ i ] = (char)((unsigned char)theIncoming[ at + i ] ^ (masked ? mask[ i % 4 ] : 0));
		text[ payloadLength ] = 0;
		payload.set( &text[ 0 ] );
	}

	theIncoming.erase( theIncoming.begin(), theIncoming.begin() + at + payloadLength );

	*opcodeOut = byte0 & 0x0F;
	*payloadOut = payload;
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// the http upgrade
// ------------------------------------------------------------------------------------------------

/** Read the client key out of the request headers.  Case-insensitive on the header name, because
	  RFC 7230 says header names are, and browsers and Python libraries disagree about the casing. */
static Bool findClientKey( const char *request, char *keyOut, Int keyOutSize )
{
	static const char *HEADER = "sec-websocket-key:";
	const Int headerLength = (Int)strlen( HEADER );

	for( const char *at = request; *at; ++at )
	{
		Int i = 0;
		while (i < headerLength && at[ i ] && tolower( (unsigned char)at[ i ] ) == HEADER[ i ])
			++i;
		if (i < headerLength)
			continue;

		const char *value = at + headerLength;
		while (*value == ' ' || *value == '\t')
			++value;

		Int written = 0;
		while (*value && *value != '\r' && *value != '\n' && written < keyOutSize - 1)
			keyOut[ written++ ] = *value++;
		keyOut[ written ] = 0;
		return written > 0;
	}

	return FALSE;
}

static Bool tryHandshake( void )
{
	// the request ends at the blank line; until then there is nothing to answer
	theIncoming.push_back( 0 );
	const char *request = &theIncoming[ 0 ];
	const char *end = strstr( request, "\r\n\r\n" );
	theIncoming.pop_back();

	if (end == NULL)
		return FALSE;

	char clientKey[ 256 ];
	char acceptKey[ 64 ];
	if (!findClientKey( request, clientKey, sizeof( clientKey ) )
			|| !ControlServer_computeAcceptKey( clientKey, acceptKey, sizeof( acceptKey ) ))
	{
		DEBUG_LOG(("CONTROL: not a websocket request, closing\n"));
		closeClient();
		return FALSE;
	}

	char reply[ 512 ];
	sprintf( reply,
					 "HTTP/1.1 101 Switching Protocols\r\n"
					 "Upgrade: websocket\r\n"
					 "Connection: Upgrade\r\n"
					 "Sec-WebSocket-Accept: %s\r\n\r\n",
					 acceptKey );
	send( theClientSocket, reply, (Int)strlen( reply ), 0 );

	const Int consumed = (Int)(end - request) + 4;
	theIncoming.erase( theIncoming.begin(), theIncoming.begin() + consumed );
	theHandshakeDone = TRUE;
	DEBUG_LOG(("CONTROL: client connected\n"));
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// commands
// ------------------------------------------------------------------------------------------------

static void replyOk( const char *extra )
{
	char reply[ 1024 ];
	if (extra && *extra)
		sprintf( reply, "{\"ok\":true,%s}", extra );
	else
		sprintf( reply, "{\"ok\":true}" );
	sendText( reply );
}

static void replyError( const char *why )
{
	char reply[ 512 ];
	sprintf( reply, "{\"ok\":false,\"error\":\"%s\"}", why );
	sendText( reply );
}

/** frame number, whether a match is running, and what each player owns. */
static void replyStatus( void )
{
	const Bool inGame = TheGameLogic && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame();

	std::vector<char> reply;
	char piece[ 256 ];

	sprintf( piece, "{\"ok\":true,\"frame\":%d,\"inGame\":%s,\"players\":[",
					 TheGameLogic ? (Int)TheGameLogic->getFrame() : 0,
					 inGame ? "true" : "false" );
	for( const char *at = piece; *at; ++at )
		reply.push_back( *at );

	if (inGame && ThePlayerList)
	{
		Bool first = TRUE;
		for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
		{
			Player *player = ThePlayerList->getNthPlayer( i );
			if (player == NULL)
				continue;

			Int units = 0;
			for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
			{
				if (obj->getControllingPlayer() == player && !obj->isEffectivelyDead())
					++units;
			}

			sprintf( piece, "%s{\"index\":%d,\"slot\":%d,\"money\":%u,\"units\":%d}",
							 first ? "" : ",", i, ThePlayerList->getSlotIndex( i ),
							 player->getMoney()->countMoney(), units );
			for( const char *at = piece; *at; ++at )
				reply.push_back( *at );
			first = FALSE;
		}
	}

	reply.push_back( ']' );
	reply.push_back( '}' );
	reply.push_back( 0 );
	sendText( &reply[ 0 ] );
}

/** Everything that changes the world is queued; see the header for why. */
static void handleCommand( const AsciiString &command )
{
	if (command.isEmpty())
		return;

	if (command == "ping")
	{
		replyOk( "\"pong\":true" );
		return;
	}

	if (command == "status")
	{
		replyStatus();
		return;
	}

	if (command == "screenshot")
	{
		if (TheDisplay == NULL)
		{
			replyError( "no display" );
			return;
		}
		TheDisplay->takeScreenShot();
		replyOk( "\"screenshot\":\"requested\"" );
		return;
	}

	if (command == "quit")
	{
		theQuitRequested = TRUE;
		replyOk( "\"quitting\":true" );
		return;
	}

	/* skirmish <players> <seed> <map>
		 The map is whatever is left on the line, spaces and all, because every shipped map has a
		 space in its name and quoting rules would be one more thing to get wrong at the far end. */
	if (strncmp( command.str(), "skirmish ", 9 ) == 0)
	{
		if (TheGameLogic && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame())
		{
			replyError( "a match is already running; quit it first" );
			return;
		}

		Int players = 0;
		Int seed = 0;
		char mapName[ 512 ];
		if (sscanf( command.str(), "skirmish %d %d %511[^\n]", &players, &seed, mapName ) != 3)
		{
			replyError( "skirmish wants <players> <seed> <map>" );
			return;
		}
		if (players < 2 || players > MAX_SLOTS)
		{
			replyError( "a skirmish holds between two and eight players" );
			return;
		}

		/* m_autoSkirmishPlayers is deliberately left alone: it is also what marks a run unattended,
			 and a match somebody is driving from here has to stay up when it ends rather than write
			 its numbers out and quit. */
		TheWritableGlobalData->m_mapName.set( mapName );
		TheWritableGlobalData->m_fixedSeed = seed;

		extern void GameEngine_startSkirmish( Int numPlayers );
		GameEngine_startSkirmish( players );
		replyOk( "\"starting\":true" );
		return;
	}

	/* Anything else is a world command, and world commands are the scenario grammar with the frame
		 number left off - so put one back on and hand it to the same parser the files go through. One
		 grammar, two front ends, and a line that works in a file works down the socket. */
	AsciiString asScenarioLine;
	asScenarioLine.set( "0 " );
	asScenarioLine.concat( command );

	ScenarioAction action;
	const ScenarioParseResult result = ScenarioDrill_parseLine( asScenarioLine.str(), &action );
	if (result != SCENARIO_PARSE_OK)
	{
		replyError( ScenarioDrill_parseResultName( result ) );
		return;
	}

	if (!TheGameLogic || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame())
	{
		replyError( "no match is running" );
		return;
	}

	if ((Int)thePendingCommands.size() >= CONTROL_MAX_COMMANDS_PER_FRAME)
	{
		replyError( "too many commands queued for one frame" );
		return;
	}

	thePendingCommands.push_back( command );
	replyOk( "\"queued\":true" );
}

// ------------------------------------------------------------------------------------------------
// the two ticks
// ------------------------------------------------------------------------------------------------

void ControlServer_poll( void )
{
	if (TheGlobalData == NULL || TheGlobalData->m_controlPort <= 0)
		return;

	if (theListenSocket == INVALID_SOCKET)
	{
		if (!openListenSocket( TheGlobalData->m_controlPort ))
		{
			// stop trying: a port that will not bind now will not bind on the next frame either
			TheWritableGlobalData->m_controlPort = 0;
			return;
		}
	}

	if (theClientSocket == INVALID_SOCKET)
	{
		const SOCKET accepted = accept( theListenSocket, NULL, NULL );
		if (accepted == INVALID_SOCKET)
			return;
		theClientSocket = accepted;
		setNonBlocking( theClientSocket );
		theHandshakeDone = FALSE;
		theIncoming.clear();
	}

	char chunk[ CONTROL_READ_CHUNK ];
	for( ;; )
	{
		const Int received = recv( theClientSocket, chunk, sizeof( chunk ), 0 );
		if (received > 0)
		{
			for( Int i = 0; i < received; ++i )
				theIncoming.push_back( chunk[ i ] );
			if ((Int)theIncoming.size() > CONTROL_MAX_REQUEST)
			{
				DEBUG_LOG(("CONTROL: client sent more than %d bytes without a frame, closing\n",
									 CONTROL_MAX_REQUEST));
				closeClient();
				return;
			}
			continue;
		}

		if (received == 0)
		{
			DEBUG_LOG(("CONTROL: client went away\n"));
			closeClient();
			return;
		}

		if (WSAGetLastError() != WSAEWOULDBLOCK)
		{
			closeClient();
			return;
		}
		break;
	}

	if (!theHandshakeDone)
	{
		if (theIncoming.empty() || !tryHandshake())
			return;
	}

	unsigned char opcode = 0;
	AsciiString payload;
	while (theClientSocket != INVALID_SOCKET && takeFrame( &opcode, &payload ))
	{
		if (opcode == WS_OPCODE_CLOSE)
		{
			closeClient();
			return;
		}
		if (opcode == WS_OPCODE_PING)
		{
			sendFrame( WS_OPCODE_PONG, payload.str(), (Int)strlen( payload.str() ) );
			continue;
		}
		if (opcode != WS_OPCODE_TEXT)
			continue;

		handleCommand( payload );
	}
}

void ControlServer_runCommands( void )
{
	if (thePendingCommands.empty())
		return;

	for( std::vector<AsciiString>::iterator it = thePendingCommands.begin();
			 it != thePendingCommands.end(); ++it )
	{
		AsciiString asScenarioLine;
		asScenarioLine.set( "0 " );
		asScenarioLine.concat( *it );

		ScenarioAction action;
		if (ScenarioDrill_parseLine( asScenarioLine.str(), &action ) != SCENARIO_PARSE_OK)
			continue;

		ScenarioDrill_execute( action );
	}

	thePendingCommands.clear();

	if (theQuitRequested && TheGameEngine)
		TheGameEngine->setQuitting( TRUE );
}

void ControlServer_shutdown( void )
{
	closeClient();

	if (theListenSocket != INVALID_SOCKET)
	{
		closesocket( theListenSocket );
		theListenSocket = INVALID_SOCKET;
	}

	if (theWinsockStarted)
	{
		WSACleanup();
		theWinsockStarted = FALSE;
	}
}
