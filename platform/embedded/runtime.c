#include "runtime.h"
#include "net.h"

#include "FreeRTOS.h"
#include "event_groups.h"
#include "task.h"

typedef struct
{
    void (*entry)(void *);
    EventBits_t done_bit;
} service_task_t;

static airplay_server_t server;
static EventGroupHandle_t completed;
static TaskHandle_t tasks[3];
static service_task_t services[3];
static unsigned started;
static int active;

/**
 * @brief service_task.
 * @param argument Parameter named argument.
 */
static void service_task(void *argument)
{
    service_task_t *service = (service_task_t *)argument;
    service->entry(&server);
    xEventGroupSetBits(completed, service->done_bit);
    /* The owner deletes this task after observing completion. This also makes
     * task-stack reclamation independent of when the idle task next runs. */
    for (;;)
        vTaskSuspend(NULL);
}

int airplay_platform_start(const airplay_config_t *config)
{
    static const char *names[] = {"airplay_mdns", "airplay_rtsp", "airplay_audio"};
    static const unsigned depths[] = {
        AIRPLAY_MDNS_STACK_WORDS, AIRPLAY_RTSP_STACK_WORDS, AIRPLAY_AUDIO_STACK_WORDS};
    void (*entries[])(void *) = {airplay_mdns_main, airplay_rtsp_main, airplay_audio_main};
    if (active || !config)
        return -1;
    if (net_init() != 0)
        return -1;
    if (airplay_server_init(&server, config) != 0)
    {
        net_deinit();
        return -1;
    }
    completed = xEventGroupCreate();
    if (!completed)
    {
        airplay_server_deinit(&server);
        net_deinit();
        return -1;
    }
    active = 1;
    for (started = 0; started < 3; ++started)
    {
        services[started].entry = entries[started];
        services[started].done_bit = (EventBits_t)1 << started;
        if (xTaskCreate(service_task,
                        names[started],
                        depths[started],
                        &services[started],
                        tskIDLE_PRIORITY + AIRPLAY_TASK_PRIORITY_OFFSET,
                        &tasks[started]) != pdPASS)
        {
            airplay_platform_stop();
            return -1;
        }
    }
    return 0;
}

int airplay_platform_stop(void)
{
    int result;
    if (!active)
        return 0;
    airplay_server_stop(&server);
    if (started)
    {
        EventBits_t all_done = ((EventBits_t)1 << started) - 1;
        xEventGroupWaitBits(completed, all_done, pdFALSE, pdTRUE, portMAX_DELAY);
    }
    while (started)
    {
        --started;
        vTaskDelete(tasks[started]);
        tasks[started] = NULL;
    }
    vEventGroupDelete(completed);
    completed = NULL;
    result = airplay_server_result(&server);
    airplay_server_deinit(&server);
    net_deinit();
    active = 0;
    return result;
}
