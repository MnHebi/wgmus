#include <windows.h>
#include <winreg.h>
#include <stdio.h>
#include <ctype.h>
#include <dirent.h>
#include <string.h>

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
HANDLE findTracks = INVALID_HANDLE_VALUE;
WIN32_FIND_DATA MusicFiles;

/* defines for storing music data */

struct track_info
{
    char path[MAX_PATH];    /* full path to track */
	int current;
	int next;
	int first;
	int last;	
};

static struct track_info tracks[MAX_TRACKS];

int numTracks = 0;

/* CONFIG FILE DEFINES END */

/* WAVEOUT DEFINES START */

WAVEFORMATEX waveformat;

/* WAVEOUT DEFINES END */

/* BASS PLAYER DEFINES START 
HWND win;

HSTREAM *strs;
int strc;
HMUSIC *mods;
int modc;
HSAMPLE *sams;
int samc;

 BASS PLAYER DEFINES END */

BOOL FileExists(LPCTSTR szPath)
{
  DWORD dwAttrib = GetFileAttributes(szPath);

  return (dwAttrib != INVALID_FILE_ATTRIBUTES && 
         !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}
 
/* Get audio settings from <exe dir>/wgmmus.ini, set in global variables */
void mmusi_config()
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
	
	if(FileExists(ConfigFileNameFullPath)) { dprintf("Reading audio settings from: %s\r\n", ConfigFileNameFullPath); }
	else { dprintf("Audio settings file %s does not exist.\r\n", ConfigFileNameFullPath); }
	
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
	findTracks = FindFirstFileA(MusicFileFullPath, &MusicFiles);
	do
	{
		numTracks++;
		dprintf("	Number of tracks is: %d\r\n", numTracks);
		dprintf("	Music track being read is: %s\r\n", MusicFiles.cFileName);
		for (int i = 1; i < 18; i++)
		{
			snprintf(tracks[i].path, sizeof tracks[i].path, MusicFiles.cFileName, MusicFolderFullPath, i);
			dprintf("	Music track being stored in track info is: %s\r\n", tracks[i].path);
		}
	} while (FindNextFileA(findTracks, &MusicFiles) != 0);
	FindClose(findTracks);

	return;
}

int mmusi_main()
{
	mmusi_config();
	return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		fh = fopen("mmusi.log", "w"); /* Renamed to .log*/

		GetModuleFileName(hinstDLL, musdll_path, sizeof musdll_path);
		dprintf("	dll attached\r\n");
		dprintf("	musdll_path = %s\r\n", musdll_path);

		InitializeCriticalSection(&cs);
		mmusi_config();
	}

	if (fdwReason == DLL_PROCESS_DETACH)
	{
        if (fh)
        {
            fclose(fh);
            fh = NULL;
        }
    }

    return TRUE;
}

MCIERROR WINAPI mmusi_mciSendCommandA(MCIDEVICEID deviceID, UINT uintMsg, DWORD_PTR dwptrCmd, DWORD_PTR dwParam)
{
	MCIERROR cmdErr;
	if(TRUE)
	{
		dprintf("mciSendCommandA(deviceID=%p, uintMsg=%p, dwptrCmd=%p, dwParam=%p)\r\n", deviceID, uintMsg, dwptrCmd, dwParam);

		if (uintMsg == MCI_OPEN)
		{
			dprintf("  MCI_OPEN\r\n");
			return 0;
		}
		else
		if (uintMsg == MCI_PAUSE)
		{
			dprintf("  MCI_PAUSE\r\n");
			return 0;
		}
		else
		if (uintMsg == MCI_STOP)
		{
			dprintf("  MCI_STOP\r\n");
			return 0;
		}
		else
		if (uintMsg == MCI_CLOSE)
		{
			dprintf("  MCI_CLOSE\r\n");
			return 0;
		}
		else
		if (uintMsg == MCI_STATUS)
		{
			LPMCI_STATUS_PARMS parms = (LPVOID)dwParam;

			dprintf("  MCI_STATUS\r\n");

			parms->dwReturn = 0;
			
			if (parms->dwItem == MCI_STATUS_NUMBER_OF_TRACKS)
			{
				dprintf("      MCI_STATUS_NUMBER_OF_TRACKS %d\r\n, numTracks");
				parms->dwReturn = numTracks;
			}
			else
			if (parms->dwItem == MCI_CDA_STATUS_TYPE_TRACK)
			{
				dprintf("      MCI_CDA_STATUS_TYPE_TRACK\r\n");
				if((parms->dwTrack > 0) &&  (parms->dwTrack , MAX_TRACKS))
				{
					parms->dwReturn = MCI_CDA_TRACK_AUDIO;
				}
			}
			return 0;
		}
	}
	return cmdErr;
}

