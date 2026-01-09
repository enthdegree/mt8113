#include <inttypes.h>
#include <stdint.h>


//#include "common.h"
#include "tools.h"
#include "printf.h"
#include "libc.h"
#include "drivers/sleepy.h"
#include "drivers/types.h"
#include "drivers/core.h"
#include "drivers/mt_sd.h"
#include "drivers/errno.h"
#include "drivers/mmc.h"
#include "mt8113_brom.h"

// ========== MSDC Register Definitions ==========
#define MSDC_BASE         0x11230000
#define _MSDC_CFG         (0x00/4)
#define _MSDC_IOCON       (0x04/4)
#define _MSDC_PS          (0x08/4)
#define _MSDC_INT         (0x0C/4)
#define _MSDC_INTEN       (0x10/4)
#define _MSDC_FIFOCS      (0x14/4)
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

extern void dump_u32_bytes(const char *label, uint32_t v);

static int emmc_read_block_raw(volatile uint32_t *msdc, uint32_t block_num, 
                                uint32_t *buffer, int do_flush)
{
    uint32_t timeout;
    uint32_t temp_buffer[200];
    uint32_t words_read;
    
    if (do_flush) {
        printf("Flushing FIFO...\n");
        
        msdc[_MSDC_FIFOCS] = 0x80000000;
        while ((msdc[_SDC_STS] >> 1 & 1) != 0);
        msdc[_MSDC_INT] = 0xFFFFFFFF;
        
        msdc[_SDC_ARG] = 0;
        msdc[_SDC_CMD] = 17 | (1 << 7) | (1 << 11) | (512 << 16);
        
        timeout = 100000;
        while (timeout-- > 0) {
        if (msdc[_MSDC_INT] & 0x100) break;
        }
        
        msdc[_MSDC_INT] = 0x100;  // Clear command done
        
        // CRITICAL: Drain FIFO AS DATA ARRIVES
        uint32_t words_discarded = 0;
        timeout = 100000;
        while (timeout-- > 0) {
        uint32_t fifo_count = msdc[_MSDC_FIFOCS] & 0xFF;
        
        while (fifo_count >= 4 && words_discarded < 200) {
        volatile uint32_t discard = msdc[_MSDC_RXDATA];
        words_discarded++;
        fifo_count = msdc[_MSDC_FIFOCS] & 0xFF;
        }
        
        // Check what interrupt fired
        uint32_t int_status = msdc[_MSDC_INT];
        if (int_status & 0x38) {
        dump_u32_bytes("Dummy transfer INT", int_status);  // ADD THIS
        break;
        }
        }

        dump_u32_bytes("Dummy discarded", words_discarded);
        
        msdc[_MSDC_INT] = 0xFFFFFFFF;
        printf("FIFO flushed\n");
    }   

    // Hard FIFO reset
    printf("Reading block %d...\n", block_num);
    
    msdc[_MSDC_FIFOCS] = 0x80000000;
    timeout = 10000;
    while (timeout-- > 0) {
        if ((msdc[_MSDC_FIFOCS] & 0x80000000) == 0) break;
    }
    
    // Verify FIFO is empty
    uint32_t fifo_before = msdc[_MSDC_FIFOCS] & 0xFF;
    if (fifo_before != 0) {
        printf("FIFO not empty! Draining...\n");
        while ((msdc[_MSDC_FIFOCS] & 0xFF) > 0) {
            volatile uint32_t discard = msdc[_MSDC_RXDATA];
        }
    }
    
    // Send CMD17
    while ((msdc[_SDC_STS] >> 1 & 1) != 0);
    msdc[_MSDC_INT] = 0xFFFFFFFF;
    
    msdc[_SDC_ARG] = block_num;
    msdc[_SDC_CMD] = 17 | (1 << 7) | (1 << 11) | (512 << 16);
    
    timeout = 100000;
    while (timeout-- > 0) {
        if (msdc[_MSDC_INT] & 0x100) break;
    }
    
    if (!(msdc[_MSDC_INT] & 0x100)) {
        printf("CMD17 failed\n");
        dump_u32_bytes("MSDC_INT", msdc[_MSDC_INT]);
        return -1;
    }
    
    msdc[_MSDC_INT] = 0x100;
    
    // Read data incrementally
    printf("Reading data...\n");
    
    words_read = 0;
    timeout = 100000;
    while (timeout-- > 0) {
        uint32_t fifo_count = msdc[_MSDC_FIFOCS] & 0xFF;
        
        while (fifo_count >= 4 && words_read < 200) {
            temp_buffer[words_read++] = __builtin_bswap32(msdc[_MSDC_RXDATA]);
            fifo_count = msdc[_MSDC_FIFOCS] & 0xFF;
        }
        
        if (msdc[_MSDC_INT] & 0x38) break;
    }
    
    dump_u32_bytes("Words read", words_read);
    
    if (words_read < 128) {
        printf("Incomplete read\n");
        return -1;
    }
    
    memcpy(buffer, temp_buffer, 512);
    return 0;
}

