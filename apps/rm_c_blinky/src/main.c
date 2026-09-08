#include "FreeRTOS.h"
#include "task.h"

#include "board.h"
#include "gyro_task.h"

volatile uint32_t g_blink_count;

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    board_led_set(0U);
    for (;;) {
    }
}

void vApplicationMallocFailedHook(void)
{
    board_led_set(0U);
    for (;;) {
    }
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

    /* SPI1 and the chip selects are brought up before the scheduler so the
     * gyro task finds a ready bus; the device bring-up itself runs inside
     * that task, where its 80 ms reset settle can yield. */
    board_imu_spi_init();

    if (xTaskCreate(blink_task, "blink", configMINIMAL_STACK_SIZE, NULL, 1, NULL) != pdPASS) {
        board_led_set(0U);
        for (;;) {
        }
    }

    if (gyro_task_create() != pdPASS) {
        board_led_set(0U);
        for (;;) {
        }
    }

    vTaskStartScheduler();
    board_led_set(0U);
    for (;;) {
    }
}
