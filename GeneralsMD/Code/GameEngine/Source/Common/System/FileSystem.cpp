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

//----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright(C) 2001 - All Rights Reserved                  
//                                                                          
//----------------------------------------------------------------------------
//
// Project:   WSYS Library
//
// Module:    IO
//
// File name: IO_FileSystem.cpp
//
// Created:   4/23/01
//
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Includes                                                      
//----------------------------------------------------------------------------

#include "PreRTS.h"
#include "Common/File.h"
#include "Common/FileSystem.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/CDManager.h"
#include "Common/GameAudio.h"
#include "Common/LocalFileSystem.h"
#include "Common/PerfTimer.h"
#include "Common/RAMFile.h"
#include "Common/RandomMapGenerator.h"


DECLARE_PERF_TIMER(FileSystem)

//----------------------------------------------------------------------------
//         Externals                                                     
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Defines                                                         
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Types                                                     
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Data                                                     
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Public Data                                                      
//----------------------------------------------------------------------------

//===============================
// TheFileSystem 
//===============================
/**
  *	This is the FileSystem's singleton class. All file access 
	* should be through TheFileSystem, unless code needs to use an explicit
	* File or FileSystem derivative.
	*
	* Using TheFileSystem->open and File exclusively for file access, particularly
	* in library or modular code, allows applications to transparently implement 
	* file access as they see fit. This is particularly important for code that
	* needs to be shared between applications, such as games and tools.
	*/
//===============================

FileSystem	*TheFileSystem = NULL;

//----------------------------------------------------------------------------
//         Private Prototypes                                               
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Private Functions                                               
//----------------------------------------------------------------------------


//----------------------------------------------------------------------------
//         Public Functions                                                
//----------------------------------------------------------------------------


//============================================================================
// FileSystem::FileSystem
//============================================================================

FileSystem::FileSystem()
{

}

//============================================================================
// FileSystem::~FileSystem
//============================================================================

FileSystem::~FileSystem()
{

}

//============================================================================
// FileSystem::init
//============================================================================

void		FileSystem::init( void )
{
	m_fileExist.clear();		// see reset(): the cache outlives whatever it was an answer about
	TheLocalFileSystem->init();
	TheArchiveFileSystem->init();
}

//============================================================================
// FileSystem::update
//============================================================================

void		FileSystem::update( void )
{
	USE_PERF_TIMER(FileSystem)
	TheLocalFileSystem->update();
	TheArchiveFileSystem->update();
}

//============================================================================
// FileSystem::reset
//============================================================================

void		FileSystem::reset( void )
{
	USE_PERF_TIMER(FileSystem)

	// doesFileExist remembers its answer for the life of the process and this is the only place
	// that ever gets told the world underneath it changed. A "no" cached before a mod archive was
	// mounted, or before Win32BIGFileSystem::init re-loaded the Patch*.big over the plain pass, made
	// that file invisible for the rest of the run - and the cache is keyed by name, so nothing else
	// would ever ask again.
	m_fileExist.clear();

	TheLocalFileSystem->reset();
	TheArchiveFileSystem->reset();
}

//============================================================================
// FileSystem::open
//============================================================================

File*		FileSystem::openFile( const Char *filename, Int access )
{
	USE_PERF_TIMER(FileSystem)
	File *file = NULL;

	//
	// A generated map is not on any disk and never will be: its seed, player count and size are in
	// its name, so the bytes are built from the name on the first read and served out of memory
	// after that.  It goes in front of both file systems because the map cache, the loader, the
	// CRC and the preview all reach a map through here and none of them has to know the difference.
	//
	const Char *generated = NULL;
	Int generatedSize = 0;
	if ( generatedMapBytes( AsciiString( filename ), &generated, &generatedSize ) )
	{
		RAMFile *ramFile = newInstance( RAMFile );
		if ( ramFile->openFromMemory( generated, generatedSize, AsciiString( filename ) ) )
			return ramFile;

		ramFile->close();
		ramFile->deleteInstance();
		return NULL;
	}

	if ( TheLocalFileSystem != NULL )
	{
		file = TheLocalFileSystem->openFile( filename, access );
	}

	if ( (TheArchiveFileSystem != NULL) && (file == NULL) )
	{
		file = TheArchiveFileSystem->openFile( filename );
	}

	return file;
}

//============================================================================
// FileSystem::doesFileExist
//============================================================================

