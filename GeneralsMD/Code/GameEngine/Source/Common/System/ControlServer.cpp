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
// -control [port]: the socket, the WebSocket framing, the reply queue, and the commands that are
// about the connection or the whole run.  ControlServer.h says where the rest of the commands live.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/ControlCommands.h"
#include "Common/ControlServer.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/ScenarioDrill.h"
#include "GameNetwork/GameInfo.h"		// MAX_SLOTS, for the skirmish command's player count

#include <deque>

// windows.h, which PreRTS.h already pulled in, brings Winsock 1.1 with it.  That is every call
// this file makes, so it takes the one already on the table rather than starting the winsock2
// header fight described in the hard constraints.

extern void GameEngine_startSkirmish( Int numPlayers );
extern void GameEngine_endMatchAndQuit( void );

// ------------------------------------------------------------------------------------------------
// sizes and constants
// ------------------------------------------------------------------------------------------------

static const Int CONTROL_LOOPBACK_ONLY = INADDR_LOOPBACK;
static const Int CONTROL_BACKLOG = 1;
static const Int CONTROL_READ_CHUNK = 16384;
static const Int CONTROL_FRAME_HEADER_BYTES = 14;			///< the longest header a client frame can carry
static const Int CONTROL_MAX_WAITING_COMMANDS = 256;

/// RFC 6455's magic string, appended to the client key before hashing
static const char *CONTROL_WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static const Int SHA1_DIGEST_BYTES = 20;
static const Int SHA1_BLOCK_BYTES = 64;

// WebSocket opcodes, from RFC 6455 section 5.2
static const unsigned char WS_OPCODE_CONTINUATION = 0x0;
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
// text: words in, JSON out
// ------------------------------------------------------------------------------------------------

static Bool isWordBreak( char c )
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void ControlServer_splitWords( const char *line, std::vector<std::string> *words )
{
	words->clear();
	const char *at = line;
	for( ;; )
	{
		while (*at && isWordBreak( *at ))
			++at;
		if (*at == 0)
			return;

		const char *start = at;
		while (*at && !isWordBreak( *at ))
			++at;
		words->push_back( std::string( start, at - start ) );
	}
}

/** One character of a JSON string.  Below a space is a control character, and from 0x80 up it is
	  escaped by code rather than written as bytes; see ControlServer_appendJsonString. */
static void appendJsonCharacter( UnsignedInt character, std::string *json )
{
	if (character == '"')
		json->append( "\\\"" );
	else if (character == '\\')
		json->append( "\\\\" );
	else if (character == '\n')
		json->append( "\\n" );
	else if (character < 0x20 || character >= 0x80)
	{
		char escaped[ 8 ];
		sprintf( escaped, "\\u%04x", character & 0xFFFF );
		json->append( escaped );
	}
	else
		json->push_back( (char)character );
}

void ControlServer_appendJsonString( const char *text, std::string *json )
{
	json->push_back( '"' );
	for( const char *at = text; *at; ++at )
		appendJsonCharacter( (unsigned char)*at, json );
	json->push_back( '"' );
}

ControlJson::ControlJson( void )
{
}

void ControlJson::clear( void )
{
	m_text.clear();
	m_isFirstInContainer.clear();
}

void ControlJson::writeName( const char *name )
{
	if (!m_isFirstInContainer.empty())
	{
		if (!m_isFirstInContainer.back())
			m_text.push_back( ',' );
		m_isFirstInContainer.back() = FALSE;
	}

	if (name)
	{
		ControlServer_appendJsonString( name, &m_text );
		m_text.push_back( ':' );
	}
}

void ControlJson::beginObject( const char *name )
{
	writeName( name );
	m_text.push_back( '{' );
	m_isFirstInContainer.push_back( TRUE );
}

void ControlJson::endObject( void )
{
	m_text.push_back( '}' );
	m_isFirstInContainer.pop_back();
}

void ControlJson::beginArray( const char *name )
{
	writeName( name );
	m_text.push_back( '[' );
	m_isFirstInContainer.push_back( TRUE );
}

void ControlJson::endArray( void )
{
	m_text.push_back( ']' );
	m_isFirstInContainer.pop_back();
}

void ControlJson::addInt( const char *name, Int value )
{
	writeName( name );
	char number[ 16 ];
	sprintf( number, "%d", value );
	m_text.append( number );
}

void ControlJson::addReal( const char *name, Real value )
{
	writeName( name );
	char number[ 64 ];
	sprintf( number, "%.3f", value );
	m_text.append( number );
}

