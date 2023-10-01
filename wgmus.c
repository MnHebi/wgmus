/* PARTIALLY BASED ON THE WORK OF TONI SPETS
 * Copyright (c) 2012 Toni Spets <toni.spets@iki.fi>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <windows.h>
#include <mmsystem.h>
#include <winreg.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>
#include <dirent.h>
#include <string.h>


/* AUDIO LIBRARY INCLUDES START */

#include <bass/bass.h>
#include <bass/basscd.h>
#include <bass/bassflac.h>
#include <bass/bassmix.h>
#include <bass/basswasapi.h>

/* AUDIO LIBRARY INCLUDES END */

#define MAGIC_DEVICEID 0xBEEF
#define MAX_TRACKS 99

/* PROJECT LIBRARIES START */

/* PROJECT LIBRARIES END */

CRITICAL_SECTION cs;

#define dprintf(...) if (fh) { fprintf(fh, __VA_ARGS__); fflush(NULL); }
FILE *fh = NULL;

char musdll_path[2048];

/* CONFIG FILE DEFINES START */

int FileFormat;
enum PLAYBACKMODE{ CD, MUSICFILE } PlaybackMode;
char MusicFolder[255];
TCHAR MusicFolderFullPath[MAX_PATH];
char strMusicFile[32];
TCHAR MusicFileFullPath[MAX_PATH];
TCHAR MusicFileStoredPath[MAX_PATH];
HANDLE findTracks = INVALID_HANDLE_VALUE;
WIN32_FIND_DATA MusicFiles;

/* defines for storing music data */

struct track_info
{
    char path[MAX_PATH];    /* full path to track */
};

static struct track_info tracks[MAX_TRACKS];
DWORD cdTracks = 0;

struct track_info *info;

int numTracks = 0;
int firstTrack = -1;
int lastTrack = 0;
int currentTrack = -1;
int nextTrack = 1;
int notify = 0;
int queriedTrack = 0;
DWORD queriedCdTrack = 0;
int previousTrack = 0;
uintptr_t notifyDevice;

/* CONFIG FILE DEFINES END */

/* BASS PLAYER DEFINES START */
HWND win;

HSTREAM *strs;
int strc;
HMUSIC *mods;
int modc;
HSAMPLE *sams;
int samc;

HSTREAM str, dec;
BASS_CHANNELINFO cinfo;
int PlaybackFinished;
HFX volFx;
QWORD bassDecodePos;
QWORD bassGetLength;
QWORD bassBufferPos;
QWORD bassFileLength;
float bassPlaybackProgress;
float wasapiVolume;

/* BASS PLAYER DEFINES END */

/* AUDIO PLAYBACK DEFINES START */

enum PLAYSTATE{ NOTPLAYING, STOPPED, PAUSED, PLAYING } playState = NOTPLAYING;
enum PLAYERSTATE{ CLOSED, OPENED } playerState = CLOSED;

int timeFormat = MCI_FORMAT_MILLISECONDS;
int timesPlayed = 0;
int changeNotify = 0;
int noFiles = 0;

/* AUDIO PLAYBACK DEFINES END */

int WasapiVolumeConfig(DWORD streamVol)
{
	if (streamVol >10000) 
	{
	streamVol = 10000;
	}
	wasapiVolume = (double) streamVol * 0.99 / 10000.0;
	dprintf("	Wasapi volume: %.2f\r\n", wasapiVolume);
	
	dprintf("	Wasapi Volume Config wasapiVolume: %.2f\r\n", wasapiVolume); 
	dprintf("	Wasapi Volume Config streamVol: %d\r\n", streamVol); 
	BASS_SetConfig(BASS_CONFIG_GVOL_STREAM, streamVol);
	
	return BASS_ChannelSetAttribute(dec, BASS_ATTRIB_VOLDSP, wasapiVolume);
}

 
int sortstring(const void* a, const void* b)
{
    const char *ia = (const char *)a;
    const char *ib = (const char *)b;
    return strcmp(ia, ib);
}

