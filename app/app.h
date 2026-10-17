/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8: the project (everything that is saved), the app's entry points and its half of the contract with fm1-base
 * (base/device/unity.c, base/host/plat_host.c). Included first by fm1-base's unity build and by its simulator. */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "seq/pattern.h"
#include "seq/seq.h"
#include "dsp/synth.h"
#include "dsp/fx.h"

#define APP_NAME "LOOM-8"
#define APP_BLOCK 256u                  /* frames a render (5.8 ms): fm1-base's USB audio input takes 256-frame halves
                                         * at most (felucca-bsp usb.c); LOOM-8's own platform had 512-frame halves,
                                         * each rendered as two 256-frame calls, which is what the simulator always did */
#define APP_CANVAS_PX (240u * 120u)     /* the tallest band, the main one, is 240 x 120 */
#define APP_BIG_FONT FONT_B             /* fm1-base's panel screens (OCT- + OCT+ at power-on): a face it has anyway */

/* the store: the panel calibration (the platform's, where every fm1-base firmware keeps it), the user presets in
 * Felucca's settings pair, then NPROJ project slots of PROJ_NOBJ objects each (the settings, then the LZ chunks of
 * the patterns), two 4 KB sectors an object, from 0x97000 up to 0xC1000. The user sample bank is raw flash beside
 * them (dsp/sample.h: 0xCF000..0xDBFFF, plat_data). Free: 0xC1000..0xCEFFF, 0xDE000..0xDFFFF. LOOM-8 was never
 * released: its own platform's map (four slots) is not kept. */
#define NPROJ 3                         /* project slots in flash */
#define PROJ_NOBJ 7
enum { OBJ_PANEL, OBJ_PRESETS, OBJ_PROJ0, OBJ_NOBJ = OBJ_PROJ0 + NPROJ * PROJ_NOBJ };
#define OBJ_PROJ(slot, k) (OBJ_PROJ0 + (slot) * PROJ_NOBJ + (k))
#define APP_STORE_MAP {0xDC000u, 0xFC000u, \
    0x97000u, 0x99000u, 0x9B000u, 0x9D000u, 0x9F000u, 0xA1000u, 0xA3000u, \
    0xA5000u, 0xA7000u, 0xA9000u, 0xAB000u, 0xAD000u, 0xAF000u, 0xB1000u, \
    0xB3000u, 0xB5000u, 0xB7000u, 0xB9000u, 0xBB000u, 0xBD000u, 0xBF000u}

/* backup and restore over USB (fm1-base's backup.c; base/tools/fm1_backup.py, base/web/backup): the user presets
 * and the three project slots, all or nothing. Not the user sample bank (raw flash, not store objects: its source is
 * the player's own WAVs, tools/loom_samples.py) */
#define APP_BACKUP_OBJS {OBJ_PRESETS, \
    OBJ_PROJ0 + 0, OBJ_PROJ0 + 1, OBJ_PROJ0 + 2, OBJ_PROJ0 + 3, OBJ_PROJ0 + 4, OBJ_PROJ0 + 5, OBJ_PROJ0 + 6, \
    OBJ_PROJ0 + 7, OBJ_PROJ0 + 8, OBJ_PROJ0 + 9, OBJ_PROJ0 + 10, OBJ_PROJ0 + 11, OBJ_PROJ0 + 12, OBJ_PROJ0 + 13, \
    OBJ_PROJ0 + 14, OBJ_PROJ0 + 15, OBJ_PROJ0 + 16, OBJ_PROJ0 + 17, OBJ_PROJ0 + 18, OBJ_PROJ0 + 19, OBJ_PROJ0 + 20}
#define APP_BACKUP_MAGIC 0x42384D4Cu     /* "LM8B" */
#define APP_BACKUP_VERSION 1u
#define APP_BACKUP_WHAT "replaces projects, presets"
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);

/* the contract: the platform calls these (app.c) */
void app_init(void);                    /* boot: the user bank, the project, the presets, the engine, the second core */
void app_render(int32_t *lr, uint32_t n, uint32_t gain_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_panic(void);                   /* silence now (before an update rewrites the flash) */
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
#define APP_SPLASH                      /* the name over the eight tracks' inks woven as threads */
void app_splash(void);
#define APP_SYSEX                       /* the user sample bank over USB-MIDI (samples.c: F0 7D 46 4C) */
int app_sysex(const uint8_t *p, uint32_t n);
#ifndef PLAT_HOST
#define APP_CSTART                      /* the second core held first thing at a start (plat_loom.c) */
void app_cstart(void);
#define APP_FAULT                       /* a crash: the second core given up, or this crash is its own (plat_loom.c:
                                         * app_fault, after felucca-bsp's fm1_crash_t) */
#endif

/* the host simulator's part (host.c): LOOM-8's checks and commands, the screen reader's dump */
#ifdef PLAT_HOST
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
#define APP_HOST_SPEAKDUMP
void app_speak_dump(void);
#define APP_HOST_REBOOT
void app_host_reboot(void);
#define APP_HOST_FAILS
int app_host_fails(void);
#endif

/* LOOM-8's own platform extras, beyond fm1-base's plat.h: on the FM-1 in plat_loom.c (felucca-bsp's helpers and the
 * second core of cpu1/), in the simulator in host.c */
uint32_t plat_cpu_peak_pct(void);       /* the render's peak load since the last call, % */
uint32_t plat_cycles(void);             /* a cycle counter for the profiler (ISR) */
int plat_store_load_prev(uint32_t obj, void *dst, uint32_t max);   /* the copy before the current one (a torn stream) */
int loom_store_save(uint32_t obj, const void *src, uint32_t len);  /* plat_store_save, the host keeping the copy before */
/* the second core (X0X's plat_cpu2_*) */
int plat_cpu2_set(int on);              /* main loop: 0 = done; -1 = it did not start (one core) */
int plat_cpu2_on(void);
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg);   /* ISR: 1 = the second core has it */
int plat_cpu2_wait(void);               /* ISR: 0 = done; -1 = it failed (and is off): do the job here */
int plat_cpu2_gave_up(void);            /* why it is off: 0 no, 1 it read a wrong job, 2 one did not finish, 3 barred */
uint32_t plat_cpu2_bad(void);
int plat_cpu2_barred(void);             /* 1: it was given up, or core 0 crashed beside it (until power-off or a new build) */
int plat_vdc14_mv(void);                /* VDC14 as set at start, mV (the Status page) */
int plat_sysvdd_mv(void);
int plat_last_crash(uint32_t v[5]);     /* 1: a crash since power-on (vec, pc, emu, dbg, rets as its red screen) */
/* the lights: lit buttons and keys; glow: the unlit buttons dim (PLAY's green one); dim_keys dim; red: PLAY red */
void loom_lights(uint32_t buttons, uint32_t keys, int glow, uint32_t dim_keys, int red);

#define PROJ_MAGIC 0x384D4F4Cu          /* "LOM8" (was "XXY1": XY-1's projects start fresh) */
#define PROJ_FORMAT 4u                  /* 4: no tape (its track and send), the reverb's modes, the master's tape,
                                         * and each drum key its envelope and filter (DP_N 9 -> 18); 2: the character
                                         * stage moved TP_PLAYER on (and every index after it) */
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
