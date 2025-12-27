# Some information about the boot0 partition's file format 

The boot0 partition contains several distinct and nested data containers.
The integrity of this data is checked by the brom.

# EMMC_BOOT header 

Todo

# MMM records

Past the *EMMC_BOOT header* are a list of *MMM records* starting at a 4-byte-aligned address `base`.
The first of the records is a header record. 

## Header record 
The first record, the one starting at `base`, has special attributes.[1]

- `base` begins with bytes `4d 4d 4d`
- `base + 0x6` has bytes `00 00`
- `base + 0x8` is the null-terminated string `FILE_INFO` 

- `base + 0x28` is a u32 `record_region_len` between 0 and 0x100000
  - `base + header_len` marks the first bit of non-header data.
- `base + 0x20` is a u32 `image_len` between `record_region_len` and `0x80000000`, inclusive
  - `base + image_len` marks the end of the image data 
  - at that address and higher is some padding with `ff` s, maybe up to the boundary of its emmc read block

## Other records
Records start at address `base`.[2] 
A record at `record_base` has these attributes:
- `record_base < base + record_region_len`
- It starts with `4d 4d 4d` 
  - ...or it's a sentinel, starting with bytes `45 45 45 45`. Records past a sentinel can't be read.

- `base + 4` is a u16 `record_len` 
  - `record_len > 8`
  - `record_len` is divisible by 4 
  - `record_base + record_len < base + record_region_len`

- `record_base + 6` is a u16 `record_id`

Meaning of records and what flags matter for validation (spans, algorithm, etc.) are unknown. (TODO) 

---

[1]: They are checked by a function at 0x0000d9f4 of the brom, `validate_first_brom_record(char *base)`

[2]: They are walked/gotten using a function at 0x0000d948 of the brom, `u32 find_nth_mmm_record_by_id(char *base, uint unused_zero, u32 record_id, char *out, u32 n)`
