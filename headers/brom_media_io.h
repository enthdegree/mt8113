typedef struct {
  char magic[10];    // "EMMC_BOOT"
  u8   nul;
  u8   pad_0b[3];
  u32  header_size;  // usually 0x200
} boot0_emmc_boot_hdr_t;

u32 read_boot0_emmc_boot_and_brlyt_headers(u32 *ctx, brom_fn_t cb);

typedef struct {
    u32 status;   // 0xff means error
    u32 lane;     // 1..3
} lane_sel_ret_t;
lane_sel_ret_t msdc_select_lane(u32 padsel, u32 lane);
void msdc_write_lane_cfg(u32 padsel, u32 which, u32 lane, u32 value3);

typedef enum {
  MSDC_TUNE_FIELD_1 = 1,
  MSDC_TUNE_FIELD_2 = 2,
  MSDC_TUNE_FIELD_3 = 3,
} msdc_tune_field_t;

