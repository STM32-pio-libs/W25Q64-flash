#ifndef W25Q64_H
#define W25Q64_H

#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * Return codes
 * ------------------------------------------------------------------------- */
#define W25Q_OK                  0
#define W25Q_ERR_INVALID_ARG    (-1)
#define W25Q_ERR_IO             (-2)
#define W25Q_ERR_TIMEOUT        (-3)
#define W25Q_ERR_NOT_ALIGNED    (-4)

/* -------------------------------------------------------------------------
 * Geometry constants (W25Q64: 8 MB)
 * ------------------------------------------------------------------------- */
#define W25Q64_PAGE_SIZE        256U        /* bytes per page             */
#define W25Q64_SECTOR_SIZE      4096U       /* bytes per sector (4 KB)    */
#define W25Q64_BLOCK32_SIZE     (32U*1024U) /* bytes per 32 KB block      */
#define W25Q64_BLOCK64_SIZE     (64U*1024U) /* bytes per 64 KB block      */
#define W25Q64_TOTAL_SIZE       (8U*1024U*1024U) /* 8 MB                  */
#define W25Q64_SECTOR_COUNT     (W25Q64_TOTAL_SIZE / W25Q64_SECTOR_SIZE)
#define W25Q64_PAGE_COUNT       (W25Q64_TOTAL_SIZE / W25Q64_PAGE_SIZE)

/* -------------------------------------------------------------------------
 * SPI transfer callback
 *
 * The library is bus-agnostic. You supply one function that performs a
 * full-duplex SPI transfer:
 *
 *   - Drive CS low before calling HAL_SPI_TransmitReceive.
 *   - Drive CS high after the transfer completes.
 *   - tx and rx may point to the same buffer (in-place is fine).
 *   - Return W25Q_OK on success, W25Q_ERR_IO on failure.
 * ------------------------------------------------------------------------- */
typedef int32_t (*W25Q_SPITransferFn)(void          *user_context,
                                      const uint8_t *tx,
                                      uint8_t       *rx,
                                      size_t         length);

/* -------------------------------------------------------------------------
 * Millisecond delay callback
 *
 * Called by W25Q_WaitBusy between each BUSY poll (1 ms per interval).
 * Usually wraps HAL_Delay().  user_context is the same pointer stored in
 * W25Q_Config.
 * ------------------------------------------------------------------------- */
typedef void (*W25Q_DelayMsFn)(void *user_context, uint32_t delay_ms);

/* -------------------------------------------------------------------------
 * Driver configuration  (pass by pointer everywhere; do not copy)
 * ------------------------------------------------------------------------- */
typedef struct {
    W25Q_SPITransferFn transfer_fn;   /* required: full-duplex SPI callback  */
    W25Q_DelayMsFn     delay_ms_fn;   /* required: millisecond delay callback */
    void              *user_context;  /* forwarded to both callbacks as-is   */
} W25Q_Config;

/* -------------------------------------------------------------------------
 * Identification
 * ------------------------------------------------------------------------- */

/*
 * W25Q_ReadJEDECID – read manufacturer + device ID.
 *
 * Expected for W25Q64:
 *   manufacturer_id = 0xEF
 *   memory_type     = 0x40
 *   capacity        = 0x17
 */
int32_t W25Q_ReadJEDECID(const W25Q_Config *cfg,
                          uint8_t *manufacturer_id,
                          uint8_t *memory_type,
                          uint8_t *capacity);

/* -------------------------------------------------------------------------
 * Status register
 * ------------------------------------------------------------------------- */

/* Read status register 1 (contains BUSY and WEL bits). */
int32_t W25Q_ReadStatusReg1(const W25Q_Config *cfg, uint8_t *status);

/*
 * W25Q_WaitBusy – poll the BUSY bit until clear or timeout expires.
 * timeout_ms = 0 means wait forever.
 */
int32_t W25Q_WaitBusy(const W25Q_Config *cfg, uint32_t timeout_ms);

/* -------------------------------------------------------------------------
 * Write control
 * ------------------------------------------------------------------------- */

/* Send Write Enable (WREN) – required before every program / erase. */
int32_t W25Q_WriteEnable(const W25Q_Config *cfg);

/* Send Write Disable (WRDI). */
int32_t W25Q_WriteDisable(const W25Q_Config *cfg);

/* -------------------------------------------------------------------------
 * Read
 * ------------------------------------------------------------------------- */

/*
 * W25Q_Read – read `length` bytes starting at `addr`.
 *
 * `addr` and `length` have no alignment restriction.
 * Reads may span page and sector boundaries freely.
 */
int32_t W25Q_Read(const W25Q_Config *cfg,
                  uint32_t addr,
                  uint8_t *buf,
                  size_t   length);

/* -------------------------------------------------------------------------
 * Write (program)
 * ------------------------------------------------------------------------- */

/*
 * W25Q_PageProgram – program up to 256 bytes within a single page.
 *
 * Rules (enforced internally):
 *   - addr must be page-aligned (addr % 256 == 0).
 *   - length must be 1..256.
 *   - The target area must have been erased first (0xFF).
 *
 * Internally calls WriteEnable and WaitBusy.
 */
int32_t W25Q_PageProgram(const W25Q_Config *cfg,
                          uint32_t addr,
                          const uint8_t *data,
                          size_t length);

/*
 * W25Q_Write – write an arbitrary buffer at any address.
 *
 * This is the high-level helper you should normally use:
 *   - Splits the write into page-aligned chunks automatically.
 *   - Does NOT erase – call W25Q_EraseSector first.
 */
int32_t W25Q_Write(const W25Q_Config *cfg,
                   uint32_t addr,
                   const uint8_t *data,
                   size_t length);

/* -------------------------------------------------------------------------
 * Erase
 *
 * All erase operations call WriteEnable and WaitBusy internally.
 * Typical durations (from datasheet, worst case):
 *   Sector (4 KB)  : 400 ms
 *   Block  (32 KB) : 1600 ms
 *   Block  (64 KB) : 2000 ms
 *   Chip           : 100 s
 * ------------------------------------------------------------------------- */

/* Erase one 4 KB sector.  addr must be sector-aligned (addr % 4096 == 0). */
int32_t W25Q_EraseSector(const W25Q_Config *cfg, uint32_t addr);

/* Erase one 32 KB block.  addr must be 32 KB-aligned. */
int32_t W25Q_EraseBlock32(const W25Q_Config *cfg, uint32_t addr);

/* Erase one 64 KB block.  addr must be 64 KB-aligned. */
int32_t W25Q_EraseBlock64(const W25Q_Config *cfg, uint32_t addr);

/* Erase the entire chip (~25–100 s). */
int32_t W25Q_EraseChip(const W25Q_Config *cfg);

/* -------------------------------------------------------------------------
 * Power management
 * ------------------------------------------------------------------------- */

/* Enter deep power-down (~1 µA standby). */
int32_t W25Q_PowerDown(const W25Q_Config *cfg);

/* Wake from deep power-down (tRES1 = 3 µs; caller must wait before use). */
int32_t W25Q_ReleasePowerDown(const W25Q_Config *cfg);

#endif /* W25Q64_H */
