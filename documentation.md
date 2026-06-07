# W25Q64 Driver Documentation

## Overview

This library drives W25Q64 (and pin-compatible clones) SPI NOR flash chips.
The driver is transport-agnostic: it does not include STM32 HAL headers and
does not depend on a specific `SPI_HandleTypeDef`.

Your application provides two callbacks: an SPI transfer function and a
millisecond delay function. The driver handles all command framing, Write
Enable sequencing, and busy-polling internally.

Compatible parts (same command set, same geometry):

| Manufacturer | Part number  | JEDEC ID     |
|---|---|---|
| Winbond      | W25Q64JV     | EF 40 17     |
| XMC          | XM25QH64C    | 20 70 17     |
| BOYA         | BY25Q64AS    | 68 40 17     |

> **Note:** JEDEC manufacturer ID `0xEF` is Winbond. If your chip is marked
> W25Q64 but reports a different manufacturer byte, it is a compatible clone.
> The command set is identical; the driver works regardless.

---

## Status Codes

| Constant | Value | Meaning |
|---|---|---|
| `W25Q_OK` | `0` | Success |
| `W25Q_ERR_INVALID_ARG` | `-1` | NULL pointer or out-of-range argument |
| `W25Q_ERR_IO` | `-2` | SPI transfer callback returned failure |
| `W25Q_ERR_TIMEOUT` | `-3` | `W25Q_WaitBusy` timed out |
| `W25Q_ERR_NOT_ALIGNED` | `-4` | Address not aligned to erase/page boundary |

---

## Geometry Constants

These reflect the physical layout of the W25Q64 (8 MB):

| Constant | Value | Description |
|---|---|---|
| `W25Q64_PAGE_SIZE` | `256` | Bytes per page (minimum program unit) |
| `W25Q64_SECTOR_SIZE` | `4096` | Bytes per sector (minimum erase unit) |
| `W25Q64_BLOCK32_SIZE` | `32768` | Bytes per 32 KB block |
| `W25Q64_BLOCK64_SIZE` | `65536` | Bytes per 64 KB block |
| `W25Q64_TOTAL_SIZE` | `8388608` | Total flash capacity (8 MB) |
| `W25Q64_SECTOR_COUNT` | `2048` | Total number of 4 KB sectors |
| `W25Q64_PAGE_COUNT` | `32768` | Total number of 256-byte pages |

---

## Types

### `W25Q_SPITransferFn`

```c
typedef int32_t (*W25Q_SPITransferFn)(void          *user_context,
                                      const uint8_t *tx,
                                      uint8_t       *rx,
                                      size_t         length);
```

Contract:

- Performs one full-duplex SPI transaction of exactly `length` bytes.
- **Must assert CS (low) before the transfer and deassert (high) after**,
  even on error. The driver issues one callback call per complete command
  frame; splitting a frame across two calls will corrupt the transaction.
- `tx` and `rx` are always valid pointers to buffers of `length` bytes
  allocated by the driver. The caller never passes NULL.
- Return `W25Q_OK` on success, `W25Q_ERR_IO` on failure.

### `W25Q_DelayMsFn`

```c
typedef void (*W25Q_DelayMsFn)(void *user_context, uint32_t delay_ms);
```

Contract:

- Called by `W25Q_WaitBusy` between each BUSY poll at 1 ms intervals.
- Usually wraps `HAL_Delay()`.
- `user_context` is the same pointer stored in `W25Q_Config`, forwarded unchanged.
- Must not return until at least `delay_ms` milliseconds have elapsed.

### `W25Q_Config`

```c
typedef struct {
    W25Q_SPITransferFn transfer_fn;
    W25Q_DelayMsFn     delay_ms_fn;
    void              *user_context;
} W25Q_Config;
```

Fields:

- `transfer_fn`: required — full-duplex SPI callback described above.
- `delay_ms_fn`: required — millisecond delay callback described above.
- `user_context`: application-owned pointer passed to both callbacks unchanged.
  Typically a pointer to your HAL `SPI_HandleTypeDef`.

Pass `W25Q_Config` by pointer to every API call. Do not copy the struct.

---

## Public API

### `W25Q_ReadJEDECID`

```c
int32_t W25Q_ReadJEDECID(const W25Q_Config *cfg,
                          uint8_t *manufacturer_id,
                          uint8_t *memory_type,
                          uint8_t *capacity);
```

