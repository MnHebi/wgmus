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
#include "patch.h"

/* AUDIO LIBRARY INCLUDES START */

#include <bass/bass.h>
#include <bass/basscd.h>
#include <bass/bassflac.h>
#include <bass/bassmix.h>
#include <bass/basswasapi.h>

/* PROJECT LIBRARIES START */

#include "wgmus_ini_helpers.h"

/* PROJECT LIBRARIES END */

/* ---------- Forward declarations ---------- */
static int open_track_stream_table(int track, const char *path);
static void fade_in_current(DWORD ms);
static void fade_out_current(DWORD ms);
static void CALLBACK OnEnd(HSYNC h, DWORD chan, DWORD data, void *user);

void init_logger_paths(HINSTANCE hinstDLL);  // opens wgmus.log next to DLL, sets _IONBF, initializes log_cs, sets g_log_ready=1
void load_log_config(HINSTANCE hinstDLL);    // reads wgmus.ini next to DLL, sets g_log_level
void log_msg(log_level_t level, const char *fmt, ...);
/* ------------------------------------------ */

/* AUDIO LIBRARY INCLUDES END */


/* ====================== LOGGER ====================== */
//typedef enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR } log_level_t;
const char* log_level_str[] = { "DEBUG","INFO","WARN","ERROR" };
CRITICAL_SECTION log_cs;
log_level_t g_log_level = LOG_INFO;
FILE *fh = NULL;
volatile LONG g_log_ready = 0;

void log_msg(log_level_t level, const char *fmt, ...)
{
    if (!fh) return;
    if (level < g_log_level) return;

    // If logger not fully ready, do a minimal, lockless write (best effort).
    if (InterlockedCompareExchange(&g_log_ready, 1, 1) == 0) {
        va_list args;
        va_start(args, fmt);
        vfprintf(fh, fmt, args);
        va_end(args);
        fputc('\n', fh);
        fflush(fh);
        return;
    }

    EnterCriticalSection(&log_cs);

    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(fh, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    fprintf(fh, "[%s] ", log_level_str[level]);

    va_list args;
    va_start(args, fmt);
    vfprintf(fh, fmt, args);
    va_end(args);

    fputc('\n', fh);
    fflush(fh);

    LeaveCriticalSection(&log_cs);
}

static const char* bass_error_string(int code)
{
    switch(code)
	{
        case 0: return "BASS_OK (no error)";
        case -1: return "BASS_NO_ERROR_CHECKED";
        case BASS_ERROR_MEM: return "BASS_ERROR_MEM"; case BASS_ERROR_FILEOPEN: return "BASS_ERROR_FILEOPEN";
        case BASS_ERROR_DRIVER: return "BASS_ERROR_DRIVER"; case BASS_ERROR_BUFLOST: return "BASS_ERROR_BUFLOST";
        case BASS_ERROR_HANDLE: return "BASS_ERROR_HANDLE"; case BASS_ERROR_FORMAT: return "BASS_ERROR_FORMAT";
        case BASS_ERROR_POSITION: return "BASS_ERROR_POSITION"; case BASS_ERROR_INIT: return "BASS_ERROR_INIT";
        case BASS_ERROR_START: return "BASS_ERROR_START"; case BASS_ERROR_ALREADY: return "BASS_ERROR_ALREADY";
        case BASS_ERROR_NOTAUDIO: return "BASS_ERROR_NOTAUDIO"; case BASS_ERROR_NOCHAN: return "BASS_ERROR_NOCHAN";
        case BASS_ERROR_ILLTYPE: return "BASS_ERROR_ILLTYPE"; case BASS_ERROR_ILLPARAM: return "BASS_ERROR_ILLPARAM";
        case BASS_ERROR_NO3D: return "BASS_ERROR_NO3D"; case BASS_ERROR_NOEAX: return "BASS_ERROR_NOEAX";
        case BASS_ERROR_DEVICE: return "BASS_ERROR_DEVICE"; case BASS_ERROR_NOPLAY: return "BASS_ERROR_NOPLAY";
        case BASS_ERROR_FREQ: return "BASS_ERROR_FREQ"; case BASS_ERROR_NOTFILE: return "BASS_ERROR_NOTFILE";
        case BASS_ERROR_NOHW: return "BASS_ERROR_NOHW"; case BASS_ERROR_EMPTY: return "BASS_ERROR_EMPTY";
        case BASS_ERROR_NONET: return "BASS_ERROR_NONET"; case BASS_ERROR_CREATE: return "BASS_ERROR_CREATE";
        case BASS_ERROR_NOFX: return "BASS_ERROR_NOFX"; case BASS_ERROR_NOTAVAIL: return "BASS_ERROR_NOTAVAIL";
        case BASS_ERROR_DECODE: return "BASS_ERROR_DECODE"; case BASS_ERROR_DX: return "BASS_ERROR_DX";
        case BASS_ERROR_TIMEOUT: return "BASS_ERROR_TIMEOUT"; case BASS_ERROR_FILEFORM: return "BASS_ERROR_FILEFORM";
        case BASS_ERROR_SPEAKER: return "BASS_ERROR_SPEAKER"; case BASS_ERROR_VERSION: return "BASS_ERROR_VERSION";
        case BASS_ERROR_CODEC: return "BASS_ERROR_CODEC"; case BASS_ERROR_ENDED: return "BASS_ERROR_ENDED";
        case BASS_ERROR_BUSY: return "BASS_ERROR_BUSY"; default: return "Unknown BASS error";
    }
}

static int check_bass_error(const char *ctx){ int e=BASS_ErrorGetCode(); if(!e) return 0; log_msg(LOG_ERROR, "%s → %s (code=%d)", ctx, bass_error_string(e), e); return e; }

#define MAGIC_DEVICEID 0xBEEF
#define FIRST_TRACK_INDEX 2
#define MAX_TRACKS 99

CRITICAL_SECTION cs;
CRITICAL_SECTION wproc_cs;

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

QWORD seekConversion;
QWORD seekPosition;

int bassTrackLengthLeft = 0;
int bassTrackActualPos = 0;
int bassSecondsCalculate = 0;

int bassMilliseconds = 0;
int bassSeconds = 0;
int bassMinutes = 0;
int bassHours = 0;
int bassFrames = 0;
QWORD bassLengthInSeconds;
QWORD bassPosInSeconds;

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
	log_msg(LOG_DEBUG, "Wasapi volume: %.2f\n", wasapiVolume);
	
	log_msg(LOG_DEBUG, "Wasapi Volume Config wasapiVolume: %.2f\n", wasapiVolume); 
	log_msg(LOG_DEBUG, "Wasapi Volume Config streamVol: %d\n", streamVol); 
	BASS_SetConfig(BASS_CONFIG_GVOL_STREAM, streamVol);
	
	return BASS_ChannelSetAttribute(dec, BASS_ATTRIB_VOLDSP, wasapiVolume);
}

 
/* Compare two C strings "naturally" (numbers by value, not digits) */
static int natcmp_ci(const char *a, const char *b) {
    unsigned char ca, cb;
    for (;;) {
        ca = (unsigned char)*a; cb = (unsigned char)*b;
        // both digits -> compare the full number
        if (ca >= '0' && ca <= '9' && cb >= '0' && cb <= '9') {
            // skip leading zeros
            const char *pa = a, *pb = b;
            while (*pa == '0') ++pa;
            while (*pb == '0') ++pb;

            // parse numeric value (use unsigned long long to be safe)
            unsigned long long va = 0, vb = 0;
            while (*pa >= '0' && *pa <= '9') { va = va*10 + (unsigned)(*pa - '0'); ++pa; }
            while (*pb >= '0' && *pb <= '9') { vb = vb*10 + (unsigned)(*pb - '0'); ++pb; }

            if (va < vb) return -1;
            if (va > vb) return 1;

            // same numeric value: shorter digit run wins (e.g., "003" vs "0003")
            int lena = (int)(pa - a), lenb = (int)(pb - b);
            if (lena != lenb) return (lena < lenb) ? -1 : 1;

            // advance both past the number and continue
            a = pa; b = pb; 
            continue;
        }

        // case-insensitive char compare
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return (ca < cb) ? -1 : 1;

        if (ca == 0) return 0; // both ended
        ++a; ++b;
    }
}

