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

// FILE: PerfTimer.cpp ///////////////////////////////////////////////////////////////////////////
// Author: 
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/PerfTimer.h"

#include "Common/GlobalData.h"
#include "GameClient/DebugDisplay.h"
#include "GameClient/Display.h"
#include "GameClient/GraphDraw.h"

__forceinline void ProfileGetTime(__int64 &t)
{
  _asm
  {
    mov ecx,[t]
    push eax
    push edx
    rdtsc
    mov [ecx],eax
    mov [ecx+4],edx
    pop edx
    pop eax
  };
}

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

#if defined(PERF_TIMERS) || defined(DUMP_PERF_STATS)

//-------------------------------------------------------------------------------------------------
static Int64 s_ticksPerSec = 0;
static double s_ticksPerMSec = 0;
static double s_ticksPerUSec = 0;

//-------------------------------------------------------------------------------------------------
void GetPrecisionTimerTicksPerSec(Int64* t)
{
	*t = s_ticksPerSec;
}

//Kris: Plugged in Martin's code to optimize timer setup.
#define HOFFESOMMER_REPLACEMENT_CODE

//-------------------------------------------------------------------------------------------------
void InitPrecisionTimer()
{
#ifdef HOFFESOMMER_REPLACEMENT_CODE

  // measure clock cycles 3 times for 20 msec each
  // then take the 2 counts that are closest, average
  _int64 n[ 3 ];
  for( int k = 0; k < 3; k++ )
  {
    // wait for end of current tick
    unsigned timeEnd = timeGetTime() + 2;
    while( timeGetTime() < timeEnd ); //do nothing
 
    // get cycles
    _int64 start, startQPC, endQPC;
    QueryPerformanceCounter( (LARGE_INTEGER *)&startQPC );
    ProfileGetTime( start );
    timeEnd += 20;
    while( timeGetTime() < timeEnd ); //do nothing
    ProfileGetTime( n[ k ] );
    n[ k ] -= start;
 
    // convert to 1 second
    if( QueryPerformanceCounter( (LARGE_INTEGER*)&endQPC ) )
    {
      QueryPerformanceFrequency( (LARGE_INTEGER*)&s_ticksPerSec );
      n[ k ] = ( n[ k ] * s_ticksPerSec ) / ( endQPC - startQPC );
    }
    else
    {
      n[ k ] = ( n[ k ] * 1000 ) / 20;
    }
  }
 
  // find two closest values
  _int64 d01 = n[ 1 ] - n[ 0 ];
	_int64 d02 = n[ 2 ] - n[ 0 ];
	_int64 d12 = n[ 2 ] - n[ 1 ];

  if( d01 < 0 )
	{
		d01 = -d01;
	}
  if( d02 < 0 ) 
	{
		d02 = -d02;
	}
  if( d12 < 0 )
	{
		d12 = -d12;
	}

  _int64 avg;
  if( d01 < d02 )
  {
    avg = d01 < d12 ? n[ 0 ] + n[ 1 ] : n[ 1 ] + n[ 2 ];
  }
  else
  {
    avg = d02 < d12 ? n[ 0 ] + n[ 2 ] : n[ 1 ] + n[ 2 ];
  }

	//s_ticksPerMSec = 1.0 * TotalTicks / totalTime;
	s_ticksPerMSec = avg / 2000.0f;
	s_ticksPerSec = s_ticksPerMSec * 1000.0f;
	s_ticksPerUSec = s_ticksPerSec / 1000000.0f;

	
#else

	//Kris: With total disrespect, this code costs 5 real seconds of init time
	//whenever we fire up the game.

	#ifdef USE_QPF
		QueryPerformanceFrequency((LARGE_INTEGER*)&s_ticksPerSec);
	#else
		// Init the precision timers
		Int64 totalTime = 0;
		Int64	TotalTicks = 0;
		static int TESTS = 5;
		
		for (int i = 0; i < TESTS; ++i) 
		{
			int        TimeStart;
			int        TimeStop;
			Int64		   StartTicks;
			Int64		   EndTicks;

			TimeStart = timeGetTime();
			GetPrecisionTimer(&StartTicks);
			for(;;)
			{
				TimeStop = timeGetTime();
				if ((TimeStop - TimeStart) > 1000)
				{
					GetPrecisionTimer(&EndTicks);
					break;
				}
			}

			TotalTicks += (EndTicks - StartTicks);

			totalTime += (TimeStop - TimeStart);
		}

		s_ticksPerMSec = 1.0 * TotalTicks / totalTime;
		s_ticksPerSec = s_ticksPerMSec * 1000.0f;
	#endif
		s_ticksPerMSec = s_ticksPerSec / 1000.0f;
		s_ticksPerUSec = s_ticksPerSec / 1000000.0f;

	#ifdef NOT_IN_USE
		Int64 bogus[8];
		GetPrecisionTimer(&start);
		for (Int ii = 0; ii < ITERS; ++ii)
		{
			GetPrecisionTimer(&bogus[0]);
			GetPrecisionTimer(&bogus[1]);
			GetPrecisionTimer(&bogus[2]);
			GetPrecisionTimer(&bogus[3]);
			GetPrecisionTimer(&bogus[4]);
			GetPrecisionTimer(&bogus[5]);
			GetPrecisionTimer(&bogus[6]);
			GetPrecisionTimer(&bogus[7]);
		}
		TheTicksToGetTicks = (bogus[7] - start) / (ITERS*8);
		DEBUG_LOG(("TheTicksToGetTicks is %d (%f usec)\n",(int)TheTicksToGetTicks,TheTicksToGetTicks/s_ticksPerUSec));
	#endif
		
#endif

}
#endif // defined(PERF_TIMERS) || defined(DUMP_PERF_STATS)

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

