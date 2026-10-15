/* SPDX-License-Identifier: GPL-3.0-only */
/* XY-1 drumline: a drum synth with eleven sounds on the black keys, each a model with tune, tone,
 * decay, pan and level (synth.h TP_DRUM(sound, DP_*)). Models (DP_MODEL): kick, snare, closed hat,
 * open hat, clap, low tom, high tom, conga, shaker, fm, resonator. Up to NDVOICE sounds at once; the
 * closed hat chokes the open hat. Analytic models (sine with pitch envelopes, filtered noise bursts,
 * six-square metal, Karplus-like resonator): cheap, no samples. Float, fastmath.h. */
#pragma once
#include <stdint.h>

#define DL_NSOUND 11
#define NDVOICE 8
enum { DM_KICK, DM_SNARE, DM_CHAT, DM_OHAT, DM_CLAP, DM_LTOM, DM_HTOM, DM_CONGA, DM_SHAKER, DM_FM, DM_RESO, NDM };
extern const char *const DM_NAME[NDM];
extern const char *const DL_SOUND_NAME[DL_NSOUND];     /* the default kit: "kick", "snare", "hat", "open", ... */
extern const uint8_t DL_DEFAULT_KIT[DL_NSOUND][6];     /* DP_* of each sound of a new drum track */

typedef struct drumline drumline_t;
#define DRUMLINE_BYTES 2048

void drumline_init(drumline_t *d, const uint8_t *par);  /* par: the track's par[] (reads TP_DRUM0..) */
void drumline_hit(drumline_t *d, int sound, int vel, int pitch_semi, int short_gate, int long_tail);
void drumline_choke(drumline_t *d);
/* render n frames: dry L/R and sends (the track's TP_SEND*), added into the buffers */
void drumline_render(drumline_t *d, float *l, float *r, float *s1, float *s2, float *st, uint32_t n);
int drumline_sounding(const drumline_t *d);
extern volatile uint8_t drumline_hitv[DL_NSOUND];      /* the last hit of each sound, 255 = full (UI lets it fall) */