BOOL FileExists(LPCTSTR szPath)
{
  DWORD dwAttrib = GetFileAttributes(szPath);

  return (dwAttrib != INVALID_FILE_ATTRIBUTES && 
         !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}
 
/* Get audio settings from <exe dir>/wgmmus.ini, set in global variables */
void wgmus_config()
{
	TCHAR ConfigFileNameFullPath[MAX_PATH];
	char *last = strrchr(musdll_path, '\\');
	if (last)
	{
		*last = '\0';
	}
	strncat(musdll_path, "\\", sizeof musdll_path - 1);
	strcpy(ConfigFileNameFullPath, musdll_path);
	LPCSTR ConfigFileName = "wgmus.ini";

	*(strrchr(ConfigFileNameFullPath, '\\')+1)=0;
	strcat(ConfigFileNameFullPath,ConfigFileName);
	
	if(FileExists(ConfigFileNameFullPath)) { dprintf("	Reading audio settings from: %s\r\n", ConfigFileNameFullPath); }
	else { dprintf("	Audio settings file %s does not exist.\r\n", ConfigFileNameFullPath); }
	
	const char *fileFormats[] = {".wav", ".mp3", ".ogg", ".flac", ".aiff"};
	unsigned int numFormats = sizeof(fileFormats) / sizeof(fileFormats[0]);
	
	FileFormat = GetPrivateProfileInt("Settings", "FileFormat", 0, ConfigFileNameFullPath);
	if (FileFormat >= numFormats)
	{
		dprintf("    FileFormat = %d: Invalid - Defaulting to 0\r\n", FileFormat);
		FileFormat = 0;
	}
	dprintf("    File Format is %s\r\n", fileFormats[FileFormat] + 1);
	PlaybackMode = GetPrivateProfileInt("Settings", "PlaybackMode", 0, ConfigFileNameFullPath);
	GetPrivateProfileString("Settings", "MusicFolder", "tamus", MusicFolder, MAX_PATH, ConfigFileNameFullPath);
	dprintf("	FileFormat = %d\r\n", FileFormat);
	dprintf("	PlaybackMode = %d\r\n", PlaybackMode);
	dprintf("	MusicFolder = %s\r\n", MusicFolder);

	strcpy(MusicFolderFullPath, musdll_path);
	*(strrchr(MusicFolderFullPath, '\\')+1)=0;
	strcat(MusicFolderFullPath, MusicFolder);
	dprintf("	Reading music files from: %s\r\n", MusicFolderFullPath);
	strcpy(MusicFileFullPath, MusicFolderFullPath);
	strcat(MusicFileFullPath, "\\");
	dprintf("	Music folder is: %s\r\n", MusicFileFullPath);
	strcpy(strMusicFile, "*");
	strcat(strMusicFile, fileFormats[FileFormat]);
	strcat(MusicFileFullPath, strMusicFile);
	if (PlaybackMode == CD)
	{
		cdTracks = BASS_CD_GetTracks(0);
		dprintf("	Number of tracks on CD is: %d\r\n", cdTracks);
	}
	else
	if (PlaybackMode == MUSICFILE)
	{
		findTracks = FindFirstFileA(MusicFileFullPath, &MusicFiles);
		int i = 2;
		if (findTracks != INVALID_HANDLE_VALUE)
		{
			do
			{
				numTracks++;
				dprintf("	Number of tracks is: %d\r\n", numTracks);
				dprintf("	Music track being read is: %s\r\n", MusicFiles.cFileName);
				strcpy(MusicFileStoredPath, MusicFolderFullPath);
				strcat(MusicFileStoredPath, "\\");
				strcat(MusicFileStoredPath, MusicFiles.cFileName);
				snprintf(tracks[i].path, sizeof tracks[i].path, MusicFileStoredPath, MusicFolderFullPath, i);
				dprintf("	Music track being stored in track info is: %s\r\n", tracks[i].path);
				i++;
			} while (FindNextFileA(findTracks, &MusicFiles) != 0);
			FindClose(findTracks);
			noFiles = 0;
		}
		if (numTracks > 0)
		{
			firstTrack = 2;
			lastTrack = numTracks += 1;
			currentTrack = 2;
			if (numTracks > 1)
			{
				nextTrack = 3;
			}
			else
			nextTrack = 2;
			dprintf("	Assigned First, Last, Current, and Next tracks\r\n");
			dprintf("	First track %d\r\n", firstTrack);
			dprintf("	Last track %d\r\n", lastTrack);
			dprintf("	Current track %d\r\n", currentTrack);
			dprintf("	Next track %d\r\n", nextTrack);
		}
		else
		if (findTracks == INVALID_HANDLE_VALUE)
		{
			currentTrack = 0;
			nextTrack = 0;
			lastTrack = 0;
			noFiles = 1;
			dprintf("	There are no tracks to play\r\n");
		}
	}
	
	return;
}

void printBassError(const char *text)
{
	if(BASS_ErrorGetCode() != 0)
	{
		dprintf("	Error(%d): %s\n", BASS_ErrorGetCode(), text);
	}
	return;
}

DWORD CALLBACK WasapiProc(void *buffer, DWORD length, void *user)
{
	DWORD c = BASS_ChannelGetData(str, buffer, length);
	bassDecodePos = BASS_ChannelGetPosition(dec, BASS_POS_DECODE);
	bassGetLength = BASS_ChannelGetLength(dec, BASS_POS_BYTE);
	bassFileLength =  BASS_StreamGetFilePosition(dec, BASS_FILEPOS_END);
	bassBufferPos = BASS_StreamGetFilePosition(dec, BASS_FILEPOS_AVAILABLE);
	if (bassFileLength > 0 ) 
	{
		bassPlaybackProgress = 100.0 * bassBufferPos / bassFileLength;
	}
	else
	{
		dprintf("    File length was 0; setting progress to 100%.\r\n")
		bassPlaybackProgress = 100.0; // Or idk if should be 0.0 Keeper
	}
	DWORD bassActivity = BASS_ChannelIsActive(dec);
	printBassError("BASS Error Occured");
	if (bassActivity == BASS_ACTIVE_STOPPED)
	{
		if(playState != PAUSED)
		{
			if(bassPlaybackProgress == 0)
			{
				notify = 0;
				changeNotify = 0;
				dprintf("	Finished playback\r\n");
				playState = STOPPED;
				SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, 0x0000000);
				printBassError("BASS Error Occured");
				dprintf("	BASS no activity\r\n");
				BASS_WASAPI_Stop(TRUE);
				BASS_WASAPI_Start();
				return 0;
			}
		}
		else
		if(playState == PAUSED)
		{
			if(bassPlaybackProgress == 0)
			{
				notify = 0;
				changeNotify = 0;
				dprintf("	Finished playback\r\n");
				playState = PLAYING;
				SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, 0x0000000);
				printBassError("BASS Error Occured");
				dprintf("	BASS no activity\r\n");
				BASS_WASAPI_Stop(TRUE);
				BASS_WASAPI_Start();
				return 0;
			}
		}
	}
	
    return c;
}

