#ifndef AIRPLAY_EMBEDDED_RUNTIME_H
#define AIRPLAY_EMBEDDED_RUNTIME_H

#include "server.h"

/* Call from one owner task after the scheduler, network interface, and board
 * clock are ready. The owner serializes start/stop; neither is called by a
 * service task or interrupt handler. Only one server instance is supported. */
int airplay_platform_start(const airplay_config_t *config);
/* Stop waits for every service before cleanup. Returns the service result. */
int airplay_platform_stop(void);

/* Board-supplied UTC microseconds since 1970, safe to read from any task.
 * Use a synchronized RTC plus subsecond clock, not RTOS uptime alone. */
uint64_t airplay_board_time_us(void);

#endif
