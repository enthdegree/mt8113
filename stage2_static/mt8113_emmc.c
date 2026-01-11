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

// ========== eMMC initialization ==========

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
    
    dump_u32_bytes("MSDC_CFG before clk", msdc[_MSDC_CFG]);
    msdc[_MSDC_CFG] = (msdc[_MSDC_CFG] & 0xFFC000FF) | 0x18500;
    dump_u32_bytes("MSDC_CFG after clk", msdc[_MSDC_CFG]);
    
    // Wait for clock stable with timeout
    for (int i = 0; i < 100000; i++) {
        if (msdc[_MSDC_CFG] & 0x80) break;
    }
    if (!(msdc[_MSDC_CFG] & 0x80)) {
        printf("Clock not stable!\n");
        dump_u32_bytes("MSDC_CFG", msdc[_MSDC_CFG]);
    }
    
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
    for (int i = 0; i < 100000; i++) {
        if (msdc[_MSDC_CFG] & 0x80) break;
    }
    
    if (msdc_wait_card_ready() != 0) {
        printf("Card not ready!\n");
        return;
    }
    
    printf("=== eMMC Init Complete ===\n");
    current_partition = EMMC_PART_USER;
    
    // Drain any leftover data in FIFO from init sequence
    msdc_clear_fifo();
    while ((msdc[_MSDC_FIFOCS] & 0xFF) > 0) {
        (void)msdc[_MSDC_RXDATA];
    }
    msdc[_MSDC_INT] = 0xFFFFFFFF;
}

// ========== Partition switching ==========

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

// ========== Block read ==========

int emmc_read_block(uint32_t partition, uint32_t block_num, uint32_t *buffer) {
    if (emmc_switch_partition(partition) != 0) return -1;
    
    // Ensure card is ready before issuing read command
    if (msdc_wait_card_ready() != 0) {
        printf("Card not ready before read\n");
        return -1;
    }
    
    // WORKAROUND: First read after partition switch or write returns stale/garbage data.
    // We do every read twice and discard the first result to ensure fresh data.
    // This is wasteful but necessary until root cause is understood.
    for (int attempt = 0; attempt < 2; attempt++) {
        // Clear FIFO and check it's empty
        msdc_clear_fifo();
        uint32_t fifocs = msdc[_MSDC_FIFOCS];
        uint32_t rx_count = fifocs & 0xFF;
        if (rx_count != 0) {
            // Drain it
            while ((msdc[_MSDC_FIFOCS] & 0xFF) > 0) {
                (void)msdc[_MSDC_RXDATA];
            }
        }
        
        // Clear any pending interrupts
        msdc[_MSDC_INT] = 0xFFFFFFFF;
        
        // CMD17 - READ_SINGLE_BLOCK
        if (msdc_send_cmd(17, block_num, CMD_R1_RESP | CMD_SINGLE_BLK | CMD_BLKLEN(512)) != 0) {
            printf("CMD17 failed\n");
            return -1;
        }
        
        // Wait for data to start arriving
        uint32_t timeout_val = 100000;
        while (timeout_val-- > 0) {
            if ((msdc[_MSDC_FIFOCS] & 0xFF) > 0) break;
            if (msdc[_MSDC_INT] & INT_DATA_BITS) break;
        }
        
        // Read 128 words (512 bytes) from FIFO
        int words_read = 0;
        timeout_val = 100000;
        
        while (timeout_val-- > 0 && words_read < 128) {
            uint32_t fifo_count = msdc[_MSDC_FIFOCS] & 0xFF;
            while (fifo_count >= 4 && words_read < 128) {
                buffer[words_read++] = msdc[_MSDC_RXDATA];
                fifo_count = msdc[_MSDC_FIFOCS] & 0xFF;
            }
            if (msdc[_MSDC_INT] & INT_XFER_COMPL) break;
        }
        
        uint32_t int_status = msdc[_MSDC_INT];
        msdc[_MSDC_INT] = int_status;
        
        if ((int_status & (INT_DATCRCERR | INT_DATTMO)) || words_read < 128) {
            printf("Read error\n");
            dump_u32_bytes("INT", int_status);
            dump_u32_bytes("words_read", words_read);
            return -1;
        }
        
        // Wait for card ready before next attempt or return
        if (msdc_wait_card_ready() != 0) {
            printf("Card not ready after read\n");
            return -1;
        }
        
        // First iteration is dummy read, discard and read again
    }
    
    return 0;
}