int bass_init()
{
	int a, count=0;
	BASS_WASAPI_INFO info;
	BASS_WASAPI_GetInfo(&info);
	DWORD bassStarted;
	DWORD bassDeviceCheck;
	DWORD wasapiDeviceCheck;
	
	bassDeviceCheck = BASS_GetDevice();
	bassDeviceCheck = BASS_WASAPI_GetDevice();
	
	static enum INITDONE{ YES, NO } initDone = NO;
	if (initDone == YES)
	{
		dprintf("    BASS already initialized, checking device status\r\n");
		
		if(bassDeviceCheck == -1)
		{
			dprintf("    BASS Device was not intialized, initializing\r\n");
			BASS_Init(0, 4800, 0, 0, NULL);
		}
		else
		dprintf("	BASS_Init already done & device is operational, doing nothing\r\n");
		
		if(wasapiDeviceCheck == -1)
		{
			BASS_WASAPI_Free();
			dprintf("    BASS WASAPI Device was not initialized, initializing\r\n");
			BASS_WASAPI_Init(-1, 0, 0, BASS_WASAPI_AUTOFORMAT, 0.1, 0, WasapiProc, NULL);
		}
		else
		{
			dprintf("	BASS_WASAPI_Init already done & device is operational, doing nothing\r\n");
		}
		
		if(playerState != OPENED)
		{
			playerState = OPENED;
		}
		
		dprintf("    Checking stream status\r\n");
		if(BASS_ErrorGetCode() == 5)
		{
			dprintf("    Encountered BASS Error 5, reinitialize Decoder stream\r\n");
			dec = BASS_StreamCreate(info.freq, info.chans, BASS_STREAM_DECODE|BASS_SAMPLE_FLOAT, STREAMPROC_DUMMY, 0);
			BASS_Mixer_StreamAddChannel(str, dec, 0);
		}
		return 0;
	}
	else
	if (noFiles == 0)
	{
		dprintf("	Audio library for commands is: BASS\r\n");
		dprintf("	BASS_Init\r\n");
		dprintf("    BASS Device initializing\r\n");
		BASS_Init(0, 4800, 0, 0, NULL);
		printBassError("BASS Error Occured");
		
		dprintf("    BASS WASAPI Device initializing\r\n");
		BASS_WASAPI_Init(-1, 0, 0, BASS_WASAPI_AUTOFORMAT, 0.1, 0, WasapiProc, NULL);
		printBassError("BASS Error Occured");

		str = BASS_Mixer_StreamCreate(info.freq, info.chans, BASS_STREAM_DECODE|BASS_SAMPLE_FLOAT);
		printBassError("BASS Error Occured");
		dec = BASS_StreamCreate(info.freq, info.chans, BASS_STREAM_DECODE|BASS_SAMPLE_FLOAT, STREAMPROC_DUMMY, 0);
		printBassError("BASS Error Occured");
		BASS_Mixer_StreamAddChannel(str, dec, 0);
		initDone = YES;
		dprintf("    Checking Player and Play Status\r\n");
		printBassError("BASS Error Occured");
		switch (playerState)
		{
			case OPENED:
			{
				dprintf("    Player Status: OPENED\r\n");
				break;
			}
			case CLOSED:
			{
				dprintf("    Player Status: CLOSED\r\n");
				dprintf("	 Player Status should not be CLOSED on INIT, SETTING OPENED\r\n");
				playerState = OPENED;
				break;
			}
		}
		switch (playState)
		{
			case PLAYING:
			{
				dprintf("    Play Status: PLAYING\r\n");
				break;
			}
			case PAUSED:
			{
				dprintf("    Play Status: PAUSED\r\n");
				break;
			}
			case STOPPED:
			{
				dprintf("    Play Status: STOPPED\r\n");
				break;
			}
		}
		dprintf("	BASS Device Number is: %d\r\n", BASS_GetDevice());
		dprintf("	BASS WASAPI Device Number is: %d\r\n", BASS_WASAPI_GetDevice());
		
		printBassError("BASS Error Occured");
		
		DWORD dataBuffer;
		DWORD bufferSize = sizeof(dataBuffer);
		DWORD dwVolume;
		DWORD finalVolume = 0;
		float wasapiVolume;
		HKEY hkey;
		if (RegOpenKeyExA(HKEY_CURRENT_USER, TEXT("SOFTWARE\\TotalM\\Total Annihilation"), 0, KEY_READ, &hkey) != ERROR_SUCCESS) 
		{
			printf("failed to open key");
			return 1;
		}

		LRESULT status = RegQueryValueEx(
		hkey,
		TEXT("musicvol"),
		NULL,
		NULL,
		(LPBYTE)&dataBuffer,
		&bufferSize);

		if (RegCloseKey(hkey) != ERROR_SUCCESS) 
		{
			printf("failed to close key");
			return 1;
		}

		dprintf("	musicvol regkey status: %d\r\n", status);
		dprintf("	musicvol regkey value: %d\r\n", dataBuffer);
		dprintf("	musicvol regkey size: %d\r\n", bufferSize);
		dwVolume = dataBuffer;
		finalVolume = dwVolume * 156.25;
		dprintf("	BASS initial stream volume set at: %d\r\n", finalVolume);
		WasapiVolumeConfig(finalVolume);
	}
	return 0;
}

int bass_pause()
{
	if (noFiles == 0)
	{
		printBassError("BASS Error Occured");
		if (BASS_ErrorGetCode() != 0)
		{
			return 0;
		}
		else
		BASS_WASAPI_Stop(FALSE);
		dprintf("	BASS_WASAPI_Stop(pause)\r\n");
		playState = PAUSED;
	}
	return 0;
}

void bass_stop()
{
	if (noFiles == 0)
	{
		printBassError("BASS Error Occured");
		if (BASS_ErrorGetCode() != 0)
		{
			if(BASS_ErrorGetCode() == -1)
			{
				BASS_WASAPI_Stop(TRUE);
				BASS_StreamFree(dec);
				BASS_WASAPI_Start();
				dprintf("	BASS_WASAPI_Stop\r\n");
				playState = STOPPED;
				return;
			}
			else
			if(BASS_ErrorGetCode() != -1 && BASS_ErrorGetCode() != 5)
			{
				BASS_WASAPI_Free();
				bass_init();
				dprintf("	BASS_WASAPI_Free\r\n");
				return;
			}
		}
		else
		BASS_WASAPI_Stop(TRUE);
		BASS_StreamFree(dec);
		BASS_WASAPI_Start();
		dprintf("	BASS_WASAPI_Stop\r\n");
		playState = STOPPED;
		/*timesPlayed = 0;*/
	}
	return;
}

