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
#include <ctype.h>
#include <dirent.h>
#include <string.h>


/* AUDIO LIBRARY INCLUDES START */

#include <bass/bass.h>
#include <bass/basscd.h>

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

int AudioLibrary;
int FileFormat;
int PlaybackMode;
char MusicFolder[255];
char strFileFormat[5];
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

/* CONFIG FILE DEFINES END */

/* BASS PLAYER DEFINES START */
HWND win;

HSTREAM *strs;
int strc;
HMUSIC *mods;
int modc;
HSAMPLE *sams;
int samc;

HSTREAM str;
BASS_CHANNELINFO cinfo;

/* BASS PLAYER DEFINES END */

/* AUDIO PLAYBACK DEFINES START */

int opened = 0;
int paused = 1;
int stopped = 1;
int closed = 1;
int playing = 0;
int playeractive = 0;
int timeFormat = MCI_FORMAT_MILLISECONDS;

/* AUDIO PLAYBACK DEFINES END */

 
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
	
	AudioLibrary = GetPrivateProfileInt("Settings", "AudioLibrary", 0, ConfigFileNameFullPath);
	FileFormat = GetPrivateProfileInt("Settings", "FileFormat", 0, ConfigFileNameFullPath);
	PlaybackMode = GetPrivateProfileInt("Settings", "PlaybackMode", 0, ConfigFileNameFullPath);
	GetPrivateProfileString("Settings", "MusicFolder", "tamus", MusicFolder, MAX_PATH, ConfigFileNameFullPath);
	dprintf("	AudioLibrary = %d\r\n", AudioLibrary);
	dprintf("	FileFormat = %d\r\n", FileFormat);
	dprintf("	PlaybackMode = %d\r\n", PlaybackMode);
	dprintf("	MusicFolder = %s\r\n", MusicFolder);
	
	if(FileFormat == 0)
	{
		strcpy(strFileFormat, ".wav");
		dprintf("	File Format is wav\r\n");
	}
	else
	if(FileFormat == 1)
	{
		strcpy(strFileFormat, ".mp3");
		dprintf("	File Format is mp3\r\n");
	}
	else
	if(FileFormat == 2)
	{
		strcpy(strFileFormat, ".ogg");
		dprintf("	File Format is ogg\r\n");
	}
	else
	if(FileFormat == 3)
	{
		strcpy(strFileFormat, ".flac");
		dprintf("	File Format is FLAC\r\n");
	}
	else
	if(FileFormat == 4)
	{
		strcpy(strFileFormat, ".aiff");
		dprintf("	File Format is AIFF\r\n");
	}

	strcpy(MusicFolderFullPath, musdll_path);
	*(strrchr(MusicFolderFullPath, '\\')+1)=0;
	strcat(MusicFolderFullPath, MusicFolder);
	dprintf("	Reading music files from: %s\r\n", MusicFolderFullPath);
	strcpy(MusicFileFullPath, MusicFolderFullPath);
	strcat(MusicFileFullPath, "\\");
	dprintf("	Music folder is: %s\r\n", MusicFileFullPath);
	strcpy(strMusicFile, "*");
	strcat(strMusicFile, strFileFormat);
	strcat(MusicFileFullPath, strMusicFile);
	if (PlaybackMode == 0)
	{
		if (AudioLibrary == 5)
		{
			cdTracks = BASS_CD_GetTracks(0);
			dprintf("	Number of tracks on CD is: %d\r\n", cdTracks);
		}
	}
	else
	if (PlaybackMode == 1)
	{
		findTracks = FindFirstFileA(MusicFileFullPath, &MusicFiles);
		int i = 2;
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
		if (numTracks > 0)
		{
			firstTrack = 1;
			lastTrack = numTracks;
			currentTrack = 1;
			if (numTracks > 1)
			{
				nextTrack = 2;
			}
			else
			nextTrack = 1;
			dprintf("	Assigned First, Last, Current, and Next tracks\r\n");
			dprintf("	First track %d\r\n", firstTrack);
			dprintf("	Last track %d\r\n", lastTrack);
			dprintf("	Current track %d\r\n", currentTrack);
			dprintf("	Next track %d\r\n", nextTrack);
		}
	}
	
	return;
}

