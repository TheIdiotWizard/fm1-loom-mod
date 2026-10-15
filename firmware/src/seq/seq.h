/* SPDX-License-Identifier: GPL-3.0-only */
/* XY-1 sequencer: runs in the audio interrupt, sample accurate (X0X's shape):
 *
 *     while (n) {
 *         k = seq_until_event(&seq, n);        // samples until the next due event (>= 1)
 *         render k samples;
 *         seq_advance(&seq, k, &sink);         // moves time, fires what is due
 *         n -= k;
 *     }
 *
 * Eleven sequenced tracks (1-8, brain, punch, tape), each on its own pattern (the scene's), with its
 * own step counter, length and scale. Track 1's pattern defines the bar for scene changes and the
 * song: when it wraps (or every 16 steps if it is shorter), cued scenes start. Time is counted in
 * ticks (PPS a sixteenth) at the tempo, swung and grooved per track.
 *
 * Step components run here: pulse and hold stall the counter, multiply ratchets, velocity fixes,
 * ramps and random move the note along the brain's scale pass by pass, portamento glides, bend writes
 * the track's perf pitch over the step, jump moves the counter, skip (and the condition) decide
 * whether the step fires. Parameter locks: before a step fires, each lane of the pattern sets its
 * value (ramped toward the next lock when the pattern's shape says so) through the sink.
 *
 * Players: the keys reach the engine through seq_key(): arp (clocked here), maestro (the recorded
 * chord, transposed, rolled), hold; the brain shifts routed tracks' notes diatonically (its degree
 * from its keys or its steps). Punch-in FX are a bit mask per group, from held keys or the punch
 * track's steps; the sequencer derives the perf state and fires the fill patterns.
 *
 * Clock: internal (BPM) or MIDI clock in (24 ppqn, seq_ext_clock); clock and transport out through
 * the sink. Everything the main loop changes is a flag or a byte, picked up at the next block. */
#pragma once
#include <stdint.h>
#include "pattern.h"
#include "../dsp/synth.h"
#define NTP_ALL NTP                   /* aux tracks use the same parameter array as the instrument tracks */

#define SEQ_FS 44100u
#define NPUNCH_FX 32                  /* bit numbers: 0..11 drum FX, 16..30 melodic FX */
enum { PD_MUTE, PD_STUTTER, PD_FILL, PD_PAN, PD_HALF_FILL, PD_OCT_UP, PD_ECHO, PD_KS_FILL, PD_SHORT, PD_HATS,
       PD_TAILS, PD_RANDOM };
enum { PM_MUTE = 16, PM_STUTTER, PM_LOOP4, PM_PAN, PM_OCT_UP, PM_OCT_DOWN, PM_DELAY, PM_REVERB, PM_FILT_DOWN,
       PM_FILT_UP, PM_RAMP, PM_RANDOM, PM_HALF, PM_DOUBLE, PM_REVERSE };
extern const char *const PUNCH_NAME[NPUNCH_FX];   /* "" where unused */

/* brain scales (the key's), 12-bit masks in BRAIN_SCALE[] */
enum { BS_MAJOR, BS_MINOR, BS_DORIAN, BS_MIXO, BS_LYDIAN, BS_PHRYGIAN, BS_HARM_MINOR, BS_PENTA, BS_PENTA_MIN,
       BS_CHROMATIC, NBS };
extern const char *const BS_NAME[NBS];
extern const uint16_t BRAIN_SCALE[NBS];
int brain_shift(int note, int key, int scale, int degree, int mode);   /* the note moved `degree` scale steps
                                                                        * (mode: TP_BRAIN 1 diatonic, 2 transpose) */
int brain_detect(const uint8_t *hist12, int *key, int *scale);         /* best key + scale for a pitch-class
                                                                        * histogram; 1 = confident */
int scale_step(int note, int key, int scale, int steps);               /* a note `steps` degrees along the scale */