Bool FileSystem::doesFileExist(const Char *filename) const
{
	USE_PERF_TIMER(FileSystem)

	// a generated map exists wherever its name can be read, which is everywhere - see openFile
	if (isGeneratedMapPath( AsciiString( filename ) ))
		return TRUE;

  unsigned key=TheNameKeyGenerator->nameToLowercaseKey(filename);
  std::map<unsigned,bool>::iterator i=m_fileExist.find(key);
  if (i!=m_fileExist.end())
    return i->second;

	if (TheLocalFileSystem->doesFileExist(filename)) 
  {
    m_fileExist[key]=true;
		return TRUE;
	}
	if (TheArchiveFileSystem->doesFileExist(filename)) 
  {
    m_fileExist[key]=true;
		return TRUE;
	}

  m_fileExist[key]=false;
	return FALSE;
}

//============================================================================
// FileSystem::getFileListInDirectory
//============================================================================
void FileSystem::getFileListInDirectory(const AsciiString& directory, const AsciiString& searchName, FilenameList &filenameList, Bool searchSubdirectories) const
{
	USE_PERF_TIMER(FileSystem)
	TheLocalFileSystem->getFileListInDirectory(AsciiString(""), directory, searchName, filenameList, searchSubdirectories);
	TheArchiveFileSystem->getFileListInDirectory(AsciiString(""), directory, searchName, filenameList, searchSubdirectories);
}

//============================================================================
// FileSystem::getFileInfo
//============================================================================
Bool FileSystem::getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const
{
	USE_PERF_TIMER(FileSystem)
	if (fileInfo == NULL) {
		return FALSE;
	}
	memset(fileInfo, 0, sizeof(fileInfo));

	// a generated map has a size and no timestamp: the bytes follow from the name, so there is no
	// moment at which they were made and nothing that could make them go stale
	const Char *generated = NULL;
	Int generatedSize = 0;
	if (generatedMapBytes(filename, &generated, &generatedSize)) {
		fileInfo->sizeLow = generatedSize;
		return TRUE;
	}

	if (TheLocalFileSystem->getFileInfo(filename, fileInfo)) {
		return TRUE;
	}

	if (TheArchiveFileSystem->getFileInfo(filename, fileInfo)) {
		return TRUE;
	}

	return FALSE;
}

//============================================================================
// FileSystem::createDirectory
//============================================================================
Bool FileSystem::createDirectory(AsciiString directory) 
{
	USE_PERF_TIMER(FileSystem)
	if (TheLocalFileSystem != NULL) {
		return TheLocalFileSystem->createDirectory(directory);
	}
	return FALSE;
}

//============================================================================
// FileSystem::areMusicFilesOnCD
//============================================================================
Bool FileSystem::areMusicFilesOnCD()
{
	// No-CD layouts (Steam) ship the security big next to the exe instead of on
	// a disc; finding it there is the same proof of media this check wants, so
	// look locally first and never prompt for a CD the install cannot have.
	File *localSec = TheLocalFileSystem->openFile("genseczh.big");
	if (localSec) {
		localSec->close();
		return TRUE;
	}

	if (!TheCDManager) {
		DEBUG_LOG(("FileSystem::areMusicFilesOnCD() - No CD Manager; returning false\n"));
		return FALSE;
	}

	AsciiString cdRoot;
	Int dc = TheCDManager->driveCount();
	for (Int i = 0; i < dc; ++i) {
		DEBUG_LOG(("FileSystem::areMusicFilesOnCD() - checking drive %d\n", i));
		CDDriveInterface *cdi = TheCDManager->getDrive(i);
		if (!cdi) {
			continue;
		}

		cdRoot = cdi->getPath();
		if (!cdRoot.endsWith("\\"))
			cdRoot.concat("\\");
		cdRoot.concat("genseczh.big");
		DEBUG_LOG(("FileSystem::areMusicFilesOnCD() - checking for %s\n", cdRoot.str()));
		File *musicBig = TheLocalFileSystem->openFile(cdRoot.str());
		if (musicBig)
		{
			DEBUG_LOG(("FileSystem::areMusicFilesOnCD() - found it!\n"));
			musicBig->close();
			return TRUE;
		}
	}
	return FALSE;
}
//============================================================================
// FileSystem::loadMusicFilesFromCD
//============================================================================
void FileSystem::loadMusicFilesFromCD()
{
	if (!TheCDManager) {
		return;
	}

	AsciiString cdRoot;
	Int dc = TheCDManager->driveCount();
	for (Int i = 0; i < dc; ++i) {
		CDDriveInterface *cdi = TheCDManager->getDrive(i);
		if (!cdi) {
			continue;
		}

		cdRoot = cdi->getPath();
		if (TheArchiveFileSystem->loadBigFilesFromDirectory(cdRoot, MUSIC_BIG)) {
			break;
		}
	}
}

//============================================================================
// FileSystem::unloadMusicFilesFromCD
//============================================================================
void FileSystem::unloadMusicFilesFromCD()
{
	if (!(TheAudio && TheAudio->isMusicPlayingFromCD())) {
		return;
	}

	TheArchiveFileSystem->closeArchiveFile( MUSIC_BIG );
}
