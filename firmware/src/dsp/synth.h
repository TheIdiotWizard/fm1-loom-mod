/* SPDX-License-Identifier: GPL-3.0-only */
/* XY-1 synth: a pool of voices playing eight engines (simple, prism, wavetable, axis, epiano, organ,
 * hardsync, dissolve), each voice with two ADSRs, a state-variable filter, an LFO and sends.
 *
 * A track's sound is a flat byte array par[NTP] (the OP-XY's four modules, four knobs each, plus the
 * play page, sends, player and mix). Continuous values are 0..127, switches small integers (ranges in
 * tp_info()). The same index space addresses parameter locks (seq/pattern.h lane_t.param), LFO
 * destinations and MIDI CCs. The drum engine's 11 sounds take TP_DRUM0 + sound * DP_N + DP_*.
 *
 * Threads: synth_render() runs in the audio interrupt; the main loop only writes par[] bytes and
 * calls synth_note_* through the engine's command queue (app/engine.c). Float, no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define SY_SR 44100.0f
#define NTRK 8                        /* instrument tracks (seq/pattern.h agrees) */
#define SY_CHUNK 64                   /* control rate: envelopes, LFO, glides, locks */
#define NVOICE 16                     /* synth voices, all tracks */
#define NVOICE_TRK 6                  /* the most one track takes */

enum { EG_SIMPLE, EG_PRISM, EG_WAVETABLE, EG_AXIS, EG_EPIANO, EG_ORGAN, EG_HARDSYNC, EG_DISSOLVE, EG_DRUMLINE,
       NENGINE };
extern const char *const EG_NAME[NENGINE];         /* "simple", "prism", ... "drumline" */
extern const char *const EG_KNOB[NENGINE][4];      /* each engine's four knob names, as the OP-XY prints them */

enum {
    TP_ENGINE, TP_E1, TP_E2, TP_E3, TP_E4,         /* Engine page: the engine and its four knobs */
    TP_VMODE, TP_PORTA, TP_BEND, TP_VOLUME,        /* Play page: poly / mono / legato, glide, bend range, volume */
    TP_A, TP_D, TP_S, TP_R,                         /* amp ADSR */
    TP_FA, TP_FD, TP_FS, TP_FR,                     /* filter ADSR */
    TP_CUT, TP_RES, TP_FENV, TP_KTRK, TP_FTYPE,     /* Filter page (+ type: LP12, LP24, HP, BP) */
    TP_SEND1, TP_SEND2, TP_SENDT,                   /* sends: fx I, fx II, tape */
    TP_LTYPE, TP_LSPEED, TP_LAMT, TP_LDEST, TP_LX,  /* LFO: type (off, value, tremolo, random, duck), speed, amount,
                                                     * destination (a TP_* index), extra (value: shape; tremolo:
                                                     * vibrato; duck: the source track; random: smooth) */
    TP_PLAYER, TP_P1, TP_P2, TP_P3, TP_P4,          /* Players: off / arp / maestro / hold and its four knobs */
    TP_LEVEL, TP_PAN,                               /* Mix */
    TP_BRAIN,                                       /* follows the brain: 0 no, 1 diatonic, 2 transpose only */
    TP_CHAN,                                        /* MIDI channel 0 = off, 1..16 */
    TP_DRUM0,                                       /* the drum kit: NDRUM sounds * DP_N */
    NTP = TP_DRUM0 + 11 * 6
};
enum { DP_MODEL, DP_TUNE, DP_TONE, DP_DECAY, DP_PAN, DP_LEVEL, DP_N };   /* a drum sound's knobs */
#define TP_DRUM(s, k) (TP_DRUM0 + (s) * DP_N + (k))
#define NTP_SOUND TP_PLAYER                          /* a preset keeps par[0..TP_PLAYER) */

