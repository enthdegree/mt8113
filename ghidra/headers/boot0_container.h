typedef struct {
  char magic[5];     // "BRLYT"
  u8  pad;
  u16 count;
  // followed by entries
} brlyt_hdr_t;
