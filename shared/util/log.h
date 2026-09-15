#ifndef AIRPLAY_LOG_H
#define AIRPLAY_LOG_H

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        AIRPLAY_LOG_DEBUG = 0,
        AIRPLAY_LOG_INFO,
        AIRPLAY_LOG_WARN,
        AIRPLAY_LOG_ERROR,
    } airplay_log_level_t;

    void airplay_log_set_level(airplay_log_level_t level);
    airplay_log_level_t airplay_log_get_level(void);

    void airplay_log_message(
        airplay_log_level_t level, const char *file, int line, const char *format, ...);

#define LOG_DEBUG(...) airplay_log_message(AIRPLAY_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)

#define LOG_INFO(...) airplay_log_message(AIRPLAY_LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)

#define LOG_WARN(...) airplay_log_message(AIRPLAY_LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)

#define LOG_ERROR(...) airplay_log_message(AIRPLAY_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* AIRPLAY_LOG_H */