Reads the three-byte JEDEC identification from the chip.

Expected values for W25Q64:

| Field | Value |
|---|---|
| `manufacturer_id` | `0xEF` (Winbond) / `0x20` (XMC) / `0x68` (BOYA) |
| `memory_type` | `0x40` |
| `capacity` | `0x17` |

Use this on startup to verify the chip is wired and responding before any
read or write operations.

Returns `W25Q_ERR_INVALID_ARG` if any pointer is NULL.

---

### `W25Q_ReadStatusReg1`

```c
int32_t W25Q_ReadStatusReg1(const W25Q_Config *cfg, uint8_t *status);
```

Reads status register 1. Useful bits:

| Bit | Name | Meaning |
|---|---|---|
| 0 | `BUSY` | Erase or program in progress |
| 1 | `WEL` | Write Enable Latch set |

You rarely need to call this directly — `W25Q_WaitBusy` polls it for you.

---

### `W25Q_WaitBusy`

```c
int32_t W25Q_WaitBusy(const W25Q_Config *cfg, uint32_t timeout_ms);
```

Polls the `BUSY` bit in status register 1 at 1 ms intervals until it clears
or `timeout_ms` elapses. The 1 ms sleep between polls is provided by
`delay_ms_fn` from `W25Q_Config`.

- `timeout_ms = 0` — wait forever (no timeout).
- Returns `W25Q_ERR_TIMEOUT` if the chip is still busy after `timeout_ms`.

All write and erase functions call this internally; you do not need to call
it after `W25Q_Write` or `W25Q_EraseSector`. It is exposed for advanced use
cases such as initiating an operation and doing other work before checking
completion.

---

### `W25Q_WriteEnable` / `W25Q_WriteDisable`

```c
int32_t W25Q_WriteEnable(const W25Q_Config *cfg);
int32_t W25Q_WriteDisable(const W25Q_Config *cfg);
```

Send the WREN / WRDI opcode. Both `W25Q_Write` and all erase functions call
`W25Q_WriteEnable` internally; you do not need to call it before normal
write/erase operations. Exposed for completeness and advanced use.

---

### `W25Q_Read`

```c
int32_t W25Q_Read(const W25Q_Config *cfg,
                  uint32_t addr,
                  uint8_t *buf,
                  size_t   length);
```

Reads `length` bytes from flash starting at `addr` into `buf`.

- No alignment restriction on `addr` or `length`.
- Reads may span page and sector boundaries freely.
- Returns immediately; no busy-polling needed (reads are not affected by the
  BUSY flag).
- Returns `W25Q_ERR_INVALID_ARG` if `cfg` or `buf` is NULL.

---

### `W25Q_PageProgram`

```c
int32_t W25Q_PageProgram(const W25Q_Config *cfg,
                          uint32_t addr,
                          const uint8_t *data,
                          size_t length);
```

Low-level program: writes up to 256 bytes within a single page.

Rules enforced internally:

- `length` must be `1..256`.
- The write must not cross a page boundary — `addr` and `addr + length - 1`
  must fall within the same 256-byte page. Returns `W25Q_ERR_NOT_ALIGNED`
  if they do not.
- The target area must have been erased first. NOR flash can only flip bits
  from `1` to `0`; writing into a non-erased area produces corrupt data
  silently. The driver does not check this condition.

Calls `W25Q_WriteEnable` and `W25Q_WaitBusy` internally.

Prefer `W25Q_Write` for general use. Use `W25Q_PageProgram` directly only
when you need precise control over page boundaries.

---

### `W25Q_Write`

```c
int32_t W25Q_Write(const W25Q_Config *cfg,
                   uint32_t addr,
                   const uint8_t *data,
                   size_t length);
```

High-level write: programs an arbitrary buffer at any address and length.

- Splits the buffer into page-aligned chunks automatically.
- `addr` may be unaligned; the driver calculates the correct page boundary.
- **Does not erase.** You must erase the target sector(s) with
  `W25Q_EraseSector` before calling this. Writing into un-erased flash
  produces silent data corruption.

Typical usage sequence:

```c
W25Q_EraseSector(&flash, 0x000000);       // erase sector first
W25Q_Write(&flash, 0x000000, buf, 128);   // then write
W25Q_Read(&flash, 0x000000, out, 128);    // read back
```

