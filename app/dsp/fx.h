/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 effects: the two send-effect tracks (fx I, fx II: one of chorus, delay, lofi, phaser, reverb each,
 * four knobs of its own; the reverb in one of eleven modes: the plate, or one of the PlayStation's SPU
 * reverbs) and the master section (3-band EQ with blend, the tape (TapeScam-lite), compressor, limiter, group
 * levels).
 *
 * All run in the audio interrupt. Buffers for the delay, the reverb and the master tape live in the pool;
 * the caller passes them in (their sizes are the memory plan's). Float, fastmath.h. */
#pragma once
#include <stdint.h>

enum { FXT_CHORUS, FXT_DELAY, FXT_LOFI, FXT_PHASER, FXT_REVERB, NFXT };
extern const char *const FXT_NAME[NFXT];
/* the reverb's modes: the plate (9W9's combs), then the SPU reverbs of the PlayStation (psx-spx's register
 * sets, as in schwung-psxverb): room, studio small / medium / large, hall, half echo, space echo, chaos echo
 * (almost endless), delay (one echo) */
enum { RV_PLATE, RV_ROOM, RV_STUDIO_S, RV_STUDIO_M, RV_STUDIO_L, RV_HALL, RV_HALF_ECHO, RV_SPACE_ECHO,
       RV_CHAOS_ECHO, RV_ECHO_ONE, NRV };
extern const char *const RV_NAME[NRV];         /* "plate", "room", "studio S", ... */
/* a track's TP_ENGINE byte names the effect and, for the reverb, its mode: the "pick", 0..NFXPICK-1:
 * chorus, delay, lofi, phaser, then the reverb's modes in order (FXT_REVERB + mode) */
#define NFXPICK (FXT_REVERB + NRV)
static inline int fx_pick_type(int pick) { return pick < FXT_REVERB ? (pick < 0 ? 0 : pick) : FXT_REVERB; }
static inline int fx_pick_mode(int pick) { return pick < FXT_REVERB ? 0 : pick - FXT_REVERB < NRV ? pick - FXT_REVERB : 0; }
const char *fx_pick_name(int pick);            /* "chorus" .. "phaser", "plate" .. "one echo" (the mode's name) */
const char *fx_knob_name(int pick, int k);     /* the four knobs' names (the plate's and the SPU modes' differ) */
/* the knobs and their defaults (stored 0..127), and what the defaults sound like:
 *   chorus      rate depth feedback width        50 86 20 127   0.33 Hz, +-5.5 ms, light feedback, the sides opposite
 *   delay       time fine repeats tone           72 64 56 64    1/8 ping-pong, ~4 repeats, repeats low-passed at 4 kHz
 *   lofi        rate bits smooth width           80 80 64 40    8.4 kHz, 11 bits, half smoothed, the right 3.4 ms late
 *   phaser      freq depth rate feedback         50 90 50 80    466 Hz, +-2.1 octaves, 0.27 Hz, feedback 0.57
 *   plate       size motion tone damp            100 40 44 72   combs x1.43 (feedback 0.88), a little wander, 7.4 kHz, damp 0.45
 *   SPU modes   size decay tone damp             127 64 64 0    the console's own: its delays (x4 of the registers), its
 *                                                               wall feedback, flat, its own reflection filter
 * delay time: 1/32 1/32D 1/16 1/16D 1/8 1/8D 1/4 1/2 of the tempo, fine +-25 %; delay tone: the repeats' low-pass,
 * 800 Hz .. 19 kHz; reverb tone: a low-pass below the centre, a high-pass above it; plate damp: the combs' damping.
 * SPU size: the delays from x1 of the registers (schwung-psxverb's) to x4 (the console's: the registers count
 * 8-byte units); SPU decay: the wall reflection's feedback, 0.5x .. x1 (64) .. as far as it stays stable;
 * SPU damp: the reflections' low-pass (vIIR) closes. The FX tracks are plain send / return: an effect's output
 * is the wet sound only. */
extern const uint8_t FXT_DEFAULT[NFXT][4];      /* the reverb's row: the plate's */
extern const uint8_t RV_SPU_DEFAULT[4];         /* the SPU modes' */

#define FX_DLY_LEN 22400              /* int16 samples a side: 0.5 s plus the line's 256-sample margin (the pool's budget) */
#define FX_RV_LEN 32768               /* int16 samples: the reverb's lines (the plate uses 11.4 k of them; an SPU mode at
                                       * x4 up to 32766: the chaos echo's and the delay's 1FFFh x 4) */