int emmc_read_block(uint32_t partition, uint32_t block_num, uint32_t *buffer)
{
    volatile uint32_t *msdc = (volatile uint32_t*)MSDC_BASE;
    static uint32_t current_partition = 0xFF;
    uint32_t timeout;
    int do_flush = 0;
    
    // Switch partition if needed
    if (partition != current_partition) {
        if (partition == EMMC_PART_BOOT0) {
            printf("Switching to boot0...\n");
            
            while ((msdc[_SDC_STS] >> 1 & 1) != 0);
            msdc[_MSDC_INT] = 0xFFFFFFFF;
            
            msdc[_SDC_ARG] = 0x03B30100;
            msdc[_SDC_CMD] = 6 | (7 << 7);
            
            timeout = 100000;
            while (timeout-- > 0) {
                if (msdc[_MSDC_INT] & 0x738) break;
            }
            
            printf("Switched to boot0\n");
        } else if (partition == EMMC_PART_USER) {
            printf("Switching to userdata...\n");
            
            while ((msdc[_SDC_STS] >> 1 & 1) != 0);
            msdc[_MSDC_INT] = 0xFFFFFFFF;
            
            msdc[_SDC_ARG] = 0x03B30000;
            msdc[_SDC_CMD] = 6 | (7 << 7);
            
            timeout = 100000;
            while (timeout-- > 0) {
                if (msdc[_MSDC_INT] & 0x738) break;
            }
            
            printf("Switched to userdata\n");
        }
        
        current_partition = partition;
        do_flush = 1;  // Flush after partition switch
    }
    
    dump_u32_bytes("CMD6 INT", msdc[_MSDC_INT]);  // ADD THIS
    dump_u32_bytes("CMD6 RESP", msdc[_SDC_RESP0]); // ADD THIS
        
    // Check for errors in response
    if (msdc[_SDC_RESP0] & 0xFFF80000) {
        dump_u32_bytes("CMD6 errors!", msdc[_SDC_RESP0]);
    }

    return emmc_read_block_raw(msdc, block_num, buffer, do_flush);
}

