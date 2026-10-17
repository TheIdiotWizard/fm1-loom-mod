/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 effects: the two send-effect tracks (fx I, fx II: one of chorus, delay, distortion, lofi,
 * phaser, reverb each, four knobs of its own), the tape loop and the master section
 * (3-band EQ with blend, saturator, compressor, limiter, group levels).
 *
 * All run in the audio interrupt. Buffers for the delay, the reverb and the tape live in the pool;
 * the caller passes them in (their sizes are the memory plan's). Float, fastmath.h. */
#pragma once
#include <stdint.h>

enum { FXT_CHORUS, FXT_DELAY, FXT_DIST, FXT_LOFI, FXT_PHASER, FXT_REVERB, NFXT };
extern const char *const FXT_NAME[NFXT];
extern const char *const FXT_KNOB[NFXT][4];
/* the knobs and their defaults (stored 0..127), and what the defaults sound like:
 *   chorus      rate depth feedback width        50 86 20 127   0.33 Hz, +-5.5 ms, light feedback, the sides opposite
 *   delay       time fine repeats tone           72 64 56 64    1/8 ping-pong, ~4 repeats, repeats low-passed at 4 kHz
 *   distortion  drive edge low cut high cut      40 32 40 100   3.7x, mostly tanh, 85 Hz .. 9.6 kHz
 *   lofi        rate bits smooth width           80 80 64 40    8.4 kHz, 11 bits, half smoothed, the right 3.4 ms late
 *   phaser      freq depth rate feedback         50 90 50 80    466 Hz, +-2.1 octaves, 0.27 Hz, feedback 0.57
 *   reverb      size motion tone damp            100 40 44 72   combs x1.43 (feedback 0.88), a little wander, 7.4 kHz, damp 0.45
 * delay time: 1/32 1/32D 1/16 1/16D 1/8 1/8D 1/4 1/2 of the tempo, fine +-25 %; delay tone: the repeats' low-pass,
 * 800 Hz .. 19 kHz; reverb tone: a low-pass below the centre, a high-pass above it; reverb damp: the combs' damping.
 * The FX tracks are plain send / return: an effect's output is the wet sound only. */
extern const uint8_t FXT_DEFAULT[NFXT][4];

#define FX_DLY_LEN 22400              /* int16 samples a side: 0.5 s plus the line's 256-sample margin (the pool's budget) */
#define FX_RV_LEN 12000               /* int16 samples: the reverb's combs and allpasses */
typedef struct fxslot fxslot_t;
#define FXSLOT_BYTES 5120             /* the chorus line (2048 int16: 4 KB, the lofi's spread reads it too) and the states */

void fx_init(fxslot_t *f, int16_t *dly_l, int16_t *dly_r, int16_t *rv);   /* a slot gets the lines it may use;
                                                                            * fx II gets no reverb line (0) if the
                                                                            * memory plan says so: it then has no reverb */
void fx_set(fxslot_t *f, int type, const uint8_t knob[4]);  /* the slot's effect and its four knobs (0..127) */
void fx_tempo(fxslot_t *f, float bpm);
void fx_filter(fxslot_t *f, int hp, int lp);                /* the slot's filter page: high-pass, low-pass 0..127 */
/* process n frames of the send (mono in) into out L/R (added) */
void fx_process(fxslot_t *f, const float *in, float *out_l, float *out_r, uint32_t n);
int fx_has(const fxslot_t *f, int type);                    /* the slot can do this effect (reverb needs its line) */

/* ---- tape: a quarter-rate mono ring of the last 16 steps; the keys loop a segment while held ---- */
#define TAPE_RATE 4                   /* 44100 / 4 */
#define TAPE_LEN 24576                /* int16 samples: 2.23 s, 16 steps down to 108 BPM at scale 1 (the pool's budget) */
/* the ring wraps by comparison, not a mask: the length need not be a power of two */
#define TAPE_WRAP(i) ((i) >= TAPE_LEN ? (i) - TAPE_LEN : (i) < 0 ? (i) + TAPE_LEN : (i))
typedef struct tape tape_t;
#define TAPE_BYTES 512
void tape_init(tape_t *t, int16_t *buf);
/* the TAPE track's four knobs: pitch in octaves (0..127 -> -2, -1, 0 (x1), +1, +2 octaves, in five steps), speed
 * 50..200 % (64 = 100 %), length 1..16 steps, mix wet / dry */
void tape_set(tape_t *t, int pitch, int speed, int length, int mix);
void tape_bar(tape_t *t, uint32_t samples_per_step);        /* the sequencer tells it a step's length (16 = a loop) */
void tape_segment(tape_t *t, int seg, int on);              /* a key / a step: segment 0..15 looping while on */
/* in: the routed tracks' send (mono), recorded unless a segment loops; out L/R: the loop, added;
 * returns the dry gain (1 - mix while looping) the caller applies to the routed tracks */
float tape_process(tape_t *t, const float *in, float *out_l, float *out_r, uint32_t n);
int tape_active(const tape_t *t);

/* ---- master ---- */
enum { MS_EQ_LO, MS_EQ_MID, MS_EQ_HI, MS_EQ_BLEND, MS_SAT_GAIN, MS_SAT_CLIP, MS_SAT_TONE, MS_SAT_BLEND,
       MS_PERC, MS_MEL, MS_COMP, MS_LEVEL, NMS };
extern const char *const MS_NAME[NMS];
typedef struct master master_t;
#define MASTER_BYTES 1024
void master_init(master_t *m);
void master_set(master_t *m, int p, int v);                 /* 0..127 (blends 0 = neutral) */
/* perc and mel: the two groups (stereo, in place), summed with the fx returns (added into mel) */
void master_process(master_t *m, float *perc_l, float *perc_r, float *mel_l, float *mel_r, float *out_l, float *out_r,
                    uint32_t n);
float master_gr_db(const master_t *m);                      /* the compressor's gain reduction (UI) */