int bass_resume()
{
	if (noFiles == 0)
	{
		printBassError("BASS Error Occured");
		if(playState == PAUSED)
		{
			BASS_Start();
			BASS_WASAPI_Start();
			playState = PLAYING;
		}
		
		if(playState == PLAYING)
		{
			BASS_Start();
			BASS_WASAPI_Start();
		}
		else
		if (playState != PLAYING)
		{
			if(PlaybackMode == CD)
			{	
				if(BASS_ErrorGetCode() == 5)
				{
					dec = BASS_CD_StreamCreate(0, currentTrack, BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT);
					BASS_Mixer_StreamAddChannel(str, dec, 0);
				}
				BASS_Start();
				BASS_WASAPI_Start();
			}	
			else
			if(PlaybackMode == MUSICFILE)
			{	
				if(BASS_ErrorGetCode() == 5)
				{
					if(FileFormat != 3)
					{
						dec = BASS_StreamCreateFile(FALSE, tracks[currentTrack].path, 0, 0, BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_STREAM_PRESCAN);
						BASS_Mixer_StreamAddChannel(str, dec, 0);
					}
					else
					if(FileFormat == 3)
					{
						dec = BASS_FLAC_StreamCreateFile(FALSE, tracks[currentTrack].path, 0, 0, BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_STREAM_PRESCAN);
						BASS_Mixer_StreamAddChannel(str, dec, 0);
					}
				}
				BASS_Start();
				BASS_WASAPI_Start();
			}
		}
		dprintf("	BASS_WASAPI_Start(unpause)\r\n");
		playState = PLAYING;
	}
	return 0;
}

int bass_clear()
{
	if (noFiles == 0)
	{
		BASS_StreamFree(dec);
		printBassError("BASS Error Occured");
		if (BASS_ErrorGetCode() != 0 && BASS_ErrorGetCode() != 5)
		{
			return 0;
		}
		else
		BASS_WASAPI_Stop(TRUE);
		if(PlaybackMode == CD)
		{
			dec = BASS_CD_StreamCreate(0, currentTrack, BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT);
			BASS_Mixer_StreamAddChannel(str, dec, 0);
			BASS_WASAPI_Start();
		}
		else
		if(PlaybackMode == MUSICFILE)
		{
			if(FileFormat != 3)
			{
				dec = BASS_StreamCreateFile(FALSE, tracks[currentTrack].path, 0, 0, BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_STREAM_PRESCAN);
				BASS_Mixer_StreamAddChannel(str, dec, 0);
				BASS_WASAPI_Start();
			}
			else
			if(FileFormat == 3)
			{
				dec = BASS_FLAC_StreamCreateFile(FALSE, tracks[currentTrack].path, 0, 0, BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_STREAM_PRESCAN);
				BASS_Mixer_StreamAddChannel(str, dec, 0);
				BASS_WASAPI_Start();
			}
		}
		dprintf("	Track for bass_clear is: %d\r\n", currentTrack);
		dprintf("	BASS_ChannelStop + StreamFree + ChannelPlay\r\n");
	}
	return 0;
}

int bass_forceplay(const char *path)
{
	DWORD bassDeviceCheck;
	DWORD wasapiDeviceCheck;
	bassDeviceCheck = BASS_GetDevice();
	wasapiDeviceCheck = BASS_WASAPI_GetDevice();
	if (noFiles == 0)
	{
		if(playState != PAUSED)
		{
			printBassError("BASS Error Occured");
			if(currentTrack == 0)
			{
				currentTrack = 2;
			}
			BASS_StreamFree(dec);
			if(wasapiDeviceCheck == -1)
			{
				bassDeviceCheck = BASS_GetDevice();
				if(bassDeviceCheck == -1)
				{
					if (!BASS_Init(0, 48000, 0, 0, NULL))
					{
						dprintf("	Bass Device Initialization FAILED\r\n");
					}
				}
				if(wasapiDeviceCheck == -1)
				{
					if (!BASS_WASAPI_Init(-1, 0, 0, BASS_WASAPI_AUTOFORMAT, 0.1, 0, WasapiProc, NULL))
					{
						dprintf("	Wasapi Device Initialization FAILED\r\n");
					}
				}
			}
			dprintf("	bass_forceplay\r\n");
			dprintf("	BASS WASAPI Device Number is: %d\r\n", BASS_WASAPI_GetDevice());
		
			if (PlaybackMode == CD)
			{
				PlaybackFinished = 0;
				BASS_StreamFree(dec);
				dec = BASS_CD_StreamCreate(0, currentTrack, BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT);
				BASS_Mixer_StreamAddChannel(str, dec, 0);
				BASS_WASAPI_Start();
				playState = PLAYING;
				timesPlayed++;
				dprintf("	Begin CD Playback\r\n");
			}
			else
			if (PlaybackMode == MUSICFILE)
			{
				PlaybackFinished = 0;
				if(FileFormat != 3)
				{
					PlaybackFinished = 0;
					BASS_StreamFree(dec);
					dec = BASS_StreamCreateFile(FALSE, tracks[currentTrack].path, 0, 0, BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_STREAM_PRESCAN);
					BASS_Mixer_StreamAddChannel(str, dec, 0);
					BASS_WASAPI_Start();
					playState = PLAYING;
					timesPlayed++;
					dprintf("	Begin Music File Playback\r\n");
				}
				else
				if(FileFormat == 3)
				{
					PlaybackFinished = 0;
					BASS_StreamFree(dec);
					dec = BASS_FLAC_StreamCreateFile(FALSE, tracks[currentTrack].path, 0, 0, BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_STREAM_PRESCAN);
					BASS_Mixer_StreamAddChannel(str, dec, 0);
					BASS_WASAPI_Start();
					playState = PLAYING;
					timesPlayed++;
					dprintf("	Begin Music File(FLAC) Playback\r\n");
				}
			}
		}
	}
	return 0;
}

