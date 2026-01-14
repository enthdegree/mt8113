#include <stdint.h>

#include "tools.h"
#include "printf.h"
#include "libc.h"
#include "drivers/sleepy.h"
#include "mt8113_emmc.h"

// USB buffer length for usbdl_put_data chunks
// Larger chunks = better throughput
#define USBDL_CHUNK_SIZE 256

extern const char* u32_to_str(uint32_t v);

void recv_data(char *addr, uint32_t sz, uint32_t flags __attribute__((unused))) {
    for (uint32_t i = 0; i < (((sz + 3) & ~3) / 4); i++) {
        ((uint32_t *)addr)[i] = __builtin_bswap32(recv_dword());
    }
}

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

static inline void dsb(void) {
    asm volatile ("dsb" ::: "memory");
}

const char* u32_to_str(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    static char buf[9];

    for (int i = 0; i < 8; i++) {
        buf[i] = hex[(v >> (28 - i * 4)) & 0xF];
    }
    buf[8] = '\0';

    return buf;
}

int main() {
    searchparams();
    char buf[0x200] = { 0 };

    printf("(c) xyz, k4y0z, bkerler 2019-2021\n");
    printf("mt8113 emmc r/w (c) enthdegree et. al. 2026\n");

//    while(1) {}

    // Initialize eMMC before entering command loop
    emmc_init();

    printf("Entering command loop\n");
    char buffer[0x200]={0xff};
    send_dword(0xB1B2B3B4);

    while (1) {
        //printf("Waiting for cmd\n");
        memset(buf, 0, sizeof(buf));    
        uint32_t magic = recv_dword();
        if (magic != 0xf00dd00d) {
            printf("Protocol error\n");
            printf("Magic received = 0x%s\n", u32_to_str(magic));
            break;
        }
        uint32_t cmd = recv_dword();
    //printf("cmd 0x%s\n", u32_to_str(cmd));
    switch (cmd) {
        case 0x1000: {
             emmc_init();
             send_dword(0xD1D1D1D1);
             break;
        }
        case 0x1001: {
            uint32_t region = recv_dword();
            uint32_t block = recv_dword();
            //printf("Read region 0x%s sector 0x%s\n", u32_to_str(region), u32_to_str(block));
            memset(buf, 0, sizeof(buf));
            if (emmc_read_sector(region, block, (uint32_t*)buf) != 0) {
                printf("Read error!\n");
            } else {
                //printf("Read succeeded, sending data...\n");
                for (int i = 0; i < 0x200; i += USBDL_CHUNK_SIZE) {
                    usbdl_put_data(&buf[i], USBDL_CHUNK_SIZE);
                }
            }
            break;
        }
        case 0x1002: {
            uint32_t region = recv_dword();
            uint32_t block = recv_dword();
            //printf("Write region 0x%s sector 0x%s\n", u32_to_str(region), u32_to_str(block));
            memset(buf, 0, sizeof(buf));
            usbdl_get_data(buf, 0x200, 0);
            if (emmc_write_sector(region, block, (uint32_t*)buf) != 0) {
                printf("Write error!\n");
            } else {
                //printf("OK\n");
                send_dword(0xD0D0D0D0);
            }
            break;
        }
        case 0x1003: {
            // Dump EXT_CSD register
            printf("Dumping EXT_CSD\n");
            uint8_t ext_csd[512];
            memset(ext_csd, 0, sizeof(ext_csd));

            if (emmc_read_ext_csd(ext_csd) != 0) {
                printf("EXT_CSD read failed!\n");
                // Still send data even on error (all zeros)
            }

            printf("Read succeeded, sending data...\n");
            for (int i = 0; i < 0x200; i += USBDL_CHUNK_SIZE) {
                usbdl_put_data(&ext_csd[i], USBDL_CHUNK_SIZE);
            }
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
            printf("Read %d Bytes from address 0x%s\n", size, u32_to_str(address));
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
                printf("Reg dword 0x%s addr with value 0x%s\n", u32_to_str((uint32_t)address), u32_to_str(*(unsigned int*)buffer));
            } else if (size==2){
                // This is needed for registers to be written correctly
                *(volatile unsigned short *)(address) = *(unsigned short*)buffer;
                dsb();
                printf("Reg short 0x%s addr with value 0x%s\n", u32_to_str((uint32_t)address), u32_to_str(*(unsigned short*)buffer));
            }
            else if (size==1){
                // This is needed for registers to be written correctly
                *(volatile unsigned char *)(address) = *(unsigned char*)buffer;
                dsb();
                printf("Reg byte 0x%s addr with value 0x%s\n", u32_to_str((uint32_t)address), u32_to_str(*(unsigned char*)buffer));
            }
            else {
                memcpy(address,buffer,size);
                }
            printf("Write %d Bytes to address 0x%s\n", size, u32_to_str((uint32_t)address));
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
        case 0x7000: { 
            emmc_boot0_verify_test();            
            //emmc_roundtrip_test(); // /!\ Dangerous to uncomment this
            printf("emmc_roundtrip_test which involves read/write is disabled in this build.\nUncomment the above line in stage2.c to enable it.\n");
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
