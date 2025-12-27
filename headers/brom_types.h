typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed int     s32;

typedef u32 (*brom_fn_t)(void);


extern volatile u32 *g_cfgword_ef5c;

static inline u32 cfgword_bit(unsigned k) { return (*g_cfgword_ef5c >> k) & 1; }

static inline u32 cfgword_bit12(void) { return cfgword_bit(12); }
static inline u32 cfgword_bit7(void)  { return cfgword_bit(7);  }
static inline u32 cfgword_bit6(void)  { return cfgword_bit(6);  }
static inline u32 cfgword_bit3(void)  { return cfgword_bit(3);  }
static inline u32 cfgword_mode_1_or_2(void) { return cfgword_bit(1) + 1; }