int bass_play(const char *path)
{
	DWORD bassDeviceCheck;
	DWORD wasapiDeviceCheck;
	bassDeviceCheck = BASS_GetDevice();
	wasapiDeviceCheck = BASS_WASAPI_GetDevice();
	if (noFiles == 0)
	{
		if(playState != PAUSED)
		{
			printBassError("BASS Error Occured");
			if(currentTrack == 0)
			{
				currentTrack = 2;
			}
			BASS_StreamFree(dec);
			if(wasapiDeviceCheck == -1)
			{
				bassDeviceCheck = BASS_GetDevice();
				if(bassDeviceCheck == -1)
				{
					if (!BASS_Init(0, 48000, 0, 0, NULL))
					{
						dprintf("	Bass Device Initialization FAILED\r\n");
					}
				}
				if(wasapiDeviceCheck == -1)
				{
					if (!BASS_WASAPI_Init(-1, 0, 0, BASS_WASAPI_AUTOFORMAT, 0.1, 0, WasapiProc, NULL))
					{
						dprintf("	Wasapi Device Initialization FAILED\r\n");
					}
				}
			}
			dprintf("	bass_play\r\n");
			dprintf("	BASS WASAPI Device Number is: %d\r\n", BASS_WASAPI_GetDevice());
		
			if (PlaybackMode == CD)
			{
				PlaybackFinished = 0;
				BASS_StreamFree(dec);
				dec = BASS_CD_StreamCreate(0, currentTrack, BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT);
				BASS_Mixer_StreamAddChannel(str, dec, 0);
				BASS_WASAPI_Start();
				playState = PLAYING;
				timesPlayed++;
				dprintf("	Begin CD Playback\r\n");
			}
			else
			if (PlaybackMode == MUSICFILE)
			{
				PlaybackFinished = 0;
				if(FileFormat != 3)
				{
					PlaybackFinished = 0;
					BASS_StreamFree(dec);
					dec = BASS_StreamCreateFile(FALSE, tracks[currentTrack].path, 0, 0, BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_STREAM_PRESCAN);
					BASS_Mixer_StreamAddChannel(str, dec, 0);
					BASS_WASAPI_Start();
					playState = PLAYING;
					timesPlayed++;
					dprintf("	Begin Music File Playback\r\n");
				}
				else
				if(FileFormat == 3)
				{
					PlaybackFinished = 0;
					BASS_StreamFree(dec);
					dec = BASS_FLAC_StreamCreateFile(FALSE, tracks[currentTrack].path, 0, 0, BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_STREAM_PRESCAN);
					BASS_Mixer_StreamAddChannel(str, dec, 0);
					BASS_WASAPI_Start();
					playState = PLAYING;
					timesPlayed++;
					dprintf("	Begin Music File(FLAC) Playback\r\n");
				}
			}
		}
	}
	
	return 0;
}

void WINAPI fake_ExitProcess(UINT uExitCode)
{
	BASS_WASAPI_Free();
	BASS_Free();
	if (fh)
	{
		fclose(fh);
		fh = NULL;
	}

	return ExitProcess(uExitCode);
}

int wgmus_main()
{
	wgmus_config();
	return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		fh = fopen("wgmus.log", "w"); /* Renamed to .log*/

		GetModuleFileName(hinstDLL, musdll_path, sizeof musdll_path);
		dprintf("	dll attached\r\n");
		dprintf("	musdll_path = %s\r\n", musdll_path);

		InitializeCriticalSection(&cs);
		wgmus_config();
	}

	if (fdwReason == DLL_PROCESS_DETACH)
	{

    }

    return TRUE;
}