/* Get base filename (no directory) from a full path */
static const char* basename_ptr(const char *path) {
    const char *s = strrchr(path, '\\');
    if (!s) s = strrchr(path, '/');
    return s ? (s + 1) : path;
}

/* Comparator for your tracks[] (sort by filename, naturally) */
static int cmp_track_natural(const void *pa, const void *pb) {
    const struct { char path[MAX_PATH]; } *A = pa, *B = pb;
    const char *na = basename_ptr(A->path);
    const char *nb = basename_ptr(B->path);
    return natcmp_ci(na, nb);
}

BOOL FileExists(LPCTSTR szPath)
{
  DWORD dwAttrib = GetFileAttributes(szPath);

  return (dwAttrib != INVALID_FILE_ATTRIBUTES && 
         !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}
 
/* Get audio settings from <exe dir>/wgmus.ini, set in global variables */
void wgmus_config(HINSTANCE hinstDLL)
{
    // --- get DLL directory without touching musdll_path ---
    char dllDir[MAX_PATH];
    {
        char tmp[MAX_PATH] = {0};
        GetModuleFileNameA(hinstDLL, tmp, sizeof tmp);
        char *slash = strrchr(tmp, '\\');
        if (slash) *slash = '\0';
        snprintf(dllDir, sizeof dllDir, "%s", tmp);
    }

    // --- INI path next to DLL ---
    char iniPath[MAX_PATH];
    snprintf(iniPath, sizeof iniPath, "%s\\wgmus.ini", dllDir);

    if (FileExists(iniPath)) log_msg(LOG_DEBUG, "Reading audio settings from: %s", iniPath);
    else                     log_msg(LOG_DEBUG, "Audio settings file %s does not exist.", iniPath);

    static const char *fileFormats[] = { ".wav", ".mp3", ".ogg", ".flac", ".aiff" };
    const unsigned int numFormats = (unsigned)(sizeof fileFormats / sizeof fileFormats[0]);

    FileFormat   = GetPrivateProfileIntA("Settings", "FileFormat",   0, iniPath);
    PlaybackMode = GetPrivateProfileIntA("Settings", "PlaybackMode", 0, iniPath);
    GetPrivateProfileStringA("Settings", "MusicFolder", "tamus", MusicFolder, MAX_PATH, iniPath);

    if ((unsigned)FileFormat >= numFormats) {
        log_msg(LOG_WARN, "FileFormat=%d invalid; defaulting to 0", FileFormat);
        FileFormat = 0;
    }

    log_msg(LOG_INFO, "FileFormat=%s (%d)", fileFormats[FileFormat] + 1, FileFormat);
    log_msg(LOG_INFO, "PlaybackMode=%d",     PlaybackMode);
    log_msg(LOG_INFO, "MusicFolder=%s",      MusicFolder);

    // --- make MusicFolder absolute (DLL dir + relative folder) ---
    BOOL isAbs = ((MusicFolder[0] && MusicFolder[1] == ':') ||  // "C:"
                  (MusicFolder[0] == '\\' && MusicFolder[1] == '\\')); // UNC
    if (isAbs) snprintf(MusicFolderFullPath, sizeof MusicFolderFullPath, "%s", MusicFolder);
    else       snprintf(MusicFolderFullPath, sizeof MusicFolderFullPath, "%s\\%s", dllDir, MusicFolder);
    log_msg(LOG_INFO, "Reading music files from: %s", MusicFolderFullPath);

    // --- CD vs files ---
    if (PlaybackMode == CD) {
        cdTracks = BASS_CD_GetTracks(0);
        log_msg(LOG_INFO, "Number of tracks on CD: %d", cdTracks);
        return;
    }

    if (PlaybackMode == MUSICFILE) {
        char searchMask[MAX_PATH];
        snprintf(searchMask, sizeof searchMask, "%s\\*.%s", MusicFolderFullPath, fileFormats[FileFormat] + 1);

        WIN32_FIND_DATAA ffd;
        HANDLE hFind = FindFirstFileA(searchMask, &ffd);
        int i = FIRST_TRACK_INDEX;  // e.g., 2
        numTracks = 0;

        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                char fullpath[MAX_PATH];
                snprintf(fullpath, sizeof fullpath, "%s\\%s", MusicFolderFullPath, ffd.cFileName);
                snprintf(tracks[i].path, sizeof tracks[i].path, "%s", fullpath);
                log_msg(LOG_DEBUG, "Track %d: %s", i, tracks[i].path);
                ++numTracks; ++i;
                if (i >= MAX_TRACKS) break;
            } while (FindNextFileA(hFind, &ffd));
            FindClose(hFind);
            noFiles = 0;
        }

        if (numTracks > 0) 
		{
            firstTrack   = FIRST_TRACK_INDEX;
            lastTrack    = FIRST_TRACK_INDEX + numTracks - 1;
			if (numTracks > 1)
			{
				qsort(&tracks[FIRST_TRACK_INDEX], (size_t)numTracks, sizeof(tracks[0]), cmp_track_natural);
			}
            currentTrack = FIRST_TRACK_INDEX;
            nextTrack    = (numTracks > 1) ? (FIRST_TRACK_INDEX + 1) : FIRST_TRACK_INDEX;
            log_msg(LOG_INFO, "Assigned tracks → first=%d last=%d current=%d next=%d (count=%d)",
                    firstTrack, lastTrack, currentTrack, nextTrack, numTracks);
        } 
		else 
		{
            currentTrack = nextTrack = lastTrack = 0;
            noFiles = 1;
            log_msg(LOG_WARN, "No tracks found in %s", MusicFolderFullPath);
        }
    }
}

static int open_track_stream(int track, int fileFormat, int playbackMode)
{
    HSTREAM newStream = 0;

    if (playbackMode == CD) {
        newStream = BASS_CD_StreamCreate(0, track, BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT);
    } 
    else if (playbackMode == MUSICFILE) {
        if (fileFormat == 3) { // FLAC
            newStream = BASS_FLAC_StreamCreateFile(
                FALSE, tracks[track].path, 0, 0,
                BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_STREAM_PRESCAN
            );
        } else { // WAV/MP3/OGG/AIFF
            newStream = BASS_StreamCreateFile(
                FALSE, tracks[track].path, 0, 0,
                BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_STREAM_PRESCAN
            );
        }
    }

    if (!newStream) {
        check_bass_error("Failed to create stream");
        return 1;
    }

    // Free old decoder stream if necessary
    if (dec) {
        BASS_StreamFree(dec);
    }

    dec = newStream;
    BASS_Mixer_StreamAddChannel(str, dec, 0);

    return 0;
}

void printBassError(const char *text)
{
	if(BASS_ErrorGetCode() != 0)
	{
		if(BASS_ErrorGetCode() != -1)
		{
			log_msg(LOG_DEBUG, "Error(%d): %s\n", BASS_ErrorGetCode(), text);

                        return;
		}
		else
		if(BASS_ErrorGetCode() == -1)
		{
			log_msg(LOG_DEBUG, "No errors(%d): %s\n", BASS_ErrorGetCode(), text);

                        return;
		}
	}
	return;
}