void emmc_init(void) {
// MSDC/eMMC Initialization for MediaTek Stage2 Bootloader
// Successfully initializes eMMC from unresponsive state after BROM handoff

    volatile uint32_t *msdc = (volatile uint32_t*)0x11230000;  // MSDC0 base
    
   
    // Pinctrl registers for MSDC pad configuration
    volatile uint32_t *pinctrl_920 = (volatile uint32_t*)0x10005920;
    volatile uint32_t *pinctrl_250 = (volatile uint32_t*)0x10005250;
    volatile uint32_t *pinctrl_260 = (volatile uint32_t*)0x10005260;
    volatile uint32_t *pinctrl_750 = (volatile uint32_t*)0x10005750;
    volatile uint32_t *pinctrl_740 = (volatile uint32_t*)0x10005740;
    
    uint32_t timeout;
    int retry;
    
    printf("=== eMMC Init ===\n");
    
    // ========== MSDC Controller Reset ==========
    msdc[_MSDC_CFG] |= 0xF;
    
    retry = 5;
    while (retry-- > 0) {
        int wait = 1000;
        while (wait-- > 0) {
            if ((msdc[_MSDC_CFG] >> 2 & 1) == 0) goto reset_complete;
        }
    }
    
reset_complete:
    msdc[_MSDC_FIFOCS] |= 0x80000000;
    
    retry = 5;
    while (retry-- > 0) {
        int wait = 1000;
        while (wait-- > 0) {
            if ((int)msdc[_MSDC_FIFOCS] >= 0) goto fifo_clear;
        }
    }
    
fifo_clear:
    
    // ========== Hardware Configuration ==========
    msdc[_MSDC_INT] = msdc[_MSDC_INT];
    msdc[_PAD_TUNE0] = 0;
    msdc[_DAT_RD_DLY0] = 0;
    msdc[_DAT_RD_DLY1] = 0;
    msdc[_MSDC_IOCON] = 0;
    msdc[_PAD_TUNE0] |= 0x202000;
    
    // CRITICAL: Silicon bug workaround patch bits
    msdc[_PATCH_BIT0] = 0x403C0046;
    msdc[_PATCH_BIT1] = 0xFFFF4289;
    msdc[_PATCH_BIT2] = 0x10008000;
    
    // eMMC 5.0 configuration
    msdc[_EMMC50_CFG0] |= 0x10;
    msdc[_EMMC50_BLOCK_LEN] &= 0xFCFFFFFF;
    msdc[_SDC_ADV_CFG0] |= 0x100000;
    
    // Pinctrl/pad drive strength and slew rate
    *pinctrl_920 = (*pinctrl_920 & 0xFFFBFFFF) | 0x7BF000;
    *pinctrl_250 = (*pinctrl_250 & 0xC003FFFF) | 0x9240000;
    *pinctrl_260 = (*pinctrl_260 & 0xFFE00000) | 0x49249;
    *pinctrl_750 = (*pinctrl_750 & 0xFFFFF88F) | 0x450;
    *pinctrl_740 = (*pinctrl_740 & 0xFFF8FFFF) | 0x60000;
    
    // MSDC IO configuration
    msdc[_MSDC_IOCON] = (msdc[_MSDC_IOCON] & 0xFFFFFFFD) | 0x2;
    msdc[_MSDC_IOCON] = (msdc[_MSDC_IOCON] & 0xFFFFFFFB) | 0xC;
    msdc[_PATCH_BIT0] |= 0x40000000;
    msdc[_MSDC_CFG] &= 0xFE7FFFFF;
    
    // Bus width: 1-bit mode
    msdc[_SDC_CFG] = (msdc[_SDC_CFG] & 0xFFFCFFFF);
    
    // Clock: 260kHz for initialization
    msdc[_MSDC_CFG] &= 0xFFBFFFFF;
    msdc[_PATCH_BIT2] |= 0x10000000;
    msdc[_MSDC_CFG] = (msdc[_MSDC_CFG] & 0xFFC000FF) | 0x18500;
    while ((msdc[_MSDC_CFG] >> 7 & 1) == 0);
    
    // Timeout configuration
    msdc[_SDC_CFG] = (msdc[_SDC_CFG] & 0x00FFFFFF);
    msdc[_SDC_CFG] &= 0xFFF7FFFD;
    msdc[_SDC_CFG] &= 0xFFEFFFFF;
    
    // Enable interrupt status
    msdc[_MSDC_INT] = msdc[_MSDC_INT];
    msdc[_MSDC_INTEN] = 0x738;
    
    printf("Config done\n");
    
    // ========== CMD0 - GO_IDLE_STATE ==========
    printf("=== CMD0 ===\n");
    
    msdc[_MSDC_INT] = 0xFFFFFFFF;
    msdc[_SDC_ARG] = 0;
    msdc[_SDC_CMD] = 0;
    
    timeout = 100000;
    while (timeout-- > 0) {
        if ((msdc[_SDC_STS] >> 1 & 1) == 0) break;
    }
    
    dump_u32_bytes("MSDC_INT", msdc[_MSDC_INT]);
    
    // ========== CMD1 - SEND_OP_COND ==========
    printf("=== CMD1 ===\n");
    
    int retries = 100;
    while (retries-- > 0) {
        while ((msdc[_SDC_STS] >> 1 & 1) != 0);
        msdc[_MSDC_INT] = 0xFFFFFFFF;
        
        msdc[_SDC_ARG] = 0x40FF8000;
        msdc[_SDC_CMD] = 0x181;
        
        timeout = 100000;
        while (timeout-- > 0) {
            if (msdc[_MSDC_INT] & 0x738) break;
        }
        
        if (msdc[_SDC_RESP0] & 0x80000000) {
            dump_u32_bytes("eMMC ready!", msdc[_SDC_RESP0]);
            goto emmc_ready;
        }
        
        volatile int delay = 10000;
        while (delay--);
    }
    
    printf("CMD1 timeout\n");
    return;
    
emmc_ready:
    
    // ========== CMD2 - ALL_SEND_CID ==========
    printf("=== CMD2 ===\n");
    
    while ((msdc[_SDC_STS] >> 1 & 1) != 0);
    msdc[_MSDC_INT] = 0xFFFFFFFF;
    
    msdc[_SDC_ARG] = 0;
    msdc[_SDC_CMD] = 2 | (2 << 7);
    
    timeout = 100000;
    while (timeout-- > 0) {
        if (msdc[_MSDC_INT] & 0x738) break;
    }
    
    if (!(msdc[_MSDC_INT] & 0x100)) {
        printf("CMD2 failed\n");
        return;
    }
    
    dump_u32_bytes("CID[0]", msdc[_SDC_RESP0]);
    dump_u32_bytes("CID[1]", msdc[_SDC_RESP1]);
    dump_u32_bytes("CID[2]", msdc[_SDC_RESP2]);
    dump_u32_bytes("CID[3]", msdc[_SDC_RESP3]);
    
    // ========== CMD3 - SET_RELATIVE_ADDR ==========
    printf("=== CMD3 ===\n");
    
    while ((msdc[_SDC_STS] >> 1 & 1) != 0);
    msdc[_MSDC_INT] = 0xFFFFFFFF;
    
    msdc[_SDC_ARG] = 0x00010000;
    msdc[_SDC_CMD] = 3 | (1 << 7);
    
    timeout = 100000;
    while (timeout-- > 0) {
        if (msdc[_MSDC_INT] & 0x738) break;
    }
    
    if (!(msdc[_MSDC_INT] & 0x100)) {
        printf("CMD3 failed\n");
        return;
    }
    
    printf("RCA set\n");
    
    // ========== CMD7 - SELECT_CARD ==========
    printf("=== CMD7 ===\n");
    
    while ((msdc[_SDC_STS] >> 1 & 1) != 0);
    msdc[_MSDC_INT] = 0xFFFFFFFF;
    
    msdc[_SDC_ARG] = 0x00010000;
    msdc[_SDC_CMD] = 7 | (7 << 7);
    
    timeout = 100000;
    while (timeout-- > 0) {
        if (msdc[_MSDC_INT] & 0x738) break;
    }
    
    if (!(msdc[_MSDC_INT] & 0x100)) {
        printf("CMD7 failed\n");
        return;
    }
    
    // ========== CMD13 - Verify TRAN state ==========
    printf("=== CMD13 ===\n");
    
    while ((msdc[_SDC_STS] >> 1 & 1) != 0);
    msdc[_MSDC_INT] = 0xFFFFFFFF;
    
    msdc[_SDC_ARG] = 0x00010000;
    msdc[_SDC_CMD] = 13 | (1 << 7);
    
    timeout = 100000;
    while (timeout-- > 0) {
        if (msdc[_MSDC_INT] & 0x738) break;
    }
    
    uint32_t state = (msdc[_SDC_RESP0] >> 9) & 0xF;
    dump_u32_bytes("Card state", state);
    
    if (state == 4) {
        printf("=== SUCCESS ===\n");
        printf("eMMC READY!\n");
    } else {
        printf("Card not in TRAN state\n");
    }

    // INCREASE CLOCK SPEED for data transfers
    printf("Increasing clock to 25MHz...\n");
    
    msdc[_MSDC_CFG] = (msdc[_MSDC_CFG] & 0xFFC000FF) | (0x4 << 8);  // Divisor 4 = ~25MHz
    
    // Wait for clock stable
    while ((msdc[_MSDC_CFG] >> 7 & 1) == 0);
    
    dump_u32_bytes("Clock increased", 1);
    dump_u32_bytes("MSDC_CFG", msdc[_MSDC_CFG]);

}