int bass_pause()
{
	return 0;
}

int bass_stop()
{
	return 0;
}

int bass_resume()
{
	return 0;
}

int bass_play(const char *path)
{
	BASS_ChannelFree(str);
	UINT deviceID = 0;
	BASS_GetConfig(BASS_CONFIG_GVOL_STREAM);
	if (PlaybackMode == 0)
	{
		str = BASS_CD_StreamCreate(0, currentTrack, BASS_SAMPLE_LOOP | BASS_STREAM_PRESCAN);
		if (str) 
		{
			strc++;
			strs = (HSTREAM*)realloc((void*)strs, strc * sizeof(*strs));
			strs[strc - 1] = str;
			dprintf("	BASS_CD_StreamCreate\r\n");
			BASS_GetConfig(BASS_CONFIG_GVOL_STREAM);
		}
		else
		dprintf("	BASS cannot stream the CD!\r\n");
	}
	if (PlaybackMode == 1)
	{
		str = BASS_StreamCreateFile(FALSE, tracks[currentTrack].path, 0, 0, BASS_SAMPLE_LOOP | BASS_STREAM_PRESCAN);
		if (str) 
		{
			strc++;
			strs = (HSTREAM*)realloc((void*)strs, strc * sizeof(*strs));
			strs[strc - 1] = str;
			dprintf("	BASS_StreamCreateFile\r\n");
			BASS_GetConfig(BASS_CONFIG_GVOL_STREAM);
		}
		else
		dprintf("	BASS cannot stream the file!\r\n");
	}

	bool strPlay = BASS_ChannelPlay(str, FALSE);
	DWORD bassDeviceCheck = BASS_ChannelGetDevice(str);
	int bassError = BASS_ErrorGetCode();
	dprintf("	BASS device: %d\r\n", bassDeviceCheck);
	
	DWORD bassActivity = BASS_ChannelIsActive(str);
	if (bassActivity == BASS_ACTIVE_STOPPED)
	{
		if (bassError == 0)
		{
			if (notify == 1)
			{
				dprintf("	BASS finished playback");
				SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, 0xBEEF);
				notify = 0;
				playing = 0;
			}
		}
		else
		dprintf("	BASS Error: %d\r\n", bassError);
	}
	if (bassActivity == BASS_ACTIVE_PLAYING)
	{
		dprintf("	BASS playing\r\n");
		BASS_GetConfig(BASS_CONFIG_GVOL_STREAM);
	}
	if (bassActivity == BASS_ACTIVE_PAUSED)
	{
		dprintf("	BASS activity was paused during playback\r\n");
		BASS_Start();
	}
	if (bassActivity == BASS_ACTIVE_PAUSED_DEVICE)
	{
		dprintf("	BASS Device was paused during playback\r\n");
		BASS_Start();
	}
	if (bassActivity == BASS_ACTIVE_STALLED)
	{
		dprintf("	BASS playback was stalled\r\n");
		/*BASS_StreamPutFileData(str, tracks[nextTrack].path, BASS_FILEDATA_END);*/
	}


	

	return 0;
}