// ========== Block write ==========

int emmc_write_block(uint32_t partition, uint32_t block_num, uint32_t *buffer) {
    if (emmc_switch_partition(partition) != 0) return -1;
    
    // Ensure card is ready before issuing write command
    if (msdc_wait_card_ready() != 0) {
        printf("Card not ready before write\n");
        return -1;
    }
    
    msdc_clear_fifo();
    msdc[_MSDC_INT] = 0xFFFFFFFF;
    
    // Set block count to 1
    msdc[_SDC_BLK_NUM] = 1;
    
    dump_u32_bytes("Write block", block_num);
    
    // CMD24 - WRITE_SINGLE_BLOCK
    msdc_wait_cmd_ready();
    msdc[_SDC_ARG] = block_num;
    msdc[_SDC_CMD] = 24 | CMD_R1_RESP | CMD_SINGLE_BLK | CMD_WRITE | CMD_BLKLEN(512);
    
    // Wait for command response
    if (msdc_wait_int(INT_CMDRDY, 100000) != 0) {
        printf("CMD24 timeout\n");
        return -1;
    }
    
    dump_u32_bytes("CMD24 RESP0", msdc[_SDC_RESP0]);
    dump_u32_bytes("CMD24 INT", msdc[_MSDC_INT]);
    
    // Check card accepted write command
    if (msdc[_SDC_RESP0] & 0xFDF90008) {
        printf("CMD24 error in response\n");
        dump_u32_bytes("RESP0", msdc[_SDC_RESP0]);
        return -1;
    }
    
    msdc[_MSDC_INT] = INT_CMDRDY;  // Clear command done, keep others
    
    // Write 128 words to FIFO, checking FIFO space
    int words_written = 0;
    uint32_t timeout_val = 100000;
    
    while (timeout_val-- > 0 && words_written < 128) {
        // FIFOCS bits [23:16] = TXCNT (bytes in TX FIFO)
        // FIFO is 128 bytes, so wait until there's space
        uint32_t tx_count = (msdc[_MSDC_FIFOCS] >> 16) & 0xFF;
        if (tx_count < 128) {  // Room for more data
            msdc[_MSDC_TXDATA] = buffer[words_written++];
        }
    }
    
    dump_u32_bytes("Words written", words_written);
    
    if (words_written < 128) {
        printf("Write FIFO timeout\n");
        return -1;
    }
    
    // Wait for transfer complete
    timeout_val = 1000000;  // Longer timeout for write
    while (timeout_val-- > 0) {
        uint32_t int_status = msdc[_MSDC_INT];
        if (int_status & INT_XFER_COMPL) break;
        if (int_status & (INT_DATCRCERR | INT_DATTMO)) {
            printf("Write data error\n");
            dump_u32_bytes("INT", int_status);
            return -1;
        }
    }
    
    if (timeout_val == 0) {
        printf("Write timeout waiting for XFER_COMPL\n");
        dump_u32_bytes("MSDC_INT", msdc[_MSDC_INT]);
        dump_u32_bytes("SDC_STS", msdc[_SDC_STS]);
        dump_u32_bytes("FIFOCS", msdc[_MSDC_FIFOCS]);
        return -1;
    }
    
    uint32_t int_status = msdc[_MSDC_INT];
    msdc[_MSDC_INT] = int_status;
    
    // Wait for card to finish programming
    if (msdc_wait_card_ready() != 0) {
        printf("Card busy after write\n");
        return -1;
    }
    
    printf("Write OK\n");
    return 0;
}

// ========== Helper: compare buffers ==========

