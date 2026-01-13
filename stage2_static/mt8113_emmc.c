#include "printf.h"
#include "mt8113_emmc.h"

#define MSDC_BASE         0x11230000
#define MSDC_CFG          (0x00/4)
#define MSDC_IOCON        (0x04/4)
#define MSDC_PS           (0x08/4)
#define MSDC_INT          (0x0C/4)
#define MSDC_INTEN        (0x10/4)
#define MSDC_FIFOCS       (0x14/4)
#define MSDC_TXDATA       (0x18/4)
#define MSDC_RXDATA       (0x1C/4)
#define SDC_CFG           (0x30/4)
#define SDC_CMD           (0x34/4)
#define SDC_ARG           (0x38/4)
#define SDC_STS           (0x3C/4)
#define SDC_RESP0         (0x40/4)
#define SDC_RESP1         (0x44/4)
#define SDC_RESP2         (0x48/4)
#define SDC_RESP3         (0x4C/4)
#define SDC_BLK_NUM       (0x50/4)
#define SDC_ADV_CFG0      (0x64/4)
#define PATCH_BIT0        (0xB0/4)
#define PATCH_BIT1        (0xB4/4)
#define PATCH_BIT2        (0xB8/4)
#define PAD_TUNE0         (0xEC/4)
#define DAT_RD_DLY0       (0xF0/4)
#define DAT_RD_DLY1       (0xF4/4)
#define EMMC50_CFG0       (0x208/4)
#define EMMC50_CFG1       (0x20C/4)
#define EMMC50_BLOCK_LEN  (0x228/4)

#define EMMC_PART_USER    0
#define EMMC_PART_BOOT0   1
#define EMMC_PART_BOOT1   2

// MSDC_CFG bits
#define MSDC_CFG_MODE           (0x1  << 0)
#define MSDC_CFG_CKPDN          (0x1  << 1)
#define MSDC_CFG_RST            (0x1  << 2)
#define MSDC_CFG_PIO            (0x1  << 3)
#define MSDC_CFG_CKDRVEN        (0x1  << 4)
#define MSDC_CFG_BV18SDT        (0x1  << 5)
#define MSDC_CFG_BV18PSS        (0x1  << 6)
#define MSDC_CFG_CKSTB          (0x1  << 7)
#define MSDC_CFG_CKDIV          (0xfff<< 8)
#define MSDC_CFG_CKMOD          (0x3  << 20)
#define MSDC_CFG_CKMOD_HS400    (0x1  << 22)

// MSDC_INT bits
#define MSDC_INT_MMCIRQ         (0x1  << 0)
#define MSDC_INT_CDSC           (0x1  << 1)
#define MSDC_INT_ACMDRDY        (0x1  << 3)
#define MSDC_INT_ACMDTMO        (0x1  << 4)
#define MSDC_INT_ACMDCRCERR     (0x1  << 5)
#define MSDC_INT_DMAQ_EMPTY     (0x1  << 6)
#define MSDC_INT_SDIOIRQ        (0x1  << 7)
#define MSDC_INT_CMDRDY         (0x1  << 8)
#define MSDC_INT_CMDTMO         (0x1  << 9)
#define MSDC_INT_RSPCRCERR      (0x1  << 10)
#define MSDC_INT_CSTA           (0x1  << 11)
#define MSDC_INT_XFER_COMPL     (0x1  << 12)
#define MSDC_INT_DXFER_DONE     (0x1  << 13)
#define MSDC_INT_DATTMO         (0x1  << 14)
#define MSDC_INT_DATCRCERR      (0x1  << 15)

// MSDC_FIFOCS bits
#define MSDC_FIFOCS_RXCNT       (0xff << 0)
#define MSDC_FIFOCS_TXCNT       (0xff << 16)
#define MSDC_FIFOCS_CLR         (0x1UL<< 31)

// SDC_CFG bits
#define SDC_CFG_BUSWIDTH        (0x3  << 16)
#define SDC_CFG_DTOC            (0xffUL << 24)

// SDC_CMD bits
#define SDC_CMD_OPC             (0x3f << 0)
#define SDC_CMD_BRK             (0x1  << 6)
#define SDC_CMD_RSPTYP          (0x7  << 7)
#define SDC_CMD_DTYP            (0x3  << 11)
#define SDC_CMD_RW              (0x1  << 13)
#define SDC_CMD_STOP            (0x1  << 14)
#define SDC_CMD_BLKLEN          (0xfff<< 16)

