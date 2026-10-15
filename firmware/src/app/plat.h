/* SPDX-License-Identifier: GPL-3.0-only */
/* XY-1 platform interface (from X0X's): everything the app needs from the hardware.
 *
 * The device implementation (plat_fm1.c) wraps Felucca's HAL and panel map; the
 * host implementation (host/xy_host.c) is a simulator fed by a script, which is
 * what lets the whole app — UI, sequencer, engines — run and be tested on a Mac.
 * Drawing goes through Felucca's lcd_fill / lcd_blit (lcd.c on the device, a
 * framebuffer on the host) and gfx.c on top. */
#pragma once
#include <stdint.h>

/* big zero-initialised buffers go in the .pool RAM region on the device */
#ifdef XY_HOST
#define XY_POOL
#else
#define XY_POOL __attribute__((section(".pool")))
#endif

/* panel controls, by printed label (Felucca's panel.c map turns these into matrix ids; Felucca
 * calls the second button SCL, the panel prints SEL) */
enum { B_FX, B_SEL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC,
       B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };

#define NKEYS 27                       /* F3 .. G5 */

uint32_t plat_ms(void);
uint32_t plat_buttons(void);           /* held: bit B_* */
uint32_t plat_keys(void);              /* held: bit k = key k (0 = F3) */
int32_t plat_enc(int role);            /* detents since the last call, + = clockwise; EN_* */
uint32_t plat_master(void);            /* MASTER pot, 0..4096 gain Q12 */
void plat_leds(uint32_t buttons, uint32_t keys);   /* lit: bit B_*, bit key */
void plat_glow(int on);                /* the unlit buttons glow dimly, readable in the dark (call before plat_leds) */
void plat_play_red(int red);           /* PLAY lights green; red (its other LED) while recording (before plat_leds) */
void plat_keys_dim(uint32_t keys);     /* keys lit dim (one scan frame in four; call before plat_leds) */

/* USB MIDI, 4-byte USB-MIDI event packets (cable 0) */
int plat_midi_in(uint32_t *pkt);       /* 1 = got one (called from the audio ISR) */
void plat_midi_out(uint32_t pkt);      /* queue one (audio ISR or main loop) */

/* persistent storage (Felucca storage.c objects: A/B sectors, CRC, torn-write safe) */
int plat_store_load(uint32_t obj, void *dst, uint32_t max);   /* bytes loaded, < 0 = none / bad */
int plat_store_save(uint32_t obj, const void *src, uint32_t len);   /* 0 = ok */
int plat_store_load_prev(uint32_t obj, void *dst, uint32_t max);   /* the copy before the current one (a torn stream) */
#define PLAT_STORE_MAX 3840u           /* bytes per object */
/* objects: the panel calibration, the user presets, then NPROJ project slots of PROJ_NOBJ objects each
 * (the settings, then the LZ chunks of the patterns). Flash map in storage.c (Felucca's data area). */
#define PROJ_NOBJ 7
enum { OBJ_PANEL, OBJ_PRESETS, OBJ_PROJ0, OBJ_NOBJ = OBJ_PROJ0 + 4 * PROJ_NOBJ };
#define OBJ_PROJ(slot, k) (OBJ_PROJ0 + (slot) * PROJ_NOBJ + (k))

/* audio load in % of the render budget, and xruns since boot (device: from the ISR) */
uint32_t plat_cpu_pct(void);
uint32_t plat_cpu_peak_pct(void);
uint32_t plat_xruns(void);
uint32_t plat_cycles(void);            /* a cycle counter for the profiler (ISR) */

/* the second core (X0X's plat_cpu2_*) */
int plat_cpu2_set(int on);             /* main loop: 0 = done; -1 = it did not start (one core) */
int plat_cpu2_on(void);
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg);   /* ISR: 1 = the second core has it */
int plat_cpu2_wait(void);              /* ISR: 0 = done; -1 = it failed (and is off): do the job here */
int plat_cpu2_gave_up(void);           /* why it is off: 0 no, 1 it read a wrong job, 2 one did not finish, 3 barred */
uint32_t plat_cpu2_bad(void);
int plat_cpu2_barred(void);            /* 1: since power-on it was given up, or core 0 crashed beside it */
int plat_vdc14_mv(void);               /* VDC14 as set at start, mV (the Status page) */
int plat_sysvdd_mv(void);
int plat_last_crash(uint32_t v[5]);    /* 1: a crash since power-on (vec, pc, emu, dbg, rets as its red screen) */