MCIERROR WINAPI wgmus_mciSendCommandA(MCIDEVICEID deviceID, UINT uintMsg, DWORD_PTR dwptrCmd, DWORD_PTR dwParam)
{
	if(TRUE)
	{
		dprintf("mciSendCommandA(deviceID=%p, uintMsg=%p, dwptrCmd=%p, dwParam=%p)\r\n", deviceID, uintMsg, dwptrCmd, dwParam);
		if (deviceID == MAGIC_DEVICEID)
		{
			if (uintMsg == MCI_OPEN)
			{
				dprintf("  MCI_OPEN\r\n");
				if(playerState != OPENED)
				{
					playerState = OPENED;		
					dprintf("	Initialize BASS\r\n");
					bass_init();
					uintMsg = 0;
				}
				if(playState == PAUSED)
				{
					BASS_WASAPI_Start();
					uintMsg = 0;
				}
				return 0;
			}
			else
			if (uintMsg == MCI_PAUSE)
			{
				if(playerState == OPENED)
				{
					dprintf("  MCI_PAUSE\r\n");
					if(playState != PAUSED)
					{
						bass_pause();
						BASS_WASAPI_Stop(FALSE);
						uintMsg = 0;
					}
				}
				return 0;
			}
			else
			if (uintMsg == MCI_STOP)
			{
				if(playerState == OPENED)
				{
					if(playState != STOPPED)
					{
						dprintf("  MCI_STOP\r\n");
						bass_stop();
						uintMsg = 0;
					}
				}
				return 0;
			}
			else
			if (uintMsg == MCI_CLOSE)
			{
				dprintf("  MCI_CLOSE\r\n");
				if(playerState != CLOSED)
				{
					//playerState = CLOSED;
					bass_stop();
					BASS_WASAPI_Stop(TRUE);
					dprintf("	Ignoring close command since TA will still send commands after it, potentially causing freezes\r\n");
					uintMsg = 0;
				}
				return 0;
			}
			else
			if (uintMsg == MCI_STATUS)
			{
				LPMCI_STATUS_PARMS parms = (LPVOID)dwParam;

				dprintf("  MCI_STATUS\r\n");

				parms->dwReturn = 0;
				
				if (PlaybackMode == CD)
				{
					if (parms->dwItem == MCI_STATUS_NUMBER_OF_TRACKS)
					{
						dprintf("      MCI_STATUS_NUMBER_OF_TRACKS %d\r\n", cdTracks);
						parms->dwReturn = cdTracks;
						uintMsg = 0;
					}
					else
					if (parms->dwItem == MCI_CDA_STATUS_TYPE_TRACK)
					{
						dprintf("      MCI_CDA_STATUS_TYPE_TRACK\r\n");
						if((parms->dwTrack > 0) &&  (parms->dwTrack <= MAX_TRACKS))
						{
							parms->dwTrack -= 1;
							DWORD bassCdTrackLength = BASS_CD_GetTrackLength(0, parms->dwTrack);
							if (bassCdTrackLength > 0)
							{
								parms->dwReturn = MCI_CDA_TRACK_AUDIO;
								dprintf("      MCI_CDA_TRACK_AUDIO\r\n");
								uintMsg = 0;
							}
							else
							if (bassCdTrackLength < 0)
							{
								parms->dwReturn = MCI_CDA_TRACK_OTHER;
								dprintf("      MCI_CDA_TRACK_OTHER\r\n");
								uintMsg = 0;
							}
						}
					}
					else
					if (parms->dwItem == MCI_STATUS_CURRENT_TRACK)
					{
						currentTrack++;
						parms->dwReturn = currentTrack;
						dprintf("	Sending current track: %d\r\n", currentTrack);
						uintMsg = 0;
					}
					else
					if (parms->dwItem == MCI_STATUS_POSITION)
					{
						char trackNumber[3];
						char trackSeconds[3];
						char trackMilliseconds[3];
						
						QWORD bassLengthInSeconds = BASS_ChannelBytes2Seconds(dec, BASS_ChannelGetLength(dec, BASS_POS_BYTE));
						dprintf("	BASS Length in seconds: %d\r\n", bassLengthInSeconds);
						QWORD bassPosInSeconds = BASS_ChannelBytes2Seconds(dec, BASS_ChannelGetPosition(dec, BASS_POS_BYTE));
						dprintf("	BASS Position in seconds: %d\r\n", bassPosInSeconds);
						int bassMilliseconds = (bassLengthInSeconds - bassPosInSeconds) * 1000;
						int bassSeconds = bassLengthInSeconds - bassPosInSeconds;
						int bassMinutes = (bassLengthInSeconds - bassPosInSeconds) / 60;
						if (dwptrCmd & MCI_TRACK)
						{
							parms->dwTrack -= 1;
							queriedCdTrack = parms->dwTrack;
							if(timeFormat == MCI_FORMAT_MILLISECONDS)
							{
								queriedCdTrack += 1;
								parms->dwReturn = queriedCdTrack;
								uintMsg = 0;
							}
							else
							if(timeFormat == MCI_FORMAT_TMSF)
							{
								queriedCdTrack += 1;
								parms->dwReturn = MCI_MAKE_TMSF(queriedCdTrack, 0, 0, 0);
								uintMsg = 0;
							}
						}
						else
						if(timeFormat == MCI_FORMAT_MILLISECONDS)
						{
							currentTrack++;
							parms->dwReturn = currentTrack;
							uintMsg = 0;
						}
						else
						if(timeFormat == MCI_FORMAT_TMSF)
						{
							currentTrack++;
							snprintf(trackNumber, 3, "%02d", currentTrack);
							snprintf(trackMilliseconds, 3, "%02d", bassMilliseconds);
							snprintf(trackSeconds, 3, "%02d", bassMinutes);
							parms->dwReturn = MCI_MAKE_TMSF(trackNumber, trackSeconds, trackMilliseconds, 0);
							uintMsg = 0;
						}
					}
					if (parms->dwItem == MCI_STATUS_MODE)
					{
						dprintf("      MCI_STATUS_MODE\r\n");
						if(playerState == OPENED && playState == NOTPLAYING)
						{
							dprintf("        we are open\r\n");
							parms->dwReturn = MCI_MODE_OPEN;
							uintMsg = 0;
						}
						else
						if(playerState == CLOSED && playState == NOTPLAYING)
						{
							dprintf("        player not ready\r\n");
							parms->dwReturn = MCI_MODE_NOT_READY;
							uintMsg = 0;
						}							
						else
						if(playerState == OPENED && playState == PAUSED)
						{
							dprintf("        we are paused\r\n");
							parms->dwReturn = MCI_MODE_PAUSE;
							uintMsg = 0;
						}
						else
						if(playerState == OPENED && playState == STOPPED)
						{
							dprintf("        we are stopped\r\n");
							parms->dwReturn = MCI_MODE_STOP;
							uintMsg = 0;
						}
						else
						if(playerState == OPENED && playState == PLAYING)
						{
							dprintf("        we are playing\r\n");
							parms->dwReturn = MCI_MODE_PLAY;
							uintMsg = 0;
						}
					}
					return 0;
				}
				else
				if (PlaybackMode == MUSICFILE)
				{
					if (parms->dwItem == MCI_STATUS_NUMBER_OF_TRACKS)
					{
						dprintf("      MCI_STATUS_NUMBER_OF_TRACKS %d\r\n", numTracks);
						parms->dwReturn = numTracks;
						uintMsg = 0;
					}
					else
					if (parms->dwItem == MCI_CDA_STATUS_TYPE_TRACK)
					{
						dprintf("      MCI_CDA_STATUS_TYPE_TRACK MCI_CDA_TRACK_OTHER\r\n");
						if((parms->dwTrack == 1) &&  (parms->dwTrack < MAX_TRACKS))
						{
							parms->dwReturn = MCI_CDA_TRACK_OTHER;
							uintMsg = 0;
						}
					}
					else
					if (parms->dwItem == MCI_STATUS_CURRENT_TRACK)
					{
						dprintf("	Sending current track: %d\r\n", currentTrack);
						parms->dwReturn = currentTrack;
						uintMsg = 0;
					}
					else
					if (parms->dwItem == MCI_STATUS_POSITION)
					{
						char trackNumber[3];
						char trackSeconds[3];
						char trackMilliseconds[3];

						QWORD bassLengthInSeconds = BASS_ChannelBytes2Seconds(dec, BASS_ChannelGetLength(dec, BASS_POS_BYTE));
						QWORD bassPosInSeconds = BASS_ChannelBytes2Seconds(dec, BASS_ChannelGetPosition(dec, BASS_POS_BYTE));
						int bassMilliseconds = (bassLengthInSeconds - bassPosInSeconds) * 1000;
						int bassSeconds = bassLengthInSeconds - bassPosInSeconds;
						int bassMinutes = (bassLengthInSeconds - bassPosInSeconds) / 60;
						if (dwptrCmd & MCI_TRACK)
						{
							dprintf("	BASS Length in seconds: %d\r\n", bassLengthInSeconds);
							dprintf("	BASS Position in seconds: %d\r\n", bassPosInSeconds);
							queriedTrack = (int)(parms->dwTrack);
							if(timeFormat == MCI_FORMAT_MILLISECONDS)
							{
								parms->dwReturn = queriedTrack;
								uintMsg = 0;
							}
							else
							if(timeFormat == MCI_FORMAT_TMSF)
							{
								parms->dwReturn = MCI_MAKE_TMSF(queriedTrack, 0, 0, 0);
								uintMsg = 0;
							}
						}
						else
						if(timeFormat == MCI_FORMAT_MILLISECONDS)
						{
							parms->dwReturn = currentTrack;
							uintMsg = 0;
						}
						else
						if(timeFormat == MCI_FORMAT_TMSF)
						{
							snprintf(trackNumber, 3, "%02d", currentTrack);
							snprintf(trackMilliseconds, 3, "%02d", bassMilliseconds);
							snprintf(trackSeconds, 3, "%02d", bassMinutes);
							parms->dwReturn = MCI_MAKE_TMSF(trackNumber, trackSeconds, trackMilliseconds, 0);
							uintMsg = 0;
						}
					}
					if (parms->dwItem == MCI_STATUS_MODE)
					{
						dprintf("      MCI_STATUS_MODE\r\n");
						if(playerState == OPENED && playState == NOTPLAYING)
						{
							dprintf("        we are open\r\n");
							parms->dwReturn = MCI_MODE_OPEN;
							uintMsg = 0;
						}
						else
						if(playerState == CLOSED && playState == NOTPLAYING)
						{
							dprintf("        player not ready\r\n");
							parms->dwReturn = MCI_MODE_NOT_READY;
							uintMsg = 0;
						}							
						else
						if(playerState == OPENED && playState == PAUSED)
						{
							dprintf("        we are paused\r\n");
							parms->dwReturn = MCI_MODE_PAUSE;
							uintMsg = 0;
						}
						else
						if(playerState == OPENED && playState == STOPPED)
						{
							dprintf("        we are stopped\r\n");
							parms->dwReturn = MCI_MODE_STOP;
							uintMsg = 0;
						}
						else
						if(playerState == OPENED && playState == PLAYING)
						{
							dprintf("        we are playing\r\n");
							parms->dwReturn = MCI_MODE_PLAY;
							uintMsg = 0;
						}
					}
					return 0;
				}
			}
			else
			if (uintMsg == MCI_SET)
			{
				LPMCI_SET_PARMS parms = (LPVOID)dwParam;
			
				dprintf("  MCI_SET\r\n");
			
				if (dwptrCmd & MCI_SET_TIME_FORMAT)
				{
					dprintf("    MCI_SET_TIME_FORMAT\r\n");
					timeFormat = parms->dwTimeFormat;
					if (parms->dwTimeFormat == MCI_FORMAT_MILLISECONDS)
					{
						dprintf("      MCI_FORMAT_MILLISECONDS\r\n");
						dwptrCmd = 0;
						uintMsg = 0;
					}
					else
					if (parms->dwTimeFormat == MCI_FORMAT_TMSF)
					{
						dprintf("      MCI_FORMAT_TMSF\r\n");
						dwptrCmd = 0;
						uintMsg = 0;
					}
				}
				return 0;
			}
			else
			if (uintMsg == MCI_PLAY)
			{
				dprintf("  MCI_PLAY\r\n");
			
				LPMCI_PLAY_PARMS parms = (LPVOID)dwParam;
				
				if(playState == PAUSED)
				{
					if (dwptrCmd & MCI_NOTIFY)
					{
						dprintf("	bass_resume from paused via notify\r\n");
						dwptrCmd = 0;
						uintMsg = 0;
						bass_resume();
					}
				}
				else
				if (playState != PAUSED)
				{
					if (dwptrCmd & MCI_FROM)
					{
						notifyDevice = deviceID;
						dprintf("  MCI_FROM\r\n");
					}
					else
					if (dwptrCmd & MCI_TO)
					{
						dprintf("  MCI_TO\r\n");
					}
					
					if (PlaybackMode == CD)
					{
						parms->dwFrom -= 1;
					}
					
					currentTrack = (int)(parms->dwFrom);
					nextTrack = (int)(parms->dwTo);
					dprintf("	From value: %d\r\n", parms->dwFrom);
					dprintf("	Current track int value is: %d\r\n", currentTrack);
					

					if (timesPlayed > 0)
					{
						changeNotify = 1;
						bass_forceplay(tracks[currentTrack].path);
						dwptrCmd = 0;
						uintMsg = 0;
						timesPlayed = 1;
					}
					else
					if (timesPlayed == 0)
					{
						bass_clear();
						notify = 1;
						dwptrCmd = 0;
						uintMsg = 0;
						bass_play(tracks[currentTrack].path);
						previousTrack = currentTrack;
					}
				}
				else
				if (dwptrCmd & MCI_NOTIFY)
				{
					dprintf("	BASS_ChannelPlay from paused via notify\r\n");
					bass_resume();
					dwptrCmd = 0;
					uintMsg = 0;
				}
				return 0;
			}
		}
	}
	return MCIERR_UNRECOGNIZED_COMMAND;
}

