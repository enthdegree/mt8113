#pragma once
#include "brom_types.h"

typedef struct {
  u32 type_word;        // +0x00
  u32 size_word;        // +0x04 (low 16 bits used in validator)
  u32 magic_a;          // +0x08
  u32 magic_b;          // +0x0c
  u8  unk_10[0x20];     // +0x10..+0x2f
  u8  tag_block_30[0x10]; // +0x30..+0x3f (memcmp 0x10 or 0x20 at ctx[4])
  u32 word_40;          // +0x40 (stored as ctx[5])
  u8  desc44[];         // +0x44 (validated by validate_desc44_block)
} boot0_image_descriptor_t;
typedef struct { u8 * boot0_base; u32 flags; u32 unk08; u8 * trailer_desc_ptr; } boot_header_t;


