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

    /**
     * @brief Emit a formatted log message.
     * @param level Severity level of the message.
     * @param file Source file containing the log call.
     * @param line Source line containing the log call.
     * @param format printf-style message format.
     * @param ... Values referenced by @p format.
     */
    void log_message(log_level_t level, const char *file, int line, const char *format, ...);

/* Emit a debug message with its source location. */
#define LOG_DEBUG(...) log_message(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
/* Emit an informational message with its source location. */
#define LOG_INFO(...) log_message(LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
/* Emit a warning message with its source location. */
#define LOG_WARN(...) log_message(LOG_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
/* Emit an error message with its source location. */
#define LOG_ERROR(...) log_message(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
