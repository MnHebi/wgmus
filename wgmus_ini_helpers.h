
#ifndef WGMUS_INI_HELPERS_H
#define WGMUS_INI_HELPERS_H

#include <windows.h>
#include <stdio.h>

/* Your logger enums/externs (should already exist in your file) */
typedef enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR } log_level_t;

/* Provided by your main file */
extern FILE *fh;
extern CRITICAL_SECTION log_cs;
extern CRITICAL_SECTION cs;
extern volatile LONG g_log_ready;
extern const char* log_level_str[];
extern log_level_t g_log_level;

/* Provided by your main file */
void log_msg(log_level_t level, const char *fmt, ...);

/* Drop-in helpers */
void path_next_to_dll(HINSTANCE hinstDLL, const char *filename, char *outPath, size_t outSize);
log_level_t parse_log_level(const char *raw);
void load_log_config(HINSTANCE hinstDLL);
void init_logger_paths(HINSTANCE hinstDLL);

#endif /* WGMUS_INI_HELPERS_H */
