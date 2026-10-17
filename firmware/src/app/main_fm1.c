/* SPDX-License-Identifier: GPL-3.0-only
 * Boot, audio ISR and main loop adapted from Felucca's main.c / audio.c, Copyright (C) 2026
 * Leo Kuroshita (@kurogedelic), Hugelton Instruments. */
/* LOOM-8 on the FM-1 (NuSaw's, from X0X's): boot (WDT first, boot-loop guard, guards), LCD, input (TIMER5
 * 10 kHz), audio (ALNK0), USB; then the main loop: UI at ~60 frames/s, input polled in between. */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[], _c1_start[], _c1_end[], _c1_load[];
extern uint32_t _dsp_start[], _dsp_end[], _dsp_load[];

/* --------------------------------------------------------------- audio --- */
/* X0X's 512-frame halves (11.6 ms): the sequencer's cost averages over a longer block, and a voice-heavy
 * stretch has room; the render works in 256-frame calls, half the voices on the second core */
#define HALF_FRAMES 512u                    /* I2S half buffer: 11.6 ms at 44.1 kHz */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define FS 44100u
#define REND_FRAMES 256u                    /* engine_render's frames per call */
#if FELUCCA_UAC
_Static_assert(REND_FRAMES == UA_HALF, "usb.c sizes the USB audio ring band for this block");
#endif
static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));

#define DBG_MAGIC 0x44424731u                       /* "DBG1": read with `fm1t memr` */
struct xy_dbg {
    uint32_t magic, halves, max_us, nested, in_audio, late, timer_irqs, ui_frames;
    uint32_t last_us, cpu_q8, boots, stage;
    uint32_t crash_seen;                            /* the crash count already reported at a boot */
} xy_dbg __attribute__((section(".noinit")));
static uint8_t safe_mode;                           /* two failed boots: no audio, USB on (safe_main) */

static void render_block(int32_t *o)                /* ALNK0: one half, measured */
{
    uint32_t t0 = fm1_ticks(), us, budget = HALF_FRAMES * 1000000u / FS;
    xy_dbg.in_audio = 1;
#if FELUCCA_UAC
    uac_render_start();
#endif
    {
        uint32_t k;
        for (k = 0; k < HALF_FRAMES; k += REND_FRAMES) {
            engine_render(o + 2u * k, REND_FRAMES, master_q12);
#if FELUCCA_UAC
            uac_tap(o + 2u * k, REND_FRAMES);       /* the USB audio input: the same master output */
#endif
        }
    }
    us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
    xy_dbg.cpu_q8 = (xy_dbg.cpu_q8 * 15u + (us * 256u) / budget) / 16u;
    audio_cpu_pct = (xy_dbg.cpu_q8 * 100u) >> 8;
    if (us * 100u / budget > audio_peak_pct)
        audio_peak_pct = us * 100u / budget;        /* the Status page's peak (read and reset) */
    engine_load(us * 100u / budget);                /* the overload guard's input */
    xy_dbg.halves++;
    xy_dbg.last_us = us;
    if (us > xy_dbg.max_us)
        xy_dbg.max_us = us;
    xy_dbg.in_audio = 0;
}

void fm1_alnk0_irq(void)                            /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p = fm1_audio_pending();
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half();
        render_block(&abuf[half * HALF_WORDS]);
        fm1_audio_ack_half();
        if (fm1_audio_free_half() != half) {
            xy_dbg.late++;                         /* the DMA moved on while we rendered */
            audio_xruns++;
        }
    }
}
extern void isr_alnk0(void);

static void audio_init(void)
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}

static void audio_silence(void)                     /* IRQs off (flash erase): the DMA would loop stale audio */
{
    uint32_t i;
    for (i = 0; i < sizeof abuf / sizeof abuf[0]; i++)
        abuf[i] = 0;
}