void ControlJson::addBool( const char *name, Bool value )
{
	writeName( name );
	m_text.append( value ? "true" : "false" );
}

void ControlJson::addString( const char *name, const char *value )
{
	writeName( name );
	ControlServer_appendJsonString( value, &m_text );
}

void ControlJson::addWide( const char *name, const WideChar *value )
{
	writeName( name );
	m_text.push_back( '"' );
	for( const WideChar *at = value; *at; ++at )
		appendJsonCharacter( (UnsignedInt)*at, &m_text );
	m_text.push_back( '"' );
}

const char *ControlCommand_word( const ControlCommand &command, size_t index )
{
	return index < command.words.size() ? command.words[ index ].c_str() : "";
}

Bool ControlCommand_getInt( const ControlCommand &command, size_t index, Int *value )
{
	if (index >= command.words.size())
		return FALSE;

	const char *text = command.words[ index ].c_str();
	char *end = NULL;
	const long parsed = strtol( text, &end, 10 );
	if (end == text || *end != 0)
		return FALSE;

	*value = (Int)parsed;
	return TRUE;
}

Bool ControlCommand_getReal( const ControlCommand &command, size_t index, Real *value )
{
	if (index >= command.words.size())
		return FALSE;

	const char *text = command.words[ index ].c_str();
	char *end = NULL;
	const double parsed = strtod( text, &end );
	if (end == text || *end != 0)
		return FALSE;

	*value = (Real)parsed;
	return TRUE;
}

Bool ControlServer_isMatchRunning( void )
{
	return TheGameLogic->isInGame() && !TheGameLogic->isInShellGame();
}

// ------------------------------------------------------------------------------------------------
// socket state
// ------------------------------------------------------------------------------------------------

static SOCKET theListenSocket = INVALID_SOCKET;
static SOCKET theClientSocket = INVALID_SOCKET;
static Bool theWinsockStarted = FALSE;
static Bool theHandshakeDone = FALSE;
static std::vector<char> theIncoming;
static std::vector<char> theOutgoing;
static std::string theMessage;						///< a text message whose continuation frames are still arriving
static Bool theMessageInProgress = FALSE;
static std::deque<std::string> theWaitingCommands;

static ControlCommand theCurrent;
static Bool theCurrentIsPending = FALSE;
static Bool theQuitRequested = FALSE;
static Bool theWorldActionPending = FALSE;
static ScenarioAction theWorldAction;

