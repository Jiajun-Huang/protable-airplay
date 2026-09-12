#ifndef LOG_H
#define LOG_H

/* Small process-wide logging interface used by shared code and platform entry points. */

#ifdef __cplusplus
extern "C"
{
#endif

    /* Log levels */
    typedef enum
    {
        LOG_LEVEL_DEBUG = 0,
        LOG_LEVEL_INFO,
        LOG_LEVEL_WARN,
        LOG_LEVEL_ERROR
    } log_level_t;

    /* Minimum severity emitted by log_message; callers may change it at runtime. */
    extern log_level_t g_log_level;

    /* Messages include their terminating newline. Use the LOG_* macros at call sites. */
    void log_message(log_level_t level, const char *tag, const char *format, ...);

/* Emit a debug message tagged with its module name. */
#define LOG_DEBUG(tag, ...) log_message(LOG_LEVEL_DEBUG, tag, __VA_ARGS__)
/* Emit an informational message tagged with its module name. */
#define LOG_INFO(tag, ...) log_message(LOG_LEVEL_INFO, tag, __VA_ARGS__)
/* Emit a warning message tagged with its module name. */
#define LOG_WARN(tag, ...) log_message(LOG_LEVEL_WARN, tag, __VA_ARGS__)
/* Emit an error message tagged with its module name. */
#define LOG_ERROR(tag, ...) log_message(LOG_LEVEL_ERROR, tag, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