DWORD CALLBACK WasapiProc(void *buffer, DWORD length, void *user)
{
	InitializeCriticalSection(&wproc_cs);
	
	if(dec == 0)
	{
		check_bass_error("WasapiProc failed cause decoder stream was 0");
		return 1;
	}
	
	if(dec < 0)
	{
		check_bass_error("WasapiProc failed cause decoder stream was less than 0");
		return 1;
	}
	
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
		log_msg(LOG_DEBUG, "File length was 0; setting progress to 100%.\n");
		bassPlaybackProgress = 100.0; // Or idk if should be 0.0 Keeper
	}
	DWORD bassActivity = BASS_ChannelIsActive(dec);
	if (bassActivity == BASS_ACTIVE_STOPPED)
	{
		if(playState != PAUSED)
		{
			if(bassPlaybackProgress == 0)
			{
				notify = 0;
				changeNotify = 0;
				log_msg(LOG_DEBUG, "Finished playback\n");
				playState = STOPPED;
				SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, currentTrack);
				log_msg(LOG_DEBUG, "BASS no activity\n");
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
				log_msg(LOG_DEBUG, "Finished playback\n");
				playState = PLAYING;
				SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, currentTrack);
				log_msg(LOG_DEBUG, "BASS no activity\n");
				BASS_WASAPI_Stop(TRUE);
				BASS_WASAPI_Start();
				return 0;
			}
		}
	}
	
    return c;
}

static void init_wgmus(HINSTANCE hinstDLL)
{
        // 1) Open log (unbuffered), init log_cs, mark logger ready, write banner
        init_logger_paths(hinstDLL);

        // 2) Load INI from the same folder, parse numeric or named levels
        load_log_config(hinstDLL);

        // 3) Confirm final level (now that parsing is done)
        log_msg(LOG_INFO, "Log level set to: %s", log_level_str[g_log_level]);

        // 4) Your DLL/game init (keep it lightweight for DllMain)
		wgmus_config(hinstDLL);
		
		// (Optional) If you still need musdll_path for other uses, set it now:
		GetModuleFileNameA(hinstDLL, musdll_path, sizeof musdll_path);

        log_msg(LOG_INFO, "==== session start ====");
		log_msg(LOG_DEBUG, "dll attached\n");
		log_msg(LOG_DEBUG, "musdll_path = %s\n", musdll_path);
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
		log_msg(LOG_DEBUG, "BASS already initialized, checking device status\n");
		
		if(bassDeviceCheck == -1)
		{
			log_msg(LOG_DEBUG, "BASS Device was not intialized, initializing\n");
			playerState = OPENED;
			playState = NOTPLAYING;
			BASS_Init(0, 4800, 0, 0, NULL);
		}
		else
		{
			log_msg(LOG_DEBUG, "BASS_Init already done & device is operational, doing nothing\n");
		}
		
		if(wasapiDeviceCheck == -1)
		{
			BASS_WASAPI_Free();
			playerState = OPENED;
			playState = NOTPLAYING;
			log_msg(LOG_DEBUG, "BASS WASAPI Device was not initialized, initializing\n");
			BASS_WASAPI_Init(-1, 0, 0, BASS_WASAPI_AUTOFORMAT, 0.1, 0, WasapiProc, NULL);
		}
		else
		{
			log_msg(LOG_DEBUG, "BASS_WASAPI_Init already done & device is operational, doing nothing\n");
		}
		
		if(playerState != OPENED)
		{
			playerState = OPENED;
		}
		
		log_msg(LOG_DEBUG, "Checking stream status\n");
		if(BASS_ErrorGetCode() == 5)
		{
			log_msg(LOG_DEBUG, "Encountered BASS Error 5, reinitialize Decoder stream\n");
			dec = BASS_StreamCreate(info.freq, info.chans, BASS_STREAM_DECODE|BASS_SAMPLE_FLOAT, (STREAMPROC*)WasapiProc, 0);
			BASS_Mixer_StreamAddChannel(str, dec, 0);
		}
	}
	else
	if (noFiles == 0)
	{
		log_msg(LOG_DEBUG, "Audio library for commands is: BASS\n");
		log_msg(LOG_DEBUG, "BASS_Init\n");
		log_msg(LOG_DEBUG, "BASS Device initializing\n");
		BASS_Init(0, 4800, 0, 0, NULL);
		check_bass_error("BASS Error Occured After BASS Init");
		
		log_msg(LOG_DEBUG, "BASS WASAPI Device initializing\n");
		BASS_WASAPI_Init(-1, 0, 0, BASS_WASAPI_AUTOFORMAT, 0.1, 0, WasapiProc, NULL);
		check_bass_error("BASS Error Occured After BASS Wasapi Init");

		BASS_WASAPI_GetInfo(&info);
		str = BASS_Mixer_StreamCreate(info.freq, info.chans, BASS_STREAM_DECODE|BASS_SAMPLE_FLOAT);
		check_bass_error("BASS Error Occured After Mixer Stream Init");
		dec = BASS_StreamCreate(info.freq, info.chans, BASS_STREAM_DECODE|BASS_SAMPLE_FLOAT, (STREAMPROC*)WasapiProc, 0);
		check_bass_error("BASS Error Occured After Decoder Stream Init");
		BASS_Mixer_StreamAddChannel(str, dec, 0);
		initDone = YES;
		log_msg(LOG_DEBUG, "Checking Player and Play Status\n");
		check_bass_error("BASS Error occured after initializing player state check");
		switch (playerState)
		{
			case OPENED:
			{
				log_msg(LOG_DEBUG, "Player Status: OPENED\n");
				break;
			}
			case CLOSED:
			{
				log_msg(LOG_DEBUG, "Player Status: CLOSED\n");
				log_msg(LOG_DEBUG, "Player Status should not be CLOSED on INIT, SETTING OPENED\n");
				playerState = OPENED;
				break;
			}
		}
		switch (playState)
		{
			case PLAYING:
			{
				log_msg(LOG_DEBUG, "Play Status: PLAYING\n");
				break;
			}
			case PAUSED:
			{
				log_msg(LOG_DEBUG, "Play Status: PAUSED\n");
				break;
			}
			case STOPPED:
			{
				log_msg(LOG_DEBUG, "Play Status: STOPPED\n");
				break;
			}
		}
		log_msg(LOG_DEBUG, "BASS Device Number is: %d\n", BASS_GetDevice());
		log_msg(LOG_DEBUG, "BASS WASAPI Device Number is: %d\n", BASS_WASAPI_GetDevice());
		
		check_bass_error("BASS Error occured after playerState and playState check");
		
		DWORD dataBuffer;
		DWORD bufferSize = sizeof(dataBuffer);
		DWORD dwVolume;
		DWORD finalVolume = 0;
		float wasapiVolume;
		HKEY hkey;
		if (RegOpenKeyExA(HKEY_CURRENT_USER, TEXT("SOFTWARE\\Cavedog Entertainment\\Total Annihilation"), 0, KEY_READ, &hkey) != ERROR_SUCCESS) 
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

		log_msg(LOG_DEBUG, "musicvol regkey status: %d\n", status);
		log_msg(LOG_DEBUG, "musicvol regkey value: %d\n", dataBuffer);
		log_msg(LOG_DEBUG, "musicvol regkey size: %d\n", bufferSize);
		dwVolume = dataBuffer;
		finalVolume = dwVolume * 156.25;
		log_msg(LOG_DEBUG, "BASS initial stream volume set at: %d\n", finalVolume);
		WasapiVolumeConfig(finalVolume);
	}
	return 0;
}