/* ------------------------------------------------------------ timer --- */
static uint8_t usb_due;
void fm1_timer5_irq(void)
{
    static uint32_t sub;
    fm1_timer5_ack();
    xy_dbg.timer_irqs++;
    if (xy_dbg.in_audio)
        xy_dbg.nested++;
    fm1_input_tick();
    /* 2 kHz: all USB SIE traffic lives here. This ISR now nests into the audio render (so the key
     * matrix keeps its rhythm, below); USB shares the MIDI queues with the audio ISR, so a poll that
     * falls inside the render waits for the first tick outside it, as it did before the nesting. */
    if (sub % 5u == 0u) {
        usb_due = 1;
#if FELUCCA_UAC
        /* the USB audio stream cannot wait for the render (one packet per 1 ms frame, a render takes
         * up to ~11.6 ms): it runs nested too. It touches only EP4 (INDEX is set on every access) and
         * the consumer side of its ring, and usb_poll never runs nested, so the two never interleave */
        if (xy_dbg.in_audio)
            uac_service();
#endif
    }
    if (usb_due && !xy_dbg.in_audio) {
        usb_due = 0;
        usb_poll();
#if XY_TRS
        uart_midi_poll();                           /* the TRS jack: same context, same queue */
#endif
    }
    if (++sub == 10u)
        sub = 0;
    {
        static uint32_t last, acc;
        uint32_t now = fm1_ticks();
        acc += now - last;
        last = now;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
}
extern void isr_timer5(void);

/* ------------------------------------------------------------- boot --- */
#define BOOTGUARD_MAGIC 0x42475244u
struct bootguard_s bootguard __attribute__((section(".noinit")));   /* declared in main.c (ota_commit) */

static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
    if (fm1_c1_on)
        fm1_cpu1_bar();                             /* a crash beside the second core: one core until power-off */
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    draw_text_box(0, 8, 240, &FONT_S, "LOOM-8 CRASH", C_WHITE, 1);
    hexs(b, c->vec);
    draw_text_box(10, 40, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->pc);
    draw_text_box(10, 60, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->emu);
    draw_text_box(10, 84, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->dbg);
    draw_text_box(10, 102, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->rets);
    draw_text_box(10, 120, 220, &FONT_S, b, C_WHITE, 0);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

static void enter_uboot(const char *why)
{
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 110, 240, &FONT_S, why, C_WHITE, 1);
    fm1_delay_ms(20);
    usb_detach();
    fm1_delay_ms(30);
    bootguard.pending = 0;                          /* intentional reset: not a failed boot */
    fm1_enter_uboot();
}

/* SAFE MODE: LOOM-8 crashed (or hung) twice while starting. Nothing that makes sound runs: no audio,
 * no engine, no UI; USB is on, so the web installer (LOOM-8, or the stock firmware) and M-UPGRADE can
 * reach it. PLAY tries LOOM-8 again. The chip's own update mode stays the last resort (fm1_cstart). */
static void safe_main(void)
{
    char b[24];
    uint32_t t0, play, both, t_oct;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 18, 240, &FONT_B, "LOOM-8 SAFE MODE", C_HI, 1);
    draw_text_box(0, 52, 240, &FONT_S, "IT CRASHED TWICE", C_WHITE, 1);
    draw_text_box(0, 72, 240, &FONT_S, "WHILE STARTING.", C_WHITE, 1);
    draw_text_box(0, 102, 240, &FONT_S, "NO SOUND. USB IS ON:", C_WHITE, 1);
    draw_text_box(0, 122, 240, &FONT_S, "REINSTALL FROM THE", C_WHITE, 1);
    draw_text_box(0, 142, 240, &FONT_S, "WEB INSTALLER.", C_WHITE, 1);
    draw_text_box(0, 172, 240, &FONT_S, "PLAY: TRY AGAIN", C_HI, 1);
    draw_text_box(0, 190, 240, &FONT_S, "HOLD OCT- + OCT+: UPDATE", C_GRAY, 1);
    if (fm1_crash.magic == FM1_CRASH_MAGIC) {
        b[0] = 'P';
        b[1] = 'C';
        b[2] = ' ';
        hexs(b + 3, fm1_crash.pc);
        draw_text_box(0, 208, 240, &FONT_S, b, C_GRAY, 1);
    }
    fm1_input_init();
    panel_init();
    usb_start();
    fm1_timer5_start(isr_timer5, 1);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    play = 1u << panel.btn[B_PLAY];
    both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
    t0 = t_oct = fm1_ms;
    for (;;) {
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if (bootguard.pending && fm1_ms - t0 > 10000u)
            bootguard.pending = 0;                  /* safe mode itself is up: not another failed boot */
        if ((fm1_in.buttons & play) && fm1_ms - t0 > 500u) {
            bootguard.failed = 0;                   /* try LOOM-8 again, from scratch */
            bootguard.pending = 0;
            draw_text_box(0, 172, 240, &FONT_S, "STARTING LOOM-8...", C_HI, 1);
            usb_detach();
            fm1_delay_ms(30);
            fm1_reboot();
        }
        if ((fm1_in.buttons & both) != both)        /* OCT- + OCT+ held 5 s: the chip's update mode, as in X0X
                                                     * (fm1-x0x#20: with USB gone, the only way left) */
            t_oct = fm1_ms;
        else if (fm1_ms - t_oct > 5000u)
            enter_uboot("UBOOT");
        ota_service();
        if (usb.ota_req) {
            usb.ota_req = 0;
            if (flash_ok)
                ota_session();                      /* an install commits and resets; else back here */
        }
        if (usb.uboot_req)
            enter_uboot("UBOOT (USB)");
    }
}

