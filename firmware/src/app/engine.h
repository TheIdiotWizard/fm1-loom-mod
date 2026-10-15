/* SPDX-License-Identifier: GPL-3.0-only */
/* XY-1 audio engine: the sequencer, the voices (synth + drumline per track), the sends, the two FX
 * tracks, the tape, the master; the two cores and the overload guard. engine_render() runs in the
 * audio interrupt; everything else is called from the main loop and reaches the interrupt through
 * the command queue (one 32-bit word a command, single producer / consumer), so the engines' state
 * is only ever touched by one context.
 *
 * Parameters: track t's par[NTP] (dsp/synth.h) lives in the project (proj.trk[t].par) and is
 * mirrored into the engine with engine_set(); the interrupt reads the engine's copy. */
#pragma once
#include <stdint.h>
#include "../seq/seq.h"
#include "../dsp/synth.h"
#include "../dsp/drumline.h"
#include "../dsp/fx.h"

extern seq_t seq;                      /* the sequencer (its pattern pointer is the project's) */
enum { PR_SEQ, PR_SYNTH0, PR_SYNTH1, PR_DRUM, PR_FX, PR_MST, PR_WAIT, ENG_PROF_N };   /* the PERF screen's stages */
extern volatile uint32_t eng_prof[ENG_PROF_N], eng_prof_frames;

void engine_init(patterns_t *P, uint8_t (*par)[NTP]);    /* par: NALLTRK arrays (the project's) */
void engine_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);   /* ISR: n stereo frames, 24-bit in int32 */
void engine_load(uint32_t pct);                          /* ISR, after a block: the overload guard's input */
void engine_set_core2(int (*run)(void (*)(uint32_t), uint32_t), int (*wait)(void));   /* 0, 0 = one core */

/* main loop -> interrupt (queued) */
void engine_set(int trk, int param, int val);            /* a knob: the engine's par copy + the voices */
void engine_set_all(int trk);                            /* every parameter of a track (load, preset) */
void engine_key(int trk, int note, int on, int vel);     /* a key through the sequencer's player and brain */
void engine_keys_off(int trk);
void engine_punch(int fx, int on, int target_trk);
void engine_tape_key(int seg, int on);
void engine_brain_key(int note, int on);
void engine_master(int p, int val);
void engine_fx(int slot, int type, const uint8_t knob[4], int hp, int lp);
void engine_tape_set(int pitch, int speed, int length, int mix);
void engine_panic(void);
void engine_midi_in(uint32_t pkt);                       /* a USB-MIDI packet (the main loop passes them) */
void engine_transport(int what);                         /* 0 stop, 1 start, 2 continue, 3 start from the top */
void engine_metronome(int mode);

/* state for the screen (written by the interrupt) */
extern volatile uint8_t eng_step[NSEQT];                 /* playheads */
extern volatile uint16_t eng_peak[NALLTRK];              /* meters, Q15, decaying */
extern volatile uint8_t eng_drum_hit[NDRUM];             /* the selected drum track's last hits (255 = full) */
extern volatile uint8_t eng_voices, eng_voices_max;      /* sounding, and the guard's cap */
extern volatile uint32_t eng_guard_count;
extern volatile uint32_t eng_punch_drum, eng_punch_mel;  /* the FX in effect */
extern volatile uint8_t eng_scene_now, eng_scene_cue;
#define SCOPE_N 256u
extern int16_t eng_scope[SCOPE_N];                       /* the last output samples, for the screen */
extern volatile uint32_t eng_scope_w;
int engine_core2_state(void);                            /* 0 one core (never had), 1 two, 2 lost, 3 barred */
