/*
 * Example 2 – Persistent boot counter
 *
 * Demonstrates:
 *   - Storing a small struct at a fixed flash address
 *   - Read-modify-erase-write pattern required for NOR flash
 *   - W25Q_WaitBusy with a timeout
 *
 * A uint32_t counter is stored at the start of sector 0.
 * On every boot the counter is read, incremented, and written back.
 * The value survives power cycles.
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

/*
 * The counter occupies the first 4 bytes of sector 0.
 * A freshly erased sector reads as 0xFFFFFFFF; we treat that as 0.
 *
 * Because the minimum erase unit is a full 4 KB sector, we must:
 *   1. Read the entire sector into RAM.
 *   2. Modify the counter bytes in the RAM copy.
 *   3. Erase the sector.
 *   4. Write the modified RAM copy back.
 *
 * If your application stores only the counter and nothing else in sector 0
 * you can skip the read-back and just write the new value directly after
 * erasing.  The full read-modify-write is shown here as the safe pattern.
 */
#define COUNTER_SECTOR_ADDR  0x000000UL
#define COUNTER_OFFSET       0U

static uint8_t sector_buf[W25Q64_SECTOR_SIZE];

int main(void)
{
    /* HAL init, clock config, GPIO and SPI init go here */

    W25Q_Config flash = {
        .transfer_fn  = flash_spi_transfer,
        .delay_ms_fn  = flash_delay_ms,
        .user_context = &hspi1
    };

    /* ------------------------------------------------------------------ */
    /* 1. Read the whole sector into RAM                                    */
    /* ------------------------------------------------------------------ */
    if (W25Q_Read(&flash, COUNTER_SECTOR_ADDR,
                  sector_buf, sizeof(sector_buf)) != W25Q_OK) {
        printf("Read failed\r\n");
        Error_Handler();
    }

    /* ------------------------------------------------------------------ */
    /* 2. Extract the counter (0xFFFFFFFF on a blank chip → treat as 0)   */
    /* ------------------------------------------------------------------ */
    uint32_t counter;
    memcpy(&counter, &sector_buf[COUNTER_OFFSET], sizeof(counter));

    if (counter == 0xFFFFFFFFUL) {
        counter = 0U;
    }

    counter++;
    printf("Boot count: %lu\r\n", (unsigned long)counter);

    /* ------------------------------------------------------------------ */
    /* 3. Patch the counter bytes in the sector buffer                     */
    /* ------------------------------------------------------------------ */
    memcpy(&sector_buf[COUNTER_OFFSET], &counter, sizeof(counter));

    /* ------------------------------------------------------------------ */
    /* 4. Erase the sector, then write the modified buffer back            */
    /* ------------------------------------------------------------------ */
    if (W25Q_EraseSector(&flash, COUNTER_SECTOR_ADDR) != W25Q_OK) {
        printf("Erase failed\r\n");
        Error_Handler();
    }

    if (W25Q_Write(&flash, COUNTER_SECTOR_ADDR,
                   sector_buf, sizeof(sector_buf)) != W25Q_OK) {
        printf("Write failed\r\n");
        Error_Handler();
    }

    printf("Counter saved\r\n");

    /* ------------------------------------------------------------------ */
    /* 5. Optional: verify the chip is idle before doing anything else     */
    /* ------------------------------------------------------------------ */
    int32_t ret = W25Q_WaitBusy(&flash, 500U); /* 500 ms timeout */
    if (ret == W25Q_ERR_TIMEOUT) {
        printf("Flash still busy after 500 ms\r\n");
    }

    while (1) {}
}
