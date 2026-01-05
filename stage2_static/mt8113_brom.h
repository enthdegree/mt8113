#include <stdint.h>
#include <stdbool.h>

#define BROM_MSDC_WAIT_IDLE_ACK_AND_UPDATE_ADDR ((uintptr_t)0x00003547u)
typedef void (*brom_msdc_wait_idle_ack_and_update_fn)(void);
static inline void brom_msdc_wait_idle_ack_and_update(void) {
    ((brom_msdc_wait_idle_ack_and_update_fn)BROM_MSDC_WAIT_IDLE_ACK_AND_UPDATE_ADDR)();
}



#define BROM_EMMC_SYNC_CONTROLLER_STATE_ADDR ((uintptr_t)0x00002F65u)
typedef void (*brom_emmc_sync_controller_state_fn)(void);
static inline void brom_emmc_sync_controller_state(void) {
    ((brom_emmc_sync_controller_state_fn)BROM_EMMC_SYNC_CONTROLLER_STATE_ADDR)();
}



#define BROM_EMMC_INIT_ADDR  ((uintptr_t)0x00002EDDu) // Thumb
typedef uint32_t (*brom_emmc_init_fn)(uint32_t media_desc);
static inline uint32_t brom_emmc_init(uint32_t media_desc) {
    return ((brom_emmc_init_fn)BROM_EMMC_INIT_ADDR)(media_desc);
}



#define BROM_DCACHE_MAINTENANCE_BEFORE_IO_ADDR ((uintptr_t)0x000003CBu)
typedef void (*brom_dcache_maintenance_before_io_fn)(void);
static inline void brom_dcache_maintenance_before_io(void) {
    ((brom_dcache_maintenance_before_io_fn)BROM_DCACHE_MAINTENANCE_BEFORE_IO_ADDR)();
}



typedef void (*brom_cache_fence_before_io_fn)(void);
#define BROM_CACHE_FENCE_BEFORE_IO_ADDR ((uintptr_t)0x000004E9u)
static inline void brom_cache_fence_before_io(void) {
    ((brom_cache_fence_before_io_fn)BROM_CACHE_FENCE_BEFORE_IO_ADDR)();
}



typedef void (*brom_msdc_start_emmc_boot_read_stream_fn)(uint8_t enable_special_mode);
#define BROM_MSDC_START_EMMC_BOOT_READ_STREAM_ADDR ((uintptr_t)0x00003607u)
static inline void brom_msdc_start_emmc_boot_read_stream(uint8_t enable_special_mode) {
    ((brom_msdc_start_emmc_boot_read_stream_fn)
        BROM_MSDC_START_EMMC_BOOT_READ_STREAM_ADDR)(enable_special_mode);
}



typedef uint32_t (*brom_msdc_maybe_wait_irq_bits_and_clear_fn)(void);
#define BROM_MSDC_MAYBE_WAIT_IRQ_BITS_AND_CLEAR_ADDR ((uintptr_t)0x0000381Fu)
static inline uint32_t brom_msdc_maybe_wait_irq_bits_and_clear(void) {
    return ((brom_msdc_maybe_wait_irq_bits_and_clear_fn)
            BROM_MSDC_MAYBE_WAIT_IRQ_BITS_AND_CLEAR_ADDR)();
}



#define BROM_MSDC_EMMC_STREAM_READ_ADDR  ((uintptr_t)0x000038BBu) // Thumb
typedef int (*brom_msdc_read_bytes_or_discard_fn)(
    uint32_t len,
    uint8_t *dest,
    uint32_t drain_only,   // 0 or 1
    uint32_t *status_io
);
static inline int brom_msdc_stream_read(uint32_t len,
                                        uint8_t *dest,
                                        uint32_t drain_only,
                                        uint32_t *status_io) {
    brom_msdc_read_bytes_or_discard_fn fn =
        (brom_msdc_read_bytes_or_discard_fn)BROM_MSDC_EMMC_STREAM_READ_ADDR;
    return fn(len, dest, drain_only, status_io);
}



typedef void (*brom_emmc_power_cycle_fn)(uint32_t short_power_cycle);
#define BROM_EMMC_POWER_CYCLE_ADDR ((uintptr_t)0x000035A3u)
static inline void brom_emmc_power_cycle(uint32_t short_power_cycle) {
    ((brom_emmc_power_cycle_fn)BROM_EMMC_POWER_CYCLE_ADDR)(short_power_cycle);
}
