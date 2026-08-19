#include "board.h"

#define BOARD_IMU_SPI_TIMEOUT_MS 100U

static SPI_HandleTypeDef imu_spi;

static void halt_on_hal_error(HAL_StatusTypeDef status)
{
    if (status != HAL_OK) {
        for (;;) {
        }
    }
}

void board_imu_spi_init(void)
{
    GPIO_InitTypeDef pin = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();

    /* Preload both selects high so neither device is addressed during the
     * push-pull switch from input to output. PA4 is written once here and never
     * driven again: no accel accessor exists, so it cannot be pulled low.
     */
    HAL_GPIO_WritePin(BOARD_IMU_GYRO_CS_PORT, BOARD_IMU_GYRO_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BOARD_IMU_ACCEL_CS_PORT, BOARD_IMU_ACCEL_CS_PIN, GPIO_PIN_SET);

    pin.Pin = BOARD_IMU_GYRO_CS_PIN;
    pin.Mode = GPIO_MODE_OUTPUT_PP;
    pin.Pull = GPIO_NOPULL;
    pin.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(BOARD_IMU_GYRO_CS_PORT, &pin);

    pin.Pin = BOARD_IMU_ACCEL_CS_PIN;
    HAL_GPIO_Init(BOARD_IMU_ACCEL_CS_PORT, &pin);

    pin.Mode = GPIO_MODE_AF_PP;
    pin.Alternate = GPIO_AF5_SPI1;
    pin.Pin = BOARD_IMU_SPI_SCK_PIN | BOARD_IMU_SPI_MISO_PIN;
    HAL_GPIO_Init(BOARD_IMU_SPI_SCK_PORT, &pin);

    pin.Pin = BOARD_IMU_SPI_MOSI_PIN;
    HAL_GPIO_Init(BOARD_IMU_SPI_MOSI_PORT, &pin);

    imu_spi.Instance = BOARD_IMU_SPI;
    imu_spi.Init.Mode = SPI_MODE_MASTER;
    imu_spi.Init.Direction = SPI_DIRECTION_2LINES;
    imu_spi.Init.DataSize = SPI_DATASIZE_8BIT;
    imu_spi.Init.CLKPolarity = SPI_POLARITY_HIGH;
    imu_spi.Init.CLKPhase = SPI_PHASE_2EDGE;
    imu_spi.Init.NSS = SPI_NSS_SOFT;
    imu_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
    imu_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    imu_spi.Init.TIMode = SPI_TIMODE_DISABLE;
    imu_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    imu_spi.Init.CRCPolynomial = 10;
    halt_on_hal_error(HAL_SPI_Init(&imu_spi));
}

void board_imu_gyro_select(void)
{
    HAL_GPIO_WritePin(BOARD_IMU_GYRO_CS_PORT, BOARD_IMU_GYRO_CS_PIN, GPIO_PIN_RESET);
}

void board_imu_gyro_deselect(void)
{
    HAL_GPIO_WritePin(BOARD_IMU_GYRO_CS_PORT, BOARD_IMU_GYRO_CS_PIN, GPIO_PIN_SET);
}

bool board_imu_spi_transfer(const uint8_t *tx, uint8_t *rx, uint16_t length)
{
    return HAL_SPI_TransmitReceive(&imu_spi, (uint8_t *)tx, rx, length,
                                   BOARD_IMU_SPI_TIMEOUT_MS) == HAL_OK;
}