static void closeClient( void )
{
	if (theClientSocket != INVALID_SOCKET)
	{
		closesocket( theClientSocket );
		theClientSocket = INVALID_SOCKET;
	}
	theHandshakeDone = FALSE;
	theIncoming.clear();
	theOutgoing.clear();
	theMessage.clear();
	theMessageInProgress = FALSE;
	theWaitingCommands.clear();
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
// sending: everything goes through one queue, because a non-blocking send takes what the socket
// has room for and a reply cut off halfway corrupts every frame after it
// ------------------------------------------------------------------------------------------------

static void flushOutgoing( void )
{
	while (theClientSocket != INVALID_SOCKET && !theOutgoing.empty())
	{
		const Int sent = send( theClientSocket, &theOutgoing[ 0 ], (Int)theOutgoing.size(), 0 );
		if (sent > 0)
		{
			theOutgoing.erase( theOutgoing.begin(), theOutgoing.begin() + sent );
			continue;
		}

		if (sent == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
			return;

		DEBUG_LOG(("CONTROL: send failed with %d, closing\n", WSAGetLastError()));
		closeClient();
		return;
	}
}

static void queueBytes( const char *bytes, Int length )
{
	if (theClientSocket == INVALID_SOCKET)
		return;
	theOutgoing.insert( theOutgoing.end(), bytes, bytes + length );
	flushOutgoing();
}

static void sendFrame( unsigned char opcode, const char *payload, Int length )
{
	char header[ 10 ];
	Int headerLength = 0;
	header[ headerLength++ ] = (char)(0x80 | opcode);

	// a server frame is never masked, so the length is the whole second byte
	if (length < 126)
	{
		header[ headerLength++ ] = (char)length;
	}
	else if (length < 65536)
	{
		header[ headerLength++ ] = (char)126;
		header[ headerLength++ ] = (char)((length >> 8) & 0xFF);
		header[ headerLength++ ] = (char)(length & 0xFF);
	}
	else
	{
		header[ headerLength++ ] = (char)127;
		for( Int i = 0; i < 4; ++i )
			header[ headerLength++ ] = (char)0;
		header[ headerLength++ ] = (char)((length >> 24) & 0xFF);
		header[ headerLength++ ] = (char)((length >> 16) & 0xFF);
		header[ headerLength++ ] = (char)((length >> 8) & 0xFF);
		header[ headerLength++ ] = (char)(length & 0xFF);
	}

	queueBytes( header, headerLength );
	queueBytes( payload, length );
}

// ------------------------------------------------------------------------------------------------
// receiving
// ------------------------------------------------------------------------------------------------

Int ControlServer_parseFrame( const char *data, Int length, ControlFrame *frame )
{
	if (length < 2)
		return 0;

	const unsigned char byte0 = (unsigned char)data[ 0 ];
	const unsigned char byte1 = (unsigned char)data[ 1 ];
	const Bool isMasked = (byte1 & 0x80) != 0;
	Int payloadLength = byte1 & 0x7F;
	Int at = 2;

	if (payloadLength == 126)
	{
		if (length < at + 2)
			return 0;
		payloadLength = ((unsigned char)data[ at ] << 8) | (unsigned char)data[ at + 1 ];
		at += 2;
	}
	else if (payloadLength == 127)
	{
		if (length < at + 8)
			return 0;
		for( Int i = 0; i < 4; ++i )
		{
			if (data[ at + i ] != 0)
				return -1;
		}
		const UnsignedInt lowHalf = ((UnsignedInt)(unsigned char)data[ at + 4 ] << 24)
															| ((UnsignedInt)(unsigned char)data[ at + 5 ] << 16)
															| ((UnsignedInt)(unsigned char)data[ at + 6 ] << 8)
															| ((UnsignedInt)(unsigned char)data[ at + 7 ]);
		if (lowHalf > (UnsignedInt)CONTROL_MAX_MESSAGE_BYTES)
			return -1;
		payloadLength = (Int)lowHalf;
		at += 8;
	}

	unsigned char mask[ 4 ] = { 0, 0, 0, 0 };
	if (isMasked)
	{
		if (length < at + 4)
			return 0;
		for( Int i = 0; i < 4; ++i )
			mask[ i ] = (unsigned char)data[ at + i ];
		at += 4;
	}

	if (length < at + payloadLength)
		return 0;

	frame->opcode = byte0 & 0x0F;
	frame->isFinal = (byte0 & 0x80) != 0;
	frame->payload.resize( payloadLength );
	for( Int i = 0; i < payloadLength; ++i )
		frame->payload[ i ] = (char)((unsigned char)data[ at + i ] ^ mask[ i % 4 ]);

	return at + payloadLength;
}

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

	const Int consumed = (Int)(end - request) + 4;
	theIncoming.erase( theIncoming.begin(), theIncoming.begin() + consumed );

	char reply[ 512 ];
	sprintf( reply,
					 "HTTP/1.1 101 Switching Protocols\r\n"
					 "Upgrade: websocket\r\n"
					 "Connection: Upgrade\r\n"
					 "Sec-WebSocket-Accept: %s\r\n\r\n",
					 acceptKey );
	queueBytes( reply, (Int)strlen( reply ) );

	theHandshakeDone = TRUE;
	DEBUG_LOG(("CONTROL: client connected\n"));
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// replies
// ------------------------------------------------------------------------------------------------

static void sendReply( ControlOutcome outcome )
{
	std::string text;
	if (outcome == CONTROL_FAILED)
	{
		text = "{\"ok\":false,\"error\":";
		ControlServer_appendJsonString( theCurrent.error.c_str(), &text );
		text.push_back( '}' );
	}
	else
	{
		theCurrent.reply.endObject();
		text = theCurrent.reply.getText();
	}
	sendFrame( WS_OPCODE_TEXT, text.data(), (Int)text.size() );
}

ControlCommand &ControlServer_current( void )
{
	return theCurrent;
}

void ControlServer_finish( ControlOutcome outcome )
{
	DEBUG_ASSERTCRASH( theCurrentIsPending, ("CONTROL: a command finished that nobody was waiting on\n") );
	theCurrentIsPending = FALSE;
	sendReply( outcome );
}

// ------------------------------------------------------------------------------------------------
// the commands about the connection and the run
// ------------------------------------------------------------------------------------------------

/** frame number, whether a match is running, and what each player owns. */
static ControlOutcome replyStatus( ControlCommand &command )
{
	const Bool inMatch = ControlServer_isMatchRunning();
	ControlJson &reply = command.reply;

	reply.addInt( "frame", (Int)TheGameLogic->getFrame() );
	reply.addBool( "inGame", inMatch );
	reply.beginArray( "players" );
	for( Int i = 0; inMatch && i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *player = ThePlayerList->getNthPlayer( i );

		Int units = 0;
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			if (obj->getControllingPlayer() == player && !obj->isEffectivelyDead())
				++units;
		}

		reply.beginObject();
		reply.addInt( "index", i );
		reply.addInt( "slot", ThePlayerList->getSlotIndex( i ) );
		reply.addInt( "money", (Int)player->getMoney()->countMoney() );
		reply.addInt( "units", units );
		reply.endObject();
	}
	reply.endArray();
	return CONTROL_DONE;
}

/* skirmish <players> <seed> <map>
	 The map is whatever is left on the line, spaces and all, because every shipped map has a space in
	 its name and quoting rules would be one more thing to get wrong at the far end. */
static ControlOutcome startSkirmish( ControlCommand &command, const std::string &line )
{
	if (ControlServer_isMatchRunning())
		return ControlCommand_fail( command, "a match is already running; quit it first" );

	Int players = 0;
	Int seed = 0;
	char mapName[ 512 ];
	if (sscanf( line.c_str(), " skirmish %d %d %511[^\n]", &players, &seed, mapName ) != 3)
		return ControlCommand_fail( command, "skirmish wants <players> <seed> <map>" );
	if (players < 2 || players > MAX_SLOTS)
		return ControlCommand_fail( command, "a skirmish holds between two and eight players" );

	/* m_autoSkirmishPlayers is deliberately left alone: it is also what marks a run unattended, and a
		 match somebody is driving from here has to stay up when it ends rather than write its numbers
		 out and quit. */
	TheWritableGlobalData->m_mapName.set( mapName );
	TheWritableGlobalData->m_fixedSeed = seed;

	GameEngine_startSkirmish( players );
	command.reply.addBool( "starting", TRUE );
	return CONTROL_DONE;
}

/* A world command is the scenario grammar with the frame number left off - so put one back on and
	 hand it to the same parser the files go through.  It runs inside the next logic frame and replies
	 from there, with whether it found anything to do. */
static ControlOutcome queueWorldCommand( ControlCommand &command, const std::string &line )
{
	std::string asScenarioLine = "0 ";
	asScenarioLine.append( line );

	const ScenarioParseResult result = ScenarioDrill_parseLine( asScenarioLine.c_str(), &theWorldAction );
	if (result != SCENARIO_PARSE_OK)
		return ControlCommand_fail( command, ScenarioDrill_parseResultName( result ) );
	if (!ControlServer_isMatchRunning())
		return ControlCommand_fail( command, "no match is running" );
	if (TheGameLogic->isGamePaused())
		return ControlCommand_fail( command, "the game is paused, and a world command only runs inside a logic frame" );

	theWorldActionPending = TRUE;
	return CONTROL_LATER;
}

static ControlOutcome handleRunCommand( ControlCommand &command, const std::string &line )
{
	const std::string &verb = command.words[ 0 ];

	if (verb == "ping")
	{
		command.reply.addBool( "pong", TRUE );
		return CONTROL_DONE;
	}
	if (verb == "status")
		return replyStatus( command );
	if (verb == "quit")
	{
		theQuitRequested = TRUE;
		command.reply.addBool( "quitting", TRUE );
		return CONTROL_DONE;
	}
	if (verb == "skirmish")
		return startSkirmish( command, line );
	if (verb == "spawn" || verb == "move" || verb == "attackmove" || verb == "attack" || verb == "stop")
		return queueWorldCommand( command, line );

	return CONTROL_NOT_MINE;
}

static void runCommand( const std::string &line )
{
	theCurrent.words.clear();
	theCurrent.error.clear();
	theCurrent.reply.clear();
	theCurrent.reply.beginObject();
	theCurrent.reply.addBool( "ok", TRUE );

	ControlServer_splitWords( line.c_str(), &theCurrent.words );
	if (theCurrent.words.empty())
	{
		theCurrent.error = "an empty command";
		sendReply( CONTROL_FAILED );
		return;
	}

	ControlOutcome outcome = handleRunCommand( theCurrent, line );
	if (outcome == CONTROL_NOT_MINE)
		outcome = ControlQuery_handle( theCurrent );
	if (outcome == CONTROL_NOT_MINE)
		outcome = ControlInput_handle( theCurrent );
	if (outcome == CONTROL_NOT_MINE)
		outcome = ControlActions_handle( theCurrent );
	if (outcome == CONTROL_NOT_MINE)
		outcome = ControlCommand_fail( theCurrent, "unknown command '" + theCurrent.words[ 0 ] + "'" );

	if (outcome == CONTROL_LATER)
	{
		theCurrentIsPending = TRUE;
		return;
	}
	sendReply( outcome );
}

/** One whole text message: a command, queued behind whatever is still running. */
static Bool acceptMessage( const std::string &message )
{
	if ((Int)theWaitingCommands.size() >= CONTROL_MAX_WAITING_COMMANDS)
	{
		DEBUG_LOG(("CONTROL: %d commands waiting and the client sent another, closing\n",
							 CONTROL_MAX_WAITING_COMMANDS));
		closeClient();
		return FALSE;
	}
	theWaitingCommands.push_back( message );
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// the two ticks
// ------------------------------------------------------------------------------------------------

static void readIncoming( void )
{
	char chunk[ CONTROL_READ_CHUNK ];
	for( ;; )
	{
		const Int received = recv( theClientSocket, chunk, sizeof( chunk ), 0 );
		if (received > 0)
		{
			theIncoming.insert( theIncoming.end(), chunk, chunk + received );
			if ((Int)theIncoming.size() > CONTROL_MAX_MESSAGE_BYTES + CONTROL_FRAME_HEADER_BYTES)
			{
				DEBUG_LOG(("CONTROL: client sent more than %d bytes without a frame, closing\n",
									 CONTROL_MAX_MESSAGE_BYTES));
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
			closeClient();
		return;
	}
}

static void takeFrames( void )
{
	ControlFrame frame;
	while (theClientSocket != INVALID_SOCKET && !theIncoming.empty())
	{
		const Int used = ControlServer_parseFrame( &theIncoming[ 0 ], (Int)theIncoming.size(), &frame );
		if (used == 0)
			return;
		if (used < 0)
		{
			DEBUG_LOG(("CONTROL: a frame longer than %d bytes, closing\n", CONTROL_MAX_MESSAGE_BYTES));
			closeClient();
			return;
		}
		theIncoming.erase( theIncoming.begin(), theIncoming.begin() + used );

		if (frame.opcode == WS_OPCODE_CLOSE)
		{
			closeClient();
			return;
		}
		if (frame.opcode == WS_OPCODE_PING)
		{
			sendFrame( WS_OPCODE_PONG, frame.payload.data(), (Int)frame.payload.size() );
			continue;
		}
		if (frame.opcode == WS_OPCODE_TEXT)
		{
			theMessage = frame.payload;
			theMessageInProgress = TRUE;
		}
		else if (frame.opcode == WS_OPCODE_CONTINUATION && theMessageInProgress)
		{
			theMessage.append( frame.payload );
		}
		else
		{
			continue;		// binary frames and stray continuations carry no command
		}

		if ((Int)theMessage.size() > CONTROL_MAX_MESSAGE_BYTES)
		{
			DEBUG_LOG(("CONTROL: a message longer than %d bytes, closing\n", CONTROL_MAX_MESSAGE_BYTES));
			closeClient();
			return;
		}
		if (frame.isFinal)
		{
			theMessageInProgress = FALSE;
			if (!acceptMessage( theMessage ))
				return;
		}
	}
}

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
		if (accepted != INVALID_SOCKET)
		{
			theClientSocket = accepted;
			setNonBlocking( theClientSocket );
			theHandshakeDone = FALSE;
			theIncoming.clear();
		}
	}

	if (theClientSocket != INVALID_SOCKET)
	{
		flushOutgoing();
		readIncoming();
		if (theClientSocket != INVALID_SOCKET && (theHandshakeDone || (!theIncoming.empty() && tryHandshake())))
			takeFrames();
	}

	// what is already under way first, so its reply goes out ahead of the next command's
	ControlInput_tick();
	ControlQuery_tick();

	while (!theCurrentIsPending && !theWaitingCommands.empty())
	{
		const std::string line = theWaitingCommands.front();
		theWaitingCommands.pop_front();
		runCommand( line );
	}

	flushOutgoing();

	if (theQuitRequested)
	{
		theQuitRequested = FALSE;
		GameEngine_endMatchAndQuit();
	}
}

void ControlServer_runCommands( void )
{
	ControlInput_logicFrame();

	if (!theWorldActionPending)
		return;
	theWorldActionPending = FALSE;

	if (ScenarioDrill_execute( theWorldAction ))
	{
		ControlServer_finish( CONTROL_DONE );
		return;
	}
	theCurrent.error = "the command ran and did nothing - no such player, template or units; the log says which";
	ControlServer_finish( CONTROL_FAILED );
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