int bass_pause()
{
	if (noFiles == 0)
	{
		if(BASS_ErrorGetCode() != -1 && BASS_ErrorGetCode() != 0)
		{
			BASS_WASAPI_Free();
			bass_init();
			log_msg(LOG_DEBUG, "Error during pause handling, calling BASS_WASAPI_FREE() and redoing device initialization\n");
			check_bass_error("Checking BASS errors after error in pause handling");
		}
		else
		BASS_WASAPI_Stop(FALSE);
		log_msg(LOG_DEBUG, "BASS_WASAPI_Stop(pause)\n");
	}
	else
	if (noFiles == 1)
	{
		log_msg(LOG_DEBUG, "Pause was called when no playable music files are present\n");
		return 1;
	}
    return 0;
}

/*
void bass_stop()
{
	if (noFiles == 0)
	{
		if (BASS_ErrorGetCode() != 0)
		{
			if(BASS_ErrorGetCode() == -1)
			{
				BASS_WASAPI_Stop(TRUE);
				BASS_StreamFree(dec);
				BASS_WASAPI_Start();
				log_msg(LOG_DEBUG, "BASS_WASAPI_Stop\n");
				playState = STOPPED;
			}
			else
			if(BASS_ErrorGetCode() != -1 && BASS_ErrorGetCode() != 5)
			{
				BASS_WASAPI_Free();
				bass_init();
				log_msg(LOG_DEBUG, "BASS_WASAPI_Free\n");
			}
			else
			if(BASS_ErrorGetCode() == 5)
			{
				BASS_WASAPI_Free();
				bass_init();
				log_msg(LOG_DEBUG, "Bass Error 5 encountered, running bass_init again to restart streams\n");
			}
		}
		else
		BASS_WASAPI_Stop(TRUE);
		BASS_StreamFree(dec);
		BASS_WASAPI_Start();
		log_msg(LOG_DEBUG, "BASS_WASAPI_Stop\n");
		playState = STOPPED;
	}
	return;
}
*/

void bass_stop(void)
{
    if (noFiles) {
        log_msg(LOG_INFO, "bass_stop: no files to stop\n");
        return;
    }

    log_msg(LOG_DEBUG, "bass_stop: stopping playback (track=%d)\n", currentTrack);

    // Stop WASAPI regardless of error state
    BASS_WASAPI_Stop(TRUE);

    // Free the decoder stream if valid
    if (dec) {
        BASS_StreamFree(dec);
        dec = 0;
    }

    // Restart WASAPI so next play call works
    BASS_WASAPI_Start();

    // Reset state
    playState = STOPPED;
    log_msg(LOG_DEBUG, "bass_stop: playback stopped successfully\n");
}

/*
int bass_resume()
{
	if (noFiles == 0)
	{
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
						log_msg(LOG_DEBUG, "Encountered BASS Error 5, reinitialize Decoder stream\n");
						dec = BASS_StreamCreateFile(FALSE, tracks[currentTrack].path, 0, 0, BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_STREAM_PRESCAN);
						BASS_Mixer_StreamAddChannel(str, dec, 0);
					}
					else
					if(FileFormat == 3)
					{
						log_msg(LOG_DEBUG, "Encountered BASS Error 5, reinitialize Decoder stream\n");
						dec = BASS_FLAC_StreamCreateFile(FALSE, tracks[currentTrack].path, 0, 0, BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_STREAM_PRESCAN);
						BASS_Mixer_StreamAddChannel(str, dec, 0);
					}
				}
				BASS_Start();
				BASS_WASAPI_Start();
			}
		}
		log_msg(LOG_DEBUG, "BASS_WASAPI_Start(unpause)\n");
		playState = PLAYING;
	}
	return 0;
}
*/

int bass_resume(void)
{
    if (noFiles) return 1;

    if (playState == PAUSED) {
        log_msg(LOG_DEBUG, "bass_resume: resuming from PAUSED\n");
        BASS_Start();
        BASS_WASAPI_Start();
        playState = PLAYING;
        return 0;
    }

    if (playState == PLAYING) {
        // Already playing, just ensure WASAPI is active
        log_msg(LOG_DEBUG, "bass_resume: already PLAYING, restarting WASAPI\n");
        BASS_Start();
        BASS_WASAPI_Start();
        return 0;
    }

    // If not paused or playing, reopen the current track
    log_msg(LOG_DEBUG, "bass_resume: reopening stream (track=%d)\n", currentTrack);

    if (open_track_stream(currentTrack, FileFormat, PlaybackMode) == 0) {
        BASS_Start();
        BASS_WASAPI_Start();
        playState = PLAYING;
        return 0;
    }
	check_bass_error("Bass_Resume error check");
    return 1; // error
}

/*
int bass_clear()
{
	if (noFiles == 0)
	{
		BASS_StreamFree(dec);
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
		log_msg(LOG_DEBUG, "Track for bass_clear is: %d\n", currentTrack);
		log_msg(LOG_DEBUG, "BASS_ChannelStop + StreamFree + ChannelPlay\n");

                return 0;
	}
	return 0;
}
*/

int bass_clear(void)
{
    if (noFiles) return 1;

    log_msg(LOG_DEBUG, "bass_clear: resetting decoder stream (track=%d)\n", currentTrack);
	

    // Free the old stream before reopening
    if (dec) {
        BASS_StreamFree(dec);
        dec = 0;
    }

    if (open_track_stream(currentTrack, FileFormat, PlaybackMode) == 0) {
        BASS_WASAPI_Stop(TRUE);  // reset playback state
        BASS_WASAPI_Start();     // restart with fresh stream
        log_msg(LOG_DEBUG, "bass_clear: stream reopened successfully\n");
        return 0;
    }
	check_bass_error("bass_clear error check");
    return 1; // error
}

/*
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
			check_bass_error("BASS Error occured during forceplay beginning()");
			if(currentTrack == 0)
			{
				currentTrack = FIRST_TRACK_INDEX;
			}
			BASS_StreamFree(dec);
			if(wasapiDeviceCheck == -1)
			{
				bassDeviceCheck = BASS_GetDevice();
				if(bassDeviceCheck == -1)
				{
					if (!BASS_Init(0, 48000, 0, 0, NULL))
					{
						log_msg(LOG_DEBUG, "Bass Device Initialization FAILED\n");
					}
				}
				if(wasapiDeviceCheck == -1)
				{
					if (!BASS_WASAPI_Init(-1, 0, 0, BASS_WASAPI_AUTOFORMAT, 0.1, 0, WasapiProc, NULL))
					{
						log_msg(LOG_DEBUG, "Wasapi Device Initialization FAILED\n");
					}
				}
			}
			log_msg(LOG_DEBUG, "bass_forceplay\n");
			log_msg(LOG_DEBUG, "BASS WASAPI Device Number is: %d\n", BASS_WASAPI_GetDevice());
		
			if (PlaybackMode == CD)
			{
				PlaybackFinished = 0;
				BASS_StreamFree(dec);
				dec = BASS_CD_StreamCreate(0, currentTrack, BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT);
				BASS_Mixer_StreamAddChannel(str, dec, 0);
				BASS_WASAPI_Start();
				playState = PLAYING;
				timesPlayed++;
				log_msg(LOG_DEBUG, "Begin CD Playback\n");
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
					log_msg(LOG_DEBUG, "Begin Music File Playback\n");
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
					log_msg(LOG_DEBUG, "			Begin Music File(FLAC) Playback\n");
				}
			}
			check_bass_error("			BASS Error check on forceplay end()");
		}
	}
	return 0;
}
*/