int buffers_equal(uint32_t *a, uint32_t *b, int words) {
    for (int i = 0; i < words; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

// ========== Helper: print hex byte ==========

void print_hex_byte(uint8_t b) {
    char hex[4];
    hex[0] = "0123456789ABCDEF"[b >> 4];
    hex[1] = "0123456789ABCDEF"[b & 0xF];
    hex[2] = ' ';
    hex[3] = 0;
    printf(hex);
}

// ========== Boot0 verification test (read-only, safe) ==========

void emmc_boot0_verify_test(void) {
    uint32_t block0[128];
    uint32_t block1[128];
    
    printf("\n=== Boot0 Read Verification Test ===\n");
    
    emmc_init();
    
    // Read boot0 block 0
    printf("\n[1] Reading boot0 block 0...\n");
    if (emmc_read_block(EMMC_PART_BOOT0, 0, block0) != 0) {
        printf("FAILED: Could not read boot0 block 0\n");
        return;
    }
    
    // Read boot0 block 1
    printf("[2] Reading boot0 block 1...\n");
    if (emmc_read_block(EMMC_PART_BOOT0, 1, block1) != 0) {
        printf("FAILED: Could not read boot0 block 1\n");
        return;
    }
    
    // Dump block 0 (should start with "EMMC_BOOT")
    printf("\n=== Boot0 Block 0 (512 bytes) ===\n");
    uint8_t *b0 = (uint8_t *)block0;
    for (int i = 0; i < 512; i += 16) {
        dump_u32_bytes("", i);  // offset
        for (int j = 0; j < 16; j++) {
            print_hex_byte(b0[i + j]);
        }
        printf("\n");
    }
    
    // Dump block 1 (should start with "BRLYT")
    printf("\n=== Boot0 Block 1 (512 bytes) ===\n");
    uint8_t *b1 = (uint8_t *)block1;
    for (int i = 0; i < 512; i += 16) {
        dump_u32_bytes("", i);  // offset
        for (int j = 0; j < 16; j++) {
            print_hex_byte(b1[i + j]);
        }
        printf("\n");
    }
    
    // Verify expected magic values
    printf("\n=== Verification ===\n");
    
    // Block 0 should start with "EMMC_BOOT"
    if (b0[0] == 'E' && b0[1] == 'M' && b0[2] == 'M' && b0[3] == 'C' &&
        b0[4] == '_' && b0[5] == 'B' && b0[6] == 'O' && b0[7] == 'O' && b0[8] == 'T') {
        printf("Block 0: EMMC_BOOT magic OK\n");
    } else {
        printf("Block 0: EMMC_BOOT magic MISSING!\n");
    }
    
    // Block 1 should start with "BRLYT"
    if (b1[0] == 'B' && b1[1] == 'R' && b1[2] == 'L' && b1[3] == 'Y' && b1[4] == 'T') {
        printf("Block 1: BRLYT magic OK\n");
    } else {
        printf("Block 1: BRLYT magic MISSING!\n");
    }
    
    printf("\n=== Boot0 Verification Test Complete ===\n");
}

// ========== Roundtrip test ==========

void emmc_roundtrip_test(void) {
    // Use a high block number unlikely to contain critical data
    // Block 0x100000 = 512MB offset (assuming 512-byte blocks)
    const uint32_t TEST_BLOCK = 0x100000;
    
    uint32_t original[128];
    uint32_t test_pattern[128];
    uint32_t readback[128];
    int failed = 0;
    
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
        test_pattern[i] = 0x0C0FFEE0 ^ i;
    }
    if (emmc_write_block(EMMC_PART_USER, TEST_BLOCK, test_pattern) != 0) {
        printf("FAILED: Could not write test pattern\n");
        failed = 1;
        goto restore;
    }
    printf("Write complete\n");
    
    // Step 3: Read back and verify test pattern
    printf("\n[3] Verifying test pattern...\n");
    if (emmc_read_block(EMMC_PART_USER, TEST_BLOCK, readback) != 0) {
        printf("FAILED: Could not read back test pattern\n");
        failed = 1;
        goto restore;
    }
    if (!buffers_equal(test_pattern, readback, 128)) {
        printf("FAILED: Test pattern mismatch!\n");
        dump_u32_bytes("Expected", test_pattern[0]);
        dump_u32_bytes("Got", readback[0]);
        failed = 1;
        goto restore;
    }
    printf("Test pattern verified OK\n");
    
restore:
    // Step 4: Restore original contents (always attempt this)
    printf("\n[4] Restoring original contents...\n");
    if (emmc_write_block(EMMC_PART_USER, TEST_BLOCK, original) != 0) {
        printf("FAILED: Could not restore original\n");
        return;
    }
    printf("Restore complete\n");
    
    if (failed) {
        printf("\n=== Roundtrip Test FAILED ===\n");
        return;
    }
    
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
