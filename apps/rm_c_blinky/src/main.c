#include "FreeRTOS.h"
#include "task.h"

#include "board.h"
#include "gyro_task.h"

volatile uint32_t g_blink_count;

static void park_fail_safe(void)
{
    board_imu_heater_force_off();
    board_led_set(0U);
    for (;;) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    park_fail_safe();
}

void vApplicationMallocFailedHook(void)
{
    park_fail_safe();
}

static void blink_task(void *context)
{
    (void)context;

    for (;;) {
        board_led_toggle();
        g_blink_count++;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int main(void)
{
    HAL_Init();
    board_clock_init();
    board_led_init();
    if (!board_imu_heater_init()) {
        park_fail_safe();
    }

    /* SPI1 and the chip selects are brought up before the scheduler so the
     * gyro task finds a ready bus; the device bring-up itself runs inside
     * that task, where its 80 ms reset settle can yield. */
    board_imu_spi_init();

    if (xTaskCreate(blink_task, "blink", configMINIMAL_STACK_SIZE, NULL, 1, NULL) != pdPASS) {
        park_fail_safe();
    }

    if (gyro_task_create() != pdPASS) {
        park_fail_safe();
    }

    vTaskStartScheduler();
    park_fail_safe();
}
