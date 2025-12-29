typedef struct {
  u32 media_id;
  u32 (*reader)(u32 *ctx, brom_fn_t cb);
} brom_media_reader_t;

extern brom_media_reader_t bl2_media_reader_table[];
