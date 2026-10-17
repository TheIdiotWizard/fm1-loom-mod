/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 data model: tracks, patterns, steps, lanes (parameter locks), scenes and the song.
 *
 * Plain data, no pointers, so a pattern copies with an assignment and the project stores the bytes
 * (X0X's rule). The UI edits these in place from the main loop while the sequencer reads them in the
 * audio interrupt: every field the sequencer reads is a single byte or an aligned 32-bit word, so a
 * read never sees half a write. */
#pragma once
#include <stdint.h>

/* The code keeps its first names; the screen and the manual say: AUX_BRAIN the conductor (COND), AUX_PUNCH the
 * throws (THROW), components (SC_*) tricks, the drumline engine the kit, the maestro player strum. */
#define NTRK 8                        /* instrument tracks 1-8 */
enum { AUX_BRAIN = NTRK, AUX_PUNCH, AUX_FX1, AUX_FX2, NALLTRK };
#define NSEQT (AUX_PUNCH + 1)         /* sequenced tracks: 1-8, brain, punch */
#define NPATT 8                       /* patterns per track */
#define NSTEP 64                      /* 4 bars of 16 */
#define NBAR 4
#define SN 4                          /* notes a step may hold */
#define NSCENE 16
#define NSONG 64                      /* scene slots in the song */
#define NLANE 96                      /* parameter lock lanes (the whole project) */
#define NDRUM 11                      /* drumline keys: the black keys (a model instance each) */
#define PPS 24                        /* ticks a step (a sixteenth at scale 1): nudge and lengths count in these */

/* step components: the OP-XY's 14, in the cheat sheet's order (the white keys 1..14 with HOME and a step held);
 * 0 = none. Values 0..8 = the black keys 1-9, 9 = black key 10 (random, where the OP-XY prints rnd). A step
 * carries up to two (comp / comp2). Defaults when a component is added (the notebook p. 139): SC_DEFAULT[]. */
enum { SC_NONE, SC_PULSE, SC_HOLD, SC_MULTIPLY, SC_VELOCITY, SC_RAMP_UP, SC_RAMP_DOWN, SC_RANDOM,
       SC_PORTAMENTO, SC_BEND, SC_TONALITY, SC_JUMP, SC_SKIP_LOCK, SC_SKIP_COMP, SC_SKIP, NSC };
#define SC_RANDOM_VAL 9
extern const uint8_t SC_DEFAULT[NSC];      /* pulse 3 (= 4), hold 3, multiply 1, velocity 4, ramps 3, random 3,
                                            * portamento 3, bend 0, tonality 3, jump 3, skips 1 (0-based values) */
/* SC_PULSE / SC_HOLD: 1..9 repeats (the total count includes the original trigger), random
 * SC_MULTIPLY: 1..9 ratchets within the step, random
 * SC_VELOCITY: 4 8 16 32 64 100 112 127 0, random
 * SC_RAMP_* / SC_RANDOM: 2..6 pitch iterations across one octave (0..4), across three octaves (5..8), random:
 *   the notes are spread evenly over the span in the brain's scale and the ramp resets after each cycle
 * SC_PORTAMENTO: 10..80 % (0..7), off (8), random
 * SC_BEND: down-up, up-down, bump-down, bump-up, spring-out, spring-in, fade-down, fade-up, random 1, random 2
 * SC_TONALITY: ignore progression, transpose only, octave up, fifth up, third up (by the scale), chromatic up,
 *   chromatic down, quantize 33 %, 66 %, 100 %
 * SC_JUMP: to 1, 5, 9, 13, forward (skips the next step), back, forward-or-back, stay, align, random
 * SC_SKIP_LOCK / SC_SKIP_COMP / SC_SKIP: a cycle of 1..9 passes (random); which passes play is the cycle's
 *   mode in the value's upper bits: SK_MODE(v) 0 = play on the last pass of the cycle, 1 = skip the last pass,
 *   2 = play only on the first; SK_N(v) = the cycle length value 0..9 */
#define SK_N(v) ((v) & 15u)
#define SK_MODE(v) (((v) >> 4) & 3u)
#define SK_VAL(n, mode) ((uint8_t)(((mode) << 4) | (n)))

/* a step's condition: 0 always; 1..99 that chance in %; COND_AB + k = the A:B cycle k (X0X's order
 * 1:2 2:2 1:3 2:3 3:3 1:4 ... 8:8: on the A-th of every B passes of the pattern) */
#define COND_AB 100
#define COND_NAB 35

typedef struct {
    uint8_t note[SN];                 /* MIDI notes, 0 = none (a drum track: the sound 1..11 in note[0]) */
    uint8_t vel;                      /* 1..127 */
    uint8_t len;                      /* quarter steps, 0 = the pattern's note length */
    int8_t nudge;                     /* ticks, -12..12 */
    uint8_t comp;                     /* SC_* */
    uint8_t cval;                     /* the component's value 0..9 (skips: SK_VAL) */
    uint8_t cond;                     /* see COND_* */
    uint8_t comp2, cval2;             /* a second component on the same step (SC_NONE = none) */
} step_t;                             /* 12 bytes */

enum { TS_HALF, TS_1, TS_2, TS_3, TS_4, TS_5, TS_6, TS_8, TS_16, NTS };   /* track scale: sixteenths a step */
/* groove types (the project's): swing, lilt, weave, stretch, drunk, jitter, accent, lazy, push pull, sway, swell
 * (the index is saved with the project: append, never reorder); a track's amount -99..99 scales its timing and
 * velocity tables (negative = early / narrow), the random ones roll per hit */
enum { GR_SWING, GR_LILT, GR_WEAVE, GR_STRETCH, GR_DRUNK, GR_JITTER, GR_ACCENT, GR_LAZY, GR_PUSHPULL,
       GR_SWAY, GR_SWELL, NGROOVE };
extern const char *const GR_NAME[NGROOVE];   /* "swing", "lilt", .. */

typedef struct {
    step_t st[NSTEP];
    uint8_t len;                      /* steps 1..64 */
    uint8_t scale;                    /* TS_* */
    int8_t groove;                    /* this track's groove amount -99..99 (the type is the project's) */
    uint8_t quant;                    /* live record quantise 0..100 */
    uint8_t nlen;                     /* note length 0..100 % of a step (50 = half a step, the default) */
    uint8_t shape;                    /* lock smoothing 0..100 */
    uint8_t rsv[2];
} tpat_t;                             /* 776 bytes */

/* a parameter lock lane: a value for each step of one track pattern, LOCK_NONE = the knob's own */
#define LOCK_NONE 0xFFu
typedef struct {
    uint8_t used, gen;                /* gen: bumped when the slot is reused (the interrupt resets its runtime) */
    uint8_t trk, pat, param;          /* the track pattern and the parameter (synth.h TP_*) */
    uint8_t rsv;
    uint8_t val[NSTEP];
} lane_t;                             /* 70 bytes */

typedef struct {
    uint8_t pat[NSEQT];               /* each sequenced track's pattern */
    uint8_t used;                     /* 1 = the scene exists */
    uint16_t mute;                    /* bit t = track t muted (NSEQT bits) */
    uint8_t brain_key, brain_scale, brain_deg;   /* the brain's state in this scene */
    uint8_t rsv;
} scene_t;                            /* 18 bytes */

typedef struct {
    uint8_t scene;                    /* 0..15 */
    uint8_t rep;                      /* 1..16 passes */
} song_slot_t;

typedef struct {
    uint8_t len;                      /* slots used, 0 = empty */
    uint8_t loop;                     /* 1 = loop, 0 = stop at the end */
    uint8_t rsv[2];
    song_slot_t slot[NSONG];
} song_t;

/* the whole pattern store: the project's (in the pool) */
typedef struct {
    tpat_t pat[NSEQT][NPATT];
    lane_t lane[NLANE];
    scene_t scene[NSCENE];
    song_t song;
} patterns_t;

void pattern_init(tpat_t *p);         /* empty, 16 steps, scale 1, no groove, note length half a step */
void patterns_init(patterns_t *P);    /* everything empty, scene 1 used with pattern 1 on every track */
/* lanes */
int lane_find(const lane_t *L, int trk, int pat, int param);     /* lane index, -1 */
int lane_alloc(lane_t *L, int trk, int pat, int param);          /* -1 = full */
void lane_clear(lane_t *L, int k);
int lane_count(const lane_t *L);
int step_has_lock(const lane_t *L, int trk, int pat, int step);
void step_clear_locks(lane_t *L, int trk, int pat, int step);
void pattern_clear_locks(lane_t *L, int trk, int pat);
void cond_ab(int k, int *a, int *b);  /* COND_AB + k -> A:B */
/* weave two patterns into out (not a or b): WV_BARS alternates their bars (a, b, a, b), WV_STEPS their steps; the
 * shorter one loops to the longer one's length. WV_RHYTHM keeps a's rhythm and plays b's notes on it, in their
 * order, cycled; WV_NOTES the other way: b's rhythm, a's notes. The pattern's settings are always a's. */
enum { WV_BARS, WV_STEPS, WV_RHYTHM, WV_NOTES, NWV };
void pattern_weave(tpat_t *out, const tpat_t *a, const tpat_t *b, int mode);