enum { VM_POLY, VM_MONO, VM_LEGATO };
enum { FT_LP12, FT_LP24, FT_HP, FT_BP, NFT };
enum { LT_OFF, LT_VALUE, LT_TREMOLO, LT_RANDOM, LT_DUCK, NLT };
enum { LS_SPEED_SYNC_N = 13 };                     /* LFO speed 0..12 synced (8 bars .. 1/32), 13..127 free Hz */
enum { PL_OFF, PL_ARP, PL_MAESTRO, PL_HOLD, NPL };
/* arp: P1 speed (1/4 1/8 1/8T 1/16 1/16T 1/32), P2 pattern (up down updown random played), P3 range 1..4,
 * P4 hold; maestro: P1 roll 0..127 (strum), P2 pattern (up down updown random), P3 -, P4 hold */

/* how a value is shown */
enum { PK_NUM, PK_PCT, PK_BIP, PK_NAMES, PK_SEMI, PK_SWITCH, PK_MS, PK_HZ_SYNC };
typedef struct {
    const char *name;                 /* short, as on the knob row (<= 8 chars) */
    uint8_t lo, hi, def;
    uint8_t kind;                     /* PK_* */
    const char *const *names;         /* PK_NAMES */
} tp_info_t;
const tp_info_t *tp_info(int p, int engine);        /* engine: the track's (TP_E1..E4 names depend on it) */
void tp_text(int p, int v, int engine, char *num, char *unit);   /* the value as the screen shows it (<= 11, <= 4) */
void sound_default(uint8_t *par, int engine);       /* par[NTP] for a fresh track of this engine */

/* factory presets: the sound part of par[] */
typedef struct {
    const char *name;
    uint8_t par[NTP_SOUND];
} preset_t;
extern const preset_t PRESETS[];
extern const int NPRESETS;
int preset_first(int engine);                       /* the first preset of an engine, -1 */

/* --------------------------------------------------------------- voices --- */
/* performance state per track, written by the sequencer / punch-in FX, read by the voices each chunk */
typedef struct {
    int16_t pitch_c;                  /* cents, from bends, ramps, punch octave */
    int16_t cut;                      /* filter cutoff offset, -127..127 */
    int8_t pan;                       /* pan offset */
    uint8_t mute;                     /* 1 = silent (punch mute: voices keep running) */
    uint8_t send1, send2;             /* send offsets 0..127 (punch delay / reverb up) */
    uint8_t long_tail;                /* drums: decay x4 */
} perf_t;

typedef struct synth synth_t;         /* opaque: dsp/synth.c */
#define SYNTH_BYTES 12288             /* room for the struct (checked at compile time in synth.c) */

void synth_init(synth_t *s, uint8_t (*par)[NTP]);   /* par: the tracks' parameter arrays (NTRK of them) */
void synth_tempo(synth_t *s, float bpm);
/* notes: tag identifies a note for its off (the sequencer uses the step, the keys the key) */
void synth_note_on(synth_t *s, int trk, int note, int vel, int glide_ms, uint32_t tag);
void synth_note_off(synth_t *s, int trk, uint32_t tag);
void synth_all_off(synth_t *s, int trk);            /* -1: every track (panic) */
void synth_bend(synth_t *s, int trk, int cents);    /* performance bend (the bend component, MIDI) */
perf_t *synth_perf(synth_t *s, int trk);            /* the track's performance state (write from the interrupt) */
void synth_lock(synth_t *s, int trk, int param, int val);   /* a parameter lock in effect (LOCK_NONE: the knob's) */
/* render n frames (any n; chunks inside): dry L/R and the three sends, added into the buffers.
 * v0..v1 is the range of voices to render (the two cores split the pool) */
void synth_render(synth_t *s, int v0, int v1, float *l, float *r, float *s1, float *s2, float *st, uint32_t n);
int synth_sounding(const synth_t *s, int v0, int v1);          /* a voice in the range is sounding */
int synth_voices_used(const synth_t *s);
void synth_limit(synth_t *s, int max_voices);                  /* the overload guard: fewer voices */
extern volatile uint16_t synth_peak[NTRK];                     /* per track meters, Q15, decaying (UI) */
