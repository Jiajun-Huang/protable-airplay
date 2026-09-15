

#ifndef AIRPLAY_EMBEDDED_RUNTIME_H
#define AIRPLAY_EMBEDDED_RUNTIME_H

#include "service/server.h"

/* FreeRTOS runtime wrapper that owns the server instance and its service tasks. */

/* Call from one owner task after the scheduler, network interface, and board
 * clock are ready. The owner serializes start/stop; neither is called by a
 * service task or interrupt handler. Only one server instance is supported. */
/**
 * @brief airplay_platform_start.
 * @param config Parameter named config.
 * @return Function result.
 */
int airplay_platform_start(const airplay_config_t *config);
/* Stop waits for every service before cleanup. Returns the service result. */
/**
 * @brief airplay_platform_stop.
 * @return Function result.
 */
int airplay_platform_stop(void);

/* Board-supplied UTC microseconds since 1970, safe to read from any task.
 * Use a synchronized RTC plus subsecond clock, not RTOS uptime alone. */
/**
 * @brief airplay_board_time_us.
 * @return Function result.
 */
uint64_t airplay_board_time_us(void);

#endif
