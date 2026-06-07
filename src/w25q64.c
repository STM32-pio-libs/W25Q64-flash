#include "w25q64.h"

#include <string.h>

/* -------------------------------------------------------------------------
 * Internal command opcodes
 * ------------------------------------------------------------------------- */
#define CMD_WRITE_ENABLE        0x06U
#define CMD_WRITE_DISABLE       0x04U
#define CMD_READ_STATUS_REG1    0x05U
#define CMD_READ_DATA           0x03U
#define CMD_PAGE_PROGRAM        0x02U
#define CMD_SECTOR_ERASE_4K     0x20U
#define CMD_BLOCK_ERASE_32K     0x52U
#define CMD_BLOCK_ERASE_64K     0xD8U
#define CMD_CHIP_ERASE          0xC7U
#define CMD_POWER_DOWN          0xB9U
#define CMD_RELEASE_POWER_DOWN  0xABU
#define CMD_JEDEC_ID            0x9FU

/* Status register bit masks */
#define SR1_BUSY                0x01U   /* Erase/Write in progress */
#define SR1_WEL                 0x02U   /* Write Enable Latch      */

/* Maximum time (ms) to wait for a sector erase to finish.
 * Datasheet worst case is 400 ms; we add margin.                  */
#define W25Q_SECTOR_ERASE_TIMEOUT_MS    500U
#define W25Q_BLOCK32_ERASE_TIMEOUT_MS   1700U
#define W25Q_BLOCK64_ERASE_TIMEOUT_MS   2100U
#define W25Q_CHIP_ERASE_TIMEOUT_MS      120000U
#define W25Q_PAGE_PROG_TIMEOUT_MS       5U

/* -------------------------------------------------------------------------
 * Private helpers
 * ------------------------------------------------------------------------- */

static uint8_t w25q_is_valid_config(const W25Q_Config *cfg)
{
    if (cfg == NULL) {
        return 0U;
    }
    return ((cfg->transfer_fn != NULL) && (cfg->delay_ms_fn != NULL)) ? 1U : 0U;
}

/*
 * w25q_transfer – thin wrapper that forwards the call to the user callback.
 * tx and rx may be NULL when the caller does not care about one direction,
 * but the underlying HAL call always needs both buffers.  We handle that by
 * providing a small scratch buffer for the direction the caller ignores.
 *
 * NOTE: to keep stack usage bounded, callers that exchange more than a few
 * bytes build their own tx/rx arrays and call transfer_fn directly.
 */
static int32_t w25q_transfer(const W25Q_Config *cfg,
                              const uint8_t *tx,
                              uint8_t       *rx,
                              size_t         length)
{
    return cfg->transfer_fn(cfg->user_context, tx, rx, length);
}

/* Send a single opcode that carries no address and no data payload. */
static int32_t w25q_cmd_only(const W25Q_Config *cfg, uint8_t opcode)
{
    uint8_t tx[1] = { opcode };
    uint8_t rx[1];
    return w25q_transfer(cfg, tx, rx, sizeof(tx));
}

/* Build a 4-byte [opcode, A23..A16, A15..A8, A7..A0] command frame. */
static void w25q_build_addr_cmd(uint8_t *frame, uint8_t opcode, uint32_t addr)
{
    frame[0] = opcode;
    frame[1] = (uint8_t)((addr >> 16U) & 0xFFU);
    frame[2] = (uint8_t)((addr >>  8U) & 0xFFU);
    frame[3] = (uint8_t)( addr         & 0xFFU);
}

/* -------------------------------------------------------------------------
 * Identification
 * ------------------------------------------------------------------------- */

int32_t W25Q_ReadJEDECID(const W25Q_Config *cfg,
                          uint8_t *manufacturer_id,
                          uint8_t *memory_type,
                          uint8_t *capacity)
{
    uint8_t tx[4] = { CMD_JEDEC_ID, 0xFFU, 0xFFU, 0xFFU };
    uint8_t rx[4] = { 0U };

    if ((w25q_is_valid_config(cfg) == 0U) ||
        (manufacturer_id == NULL) ||
        (memory_type     == NULL) ||
        (capacity        == NULL))
    {
        return W25Q_ERR_INVALID_ARG;
    }

    if (w25q_transfer(cfg, tx, rx, sizeof(tx)) != W25Q_OK) {
        return W25Q_ERR_IO;
    }

    /* rx[0] is the dummy byte clocked out during the command phase */
    *manufacturer_id = rx[1];
    *memory_type     = rx[2];
    *capacity        = rx[3];

    return W25Q_OK;
}