typedef struct {
    void (*note_on)(void *ctx, int trk, int note, int vel, int glide_ms, uint32_t tag);
    void (*note_off)(void *ctx, int trk, uint32_t tag);
    void (*drum)(void *ctx, int trk, int sound, int vel, int pitch_semi, int short_gate, int long_tail);
    void (*lock)(void *ctx, int trk, int param, int val);          /* LOCK_NONE = back to the knob */
    void (*bend)(void *ctx, int trk, int cents);                   /* the bend component, each chunk */
    void (*punch)(void *ctx, uint32_t drum_mask, uint32_t mel_mask);   /* the live FX state changed */
    void (*tape_seg)(void *ctx, int seg, int on);
    void (*tape_step)(void *ctx, uint32_t samples_per_step);
    void (*midi)(void *ctx, uint8_t status, uint8_t d1, uint8_t d2);
    void (*step)(void *ctx, int trk, int step);                    /* UI playheads */
    void (*scene)(void *ctx, int scene);                           /* a scene started */
    void (*metro)(void *ctx, int accent);                          /* a metronome click (accent: the bar) */
    void *ctx;
} seq_sink_t;

#define SEQ_PENDING 16                /* note-offs a track may have waiting */
typedef struct {
    uint32_t off_at;                  /* samples left, Q8 (counted down; due under 256) */
    uint32_t tag;
    uint8_t used;
    uint8_t kind;                     /* OFF_NOTE: sink note_off; OFF_PUNCH: clear a punch_seq bit; OFF_TAPE: a segment off */
    uint8_t note;                     /* the MIDI note echoed, 0 none */
} seq_off_t;
enum { OFF_NOTE, OFF_PUNCH, OFF_TAPE };

typedef struct {                      /* one track's runtime (interrupt only) */
    uint8_t pos;                      /* the step playing */
    uint8_t started;
    uint8_t stall;                    /* pulse / hold repeats left on this step */
    uint8_t ratchet, ratchets;        /* multiply: hits done, hits wanted */
    uint16_t pass;                    /* passes of the pattern since start (conditions, skips, ramps) */
    uint32_t to_next;                 /* samples to the next step, Q8 */
    uint32_t step_q8;                 /* the current step's length (ratchets, bends) */
    uint32_t rng;
    int16_t ramp_deg;                 /* ramp / random: the degree offset this pass */
    uint8_t bend_shape, bend_on;
    uint32_t bend_t, bend_len;        /* samples into the bend, its length */
    seq_off_t off[SEQ_PENDING];
    uint8_t last_note[SN];            /* the notes sounding (step mode: one step at a time) */
    uint8_t held_drum;                /* fill FX: the last sound hit (1..11, 0 none) */
    uint8_t loop4_from;               /* PM_LOOP4: the step it loops from */
    uint8_t loop4_on;
    uint8_t stall_kind;               /* SC_PULSE or SC_HOLD while stall > 0 */
    uint8_t jump_to;                  /* the jump component's next step, 0xFF none */
    uint8_t nlast;                    /* notes in last_note[] */
    uint8_t last_step;                /* the step last_note[] came from */
    uint8_t last_vel, last_glide;     /* for retriggers (ratchets, stutter) */
    uint8_t ramp_semi;                /* PM_RAMP: semitones added, one more each sixteenth */
    uint8_t ext_ticks;                /* external clock: ticks into this step */
    uint8_t stut_on;                  /* a stutter is running */
    uint8_t echo_n, echo_vel;         /* PD_ECHO: the hit to repeat */
    uint8_t echo_sound[SN];
    uint8_t bend_last_set;
    int16_t bend_last;                /* cents last sent */
    int16_t bend_amp;                 /* random 2: the amplitude rolled */
    uint32_t sub_to_next;             /* ratchets: samples to the next hit, Q8 (NEVER none) */
    uint32_t stut_to_next;            /* stutter: the next retrigger, Q8 */
    uint32_t echo_to_next;            /* echo: the repeat, Q8 */
    uint32_t age;                     /* samples since the step fired, Q8 (live record quantise) */
    int32_t off_q8;                   /* the current step's timing offset (swing, groove, nudge), Q8 */
} seq_trk_t;