// SDC_STS bits
#define SDC_STS_SDCBUSY         (0x1  << 0)
#define SDC_STS_CMDBUSY         (0x1  << 1)

// Constants
#define MSDC_FIFO_SZ            (128)
#define MSDC_FIFO_THD           (64)
#define MSDC_BUS_1BITS          (0)
#define MSDC_BUS_4BITS          (1)
#define MSDC_BUS_8BITS          (2)

// These are DIFFERENT from mt_sd.h definitions above.
#define INT_CMDRDY        (1 << 8)
#define INT_DATCRCERR     (1 << 5)
#define INT_DATTMO        (1 << 4)
#define INT_XFER_COMPL    (1 << 3)
#define INT_DATA_BITS     (0x38)

#define CMD_R1_RESP       (1 << 7)
#define CMD_R1B_RESP      (7 << 7)
#define CMD_R2_RESP       (2 << 7)
#define CMD_R3_RESP       (3 << 7)
#define CMD_SINGLE_BLK    (1 << 11)
#define CMD_WRITE         (1 << 13)
#define CMD_BLKLEN(x)     ((x) << 16)

static volatile uint32_t *msdc = (volatile uint32_t*)MSDC_BASE;
static uint32_t current_partition = 0xFF;

void msdc_wait_cmd_ready(void) {
    while (msdc[SDC_STS] & 0x2);
}

void msdc_clear_fifo(void) {
    msdc[MSDC_FIFOCS] = 0x80000000;
    while (msdc[MSDC_FIFOCS] & 0x80000000);
}

int msdc_wait_int(uint32_t mask, uint32_t timeout_val) {
    while (timeout_val-- > 0) {
        if (msdc[MSDC_INT] & mask) return 0;
    }
    return -1;
}

int msdc_send_cmd(uint8_t cmd_idx, uint32_t arg, uint32_t flags) {
    msdc_wait_cmd_ready();
    msdc[MSDC_INT] = 0xFFFFFFFF;
    msdc[SDC_ARG] = arg;
    msdc[SDC_CMD] = cmd_idx | flags;
    return msdc_wait_int(INT_CMDRDY, 100000);
}