int bass_queue(const char *path)
{
	return 0;
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
		if (AudioLibrary == 2)
		{
			mciSendCommandA(MAGIC_DEVICEID, MCI_CLOSE, 0, (DWORD_PTR)NULL);
		}
		if (AudioLibrary == 5)
		{
			BASS_Free();
		}
        if (fh)
        {
            fclose(fh);
            fh = NULL;
        }
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
				if(opened == 0)
				{
					opened = 1;
					closed = 0;
					if(AudioLibrary == 5)
					{				
						if (playeractive == 0)
						{
							dprintf("	Audio library for commands is: BASS\r\n");	
							BASS_Init(1, 44100, 0, win, NULL);
							/*BASS_SetDevice(MAGIC_DEVICEID);*/
							playeractive = 1;
							dprintf("	BASS_Init\r\n");
							dprintf("	BASS Device Number is: %d\r\n", BASS_GetDevice());
							str = BASS_StreamCreate(44100, 2, 0, STREAMPROC_DEVICE, 0);
							
							DWORD dataBuffer;
							DWORD bufferSize = sizeof(dataBuffer);
							DWORD dwVolume;
							DWORD finalVolume = 0;
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
							BASS_SetConfig(BASS_CONFIG_GVOL_STREAM, finalVolume);
							BASS_GetConfig(BASS_CONFIG_GVOL_STREAM);
						}
						else
						if (playeractive == 1)
						{
							dprintf("	BASS already initialized, doing nothing\r\n");
							BASS_GetConfig(BASS_CONFIG_GVOL_STREAM);
						}
					}
				}
				if (paused == 1)
				{
					BASS_ChannelPlay(str, FALSE);
				}
				return 0;
			}
			else
			if (uintMsg == MCI_PAUSE)
			{
				dprintf("  MCI_PAUSE\r\n");
				if(paused == 0)
				{
					paused = 1;
					playing = 0;
					if (AudioLibrary == 5)
					{
						BASS_ChannelPause(str);
						dprintf("	BASS_ChannelPause\r\n");
						BASS_GetConfig(BASS_CONFIG_GVOL_STREAM);
					}
				}
				return 0;
			}
			else
			if (uintMsg == MCI_STOP)
			{
				if (paused == 0)
				{
					dprintf("  MCI_STOP\r\n");
					if(stopped == 0)
					{
						stopped = 1;
						playing = 0;
						if (AudioLibrary == 5)
						{
							BASS_ChannelStop(str);
							dprintf("	BASS_ChannelStop\r\n");
							BASS_GetConfig(BASS_CONFIG_GVOL_STREAM);
						}
					}
				}
				return 0;
			}
			else
			if (uintMsg == MCI_CLOSE)
			{
				dprintf("  MCI_CLOSE\r\n");
				if(closed == 0)
				{
					closed = 1;
					opened = 0;
					dprintf("	Ignoring close command since TA will still send commands after it, potentially causing freezes\r\n");
				}
				return 0;
			}
			else
			if (uintMsg == MCI_STATUS)
			{
				LPMCI_STATUS_PARMS parms = (LPVOID)dwParam;

				dprintf("  MCI_STATUS\r\n");

				parms->dwReturn = 0;
				
				if (PlaybackMode == 0)
				{
					if (parms->dwItem == MCI_STATUS_NUMBER_OF_TRACKS)
					{
						dprintf("      MCI_STATUS_NUMBER_OF_TRACKS %d\r\n", cdTracks);
						parms->dwReturn = cdTracks;
					}
					else
					if (parms->dwItem == MCI_CDA_STATUS_TYPE_TRACK)
					{
						dprintf("      MCI_CDA_STATUS_TYPE_TRACK\r\n");
						if((parms->dwTrack > 0) &&  (parms->dwTrack <= MAX_TRACKS))
						{
							if (AudioLibrary == 5)
							{
								parms->dwTrack -= 1;
								DWORD bassCdTrackLength = BASS_CD_GetTrackLength(0, parms->dwTrack);
								if (bassCdTrackLength > 0)
								{
									parms->dwReturn = MCI_CDA_TRACK_AUDIO;
									dprintf("      MCI_CDA_TRACK_AUDIO\r\n");
								}
								else
								if (bassCdTrackLength < 0)
								{
									parms->dwReturn = MCI_CDA_TRACK_OTHER;
									dprintf("      MCI_CDA_TRACK_OTHER\r\n");
								}
							}
						}
					}
					else
					if (parms->dwItem == MCI_STATUS_CURRENT_TRACK)
					{
						parms->dwReturn = currentTrack + 1;
						dprintf("	Sending current track: %d\r\n", currentTrack);
					}
					else
					if (parms->dwItem == MCI_STATUS_POSITION)
					{
						char trackNumber[3];
						char trackSeconds[3];
						char trackMilliseconds[3];
						if (AudioLibrary == 5)
						{
							QWORD bassLengthInSeconds = BASS_ChannelBytes2Seconds(str, BASS_ChannelGetLength(str, BASS_POS_BYTE));
							dprintf("	BASS Length in seconds: %d\r\n", bassLengthInSeconds);
							QWORD bassPosInSeconds = BASS_ChannelBytes2Seconds(str, BASS_ChannelGetPosition(str, BASS_POS_BYTE));
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
								}
								else
								if(timeFormat == MCI_FORMAT_TMSF)
								{
									queriedCdTrack += 1;
									parms->dwReturn = MCI_MAKE_TMSF(queriedCdTrack, 0, 0, 0);
								}
							}
							else
							if(timeFormat == MCI_FORMAT_MILLISECONDS)
							{
								parms->dwReturn = currentTrack + 1;
							}
							else
							if(timeFormat == MCI_FORMAT_TMSF)
							{
								snprintf(trackNumber, 3, "%02d", currentTrack + 1);
								snprintf(trackMilliseconds, 3, "%02d", bassMilliseconds);
								snprintf(trackSeconds, 3, "%02d", bassMinutes);
								parms->dwReturn = MCI_MAKE_TMSF(trackNumber, trackSeconds, trackMilliseconds, 0);
							}
						}
					}
					if (parms->dwItem == MCI_STATUS_MODE)
					{
						dprintf("      MCI_STATUS_MODE\r\n");
						if(opened && !(playing))
						{
							dprintf("        we are open\r\n");
							parms->dwReturn = MCI_MODE_OPEN;
						}
						else
						if(paused)
						{
							dprintf("        we are paused\r\n");
							parms->dwReturn = MCI_MODE_PAUSE;
						}
						else
						if(stopped)
						{
							dprintf("        we are stopped\r\n");
							parms->dwReturn = MCI_MODE_STOP;
						}
						else
						if(playing)
						{
							dprintf("        we are playing\r\n");
							parms->dwReturn = MCI_MODE_PLAY;
						}
					}
					return 0;
				}
				else
				if (PlaybackMode == 1)
				{
					if (parms->dwItem == MCI_STATUS_NUMBER_OF_TRACKS)
					{
						dprintf("      MCI_STATUS_NUMBER_OF_TRACKS %d\r\n", numTracks);
						parms->dwReturn = numTracks;
					}
					else
					if (parms->dwItem == MCI_CDA_STATUS_TYPE_TRACK)
					{
						dprintf("      MCI_CDA_STATUS_TYPE_TRACK\r\n");
						if((parms->dwTrack > 0) &&  (parms->dwTrack < MAX_TRACKS))
						{
							parms->dwReturn = MCI_CDA_TRACK_AUDIO;
						}
					}
					else
					if (parms->dwItem == MCI_STATUS_CURRENT_TRACK)
					{
						dprintf("	Sending current track: %d\r\n", currentTrack);
						parms->dwReturn = currentTrack;
					}
					else
					if (parms->dwItem == MCI_STATUS_POSITION)
					{
						char trackNumber[3];
						char trackSeconds[3];
						char trackMilliseconds[3];
						if (AudioLibrary == 5)
						{
							QWORD bassLengthInSeconds = BASS_ChannelBytes2Seconds(str, BASS_ChannelGetLength(str, BASS_POS_BYTE));
							dprintf("	BASS Length in seconds: %d\r\n", bassLengthInSeconds);
							QWORD bassPosInSeconds = BASS_ChannelBytes2Seconds(str, BASS_ChannelGetPosition(str, BASS_POS_BYTE));
							dprintf("	BASS Position in seconds: %d\r\n", bassPosInSeconds);
							int bassMilliseconds = (bassLengthInSeconds - bassPosInSeconds) * 1000;
							int bassSeconds = bassLengthInSeconds - bassPosInSeconds;
							int bassMinutes = (bassLengthInSeconds - bassPosInSeconds) / 60;
							if (dwptrCmd & MCI_TRACK)
							{
								queriedTrack = (int)(parms->dwTrack);
								if(timeFormat == MCI_FORMAT_MILLISECONDS)
								{
									parms->dwReturn = queriedTrack;
								}
								else
								if(timeFormat == MCI_FORMAT_TMSF)
								{
									parms->dwReturn = MCI_MAKE_TMSF(queriedTrack, 0, 0, 0);
								}
							}
						
							if(timeFormat == MCI_FORMAT_MILLISECONDS)
							{
								parms->dwReturn = currentTrack;
							}
							else
							if(timeFormat == MCI_FORMAT_TMSF)
							{
								snprintf(trackNumber, 3, "%02d", currentTrack);
								snprintf(trackMilliseconds, 3, "%02d", bassMilliseconds);
								snprintf(trackSeconds, 3, "%02d", bassMinutes);
								parms->dwReturn = MCI_MAKE_TMSF(trackNumber, trackSeconds, trackMilliseconds, 0);
							}
						}
					}
					if (parms->dwItem == MCI_STATUS_MODE)
					{
						dprintf("      MCI_STATUS_MODE\r\n");
						if(opened && !(playing))
						{
							dprintf("        we are open\r\n");
							parms->dwReturn = MCI_MODE_OPEN;
						}
						else
						if(paused)
						{
							dprintf("        we are paused\r\n");
							parms->dwReturn = MCI_MODE_PAUSE;
						}
						else
						if(stopped)
						{
							dprintf("        we are stopped\r\n");
							parms->dwReturn = MCI_MODE_STOP;
						}
						else
						if(playing)
						{
							dprintf("        we are playing\r\n");
							parms->dwReturn = MCI_MODE_PLAY;
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
					}
					else
					if (parms->dwTimeFormat == MCI_FORMAT_TMSF)
					{
						dprintf("      MCI_FORMAT_TMSF\r\n");
					}
				}
				return 0;
			}
			else
			if (uintMsg == MCI_PLAY)
			{
				dprintf("  MCI_PLAY\r\n");
			
				LPMCI_PLAY_PARMS parms = (LPVOID)dwParam;
				if (paused == 1)
				{
					paused = 0;
					if(playing == 1)
					{
						if (AudioLibrary == 5)
						{
							BASS_Start();
							dprintf("	BASS_Start from paused\r\n");
							BASS_GetConfig(BASS_CONFIG_GVOL_STREAM);
						}
					}
					if(playing == 0)
					{
						if(timeFormat == MCI_FORMAT_MILLISECONDS)
						{
							if (dwptrCmd & MCI_FROM && (dwptrCmd & MCI_TO && (dwptrCmd && MCI_NOTIFY)))
							{
								dprintf("  MCI_FROM\r\n");
								dprintf("  MCI_TO\r\n");
								dprintf("  MCI_NOTIFY\r\n");
								if (AudioLibrary == 5)
								{
									dprintf("	BASS_ChannelPlay from paused\r\n");
									playing = 1;
									stopped = 0;
									closed = 0;
									paused = 0;
									notify = 1;
									BASS_ChannelPlay(str, FALSE);
									dwptrCmd = 0;
									BASS_GetConfig(BASS_CONFIG_GVOL_STREAM);
								}
							}							
						}
					}
				}
				else
				if (stopped == 1)
				{
					stopped = 0;
					if(playing == 1)
					{
						if (AudioLibrary == 5)
						{
							BASS_Start();
							dprintf("	BASS_Start from stopped\r\n");
							BASS_GetConfig(BASS_CONFIG_GVOL_STREAM);
						}
					}
				}
				else
				if (playing == 1)
				{
					if (paused == 1)
					{
						if (AudioLibrary == 5)
						{
							BASS_Start();
							dprintf("	BASS_Start from paused due to unknown interference\r\n");
							BASS_GetConfig(BASS_CONFIG_GVOL_STREAM);
						}
					}
					if (stopped == 1)
					{
						if (AudioLibrary == 5)
						{
							BASS_Start();
							dprintf("	BASS_Start from stopped due to unknown interference\r\n");
							BASS_GetConfig(BASS_CONFIG_GVOL_STREAM);
						}	
					}						
				}
				if (paused == 0)
				{
					if (dwptrCmd & MCI_FROM && !(dwptrCmd & MCI_TO))
					{
						dprintf("  MCI_FROM\r\n");
						if (PlaybackMode == 0)
						{
							parms->dwFrom -= 1;
						}
						currentTrack = (int)(parms->dwFrom);
						dprintf("	From value: %d\r\n", parms->dwFrom);
						dprintf("	Current track int value is: %d\r\n", currentTrack);
						if (AudioLibrary == 5)
						{
							playing = 1;
							stopped = 0;
							paused = 0;
							closed = 0;
							bass_play(tracks[currentTrack].path);
						}
					}
					else
					if (dwptrCmd & MCI_TO && !(dwptrCmd & MCI_FROM))
					{
						dprintf("  MCI_TO\r\n");
						nextTrack = (int)(parms->dwTo);
						dprintf("	Next track is: %s\r\n", tracks[nextTrack].path);
						if (playing == 1)
						{
							
						}
					}
					else
					if (dwptrCmd & MCI_FROM && (dwptrCmd & MCI_TO && (dwptrCmd && MCI_NOTIFY)))
					{
						dprintf("  MCI_FROM\r\n");
						dprintf("  MCI_TO\r\n");
						dprintf("  MCI_NOTIFY\r\n");
						if (PlaybackMode == 0)
						{
							parms->dwFrom -= 1;
						}
						currentTrack = (int)(parms->dwFrom);
						nextTrack = (int)(parms->dwTo);
						dprintf("	From value: %d\r\n", parms->dwFrom);
						dprintf("	Current track int value is: %d\r\n", currentTrack);
						if (AudioLibrary == 5)
						{
							playing = 1;
							stopped = 0;
							paused = 0;
							closed = 0;
							notify = 1;
							bass_play(tracks[currentTrack].path);
						}
					}
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
		dprintf("[MCI String = %s]\n", lpszCmd);
		
		for (int i = 0; lpszCmd[i]; i++)
		{
			tolower(lpszCmd[i]);
		}
		
		int cTrack = 0;
		
		if (strstr(lpszCmd, "open cdaudio"))
		{
			static MCI_WAVE_OPEN_PARMS waveParms;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_OPEN, 0, (DWORD_PTR)NULL);
			return 0;
		}
		if (strstr(lpszCmd, "pause cdaudio"))
		{
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_PAUSE, 0, (DWORD_PTR)NULL);
			return 0;
		}
		if (strstr(lpszCmd, "stop cdaudio"))
		{
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STOP, 0, (DWORD_PTR)NULL);
			return 0;
		}
		if (strstr(lpszCmd, "close cdaudio"))
		{
			wgmus_mciSendCommandA(0, MCI_CLOSE, 0, (DWORD_PTR)NULL);
			return 0;
		}
		if (strstr(lpszCmd, "set cdaudio time format milliseconds"))
		{
			static MCI_SET_PARMS parms;
			parms.dwTimeFormat = MCI_FORMAT_MILLISECONDS;	
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
			return 0;
		}
		if (strstr(lpszCmd, "set cdaudio time format tmsf"))
		{
			static MCI_SET_PARMS parms;
			parms.dwTimeFormat = MCI_FORMAT_TMSF;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
			return 0;
		}
		if (strstr(lpszCmd, "status cdaudio number of tracks"))
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_NUMBER_OF_TRACKS;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&parms);
			sprintf(lpszRetStr, "%d", numTracks);
			return 0;
		}
		if (sscanf(lpszCmd, "status cdaudio type track %d", &cTrack))
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_CDA_STATUS_TYPE_TRACK;
			parms.dwTrack = cTrack;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
			sprintf(lpszRetStr, "%d", parms.dwReturn);
			return 0;
		}
		if (strstr(lpszCmd, "status cdaudio mode"))
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_MODE;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_STATUS_MODE, (DWORD_PTR)&parms);
			return 0;
		}
		if (strstr(lpszCmd, "status cdaudio current track"))
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_CURRENT_TRACK;
			parms.dwTrack = currentTrack;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
			sprintf(lpszRetStr, "%d", parms.dwReturn);	
			return 0;
		}
		if (strstr(lpszCmd, "position"))
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_POSITION;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&parms);
			sprintf(lpszRetStr, "%d", parms.dwReturn);
			return 0;
        }
		if (sscanf(lpszCmd, "position track %d", &cTrack))
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_POSITION;
			parms.dwTrack = cTrack;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
			sprintf(lpszRetStr, "%d", parms.dwReturn);
			return 0;
        }
		int from = -1, to = -1;
		if (sscanf(lpszCmd, "play cdaudio from %d to %d notify", &from, &to) == 2)
		{
			static MCI_PLAY_PARMS parms;
			parms.dwFrom = from;
			parms.dwTo = to;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM|MCI_TO|MCI_NOTIFY, (DWORD_PTR)&parms);
			return 0;
		}
		if (sscanf(lpszCmd, "play cdaudio from %d", &from) == 1)
		{
			static MCI_PLAY_PARMS parms;
			parms.dwFrom = from;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM, (DWORD_PTR)&parms);
			return 0;
		}
		if (sscanf(lpszCmd, "play cdaudio to %d", &to) == 1)
		{
			static MCI_PLAY_PARMS parms;
			parms.dwTo = to;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_TO, (DWORD_PTR)&parms);
			return 0;
		}
	}
	return err;
}

