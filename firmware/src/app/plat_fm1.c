/* SPDX-License-Identifier: GPL-3.0-only */
/* XY-1 platform on the FM-1 (NuSaw's, with X0X's cycle counter): plat.h over Felucca's HAL, panel
 * map, USB rings and storage. Part of the unity build (main.c), after the HAL, usb.c, storage.c and
 * panel.c. */

uint32_t plat_ms(void) { return fm1_ms; }

uint32_t plat_buttons(void)
{
    uint32_t raw = fm1_in.buttons, out = 0, i;
    for (i = 0; i < NB; i++)
        if (raw & (1u << panel.btn[i]))
            out |= 1u << i;
    return out;
}

uint32_t plat_keys(void) { return fm1_in.notes & ((1u << NKEYS) - 1u); }

int32_t plat_enc(int role)
{
    if (role < 0 || role >= NE)
        return 0;
    return fm1_enc_take(panel.enc[role]) * panel.dir[role];
}

static volatile uint32_t master_q12 = 2048;
uint32_t plat_master(void) { return master_q12; }

/* LEDs: the picture is built off-line and copied one byte per column (Felucca: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker) */
static uint8_t led_pos[FM1_NKEY];
static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < FM1_NKEY; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}
/* Glow: every button not lit is lit dim, on one scan frame in GLOW_DIV (the HAL's fm1_led_dim) */
#define GLOW_DIV 4u
static uint8_t glow_on = 1;
void plat_glow(int on) { glow_on = (uint8_t)(on != 0); }
/* PLAY has two LEDs: its red at the button's own position, a green at the keyless matrix position
 * column 8, row PA5 (found on the device). Green, unless recording. */
static uint8_t play_red;
void plat_play_red(int red) { play_red = (uint8_t)(red != 0); }
static uint32_t keys_dim;
void plat_keys_dim(uint32_t keys) { keys_dim = keys; }
void plat_leds(uint32_t buttons, uint32_t keys)
{
    uint8_t nl[FM1_NCOL] = {0}, dl[FM1_NCOL] = {0};
    uint32_t i, c;
    for (i = 0; i < NB + NKEYS; i++) {
        uint32_t id = i < NB ? panel.btn[i] : 14u + (i - NB);
        int on = i < NB ? (buttons >> i) & 1u : (keys >> (i - NB)) & 1u;
        uint8_t q = led_pos[id];
        if (i == B_PLAY && !play_red)            /* PLAY's green LED instead (keyless position col 8, row PA5) */
            q = (uint8_t)((8u << 3) | 1u);
        if (q == 0xFF)
            continue;
        if (on)
            nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
        else if (i < NB ? glow_on : (keys_dim >> (i - NB)) & 1u)
            dl[q >> 3] |= (uint8_t)(1u << (q & 7u));
    }
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led[c] = nl[c];
        fm1_led_dim[c] = dl[c];
    }
    fm1_led_dim_div = glow_on || keys_dim ? GLOW_DIV : 0u;
}

/* MIDI: Felucca's rings carry 4-byte USB-MIDI event packets, byte 0 = cable / CIN */
int plat_midi_in(uint32_t *pkt)
{
    if (mi_r == mi_w)
        return 0;
    *pkt = midi_in_q[mi_r % MQ];
    mi_r++;
    return 1;
}
void plat_midi_out(uint32_t pkt) { midi_out_event(pkt); }

int plat_store_load(uint32_t obj, void *dst, uint32_t max) { return flash_ok ? st_load(obj, dst, max) : -1; }
int plat_store_load_prev(uint32_t obj, void *dst, uint32_t max) { return flash_ok ? st_load_prev(obj, dst, max) : -1; }
int plat_store_save(uint32_t obj, const void *src, uint32_t len) { return flash_ok ? st_save(obj, src, len) : -9; }

static volatile uint32_t audio_cpu_pct, audio_xruns, audio_peak_pct;
uint32_t plat_cpu_pct(void) { return audio_cpu_pct; }
uint32_t plat_xruns(void) { return audio_xruns; }
uint32_t plat_cpu_peak_pct(void)                      /* the Status page's peak since it last asked */
{
    uint32_t p = audio_peak_pct;
    audio_peak_pct = 0;
    return p;
}
/* the profiler's clock: the CPU's cycle counter when it runs, else the 24 MHz timer (X0X) */
static int perf_cyc_ok;
static void plat_perf_init(void) { perf_cyc_ok = fm1_perf_cycles_run(); }
uint32_t plat_cycles(void) { return perf_cyc_ok ? fm1_perf_cycles() : fm1_ticks(); }

/* the second core (X0X's plat_cpu2_*) */
int plat_cpu2_set(int on)
{
    int rc;
    if (!on) {
        fm1_cpu1_hold();
        return 0;
    }
    {
        int locked = fm1_guard_top_locked();            /* at boot the top is not locked yet: leave it so */
        if (locked)
            fm1_guard_unlock_top();                     /* its entry word is in the protected top of RAM */
        fm1_guard_pc_open();                            /* it starts in the chip's ROM */
        rc = fm1_cpu1_start();
        fm1_guard_enable(FM1_GUARD_PC);
        if (locked)
            fm1_guard_lock_top();
    }
    return rc;
}
int plat_cpu2_on(void) { return fm1_c1_on; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg) { return fm1_cpu1_run(fn, arg); }
int plat_cpu2_wait(void) { return fm1_cpu1_wait(); }
int plat_cpu2_gave_up(void) { return fm1_c1_gave_up; }
uint32_t plat_cpu2_bad(void) { return fm1_c1_mb.bad; }
uint8_t plat_vdc14_level, plat_sysvdd_level;    /* main_fm1.c, at start */
int plat_vdc14_mv(void) { return 1250 + 50 * plat_vdc14_level; }
int plat_sysvdd_mv(void) { return 930 + 30 * plat_sysvdd_level; }
int plat_last_crash(uint32_t v[5])                   /* fm1_crash (.noinit): a warm reset keeps it, power-off clears it */
{
    if (fm1_crash.magic != FM1_CRASH_MAGIC)
        return 0;
    v[0] = fm1_crash.vec, v[1] = fm1_crash.pc, v[2] = fm1_crash.emu, v[3] = fm1_crash.dbg, v[4] = fm1_crash.rets;
    return 1;
}
int plat_cpu2_barred(void) { return fm1_c1_bar.magic == FM1_C1_BAR_MAGIC && fm1_c1_bar.barred == 1u; }
