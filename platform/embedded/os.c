#include "os.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

extern uint64_t airplay_board_time_us(void);

int os_mutex_init(os_mutex_t *mutex)
{
    if (!mutex)
        return -1;
    mutex->handle = xSemaphoreCreateMutex();
    return mutex->handle ? 0 : -1;
}

void os_mutex_lock(os_mutex_t *mutex)
{
    xSemaphoreTake((SemaphoreHandle_t)mutex->handle, portMAX_DELAY);
}

void os_mutex_unlock(os_mutex_t *mutex)
{
    xSemaphoreGive((SemaphoreHandle_t)mutex->handle);
}

void os_mutex_deinit(os_mutex_t *mutex)
{
    if (mutex && mutex->handle)
    {
        vSemaphoreDelete((SemaphoreHandle_t)mutex->handle);
        mutex->handle = NULL;
    }
}

void os_sleep_ms(unsigned ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);
    if (ms && !ticks)
        ticks = 1;
    if (ticks)
        vTaskDelay(ticks);
    else
        taskYIELD();
}

uint64_t os_time_us(void)
{
    return airplay_board_time_us();
}
