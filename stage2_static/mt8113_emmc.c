#include <stdint.h>
#include <stdbool.h>

#include "printf.h"
#include "mt8113_emmc.h"

// ========== MSDC Register Definitions ==========
#define MSDC_BASE         0x11230000
#define _MSDC_CFG         (0x00/4)
#define _MSDC_IOCON       (0x04/4)
#define _MSDC_PS          (0x08/4)
#define _MSDC_INT         (0x0C/4)
#define _MSDC_INTEN       (0x10/4)
#define _MSDC_FIFOCS      (0x14/4)
#define _MSDC_TXDATA      (0x18/4)
#define _MSDC_RXDATA      (0x1C/4)
#define _SDC_CFG          (0x30/4)
#define _SDC_CMD          (0x34/4)
#define _SDC_ARG          (0x38/4)
#define _SDC_STS          (0x3C/4)
#define _SDC_RESP0        (0x40/4)
#define _SDC_RESP1        (0x44/4)
#define _SDC_RESP2        (0x48/4)
#define _SDC_RESP3        (0x4C/4)
#define _SDC_BLK_NUM      (0x50/4)
#define _SDC_ADV_CFG0     (0x64/4)
#define _PATCH_BIT0       (0xB0/4)
#define _PATCH_BIT1       (0xB4/4)
#define _PATCH_BIT2       (0xB8/4)
#define _PAD_TUNE0        (0xEC/4)
#define _DAT_RD_DLY0      (0xF0/4)
#define _DAT_RD_DLY1      (0xF4/4)
#define _EMMC50_CFG0      (0x208/4)
#define _EMMC50_CFG1      (0x20C/4)
#define _EMMC50_BLOCK_LEN (0x228/4)

#define EMMC_PART_USER    0
#define EMMC_PART_BOOT0   1
#define EMMC_PART_BOOT1   2

// MSDC_INT bits
#define INT_CMDRDY        (1 << 8)
#define INT_DATCRCERR     (1 << 5)
#define INT_DATTMO        (1 << 4)
#define INT_XFER_COMPL    (1 << 3)
#define INT_DATA_BITS     (0x38)

// SDC_CMD encoding
#define CMD_R1_RESP       (1 << 7)
#define CMD_R1B_RESP      (7 << 7)
#define CMD_R2_RESP       (2 << 7)
#define CMD_R3_RESP       (3 << 7)
#define CMD_SINGLE_BLK    (1 << 11)
#define CMD_WRITE         (1 << 13)
#define CMD_BLKLEN(x)     ((x) << 16)

static volatile uint32_t *msdc = (volatile uint32_t*)MSDC_BASE;
static uint32_t current_partition = 0xFF;
extern void dump_u32_bytes(const char *label, uint32_t v);

// ========== Low-level helpers ==========

void msdc_wait_cmd_ready(void) {
    while (msdc[_SDC_STS] & 0x2);
}

void msdc_clear_fifo(void) {
    msdc[_MSDC_FIFOCS] = 0x80000000;
    while (msdc[_MSDC_FIFOCS] & 0x80000000);
}

int msdc_wait_int(uint32_t mask, uint32_t timeout_val) {
    while (timeout_val-- > 0) {
        if (msdc[_MSDC_INT] & mask) return 0;
    }
    return -1;
}

int msdc_send_cmd(uint8_t cmd_idx, uint32_t arg, uint32_t flags) {
    msdc_wait_cmd_ready();
    msdc[_MSDC_INT] = 0xFFFFFFFF;
    msdc[_SDC_ARG] = arg;
    msdc[_SDC_CMD] = cmd_idx | flags;
    return msdc_wait_int(INT_CMDRDY, 100000);
}

int msdc_wait_card_ready(void) {
    for (int i = 0; i < 1000; i++) {
        if (msdc_send_cmd(13, 0x00010000, CMD_R1_RESP) == 0) {
            uint32_t state = (msdc[_SDC_RESP0] >> 9) & 0xF;
            if (state == 4) return 0;
        }
        for (volatile int d = 0; d < 1000; d++);
    }
    return -1;
}

