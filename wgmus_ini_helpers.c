
#include "wgmus_ini_helpers.h"
#include <string.h>
#include <stdlib.h>

/* Build "<dll_dir>\filename" into outPath */
void path_next_to_dll(HINSTANCE hinstDLL, const char *filename, char *outPath, size_t outSize)
{
    if (!outPath || outSize < 4) return;
    outPath[0] = '\0';

    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(hinstDLL, buf, sizeof buf);
    if (n == 0 || n >= sizeof buf) return;

    char *slash = strrchr(buf, '\\');
    if (!slash) return;
    *slash = '\0'; /* keep only directory */

    _snprintf_s(outPath, outSize, _TRUNCATE, "%s\\%s", buf, filename);
}

/* Accept "0..3" or "DEBUG/INFO/WARN/ERROR" (case-insensitive) */
log_level_t parse_log_level(const char *raw)
{
    if (!raw || !*raw) return LOG_INFO;

    char buf[32];
    size_t n = 0;

    /* ltrim */
    while (*raw == ' ' || *raw == '\t') raw++;

    /* copy up to buffer */
    while (raw[n] && n < sizeof(buf)-1) { buf[n] = raw[n]; n++; }
    buf[n] = '\0';

    /* rtrim */
    while (n && (buf[n-1]==' ' || buf[n-1]=='\t')) buf[--n] = '\0';

    /* numeric? */
    int all_digit = 1;
    for (size_t i=0; i<n; ++i) {
        if (buf[i] < '0' || buf[i] > '9') { all_digit = 0; break; }
    }
    if (all_digit) {
        int v = atoi(buf);
        if (v < LOG_DEBUG) v = LOG_DEBUG;
        if (v > LOG_ERROR) v = LOG_ERROR;
        return (log_level_t)v;
    }

    if (_stricmp(buf, "DEBUG") == 0) return LOG_DEBUG;
    if (_stricmp(buf, "INFO")  == 0) return LOG_INFO;
    if (_stricmp(buf, "WARN")  == 0 || _stricmp(buf, "WARNING") == 0) return LOG_WARN;
    if (_stricmp(buf, "ERROR") == 0) return LOG_ERROR;
    return LOG_INFO;
}

void load_log_config(HINSTANCE hinstDLL)
{
    char iniPath[MAX_PATH];
    path_next_to_dll(hinstDLL, "wgmus.ini", iniPath, sizeof iniPath);

    char raw[64] = {0};
    GetPrivateProfileStringA("Settings", "LogLevel", "", raw, sizeof raw, iniPath);
    g_log_level = parse_log_level(raw);

    log_msg(LOG_INFO, "Config path: %s", iniPath);
    log_msg(LOG_INFO, "LogLevel raw='%s' -> %s", raw[0]?raw:"<empty>", log_level_str[g_log_level]);
}

/* Open log next to DLL, unbuffered, and mark logger ready.
   Writes a guaranteed banner line even if log level filters most messages. */
void init_logger_paths(HINSTANCE hinstDLL)
{
    char logPath[MAX_PATH];
    path_next_to_dll(hinstDLL, "wgmus.log", logPath, sizeof logPath);

    if (!fh) {
        fopen_s(&fh, logPath, "w");
        if (fh) {
            setvbuf(fh, NULL, _IONBF, 0);             /* unbuffered */
            InitializeCriticalSection(&log_cs);       /* safe for later log_msg locking */
            InterlockedExchange(&g_log_ready, 1);     /* enable full locking path */

            /* guaranteed bytes even if we crash early */
            fprintf(fh, "[logger] started at %s\n", logPath);
            fflush(fh);
        }
    }
}
