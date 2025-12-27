u32 validate_loaded_boot0_header(image_layout_desc_t *);
u32 sanity_check_storage_info(image_layout_desc_t *);

typedef struct {
  u32 unk0;      // +0x00
  u32 unk4;      // +0x04
  u8 *base;      // +0x08  (used as base + g_layout_base_offset)
} layout_base_carrier_t;

struct layout_out_t {
    u32 out_word_1a;
    u32 out_total_len;
    struct layout_base_carrier_t *carrier;
};