/* remote control: F0 7D 4C 43 ("LC") op ... F7, 7-bit bytes (fm1-base's, tools/fm1_remote.py there):
 *   1 b0 b1          the buttons held (B_* bits: b0 the low seven, b1 the next seven)
 *   2 k0 k1 k2 k3    the keys held (bit k = key k, seven bits a byte, low first)
 *   3 role delta     an encoder turned (EN_*; delta - 64 detents)
 *   4 lo hi          MASTER's position (0..4096; 4097 or more: the pot's own again)
 * Each is added to the panel's own input (plat_fm1.c remote); the answer is F0 7D 4C 43 op F7. */
static int remote_frame(const uint8_t *p, uint32_t n)
{
    static uint8_t ack[6] = {0xF0, 0x7D, 0x4C, 0x43, 0, 0xF7};
    if (n < 4u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x43u)
        return 0;
    switch (p[3]) {
    case 1:
        if (n >= 6u)
            remote.btn = ((uint32_t)p[4] | (uint32_t)p[5] << 7) & ((1u << NB) - 1u);
        break;
    case 2:
        if (n >= 8u)
            remote.keys = ((uint32_t)p[4] | (uint32_t)p[5] << 7 | (uint32_t)p[6] << 14 | (uint32_t)p[7] << 21) &
                          ((1u << NKEYS) - 1u);
        break;
    case 3:
        if (n >= 6u && p[4] < NE)
            remote.enc[p[4]] += (int32_t)p[5] - 64;
        break;
    case 4:
        if (n >= 6u) {
            uint32_t v = (uint32_t)p[4] | (uint32_t)p[5] << 7;
            remote.master = v > 4096u ? -1 : (int32_t)v;
        }
        break;
    default:
        return 1;
    }
    ack[4] = p[3];
    ota_wire_send(ack, sizeof ack);
    return 1;
}

static void splash(void)
{
    /* the name over the eight tracks' inks woven as threads, in the default theme (pages.c THEMES[0]) */
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
    lcd_sync();
}