void emmc_read_test(void)
{
    volatile uint32_t *msdc = (volatile uint32_t*)MSDC_BASE;
    uint32_t buffer[128];
    uint32_t timeout;
    
    printf("\n=== eMMC Read Test ===\n");
    
    // Init
    emmc_init();
     
    // Check card state BEFORE doing anything
    printf("Checking card state...\n");
    
    while ((msdc[_SDC_STS] >> 1 & 1) != 0);
    msdc[_MSDC_INT] = 0xFFFFFFFF;
    
    msdc[_SDC_ARG] = 0x00010000;  // RCA = 1
    msdc[_SDC_CMD] = 13 | (1 << 7);  // CMD13
    
    timeout = 100000;
    while (timeout-- > 0) {
        if (msdc[_MSDC_INT] & 0x738) break;
    }
    
    dump_u32_bytes("CMD13 INT", msdc[_MSDC_INT]);
    dump_u32_bytes("CMD13 RESP", msdc[_SDC_RESP0]);
    dump_u32_bytes("Card state", (msdc[_SDC_RESP0] >> 9) & 0xF);
   
    // Read boot0 blocks
    if (emmc_read_block(EMMC_PART_BOOT0, 0, buffer) == 0) {
        printf("\nBoot0 block 0:\n");
        for (int i = 0; i < 128; i++) {
            dump_u32_bytes("", buffer[i]);
        }
    }
    
    if (emmc_read_block(EMMC_PART_BOOT0, 1, buffer) == 0) {
        printf("\nBoot0 block 1:\n");
        for (int i = 0; i < 128; i++) {
            dump_u32_bytes("", buffer[i]);
        }
    }
    
    // Read userdata block
    if (emmc_read_block(EMMC_PART_USER, 0, buffer) == 0) {
        printf("\nUserdata block 0:\n");
        for (int i = 0; i < 128; i++) {
            dump_u32_bytes("", buffer[i]);
        }
    }
    
    printf("\n=== Test Complete ===\n");
}



