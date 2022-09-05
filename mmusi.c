#include <bass/bass.h>
#include <windows.h>
#include <winreg.h>
#include <stdio.h>
#include <ctype.h>
#include <dirent.h>
#include <string.h>

#define MAGIC_DEVICEID 0xBEEF
#define MAX_TRACKS 99
#define dprintf(...) if (fh) { fprintf(fh, __VA_ARGS__); fflush(NULL); }
FILE *fh = NULL;

int current  = 1;
int opened = 0;
int paused = 0;
int stopped = 0;
int closed = 0;
int notify = 0;
int playing = 0;
int notready = 1;
int firstTrack = -1;
int lastTrack = 0;
int numTracks = 1; /* +1 for data track on mixed mode cd's */
DWORD dwCurTimeFormat = -1;
int time_format = MCI_FORMAT_TMSF;
CRITICAL_SECTION cs;
char alias_s[100] = "cdaudio";

/* BASS PLAYER DEFINES START */
HWND win;

HSTREAM *strs;
int strc;
HMUSIC *mods;
int modc;
HSAMPLE *sams;
int samc;

/* BASS PLAYER DEFINES END */

struct track_info
{
    char path[MAX_PATH];    /* full path to ogg */
    unsigned int length;    /* seconds */
    unsigned int position;  /* seconds */
};

static struct track_info tracks[MAX_TRACKS];

struct play_info
{
    int from;
    int to;
	int current;
};

static struct play_info info = { -1, -1, 1 };

int AudioLibrary;
int FileFormat;
int PlaybackMode;
char MusicFolder[255];

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
	LPCSTR ConfigFileName = "wgmus.ini";

	*(strrchr(ConfigFileNameFullPath, '\\')+1)=0;
	strcat(ConfigFileNameFullPath,ConfigFileName);
	
	if(FileExists(ConfigFileNameFullPath)) { dprintf("Reading audio settings from: %s\r\n", ConfigFileNameFullPath); }
	else { dprintf("Audio settings file %s does not exist.\r\n", ConfigFileNameFullPath); }
	
	AudioLibrary = GetPrivateProfileInt("Settings", "AudioLibrary", 0, ConfigFileNameFullPath);
	FileFormat = GetPrivateProfileInt("Settings", "FileFormat", 0, ConfigFileNameFullPath);
	PlaybackMode = GetPrivateProfileInt("Settings", "PlaybackMode", 0, ConfigFileNameFullPath);
	GetPrivateProfileString("Settings", "MusicFolder", "tamus", MusicFolder, MAX_PATH, ConfigFileNameFullPath);
	
	return;
}

int mmusi_main()
{
	fh = fopen("mmusi.log", "w"); /* Renamed to .log*/
	mmusi_config();
	unsigned int position = 0;

	for (int i = 1; i < MAX_TRACKS; i++) /* "Changed: int i = 0" to "1" we can skip track00.ogg" */
	{
		snprintf(tracks[i].path, sizeof tracks[i].path, "%s\\%02d.ogg", MusicFolder, i);
		tracks[i].length = ?????(tracks[i].path);
		tracks[i].position = position;

		if (tracks[i].length < 4)
            {
                tracks[i].path[0] = '\0';
                position += 4; /* missing tracks are 4 second data tracks for us */
            }
            else
            {
                if (firstTrack == -1)
                {
                    firstTrack = i;
                }
                if(i == numTracks) numTracks -= 1; /* Take into account pure music cd's starting with track01.ogg */

                dprintf("Track %02d: %02d:%02d @ %d seconds\r\n", i, tracks[i].length / 60, tracks[i].length % 60, tracks[i].position);
                numTracks++;
                lastTrack = i;
                position += tracks[i].length;
            }
        }
	return 0;
}

