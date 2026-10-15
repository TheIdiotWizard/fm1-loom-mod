/* SPDX-License-Identifier: GPL-3.0-only */
/* XY-1 data model: tracks, patterns, steps, lanes (parameter locks), scenes and the song.
 *
 * Plain data, no pointers, so a pattern copies with an assignment and the project stores the bytes
 * (X0X's rule). The UI edits these in place from the main loop while the sequencer reads them in the
 * audio interrupt: every field the sequencer reads is a single byte or an aligned 32-bit word, so a
 * read never sees half a write. */
#pragma once
#include <stdint.h>

#define NTRK 8                        /* instrument tracks 1-8 */
enum { AUX_BRAIN = NTRK, AUX_PUNCH, AUX_TAPE, AUX_FX1, AUX_FX2, NALLTRK };
#define NSEQT (AUX_TAPE + 1)          /* sequenced tracks: 1-8, brain, punch, tape */
#define NPATT 8                       /* patterns per track */
#define NSTEP 64                      /* 4 bars of 16 */
#define NBAR 4
#define SN 4                          /* notes a step may hold */
#define NSCENE 16
#define NSONG 64                      /* scene slots in the song */
#define NLANE 96                      /* parameter lock lanes (the whole project) */
#define NDRUM 11                      /* drumline sounds: the black keys */
#define NPUNCH_D 12                   /* drum punch-in FX: the low 12 keys F3..E4 */
#define NPUNCH_M 15                   /* melodic punch-in FX: the upper 15 keys F4..G5 */
#define PPS 24                        /* ticks a step (a sixteenth at scale 1): nudge and lengths count in these */

/* step components, on the black keys 1..11 (0 = none); values 0..8 = the OP-XY's 1-9, 9 = random */
enum { SC_NONE, SC_PULSE, SC_HOLD, SC_MULTIPLY, SC_VELOCITY, SC_RAMP_UP, SC_RAMP_DOWN, SC_RANDOM,
       SC_PORTAMENTO, SC_BEND, SC_JUMP, SC_SKIP, NSC };
#define SC_RANDOM_VAL 9
/* SC_VELOCITY values: 4 8 16 32 64 100 112 127 0, random
 * SC_RAMP_*: 2..6 degrees an octave (0..4), 2..6 degrees over three octaves (5..8), random
 * SC_PORTAMENTO: 10..90 % (0..8), random
 * SC_BEND: down-up, up-down, bump-down, bump-up, spring-out, spring-in, fade-down, fade-up, random 1, random 2
 * SC_JUMP: to 1, 5, 9, 13, forward, back, forward-or-back, stay, align, random
 * SC_SKIP: play every 1st..9th pass, random */

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
    uint8_t cval;                     /* the component's value 0..9 */
    uint8_t cond;                     /* see COND_* */
} step_t;                             /* 10 bytes */

enum { TS_HALF, TS_1, TS_2, TS_3, TS_4, TS_6, TS_8, TS_16, NTS };   /* track scale: sixteenths a step */
enum { GR_SWING, GR_HALF, GR_BOMBORA, GR_WOBBLY, GR_GAUSS, GR_ACCENTS, GR_ISLAND, NGROOVE };

typedef struct {
    step_t st[NSTEP];
    uint8_t len;                      /* steps 1..64 */
    uint8_t scale;                    /* TS_* */
    uint8_t groove;                   /* GR_* */
    uint8_t swing;                    /* 0..100 (50 = straight) */
    uint8_t quant;                    /* live record quantise 0..100 */
    uint8_t nlen;                     /* note length, quarter steps 1..16 */
    uint8_t shape;                    /* lock smoothing 0..100 */
    uint8_t rsv;
} tpat_t;                             /* 648 bytes */

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

void pattern_init(tpat_t *p);         /* empty, 16 steps, scale 1, straight, note length a step */
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
