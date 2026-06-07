# W25Q64-flash

Portable W25Q64 SPI NOR flash driver for STM32 using STM32Cube HAL, with pluggable SPI transfer and delay callbacks.

## Features

- Transport-agnostic architecture — no hard dependency on a specific STM32 peripheral instance
- Config-driven setup through `W25Q_Config`
- User-supplied SPI transfer callback with full CS control
- User-supplied millisecond delay callback for HAL-agnostic busy polling
- Compatible with Winbond W25Q64 and clones (XMC XM25QH64C, BOYA BY25Q64)
- Read API with no alignment restriction — spans page and sector boundaries freely
- High-level write API (`W25Q_Write`) with automatic page splitting
- Low-level page program (`W25Q_PageProgram`) for precise control
- Full erase suite:
  - `W25Q_EraseSector` (4 KB)
  - `W25Q_EraseBlock32` (32 KB)
  - `W25Q_EraseBlock64` (64 KB)
  - `W25Q_EraseChip` (8 MB)
- Power management (`W25Q_PowerDown`, `W25Q_ReleasePowerDown`)
- JEDEC ID read for startup chip verification

## Project Layout

- `include/w25q64.h` — public API
- `src/w25q64.c` — driver implementation
- `examples/jedec_id_rw_verify.c` — chip identification and read/write/erase verification
- `examples/boot_counter.c` — persistent counter using read-modify-erase-write pattern
- `examples/power_mgmt_sector_layout.c` — power-down usage and multi-sector flash layout
- `documentation.md` — detailed API and usage documentation

## Quick Start

1. Configure your STM32 clock, GPIO, and SPI peripheral.
2. Implement a `W25Q_SPITransferFn` callback that asserts CS, calls `HAL_SPI_TransmitReceive`, and deasserts CS.
3. Implement a `W25Q_DelayMsFn` callback that wraps `HAL_Delay`.
4. Fill `W25Q_Config` with both callbacks and your SPI handle as `user_context`.
5. Call `W25Q_ReadJEDECID` to verify the chip is responding.
6. Erase a sector with `W25Q_EraseSector`, then write with `W25Q_Write` and read with `W25Q_Read`.

### Minimal Flow

```c
static int32_t flash_spi_transfer(void *user_context,
                                  const uint8_t *tx, uint8_t *rx, size_t length)
{
    SPI_HandleTypeDef *spi = (SPI_HandleTypeDef *)user_context;
    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_RESET);
    HAL_StatusTypeDef r = HAL_SPI_TransmitReceive(spi, (uint8_t *)tx, rx,
                                                   (uint16_t)length, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET);
    return (r == HAL_OK) ? W25Q_OK : W25Q_ERR_IO;
}

static void flash_delay_ms(void *user_context, uint32_t delay_ms)
{
    (void)user_context;
    HAL_Delay(delay_ms);
}

W25Q_Config flash = {
    .transfer_fn  = flash_spi_transfer,
    .delay_ms_fn  = flash_delay_ms,
    .user_context = &hspi1
};

uint8_t buf[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

W25Q_EraseSector(&flash, 0x000000);
W25Q_Write(&flash,       0x000000, buf, sizeof(buf));
W25Q_Read(&flash,        0x000000, buf, sizeof(buf));
```

## Return Codes

| Code | Value | Meaning |
|---|---|---|
| `W25Q_OK` | `0` | Success |
| `W25Q_ERR_INVALID_ARG` | `-1` | NULL pointer or out-of-range argument |
| `W25Q_ERR_IO` | `-2` | SPI transfer callback returned failure |
| `W25Q_ERR_TIMEOUT` | `-3` | `W25Q_WaitBusy` timed out |
| `W25Q_ERR_NOT_ALIGNED` | `-4` | Address not aligned to required boundary |

## Notes

- **Erase before write.** NOR flash bits only go `1→0`. Writing into un-erased flash silently corrupts data. Always call `W25Q_EraseSector` (or a block erase) before `W25Q_Write`.
- **CS control is your responsibility.** The `transfer_fn` callback must assert CS low before the transfer and deassert high after — including on error. The driver sends each command as one complete callback call; splitting a call corrupts the transaction.
- **`W25Q_Write` does not erase.** It handles page boundary splitting automatically but assumes the target area is already erased.
- **After `W25Q_ReleasePowerDown`, wait at least 3 µs** (t_RES1) before issuing any command. The driver does not insert this delay.
- For wear-levelled storage with power-loss resilience, use [STM32-pio-libs/littlefs](https://github.com/STM32-pio-libs/littlefs) on top of this driver.

See [documentation.md](documentation.md) for the complete API reference.
