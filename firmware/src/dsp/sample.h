/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 samples: the factory bank compiled into the image (build/gen/samples.h, tools/gen_samples.py) and
 * the user bank in flash (0xCF000..0xDBFFF, 52 KB, uploaded over USB with tools/loom_samples.py), both
 * mono IMA ADPCM at 22050 Hz, read in place (the user bank through the XIP plaintext window). One index
 * space: 0..SMP_NFACT-1 the factory samples, then SMP_NUSER user slots (empty ones play nothing).
 *
 * Every SMP_BLK samples a checkpoint holds the decoder's state, so a voice reads any sample through a
 * small cache (smp_cache_t, 80 bytes: a window of SMP_WIN + 1 decoded samples and the decoder's state after
 * it). Forward, the next window carries on from that state (32 IMA steps per 32 samples); backward or after
 * a jump (a loop, a start anywhere) it starts again from the window's checkpoint (33 or 65 steps). The
 * format and the user bank's layout: tools/sampleio.py.
 *
 * Threads: smp_get / smp_at run in the audio interrupt; smp_user_drop / smp_user_scan in the main loop
 * (the upload). A drop makes every user sample vanish at once (smp_get returns 0 from then on: a voice
 * reading one stops) before the flash under them is erased. */
#pragma once
#include <stdint.h>

#define SMP_RATE 22050
#define SMP_BLK 64                        /* samples per checkpoint (a power of two) */
#define SMP_WIN 32                        /* samples a cache window holds (and the next one's first) */
#define SMP_NUSER 16                      /* the user bank's slots */
#define SMP_NAME 12
#define SMP_BANK_BASE 0xCF000u            /* flash offset of the user bank */
#define SMP_BANK_SIZE 0xD000u             /* 52 KB: 0xCF000 .. 0xDBFFF */
#define SMP_BANK_HDR 1024u                /* the header; the data follows */
#define SMP_BANK_MAGIC 0x504D534Cu        /* "LSMP" */
#define SMP_BREAK 7                       /* the factory break (tools/gen_samples.py): the slice engine's default */
#define SMP_BARS_MAX 16                   /* a user entry's bars above this: not stated */

typedef struct {
    const uint8_t *adp;                   /* IMA ADPCM, the low nibble first */
    const uint32_t *ck;                   /* a checkpoint a block: (predictor & 0xFFFF) | index << 16 */
    uint32_t n;                           /* samples */
    uint32_t ls, le;                      /* its loop (le 0: none) */
    uint8_t root;                         /* the MIDI note it plays at its own rate (the slice engine: slice 1's) */
    uint8_t bars;                         /* a loop of this many bars (the slice engine follows the tempo with
                                           * it), 0: not stated */
    char name[SMP_NAME];
} smp_t;

typedef struct {
    int32_t w;                            /* the window's first sample (a multiple of SMP_WIN), -1 none */
    const smp_t *s;                       /* of this sample */
    int16_t pred;                         /* the decoder after the window's last sample (b[SMP_WIN]) */
    uint8_t idx, rsv;
    int16_t b[SMP_WIN + 1];               /* its samples and the next window's first */
} smp_cache_t;

int smp_slots(void);                      /* factory + user slots: a knob's range */
int smp_factory(void);                    /* the factory samples */
const smp_t *smp_get(int i);              /* 0: nothing there (an empty user slot, the bank being written) */
const char *smp_name(int i);              /* "kick", an upload's name, "user 3" for an empty slot */
void smp_fill(smp_cache_t *c, const smp_t *s, int32_t w);     /* decode the window from sample w (a multiple of
                                                                 * SMP_WIN) into the cache */
static inline void smp_cache_reset(smp_cache_t *c) { c->w = -1; c->s = 0; }
/* sample i (0 outside the sample), as a float -1..1 */
static inline float smp_one(smp_cache_t *c, const smp_t *s, int32_t i)
{
    if (i < 0 || (uint32_t)i >= s->n)
        return 0.0f;
    if (i - c->w >= SMP_WIN || i < c->w || c->s != s)
        smp_fill(c, s, i & ~(SMP_WIN - 1));
    return (float)c->b[i - c->w] * (1.0f / 32768.0f);
}
/* play: up to m samples into out[], linearly interpolated, from the position *pos + *fr stepping inc a
 * sample (dir +1 forward, -1 backward) while the position stays in [lo, hi) (clipped to the sample); returns
 * how many it wrote: fewer than m when the position left the range (the caller loops, bounces or ends).
 * *pos and *fr are left where it stopped */
int smp_play(smp_cache_t *c, const smp_t *s, int32_t *pos, float *fr, float inc, int dir, int32_t lo, int32_t hi,
             float *out, int m);

/* the user bank */
void smp_user_attach(const uint8_t *base);   /* where it reads (the XIP window, a RAM image on the host) */
int smp_user_scan(void);                  /* (re)read its header: the samples it holds, 0 = none / not valid */
void smp_user_drop(void);                 /* about to be rewritten: the user samples vanish now */
int smp_user_count(void);
uint32_t smp_user_bytes(void);            /* its data, bytes */
uint32_t smp_crc32(const uint8_t *p, uint32_t n);
