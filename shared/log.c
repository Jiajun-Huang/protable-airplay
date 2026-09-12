#include "log.h"
#include "airplay_config.h"
#include <stdarg.h>
#include <stdio.h>

log_level_t g_log_level = AIRPLAY_LOG_LEVEL;

void log_message(log_level_t level, const char *tag, const char *format, ...)
{
    static const char *const names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    va_list args;
    FILE *output;

    if (level < LOG_LEVEL_DEBUG || level > LOG_LEVEL_ERROR || level < g_log_level)
        return;

    output = level >= LOG_LEVEL_WARN ? stderr : stdout;
    if (tag && tag[0])
        fprintf(output, "[%s][%s] ", names[level], tag);
    else
        fprintf(output, "[%s] ", names[level]);
    va_start(args, format);
    vfprintf(output, format, args);
    va_end(args);
}