MCIERROR WINAPI wgmus_mciSendStringA(LPCTSTR lpszCmd, LPTSTR lpszRetStr, UINT cchReturn, HANDLE  hwndCallback)
{
	MCIERROR err;
	if(TRUE) 
	{
		dprintf("[MCI String = %s, MCI DEVICE ID = %08X]\n", lpszCmd, hwndCallback);
		
		for (int i = 0; lpszCmd[i]; i++)
		{
			tolower(lpszCmd[i]);
		}
		
		int cTrack = 0;
		
		if (strstr(lpszCmd, "open cdaudio"))
		{
			static MCI_WAVE_OPEN_PARMS waveParms;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_OPEN, 0, (DWORD_PTR)NULL);
			lpszCmd = "";
			return 0;
		}
		if (strstr(lpszCmd, "pause cdaudio"))
		{
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_PAUSE, 0, (DWORD_PTR)NULL);
			lpszCmd = "";
			return 0;
		}
		if (strstr(lpszCmd, "stop cdaudio"))
		{
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STOP, 0, (DWORD_PTR)NULL);
			lpszCmd = "";
			return 0;
		}
		if (strstr(lpszCmd, "close cdaudio"))
		{
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_CLOSE, 0, (DWORD_PTR)NULL);
			lpszCmd = "";
			return 0;
		}
		if (strstr(lpszCmd, "set cdaudio time format milliseconds"))
		{
			static MCI_SET_PARMS parms;
			parms.dwTimeFormat = MCI_FORMAT_MILLISECONDS;	
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
			lpszCmd = "";
			return 0;
		}
		if (strstr(lpszCmd, "set cdaudio time format tmsf"))
		{
			static MCI_SET_PARMS parms;
			parms.dwTimeFormat = MCI_FORMAT_TMSF;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
			lpszCmd = "";
			return 0;
		}
		if (strstr(lpszCmd, "status cdaudio number of tracks"))
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_NUMBER_OF_TRACKS;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&parms);
			sprintf(lpszRetStr, "%d", numTracks);
			lpszCmd = "";
			return 0;
		}
		if (sscanf(lpszCmd, "status cdaudio type track %d", &cTrack))
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_CDA_STATUS_TYPE_TRACK;
			parms.dwTrack = cTrack;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
			sprintf(lpszRetStr, "%d", parms.dwReturn);
			lpszCmd = "";
			return 0;
		}
		if (strstr(lpszCmd, "status cdaudio mode"))
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_MODE;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_STATUS_MODE, (DWORD_PTR)&parms);
			lpszCmd = "";
			return 0;
		}
		if (strstr(lpszCmd, "status cdaudio current track"))
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_CURRENT_TRACK;
			parms.dwTrack = currentTrack;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
			sprintf(lpszRetStr, "%d", parms.dwReturn);
			lpszCmd = "";
			return 0;
		}
		if (strstr(lpszCmd, "position"))
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_POSITION;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&parms);
			sprintf(lpszRetStr, "%d", parms.dwReturn);
			lpszCmd = "";
			return 0;
        }
		if (sscanf(lpszCmd, "position track %d", &cTrack))
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_POSITION;
			parms.dwTrack = cTrack;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
			sprintf(lpszRetStr, "%d", parms.dwReturn);
			lpszCmd = "";
			return 0;
        }
		int from = -1, to = -1;
		if (sscanf(lpszCmd, "play cdaudio from %d to %d notify", &from, &to))
		{
			static MCI_PLAY_PARMS parms;
			parms.dwFrom = from;
			parms.dwTo = to;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM|MCI_TO|MCI_NOTIFY, (DWORD_PTR)&parms);
			lpszCmd = "";
			return 0;
		}
		else
		if (sscanf(lpszCmd, "play cdaudio from %d notify", &from))
		{
			static MCI_PLAY_PARMS parms;
			parms.dwFrom = from;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM|MCI_NOTIFY, (DWORD_PTR)&parms);
			lpszCmd = "";
			return 0;
		}
		else
		if (sscanf(lpszCmd, "play cdaudio from %d", &from))
		{
			static MCI_PLAY_PARMS parms;
			parms.dwFrom = from;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM, (DWORD_PTR)&parms);
			lpszCmd = "";
			return 0;
		}
		else
		if (strstr(lpszCmd, "play cdaudio notify"))
		{
			static MCI_PLAY_PARMS parms;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_NOTIFY, (DWORD_PTR)&parms);
			lpszCmd = "";
			return 0;
		}
	}
	return err;
}

