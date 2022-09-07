#include <windows.h>
#include <winreg.h>
#include <stdio.h>
#include <ctype.h>
#include <dirent.h>
#include <string.h>

CRITICAL_SECTION cs;

#define dprintf(...) if (fh) { fprintf(fh, __VA_ARGS__); fflush(NULL); }
FILE *fh = NULL;

char musdll_path[2048];

/* Config File Defines start */
int AudioLibrary;
int FileFormat;
int PlaybackMode;
char MusicFolder[255];
/* Config File Defines end */

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
}

MCIERROR WINAPI mmusi_mciSendStringA(LPCTSTR lpszCmd, LPTSTR lpszRetStr, UINT cchReturn, HANDLE  hwndCallback)
{
}

UINT WINAPI mmusi_auxGetNumDevs()
{
}

MMRESULT WINAPI mmusi_auxGetDevCapsA(UINT_PTR uintptrDeviceID, LPAUXCAPSA lpcapsa, UINT cbCaps)
{
}

MMRESULT WINAPI mmusi_auxGetVolume(UINT uintDeviceID, LPDWORD lpdwVolume)
{
}


MMRESULT WINAPI mmusi_auxSetVolume(UINT uintDeviceID, DWORD dwVolume)
{
}


BOOL WINAPI mmusi_PlaySoundA(LPCTSTR lpctstrSound, HMODULE hmod, DWORD dwSound)
{
}


UINT WINAPI mmusi_waveOutGetNumDevs()
{
}

MMRESULT WINAPI mmusi_waveOutGetVolume(HWAVEOUT hwo, LPDWORD lpdwVolume)
{
}


MMRESULT WINAPI mmusi_waveOutSetVolume(HWAVEOUT hwo, DWORD dwVolume)
{
}