#include "util/log.h"

#include "airplay_config.h"

#include <stdarg.h>
#include <stdio.h>

static airplay_log_level_t s_log_level = AIRPLAY_LOG_LEVEL;

void airplay_log_set_level(airplay_log_level_t level)
{
    if (level >= AIRPLAY_LOG_DEBUG && level <= AIRPLAY_LOG_ERROR)
        s_log_level = level;
}

airplay_log_level_t airplay_log_get_level(void)
{
    return s_log_level;
}

void airplay_log_message(
    airplay_log_level_t level, const char *file, int line, const char *format, ...)
{
    static const char *const names[] = {
        "DEBUG",
        "INFO",
        "WARN",
        "ERROR",
    };

    if (level < AIRPLAY_LOG_DEBUG || level > AIRPLAY_LOG_ERROR || level < s_log_level)
    {
        return;
    }

    FILE *output = level >= AIRPLAY_LOG_WARN ? stderr : stdout;

    fprintf(output, "[%s][%s:%d] ", names[level], file, line);

    va_list args;
    va_start(args, format);
    vfprintf(output, format, args);
    va_end(args);

    fputc('\n', output);
}
