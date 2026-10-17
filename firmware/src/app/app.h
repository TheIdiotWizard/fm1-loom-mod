/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 app: the project (everything that is saved) and the app's entry points. Included by the unity
 * build after gfx.c (drawing helpers in scope) and by the host simulator. */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "../seq/pattern.h"
#include "../seq/seq.h"
#include "../dsp/synth.h"
#include "../dsp/fx.h"

#ifndef XY_VERSION                      /* a release build passes its own (tools/build.py --release) */
#define XY_VERSION "DEV"
#endif
#define PROJ_MAGIC 0x384D4F4Cu          /* "LOM8" (was "XXY1": XY-1's projects start fresh) */
#define PROJ_FORMAT 3u                  /* 3: no tape (its track, its send: every index after them moved), no
                                         * distortion, the reverb's modes, the master's tape; 2: the character stage */
#define NPROJ 4                         /* project slots in flash */
#define NUSER_PRESETS 16
#define NAME_LEN 12

typedef struct {
    uint8_t par[NTP];                   /* dsp/synth.h: the sound, player, mix, brain, channel, the drum kit */
    char name[NAME_LEN];                /* the preset's name, as loaded or saved */
    uint8_t rsv[4];
} trk_t;

/* the settings object: small, stored raw. The patterns (seq/pattern.h patterns_t, in the pool) are
 * an LZ stream across the slot's other objects */
typedef struct {
    uint32_t magic, format;
    char name[NAME_LEN];
    uint16_t tempo_x10;                 /* 400..3000 */
    uint8_t swing;                      /* unused since the grooves (a reserved byte; the UI may still write it) */
    uint8_t groove;                     /* the project's groove type (pattern.h GR_*); each track has its amount */
    uint8_t metronome;                  /* 0 off, 1 while recording, 2 on */
    uint8_t lights;                     /* 0 off, 1 keys and glow, 2 keys only */
    uint8_t send_clock, send_notes, one_core, usb_full;
    uint8_t scene;                      /* the scene to start on */
    uint8_t trk_sel;                    /* the track selected */
    uint8_t rsv[14];
    uint8_t master[NMS];
    trk_t trk[NALLTRK];
} settings_t;

typedef struct {
    char name[NAME_LEN];
    uint8_t engine;
    uint8_t par[NTP_SOUND];
} user_preset_t;

extern settings_t proj;                 /* the project's settings (RAM) */
extern uint8_t proj_par[NALLTRK][NTP];  /* the tracks' par[] as one contiguous array: what the synth and the
                                         * sequencer read (engine_init gets it); engine_set / engine_set_all
                                         * keep it in step with proj.trk[].par (the UI writes those) */
extern patterns_t pats;                 /* its patterns, lanes, scenes, song (pool) */
extern user_preset_t user_presets[NUSER_PRESETS];
extern uint8_t proj_slot;               /* the slot loaded */

void project_defaults(void);
int project_mem_pct(void);              /* how full the patterns' room in flash is, % (the last save or load) */
int project_load(int slot);             /* 0 = loaded; else defaults are in place */
int project_save(int slot);             /* 0 = saved and verified; -1 = full / failed */
void project_apply(void);               /* every value to the engine (boot, load) */
int project_exists(int slot);
void project_name_of(int slot, char *name);   /* "" if empty */
int presets_load(void);
int presets_save(void);

void ui_init(void);
void ui_frame(void);
void ui_input_only(void);
int ui_dirty(void);
void ui_say(const char *a, const char *b);
