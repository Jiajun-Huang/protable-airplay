#ifndef LOG_H
#define LOG_H

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

    /* Global log level - can be modified at runtime */
    extern log_level_t g_log_level;

    /* Messages include their terminating newline. Use the LOG_* macros at call sites. */
    void log_message(log_level_t level, const char *tag, const char *format, ...);

/* Convenience macros for different log levels */
#define LOG_DEBUG(tag, ...) log_message(LOG_LEVEL_DEBUG, tag, __VA_ARGS__)
#define LOG_INFO(tag, ...) log_message(LOG_LEVEL_INFO, tag, __VA_ARGS__)
#define LOG_WARN(tag, ...) log_message(LOG_LEVEL_WARN, tag, __VA_ARGS__)
#define LOG_ERROR(tag, ...) log_message(LOG_LEVEL_ERROR, tag, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