MMRESULT WINAPI wgmus_auxGetDevCapsA(UINT_PTR uintptrDeviceID, LPAUXCAPSA lpCapsa, UINT cbCaps)
{
	dprintf("	wgmus_auxGetDevCapsA(uintptrDeviceID=%08X, lpCapsa=%p, cbCaps=%08X\n", uintptrDeviceID, lpCapsa, cbCaps);

	lpCapsa->wMid = 2 /*MM_CREATIVE*/;
	lpCapsa->wPid = 401 /*MM_CREATIVE_AUX_CD*/;
	lpCapsa->vDriverVersion = 1;
	strcpy(lpCapsa->szPname, "wgmus virtual CD");
	lpCapsa->wTechnology = AUXCAPS_CDAUDIO;
	lpCapsa->dwSupport = AUXCAPS_VOLUME;

	return MMSYSERR_NOERROR;
}

MMRESULT WINAPI wgmus_auxGetVolume(UINT uintDeviceID, LPDWORD lpdwVolume)
{
	dprintf("	wgmus_auxGetVolume(uintDeviceID=%08X, lpdwVolume=%p)\r\n", uintDeviceID, lpdwVolume);
	
	return MMSYSERR_NOERROR;
}


MMRESULT WINAPI wgmus_auxSetVolume(UINT uintDeviceID, DWORD dwVolume)
{
	dprintf("	wgmus_auxSetVolume(uintDeviceId=%08X, dwVolume=%08X)\r\n", uintDeviceID, dwVolume);
	
	static DWORD oldVolume = -1;
	DWORD finalVolume;
	float wasapiVolume;
	WORD leftChannel;
	WORD left;
	WORD right;
	
    left = dwVolume & 0xffff;
    right = (dwVolume >> 16) & 0xffff;

    dprintf("    Set Left Speaker value at: %08X\r\n", left);
    dprintf("    Set Right Speaker value at: %08X\r\n", right);
	
	dprintf("	Set aux volume at: %08X\r\n", dwVolume);
	
	finalVolume = left / 6.554;
	dprintf("	BASS stream volume set at: %d\r\n", finalVolume);
	WasapiVolumeConfig(finalVolume);


    return MMSYSERR_NOERROR;
}