static void fm1_main(void)
{
    int32_t knob = 512 * 16;
    uint32_t last_frame = 0;
    {
        uint32_t f = irq_save();
        fm1_vdc14_raise(7);                         /* M-VAVE's own supplies before anything loads them: */
        fm1_sysvdd_raise(15);                       /* 1.60 V, then 1.38 V (fm1_sys.h) */
        plat_vdc14_level = (uint8_t)fm1_vdc14();
        plat_sysvdd_level = (uint8_t)fm1_sysvdd();
        flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;   /* the expected 1 MiB part, else RAM only */
        irq_restore(f);
        if (flash_ok) {
            fl_plain_window_init();                 /* the user sample bank reads as plaintext through XIP (X0X's) */
            smp_user_attach(fm1_xip_ptr(SMP_BANK_BASE));
            smp_user_scan();                        /* (an empty or torn bank: no user samples) */
            ota_boot_cleanup();
        }
    }
    lcd_init();
    palette_set(1);
    splash();
    if (xy_dbg.magic != DBG_MAGIC) {
        memset(&xy_dbg, 0, sizeof xy_dbg);
        xy_dbg.magic = DBG_MAGIC;
    }
    if (safe_mode)
        safe_main();                                /* never returns */
    if (fm1_crash.magic == FM1_CRASH_MAGIC && fm1_crash.count != xy_dbg.crash_seen) {
        char b[16];                                 /* a crash since the last boot: say so, a moment */
        xy_dbg.crash_seen = fm1_crash.count;
        b[0] = 'P';
        b[1] = 'C';
        b[2] = ' ';
        hexs(b + 3, fm1_crash.pc);
        draw_text_box(0, 176, 240, &FONT_S, fm1_crash.core == 1 ? "THE SECOND CORE CRASHED"
                      : "RESTARTED AFTER A CRASH", RGB(255, 80, 60), 1);
        draw_text_box(0, 196, 240, &FONT_S, b, C_GRAY, 1);
        fm1_wdt_feed();
        fm1_delay_ms(2500);
        fm1_wdt_feed();
    }
    xy_dbg.boots++;
    xy_dbg.max_us = 0;
    plat_perf_init();                               /* the profiler's clock */
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    led_pos_init();
    project_load(0);                                /* defaults when nothing (or another format) is saved */
    presets_load();
    engine_init(&pats, proj_par);
    project_apply();
    if (plat_cpu2_barred() && !proj.one_core) {     /* it crashed (or was given up) with two cores: one from now on */
        proj.one_core = 1;
        project_save(proj_slot);                    /* written now, on one core, the project as it loaded;
                                                     * Setup > 2nd core turns it back on */
    }
    if (!proj.one_core && !bootguard.failed &&      /* after a failed start, one core (as X0X) */
        plat_cpu2_set(1) == 0)                      /* the second core: at power-on, before the audio and the */
        engine_set_core2(plat_cpu2_run, plat_cpu2_wait);   /* timers, as X0X starts it; if it does not answer, one core */
    audio_init();
    usb_start();
#if XY_TRS
    uart_midi_init();                               /* before TIMER5: it read-modify-writes port H too */
#endif
    /* above ALNK0 (3): the key / encoder / LED matrix is scanned one column per tick and must keep
     * its rhythm through a render (X0X's finding: below the audio, turns lost steps and LEDs flickered) */
    fm1_timer5_start(isr_timer5, 4);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_delay_ms(30);
    if ((fm1_in.buttons & 3u) == 3u)
        panel_setup();                              /* OCT- + OCT+ held at power-on */
    fm1_delay_ms(400);
    ui_init();

    for (;;) {
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if (fm1_ms > 30000u && bootguard.pending) {     /* a crash or hang in the first 30 s counts */
            bootguard.pending = 0;
            bootguard.failed = 0;
        }
        {
            int32_t a = fm1_adc_read(FM1_ADC_MASTER);
            if (a >= 0) {
                uint32_t k10;
                knob += (a * 16 - knob) / 8;
                k10 = (uint32_t)(knob / 16);
                master_q12 = remote.master >= 0 ? (uint32_t)remote.master : (k10 * k10) >> 8;   /* 0 .. ~4096 */
            }
        }
        {   /* OCT- + OCT+ held 5 s: update mode (Felucca's, unchanged) */
            static uint32_t t0, shown;
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if ((fm1_in.buttons & both) != both) {
                if (shown)
                    ui_say("UPDATE MODE", "CANCELLED");
                shown = 0;
                t0 = fm1_ms;
            } else if (fm1_ms - t0 > 2000u && fm1_ms - t0 <= 5000u) {
                uint32_t left = (5000u - (fm1_ms - t0) + 999u) / 1000u;
                if (left != shown) {
                    char d[4] = {(char)('0' + left), '.', '.', 0};
                    ui_say("UPDATE MODE IN", d);
                    shown = left;
                }
            } else if (fm1_ms - t0 > 5000u) {
                enter_uboot("UBOOT");
            }
        }
        {   /* a screen reader's SysEx (announce.c), the remote, the sample bank (samples.c), before the updater
             * looks */
            const uint8_t *p;
            uint32_t n;
            if (ota_frame_get(&p, &n) && (ann_frame(p, n, fm1_ms) || remote_frame(p, n) || smp_frame(p, n)))
                ota_frame_done();
        }
        ota_service();                              /* M-UPGRADE handshake (other SysEx is dropped) */
        if (usb.ota_req) {
            usb.ota_req = 0;
            engine_transport(0);
            engine_panic();
            if (flash_ok)
                ota_session();                      /* returns only if nothing was committed */
            ui_init();
        }
        if (usb.uboot_req)
            enter_uboot("UBOOT (USB)");
#if XY_CDC
        cdc_task();
#endif
        if (fm1_ms - last_frame >= 16u) {           /* ~60 frames/s at most; input in between */
            last_frame = fm1_ms;
            xy_dbg.ui_frames++;
            ui_frame();
        } else {
            ui_input_only();
        }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt;
    fm1_cpu1_halt();                                /* a second core left running by the reset would read
                                                     * the RAM cleared below (Jangada, fm1-x0x#10) */
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    if (bootguard.magic != BOOTGUARD_MAGIC) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = 0;
        bootguard.pending = 0;
    }
    if (bootguard.pending)
        bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 4u) {                   /* safe mode failed too: the chip's own update mode */
        bootguard.failed = 0;
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
    p3 |= (bootguard.failed >= 2u) << 8;            /* two failed boots: safe mode (kept past the .bss clear) */
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _c1_start; s++, d++)
        *d = *s;
    for (s = _c1_load, d = _c1_start; d < _c1_end; s++, d++)
        *d = *s;                                    /* the second core's loop (fm1_cpu1.h) */
    for (s = _dsp_load, d = _dsp_start; d < _dsp_end; s++, d++)
        *d = *s;                                    /* the audio's code in RAM (XY_RAM_DSP builds) */
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    safe_mode = (uint8_t)(p3 >> 8);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
    fm1_main();
    for (;;)
        ;
}
