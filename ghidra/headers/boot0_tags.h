typedef struct {
  u32 magic;
  u32 unk_04;
  u16 len_08;
  u16 unk_0a;
  u32 typeword_0c;
} boot0_tag_hdr_t;

typedef struct {
  u32 magic_00;      // +0x00
  u32 unk_04;        // +0x04
  u16 len_08;        // +0x08 must be 0x052c
  u16 unk_0a;        // +0x0a
  u8  desc44_10[];   // +0x10 validated by validate_desc44_block(tag + 0x10, 0)
  // ...
  u8  region_428[];  // +0x428 passed to FUN_000079e0(1, tag + 0x428)
} boot0_tag1_kind5_t;

typedef struct {
  u16 mode;       // +0x00 must be 1 or 2
  u16 count_02;   // +0x02 <= 0x1f if mode==1, <=0xdf if mode==2
  u8  rest[];     // +0x04
} boot0_tag1_region_428_t;

typedef struct {
  u32 magic_00;      // +0x00
  u32 unk_04;        // +0x04
  u16 len_08;        // +0x08 must be 0x0348
  u16 unk_0a;        // +0x0a
  u8  body[];        // +0x0c
} boot0_tag3_t;

typedef struct {
  u32 magic_00;      // +0x00
  u32 unk_04;        // +0x04
  u16 len_08;        // +0x08 must be 0x013c
  u16 unk_0a;        // +0x0a
  u8  body[];        // +0x0c
} boot0_tag4_t; 

typedef struct {
  u16 tag_id;
  u16 _pad;
  u32 offset;
  u32 size;
} boot0_sig5_index_entry_t;

typedef struct {
  u8  *trailer_ptr;
  u32 entry_count;
  boot0_sig5_index_entry_t e[4];
} boot0_sig5_index_out_t;

u32 build_sig5_trailer_index(image_layout_desc_t *, boot0_sig5_index_out_t *);