typedef struct {
    uint8_t gen, applied;             /* lane runtime: the value last applied (LOCK_NONE = none) */
    uint8_t from, to;                 /* the ramp across this step (shape) */
    uint8_t ramp;                     /* ramping toward `to` */
    uint32_t age, len, hold;          /* samples into the gap, its length, the part held flat before the ramp */
} lane_rt_t;

typedef struct {                      /* a player (per track; the arp's clock runs here) */
    uint8_t held[16], nheld;          /* the keys down, in order */
    uint8_t latched;                  /* hold: notes stay */
    uint8_t arp_i, arp_oct, arp_dir;
    uint8_t arp_note;                 /* sounding arp note, 0 none */
    uint32_t arp_to_next, arp_to_off; /* Q8 */
    uint8_t chord[SN * 2], nchord;    /* maestro: the recorded chord (up to 8 notes) */
    uint8_t maestro_root;             /* the key playing it, 0 none */
    uint8_t roll_i; uint32_t roll_to_next;
    uint8_t sounding[16], nsounding;  /* notes the player holds on the engine (for their offs) */
    uint32_t phys[4];                 /* the keys physically down (bit note) */
    uint8_t nphys;
    uint8_t roll[SN * 2], nroll;      /* maestro: the strum in its order */
    uint8_t roll_vel;
    uint8_t arp_wait;                 /* the first arp note waits for the grid */
} player_t;

typedef struct {
    patterns_t *P;                    /* the project's patterns, lanes, scenes, song */
    uint8_t (*par)[NTP_ALL];          /* each track's parameters (NALLTRK of them): TP_PLAYER.., TP_BRAIN, mutes */
    volatile uint8_t playing;
    volatile uint8_t scene;           /* the scene playing */
    volatile uint8_t cue;             /* the scene cued at the bar, 0xFF none */
    volatile uint8_t song_on;         /* the song decides the scenes */
    volatile uint8_t song_pos, song_rep;
    volatile uint8_t ppat[NSEQT];     /* each track's pattern (the scene's, or cued per track) */
    volatile uint16_t mute;           /* bit t */
    volatile uint8_t ext, send_clock, send_notes;
    volatile uint8_t metronome;       /* 0 off, 1 while recording, 2 on */
    volatile uint8_t rec;             /* live recording: the keys write steps */
    volatile uint8_t count_in;        /* bars of count-in left before rec */
    float bpm, ext_bpm;
    /* the brain */
    volatile uint8_t brain_mode, brain_key, brain_scale, brain_deg, brain_link, brain_auto_ok;
    volatile uint16_t brain_route;    /* bit t = track t follows */
    uint8_t brain_hist[12];
    /* punch-in */
    volatile uint32_t punch_keys;     /* bits NPUNCH_FX: held keys (UI writes) */
    uint32_t punch_seq;               /* from the punch track's steps */
    uint32_t punch_last;
    volatile uint8_t punch_target;    /* 0xFF = the group, else one track (hold HOME + key on a track) */
    uint8_t punch_phase;              /* stutter / fill counters */
    uint32_t punch_to_next;
    /* runtime */
    seq_trk_t t[NSEQT];
    lane_rt_t lrt[NLANE];
    player_t player[NTRK];
    uint32_t clk_to_next, bar_to_next;   /* Q8 */
    uint32_t ext_since, ext_period;
    volatile uint32_t ext_pending;
    volatile uint8_t req_start, req_stop, req_cont, req_from_start;
    uint32_t rng;
    uint32_t step_tick;               /* sixteenths since start (the metronome, the tape, the arp grid) */
    uint8_t metro_beat;
    /* added runtime */
    volatile uint8_t pcue[NSEQT];     /* a pattern cued per track, 0xFF none */
    volatile uint8_t req_scene;       /* seq_set_scene: apply now, 0xFF none */
    volatile uint32_t kq[32];         /* the keys from the main loop, played in the interrupt */
    volatile uint8_t kq_w, kq_r;
    uint32_t six_to_next;             /* samples to the next sixteenth, Q8 */
    uint32_t bar_six;                 /* sixteenths since the bar count started */
    uint32_t bar_len;                 /* the bar in sixteenths (track 1's span, at least 16) */
    uint8_t bar16;                    /* the sixteenth of the bar, 0..15 */
    uint8_t countin_six;
    uint8_t bar_fresh;                /* the bar count just restarted: the next sixteenth is not a bar's end */
    uint8_t ext_six;                  /* external clock: ticks into the sixteenth */
    uint8_t chunk_cap;                /* a bend or a lock ramp runs: advance in chunks */
    uint8_t brain_seen[4];            /* the BRAIN track's E1..E4 last read */
    uint8_t ramp_ticks;               /* sixteenths passed this block: the ramp FX rises after the steps */
    uint32_t tape_last;               /* samples per sixteenth last sent to the tape */
    uint32_t punch_sent_d, punch_sent_m;
    uint8_t rec_note[8], rec_trk[8], rec_step[8];   /* live record: keys down and the steps they took */
    uint32_t rec_age[8];
} seq_t;

