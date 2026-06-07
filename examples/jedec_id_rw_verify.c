/*
 * Example 1 – Identify the chip and verify read/write/erase
 *
 * Demonstrates:
 *   - W25Q_Config setup with SPI transfer and delay callbacks
 *   - W25Q_ReadJEDECID to confirm the chip is responding
 *   - W25Q_EraseSector → W25Q_Write → W25Q_Read sequence
 *   - Basic pass/fail verification
 *
 * Replace the HAL stubs at the top with your actual peripheral handles
 * and GPIO definitions.  Everything below main() is portable.
 */

#include "w25q64.h"
#include <stdio.h>
#include <string.h>

/* -- Replace these with your hardware ------------------------------------ */
#define FLASH_CS_PORT   GPIOA
#define FLASH_CS_PIN    GPIO_PIN_4

extern SPI_HandleTypeDef hspi1;
/* ------------------------------------------------------------------------ */

static int32_t flash_spi_transfer(void          *user_context,
                                  const uint8_t *tx,
                                  uint8_t       *rx,
                                  size_t         length)
{
    SPI_HandleTypeDef *spi = (SPI_HandleTypeDef *)user_context;

    if ((spi == NULL) || (tx == NULL) || (rx == NULL) || (length == 0U)) {
        return W25Q_ERR_INVALID_ARG;
    }

    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_RESET);
    HAL_StatusTypeDef result = HAL_SPI_TransmitReceive(spi,
                                                        (uint8_t *)tx, rx,
                                                        (uint16_t)length,
                                                        HAL_MAX_DELAY);
    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET);

    return (result == HAL_OK) ? W25Q_OK : W25Q_ERR_IO;
}

static void flash_delay_ms(void *user_context, uint32_t delay_ms)
{
    (void)user_context;
    HAL_Delay(delay_ms);
}

int main(void)
{
    /* HAL init, clock config, GPIO and SPI init go here */

    /*
     * W25Q_Config:
     * - transfer_fn  : full-duplex SPI callback; asserts/deasserts CS internally.
     * - delay_ms_fn  : used by WaitBusy between BUSY polls.
     * - user_context : passed to both callbacks – HAL SPI handle here.
     */
    W25Q_Config flash = {
        .transfer_fn  = flash_spi_transfer,
        .delay_ms_fn  = flash_delay_ms,
        .user_context = &hspi1
    };

    /* ------------------------------------------------------------------ */
    /* 1. Read JEDEC ID – confirms wiring and SPI settings are correct     */
    /* ------------------------------------------------------------------ */
    uint8_t mfr, mem_type, capacity;

    if (W25Q_ReadJEDECID(&flash, &mfr, &mem_type, &capacity) != W25Q_OK) {
        printf("JEDEC read failed – check wiring\r\n");
        Error_Handler();
    }

    /*
     * W25Q64 (Winbond)  : EF 40 17
     * XM25QH64C (XMC)   : 20 70 17
     * BY25Q64 (BOYA)     : 68 40 17
     * All are compatible; only manufacturer byte differs.
     */
    printf("JEDEC ID: %02X %02X %02X\r\n", mfr, mem_type, capacity);

    /* ------------------------------------------------------------------ */
    /* 2. Erase sector 0, write a pattern, read it back, verify            */
    /* ------------------------------------------------------------------ */
    const uint32_t ADDR = 0x000000UL;

    /* Erase must come before every write – NOR flash bits only go 1→0.
     * Erase resets the sector to 0xFF.  Sector erase takes up to 400 ms;
     * W25Q_EraseSector blocks until the chip is idle.                     */
    if (W25Q_EraseSector(&flash, ADDR) != W25Q_OK) {
        printf("Erase failed\r\n");
        Error_Handler();
    }

    /* Build a simple incrementing pattern */
    uint8_t write_buf[64];
    for (uint8_t i = 0; i < sizeof(write_buf); i++) {
        write_buf[i] = i;
    }

    /* W25Q_Write splits across page boundaries automatically */
    if (W25Q_Write(&flash, ADDR, write_buf, sizeof(write_buf)) != W25Q_OK) {
        printf("Write failed\r\n");
        Error_Handler();
    }

    uint8_t read_buf[64] = {0};

    if (W25Q_Read(&flash, ADDR, read_buf, sizeof(read_buf)) != W25Q_OK) {
        printf("Read failed\r\n");
        Error_Handler();
    }

    if (memcmp(write_buf, read_buf, sizeof(write_buf)) == 0) {
        printf("Verification PASS\r\n");
    } else {
        printf("Verification FAIL\r\n");
    }

    while (1) {}
}
