#include <stdint.h>
#include <stdbool.h>

extern void dump_u32_bytes(const char *label, uint32_t v);
int buffers_equal(uint32_t *a, uint32_t *b, int words);
void msdc_wait_cmd_ready(void);
void msdc_clear_fifo(void);
int msdc_wait_int(uint32_t mask, uint32_t timeout_val);
int msdc_send_cmd(uint8_t cmd_idx, uint32_t arg, uint32_t flags);
int msdc_wait_card_ready(void); 

void emmc_init(void);
int emmc_switch_partition(uint32_t partition);
int emmc_read_block(uint32_t partition, uint32_t block_num, uint32_t *buffer);
int emmc_write_block(uint32_t partition, uint32_t block_num, uint32_t *buffer);
void emmc_roundtrip_test(void);
void emmc_boot0_verify_test(void); 