void seq_init(seq_t *s, patterns_t *P, uint8_t (*par)[NTP_ALL]);
uint32_t seq_until_event(seq_t *s, uint32_t n);
void seq_advance(seq_t *s, uint32_t k, const seq_sink_t *o);

/* main loop / MIDI side (flag writes, picked up at the next block) */
void seq_start(seq_t *s, int from_start);
void seq_stop(seq_t *s);
void seq_continue(seq_t *s);
void seq_ext_clock(seq_t *s);                  /* one 24-ppqn tick */
void seq_cue_scene(seq_t *s, int scene);       /* at the bar (now, if stopped) */
void seq_set_scene(seq_t *s, int scene);       /* now: the scene's patterns, mutes, brain */
void seq_cue_pattern(seq_t *s, int trk, int pat);   /* one track's pattern at its wrap */
float seq_tempo(const seq_t *s);
uint32_t seq_step_q8(const seq_t *s, int trk);     /* a step of track trk in samples Q8 (its scale, no swing) */
int seq_bar_step(const seq_t *s);                   /* 0..15: the sixteenth of the bar (UI, metronome) */

/* the keys (main loop): through the track's player and the brain, to the engine; tag = the key */
void seq_key(seq_t *s, int trk, int note, int on, int vel);
void seq_key_all_off(seq_t *s, int trk);
void seq_maestro_record(seq_t *s, int trk, const uint8_t *notes, int n);   /* the maestro's chord */
void seq_brain_key(seq_t *s, int note, int on);     /* a key on the BRAIN track: degree from the key */
void seq_punch_key(seq_t *s, int fx, int on, int target_trk);   /* fx 0..30; target -1 = the group */
void seq_tape_key(seq_t *s, int seg, int on);

/* live / step recording (main loop) */
void seq_rec(seq_t *s, int on);                     /* live: keys write steps (quantised) while playing */
void seq_rec_knob(seq_t *s, int trk, int param, int val);   /* REC held + a knob: a lock on the step playing */
int seq_rec_pos(const seq_t *s, int trk);           /* the step live keys land on now */
int seq_step_rec(seq_t *s, int trk, int step, int note, int vel);   /* step record: the note into the step of the
                                                     * track's pattern (overdubs; the UI moves its cursor); 1 = written */
/* step helpers (main loop) */
void step_set_notes(step_t *st, const uint8_t *notes, int n, int vel);
void step_toggle_note(step_t *st, int note);
int step_empty(const step_t *st);