MCIERROR WINAPI mmusi_mciSendStringA(LPCTSTR lpszCmd, LPTSTR lpszRetStr, UINT cchReturn, HANDLE  hwndCallback)
{
	MCIERROR err;
	if(TRUE) 
	{
		dprintf("[MCI String = %s]\n", lpszCmd);
		
		for (int i = 0; lpszCmd[i]; i++)
		{
			tolower(lpszCmd[i]);
		}
		
		if (strstr(lpszCmd, "open cdaudio"))
		{
			mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_OPEN, 0, (DWORD_PTR)NULL);
			dprintf("	OPEN COMMAND SENT\r\n");
			return 0;
		}
		if (strstr(lpszCmd, "pause cdaudio"))
		{
			mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_PAUSE, 0, (DWORD_PTR)NULL);
			dprintf("	PAUSE COMMAND SENT\r\n");
			return 0;
		}
		if (strstr(lpszCmd, "stop cdaudio"))
		{
			mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_STOP, 0, (DWORD_PTR)NULL);
			dprintf("	STOP COMMAND SENT\r\n");
			return 0;
		}
		if (strstr(lpszCmd, "close cdaudio"))
		{
			mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_CLOSE, 0, (DWORD_PTR)NULL);
			dprintf("	CLOSE COMMAND SENT\r\n");
			return 0;
		}
		if (strstr(lpszCmd, "set cdaudio time format milliseconds"))
		{
			static MCI_SET_PARMS parms;
			parms.dwTimeFormat = MCI_FORMAT_MILLISECONDS;
			mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
			dprintf("	TIME FORMAT MILLISECONDS COMMAND SENT\r\n");
			return 0;
		}
		if (strstr(lpszCmd, "set cdaudio time format tmsf"))
		{
			static MCI_SET_PARMS parms;
			parms.dwTimeFormat = MCI_FORMAT_TMSF;
			mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
			dprintf("	TIME FORMAT TMSF COMMAND SENT\r\n");
			return 0;
		}
		if (strstr(lpszCmd, "status cdaudio number of tracks"))
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_NUMBER_OF_TRACKS;
			mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&parms);
			dprintf("	NUMBER OF TRACKS STATUS COMMAND SENT\r\n");
			return 0;
		}
		if (strstr(lpszCmd, "status cdaudio type track 1"))
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_CDA_STATUS_TYPE_TRACK;
			parms.dwTrack = 1;
			mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
			dprintf("	STATUS TYPE TRACK 1 COMMAND SENT\r\n");
			return 0;
		}
	}
	return err;
}

UINT WINAPI mmusi_auxGetNumDevs()
{
	return 1;
}

MMRESULT WINAPI mmusi_auxGetDevCapsA(UINT_PTR uintptrDeviceID, LPAUXCAPSA lpCapsa, UINT cbCaps)
{
	dprintf("mmusi_auxGetDevCapsA(uintptrDeviceID=%08X, lpCapsa=%p, cbCaps=%08X\n", uintptrDeviceID, lpCapsa, cbCaps);

	lpCapsa->wMid = 2 /*MM_CREATIVE*/;
	lpCapsa->wPid = 401 /*MM_CREATIVE_AUX_CD*/;
	lpCapsa->vDriverVersion = 1;
	strcpy(lpCapsa->szPname, "mmusi virtual CD");
	lpCapsa->wTechnology = AUXCAPS_CDAUDIO;
	lpCapsa->dwSupport = AUXCAPS_VOLUME;

	return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mmusi_auxGetVolume(UINT uintDeviceID, LPDWORD lpdwVolume)
{
	dprintf("mmusi_auxGetVolume(uintDeviceID=%08X, lpdwVolume=%p)\r\n", uintDeviceID, lpdwVolume);
	*lpdwVolume = 0x00000000;
	return MMSYSERR_NOERROR;
}


MMRESULT WINAPI mmusi_auxSetVolume(UINT uintDeviceID, DWORD dwVolume)
{
}


BOOL WINAPI mmusi_PlaySoundA(LPCTSTR lpctstrSound, HMODULE hmod, DWORD dwSound)
{
}


UINT WINAPI mmusi_waveOutGetNumDevs()
{
	return 1;
}

MMRESULT WINAPI mmusi_waveOutGetVolume(HWAVEOUT hwo, LPDWORD lpdwVolume)
{
}


MMRESULT WINAPI mmusi_waveOutSetVolume(HWAVEOUT hwo, DWORD dwVolume)
{
}