int bass_forceplay(const char *path)
{
    if (noFiles) return 1;

	log_msg(LOG_DEBUG, "bass_forceplay\n");

    if (currentTrack == 0) {
        currentTrack = 2;
    }

    if (open_track_stream(currentTrack, FileFormat, PlaybackMode) == 0) {
        BASS_WASAPI_Start();
        playState = PLAYING;
        timesPlayed++;
        log_msg(LOG_DEBUG, "Forced playback started (track=%d)\n", currentTrack);
    }
	check_bass_error("BASS Error check on forceplay end");
    return 0;
}

/*
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
			check_bass_error("			BASS Error occured during play beginning");
			if(currentTrack == 0)
			{
				currentTrack = FIRST_TRACK_INDEX;
			}
			BASS_StreamFree(dec);
			if(wasapiDeviceCheck == -1)
			{
				bassDeviceCheck = BASS_GetDevice();
				if(bassDeviceCheck == -1)
				{
					if (!BASS_Init(0, 48000, 0, 0, NULL))
					{
						log_msg(LOG_DEBUG, "			Bass Device Initialization FAILED\n");
					}
				}
				if(wasapiDeviceCheck == -1)
				{
					if (!BASS_WASAPI_Init(-1, 0, 0, BASS_WASAPI_AUTOFORMAT, 0.1, 0, WasapiProc, NULL))
					{
						log_msg(LOG_DEBUG, "			Wasapi Device Initialization FAILED\n");
					}
				}
			}
			log_msg(LOG_DEBUG, "			bass_play\n");
			log_msg(LOG_DEBUG, "			BASS WASAPI Device Number is: %d\n", BASS_WASAPI_GetDevice());
		
			if (PlaybackMode == CD)
			{
				PlaybackFinished = 0;
				BASS_StreamFree(dec);
				dec = BASS_CD_StreamCreate(0, currentTrack, BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT);
				BASS_Mixer_StreamAddChannel(str, dec, 0);
				BASS_WASAPI_Start();
				playState = PLAYING;
				timesPlayed++;
				log_msg(LOG_DEBUG, "			Begin CD Playback\n");
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
					log_msg(LOG_DEBUG, "			Begin Music File Playback\n");
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
					log_msg(LOG_DEBUG, "			Begin Music File(FLAC) Playback\n");
				}
			}
			check_bass_error("			BASS Error check on play end");
		}
	}
	
	return 0;
}
*/

int bass_play(const char *path)
{
    if (noFiles) return 1;

    if (playState == PAUSED) {
        // Already paused, nothing to do
        return 0;
    }

    log_msg(LOG_DEBUG, "bass_play: preparing playback\n");

    if (currentTrack == 0) {
        currentTrack = 2; // keep your existing offset logic
    }

    if (open_track_stream(currentTrack, FileFormat, PlaybackMode) == 0) {
        BASS_WASAPI_Start();
        playState = PLAYING;
        timesPlayed++;
        log_msg(LOG_DEBUG, "Playback started (track=%d)\n", currentTrack);
    }
	check_bass_error("BASS Error check on play end");
    return 0;
}

void WINAPI fake_ExitProcess(UINT uExitCode)
{
	BASS_WASAPI_Free();
	BASS_Free();
	
	DeleteCriticalSection(&cs);
	DeleteCriticalSection(&wproc_cs);
	DeleteCriticalSection(&log_cs);
	if (fh)
	{
		fflush(fh);
		fclose(fh);
		fh = NULL;
	}

	return ExitProcess(uExitCode);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		/* Make sure we are in Total Annihilation process before patching */
		HMODULE game_exe = GetModuleHandleA(NULL);
		if (game_exe && memcmp((char*)game_exe + 0x00010000, "\x14\x68\x78\x1B\x50\x00\x8D\x4C\x24\x1B", 10) == 0)
		{
			patch_call_nop((void*)0x004E4708, (void*)fake_ExitProcess);
			patch_call_nop((void*)0x004E71A0, (void*)fake_ExitProcess);
			patch_call_nop((void*)0x004EADF2, (void*)fake_ExitProcess);
		}
        // Avoid per-thread attach/detach calls to keep things lean
        DisableThreadLibraryCalls(hinstDLL);
		
		InitializeCriticalSection(&cs);
		init_wgmus(hinstDLL);
	}

	if (fdwReason == DLL_PROCESS_DETACH)
	{
		if (fh) {
            log_msg(LOG_INFO, "dll detached, shutting down");
        }
    }

    return TRUE;
}

