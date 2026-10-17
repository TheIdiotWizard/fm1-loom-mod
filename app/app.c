/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 on fm1-base: the app's unity build (compiled into fm1-base's device/unity.c after the platform, and into its
 * simulator host/plat_host.c) and the glue between fm1-base's contract (app.h) and LOOM-8's parts: its drawing
 * (gfx_loom.c), the engine (engine.c; the DSP and the sequencer in dsp/ and seq/, their own -O2 units), the project
 * (project.c), the pages (pages.c), the input (ui.c), the user sample bank over USB (samples.c), and on the FM-1 its
 * platform extras (plat_loom.c: the second core, the profiler's clock, the supplies). */
#include "gfx_loom.c"
#include "engine.c"
#include "project.c"
#include "pages.c"
#include "ui.c"
#include "samples.c"
#ifndef PLAT_HOST
#include "plat_loom.c"
#else
static uint32_t render_t0(void);
static uint32_t render_pct(uint32_t t0, uint32_t n);
static int load_force = -1;             /* the script's "load N": the guard sees N %, -1 measured (host.c) */
#endif

_Static_assert(OBJ_NOBJ == 23 && OBJ_PROJ0 == 2, "the store map lists every object (APP_STORE_MAP)");
_Static_assert(SMP_BANK_BASE >= 0xC1000u && SMP_BANK_BASE + SMP_BANK_SIZE <= 0xDC000u,
               "the user sample bank between the project slots and the panel calibration");

/* the lights: LOOM-8's (the unlit buttons glowing, keys dim, PLAY green or red) on fm1-base's plat_lights */
void loom_lights(uint32_t buttons, uint32_t keys, int glow, uint32_t dim_keys, int red)
{
    uint32_t play = 1u << B_PLAY, b = buttons, dim = glow ? ((1u << NB) - 1u) & ~buttons : 0u;
    if (!red) {                                     /* PLAY's green LED, unless recording */
        b = (b & ~play) | (buttons & play ? LED_PLAY_GREEN : 0u);
        dim = (dim & ~play) | (dim & play ? LED_PLAY_GREEN : 0u);
    }
    plat_lights(b, keys, dim, dim_keys & ~keys, glow || dim_keys ? 2 : 0);
}

/* ---- the contract */
void app_init(void)
{
#ifndef PLAT_HOST
    plat_loom_init();
#endif
    smp_user_attach(plat_data(SMP_BANK_BASE));      /* the user bank, read in place (none without the flash) */
    smp_user_scan();                                /* (an empty or torn bank: no user samples) */
    project_load(0);                                /* defaults when nothing (or another format) is saved */
    presets_load();
    engine_init(&pats, proj_par);
    project_apply();
#ifndef PLAT_HOST
    if (plat_cpu2_barred() && !proj.one_core) {     /* it crashed (or was given up) with two cores: one from now on */
        proj.one_core = 1;
        project_save(proj_slot);                    /* written now, on one core, the project as it loaded;
                                                     * Setup > 2nd core turns it back on */
    }
    if (!proj.one_core && plat_loom_two_cores_ok() &&   /* after a failed start, one core (as X0X) */
        plat_cpu2_set(1) == 0)                      /* the second core: at power-on, before the audio and the */
        engine_set_core2(plat_cpu2_run, plat_cpu2_wait);   /* timers, as X0X starts it; if it does not answer, one core */
#else
    engine_set_core2(plat_cpu2_run, plat_cpu2_wait);
#endif
}

void app_render(int32_t *lr, uint32_t n, uint32_t gain_q12)
{
    uint32_t t0 = render_t0(), pct;
    engine_render(lr, n, gain_q12);
    pct = render_pct(t0, n);
#ifdef PLAT_HOST
#ifdef PLAT_WEB
    pct = load_force >= 0 ? (uint32_t)load_force : 0u;   /* a browser's timing is not the FM-1's */
#else
    pct = load_force >= 0 ? (uint32_t)load_force : pct;
#endif
#endif
    engine_load(pct);                               /* the overload guard's input */
}

void app_panic(void)
{
    engine_transport(0);
    engine_panic();
}
void app_ui_init(void) { ui_init(); }               /* boot; again after an update that did not happen, a restore */
void app_frame(void)
{
    plat_usb_audio_full(proj.usb_full);             /* Setup's USB AUDIO: follows the knob, or full */
    ui_frame();
}
void app_input_only(void) { ui_input_only(); }
void app_say(const char *a, const char *b) { ui_say(a, b); }
int app_sysex(const uint8_t *p, uint32_t n) { return smp_frame(p, n); }

/* the name over the eight tracks' inks woven as threads, in the default theme (pages.c THEMES[0]) */
void app_splash(void)
{
    const theme_t *t = &THEMES[0];
    lcd_fill(0, 0, 240, 240, t->bg);
    cv_begin(240, 96, t->bg);
    {
        int32_t w = text_w(&FONT_L, "LOOM-8");
        cv_text((240 - w) / 2, 0, &FONT_L, "LOOM-8", t->text);
        for (int i = 0; i < 8; i++)                  /* the warp: eight threads, each over and under the weft */
            for (int j = 0; j < 12; j++)
                cv_rect(56 + i * 16, 44 + j * 3, 8, (i + j) & 1 ? 3 : 2, (i + j) & 1 ? t->ink[i] : t->panel2);
        w = text_w(&FONT_S, "a pocket groovebox");
        cv_text((240 - w) / 2, 76, &FONT_S, "a pocket groovebox", t->dim);
    }
    cv_blit(0, 76);
}

/* backup and restore over USB (fm1-base's backup.c): the slots and the presets as saved (LOOM-8 saves when asked:
 * what is not saved stays so); no saving while a backup streams; a restore: everything from the flash again */
int app_backup_playing(void) { return seq.playing != 0; }
void app_backup_flush(void) {}
void app_backup_hold(int on) { store_held = (uint8_t)(on != 0); }
void app_backup_restored(void)
{
    memset(stored_known, 0, sizeof stored_known);  /* (what project.c knew of the flash is stale) */
    if (project_load(proj_slot) != 0)
        proj_slot = 0;
    presets_load();
    after_load();
    ui.dirty = 0;
}

#ifdef PLAT_HOST
#include "host.c"                                   /* the host simulator's part: LOOM-8's checks and commands */
#endif

/* the platform's own code after this (fm1-base's main_fm1.c) draws with its own fonts again */
#undef felucca_font_t
#undef FONT_XS
#undef FONT_S
#undef FONT_B
#undef FONT_M
#undef FONT_L
#undef cv_begin
#undef cv_line
#undef cv_text
#undef text_w
#undef draw_text_box
