#include <windows.h>
#include "wgmus.h"

static HINSTANCE winmm = 0;

HINSTANCE getWinmmHandle()
{
    return winmm;
}

/* watches for the app to close, unloads the library when it does */
/* since FreeLibrary is dangerous in DllMain */
void ExitMonitor(LPVOID DLLHandle)
{
    WaitForSingleObject(DLLHandle, INFINITE);
    FreeLibrary(getWinmmHandle());
}

/* if winmm.dll is already loaded, return its handle */
/* otherwise, load it */
HINSTANCE loadRealDLL()
{
    if (winmm)
        return winmm;

    char winmm_path[MAX_PATH];

    GetSystemDirectory(winmm_path, MAX_PATH);
    strncat(winmm_path, "\\winmm.DLL", 11); /* fixed gcc overflow warning */

    winmm = LoadLibrary(winmm_path);
    
    /* start watcher thread to close the library */
    CreateThread(NULL, 500, (LPTHREAD_START_ROUTINE)ExitMonitor, GetCurrentThread(), 0, NULL);

    return winmm;
}