MCIERROR WINAPI wgmus_mciSendCommandA(MCIDEVICEID deviceID, UINT uintMsg, DWORD_PTR dwptrCmd, DWORD_PTR dwParam)
{
	if(TRUE)
	{
		log_msg(LOG_DEBUG, "mciSendCommandA(deviceID=%p, uintMsg=%p, dwptrCmd=%p, dwParam=%p)\n", deviceID, uintMsg, dwptrCmd, dwParam);
		if (deviceID == MAGIC_DEVICEID)
		{
			if (uintMsg == MCI_OPEN)
			{
				log_msg(LOG_DEBUG, "MCI_OPEN\n");
				if(playerState != OPENED)
				{
					playerState = OPENED;		
					log_msg(LOG_DEBUG, "Initialize BASS\n");
					bass_init();
					uintMsg = 0;
					return 0;
				}
				if(playState == PAUSED)
				{
					BASS_WASAPI_Start();
					uintMsg = 0;
					return 0;
				}
				else
				return 1;
			}
			else
			if (uintMsg == MCI_PAUSE)
			{
				if(playerState == OPENED)
				{
					log_msg(LOG_DEBUG, "MCI_PAUSE\n");
					if(playState == PAUSED)
					{
						bass_pause();
						BASS_WASAPI_Stop(FALSE);
						log_msg(LOG_DEBUG, "playState was paused when pause was called for\n");
						uintMsg = 0;
						return 0;
					}
					else
					if(playState != PAUSED)
					{
						bass_pause();
						BASS_WASAPI_Stop(FALSE);
						log_msg(LOG_DEBUG, "playState was not paused when pause was called for\n");
						uintMsg = 0;
						return 0;
					}
				}
				else
				log_msg(LOG_DEBUG, "major error while calling MCI_PAUSE\n");
				return 1;
			}
			else
			if (uintMsg == MCI_STOP)
			{
				if(playerState == OPENED)
				{
					if(playState != STOPPED)
					{
						log_msg(LOG_DEBUG, "MCI_STOP\n");
						bass_stop();
						uintMsg = 0;
						return 0;
					}
				}
				else
				return 1;
			}
			else
			if (uintMsg == MCI_CLOSE)
			{
				log_msg(LOG_DEBUG, "MCI_CLOSE\n");
				if(playerState != CLOSED)
				{
					//playerState = CLOSED;
					bass_stop();
					BASS_WASAPI_Stop(TRUE);
					log_msg(LOG_DEBUG, "Ignoring close command since TA will still send commands after it, potentially causing freezes\n");
					uintMsg = 0;
					return 0;
				}
				else
				return 1;
			}
			else
			if (uintMsg == MCI_STATUS)
			{
				LPMCI_STATUS_PARMS parms = (LPVOID)dwParam;

				log_msg(LOG_DEBUG, "MCI_STATUS\n");

				parms->dwReturn = 0;
				
				if (PlaybackMode == CD)
				{
					if (parms->dwItem == MCI_STATUS_NUMBER_OF_TRACKS)
					{
						log_msg(LOG_DEBUG, "MCI_STATUS_NUMBER_OF_TRACKS %d\n", cdTracks);
						parms->dwReturn = cdTracks;
						uintMsg = 0;
						return 0;
					}
					else
					if (parms->dwItem == MCI_CDA_STATUS_TYPE_TRACK)
					{
						log_msg(LOG_DEBUG, "MCI_CDA_STATUS_TYPE_TRACK\n");
						if((parms->dwTrack > 0) &&  (parms->dwTrack <= MAX_TRACKS))
						{
							parms->dwTrack -= 1;
							DWORD bassCdTrackLength = BASS_CD_GetTrackLength(0, parms->dwTrack);
							if (bassCdTrackLength > 0)
							{
								parms->dwReturn = MCI_CDA_TRACK_AUDIO;
								log_msg(LOG_DEBUG, "MCI_CDA_TRACK_AUDIO\n");
								uintMsg = 0;
								return 0;
							}
							else
							if (bassCdTrackLength < 0)
							{
								parms->dwReturn = MCI_CDA_TRACK_OTHER;
								log_msg(LOG_DEBUG, "MCI_CDA_TRACK_OTHER\n");
								uintMsg = 0;
								return 0;
							}
						}
					}
					else
					if (parms->dwItem == MCI_STATUS_CURRENT_TRACK)
					{
						currentTrack++;
						parms->dwReturn = currentTrack;
						log_msg(LOG_DEBUG, "Sending current track: %d\n", currentTrack);
						uintMsg = 0;
						return 0;
					}
					else
					if (parms->dwItem == MCI_STATUS_POSITION)
					{
						log_msg(LOG_DEBUG, "MCI_STATUS_POSITION\n");
						
						bassLengthInSeconds = BASS_ChannelBytes2Seconds(dec, BASS_ChannelGetLength(dec, BASS_POS_BYTE));
						log_msg(LOG_DEBUG, "BASS Length in seconds: %d\n", bassLengthInSeconds);
						bassPosInSeconds = BASS_ChannelBytes2Seconds(dec, BASS_ChannelGetPosition(dec, BASS_POS_BYTE));
						log_msg(LOG_DEBUG, "BASS Position in seconds: %d\n", bassPosInSeconds);
						bassFrames = 0;
						bassMilliseconds = 0;
						bassSeconds = 0;
						bassMinutes = 0;
						bassHours = 0;
						bassHours = (bassPosInSeconds/3600);
						bassMilliseconds = bassPosInSeconds*1000;
						if(bassMilliseconds < 0)
						{
							bassMilliseconds = 0;
						}
						bassMinutes = bassPosInSeconds/60;
						if(bassMinutes < 0)
						{
							bassMinutes = 0;
						}
						if(bassMinutes == 0)
						{
							bassSeconds = bassPosInSeconds;
						}
						else
						bassSeconds = (bassPosInSeconds -(bassMinutes*60));
						if(bassSeconds < 0)
						{
							bassSeconds = 0;
						}
						bassFrames = bassSeconds*75/1000;
						currentTrack++;
						log_msg(LOG_DEBUG, "currentTrack: %d\n", currentTrack);
						log_msg(LOG_DEBUG, "bassFrames: %d\n", bassFrames);
						log_msg(LOG_DEBUG, "bassMilliseconds: %d\n", bassMilliseconds);
						log_msg(LOG_DEBUG, "bassSeconds: %d\n", bassSeconds);
						log_msg(LOG_DEBUG, "bassMinutes: %d\n", bassMinutes);
						log_msg(LOG_DEBUG, "bassHours: %d\n", bassHours);
						log_msg(LOG_DEBUG, "sent track position\n");
						if (dwptrCmd & MCI_TRACK)
						{
							log_msg(LOG_DEBUG, "MCI_TRACK\n");
							parms->dwTrack -= 1;
							queriedCdTrack = parms->dwTrack;
							if(timeFormat == MCI_FORMAT_MILLISECONDS)
							{
								queriedCdTrack += 1;
								parms->dwReturn += bassMilliseconds;
								uintMsg = 0;
								return 0;
							}
							else
							if(timeFormat == MCI_FORMAT_TMSF)
							{
								queriedCdTrack += 1;
								parms->dwReturn = MCI_MAKE_TMSF(queriedCdTrack, 0, 0, 0);
								uintMsg = 0;
								return 0;
							}
						}
						else
						if(timeFormat == MCI_FORMAT_MILLISECONDS)
						{
							log_msg(LOG_DEBUG, "MCI_FORMAT_MILLISECONDS\n");
							currentTrack++;
							parms->dwReturn += bassMilliseconds;
							uintMsg = 0;
							return 0;
						}
						else
						if(timeFormat == MCI_FORMAT_TMSF)
						{
							log_msg(LOG_DEBUG, "MCI_FORMAT_TMSF\n");
							currentTrack++;
							parms->dwReturn = MCI_MAKE_TMSF(currentTrack, bassMinutes, bassSeconds, bassFrames);
							uintMsg = 0;
							return 0;
						}
					}
					if (parms->dwItem == MCI_STATUS_MODE)
					{
						log_msg(LOG_DEBUG, "MCI_STATUS_MODE\n");
						if(playerState == OPENED && playState == NOTPLAYING)
						{
							log_msg(LOG_DEBUG, "we are open\n");
							parms->dwReturn = MCI_MODE_OPEN;
							uintMsg = 0;
							return 0;
						}
						else
						if(playerState == CLOSED && playState == NOTPLAYING)
						{
							log_msg(LOG_DEBUG, "player not ready\n");
							parms->dwReturn = MCI_MODE_NOT_READY;
							uintMsg = 0;
							return 0;
						}							
						else
						if(playerState == OPENED && playState == PAUSED)
						{
							log_msg(LOG_DEBUG, "we are paused\n");
							parms->dwReturn = MCI_MODE_PAUSE;
							uintMsg = 0;
							return 0;
						}
						else
						if(playerState == OPENED && playState == STOPPED)
						{
							log_msg(LOG_DEBUG, "we are stopped\n");
							parms->dwReturn = MCI_MODE_STOP;
							uintMsg = 0;
							return 0;
						}
						else
						if(playerState == OPENED && playState == PLAYING)
						{
							log_msg(LOG_DEBUG, "we are playing\n");
							parms->dwReturn = MCI_MODE_PLAY;
							uintMsg = 0;
							return 0;
						}
					}
					return 0;
				}
				else
				if (PlaybackMode == MUSICFILE)
				{
					if (parms->dwItem == MCI_STATUS_NUMBER_OF_TRACKS)
					{
						log_msg(LOG_DEBUG, "MCI_STATUS_NUMBER_OF_TRACKS %d\n", numTracks);
						parms->dwReturn = numTracks;
						uintMsg = 0;
						return 0;
					}
					else
					if (parms->dwItem == MCI_CDA_STATUS_TYPE_TRACK)
					{
						log_msg(LOG_DEBUG, "MCI_CDA_STATUS_TYPE_TRACK MCI_CDA_TRACK_OTHER\n");
						if((parms->dwTrack == 1) &&  (parms->dwTrack < MAX_TRACKS))
						{
							parms->dwReturn = MCI_CDA_TRACK_OTHER;
							uintMsg = 0;
							return 0;
						}
					}
					else
					if (parms->dwItem == MCI_STATUS_CURRENT_TRACK)
					{
						log_msg(LOG_DEBUG, "Sending current track: %d\n", currentTrack);
						parms->dwReturn = currentTrack;
						uintMsg = 0;
						return 0;
					}
					else
					if (parms->dwItem == MCI_STATUS_POSITION)
					{
						log_msg(LOG_DEBUG, "MCI_STATUS_POSITION\n");
						if (dwptrCmd & MCI_TRACK)
						{
							nextTrack = currentTrack + 1;
							if (nextTrack > 17)
							{
								nextTrack = 2;
							}
							log_msg(LOG_DEBUG, "sent next track %d starting position\n", nextTrack);
							parms->dwReturn = nextTrack;
							uintMsg = 0;
							return 0;
						}
						else
						{
							bassLengthInSeconds = BASS_ChannelBytes2Seconds(dec, BASS_ChannelGetLength(dec, BASS_POS_BYTE));
							log_msg(LOG_DEBUG, "BASS Length in seconds: %d\n", bassLengthInSeconds);
							bassPosInSeconds = BASS_ChannelBytes2Seconds(dec, BASS_ChannelGetPosition(dec, BASS_POS_BYTE));
							log_msg(LOG_DEBUG, "BASS Position in seconds: %d\n", bassPosInSeconds);
							bassFrames = 0;
							bassMilliseconds = 0;
							bassSeconds = 0;
							bassMinutes = 0;
							bassHours = 0;
							bassHours = (bassPosInSeconds/3600);
							bassMilliseconds = bassPosInSeconds*1000;
							if(bassMilliseconds < 0)
							{
								bassMilliseconds = 0;
							}
							bassMinutes = bassPosInSeconds/60;
							if(bassMinutes < 0)
							{
								bassMinutes = 0;
							}
							if(bassMinutes == 0)
							{
								bassSeconds = bassPosInSeconds;
							}
							else
							bassSeconds = (bassPosInSeconds -(bassMinutes*60));
							if(bassSeconds < 0)
							{
								bassSeconds = 0;
							}
							bassFrames = bassSeconds*75/1000;
							log_msg(LOG_DEBUG, "currentTrack: %d\n", currentTrack);
							log_msg(LOG_DEBUG, "bassFrames: %d\n", bassFrames);
							log_msg(LOG_DEBUG, "bassMilliseconds: %d\n", bassMilliseconds);
							log_msg(LOG_DEBUG, "bassSeconds: %d\n", bassSeconds);
							log_msg(LOG_DEBUG, "bassMinutes: %d\n", bassMinutes);
							log_msg(LOG_DEBUG, "bassHours: %d\n", bassHours);
							parms->dwReturn = currentTrack;
							log_msg(LOG_DEBUG, "sent track position\n");
							uintMsg = 0;
							return 0;
						}
					}
					if (parms->dwItem == MCI_STATUS_MODE)
					{
						log_msg(LOG_DEBUG, "MCI_STATUS_MODE\n");
						if(playerState == OPENED && playState == NOTPLAYING)
						{
							log_msg(LOG_DEBUG, "we are open\n");
							parms->dwReturn = MCI_MODE_OPEN;
							uintMsg = 0;
							return 0;
						}
						else
						if(playerState == CLOSED && playState == NOTPLAYING)
						{
							log_msg(LOG_DEBUG, "player not ready\n");
							parms->dwReturn = MCI_MODE_NOT_READY;
							uintMsg = 0;
							return 0;
						}							
						else
						if(playerState == OPENED && playState == PAUSED)
						{
							log_msg(LOG_DEBUG, "we are paused\n");
							parms->dwReturn = MCI_MODE_PAUSE;
							uintMsg = 0;
							return 0;
						}
						else
						if(playerState == OPENED && playState == STOPPED)
						{
							log_msg(LOG_DEBUG, "we are stopped\n");
							parms->dwReturn = MCI_MODE_STOP;
							uintMsg = 0;
							return 0;
						}
						else
						if(playerState == OPENED && playState == PLAYING)
						{
							log_msg(LOG_DEBUG, "we are playing\n");
							parms->dwReturn = MCI_MODE_PLAY;
							uintMsg = 0;
							return 0;
						}
					}
					return 0;
				}
			}
			else
			if (uintMsg == MCI_SET)
			{
				LPMCI_SET_PARMS parms = (LPVOID)dwParam;
			
				log_msg(LOG_DEBUG, "MCI_SET\n");
			
				if (dwptrCmd & MCI_SET_TIME_FORMAT)
				{
					log_msg(LOG_DEBUG, "MCI_SET_TIME_FORMAT\n");
					if (parms->dwTimeFormat == MCI_FORMAT_MILLISECONDS)
					{
						timeFormat = MCI_FORMAT_MILLISECONDS;
						log_msg(LOG_DEBUG, "MCI_FORMAT_MILLISECONDS\n");
						dwptrCmd = 0;
						uintMsg = 0;
						return 0;
					}
					else
					if (parms->dwTimeFormat == MCI_FORMAT_TMSF)
					{
						timeFormat = MCI_FORMAT_TMSF;
						log_msg(LOG_DEBUG, "MCI_FORMAT_TMSF\n");
						dwptrCmd = 0;
						uintMsg = 0;
						return 0;
					}
				}
				return 0;
			}
			else
			if (uintMsg == MCI_PLAY)
			{
				log_msg(LOG_DEBUG, "MCI_PLAY\n");
			
				LPMCI_PLAY_PARMS parms = (LPVOID)dwParam;
				
				if(playState == PAUSED)
				{
					if (dwptrCmd & MCI_NOTIFY)
					{
						log_msg(LOG_DEBUG, "bass_resume from paused via notify\n");
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
						log_msg(LOG_DEBUG, "MCI_FROM\n");
					}
					else
					if (dwptrCmd & MCI_TO)
					{
						log_msg(LOG_DEBUG, "MCI_TO\n");
					}
					
					if (PlaybackMode == CD)
					{
						parms->dwFrom -= 1;
					}
					
					currentTrack = (int)(parms->dwFrom);
					nextTrack = (int)(parms->dwTo);
					log_msg(LOG_DEBUG, "From value: %d\n", parms->dwFrom);
					log_msg(LOG_DEBUG, "Current track int value is: %d\n", currentTrack);
					

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
					log_msg(LOG_DEBUG, "BASS_ChannelPlay from paused via notify\n");
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
		log_msg(LOG_DEBUG, "[MCI String = %s, MCI DEVICE ID = %08X]\n", lpszCmd, hwndCallback);
		
		for (int i = 0; lpszCmd[i]; i++)
		{
			tolower(lpszCmd[i]);
		}
		
		int cTrack = 0;
		
		if (strcmp(lpszCmd, "open cdaudio") == 0)
		{
			log_msg(LOG_DEBUG, "mciSendStringA called for MCI_OPEN\n");
			static MCI_WAVE_OPEN_PARMS waveParms;
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_OPEN, 0, (DWORD_PTR)NULL);
			lpszCmd = "";
			return 0;
		}
		if (strcmp(lpszCmd, "pause cdaudio") == 0)
		{
			log_msg(LOG_DEBUG, "mciSendStringA called for MCI_PAUSE\n");
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_PAUSE, 0, (DWORD_PTR)NULL);
			lpszCmd = "";
			playState = PAUSED;
			return 0;
		}
		if (strcmp(lpszCmd, "stop cdaudio") == 0)
		{
			log_msg(LOG_DEBUG, "mciSendStringA called for MCI_STOP\n");
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STOP, 0, (DWORD_PTR)NULL);
			lpszCmd = "";
			return 0;
		}
		if (strcmp(lpszCmd, "close cdaudio") == 0)
		{
			log_msg(LOG_DEBUG, "mciSendStringA called for MCI_CLOSE\n");
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_CLOSE, 0, (DWORD_PTR)NULL);
			lpszCmd = "";
			return 0;
		}
		if (strcmp(lpszCmd, "set cdaudio time format milliseconds") == 0)
		{
			static MCI_SET_PARMS parms;
			parms.dwTimeFormat = MCI_FORMAT_MILLISECONDS;
			log_msg(LOG_DEBUG, "mciSendStringA called for MCI_SET with MCI_SET_TIME_FORMAT MCI_FORMAT_MILLISECONDS\n");			
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
			lpszCmd = "";
			return 0;
		}
		if (strcmp(lpszCmd, "set cdaudio time format tmsf") == 0)
		{
			static MCI_SET_PARMS parms;
			parms.dwTimeFormat = MCI_FORMAT_TMSF;
			log_msg(LOG_DEBUG, "mciSendStringA called for MCI_SET with MCI_SET_TIME_FORMAT MCI_FORMAT_TMSF\n");
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
			lpszCmd = "";
			return 0;
		}
		if (strcmp(lpszCmd, "status cdaudio number of tracks") == 0)
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_NUMBER_OF_TRACKS;
			log_msg(LOG_DEBUG, "mciSendStringA called for MCI_STATUS with MCI_STATUS_ITEM number of tracks \n");
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&parms);
			sprintf(lpszRetStr, "%d", numTracks);
			lpszCmd = "";
			return 0;
		}
		if (sscanf(lpszCmd, "status cdaudio type track %d", &cTrack) == 1)
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_CDA_STATUS_TYPE_TRACK;
			parms.dwTrack = cTrack;
			log_msg(LOG_DEBUG, "mciSendStringA called for MCI_STATUS with MCI_STATUS_ITEM|MCI_TRACK \n");
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
			sprintf(lpszRetStr, "%d", parms.dwReturn);
			lpszCmd = "";
			return 0;
		}
		if (strcmp(lpszCmd, "status cdaudio mode") == 0)
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_MODE;
			log_msg(LOG_DEBUG, "mciSendStringA called for MCI_STATUS with MCI_STATUS_ITEM|MCI_STATUS_MODE \n");
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_STATUS_MODE, (DWORD_PTR)&parms);
			lpszCmd = "";
			return 0;
		}
		if (strcmp(lpszCmd, "status cdaudio current track") == 0)
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_CURRENT_TRACK;
			parms.dwTrack = currentTrack;
			log_msg(LOG_DEBUG, "mciSendStringA called for MCI_STATUS parms MCI_STATUS_CURRENT_TRACK with MCI_STATUS_ITEM|MCI_TRACK \n");
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
			sprintf(lpszRetStr, "%d", parms.dwReturn);
			lpszCmd = "";
			return 0;
		}
		if (sscanf(lpszCmd, "status cdaudio length track %d", &cTrack) == 1)
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_LENGTH;
			parms.dwTrack = cTrack;
			log_msg(LOG_DEBUG, "mciSendStringA called for MCI_STATUS parms MCI_STATUS_LENGTH with MCI_STATUS_ITEM|MCI_TRACK(track number %) \n");
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
			sprintf(lpszRetStr, "%d", parms.dwReturn);
			lpszCmd = "";
			return 0;
        }
		if (sscanf(lpszCmd, "status cdaudio position track %d", &cTrack) == 1)
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_POSITION;
			parms.dwTrack = cTrack;
			log_msg(LOG_DEBUG, "mciSendStringA called for MCI_STATUS parms MCI_STATUS_POSITION with MCI_STATUS_ITEM|MCI_TRACK(track number %) \n");
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
			sprintf(lpszRetStr, "%d", parms.dwReturn);
			lpszCmd = "";
			return 0;
        }
		else
		if (strcmp(lpszCmd, "status cdaudio position") == 0)
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_POSITION;
			log_msg(LOG_DEBUG, "mciSendStringA called for MCI_STATUS parms MCI_STATUS_POSITION with MCI_STATUS_ITEM \n");
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&parms);
			sprintf(lpszRetStr, "%d", parms.dwReturn);
			lpszCmd = "";
			return 0;
        }
		int from = -1, to = -1;
		if (sscanf(lpszCmd, "play cdaudio from %d to %d notify", &from, &to) == 2)
		{
			static MCI_PLAY_PARMS parms;
			parms.dwFrom = from;
			parms.dwTo = to;
			log_msg(LOG_DEBUG, "mciSendStringA called for MCI_PLAY with MCI_FROM|MCI_TO|MCI_NOTIFY \n");
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM|MCI_TO|MCI_NOTIFY, (DWORD_PTR)&parms);
			lpszCmd = "";
			return 0;
		}
		else
		if (sscanf(lpszCmd, "play cdaudio from %d notify", &from) == 1)
		{
			static MCI_PLAY_PARMS parms;
			parms.dwFrom = from;
			log_msg(LOG_DEBUG, "mciSendStringA called for MCI_PLAY with MCI_FROM|MCI_NOTIFY \n");
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM|MCI_NOTIFY, (DWORD_PTR)&parms);
			lpszCmd = "";
			return 0;
		}
		else
		if (sscanf(lpszCmd, "play cdaudio from %d", &from) == 1)
		{
			static MCI_PLAY_PARMS parms;
			parms.dwFrom = from;
			log_msg(LOG_DEBUG, "mciSendStringA called for MCI_PLAY with MCI_FROM \n");
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM, (DWORD_PTR)&parms);
			lpszCmd = "";
			return 0;
		}
		else
		if (strcmp(lpszCmd, "play cdaudio notify") == 0)
		{
			static MCI_PLAY_PARMS parms;
			log_msg(LOG_DEBUG, "mciSendStringA called for MCI_PLAY with MCI_NOTIFY \n");
			wgmus_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_NOTIFY, (DWORD_PTR)&parms);
			lpszCmd = "";
			return 0;
		}
	}
	return err;
}