/* -------------------------------------------------------------------------
 * Status register
 * ------------------------------------------------------------------------- */

int32_t W25Q_ReadStatusReg1(const W25Q_Config *cfg, uint8_t *status)
{
    uint8_t tx[2] = { CMD_READ_STATUS_REG1, 0xFFU };
    uint8_t rx[2] = { 0U };

    if ((w25q_is_valid_config(cfg) == 0U) || (status == NULL)) {
        return W25Q_ERR_INVALID_ARG;
    }

    if (w25q_transfer(cfg, tx, rx, sizeof(tx)) != W25Q_OK) {
        return W25Q_ERR_IO;
    }

    *status = rx[1];
    return W25Q_OK;
}

int32_t W25Q_WaitBusy(const W25Q_Config *cfg, uint32_t timeout_ms)
{
    uint8_t  status      = 0U;
    uint32_t elapsed_ms  = 0U;
    int32_t  ret;

    if (w25q_is_valid_config(cfg) == 0U) {
        return W25Q_ERR_INVALID_ARG;
    }

    for (;;) {
        ret = W25Q_ReadStatusReg1(cfg, &status);
        if (ret != W25Q_OK) {
            return W25Q_ERR_IO;
        }

        if ((status & SR1_BUSY) == 0U) {
            return W25Q_OK;
        }

        cfg->delay_ms_fn(cfg->user_context, 1U);
        elapsed_ms++;

        if ((timeout_ms > 0U) && (elapsed_ms >= timeout_ms)) {
            return W25Q_ERR_TIMEOUT;
        }
    }
}

/* -------------------------------------------------------------------------
 * Write control
 * ------------------------------------------------------------------------- */

int32_t W25Q_WriteEnable(const W25Q_Config *cfg)
{
    if (w25q_is_valid_config(cfg) == 0U) {
        return W25Q_ERR_INVALID_ARG;
    }

    return (w25q_cmd_only(cfg, CMD_WRITE_ENABLE) == W25Q_OK)
               ? W25Q_OK
               : W25Q_ERR_IO;
}

int32_t W25Q_WriteDisable(const W25Q_Config *cfg)
{
    if (w25q_is_valid_config(cfg) == 0U) {
        return W25Q_ERR_INVALID_ARG;
    }

    return (w25q_cmd_only(cfg, CMD_WRITE_DISABLE) == W25Q_OK)
               ? W25Q_OK
               : W25Q_ERR_IO;
}

/* -------------------------------------------------------------------------
 * Read
 * ------------------------------------------------------------------------- */

int32_t W25Q_Read(const W25Q_Config *cfg,
                  uint32_t  addr,
                  uint8_t  *buf,
                  size_t    length)
{
    /*
     * The READ DATA command requires [0x03, A23, A15, A7] followed
     * immediately by the data bytes, ALL under one continuous CS assertion.
     * Because the callback toggles CS once per call, we must send the
     * 4-byte command header and the data payload as a single flat buffer.
     *
     * To keep stack usage bounded we cap each transfer at 256 data bytes
     * (+ 4 header = 260 bytes total) and loop, issuing a fresh command for
     * each chunk.  The chip supports auto-increment across page boundaries
     * so this is safe for any address and any length.
     *
     * Layout of frame_tx / frame_rx (260 bytes max):
     *   [0]      opcode  (0x03)
     *   [1]      A23..16
     *   [2]      A15..8
     *   [3]      A7..0
     *   [4..N]   0xFF dummy clocks  →  rx bytes land in frame_rx[4..N]
     */
    /* 4 cmd bytes + up to 256 data bytes */
    uint8_t  frame_tx[4U + W25Q64_PAGE_SIZE];
    uint8_t  frame_rx[4U + W25Q64_PAGE_SIZE];
    size_t   remaining;
    size_t   chunk;
    uint32_t cur_addr;
    int32_t  ret;

    if ((w25q_is_valid_config(cfg) == 0U) || (buf == NULL)) {
        return W25Q_ERR_INVALID_ARG;
    }

    if (length == 0U) {
        return W25Q_OK;
    }

    remaining = length;
    cur_addr  = addr;

    while (remaining > 0U) {
        chunk = (remaining > W25Q64_PAGE_SIZE) ? W25Q64_PAGE_SIZE : remaining;

        /* Build combined frame: command header + dummy TX bytes */
        w25q_build_addr_cmd(frame_tx, CMD_READ_DATA, cur_addr);
        memset(&frame_tx[4], 0xFFU, chunk);

        ret = w25q_transfer(cfg, frame_tx, frame_rx, 4U + chunk);
        if (ret != W25Q_OK) {
            return W25Q_ERR_IO;
        }

        /* Data bytes begin at offset 4 in the RX frame */
        memcpy(buf, &frame_rx[4], chunk);

        buf       += chunk;
        cur_addr  += (uint32_t)chunk;
        remaining -= chunk;
    }

    return W25Q_OK;
}