/* MCI commands */
/* https://docs.microsoft.com/windows/win32/multimedia/multimedia-commands */
MCIERROR WINAPI mmusi_mciSendCommandA(MCIDEVICEID IDDevice, UINT uMsg, DWORD_PTR fdwCommand, DWORD_PTR dwParam)
{
    dprintf("mciSendCommandA(IDDevice=%p, uMsg=%p, fdwCommand=%p, dwParam=%p)\r\n", IDDevice, uMsg, &fdwCommand, &dwParam);
	
    if (fdwCommand & MCI_NOTIFY)
    {
		if(AudioLibrary == 5)
		{
			SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, 0xBEEF);
			return 0;
		}
    }
	else
    if (fdwCommand & MCI_WAIT)
    {
		if(AudioLibrary == 5)
		{
			return 0;
		}
    }
	else
    if (uMsg == MCI_OPEN)
    {
        LPMCI_OPEN_PARMS parms = (LPVOID)dwParam;

        dprintf("  MCI_OPEN\r\n");
		if(AudioLibrary == 5)
		{
			if((closed = 1) || (opened = 0))
			{
				BASS_Init(1, 44100, 0, win, NULL);
				BASS_SetDevice(MAGIC_DEVICEID);
				opened = 1;
				closed = 0;
				dprintf("	BASS_Init\r\n");
			}
		}
		return 0;
    }
	else
    if (uMsg == MCI_SET)
    {
		if(AudioLibrary == 5)
		{
			if (IDDevice == MAGIC_DEVICEID || IDDevice == 0 || IDDevice == 0xFFFFFFFF)
			{
				if (uMsg == MCI_SET)
				{
					LPMCI_SET_PARMS parms = (LPVOID)dwParam;

					dprintf("  MCI_SET\r\n");

					if (fdwCommand & MCI_SET_TIME_FORMAT)
					{
						dprintf("    MCI_SET_TIME_FORMAT\r\n");

						time_format = parms->dwTimeFormat;

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
				}
			}
			return 0;
		}		
	}
	else
    if (uMsg == MCI_CLOSE)
    {
		dprintf("  MCI_CLOSE\r\n");
		if(AudioLibrary == 5)
		{
			BASS_Free();
			opened = 0;
			closed = 1;
			dprintf("	BASS_Free\r\n");
		}
		return 0;
    }
	else
    if (uMsg == MCI_PLAY)
    {
		dprintf("  MCI_PLAY\r\n");
		
		LPMCI_PLAY_PARMS parms = (LPVOID)dwParam;
		
		if(paused == 1)
		{
			BASS_Start();
			dprintf("	BASS_Start\r\n");
			paused = 0;
		}

		if (fdwCommand & MCI_FROM)
		{
			dprintf("    dwFrom: %d\r\n", parms->dwFrom);
			if(AudioLibrary == 5)
			{
				if (time_format == MCI_FORMAT_TMSF)
				{
                    info.from = MCI_TMSF_TRACK(parms->dwFrom);

                    dprintf("      TRACK  %d\n", MCI_TMSF_TRACK(parms->dwFrom));
                    dprintf("      MINUTE %d\n", MCI_TMSF_MINUTE(parms->dwFrom));
                    dprintf("      SECOND %d\n", MCI_TMSF_SECOND(parms->dwFrom));
                    dprintf("      FRAME  %d\n", MCI_TMSF_FRAME(parms->dwFrom));
				}
                else if (time_format == MCI_FORMAT_MILLISECONDS)
                {
                    info.from = 0;

                    for (int i = 0; i < MAX_TRACKS; i++)
                    {
                        // FIXME: take closest instead of absolute
                        if (tracks[i].position == parms->dwFrom / 1000)
                        {
                            info.from = i;
                        }
                    }

                    dprintf("      mapped milliseconds to %d\n", info.from);
                }
                else
                {
                    // FIXME: not really
                    info.from = parms->dwFrom;
                }
                if (info.from < firstTrack)
				{
                    info.from = firstTrack;
				}
                if (info.from > lastTrack)
				{
                    info.from = lastTrack;
				}
                info.to = info.from;
				HSTREAM str = BASS_StreamCreateFile(FALSE, tracks[current].path, 0, 0, 0);
				dprintf("	BASS_StreamCreateFile\r\n");
				playing = 1;
			}
		}
		else
		if (fdwCommand & MCI_TO)
		{
			dprintf("    dwTo:   %d\r\n", parms->dwTo);

			if (time_format == MCI_FORMAT_TMSF)
            {
                info.to = MCI_TMSF_TRACK(parms->dwTo);

                dprintf("      TRACK  %d\n", MCI_TMSF_TRACK(parms->dwTo));
                dprintf("      MINUTE %d\n", MCI_TMSF_MINUTE(parms->dwTo));
                dprintf("      SECOND %d\n", MCI_TMSF_SECOND(parms->dwTo));
                dprintf("      FRAME  %d\n", MCI_TMSF_FRAME(parms->dwTo));
            }
            else if (time_format == MCI_FORMAT_MILLISECONDS)
            {
                info.to = info.from;

                for (int i = info.from; i < MAX_TRACKS; i ++)
                {
                    /* FIXME: use better matching */
                    if (tracks[i].position + tracks[i].length > parms->dwFrom / 1000)
                    {
                        info.to = i;
						break;
                    }
                }
                dprintf("      mapped milliseconds to %d\n", info.to);
            }
            else
			{
                info.to = parms->dwTo;

                if (info.to < info.from)
				{
                    info.to = info.from;
				}

                if (info.to > lastTrack)
				{
                    info.to = lastTrack;
				}
            }
			HSTREAM str = BASS_StreamCreateFile(FALSE, tracks[current].path, 0, 0, 0);
			dprintf("	BASS_StreamCreateFile\r\n");
			playing = 1;
        }
		return 0;
    }
	else
    if (uMsg == MCI_STOP)
    {
		dprintf("  MCI_STOP\r\n");
		if(AudioLibrary == 5)
		{
			if(stopped == 0)
			{
				BASS_Pause();
				BASS_Stop();
				stopped = 1;
				playing = 0;
				dprintf("	BASS_Stop\r\n");
			}
		}
		info.from = firstTrack; /* Reset first track */
		current  = 1; /* Reset current track*/
		return 0;
    }
	else
    if (uMsg == MCI_PAUSE)
    {
		if(AudioLibrary == 5)
		{
			if(paused == 0)
			{
				BASS_Pause();
				paused = 1;
				playing = 0;
				dprintf("	BASS_Pause\r\n");
			}	
		}
		return 0;
    }
	else
    if (uMsg == MCI_SYSINFO)
    {
		if(AudioLibrary == 5)
		{
			return 0;
		}
    }
	else
	if (uMsg == MCI_INFO)
	{
		if(AudioLibrary == 5)
		{
			return 0;
		}		
	}
	else
    if (uMsg == MCI_STATUS)
    {
		if(AudioLibrary == 5)
		{
            LPMCI_STATUS_PARMS parms = (LPVOID)dwParam;

            dprintf("  MCI_STATUS\r\n");

            parms->dwReturn = 0;

            if (fdwCommand & MCI_TRACK)
            {
                dprintf("    MCI_TRACK\r\n");
                dprintf("      dwTrack = %d\r\n", parms->dwTrack);
            }
			else
            if (fdwCommand & MCI_STATUS_ITEM)
            {
                dprintf("    MCI_STATUS_ITEM\r\n");

                if (parms->dwItem == MCI_STATUS_CURRENT_TRACK)
                {
                    dprintf("      MCI_STATUS_CURRENT_TRACK\r\n");
					int track = 0;
					track =  parms->dwTrack;
					parms->dwReturn = track;
                }
				else
                if (parms->dwItem == MCI_STATUS_LENGTH)
                {
                    dprintf("      MCI_STATUS_LENGTH\r\n");

                    /* Get track length */
                    if(fdwCommand & MCI_TRACK)
                    {
                        int seconds = tracks[parms->dwTrack].length;
                        if (time_format == MCI_FORMAT_MILLISECONDS)
                        {
                            parms->dwReturn = seconds * 1000;
                        }
                        else
                        {
                            parms->dwReturn = MCI_MAKE_MSF(seconds / 60, seconds % 60, 0);
                        }
                    }
                    /* Get full length */
                    else
                    {
                        if (time_format == MCI_FORMAT_MILLISECONDS)
                        {
                            parms->dwReturn = (tracks[lastTrack].position + tracks[lastTrack].length) * 1000;
                        }
                        else
                        {
                            parms->dwReturn = MCI_MAKE_TMSF(lastTrack, 0, 0, 0);
                        }
                    }
                }
				else
                if (parms->dwItem == MCI_CDA_STATUS_TYPE_TRACK)
                {
                    dprintf("      MCI_CDA_STATUS_TYPE_TRACK\r\n");
                    /*Fix from the Dxwnd project*/
                    /* ref. by WinQuake */
                    if((parms->dwTrack > 0) &&  (parms->dwTrack , MAX_TRACKS)){
                        if(tracks[parms->dwTrack].length > 0)
						{
                            parms->dwReturn = MCI_CDA_TRACK_AUDIO; 
						}
                    }
                }
				else
                if (parms->dwItem == MCI_STATUS_MEDIA_PRESENT)
                {
                    dprintf("      MCI_STATUS_MEDIA_PRESENT\r\n");
                    parms->dwReturn = TRUE;
                }
				else
                if (parms->dwItem == MCI_STATUS_NUMBER_OF_TRACKS)
                {
                    dprintf("      MCI_STATUS_NUMBER_OF_TRACKS\r\n");
                    parms->dwReturn = numTracks;
                }
				else
                if (parms->dwItem == MCI_STATUS_POSITION)
                {
                    /* Track position */
                    dprintf("      MCI_STATUS_POSITION\r\n");

                    if (fdwCommand & MCI_TRACK)
                    {
                        if (time_format == MCI_FORMAT_MILLISECONDS)
						{
                            /* FIXME: implying milliseconds */
                            parms->dwReturn = tracks[parms->dwTrack].position * 1000;
						}
                        else /* TMSF */
						{
                            parms->dwReturn = MCI_MAKE_TMSF(parms->dwTrack, 0, 0, 0);
						}
                    }
                    else {
                        /* Current position */
                        int track = current % 0xFF;
                        if (time_format == MCI_FORMAT_MILLISECONDS)
						{
                            parms->dwReturn = tracks[track].position * 1000;
						}
                        else /* TMSF */
						{
                            parms->dwReturn = MCI_MAKE_TMSF(track, 0, 0, 0);
						}
                    }
                }
				else
                if (parms->dwItem == MCI_STATUS_MODE)
                {
                    dprintf("      MCI_STATUS_MODE\r\n");
                    
                    if(paused)
					{ 
                        dprintf("        we are paused\r\n");
                        parms->dwReturn = MCI_MODE_PAUSE;
					}
                    else
                    if(stopped)
					{
                        dprintf("        we are paused\r\n");
                        parms->dwReturn = MCI_MODE_STOP;
					}
					else
                    if(opened)
					{
                        dprintf("        we are paused\r\n");
                        parms->dwReturn = MCI_MODE_OPEN;
					}
					else
                    if(playing)
					{ 
                        dprintf("        we are paused\r\n");
                        parms->dwReturn = MCI_MODE_PLAY;
					}
                }
				else
                if (parms->dwItem == MCI_STATUS_READY)
                {
                    dprintf("      MCI_STATUS_READY\r\n");
                    /*Fix from the Dxwnd project*/
                    /* referenced by Quake/cd_win.c */
                    parms->dwReturn = TRUE; /* TRUE=ready, FALSE=not ready */
                }
				else
                if (parms->dwItem == MCI_STATUS_TIME_FORMAT)
                {
                    dprintf("      MCI_STATUS_TIME_FORMAT\r\n");
					if(time_format == MCI_FORMAT_MILLISECONDS)
					{
						parms->dwReturn = MCI_FORMAT_MILLISECONDS;
					}
					else
					if(time_format == MCI_FORMAT_TMSF)
					{
						parms->dwReturn = MCI_FORMAT_TMSF;
					}
                }
				else
                if (parms->dwItem == MCI_STATUS_START)
                {
                    dprintf("      MCI_STATUS_START\r\n");
                }
            }
            dprintf("  dwReturn %d\n", parms->dwReturn);
			return 0;
		}
    }

    /* fallback */
    return MCIERR_UNRECOGNIZED_COMMAND;
}

