#pragma once
#include "brom_types.h"   // u8/u16/u32

/* -------------------------------------------------------------------------- */
/* Verify context (passed into verify_image_integrity)                          */
/* -------------------------------------------------------------------------- */

typedef void (*stage_hook_t)(u32 stage, u32 enter);        // called with (7/8, 0/1)
typedef void (*report_t)(u32 stage, u32 unused, u32 a, u32 b);

typedef struct {
    u32          a0;         // used as ctx->a0 (candidate image pointer)
    u32          a1;         // used as ctx->a1 (candidate image length)
    stage_hook_t stage_hook; // may be 0
    report_t     report;     // may be 0
} verify_ctx_t;

/* -------------------------------------------------------------------------- */
/* Common IO pair                                                              */
/* -------------------------------------------------------------------------- */

typedef struct {
    u8  *ptr;
    u32  len;
} io_pair_t;

/* All policy ops in this file use this call shape in verify_image_integrity. */
typedef void *(*policy_fn_t)(verify_ctx_t *ctx, u32 image_id_or_slot, void *io, void *out);

/* -------------------------------------------------------------------------- */
/* Stage7 ops table (policy->ops_1c points here)                               */
/* -------------------------------------------------------------------------- */

typedef struct {
    policy_fn_t op0; // called first  : (ctx,id,&io_pair,out_hash_or_scratch)
    policy_fn_t op1; // called second : (ctx,id,&io_pair,digest32_out[0x20])
    policy_fn_t op2; // called third  : (ctx,id,&io_pair,out_hash_or_scratch)
} stage7_ops_t;

/* -------------------------------------------------------------------------- */
/* Stage8 blob (policy->stage8_blob points here)                               */
/* -------------------------------------------------------------------------- */

typedef struct {
    u32        kind_or_len_00; // observed: sometimes 0x180 elsewhere; treat as opaque
    policy_fn_t init_04;       // called first in stage8 path
    policy_fn_t fini_08;       // called third in stage8 path
    policy_fn_t cleanup_0c;    // called on one error path before returning
    policy_fn_t update_10;     // called second in stage8 path
} stage8_blob_t;

/* verify_image_integrity builds this on-stack and passes &io as “io” to stage8 fns */
typedef struct {
    u8   *expected_ptr;  // +0x00
    u32   expected_len;  // +0x04
    u8   *image_ptr;     // +0x08 (image_source)
    u32   image_len;     // +0x0c
    u32   flags;         // +0x10 (verify_image_integrity sets to 1)
    void *key_desc_ptr;  // +0x14 (copied from policy->key_desc_ptr_00)
} stage8_io18_t; // sizeof 0x18

/* -------------------------------------------------------------------------- */
/* Policy object as used by verify_image_integrity                             */
/* -------------------------------------------------------------------------- */

typedef struct {
    void        *key_desc_ptr_00;   // used only when stage8 enabled (must be non-NULL)

    stage8_blob_t *stage8_blob_04;  // callbacks for stage8 path (must be non-NULL when enabled)

    u32          _pad_08;           // unknown / unused here
    u32          _pad_0c;           // unknown / unused here
    u32          _pad_10;           // unknown / unused here
    u32          _pad_14;           // unknown / unused here

    /* RSA-PSS template bytes: verify_image_integrity copies 0x1c bytes from &templ_18 */
    u32          pss_templ_word_18; // first word of template (for alignment / copying)
    u8           pss_templ_1c[0x1c];// total copied size is 0x1c starting at &pss_templ_word_18

    /* Points to 3-function table: called as *(ops+0), *(ops+8), *(ops+4) in that order. */
    stage7_ops_t *ops_1c;           // verify_image_integrity calls ops_1c->op0/op1/op2

    /* Hash IO pair: verify_image_integrity uses both ptr and len.
       NOTE: bit0 of flags_44 gates stage8 usage; flags_44 is checked via shifts too. */
    io_pair_t     hash_io_44;       // ptr used as “hash io ptr”; len used as token_len

    u8           *expected_ptr_3c;  // compared against digest (or used as expected buffer)
    u32           expected_len_40;  // length for compares / RSA verify input

    /* Flags word. Bits observed in verify_image_integrity:
       - bit0: stage8 enabled (if set, stage8_blob/key_desc must be valid)
       - bit1 (low byte): if set, copies digest32 to out_hash_or_scratch
       - bit25: select RSA-PSS-SHA256 verify mode (set after stage8 path)
       - bit26: select RSA-PKCS1v1.5 verify mode (set after stage8 path)
       Treat all other bits as opaque.
    */
    u32           flags_44;

    u32           token_len_48;     // compared against expected_len_40; used in memcmp length
} verification_policy_ops_t;

/* -------------------------------------------------------------------------- */
/* Placeholders you can keep without committing to layout yet                   */
/* -------------------------------------------------------------------------- */

typedef struct {
    u8 raw[0x34];
} stage8_state_t;
