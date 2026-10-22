/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8's modulation matrix: per instrument track four slots, each a source sent to a destination by an amount.
 * The idea, the sources and the way the destinations scale come from Melodee's mod.c (Kerem Kilic, Ellic Studio,
 * GPL-3.0); the code is LOOM-8's own, on its voices (dsp/synth.c, "the matrix").
 *
 * Sources: unipolar (0..1) ENV the voice's amp envelope, VEL the note's velocity, MODW the mod wheel (MIDI CC 1),
 * AT channel aftertouch, EXPR expression (MIDI CC 11; 1 until a controller arrives); bipolar (-1..1) LFO the
 * track's LFO (its shape, before its amount; only while the LFO's type is wave, drift or wobble), KEY the note
 * about middle C (+-64 semitones = +-1), RAND a new random value per note, S/H a random value held for one cycle of
 * the track LFO (whatever its type: the LFO's speed still runs), SLEW the same glided to the next one.
 * Destinations: per voice PITCH (full source at amount 63: +-12 semitones), CUT and RES (knob units: 63 = +-63 of
 * 127), AMP (a gain <= 1: a positive amount opens it with the source, a negative closes it; slots multiply), PAN
 * (knob units), the engine's four knobs E1..E4 (the knob's whole range at amount 64); per track RATE (the LFO's
 * rate: 63 = +-3 octaves), VIB (the depth of a vibrato, a sine at the LFO's rate, or 5.5 Hz while the LFO is off
 * or a pump: 63 = +-1 semitone), DEPTH (a gain <= 1 on the track LFO, as AMP is on the voice: with RATE the matrix
 * modulates the modulator, MODW to DEPTH is a mod-wheel LFO).
 * Idle (no slot with a source, a destination and an amount, or every source at its zero) the sound is bit-identical
 * to a build without the matrix.
 * The stored values are LOOM-8's own: append to the lists, never reorder (they are saved by number). */
#pragma once
#include <stdint.h>

#define MX_NSLOT 4
enum { MXS_OFF, MXS_LFO, MXS_ENV, MXS_VEL, MXS_KEY, MXS_RAND, MXS_MODW, MXS_AT, MXS_EXPR, MXS_SH, MXS_SLEW, MXS_N };
enum { MXD_OFF, MXD_PITCH, MXD_CUT, MXD_RES, MXD_AMP, MXD_PAN, MXD_RATE, MXD_VIB, MXD_DEPTH,
       MXD_E1, MXD_E2, MXD_E3, MXD_E4, MXD_N };
#define MX_AMT_LO (-64)
#define MX_AMT_HI 63
/* a new track: the mod wheel opens a vibrato about +-0.5 semitone (half the way) */
#define MX_DEF_SRC MXS_MODW
#define MX_DEF_DST MXD_VIB
#define MX_DEF_AMT 32

typedef struct {
    uint8_t src, dst;                 /* MXS_*, MXD_* */
    int8_t amt;                       /* MX_AMT_LO..MX_AMT_HI */
} mxslot_t;
typedef struct {
    mxslot_t s[MX_NSLOT];
} mx_t;                               /* 12 bytes a track */

/* the live controllers of a track (written by the audio interrupt from MIDI in, read by the voices): the mod wheel
 * and channel aftertouch 0..127 (0 until a controller arrives, and after CC 121), expression 0..127 (127 likewise) */
enum { MXL_MODW, MXL_AT, MXL_EXPR, MXL_N };

static inline void mx_clear(mx_t *m)
{
    for (int i = 0; i < MX_NSLOT; i++)
        m->s[i] = (mxslot_t){0, 0, 0};
}
static inline void mx_default(mx_t *m)
{
    mx_clear(m);
    m->s[0] = (mxslot_t){MX_DEF_SRC, MX_DEF_DST, MX_DEF_AMT};
}
/* a stored matrix made safe to index with (a stray byte, a build whose lists moved) */
static inline void mx_clamp(mx_t *m)
{
    for (int i = 0; i < MX_NSLOT; i++) {
        mxslot_t *s = &m->s[i];
        if (s->src >= MXS_N || s->dst >= MXD_N)
            s->src = s->dst = 0, s->amt = 0;
        if (s->amt < MX_AMT_LO)
            s->amt = MX_AMT_LO;
        if (s->amt > MX_AMT_HI)
            s->amt = MX_AMT_HI;
    }
}
extern const char *const MXS_NAME[MXS_N];     /* "OFF", "LFO", ... (dsp/synth.c) */
extern const char *const MXS_LONG[MXS_N];     /* "off", "LFO", "envelope", ... */
extern const char *const MXD_NAME[MXD_N];     /* "OFF", "PITCH", ..., "E1".."E4" (the engine's knob names: MXD_KNOB) */