/* -------------------------------------------------------------------------
 * Write (program)
 * ------------------------------------------------------------------------- */

int32_t W25Q_PageProgram(const W25Q_Config *cfg,
                          uint32_t addr,
                          const uint8_t *data,
                          size_t length)
{
    /*
     * Full transaction = [CMD_PAGE_PROGRAM, A23, A15, A7] + up to 256 data
     * bytes, all under a single CS assertion.
     *
     * The callback toggles CS once per call, so we build the entire frame
     * (cmd + data, max 260 bytes) on the stack and send it in one shot.
     */
    uint8_t  frame[4U + W25Q64_PAGE_SIZE];
    uint8_t  rx[4U + W25Q64_PAGE_SIZE];
    int32_t  ret;

    if ((w25q_is_valid_config(cfg) == 0U) || (data == NULL)) {
        return W25Q_ERR_INVALID_ARG;
    }

    if ((length == 0U) || (length > W25Q64_PAGE_SIZE)) {
        return W25Q_ERR_INVALID_ARG;
    }

    /* Guard: write must not cross a page boundary (chip wraps, not advances). */
    {
        uint32_t page_start = addr & ~((uint32_t)(W25Q64_PAGE_SIZE - 1U));
        if ((addr + (uint32_t)length) > (page_start + W25Q64_PAGE_SIZE)) {
            return W25Q_ERR_NOT_ALIGNED;
        }
    }

    ret = W25Q_WriteEnable(cfg);
    if (ret != W25Q_OK) {
        return ret;
    }

    w25q_build_addr_cmd(frame, CMD_PAGE_PROGRAM, addr);
    memcpy(&frame[4], data, length);

    ret = w25q_transfer(cfg, frame, rx, 4U + length);
    if (ret != W25Q_OK) {
        return W25Q_ERR_IO;
    }

    return W25Q_WaitBusy(cfg, W25Q_PAGE_PROG_TIMEOUT_MS);
}

int32_t W25Q_Write(const W25Q_Config *cfg,
                   uint32_t addr,
                   const uint8_t *data,
                   size_t length)
{
    /*
     * Split the write into page-sized chunks, respecting page boundaries.
     * The caller is responsible for erasing before writing.
     *
     * For each iteration:
     *   - Calculate how many bytes remain until the end of the current page.
     *   - Program that chunk (or fewer bytes if that is all that remains).
     *   - Advance addr and data pointer.
     *
     * W25Q_PageProgram handles any start offset within a page; the only
     * invariant it enforces is that the write does not cross a page boundary.
     */
    size_t         chunk;
    const uint8_t *src;
    size_t         remaining;
    int32_t        ret;

    if ((w25q_is_valid_config(cfg) == 0U) || (data == NULL)) {
        return W25Q_ERR_INVALID_ARG;
    }

    if (length == 0U) {
        return W25Q_OK;
    }

    remaining = length;
    src       = data;

    while (remaining > 0U) {
        /* Bytes left until the end of the current 256-byte page */
        uint32_t page_start  = addr & ~((uint32_t)(W25Q64_PAGE_SIZE - 1U));
        size_t   bytes_to_page_end = (size_t)(page_start + W25Q64_PAGE_SIZE - addr);

        chunk = (remaining < bytes_to_page_end) ? remaining : bytes_to_page_end;

        ret = W25Q_PageProgram(cfg, addr, src, chunk);
        if (ret != W25Q_OK) {
            return ret;
        }

        addr      += (uint32_t)chunk;
        src       += chunk;
        remaining -= chunk;
    }

    return W25Q_OK;
}