void recv_data(char *addr, uint32_t sz, uint32_t flags __attribute__((unused))) {
    for (uint32_t i = 0; i < (((sz + 3) & ~3) / 4); i++) {
        ((uint32_t *)addr)[i] = __builtin_bswap32(recv_dword());
    }
}

/*
void cache1()
{
  unsigned int v0; // r0
  unsigned int v1; // r0
  unsigned int v2; // r10
  unsigned int v3; // r1
  char v4; // r5
  int v5; // r7
  int v6; // r9
  bool v7; // cc

  asm volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(v0));
  v0 = v0 & 0xFFFFFFFB;
  asm volatile ("dsb 0xF":::"memory");
  asm volatile ("mcr p15, 0, %0, c1, c0, 0" :: "r"(v0));
  asm volatile ("dsb 0xF":::"memory");
  asm volatile ("isb 0xF":::"memory");
  asm volatile ("mcr p15, 0, %0, c8, c7, 1" :: "r"(v0));
  asm volatile ("dsb 0xF":::"memory");
  asm volatile ("isb 0xF":::"memory");
  asm volatile ("dmb 0xF":::"memory");
  asm volatile ("mrc p15, 1, %0, c0, c0, 1" : "=r"(v1));
  if ( (v1 & 0x7000000) != 0 )
  {
    v2 = 0;
    do
    {
      if ( ((v1 >> (v2 + (v2 >> 1))) & 7) >= 2 )
      {
        asm volatile ("mcr p15, 2, %0, c0, c0, 0" :: "r"(v2));
        asm volatile ("dsb 0xF":::"memory");
        asm volatile ("isb 0xF":::"memory");
        asm volatile ("mrc p15, 1, %0, c0, c0, 0" : "=r"(v3));
        asm volatile("clz    %0, %1 \n" : "=r" (v4) : "r"((v3 >> 3) & 0x3FF));
        v5 = (v3 >> 13) & 0x7FFF;
        do
        {
          v6 = (v3 >> 3) & 0x3FF;
          do
          {
            asm volatile ("mcr p15, 0, %0, c7, c14, 2" :: "r"(v2 | (v6 << v4) | (v5 << ((v3 & 7) + 4))));
            v7 = v6-- < 1;
          }
          while ( !v7 );
          v7 = v5-- < 1;
        }
        while ( !v7 );
      }
      v2 += 2;
    }
    while ( (int)((v1 & 0x7000000) >> 23) > (int)v2 );
  }
  asm volatile ("mcr p15, 2, %0, c0, c0, 0" :: "r"(0));
  asm volatile ("dsb 0xF":::"memory");
  asm volatile ("isb 0xF":::"memory");
}

void cache2(int flag){
            uint32_t tmp;
            uint32_t val;
            asm volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(tmp));
            if (flag) val=tmp|0x1000;
            else val=tmp&0xFFFFEFFF;
            asm volatile ("mcr p15, 0, %0, c1, c0, 0" :: "r"(val));
            asm volatile ("dsb 0xF":::"memory");
            asm volatile ("isb 0xF":::"memory");
}

void cache3(){
    asm volatile ("mcr p15, 0, %0, c7, c1, 0" :: "r"(0));
    asm volatile ("dsb 0xF":::"memory");
    asm volatile ("isb 0xF":::"memory");
}
*/