int msdc_wait_card_ready(void) {
    for (int i = 0; i < 1000; i++) {
        if (msdc_send_cmd(13, 0x00010000, CMD_R1_RESP) == 0) {
            uint32_t state = (msdc[SDC_RESP0] >> 9) & 0xF;
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
    msdc[MSDC_CFG] |= 0xF;
    for (retry = 5000; retry > 0; retry--) {
        if ((msdc[MSDC_CFG] & 0x4) == 0) break;
    }
    
    msdc_clear_fifo();
    
    // Hardware configuration
    msdc[MSDC_INT] = msdc[MSDC_INT];
    msdc[PAD_TUNE0] = 0x202000;
    msdc[DAT_RD_DLY0] = 0;
    msdc[DAT_RD_DLY1] = 0;
    msdc[MSDC_IOCON] = 0;
    
    // Silicon bug workarounds
    msdc[PATCH_BIT0] = 0x403C0046;
    msdc[PATCH_BIT1] = 0xFFFF4289;
    msdc[PATCH_BIT2] = 0x10008000;
    
    // eMMC 5.0 config
    msdc[EMMC50_CFG0] |= 0x10;
    msdc[EMMC50_BLOCK_LEN] &= 0xFCFFFFFF;
    msdc[SDC_ADV_CFG0] |= 0x100000;
    
    // IO config
    msdc[MSDC_IOCON] |= 0xE;
    msdc[PATCH_BIT0] |= 0x40000000;
    msdc[MSDC_CFG] &= 0xFE7FFFFF;
    
    // 1-bit bus width
    msdc[SDC_CFG] &= 0xFFFCFFFF;
    
    // Clock: 260kHz for init
    msdc[MSDC_CFG] &= 0xFFBFFFFF;
    msdc[PATCH_BIT2] |= 0x10000000;
    
    printf("MSDC_CFG before clk 0x%s\n", u32_to_str(msdc[MSDC_CFG]));
    msdc[MSDC_CFG] = (msdc[MSDC_CFG] & 0xFFC000FF) | 0x18500;
    printf("MSDC_CFG after clk 0x%s\n", u32_to_str(msdc[MSDC_CFG]));
    
    // Wait for clock stable with timeout
    for (int i = 0; i < 100000; i++) {
        if (msdc[MSDC_CFG] & 0x80) break;
    }
    if (!(msdc[MSDC_CFG] & 0x80)) {
        printf("Clock not stable!\n");
        printf("MSDC_CFG 0x%s\n", u32_to_str(msdc[MSDC_CFG]));
    }
    
    // Timeout config
    msdc[SDC_CFG] &= 0x00E7FFFD;
    
    // Interrupts
    msdc[MSDC_INT] = msdc[MSDC_INT];
    msdc[MSDC_INTEN] = 0x738;
    
    printf("Controller configured\n");
    
    // CMD0 - GO_IDLE
    msdc_send_cmd(0, 0, 0);
    
    // CMD1 - SEND_OP_COND
    for (retry = 100; retry > 0; retry--) {
        if (msdc_send_cmd(1, 0x40FF8080, CMD_R3_RESP) == 0) {
            if (msdc[SDC_RESP0] & 0x80000000) {
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
    msdc[MSDC_CFG] = (msdc[MSDC_CFG] & 0xFFC000FF) | (0x4 << 8);
    for (int i = 0; i < 100000; i++) {
        if (msdc[MSDC_CFG] & 0x80) break;
    }
    
    if (msdc_wait_card_ready() != 0) {
        printf("Card not ready!\n");
        return;
    }
    
    printf("=== eMMC Init Complete ===\n");
    current_partition = EMMC_PART_USER;
    
    // Drain any leftover data in FIFO from init sequence
    msdc_clear_fifo();
    while ((msdc[MSDC_FIFOCS] & 0xFF) > 0) {
        (void)msdc[MSDC_RXDATA];
    }
    msdc[MSDC_INT] = 0xFFFFFFFF;
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

int emmc_read_sector(uint32_t partition, uint32_t sector_num, uint32_t *buffer) {
    if (emmc_switch_partition(partition) != 0) return -1;
    
    // Ensure card is ready before issuing read command
    if (msdc_wait_card_ready() != 0) {
        printf("Card not ready before read\n");
        return -1;
    }
    
    // First read after partition switch or write returns stale data.
    // Do every read twice and discard the first result.
    for (int attempt = 0; attempt < 2; attempt++) {
        // Clear FIFO and check it's empty
        msdc_clear_fifo();
        uint32_t fifocs = msdc[MSDC_FIFOCS];
        uint32_t rx_count = fifocs & 0xFF;
        if (rx_count != 0) {
            // Drain it
            while ((msdc[MSDC_FIFOCS] & 0xFF) > 0) {
                (void)msdc[MSDC_RXDATA];
            }
        }
        
        // Clear any pending interrupts
        msdc[MSDC_INT] = 0xFFFFFFFF;

        // CMD17 - READ_SINGLE_BLOCK
        if (msdc_send_cmd(17, sector_num, CMD_R1_RESP | CMD_SINGLE_BLK | CMD_BLKLEN(512)) != 0) {
            printf("CMD17 failed\n");
            return -1;
        }
        
        // Wait for data to start arriving
        uint32_t timeout_val = 100000;
        while (timeout_val-- > 0) {
            if ((msdc[MSDC_FIFOCS] & 0xFF) > 0) break;
            if (msdc[MSDC_INT] & INT_DATA_BITS) break;
        }
        
        // Read 128 words (512 bytes) from FIFO
        int words_read = 0;
        timeout_val = 100000;
        
        while (timeout_val-- > 0 && words_read < 128) {
            uint32_t fifo_count = msdc[MSDC_FIFOCS] & 0xFF;
            while (fifo_count >= 4 && words_read < 128) {
                buffer[words_read++] = msdc[MSDC_RXDATA];
                fifo_count = msdc[MSDC_FIFOCS] & 0xFF;
            }
            if (msdc[MSDC_INT] & INT_XFER_COMPL) break;
        }
        
        uint32_t int_status = msdc[MSDC_INT];
        msdc[MSDC_INT] = int_status;
        
        if ((int_status & (INT_DATCRCERR | INT_DATTMO)) || words_read < 128) {
            printf("Read error\n");
            printf("INT 0x%s\n", u32_to_str(int_status));
            printf("words_read 0x%s\n", u32_to_str(words_read));
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

int emmc_write_sector(uint32_t partition, uint32_t sector_num, uint32_t *buffer) {
    if (emmc_switch_partition(partition) != 0) return -1;
    
    // Ensure card is ready before issuing write command
    if (msdc_wait_card_ready() != 0) {
        printf("Card not ready before write\n");
        return -1;
    }
    
    msdc_clear_fifo();
    msdc[MSDC_INT] = 0xFFFFFFFF;
    
    // Set block count to 1
    msdc[SDC_BLK_NUM] = 1;

    printf("Write sector 0x%s\n", u32_to_str(sector_num));

    // CMD24 - WRITE_SINGLE_BLOCK
    msdc_wait_cmd_ready();
    msdc[SDC_ARG] = sector_num;
    msdc[SDC_CMD] = 24 | CMD_R1_RESP | CMD_SINGLE_BLK | CMD_WRITE | CMD_BLKLEN(512);

    // Wait for command response
    if (msdc_wait_int(INT_CMDRDY, 100000) != 0) {
        printf("CMD24 timeout\n");
        return -1;
    }

    printf("CMD24 RESP0 0x%s\n", u32_to_str(msdc[SDC_RESP0]));
    printf("CMD24 INT 0x%s\n", u32_to_str(msdc[MSDC_INT]));

    // Check card accepted write command
    if (msdc[SDC_RESP0] & 0xFDF90008) {
        printf("CMD24 error in response\n");
        printf("RESP0 0x%s\n", u32_to_str(msdc[SDC_RESP0]));
        return -1;
    }
    
    msdc[MSDC_INT] = INT_CMDRDY;  // Clear command done, keep others
    
    // Write 128 words to FIFO, checking FIFO space
    int words_written = 0;
    uint32_t timeout_val = 100000;

    while (timeout_val-- > 0 && words_written < 128) {
        // FIFOCS bits [23:16] = TXCNT (bytes in TX FIFO)
        // FIFO is 128 bytes, so wait until there's space
        uint32_t tx_count = (msdc[MSDC_FIFOCS] >> 16) & 0xFF;
        if (tx_count < 128) {  // Room for more data
            msdc[MSDC_TXDATA] = buffer[words_written++];
        }
    }
    
    printf("Words written 0x%s\n", u32_to_str(words_written));

    if (words_written < 128) {
        printf("Write FIFO timeout\n");
        return -1;
    }

    // Wait for transfer complete
    timeout_val = 1000000;  // Longer timeout for write
    while (timeout_val-- > 0) {
        uint32_t int_status = msdc[MSDC_INT];
        if (int_status & INT_XFER_COMPL) break;
        if (int_status & (INT_DATCRCERR | INT_DATTMO)) {
            printf("Write data error\n");
            printf("INT 0x%s\n", u32_to_str(int_status));
            return -1;
        }
    }

    if (timeout_val == 0) {
        printf("Write timeout waiting for XFER_COMPL\n");
        printf("MSDC_INT 0x%s\n", u32_to_str(msdc[MSDC_INT]));
        printf("SDC_STS 0x%s\n", u32_to_str(msdc[SDC_STS]));
        printf("FIFOCS 0x%s\n", u32_to_str(msdc[MSDC_FIFOCS]));
        return -1;
    }
    
    uint32_t int_status = msdc[MSDC_INT];
    msdc[MSDC_INT] = int_status;
    
    // Wait for card to finish programming
    if (msdc_wait_card_ready() != 0) {
        printf("Card busy after write\n");
        return -1;
    }
    
    printf("Write OK\n");
    return 0;
}

int buffers_equal(uint32_t *a, uint32_t *b, int words) {
    for (int i = 0; i < words; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

void print_hex_byte(uint8_t b) {
    char hex[4];
    hex[0] = "0123456789ABCDEF"[b >> 4];
    hex[1] = "0123456789ABCDEF"[b & 0xF];
    hex[2] = ' ';
    hex[3] = 0;
    printf(hex);
}

void emmc_boot0_verify_test(void) {
    uint32_t block0[128];
    uint32_t block1[128];
    
    printf("\n=== Boot0 Read Verification Test ===\n");
    
    emmc_init();
    
    // Read boot0 block 0
    printf("\n[1] Reading boot0 block 0...\n");
    if (emmc_read_sector(EMMC_PART_BOOT0, 0, block0) != 0) {
        printf("FAILED: Could not read boot0 block 0\n");
        return;
    }
    
    // Read boot0 block 1
    printf("[2] Reading boot0 block 1...\n");
    if (emmc_read_sector(EMMC_PART_BOOT0, 1, block1) != 0) {
        printf("FAILED: Could not read boot0 block 1\n");
        return;
    }
    
    // Dump block 0
    printf("\n=== Boot0 Block 0 (512 bytes) ===\n");
    uint8_t *b0 = (uint8_t *)block0;
    for (int i = 0; i < 512; i += 16) {
        printf("0x%s ", u32_to_str(i));
        for (int j = 0; j < 16; j++) {
            print_hex_byte(b0[i + j]);
        }
        printf("\n");
    }

    // Dump block 1
    printf("\n=== Boot0 Block 1 (512 bytes) ===\n");
    uint8_t *b1 = (uint8_t *)block1;
    for (int i = 0; i < 512; i += 16) {
        printf("0x%s ", u32_to_str(i));
        for (int j = 0; j < 16; j++) {
            print_hex_byte(b1[i + j]);
        }
        printf("\n");
    }
    
    // Verify expected magic values
    printf("\n=== Verification ===\n");
    
    // Check block 0
    if (b0[0] == 'E' && b0[1] == 'M' && b0[2] == 'M' && b0[3] == 'C' &&
        b0[4] == '_' && b0[5] == 'B' && b0[6] == 'O' && b0[7] == 'O' && b0[8] == 'T') {
        printf("Block 0: EMMC_BOOT magic OK\n");
    } else {
        printf("Block 0: EMMC_BOOT magic MISSING!\n");
    }
    
    // Check block 1
    if (b1[0] == 'B' && b1[1] == 'R' && b1[2] == 'L' && b1[3] == 'Y' && b1[4] == 'T') {
        printf("Block 1: BRLYT magic OK\n");
    } else {
        printf("Block 1: BRLYT magic MISSING!\n");
    }
    
    printf("\n=== Boot0 Verification Test Complete ===\n");
}

void emmc_roundtrip_test(void) {
    // Use a high block number unlikely to contain critical data
    // Block 0x100000 = 512MB offset (assuming 512-byte blocks)
    const uint32_t TEST_BLOCK = 0x100000;
    
    uint32_t original[128];
    uint32_t test_pattern[128];
    uint32_t readback[128];
    int failed = 0;
    
    printf("\n=== eMMC Roundtrip Test ===\n");
    printf("Test block 0x%s\n", u32_to_str(TEST_BLOCK));

    emmc_init();

    // Step 1: Read original contents
    printf("\n[1] Reading original contents...\n");
    if (emmc_read_sector(EMMC_PART_USER, TEST_BLOCK, original) != 0) {
        printf("FAILED: Could not read original block\n");
        return;
    }
    printf("Original first word 0x%s\n", u32_to_str(original[0]));

    // Step 2: Create and write test pattern
    printf("\n[2] Writing test pattern...\n");
    for (int i = 0; i < 128; i++) {
        test_pattern[i] = 0x0C0FFEE0 ^ i;
    }
    if (emmc_write_sector(EMMC_PART_USER, TEST_BLOCK, test_pattern) != 0) {
        printf("FAILED: Could not write test pattern\n");
        failed = 1;
        goto restore;
    }
    printf("Write complete\n");

    // Step 3: Read back and verify test pattern
    printf("\n[3] Verifying test pattern...\n");
    if (emmc_read_sector(EMMC_PART_USER, TEST_BLOCK, readback) != 0) {
        printf("FAILED: Could not read back test pattern\n");
        failed = 1;
        goto restore;
    }
    if (!buffers_equal(test_pattern, readback, 128)) {
        printf("FAILED: Test pattern mismatch!\n");
        printf("Expected 0x%s\n", u32_to_str(test_pattern[0]));
        printf("Got 0x%s\n", u32_to_str(readback[0]));
        failed = 1;
        goto restore;
    }
    printf("Test pattern verified OK\n");
    
restore:
    // Step 4: Restore original contents
    printf("\n[4] Restoring original contents...\n");
    if (emmc_write_sector(EMMC_PART_USER, TEST_BLOCK, original) != 0) {
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
    if (emmc_read_sector(EMMC_PART_USER, TEST_BLOCK, readback) != 0) {
        printf("FAILED: Could not read back original\n");
        return;
    }
    if (!buffers_equal(original, readback, 128)) {
        printf("FAILED: Original not restored correctly!\n");
        printf("Expected 0x%s\n", u32_to_str(original[0]));
        printf("Got 0x%s\n", u32_to_str(readback[0]));
        return;
    }
    printf("Original restored OK\n");
    
    printf("\n=== Roundtrip Test PASSED ===\n");
}