---

### Erase functions

```c
int32_t W25Q_EraseSector(const W25Q_Config *cfg, uint32_t addr);
int32_t W25Q_EraseBlock32(const W25Q_Config *cfg, uint32_t addr);
int32_t W25Q_EraseBlock64(const W25Q_Config *cfg, uint32_t addr);
int32_t W25Q_EraseChip(const W25Q_Config *cfg);
```

Erase the region containing `addr`. All functions call `W25Q_WriteEnable`
and block on `W25Q_WaitBusy` before returning.

Alignment requirements:

| Function | `addr` alignment | Erased size |
|---|---|---|
| `W25Q_EraseSector` | 4096 bytes | 4 KB |
| `W25Q_EraseBlock32` | 32768 bytes | 32 KB |
| `W25Q_EraseBlock64` | 65536 bytes | 64 KB |
| `W25Q_EraseChip` | — (no `addr`) | 8 MB |

Returns `W25Q_ERR_NOT_ALIGNED` if `addr` is not aligned to the required
boundary.

Worst-case durations from the datasheet (your chip may be faster):

| Operation | Typical | Max |
|---|---|---|
| Sector erase (4 KB) | 45 ms | 400 ms |
| Block erase (32 KB) | 120 ms | 1600 ms |
| Block erase (64 KB) | 150 ms | 2000 ms |
| Chip erase | 20 s | 100 s |

---

### `W25Q_PowerDown` / `W25Q_ReleasePowerDown`

```c
int32_t W25Q_PowerDown(const W25Q_Config *cfg);
int32_t W25Q_ReleasePowerDown(const W25Q_Config *cfg);
```

Enter and exit deep power-down mode.

- In deep power-down the chip draws approximately 1 µA and ignores all
  commands except Release Power-Down.
- After `W25Q_ReleasePowerDown`, wait at least **3 µs** (t_RES1) before
  issuing any command. The driver does not insert this delay; the caller
  is responsible.

---

## Example Callbacks

```c
#define FLASH_CS_PORT GPIOA
#define FLASH_CS_PIN  GPIO_PIN_4

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
                                                        (uint8_t *)tx,
                                                        rx,
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
```

---

## Example Usage

```c
W25Q_Config flash = {
    .transfer_fn  = flash_spi_transfer,
    .delay_ms_fn  = flash_delay_ms,
    .user_context = &hspi1
};

/* Verify the chip is alive */
uint8_t mfr, mem_type, cap;
W25Q_ReadJEDECID(&flash, &mfr, &mem_type, &cap);
// W25Q64: EF 40 17  /  XMC clone: 20 70 17

/* Erase a sector, write data, read back */
uint8_t write_buf[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
uint8_t read_buf[16]  = {0};

W25Q_EraseSector(&flash, 0x000000);
W25Q_Write(&flash,       0x000000, write_buf, sizeof(write_buf));
W25Q_Read(&flash,        0x000000, read_buf,  sizeof(read_buf));

/* Power saving when flash is not needed */
W25Q_PowerDown(&flash);
HAL_Delay(1);                  /* wait for standby entry */
/* ... time passes ... */
W25Q_ReleasePowerDown(&flash);
HAL_Delay(1);                  /* wait tRES1 >= 3 µs before next command */
```

---

## NOR Flash Rules

NOR flash has constraints that differ from RAM or EEPROM. Violating them
produces silent data corruption with no error returned by the driver:

**Erase before write.** Flash bits can only be programmed from `1` to `0`.
Erasing resets a sector back to `0xFF` (all ones). Writing into a location
that has not been erased will AND your data with the existing content.

**Erase granularity is 4 KB minimum.** There is no way to erase a single
byte or a single page. To update even one byte in a sector you must erase
the whole 4 KB sector. If the rest of the sector contains data you need to
keep, read it out first, modify the byte in RAM, erase, then write the
whole sector back.

**Write granularity is 256 bytes (one page).** `W25Q_Write` handles page
splitting automatically.

**Endurance.** The W25Q64 is rated for 100,000 erase cycles per sector.
Repeatedly erasing the same sector will wear it out. Use a filesystem with
wear levelling (such as LittleFS) for any application that writes frequently.