/* -------------------------------------------------------------------------
 * Erase helpers (shared implementation)
 * ------------------------------------------------------------------------- */

static int32_t w25q_erase(const W25Q_Config *cfg,
                           uint8_t  opcode,
                           uint32_t addr,
                           uint32_t alignment_mask,
                           uint32_t timeout_ms)
{
    uint8_t frame[4];
    uint8_t rx[4];
    int32_t ret;

    if ((addr & alignment_mask) != 0U) {
        return W25Q_ERR_NOT_ALIGNED;
    }

    ret = W25Q_WriteEnable(cfg);
    if (ret != W25Q_OK) {
        return ret;
    }

    w25q_build_addr_cmd(frame, opcode, addr);

    ret = w25q_transfer(cfg, frame, rx, sizeof(frame));
    if (ret != W25Q_OK) {
        return W25Q_ERR_IO;
    }

    return W25Q_WaitBusy(cfg, timeout_ms);
}

/* -------------------------------------------------------------------------
 * Erase – public API
 * ------------------------------------------------------------------------- */

int32_t W25Q_EraseSector(const W25Q_Config *cfg, uint32_t addr)
{
    if (w25q_is_valid_config(cfg) == 0U) {
        return W25Q_ERR_INVALID_ARG;
    }

    return w25q_erase(cfg,
                      CMD_SECTOR_ERASE_4K,
                      addr,
                      W25Q64_SECTOR_SIZE - 1U,
                      W25Q_SECTOR_ERASE_TIMEOUT_MS);
}

int32_t W25Q_EraseBlock32(const W25Q_Config *cfg, uint32_t addr)
{
    if (w25q_is_valid_config(cfg) == 0U) {
        return W25Q_ERR_INVALID_ARG;
    }

    return w25q_erase(cfg,
                      CMD_BLOCK_ERASE_32K,
                      addr,
                      W25Q64_BLOCK32_SIZE - 1U,
                      W25Q_BLOCK32_ERASE_TIMEOUT_MS);
}

int32_t W25Q_EraseBlock64(const W25Q_Config *cfg, uint32_t addr)
{
    if (w25q_is_valid_config(cfg) == 0U) {
        return W25Q_ERR_INVALID_ARG;
    }

    return w25q_erase(cfg,
                      CMD_BLOCK_ERASE_64K,
                      addr,
                      W25Q64_BLOCK64_SIZE - 1U,
                      W25Q_BLOCK64_ERASE_TIMEOUT_MS);
}

int32_t W25Q_EraseChip(const W25Q_Config *cfg)
{
    int32_t ret;

    if (w25q_is_valid_config(cfg) == 0U) {
        return W25Q_ERR_INVALID_ARG;
    }

    ret = W25Q_WriteEnable(cfg);
    if (ret != W25Q_OK) {
        return ret;
    }

    ret = w25q_cmd_only(cfg, CMD_CHIP_ERASE);
    if (ret != W25Q_OK) {
        return W25Q_ERR_IO;
    }

    return W25Q_WaitBusy(cfg, W25Q_CHIP_ERASE_TIMEOUT_MS);
}

/* -------------------------------------------------------------------------
 * Power management
 * ------------------------------------------------------------------------- */

int32_t W25Q_PowerDown(const W25Q_Config *cfg)
{
    if (w25q_is_valid_config(cfg) == 0U) {
        return W25Q_ERR_INVALID_ARG;
    }

    return (w25q_cmd_only(cfg, CMD_POWER_DOWN) == W25Q_OK)
               ? W25Q_OK
               : W25Q_ERR_IO;
}

int32_t W25Q_ReleasePowerDown(const W25Q_Config *cfg)
{
    if (w25q_is_valid_config(cfg) == 0U) {
        return W25Q_ERR_INVALID_ARG;
    }

    return (w25q_cmd_only(cfg, CMD_RELEASE_POWER_DOWN) == W25Q_OK)
               ? W25Q_OK
               : W25Q_ERR_IO;
}