#ifdef PERF_TIMERS

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

/*static*/ Bool AutoPerfGatherIgnore::s_ignoring = false;

//-------------------------------------------------------------------------------------------------
typedef std::vector< std::pair< AsciiString, AsciiString > > StringPairVec;

//-------------------------------------------------------------------------------------------------
// PerfMetrics class. Basically, request a handle with your name and it will return. We use a vector
// of pairs of asciistrings for this

class PerfMetricsOutput
{
private:
	StringPairVec m_outputStats;

public:

	AsciiString& getStatsString(const AsciiString& id)
	{
		for (int i = 0; i < m_outputStats.size(); ++i) 
		{
			if (m_outputStats[i].first == id) 
				return m_outputStats[i].second;
		}
		std::pair<AsciiString, AsciiString> newPair;
		newPair.first = id;
		m_outputStats.push_back(newPair);
		return m_outputStats.back().second;
	}

	void clearStatsString(const AsciiString& id)
	{
		for (int i = 0; i < m_outputStats.size(); ++i) 
		{
			if (m_outputStats[i].first == id) 
			{
				m_outputStats.erase(m_outputStats.begin() + i);
				return;
			}
		}
	}

	StringPairVec& friend_getAllStatsStrings() { return m_outputStats; }
};

//-------------------------------------------------------------------------------------------------
static PerfMetricsOutput s_output;
static FILE* s_perfStatsFile = NULL;
static Int s_perfDumpOptions = 0;
static UnsignedInt s_lastDumpedFrame = 0;
static char s_buf[256] = "";

__declspec(thread) PerfGather*	PerfGather::m_active[MAX_ACTIVE_STACK] = { 0 };
__declspec(thread) Int					PerfGather::m_activeDepth = 0;
__declspec(thread) Bool					PerfGather::m_gatherOnThisThread = FALSE;
Int64													PerfGather::s_stopStartOverhead = -1;


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
/*static*/ PerfGather*& PerfGather::getHeadPtr()
{
	// funky technique for order-of-init problem. trust me. (srj)
	static PerfGather* s_head = NULL;
	return s_head;
}

//-------------------------------------------------------------------------------------------------
void PerfGather::addToList()
{
	PerfGather*& head = getHeadPtr();

	this->m_next = head;
	if (head)
		head->m_prev = this;
	head = this;
}

//-------------------------------------------------------------------------------------------------
void PerfGather::removeFromList()
{
	PerfGather*& head = getHeadPtr();

	if (this->m_next)
		this->m_next->m_prev = this->m_prev;

	if (this->m_prev)
		this->m_prev->m_next = this->m_next;
	else
		head = this->m_next;

	this->m_prev = 0;
	this->m_next = 0;
}