void emmc_init(void) {
    int retry;
    
    printf("=== eMMC Init ===\n");
    
    // Controller reset
    msdc[_MSDC_CFG] |= 0xF;
    for (retry = 5000; retry > 0; retry--) {
        if ((msdc[_MSDC_CFG] & 0x4) == 0) break;
    }
    
    msdc_clear_fifo();
    
    // Hardware configuration
    msdc[_MSDC_INT] = msdc[_MSDC_INT];
    msdc[_PAD_TUNE0] = 0x202000;
    msdc[_DAT_RD_DLY0] = 0;
    msdc[_DAT_RD_DLY1] = 0;
    msdc[_MSDC_IOCON] = 0;
    
    // Silicon bug workarounds
    msdc[_PATCH_BIT0] = 0x403C0046;
    msdc[_PATCH_BIT1] = 0xFFFF4289;
    msdc[_PATCH_BIT2] = 0x10008000;
    
    // eMMC 5.0 config
    msdc[_EMMC50_CFG0] |= 0x10;
    msdc[_EMMC50_BLOCK_LEN] &= 0xFCFFFFFF;
    msdc[_SDC_ADV_CFG0] |= 0x100000;
    
    // IO config
    msdc[_MSDC_IOCON] |= 0xE;
    msdc[_PATCH_BIT0] |= 0x40000000;
    msdc[_MSDC_CFG] &= 0xFE7FFFFF;
    
    // 1-bit bus width
    msdc[_SDC_CFG] &= 0xFFFCFFFF;
    
    // Clock: 260kHz for init
    msdc[_MSDC_CFG] &= 0xFFBFFFFF;
    msdc[_PATCH_BIT2] |= 0x10000000;
    msdc[_MSDC_CFG] = (msdc[_MSDC_CFG] & 0xFFC000FF) | 0x18500;
    while ((msdc[_MSDC_CFG] & 0x80) == 0);
    
    // Timeout config
    msdc[_SDC_CFG] &= 0x00E7FFFD;
    
    // Interrupts
    msdc[_MSDC_INT] = msdc[_MSDC_INT];
    msdc[_MSDC_INTEN] = 0x738;
    
    printf("Controller configured\n");
    
    // CMD0 - GO_IDLE
    msdc_send_cmd(0, 0, 0);
    
    // CMD1 - SEND_OP_COND
    for (retry = 100; retry > 0; retry--) {
        if (msdc_send_cmd(1, 0x40FF8080, CMD_R3_RESP) == 0) {
            if (msdc[_SDC_RESP0] & 0x80000000) {
                printf("eMMC ready\n");
                break;
            }
        }
        for (volatile int d = 0; d < 10000; d++);
    }
    if (retry == 0) {
        printf("CMD1 timeout!\n");
        return;
    }
    
    // CMD2 - ALL_SEND_CID
    if (msdc_send_cmd(2, 0, CMD_R2_RESP) != 0) {
        printf("CMD2 failed\n");
        return;
    }
    
    // CMD3 - SET_RELATIVE_ADDR
    if (msdc_send_cmd(3, 0x00010000, CMD_R1_RESP) != 0) {
        printf("CMD3 failed\n");
        return;
    }
    
    // CMD7 - SELECT_CARD
    if (msdc_send_cmd(7, 0x00010000, CMD_R1B_RESP) != 0) {
        printf("CMD7 failed\n");
        return;
    }
    
    // Increase clock to 25MHz
    msdc[_MSDC_CFG] = (msdc[_MSDC_CFG] & 0xFFC000FF) | (0x4 << 8);
    while ((msdc[_MSDC_CFG] & 0x80) == 0);
    
    if (msdc_wait_card_ready() != 0) {
        printf("Card not ready!\n");
        return;
    }
    
    printf("=== eMMC Init Complete ===\n");
    current_partition = EMMC_PART_USER;
}

int emmc_switch_partition(uint32_t partition) {
    if (partition == current_partition) return 0;
    
    uint8_t part_config;
    switch (partition) {
        case EMMC_PART_USER:  part_config = 0x48; break;
        case EMMC_PART_BOOT0: part_config = 0x49; break;
        case EMMC_PART_BOOT1: part_config = 0x4A; break;
        default: return -1;
    }
    
    uint32_t arg = 0x03B30000 | (part_config << 8);
    if (msdc_send_cmd(6, arg, CMD_R1B_RESP) != 0) {
        printf("Partition switch failed\n");
        return -1;
    }
    
    if (msdc_wait_card_ready() != 0) {
        printf("Card not ready after switch\n");
        return -1;
    }
    
    current_partition = partition;
    return 0;
}

int emmc_read_block(uint32_t partition, uint32_t block_num, uint32_t *buffer) {
    if (emmc_switch_partition(partition) != 0) return -1;
    
    msdc_clear_fifo();
    
    // CMD17 - READ_SINGLE_BLOCK
    if (msdc_send_cmd(17, block_num, CMD_R1_RESP | CMD_SINGLE_BLK | CMD_BLKLEN(512)) != 0) {
        printf("CMD17 failed\n");
        return -1;
    }
    
    int words_read = 0;
    uint32_t timeout_val = 100000;
    
    while (timeout_val-- > 0 && words_read < 128) {
        uint32_t fifo_count = msdc[_MSDC_FIFOCS] & 0xFF;
        while (fifo_count >= 4 && words_read < 128) {
            buffer[words_read++] = msdc[_MSDC_RXDATA];
            fifo_count = msdc[_MSDC_FIFOCS] & 0xFF;
        }
        if (msdc[_MSDC_INT] & INT_DATA_BITS) break;
    }
    
    uint32_t int_status = msdc[_MSDC_INT];
    msdc[_MSDC_INT] = int_status;
    
    if ((int_status & (INT_DATCRCERR | INT_DATTMO)) || words_read < 128) {
        printf("Read error\n");
        return -1;
    }
    
    return 0;
}