UINT WINAPI wgmus_auxGetNumDevs()
{
	return 1;
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
	static DWORD oldVolume = -1;
	DWORD finalVolume;
	WORD left;
	WORD right;
	
	if (dwVolume > 0)
	{
		left = dwVolume & 0xffff;
		dprintf("	Set Left Speaker value at: %d\r\n", left);
		right = (dwVolume >> 16) & 0xffff;
		dprintf("	Set Right Speaker value at: %d\r\n", right);
	}
	else
	if (dwVolume <= 0)
	{
		left = 0 & 0xffff;
		dprintf("	Set Left Speaker value at: %d\r\n", left);
		right = (0 >> 16) & 0xffff;
		dprintf("	Set Right Speaker value at: %d\r\n", right);
	}

	if (AudioLibrary == 5)
	{
		finalVolume = left / 6.554;
		dprintf("	BASS stream volume set at: %d\r\n", finalVolume);
		BASS_SetConfig(BASS_CONFIG_GVOL_STREAM, finalVolume);
		BASS_GetConfig(BASS_CONFIG_GVOL_STREAM);
	}	
	
	LPARAM vParam = MAKELPARAM(left, right);
	dwVolume = vParam;

    dprintf("	wgmus_auxSetVolume(uintDeviceId=%08X, dwVolume=%08X)\r\n", uintDeviceID, dwVolume);

    if (dwVolume == oldVolume)
    {
        return MMSYSERR_NOERROR;
    }

    return MMSYSERR_NOERROR;
}


BOOL WINAPI wgmus_PlaySoundA(LPCTSTR lpctstrSound, HMODULE hmod, DWORD dwSound)
{
	dprintf("	wgmus_PlaySoundA(lpctstrSound=%s, hmod=%p, dwSound=%08X)\r\n", lpctstrSound, hmod, dwSound);
	return 0;
}


UINT WINAPI wgmus_waveOutGetNumDevs()
{
	return 1;
}

MMRESULT WINAPI wgmus_waveOutGetVolume(HWAVEOUT hwo, LPDWORD lpdwVolume)
{
	dprintf("	wgmus_waveOutGetVolume(hwo=%08X, lpdwVolume=%p)\r\n", hwo, lpdwVolume);
	*lpdwVolume = 0x00000000;
	return MMSYSERR_NOERROR;
}


MMRESULT WINAPI wgmus_waveOutSetVolume(HWAVEOUT hwo, DWORD dwVolume)
{
	dprintf("	wgmus_waveOutSetVolume(hwo=%08X, dwVolume=%08X)\r\n", hwo, dwVolume);
	return MMSYSERR_NOERROR;
}