void apmcu_icache_invalidate(){
    asm volatile ("mcr p15, 0, %0, c7, c5, 0" :: "r"(0));
}

void apmcu_isb(){
    asm volatile ("ISB");
}

void apmcu_disable_icache(){
    uint32_t r0=0;
    asm volatile ("mcr p15, 0, %0, c7, c5, 6" :: "r"(r0)); /* Flush entire branch target cache */
    asm volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(r0));
    asm volatile ("bic %0,%0,#0x1800" : "=r"(r0) : "r"(r0)); /* I+Z bits */
    asm volatile ("mcr p15, 0, %0, c1, c0, 0" :: "r"(r0));
}

void apmcu_disable_smp(){
    uint32_t r0=0;
    asm volatile ("mrc p15, 0, %0, c1, c0, 1" : "=r"(r0));
    asm volatile ("bic %0,%0,#0x40" : "=r"(r0) : "r"(r0)); /* SMP bit */
    asm volatile ("mcr p15, 0, %0, c1, c0, 1" :: "r"(r0));
}

static void put_hex_nibble(uint8_t v)
{
    v &= 0xF;
    char * s = "0";
    if (v < 10) s[0] = '0' + v;
    else s[0] = 'A' + (v - 10);
    printf(s);
}

static void put_hex_byte(uint8_t v)
{
    put_hex_nibble(v >> 4);
    put_hex_nibble(v);
}

void dump_u32_bytes(const char *label, uint32_t v)
{
    const uint8_t *b = (const uint8_t *)&v;
    printf("%s ", label);
    put_hex_byte(b[3]); 
    put_hex_byte(b[2]); 
    put_hex_byte(b[1]);
    put_hex_byte(b[0]);
    printf("\n");
}

