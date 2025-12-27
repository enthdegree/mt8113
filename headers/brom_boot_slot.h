typedef struct {
  u8  state;            // enum-like
  u8  flags;
  u16 last_error;
  u32 base;
  u32 size;
  u32 media_id;
  u32 (*verify)(void);
  u32 (*prepare)(void);
  u32 (*launch)(void);  // often stubbed
} brom_boot_slot_t;




typedef struct {
  u8  pad_00[0x06];
  u16 media_type;     // +0x06 read as *(u16 *)(media_ptr + 6)
  u8  pad_08[0x14-0x08];
} boot_slot_media_desc_t;          // sizeof 0x14

typedef struct {
  u32 buf;            // +0x00 used as ctx_blob base (base+0x808)
  u32 buf_len;        // +0x04 (base+0x80c)
  u32 media_ptr;      // +0x08 points into base+0x21c + slot*0x14
  u8  unk_0c;         // +0x0c set to slot index
  u8  state;          // +0x0d state machine
  u16 status_u16;     // +0x0e status/error
  u16 extra_u16;      // +0x10 flags/extra
  u16 pad_12;         // +0x12
} boot_candidate_slot_rec_t;       // sizeof 0x14

typedef struct {
  u32 hdr_000;                          // +0x000 set to 5
  u8  pad_004[0x21c-0x004];             // +0x004..+0x21b
  boot_slot_media_desc_t media[8];      // +0x21c..+0x2bb
  u8  pad_2bc[0x808-0x2bc];             // +0x2bc..+0x807
  boot_candidate_slot_rec_t r[8];       // +0x808..+0x8a7
  u32 flags_8a8;                        // +0x8a8 ORs with 1 and 8
  u8  pad_8ac[0x93c-0x8ac];             // +0x8ac..+0x93b
} boot_candidate_slot_table_t;          // sizeof 0x93c



typedef struct boot_candidate_status {
  u8  _pad_00_0c[0x0d];  // +0x00..+0x0c
  u8  stage;             // +0x0d
  u16 status_u16;        // +0x0e
} boot_candidate_status_t;

typedef struct {
  // +0x00..0x0F — preserved across reset
  u32 preserve_00;
  u32 preserve_04;
  u32 preserve_08;
  u32 preserve_0c;

  // +0x10..
  // scratch / recomputed each cycle

  // current selected boot candidate status
  boot_candidate_status_t *current_status; // offset observed via accessor

  u8  _pad_14_to_98[0x98 - 0x14];

  // verified tag cache (mode 1)
  void *verified_layout0; // +0x98
  void *verified_tag2;    // +0x9C
  void *verified_tag7;    // +0xA0
  void *verified_tag8;    // +0xA4
  void *verified_tag3;    // +0xA8

  // next-stage handoff parameters
  void *next_param_1;     // +0xAC
  void *next_param_2;     // +0xB0
  void *next_param_3;     // +0xB4

  u8  _pad_tail[0xD4 - 0xB8];
} bootflow_t;

typedef struct {
  u8  unk_00[8];
  void *ptr_08;      // read as *(u32 *)(ctx_blob+8), then +6 used as mode_u16
  u8  unk_0c;
  u8  status_class;  // +0x0d set to 4
  u16 status_u16;    // +0x0e
  u16 aux_u16;       // +0x10 set once
  u8 table[];
} boot_slot_ctx_t;

extern brom_boot_slot_t boot_slot_table[];