int emmc_write_block(uint32_t partition, uint32_t block_num, uint32_t *buffer) {
    if (emmc_switch_partition(partition) != 0) return -1;
    
    msdc_clear_fifo();
    
    // CMD24 - WRITE_SINGLE_BLOCK
    if (msdc_send_cmd(24, block_num, CMD_R1_RESP | CMD_SINGLE_BLK | CMD_WRITE | CMD_BLKLEN(512)) != 0) {
        printf("CMD24 failed\n");
        return -1;
    }
    
    // Write 128 words to FIFO
    for (int i = 0; i < 128; i++) {
        msdc[_MSDC_TXDATA] = buffer[i];
    }
    
    // Wait for transfer complete
    if (msdc_wait_int(INT_XFER_COMPL, 100000) != 0) {
        printf("Write timeout\n");
        return -1;
    }
    
    uint32_t int_status = msdc[_MSDC_INT];
    msdc[_MSDC_INT] = int_status;
    
    if (int_status & (INT_DATCRCERR | INT_DATTMO)) {
        printf("Write error\n");
        return -1;
    }
    
    // Wait for card to finish programming
    if (msdc_wait_card_ready() != 0) {
        printf("Card busy after write\n");
        return -1;
    }
    
    return 0;
}

int buffers_equal(uint32_t *a, uint32_t *b, int words) {
    for (int i = 0; i < words; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

void emmc_roundtrip_test(void) {
    // Use a high block number unlikely to contain critical data
    // Block 0x100000 = 512MB offset (assuming 512-byte blocks)
    const uint32_t TEST_BLOCK = 0x100000;
    
    uint32_t original[128];
    uint32_t test_pattern[128];
    uint32_t readback[128];
    
    printf("\n=== eMMC Roundtrip Test ===\n");
    dump_u32_bytes("Test block", TEST_BLOCK);
    
    emmc_init();
    
    // Step 1: Read original contents
    printf("\n[1] Reading original contents...\n");
    if (emmc_read_block(EMMC_PART_USER, TEST_BLOCK, original) != 0) {
        printf("FAILED: Could not read original block\n");
        return;
    }
    dump_u32_bytes("Original first word", original[0]);
    
    // Step 2: Create and write test pattern
    printf("\n[2] Writing test pattern...\n");
    for (int i = 0; i < 128; i++) {
        test_pattern[i] = 0xDEADBEEF;
    }
    if (emmc_write_block(EMMC_PART_USER, TEST_BLOCK, test_pattern) != 0) {
        printf("FAILED: Could not write test pattern\n");
        return;
    }
    printf("Write complete\n");
    
    // Step 3: Read back and verify test pattern
    printf("\n[3] Verifying test pattern...\n");
    if (emmc_read_block(EMMC_PART_USER, TEST_BLOCK, readback) != 0) {
        printf("FAILED: Could not read back test pattern\n");
        return;
    }
    if (!buffers_equal(test_pattern, readback, 128)) {
        printf("FAILED: Test pattern mismatch!\n");
        dump_u32_bytes("Expected", test_pattern[0]);
        dump_u32_bytes("Got", readback[0]);
        return;
    }
    printf("Test pattern verified OK\n");
    
    // Step 4: Restore original contents
    printf("\n[4] Restoring original contents...\n");
    if (emmc_write_block(EMMC_PART_USER, TEST_BLOCK, original) != 0) {
        printf("FAILED: Could not restore original\n");
        return;
    }
    printf("Restore complete\n");
    
    // Step 5: Verify original restored
    printf("\n[5] Verifying restoration...\n");
    if (emmc_read_block(EMMC_PART_USER, TEST_BLOCK, readback) != 0) {
        printf("FAILED: Could not read back original\n");
        return;
    }
    if (!buffers_equal(original, readback, 128)) {
        printf("FAILED: Original not restored correctly!\n");
        dump_u32_bytes("Expected", original[0]);
        dump_u32_bytes("Got", readback[0]);
        return;
    }
    printf("Original restored OK\n");
    
    printf("\n=== Roundtrip Test PASSED ===\n");
}