//-------------------------------------------------------------------------------------------------
// The header has declared this with a netOnly flag since EA added DECLARE_TOTAL_PERF_TIMER;
// the definition never grew it, because nothing has compiled this file since.  Without the
// parameter every gather is a net-time gather and DECLARE_TOTAL_PERF_TIMER does not link.
PerfGather::PerfGather(const char *identifier, Bool netOnly) :
	m_identifier(identifier),
	m_startTime(0),
	m_runningTimeGross(0),
	m_runningTimeNet(0),
	m_callCount(0),
	m_totalTimeNet(0),
	m_totalTimeGross(0),
	m_totalCallCount(0),
	m_worstFrameNet(0),
	m_next(0),
	m_prev(0),
	m_netTimeOnly(netOnly)
{
	//Added By Sadullah Nader
	//Initializations inserted
	m_ignore = FALSE;
	//
	DEBUG_ASSERTCRASH(strchr(m_identifier, ',') == NULL, ("PerfGather names must not contain commas"));
	addToList();
}

//-------------------------------------------------------------------------------------------------
PerfGather::~PerfGather()
{
	removeFromList();
}

//-------------------------------------------------------------------------------------------------
void PerfGather::reset()
{
	m_startTime = 0;
	m_runningTimeGross = 0;
	m_runningTimeNet = 0;
	m_callCount = 0;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void PerfGather::resetAll()
{
	for (PerfGather* head = getHeadPtr(); head != NULL; head = head->m_next)
	{
		head->reset();
	}
}

//-------------------------------------------------------------------------------------------------
static UnsignedInt s_framesAccumulated = 0;

/*static*/ void PerfGather::accumulateFrame(Real frameMS)
{
	// The first second of a match loads assets and would drown every average that follows,
	// which is the same reason dumpAll() skips it.
	if (TheGameLogic && TheGameLogic->getFrame() <= 30)
		return;

	++s_framesAccumulated;
	for (PerfGather* head = getHeadPtr(); head != NULL; head = head->m_next)
	{
		head->m_totalTimeNet += head->m_runningTimeNet;
		head->m_totalTimeGross += head->m_runningTimeGross;
		head->m_totalCallCount += head->m_callCount;
		if (head->m_runningTimeNet > head->m_worstFrameNet)
			head->m_worstFrameNet = head->m_runningTimeNet;
	}

	/* A frame nobody would have noticed needs no report; a frame that dropped a picture needs one
		 while the evidence is still in the gathers, because resetAll() is about to throw it away.
		 Written as one line so a run's stutters can be read off the log with a grep. */
	if (frameMS < (Real)SPIKE_MS || s_ticksPerUSec <= 0.0)
		return;

	char line[1024];
	Int used = sprintf(line, "HEADLESS SPIKE: frame %d took %.1f ms |",
										 TheGameLogic ? TheGameLogic->getFrame() : 0, frameMS);

	// The four worst scopes of this frame, by net time.  Four because a fifth has never yet been
	// the answer and the line has to stay one line.
	enum { TOP = 4 };
	const PerfGather* picked[ TOP ] = { 0 };
	for (Int slot = 0; slot < TOP; ++slot)
	{
		const PerfGather* best = NULL;
		Int64 bestTime = 0;
		for (const PerfGather* head = getHeadPtr(); head != NULL; head = head->m_next)
		{
			if (head->m_runningTimeNet <= bestTime)
				continue;
			Bool taken = FALSE;
			for (Int i = 0; i < slot; ++i)
				if (picked[i] == head) { taken = TRUE; break; }
			if (!taken)
			{
				best = head;
				bestTime = head->m_runningTimeNet;
			}
		}
		if (best == NULL)
			break;
		picked[ slot ] = best;
		const double ms = (double)bestTime / s_ticksPerUSec / 1000.0;
		if (ms < 0.05)
			break;						// nothing left worth naming
		if (used > (Int)sizeof(line) - 64)
			break;
		used += sprintf(line + used, " %s %.1f", best->m_identifier, ms);
	}
	DEBUG_LOG(("%s\n", line));
}

//-------------------------------------------------------------------------------------------------
/*static*/ void PerfGather::dumpSummary()
{
	if (s_framesAccumulated == 0 || s_ticksPerUSec <= 0.0)
	{
		DEBUG_LOG(("HEADLESS PERF: nothing gathered\n"));
		return;
	}

	// Sorted by net time, because the only question this table exists to answer is "what is the
	// frame actually spent on".  Insertion sort over a list that is a few dozen long at most.
	enum { MAX_ROWS = 256 };
	const PerfGather* rows[MAX_ROWS];
	Int numRows = 0;
	for (const PerfGather* head = getHeadPtr(); head != NULL && numRows < MAX_ROWS; head = head->m_next)
	{
		if (head->m_totalCallCount == 0)
			continue;
		Int at = numRows++;
		while (at > 0 && rows[at-1]->m_totalTimeNet < head->m_totalTimeNet)
		{
			rows[at] = rows[at-1];
			--at;
		}
		rows[at] = head;
	}

	DEBUG_LOG(("HEADLESS PERF: %d frames gathered, us/frame net (gross) x calls/frame, worst single frame\n", s_framesAccumulated));
	for (Int i = 0; i < numRows; ++i)
	{
		const PerfGather* g = rows[i];
		// The worst column is the stability column: a scope whose average is nothing and whose
		// worst frame is twenty milliseconds is a stutter, and the average alone hides it.
		DEBUG_LOG(("HEADLESS PERF %s: %.1f (%.1f) x%.2f worst %.1f\n",
			g->m_identifier,
			(double)g->m_totalTimeNet / s_ticksPerUSec / s_framesAccumulated,
			(double)g->m_totalTimeGross / s_ticksPerUSec / s_framesAccumulated,
			(double)g->m_totalCallCount / s_framesAccumulated,
			(double)g->m_worstFrameNet / s_ticksPerUSec));
	}
}

//-------------------------------------------------------------------------------------------------
/*static*/ void PerfGather::initPerfDump(const char* fname, Int options)
{
	// Whichever thread sets the profile up is the one whose frame is being measured.
	m_gatherOnThisThread = TRUE;

	PerfGather::termPerfDump();

	strcpy(s_buf, fname);

	char tmp[256];
	strlcpy(tmp, s_buf, ARRAY_SIZE(tmp));
	strlcat(tmp, ".csv", ARRAY_SIZE(tmp));

	s_perfStatsFile = fopen(tmp, "w");
	s_perfDumpOptions = options;

	if (s_perfStatsFile == NULL)
	{
		DEBUG_CRASH(("could not open/create perf file %s -- is it open in another app?",s_buf));
		return;
	}
	
	if (s_stopStartOverhead == -1)
	{
		const Int ITERS = 100000;
		Int64 start, end;
		PerfGather pf("timer");
		GetPrecisionTimer(&start);
		for (Int ii = 0; ii < ITERS; ++ii)
		{
			pf.startTimer(); pf.stopTimer();
			pf.startTimer(); pf.stopTimer();
			pf.startTimer(); pf.stopTimer();
			pf.startTimer(); pf.stopTimer();
			pf.startTimer(); pf.stopTimer();
			pf.startTimer(); pf.stopTimer();
			pf.startTimer(); pf.stopTimer();
			pf.startTimer(); pf.stopTimer();
		}
		GetPrecisionTimer(&end);
		s_stopStartOverhead = (end - start) / (ITERS*8);
		DEBUG_LOG(("s_stopStartOverhead is %d (%f usec)\n",(int)s_stopStartOverhead,s_stopStartOverhead/s_ticksPerUSec));
	}
}

//-------------------------------------------------------------------------------------------------
/*static*/ void PerfGather::dumpAll(UnsignedInt frame)
{
	if (frame < s_lastDumpedFrame)
	{
		// must have reset or started a new game. 
		termPerfDump();
		initPerfDump(s_buf, s_perfDumpOptions);
	}

	if (!s_perfStatsFile)
	{
		DEBUG_CRASH(("not inited"));
		return;
	}

	if (frame >= 1 && frame <= 30)
	{
		// always skip the first second or so, since it loads everything and skews the results horribly
	}
	else
	{
		if (s_lastDumpedFrame == 0)
		{
			fprintf(s_perfStatsFile, "Frame");
			if (s_perfDumpOptions & PERF_GROSSTIME)
			{
				for (const PerfGather* head = getHeadPtr(); head != NULL; head = head->m_next)
				{
					fprintf(s_perfStatsFile, ",Gross:%s", head->m_identifier);
				}
			}
			if (s_perfDumpOptions & PERF_NETTIME)
			{
				for (const PerfGather* head = getHeadPtr(); head != NULL; head = head->m_next)
				{
					fprintf(s_perfStatsFile, ",Net:%s", head->m_identifier);
				}
			}
			if (s_perfDumpOptions & PERF_CALLCOUNT)
			{
				for (const PerfGather* head = getHeadPtr(); head != NULL; head = head->m_next)
				{
					fprintf(s_perfStatsFile, ",Count:%s", head->m_identifier);
				}
			}
			fprintf(s_perfStatsFile, "\n");
		}
		
		// a strange value so we can find it in the dump, if necessary.
		// there's nothing magic about this value, it's purely determined from sample dumps...
//		const Real CLIP_BIG_SPIKES = 1e10f;
		const Real CLIP_BIG_SPIKES = 100000.0f;

		// make this a nonnumeric thing so Excel won't try to graph it...
		fprintf(s_perfStatsFile, "Frame%08d", frame);
		if (s_perfDumpOptions & PERF_GROSSTIME)
		{
			for (const PerfGather* head = getHeadPtr(); head != NULL; head = head->m_next)
			{
				double t = head->m_runningTimeGross;
				t /= s_ticksPerUSec;
				if (t > CLIP_BIG_SPIKES)
					t = CLIP_BIG_SPIKES;
				fprintf(s_perfStatsFile, ",%f", t);
			}
		}
		if (s_perfDumpOptions & PERF_NETTIME)
		{
			for (const PerfGather* head = getHeadPtr(); head != NULL; head = head->m_next)
			{
				double t = head->m_runningTimeNet;
				t /= s_ticksPerUSec;
				if (t > CLIP_BIG_SPIKES)
					t = CLIP_BIG_SPIKES;
				fprintf(s_perfStatsFile, ",%f", t);
			}
		}
		if (s_perfDumpOptions & PERF_CALLCOUNT)
		{
			for (const PerfGather* head = getHeadPtr(); head != NULL; head = head->m_next)
			{
				fprintf(s_perfStatsFile, ",%d", head->m_callCount);
			}
		}

		fprintf(s_perfStatsFile, "\n");
		fflush(s_perfStatsFile);

		s_lastDumpedFrame = frame;

	}

}

//-------------------------------------------------------------------------------------------------
// This function will queue up stuff to draw on the next frame. We also need to adjust the 
// perf timers to not include time spent paused by the script engine.
/*static*/ void PerfGather::displayGraph(UnsignedInt frame)
{
	if (!TheGraphDraw) {
		return;
	}

	if (frame >= 1 && frame <= 30)
	{
		// always skip the first second or so, since it loads everything and skews the results horribly
	}
	else
	{	
		const Real CLIP_BIG_SPIKES = 100000.0f;

		if (s_perfDumpOptions & PERF_GROSSTIME)
		{
			for (const PerfGather* head = getHeadPtr(); head != NULL; head = head->m_next)
			{
				Real t = head->m_runningTimeGross;
				t /= s_ticksPerUSec;
				if (t > CLIP_BIG_SPIKES)
					t = CLIP_BIG_SPIKES;

				TheGraphDraw->addEntry(head->m_identifier, REAL_TO_INT(t));
			}
		}
		if (s_perfDumpOptions & PERF_NETTIME)
		{
			for (const PerfGather* head = getHeadPtr(); head != NULL; head = head->m_next)
			{
				Real t = head->m_runningTimeNet;
				t /= s_ticksPerUSec;
				if (t > CLIP_BIG_SPIKES)
					t = CLIP_BIG_SPIKES;
				
				TheGraphDraw->addEntry(head->m_identifier, REAL_TO_INT(t));
			}
		}
		if (s_perfDumpOptions & PERF_CALLCOUNT)
		{
			for (const PerfGather* head = getHeadPtr(); head != NULL; head = head->m_next)
			{
				Real t = head->m_callCount;
				TheGraphDraw->addEntry(head->m_identifier, REAL_TO_INT(t));
				
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/*static*/ void PerfGather::termPerfDump()
{
	if (s_perfStatsFile)
	{
		fflush(s_perfStatsFile);
		fclose(s_perfStatsFile);
		s_perfStatsFile = NULL;
	}
	s_lastDumpedFrame = 0;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
PerfTimer::PerfTimer( const char *identifier, Bool crashWithInfo, Int startFrame, Int endFrame) :
	m_identifier(identifier), 
	m_crashWithInfo(crashWithInfo), 
	m_startFrame(startFrame), 
	m_endFrame(endFrame),
	m_callCount(0),
	m_runningTime(0),
	m_outputInfo(true),
	//Added By Sadullah Nader
	//Intializations inserted
	m_lastFrame(-1)
{
}

//-------------------------------------------------------------------------------------------------
PerfTimer::~PerfTimer( )
{
	if (m_endFrame == -1) {
		outputInfo();
	}
}

//-------------------------------------------------------------------------------------------------
void PerfTimer::outputInfo( void )
{
	if (TheGlobalData->m_showMetrics) {
		return;
	}

	if (m_outputInfo && !TheGlobalData->m_showMetrics) {
		m_outputInfo = false;
	} else {
		return;
	}

	if (!s_ticksPerSec) {
		// DEBUG_CRASH here
		return;
	}

	// Not under _DEBUG/_INTERNAL: the DEBUG_LOG below uses these in every configuration a
	// PERF_TIMERS build can be, and a Release measurement build is exactly such a configuration.
	double totalTimeInMS = 1000.0 * m_runningTime / s_ticksPerSec;
	double avgTimePerFrame = totalTimeInMS / (m_lastFrame - m_startFrame + 1);
	double avgTimePerCall = totalTimeInMS / m_callCount;

	if (m_crashWithInfo) {
		DEBUG_CRASH(("%s\n"
								 "Average Time (per call): %.4f ms\n" 
								 "Average Time (per frame): %.4f ms\n"
								 "Average calls per frame: %.2f\n"
								 "Number of calls: %d\n"
								 "Max possible FPS: %.4f\n",
								 m_identifier, 
								 avgTimePerCall,
								 avgTimePerFrame,
								 1.0f * m_callCount / (m_lastFrame - m_startFrame + 1),
								 m_callCount,								 
								 1000.0f / avgTimePerFrame));
	} else {
		DEBUG_LOG(("%s\n"
								 "Average Time (per call): %.4f ms\n" 
								 "Average Time (per frame): %.4f ms\n"
								 "Average calls per frame: %.2f\n"
								 "Number of calls: %d\n"
								 "Max possible FPS: %.4f\n",
								 m_identifier, 
								 avgTimePerCall,
								 avgTimePerFrame,
								 1.0f * m_callCount / (m_lastFrame - m_startFrame + 1),
								 m_callCount,								 
								 1000.0f / avgTimePerFrame));
	}
}

//-------------------------------------------------------------------------------------------------
void PerfTimer::showMetrics( void )
{
	double totalTimeInMS = 1000.0 * m_runningTime / s_ticksPerSec;
	double avgTimePerFrame = totalTimeInMS / (m_lastFrame - m_startFrame + 1);
	double avgTimePerCall = totalTimeInMS / m_callCount;

	// we want to work on the thing in the array, so just store a reference.
	AsciiString &outputStats = s_output.getStatsString(m_identifier);

	outputStats.format("%s: %.2fms / call, %.2fms / frame \n", 
											m_identifier, 
											avgTimePerCall,											
											avgTimePerFrame);
	m_callCount = 0;
	m_runningTime = 0;

	UnsignedInt frm = (TheGameLogic ? TheGameLogic->getFrame() : m_startFrame);
	m_startFrame = frm + 1;
	m_endFrame = m_startFrame + PERFMETRICS_BETWEEN_METRICS;
}

//-------------------------------------------------------------------------------StatMetricsDisplay
void StatMetricsDisplay( DebugDisplayInterface *dd, void *, FILE *fp )
{
	dd->printf("Performance Metrics: \n");
	// no copies will take place because we are storing a reference to the thing
	StringPairVec &stats = s_output.friend_getAllStatsStrings();

	for (int i = 0; i < stats.size(); ++i) {
		dd->printf("%s", stats[i].second.str());
	}
}

#endif	// PERF_TIMERS