MMRESULT WINAPI wgmus_auxGetDevCapsA(UINT_PTR uintptrDeviceID, LPAUXCAPSA lpCapsa, UINT cbCaps)
{
	log_msg(LOG_DEBUG, "wgmus_auxGetDevCapsA(uintptrDeviceID=%08X, lpCapsa=%p, cbCaps=%08X\n", uintptrDeviceID, lpCapsa, cbCaps);

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
	log_msg(LOG_DEBUG, "wgmus_auxGetVolume(uintDeviceID=%08X, lpdwVolume=%p)\n", uintDeviceID, lpdwVolume);
	
	return MMSYSERR_NOERROR;
}


MMRESULT WINAPI wgmus_auxSetVolume(UINT uintDeviceID, DWORD dwVolume)
{
	log_msg(LOG_DEBUG, "wgmus_auxSetVolume(uintDeviceId=%08X, dwVolume=%08X)\n", uintDeviceID, dwVolume);
	
	static DWORD oldVolume = -1;
	DWORD finalVolume;
	float wasapiVolume;
	WORD leftChannel;
	WORD left;
	WORD right;
	
    left = dwVolume & 0xffff;
    right = (dwVolume >> 16) & 0xffff;

    log_msg(LOG_DEBUG, "Set Left Speaker value at: %08X\n", left);
    log_msg(LOG_DEBUG, "Set Right Speaker value at: %08X\n", right);
	
	log_msg(LOG_DEBUG, "Set aux volume at: %08X\n", dwVolume);
	
	finalVolume = left / 6.554; // so volume is not too loud, BASS quirk
	log_msg(LOG_DEBUG, "BASS stream volume set at: %d\n", finalVolume);
	WasapiVolumeConfig(finalVolume);


    return MMSYSERR_NOERROR;
}
