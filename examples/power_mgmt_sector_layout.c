/*
 * Example 3 – Power management and multi-sector layout
 *
 * Demonstrates:
 *   - W25Q_PowerDown / W25Q_ReleasePowerDown for low-power idle
 *   - Storing different data types in separate sectors
 *   - W25Q_EraseBlock64 to wipe a larger region in one call
 *
 * Flash layout used in this example:
 *
 *   Sector 0  (0x000000 – 0x000FFF)  device configuration struct
 *   Sector 1  (0x001000 – 0x001FFF)  reserved / unused
 *   ...
 *   Block 1   (0x010000 – 0x01FFFF)  64 KB data log region
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

/* -------------------------------------------------------------------------
 * Application-defined layout
 * ------------------------------------------------------------------------- */
#define ADDR_CONFIG     0x000000UL   /* sector 0: device config (4 KB)  */
#define ADDR_LOG        0x010000UL   /* block  1: data log region (64 KB) */

/* A small config struct that gets persisted to flash */
typedef struct {
    uint32_t magic;         /* 0xDEADBEEF when valid                    */
    uint16_t sample_rate;   /* sensor sample rate in Hz                 */
    uint8_t  node_id;       /* device identifier                        */
    uint8_t  reserved;
} DeviceConfig;

#define CONFIG_MAGIC  0xDEADBEEFUL

int main(void)
{
    /* HAL init, clock config, GPIO and SPI init go here */

    W25Q_Config flash = {
        .transfer_fn  = flash_spi_transfer,
        .delay_ms_fn  = flash_delay_ms,
        .user_context = &hspi1
    };

    /* ------------------------------------------------------------------ */
    /* 1. Write a device config struct to sector 0                         */
    /* ------------------------------------------------------------------ */
    DeviceConfig cfg_write = {
        .magic       = CONFIG_MAGIC,
        .sample_rate = 100U,
        .node_id     = 7U,
        .reserved    = 0U
    };

    /* Erase sector 0 before writing */
    if (W25Q_EraseSector(&flash, ADDR_CONFIG) != W25Q_OK) {
        printf("Config sector erase failed\r\n");
        Error_Handler();
    }

    if (W25Q_Write(&flash, ADDR_CONFIG,
                   (const uint8_t *)&cfg_write, sizeof(cfg_write)) != W25Q_OK) {
        printf("Config write failed\r\n");
        Error_Handler();
    }

    /* Read it back and validate the magic number */
    DeviceConfig cfg_read = {0};

    if (W25Q_Read(&flash, ADDR_CONFIG,
                  (uint8_t *)&cfg_read, sizeof(cfg_read)) != W25Q_OK) {
        printf("Config read failed\r\n");
        Error_Handler();
    }

    if (cfg_read.magic == CONFIG_MAGIC) {
        printf("Config OK – node %u, sample rate %u Hz\r\n",
               cfg_read.node_id, cfg_read.sample_rate);
    } else {
        printf("Config invalid (magic %08lX)\r\n",
               (unsigned long)cfg_read.magic);
    }

    /* ------------------------------------------------------------------ */
    /* 2. Erase the entire 64 KB log block with one call                   */
    /*                                                                     */
    /* W25Q_EraseBlock64 erases 64 KB in one operation, which is faster   */
    /* than calling W25Q_EraseSector 16 times.  Use it when you need to   */
    /* clear a large contiguous region.  addr must be 64 KB-aligned.      */
    /* ------------------------------------------------------------------ */
    if (W25Q_EraseBlock64(&flash, ADDR_LOG) != W25Q_OK) {
        printf("Log block erase failed\r\n");
        Error_Handler();
    }
    printf("Log block erased\r\n");

    /* Write a short log entry at the start of the log region */
    const char *entry = "boot event";
    if (W25Q_Write(&flash, ADDR_LOG,
                   (const uint8_t *)entry, strlen(entry)) != W25Q_OK) {
        printf("Log write failed\r\n");
        Error_Handler();
    }

    /* ------------------------------------------------------------------ */
    /* 3. Enter deep power-down between flash accesses                     */
    /*                                                                     */
    /* In deep power-down the chip draws ~1 µA and ignores all commands   */
    /* except Release Power-Down.  Useful between infrequent log writes.  */
    /* ------------------------------------------------------------------ */
    if (W25Q_PowerDown(&flash) != W25Q_OK) {
        printf("Power-down failed\r\n");
        Error_Handler();
    }
    printf("Flash in deep power-down\r\n");

    /* Simulate doing other work while the flash is asleep */
    HAL_Delay(500U);

    /* Wake the chip.  Must wait at least 3 µs (tRES1) before next use.
     * HAL_Delay(1) gives well over that on any STM32.                    */
    if (W25Q_ReleasePowerDown(&flash) != W25Q_OK) {
        printf("Release power-down failed\r\n");
        Error_Handler();
    }
    HAL_Delay(1U);  /* tRES1 guard – chip ready after this */

    /* Verify the log entry survived the power-down cycle */
    char read_buf[16] = {0};
    W25Q_Read(&flash, ADDR_LOG, (uint8_t *)read_buf, strlen(entry));
    printf("Log entry: %s\r\n", read_buf);

    while (1) {}
}