int main() {
    searchparams();
    char buf[0x200] = { 0 };
    int ret = 0;

    printf("2ND stage payload\n");
    printf("(c) xyz, k4y0z, bkerler 2019-2021\n");

//    while(1) {}

    printf("Entering command loop\n");
    char buffer[0x200]={0xff};
    send_dword(0xB1B2B3B4);
    struct msdc_host host = { 0 };
    host.ocr_avail = MSDC_OCR_AVAIL;

    while (1) {
        printf("Waiting for cmd\n");
        memset(buf, 0, sizeof(buf));    
        uint32_t magic = recv_dword();
        if (magic != 0xf00dd00d) {
            printf("Protocol error\n");
            printf("Magic received = 0x%08X\n", magic);
            break;
        }
        uint32_t cmd = recv_dword();
    dump_u32_bytes("cmd", cmd);
    switch (cmd) {
        case 0x1000: {
            uint32_t block = recv_dword();
            printf("Read block 0x%08X\n", block);
            memset(buf, 0, sizeof(buf));
            if (mmc_read(&host, block, buf) != 0) {
                printf("Read error!\n");
            } else {
                usbdl_put_data(buf, sizeof(buf));
            }
            break;
        }
        case 0x1001: {
            uint32_t block = recv_dword();
            printf("Write block 0x%08X ", block);
            memset(buf, 0, sizeof(buf));
            usbdl_get_data(buf, 0x200, 0);
            if (mmc_write(&host, block, buf) != 0) {
                printf("Write error!\n");
            } else {
                printf("OK\n");
                send_dword(0xD0D0D0D0);
            }
            break;
        }
        case 0x1002: {
            uint32_t part = recv_dword();
            printf("Switch to partition %d => ", part);
            ret = mmc_set_part(&host, part);
            printf("0x%08X\n", ret);
            mdelay(500); // just in case
            break;
        }
        case 0x2000: {
            printf("Read rpmb\n");
            uint16_t addr = (uint16_t)recv_word();
            mmc_rpmb_read(&host, addr, buf);
            usbdl_put_data(buf, 0x100);
            break;
        }
        case 0x2001: {
            printf("Write rpmb\n");
            usbdl_get_data(buf, 0x100, 0);
            mmc_rpmb_write(&host, buf);
            break;
        }
        case 0x3000: {
            printf("Reboot\n");
            volatile uint32_t *reg = (volatile uint32_t *)0x10007000;
            reg[8/4] = 0x1971;
            reg[0/4] = 0x22000014;
            reg[0x14/4] = 0x1209;

            while (1) {

            }
        }
        case 0x3001: {
            printf("Kick watchdog\n");
            volatile uint32_t *reg = (volatile uint32_t *)0x10007000;
            reg[8/4] = 0x1971;
            break;
        }
        case 0x4002: {
            uint32_t address = recv_dword();
            uint32_t size = recv_dword();
            printf("Read %d Bytes from address 0x%08X\n", size, address);
            usbdl_put_data(address, size);
            break;
        }
        case 0x4000: {
            char* address = (char*)recv_dword();
            uint32_t size = recv_dword();
            uint32_t rdsize=size;
            if (size%4!=0) rdsize=((size/4)+1)*4;
            recv_data(buffer, rdsize, 0);
            if (size==4){
                // This is needed for registers to be written correctly
                *(volatile unsigned int *)(address) = *(unsigned int*)buffer;
                dsb();
                printf("Reg dword 0x%08X addr with value 0x%08X\n", address, *(unsigned int*)buffer);
            } else if (size==2){
                // This is needed for registers to be written correctly
                *(volatile unsigned short *)(address) = *(unsigned short*)buffer;
                dsb();
                printf("Reg short 0x%08X addr with value 0x%08X\n", address, *(unsigned short*)buffer);
            }
            else if (size==1){
                // This is needed for registers to be written correctly
                *(volatile unsigned char *)(address) = *(unsigned char*)buffer;
                dsb();
                printf("Reg byte 0x%08X addr with value 0x%08X\n", address, *(unsigned char*)buffer);
            }
            else {
                memcpy(address,buffer,size);
                }
            printf("Write %d Bytes to address 0x%08X\n", size, address);
            send_dword(0xD0D0D0D0);
            break;
        }
        case 0x5000: {
            apmcu_icache_invalidate();
            apmcu_disable_icache();
            apmcu_isb();
            apmcu_disable_smp();
            send_dword(0xD0D0D0D0);
            break;
        }
        case 0x6000: {
             mmc_init(&host);
             mmc_host_init(&host);
             send_dword(0xD1D1D1D1);
             break;
        }
        case 0x7000: { 
        emmc_read_test();            
        break;
        }
        default:
            printf("Invalid command\n");
            break;
        }
    }

    printf("Exiting the payload\n");

    while (1) {

    }
}