/* MCI command strings */
/* https://docs.microsoft.com/windows/win32/multimedia/multimedia-command-strings */
MCIERROR WINAPI mmusi_mciSendStringA(LPCTSTR cmd, LPTSTR ret, UINT cchReturn, HANDLE hwndCallback)
{
	MCIERROR err;
	if(TRUE) {
		static char sPlayerNickName[80+1] = "";
		char sNickName[80+1];
		char sCommand[80+1];
		char sDevice[80+1];
		char *sCmdTarget;
		DWORD dwCommand;
		
		DWORD dwNewTimeFormat = -1;
		
		char cmdbuf[1024];
		char cmp_str[1024];

		if(!strcmp(sCommand, "open")) dwCommand = MCI_OPEN; else
		if(!strcmp(sCommand, "close")) dwCommand = MCI_CLOSE; else
		if(!strcmp(sCommand, "stop")) dwCommand = MCI_STOP; else
		if(!strcmp(sCommand, "pause")) dwCommand = MCI_PAUSE; else
		if(!strcmp(sCommand, "resume")) dwCommand = MCI_RESUME; else
		if(!strcmp(sCommand, "set")) dwCommand = MCI_SET; else
		if(!strcmp(sCommand, "status")) dwCommand = MCI_STATUS; else
		if(!strcmp(sCommand, "play")) dwCommand = MCI_PLAY; else
		if(!strcmp(sCommand, "seek")) dwCommand = MCI_SEEK; else
		if(!strcmp(sCommand, "capability")) dwCommand = MCI_GETDEVCAPS; else
		dwCommand = 0; 
		
		if(dwCommand && (dwCommand != MCI_OPEN)){
			// don't try to parse unknown commands, nor open command that
			// doesn't necessarily have extra arguments
			sCmdTarget = (char *)cmd;
			while (*sCmdTarget && *sCmdTarget != ' ') sCmdTarget++; // skip command
			while (*sCmdTarget && *sCmdTarget == ' ') sCmdTarget++; // skip first separator
			while (*sCmdTarget && *sCmdTarget != ' ') sCmdTarget++; // skip deviceid
			while (*sCmdTarget && *sCmdTarget == ' ') sCmdTarget++; // skip second separator
		}

		dprintf("[MCI String = %s]\n", cmd);

		/* copy cmd into cmdbuf */
		strcpy (cmdbuf,cmd);
		/* change cmdbuf into lower case */
		for (int i = 0; cmdbuf[i]; i++)
		{
			cmdbuf[i] = tolower(cmdbuf[i]);
		}

		if (strstr(cmd, "sysinfo cdaudio quantity"))
		{
			dprintf("  Returning quantity: 1\r\n");
			strcpy(ret, "1");
			return 0;
		}

		/* Example: "sysinfo cdaudio name 1 open" returns "cdaudio" or the alias.*/
		if (strstr(cmd, "sysinfo cdaudio name"))
		{
			dprintf("  Returning name: cdaudio\r\n");
			sprintf(ret, "%s", alias_s);
			return 0;
		}	

		sprintf(cmp_str, "info %s", alias_s);
		if (strstr(cmd, cmp_str))
		{
			mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_INFO, 0, (DWORD_PTR)NULL);
			return 0;
		}
		
		sprintf(cmp_str, "stop %s", alias_s);
		if (strstr(cmd, cmp_str))
		{
			mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_STOP, 0, (DWORD_PTR)NULL);
			return 0;
		}

		sprintf(cmp_str, "pause %s", alias_s);
		if (strstr(cmd, cmp_str))
		{
			mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_PAUSE, 0, (DWORD_PTR)NULL);
			return 0;
		}

		sprintf(cmp_str, "open %s", alias_s);
		if (strstr(cmd, cmp_str))
		{
			mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_OPEN, 0, (DWORD_PTR)NULL);
			return 0;
		}
		
		sprintf(cmp_str, "close %s", alias_s);
		if (strstr(cmd, cmp_str))
		{
			mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_CLOSE, 0, (DWORD_PTR)NULL);
			return 0;
		}

		/* Handle "set cdaudio/alias time format" */
		sprintf(cmp_str, "set %s", alias_s);
		if (strstr(cmd, cmp_str))
		{
			dwNewTimeFormat = -1;
			if (strstr(cmd, "time format milliseconds"))
			{
				static MCI_SET_PARMS parms;
				parms.dwTimeFormat = MCI_FORMAT_MILLISECONDS;
				mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
				dwNewTimeFormat = 1;
				return 0;
			}
			else
			if (strstr(cmd, "time format tmsf"))
			{
				static MCI_SET_PARMS parms;
				parms.dwTimeFormat = MCI_FORMAT_TMSF;
				mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
				dwNewTimeFormat = 2;
				return 0;
			}
			else
			if (strstr(cmd, "time format msf"))
			{
				static MCI_SET_PARMS parms;
				parms.dwTimeFormat = MCI_FORMAT_MSF;
				mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
				dwNewTimeFormat = 3;
				return 0;
			}
			else
			if (dwNewTimeFormat == -1)
			{
				dprintf("set time format failed\r\n");
				mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_CLOSE, 0, (DWORD_PTR)NULL);
			}
		}

		/* Handle "status cdaudio/alias" */
		sprintf(cmp_str, "status %s", alias_s);
		if (strstr(cmd, cmp_str)){
			if (strstr(cmd, "number of tracks"))
			{
				static MCI_STATUS_PARMS parms;
				parms.dwItem = MCI_STATUS_NUMBER_OF_TRACKS;
				mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_WAIT, (DWORD_PTR)&parms);
				dprintf("  Returning number of tracks (%d)\r\n", numTracks);
				sprintf(ret, "%d", numTracks);
				return 0;
			}
			int track = 0;
			if (sscanf(cmd, "status %*s length track %d", &track) == 1)
			{
				static MCI_STATUS_PARMS parms;
				parms.dwItem = MCI_STATUS_LENGTH;
				parms.dwTrack = track;
				mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK|MCI_WAIT, (DWORD_PTR)&parms);
				sprintf(ret, "%d", parms.dwReturn);
				return 0;
			}
			if (strstr(cmd, "length"))
			{
				static MCI_STATUS_PARMS parms;
				parms.dwItem = MCI_STATUS_LENGTH;
				mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&parms);
				sprintf(ret, "%d", parms.dwReturn);
				return 0;
			}
			if (sscanf(cmd, "status %*s type track %d", &track) == 1)
			{
				static MCI_STATUS_PARMS parms;
				parms.dwItem = MCI_CDA_STATUS_TYPE_TRACK;
				parms.dwTrack = track;
				mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
				sprintf(ret, "%d", parms.dwReturn);
				return 0;
			}
			if (sscanf(cmd, "status %*s position track %d", &track) == 1)
			{
				static MCI_STATUS_PARMS parms;
				parms.dwItem = MCI_STATUS_POSITION;
				parms.dwTrack = track;
				mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
				sprintf(ret, "%d", parms.dwReturn);
				return 0;
			}
			if (strstr(cmd, "position"))
			{
				static MCI_STATUS_PARMS parms;
				parms.dwItem = MCI_STATUS_POSITION;
				mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&parms);
				sprintf(ret, "%d", parms.dwReturn);
				return 0;
			}
			if (strstr(cmd, "mode"))
			{
				static MCI_STATUS_PARMS parms;
				parms.dwItem = MCI_STATUS_MODE;
				mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_STATUS_MODE, (DWORD_PTR)&parms);
				sprintf(ret, "%d", parms.dwReturn);
				return 0;
			}
			if (strstr(cmd, "current"))
			{
				static MCI_STATUS_PARMS parms;
				parms.dwItem = MCI_STATUS_CURRENT_TRACK;
				parms.dwTrack = track;
				mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
				sprintf(ret, "%d", parms.dwReturn);
				return 0;
			}
			if (strstr(cmd, "media present"))
			{
				strcpy(ret, "TRUE");
				return 0;
			}
		}

		/* Handle "play cdaudio/alias" */
		int from = -1, to = -1;
		sprintf(cmp_str, "play %s", alias_s);
		if (strstr(cmd, cmp_str))
		{
			if (sscanf(cmd, "play %*s from %d to %d notify", &from, &to) == 2)
			{
				static MCI_PLAY_PARMS parms;
				parms.dwFrom = from;
				parms.dwTo = to;
				mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM|MCI_TO|MCI_NOTIFY, (DWORD_PTR)&parms);
				return 0;
			}
			if (sscanf(cmd, "play %*s from %d", &from) == 1)
			{
				static MCI_PLAY_PARMS parms;
				parms.dwFrom = from;
				mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM, (DWORD_PTR)&parms);
				return 0;
			}
			if (sscanf(cmd, "play %*s to %d", &to) == 1)
			{
				static MCI_PLAY_PARMS parms;
				parms.dwTo = to;
				mmusi_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_TO, (DWORD_PTR)&parms);
				return 0;
			}
		}
	}
    return err;
}

UINT WINAPI mmusi_auxGetNumDevs()
{
    dprintf("mmusi_auxGetNumDevs()\r\n");
    return 1;
}

MMRESULT WINAPI mmusi_auxGetDevCapsA(UINT_PTR uDeviceID, LPAUXCAPS lpCaps, UINT cbCaps)
{

    lpCaps->wMid = 2 /*MM_CREATIVE*/;
    lpCaps->wPid = 401 /*MM_CREATIVE_AUX_CD*/;
    lpCaps->vDriverVersion = 1;
    strcpy(lpCaps->szPname, "mmusi virtual CD");
    lpCaps->wTechnology = AUXCAPS_CDAUDIO;
    lpCaps->dwSupport = AUXCAPS_VOLUME;

    return MMSYSERR_NOERROR;
}


MMRESULT WINAPI mmusi_auxGetVolume(UINT uDeviceID, LPDWORD lpdwVolume)
{
    *lpdwVolume = 0x00000000;
    return MMSYSERR_NOERROR;
}