typedef struct fxslot fxslot_t;
#define FXSLOT_BYTES 5376             /* the chorus line (2048 int16: 4 KB, the lofi's spread reads it too) and the states */

void fx_init(fxslot_t *f, int16_t *dly_l, int16_t *dly_r, int16_t *rv);   /* a slot gets the lines it may use;
                                                                            * a slot without a reverb line (0) has no
                                                                            * reverb (fx I: the memory plan) */
void fx_set(fxslot_t *f, int pick, const uint8_t knob[4]);  /* the slot's effect (a pick: the type and the reverb's
                                                             * mode) and its four knobs (0..127) */
void fx_tempo(fxslot_t *f, float bpm);
void fx_filter(fxslot_t *f, int hp, int lp);                /* the slot's filter page: high-pass, low-pass 0..127 */
/* process n frames of the send (mono in) into out L/R (added) */
void fx_process(fxslot_t *f, const float *in, float *out_l, float *out_r, uint32_t n);
int fx_has(const fxslot_t *f, int pick);                    /* the slot can do this effect (reverb needs its line) */
int fx_awake(const fxslot_t *f);                            /* the slot works this block: its input heard, or its line
                                                            * still ringing (a delay or reverb sleeps on silence; the
                                                            * others always work) */

/* ---- master ---- */
/* the master's knobs, three pages of four: EQ (low, mid, high, blend), tape (TapeScam-lite: drive, color,
 * wobble, noise; all four at 0 = the tape out of the path), out (perc, mel, comp, pump); then the out page's
 * list (SEL): what the pump follows, its release, accent, and the level (the drive into the limiter).
 * PUMP (X0X's master PUMP): every hit of its source lowers the whole mix by up to 24 dB (rising in 12 ms) and
 * lets it swell back over the release; it is keyed by the sequencer, not by audio, so a muted track drives it
 * too. pump by: PB_KICK any kick of a kit (its first key, or a key on the kick model), PB_T1.. any note of
 * track 1..8, PB_METRO the metronome's click. release 0..127: 10 .. 1500 ms (60 = 107 ms, X0X's); accent
 * 0 / 1: a hit pumps by its velocity (0.5 .. 1 of the depth) or always fully. */
enum { MS_EQ_LO, MS_EQ_MID, MS_EQ_HI, MS_EQ_BLEND, MS_TP_DRIVE, MS_TP_COLOR, MS_TP_WOBBLE, MS_TP_NOISE,
       MS_PERC, MS_MEL, MS_COMP, MS_PUMP, MS_PUMP_BY, MS_PUMP_REL, MS_PUMP_ACC, MS_LEVEL, NMS };
#define MS_NKNOB 12                   /* the knob pages' (3 x 4); MS_PUMP_BY.. are the out page's list */
enum { PB_KICK, PB_T1, PB_METRO = PB_T1 + 8, NPB };
#define MS_PUMP_REL_DEF 60
extern const char *const MS_NAME[NMS];
#define MS_TP_COLOR_DEF 0             /* a new project's tape: out of the path (it was 24: a little colour; the demos
                                       * measured it taking 2-9 dB off the top octave and squashing the transients) */
typedef struct master master_t;
#define MASTER_BYTES 1280
#define MST_TAPE_DLY 512              /* the tape's wow / flutter line, floats a side (5 ms + 4.2 ms of swing at 44.1 kHz) */
void master_init(master_t *m, float *tape_l, float *tape_r);   /* the tape's two lines (the pool), MST_TAPE_DLY each */
void master_set(master_t *m, int p, int v);                 /* 0..127 (EQ blend 0, the tape's four 0 = neutral;
                                                             * pump by 0..NPB-1, accent 0..1) */
int master_max(int p);                                      /* a value's top: 127, NPB - 1 or 1 */
float master_pump_ms(int v);                                /* the pump's release, ms */
/* a hit on pump source src (PB_*), velocity 1..127: the pump drops if it follows src (the audio interrupt) */
void master_key(master_t *m, int src, int vel);
/* perc and mel: the two groups (stereo, in place), summed with the fx returns (added into mel) */
void master_process(master_t *m, float *perc_l, float *perc_r, float *mel_l, float *mel_r, float *out_l, float *out_r,
                    uint32_t n);
float master_gr_db(const master_t *m);                      /* the compressor's gain reduction (UI) */
float master_pump_db(const master_t *m);                    /* the pump's, now (UI: falls back slowly) */
#ifdef PLAT_HOST
extern int fx_host_stage;                                   /* the host's master taps (host.c LOOM_MST) */
#endif
