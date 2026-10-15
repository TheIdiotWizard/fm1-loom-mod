/* SPDX-License-Identifier: GPL-3.0-only */
/* XY-1 on the host: the whole app (UI, project, sequencer, engine) against a simulated FM-1 driven
 * by a script. Audio goes to a WAV (never played), the screen to PNGs, the lights to text, MIDI out
 * to a log. The device-only parts (HAL, USB, OTA, flash driver) are replaced by plat.h implemented
 * here. (From FoMni's omni_host, with X0X / SPH-16's engine commands.)
 *
 *   xy_host SCRIPT [OUTDIR]
 *
 * Script, one command per line (# comments). Tracks, patterns, steps and scenes count from 1 in a
 * script, as the screen shows them; "expect step" reports the playhead 0-based (eng_step).
 *   wait MS                      run the device for MS milliseconds
 *   press BTN | release BTN | tap BTN     (BTN: FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+)
 *   key K down|up | tapkey K     (K: 0..26 = F3..G5, or w0..w15 white keys, b0..b10 black keys)
 *   strum A B [MS]               white keys A..B pressed in turn (down then up), MS apart (default 25)
 *   turn ENC N [MS]              (ENC: SELECT ALGO PRESET K1 K2 K3 K4; N detents, MS apart, default 80)
 *   spin ENC N                   N detents at once
 *   master N                     MASTER pot 0..4096
 *   midi B0 B1 B2                incoming USB MIDI message (hex bytes)
 *   param T P V                  parameter P (synth.h TP_*) of track T set to V, as a knob would
 *   load N                       every block costs N % (the overload guard's input); -1 = measured
 *   steps T PAT                  print track T's pattern PAT: each step's notes, component, condition
 *   step T PAT S NOTE VEL        write one note on step S (NOTE 0 clears the step)
 *   scene N                      switch to scene N now
 *   bpm N                        the tempo
 *   cores N                      the second core on (1) or off (0): its job then runs in place
 *   wav FILE | wavstop           start / stop recording the output
 *   peakreset                    restart the output peak (expect peak_db_min / peak_db_max)
 *   shot FILE.png                save the screen
 *   leds                         print the lit buttons and keys
 *   expect WHAT VALUE            check state: exit 1 on mismatch; VALUE "<N" / ">N" is a bound. WHAT:
 *                                playing, step T, scene, voices, dirty, store_writes, midi_out, clocks_out,
 *                                tempo, trk, page NAME, comp T S, note T PAT S, cores, guard, parT.P,
 *                                peak_db_max / peak_db_min
 *   reboot                       re-run boot from the simulated flash (persistence test)
 *   listen                       a screen reader starts listening (HELLO, again every 2 s): each SAY is printed
 *   where                        the reader asks for everything on the screen
 *   said TEXT                    some SAY since the last "said" (or "listen") has TEXT in it: else a failure
 *   speakdump                    every page, knob value and message as the reader gets them (the audit)
 * XY_SAID_LOG=FILE: a reader listens from boot, quietly, and every SAY is appended to FILE (the audit).
 */
#define XY_HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#define __attribute__(x)

/* ------------------------------------------------------------ screen --- */
static uint16_t fb[240 * 240];          /* RGB565, byte-swapped as the panel takes it (gfx.c) */
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t sc = (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8));
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = sc;
}
static uint32_t blits;
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    blits++;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u)
                fb[(y + j) * 240u + x + i] = px[j * w + i];
}
#include "../firmware/src/gfx.c"

/* ---------------------------------------------------------- platform --- */
#include "../firmware/src/app/plat.h"

static uint32_t now_ms, held_btn, held_keys, master = 2048;
static int32_t enc_acc[NE];
static uint32_t lit_btn, lit_keys;

uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_buttons(void) { return held_btn; }
uint32_t plat_keys(void) { return held_keys; }
int32_t plat_enc(int role)
{
    int32_t v = enc_acc[role];
    enc_acc[role] = 0;
    return v;
}
uint32_t plat_master(void) { return master; }
void plat_leds(uint32_t b, uint32_t k)
{
    lit_btn = b;
    lit_keys = k;
}
static int glow_on = 1;                 /* the device's button glow (plat_glow); the lights printed are the bright ones */
void plat_glow(int on) { glow_on = on; }
void plat_play_red(int red) { (void)red; }      /* the device's PLAY colour: green, red while recording */
void plat_keys_dim(uint32_t keys) { (void)keys; }

#define MQ 256
static uint32_t min_q[MQ], mi_w, mi_r;
static FILE *midi_log;
static uint32_t midi_out_count, clock_out_count;
int plat_midi_in(uint32_t *pkt)
{
    if (mi_r == mi_w)
        return 0;
    *pkt = min_q[mi_r++ % MQ];
    return 1;
}
void plat_midi_out(uint32_t pkt)
{
    midi_out_count++;
    if (((pkt >> 8) & 0xFF) == 0xF8) {
        clock_out_count++;
        return;
    }
    if (midi_log)
        fprintf(midi_log, "%u ms: %02X %02X %02X\n", now_ms, (pkt >> 8) & 0xFF, (pkt >> 16) & 0xFF, (pkt >> 24) & 0xFF);
}

/* flash: two copies per object (the current and the one before, as storage.c keeps), kept across a
 * simulated reboot */
static uint8_t store[OBJ_NOBJ][PLAT_STORE_MAX], store_prev[OBJ_NOBJ][PLAT_STORE_MAX];
static int store_len[OBJ_NOBJ] = {[0 ... OBJ_NOBJ - 1] = -1}, store_prev_len[OBJ_NOBJ] = {[0 ... OBJ_NOBJ - 1] = -1};
static uint32_t store_writes;
int plat_store_load(uint32_t obj, void *dst, uint32_t max)
{
    int n;
    if (obj >= OBJ_NOBJ || (n = store_len[obj]) < 0)
        return -1;
    if ((uint32_t)n > max)
        n = (int)max;
    memcpy(dst, store[obj], (size_t)n);
    return n;
}
int plat_store_load_prev(uint32_t obj, void *dst, uint32_t max)
{
    int n;
    if (obj >= OBJ_NOBJ || (n = store_prev_len[obj]) < 0)
        return -1;
    if ((uint32_t)n > max)
        n = (int)max;
    memcpy(dst, store_prev[obj], (size_t)n);
    return n;
}
int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    if (obj >= OBJ_NOBJ || len > PLAT_STORE_MAX)
        return -1;
    if (store_len[obj] >= 0) {
        memcpy(store_prev[obj], store[obj], (size_t)store_len[obj]);
        store_prev_len[obj] = store_len[obj];
    }
    memcpy(store[obj], src, len);
    store_len[obj] = (int)len;
    store_writes++;
    return 0;
}

static uint32_t cpu_pct, cpu_peak;
static int load_force = -1;             /* script "load N": the guard sees N %, -1 measured */
uint32_t plat_cpu_pct(void) { return cpu_pct; }
uint32_t plat_cpu_peak_pct(void)
{
    uint32_t p = cpu_peak;
    cpu_peak = 0;
    return p;
}
uint32_t plat_xruns(void) { return 0; }
uint32_t plat_cycles(void)              /* the profiler's clock: nanoseconds / 10 here */
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)((uint64_t)t.tv_sec * 100000000ull + (uint64_t)t.tv_nsec / 10u);
}
/* the second core, simulated: a job runs at once, in order (the split path, the same sums) */
static int cpu2_on = 1;
int plat_cpu2_set(int on) { cpu2_on = on; return 0; }
int plat_cpu2_on(void) { return cpu2_on; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (!cpu2_on)
        return 0;
    fn(arg);
    return 1;
}
int plat_cpu2_wait(void) { return 0; }
int plat_cpu2_gave_up(void) { return 0; }
uint32_t plat_cpu2_bad(void) { return 0; }
int plat_cpu2_barred(void) { return 0; }
int plat_vdc14_mv(void) { return 1600; }
int plat_sysvdd_mv(void) { return 1380; }
int plat_last_crash(uint32_t v[5]) { (void)v; return 0; }

/* -------------------------------------------------------------- app --- */
#include "../firmware/src/app/app.h"
#include "../firmware/src/app/engine.h"
#define ANN_NAME "XY-1 " XY_VERSION
#include "ann_words.h"                                  /* the words file's link (tools/gen_words.py) */
#include "../firmware/src/announce.c"
/* what the reader is told: every SAY to stdout with "said:" (listen), and kept for the "said" check */
static char said_last[ANN_MAX + 1], said_log[16384];   /* said_log: every SAY since the last "said" check */
static uint32_t said_len;
static int said_print = 1, sim_listen;
static FILE *said_log_f;                                /* XY_SAID_LOG=FILE: every SAY, for fm1-reader's audit */
#ifdef XY_WEB
static void web_said(const uint8_t *p, uint32_t n);
#endif
static int ann_wire_send(const uint8_t *p, uint32_t n)
{
#ifdef XY_WEB
    web_said(p, n);
#endif
    if (n >= 7u && p[4] == ANN_SAY) {
        uint32_t i, k = 0;
        for (i = 6; i + 1u < n && k < ANN_MAX; i++)
            said_last[k++] = (char)p[i];
        said_last[k] = 0;
        if (said_len + k + 2u < sizeof said_log) {
            memcpy(said_log + said_len, said_last, k);
            said_len += k;
            said_log[said_len++] = '\n';
            said_log[said_len] = 0;
        }
#ifndef XY_WEB
        if (said_print)
            printf("said%s: %s\n", (p[5] & ANN_NOW) ? "!" : "", said_last);
        if (said_log_f)
            fprintf(said_log_f, "%s\t%s\n", (p[5] & ANN_KEEP) ? "WARN" : "MSG", said_last);
#endif
    }
    return 0;
}
#include "../firmware/src/app/engine.c"
#include "../firmware/src/app/project.c"
#include "../firmware/src/app/pages.c"
#include "../firmware/src/app/ui.c"
const char *ui_page_name(void);                          /* ui.c: the page shown, for "expect page" */

/* ---------------------------------------------------------- outputs --- */
static void png_write(const char *path)
{
    /* RGB PNG with stored (uncompressed) deflate blocks: no zlib needed */
    static uint8_t raw[240 * (1 + 240 * 3)];
    FILE *f = fopen(path, "wb");
    uint32_t crc_t[256], i, j, k, n = 0, a = 1, b = 0, pos, len = sizeof raw;
    uint8_t hdr[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    for (i = 0; i < 256; i++) {
        uint32_t c = i;
        for (k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_t[i] = c;
    }
    for (j = 0; j < 240; j++) {
        raw[n++] = 0;
        for (i = 0; i < 240; i++) {
            uint16_t p = fb[j * 240 + i];
            p = (uint16_t)((p >> 8) | (p << 8));
            raw[n++] = (uint8_t)(((p >> 11) & 31) * 255 / 31);
            raw[n++] = (uint8_t)(((p >> 5) & 63) * 255 / 63);
            raw[n++] = (uint8_t)((p & 31) * 255 / 31);
        }
    }
    for (i = 0; i < len; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
#define PUT32(v) do { uint8_t q_[4] = {(uint8_t)((v) >> 24), (uint8_t)((v) >> 16), (uint8_t)((v) >> 8), (uint8_t)(v)}; fwrite(q_, 1, 4, f); } while (0)
    fwrite(hdr, 1, 8, f);
    {
        uint8_t ih[17] = {'I', 'H', 'D', 'R', 0, 0, 0, 240, 0, 0, 0, 240, 8, 2, 0, 0, 0};
        uint32_t c = 0xFFFFFFFFu;
        PUT32(13u);
        fwrite(ih, 1, 17, f);
        for (i = 0; i < 17; i++)
            c = crc_t[(c ^ ih[i]) & 255] ^ (c >> 8);
        PUT32(~c);
    }
    {
        uint32_t nblk = (len + 65534) / 65535, zlen = 2 + len + nblk * 5 + 4, c = 0xFFFFFFFFu;
        uint8_t *z = malloc(zlen + 4), *q = z;
        memcpy(q, "IDAT", 4);
        q += 4;
        *q++ = 0x78;
        *q++ = 0x01;
        for (pos = 0; pos < len;) {
            uint32_t m = len - pos > 65535 ? 65535 : len - pos;
            *q++ = pos + m >= len ? 1 : 0;
            *q++ = (uint8_t)m;
            *q++ = (uint8_t)(m >> 8);
            *q++ = (uint8_t)~m;
            *q++ = (uint8_t)(~m >> 8);
            memcpy(q, raw + pos, m);
            q += m;
            pos += m;
        }
        *q++ = (uint8_t)(b >> 8);
        *q++ = (uint8_t)b;
        *q++ = (uint8_t)(a >> 8);
        *q++ = (uint8_t)a;
        PUT32(zlen);
        fwrite(z, 1, zlen + 4, f);
        for (i = 0; i < zlen + 4; i++)
            c = crc_t[(c ^ z[i]) & 255] ^ (c >> 8);
        PUT32(~c);
        free(z);
    }
    {
        uint8_t ie[4] = {'I', 'E', 'N', 'D'};
        uint32_t c = 0xFFFFFFFFu;
        PUT32(0u);
        fwrite(ie, 1, 4, f);
        for (i = 0; i < 4; i++)
            c = crc_t[(c ^ ie[i]) & 255] ^ (c >> 8);
        PUT32(~c);
    }
    fclose(f);
}

static FILE *wav;
static uint32_t wav_frames;
static float peak_out;
static void wav_open(const char *path)
{
    static const uint8_t h[44] = {0};
    wav = fopen(path, "wb");
    if (wav)
        fwrite(h, 1, 44, wav);
    wav_frames = 0;
}
static void wav_close(void)
{
    uint32_t bytes = wav_frames * 4, v;
    if (!wav)
        return;
    fseek(wav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, wav);
    v = 36 + bytes;
    fwrite(&v, 4, 1, wav);
    fwrite("WAVEfmt ", 1, 8, wav);
    v = 16;
    fwrite(&v, 4, 1, wav);
    {
        uint16_t fmt[2] = {1, 2};
        uint32_t r[2] = {44100, 44100 * 4};
        uint16_t al[2] = {4, 16};
        fwrite(fmt, 2, 2, wav);
        fwrite(r, 4, 2, wav);
        fwrite(al, 2, 2, wav);
    }
    fwrite("data", 1, 4, wav);
    fwrite(&bytes, 4, 1, wav);
    fclose(wav);
    wav = 0;
}

/* --------------------------------------------------------- the device --- */
static double audio_due_ms, render_ns_total, render_budget_ns_total;
static uint32_t ui_due;
static double now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}

static void boot(void)
{
    project_load(0);
    presets_load();
    engine_init(&pats, proj_par);
    project_apply();
    engine_set_core2(plat_cpu2_run, plat_cpu2_wait);
    lcd_fill(0, 0, 240, 240, 0);
    ui_init();
}

static void run_ms(uint32_t ms)
{
    static int32_t blk[512];
    uint32_t i, k;
    for (i = 0; i < ms; i++) {
        now_ms++;
        while (audio_due_ms <= (double)now_ms) {      /* 256-frame renders, as the device makes them (two a half) */
            double t0 = now_ns(), dt;
            engine_render(blk, 256, master);
#ifdef XY_WEB
            web_audio(blk, 256);
#endif
            dt = now_ns() - t0;
            render_ns_total += dt;
            render_budget_ns_total += 256.0 * 1e9 / 44100.0;
            cpu_pct = (uint32_t)(100.0 * dt / (256.0 * 1e9 / 44100.0));
            if (cpu_pct > cpu_peak)
                cpu_peak = cpu_pct;
#ifdef XY_WEB
            engine_load(load_force >= 0 ? (uint32_t)load_force : 0u);   /* a browser's timing is not the FM-1's */
#else
            engine_load(load_force >= 0 ? (uint32_t)load_force : cpu_pct);
#endif
            for (k = 0; k < 256; k++) {
                float l = (float)blk[2 * k] / 8388608.0f, r = (float)blk[2 * k + 1] / 8388608.0f;
                int16_t s[2];
                if (fabsf(l) > peak_out)
                    peak_out = fabsf(l);
                if (fabsf(r) > peak_out)
                    peak_out = fabsf(r);
                s[0] = (int16_t)(l * 32767.0f);
                s[1] = (int16_t)(r * 32767.0f);
                if (wav) {
                    fwrite(s, 2, 2, wav);
                    wav_frames++;
                }
            }
            audio_due_ms += 256.0 * 1000.0 / 44100.0;
        }
        if (sim_listen && now_ms % 2000u == 0) {        /* the reader's HELLO, as it sends it */
            static const uint8_t HELLO[] = {ANN_H0, ANN_H1, ANN_H2, ANN_HELLO, ANN_VER};
            ann_frame(HELLO, sizeof HELLO, now_ms);
        }
        if (now_ms >= ui_due) {
            ui_due = now_ms + 16;
            ui_frame();
        } else {
            ui_input_only();
        }
    }
}

/* ---------------------------------------------------------- script --- */
static const char *const BTN_N[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                      "PLAY", "REC", "OCT-", "OCT+"};
static const char *const ENC_N[NE] = {"SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"};
/* the FM-1's keys, F3..G5: the white and the black ones by number */
static const uint8_t HOST_WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t HOST_BLACK[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};

static int find_name(const char *const *t, int n, const char *s)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(t[i], s))
            return i;
    return -1;
}

static int key_of(const char *s)
{
    if (s[0] == 'w')
        return HOST_WHITE[atoi(s + 1) & 15];
    if (s[0] == 'b')
        return HOST_BLACK[atoi(s + 1) % 11];
    return atoi(s);
}

static tpat_t *pat_of(int t, int p)             /* track T, pattern P (1-based; P 0 = the one playing) */
{
    if (t < 1 || t > NSEQT)
        return 0;
    if (p == 0)
        p = seq.ppat[t - 1] + 1;
    if (p < 1 || p > NPATT)
        return 0;
    return &pats.pat[t - 1][p - 1];
}

static void steps_print(int t, int p)
{
    tpat_t *tp = pat_of(t, p);
    int s, k;
    if (!tp) {
        printf("steps: no track %d pattern %d\n", t, p);
        return;
    }
    printf("  track %d pattern %d: %d steps, scale %d, nlen %d\n", t, p, tp->len, tp->scale, tp->nlen);
    for (s = 0; s < tp->len; s++) {
        const step_t *st = &tp->st[s];
        if (step_empty(st) && !st->comp && !st->cond)
            continue;
        printf("    %2d:", s + 1);
        for (k = 0; k < SN; k++)
            if (st->note[k])
                printf(" %d", st->note[k]);
        printf("  vel %d len %d nudge %d comp %d/%d cond %d\n", st->vel, st->len, st->nudge, st->comp, st->cval,
               st->cond);
    }
}

static int expect(const char *what, const char *val, const char *b, const char *c)
{
    int got;
    if (!strcmp(what, "playing"))
        got = seq.playing;
    else if (!strcmp(what, "step"))                 /* expect step T VALUE: the playhead of track T (0-based) */
        got = eng_step[(atoi(val) - 1) % NSEQT], val = b;
    else if (!strcmp(what, "scene"))
        got = seq.scene + 1;
    else if (!strcmp(what, "voices"))
        got = eng_voices;
    else if (!strcmp(what, "guard"))
        got = eng_voices_max;
    else if (!strcmp(what, "cores"))
        got = engine_core2_state();
    else if (!strcmp(what, "dirty"))
        got = ui_dirty();
    else if (!strcmp(what, "store_writes"))
        got = (int)store_writes;
    else if (!strcmp(what, "clocks_out"))
        got = (int)clock_out_count;
    else if (!strcmp(what, "midi_out"))
        got = (int)midi_out_count;
    else if (!strcmp(what, "tempo"))
        got = (int)(seq_tempo(&seq) + 0.5f);
    else if (!strcmp(what, "trk"))
        got = proj.trk_sel + 1;
    else if (!strcmp(what, "page")) {               /* expect page NAME */
        const char *p = ui_page_name();
        int bad = strcmp(p, val) != 0;
        printf("%s page \"%s\"%s%s\n", bad ? "FAIL" : "  ok", p, bad ? ", expected " : "", bad ? val : "");
        return bad;
    } else if (!strcmp(what, "comp")) {             /* expect comp T S VALUE: step S's component on the playing pattern */
        tpat_t *tp = pat_of(atoi(val), 0);
        int s = atoi(b) - 1;
        if (!tp || s < 0 || s >= NSTEP) {
            printf("FAIL expect comp: no track %s step %s\n", val, b);
            return 1;
        }
        got = tp->st[s].comp;
        val = c;
    } else if (!strncmp(what, "par", 3)) {          /* parT.P: parameter P of track T */
        int t = atoi(what + 3) - 1, p = strchr(what, '.') ? atoi(strchr(what, '.') + 1) : 0;
        if (t < 0 || t >= NALLTRK || p < 0 || p >= NTP) {
            printf("FAIL unknown expect %s\n", what);
            return 1;
        }
        got = proj.trk[t].par[p];
    } else if (!strcmp(what, "peak_db_max") || !strcmp(what, "peak_db_min")) {
        double db = 20.0 * log10((double)(peak_out > 1e-9f ? peak_out : 1e-9f));
        int bad = what[8] == 'm' && what[9] == 'a' ? db > atof(val) : db < atof(val);
        printf("%s peak %.1f dBFS (%s %s)\n", bad ? "FAIL" : "  ok", db, what + 8, val);
        return bad;
    } else {
        printf("FAIL unknown expect %s\n", what);
        return 1;
    }
    if (val[0] == '<' || val[0] == '>') {
        int lim = (int)strtol(val + 1, 0, 0);
        if (val[0] == '<' ? got >= lim : got <= lim) {
            printf("FAIL expect %s %s, got %d\n", what, val, got);
            return 1;
        }
        printf("  ok %s == %d (%s)\n", what, got, val);
        return 0;
    }
    if (got != (int)strtol(val, 0, 0)) {
        printf("FAIL expect %s == %s, got %d\n", what, val, got);
        return 1;
    }
    printf("  ok %s == %d\n", what, got);
    return 0;
}

#ifndef XY_WEB
int main(int argc, char **argv)
{
    FILE *sc;
    char line[512], out[400];
    const char *dir = argc > 2 ? argv[2] : ".";
    int lineno = 0, fails = 0;
    if (argc < 2) {
        fprintf(stderr, "usage: xy_host SCRIPT [OUTDIR]\n");
        return 2;
    }
    sc = fopen(argv[1], "r");
    if (!sc) {
        perror(argv[1]);
        return 2;
    }
    snprintf(out, sizeof out, "%s/midi_out.txt", dir);
    midi_log = fopen(out, "w");
    if (getenv("XY_SAID_LOG")) {                         /* a reader listens from the start, quietly: the log only */
        said_log_f = fopen(getenv("XY_SAID_LOG"), "a");
        sim_listen = 1;
        said_print = 0;
    }
    boot();
    if (sim_listen) {
        static const uint8_t HELLO[] = {ANN_H0, ANN_H1, ANN_H2, ANN_HELLO, ANN_VER};
        ann_frame(HELLO, sizeof HELLO, now_ms);
    }
    while (fgets(line, sizeof line, sc)) {
        char cmd[32] = {0}, a[256] = {0}, b[64] = {0}, c[64] = {0}, d[64] = {0}, e[64] = {0};
        lineno++;
        if (line[0] == '#' || sscanf(line, "%31s %255s %63s %63s %63s %63s", cmd, a, b, c, d, e) < 1)
            continue;
        if (!strcmp(cmd, "wait"))
            run_ms((uint32_t)atoi(a));
        else if (!strcmp(cmd, "press") || !strcmp(cmd, "release") || !strcmp(cmd, "tap")) {
            int i = find_name(BTN_N, NB, a);
            if (i < 0) {
                printf("line %d: no button %s\n", lineno, a);
                return 2;
            }
            if (cmd[0] != 'r')
                held_btn |= 1u << i;
            if (cmd[0] == 't')
                run_ms(40);
            if (cmd[0] != 'p')
                held_btn &= ~(1u << i);
            run_ms(cmd[0] == 't' ? 40 : 2);
        } else if (!strcmp(cmd, "key") || !strcmp(cmd, "tapkey")) {
            int k = key_of(a);
            if (cmd[0] == 't') {
                held_keys |= 1u << k;
                run_ms(60);
                held_keys &= ~(1u << k);
                run_ms(30);
            } else {
                if (!strcmp(b, "down"))
                    held_keys |= 1u << k;
                else
                    held_keys &= ~(1u << k);
                run_ms(2);
            }
        } else if (!strcmp(cmd, "strum")) {          /* strum A B [MS]: a finger across the white keys */
            int x = atoi(a), y = atoi(b), gap = c[0] ? atoi(c) : 25, s = y >= x ? 1 : -1;
            for (;; x += s) {
                held_keys |= 1u << HOST_WHITE[x & 15];
                run_ms((uint32_t)gap);
                held_keys &= ~(1u << HOST_WHITE[x & 15]);
                if (x == y)
                    break;
            }
            run_ms(2);
        } else if (!strcmp(cmd, "turn")) {
            int en = find_name(ENC_N, NE, a), n = atoi(b), s = n > 0 ? 1 : -1;
            uint32_t gap = c[0] ? (uint32_t)atoi(c) : 80u;
            if (en < 0) {
                printf("line %d: no encoder %s\n", lineno, a);
                return 2;
            }
            for (; n; n -= s) {
                enc_acc[en] += s;
                run_ms(gap);
            }
        } else if (!strcmp(cmd, "spin")) {
            int en = find_name(ENC_N, NE, a);
            if (en < 0) {
                printf("line %d: no encoder %s\n", lineno, a);
                return 2;
            }
            enc_acc[en] += atoi(b);
            accel_off = 1;                           /* a test's exact values, not a hand's speed (ui.c) */
            run_ms(1);
            accel_off = 0;
        } else if (!strcmp(cmd, "master"))
            master = (uint32_t)atoi(a);
        else if (!strcmp(cmd, "midi")) {
            uint32_t s = (uint32_t)strtoul(a, 0, 16), d1 = (uint32_t)strtoul(b, 0, 16), d2 = (uint32_t)strtoul(c, 0, 16);
            uint32_t cin = s >= 0xF0 ? 0x0F : s >> 4;
            min_q[mi_w++ % MQ] = cin | s << 8 | d1 << 16 | d2 << 24;
        } else if (!strcmp(cmd, "param")) {          /* param T P V */
            int t = atoi(a) - 1, p = atoi(b), v = atoi(c);
            if (t < 0 || t >= NALLTRK || p < 0 || p >= NTP) {
                printf("line %d: no param %s %s\n", lineno, a, b);
                return 2;
            }
            proj.trk[t].par[p] = (uint8_t)v;
            engine_set(t, p, v);
            run_ms(1);
        } else if (!strcmp(cmd, "load"))
            load_force = atoi(a);
        else if (!strcmp(cmd, "steps"))
            steps_print(atoi(a), atoi(b));
        else if (!strcmp(cmd, "step")) {             /* step T PAT S NOTE VEL */
            tpat_t *tp = pat_of(atoi(a), atoi(b));
            int s = atoi(c) - 1, note = atoi(d), vel = e[0] ? atoi(e) : 100;
            if (!tp || s < 0 || s >= NSTEP) {
                printf("line %d: no step %s %s %s\n", lineno, a, b, c);
                return 2;
            }
            if (note <= 0) {
                memset(&tp->st[s], 0, sizeof tp->st[s]);
            } else {
                uint8_t n1 = (uint8_t)note;
                step_set_notes(&tp->st[s], &n1, 1, vel);
            }
        } else if (!strcmp(cmd, "scene"))
            seq_set_scene(&seq, (atoi(a) - 1) & (NSCENE - 1));
        else if (!strcmp(cmd, "bpm"))
            seq.bpm = (float)atof(a);
        else if (!strcmp(cmd, "cores"))
            plat_cpu2_set(atoi(a));
        else if (!strcmp(cmd, "wav")) {
            snprintf(out, sizeof out, "%s/%s", dir, a);
            wav_open(out);
        } else if (!strcmp(cmd, "wavstop"))
            wav_close();
        else if (!strcmp(cmd, "peakreset"))
            peak_out = 0.0f;
        else if (!strcmp(cmd, "shot")) {
            snprintf(out, sizeof out, "%s/%s", dir, a);
            png_write(out);
        } else if (!strcmp(cmd, "leds")) {
            int i;
            printf("  leds: buttons");
            for (i = 0; i < NB; i++)
                if (lit_btn >> i & 1u)
                    printf(" %s", BTN_N[i]);
            printf(" | white ");
            for (i = 0; i < 16; i++)
                printf("%c", (lit_keys >> HOST_WHITE[i] & 1u) ? '#' : '.');
            printf(" black ");
            for (i = 0; i < 11; i++)
                printf("%c", (lit_keys >> HOST_BLACK[i] & 1u) ? '#' : '.');
            printf("\n");
        } else if (!strcmp(cmd, "expect")) {
            if (!strcmp(a, "note")) {                /* expect note T PAT S VALUE: five words */
                tpat_t *tp = pat_of(atoi(b), atoi(c));
                int s = atoi(d) - 1, got = tp && s >= 0 && s < NSTEP ? tp->st[s].note[0] : -1;
                if (got != atoi(e)) {
                    printf("FAIL expect note %s %s %s == %s, got %d\n", b, c, d, e, got);
                    fails++;
                } else
                    printf("  ok note %s %s %s == %d\n", b, c, d, got);
            } else
                fails += expect(a, b, c, d);
        } else if (!strcmp(cmd, "reboot")) {
            memset(&proj, 0, sizeof proj);
            boot();
        } else if (!strcmp(cmd, "listen")) {
            static const uint8_t HELLO[] = {ANN_H0, ANN_H1, ANN_H2, ANN_HELLO, ANN_VER};
            sim_listen = 1;
            said_len = 0;
            said_log[0] = 0;
            ann_frame(HELLO, sizeof HELLO, now_ms);
            run_ms(40);
        } else if (!strcmp(cmd, "speakdump")) {        /* every screen's text for a screen reader, to stdout */
            speak_dump();
        } else if (!strcmp(cmd, "where")) {
            static const uint8_t WHERE[] = {ANN_H0, ANN_H1, ANN_H2, ANN_WHERE};
            ann_frame(WHERE, sizeof WHERE, now_ms);
            run_ms(40);
        } else if (!strcmp(cmd, "said")) {
            char *t = line + 4, *en;
            while (*t == ' ')
                t++;
            en = t + strlen(t);
            while (en > t && (en[-1] == '\n' || en[-1] == '\r' || en[-1] == ' '))
                *--en = 0;
            if (!strstr(said_log, t)) {
                printf("FAIL line %d: said \"%s\": not said (since the last check: %s)\n", lineno, t,
                       said_len ? said_log : "nothing\n");
                fails++;
            } else {
                printf("  ok said \"%s\"\n", t);
            }
            said_len = 0;
            said_log[0] = 0;
        } else if (!strcmp(cmd, "echo"))
            printf("%s", line + 5);
        else {
            printf("line %d: unknown command %s\n", lineno, cmd);
            return 2;
        }
    }
    wav_close();
    if (midi_log)
        fclose(midi_log);
    printf("host: %u ms simulated, %u blits, render %.1f%% of real time (host CPU)%s\n", now_ms, blits,
           render_budget_ns_total > 0 ? 100.0 * render_ns_total / render_budget_ns_total : 0.0,
           fails ? ", EXPECTATIONS FAILED" : "");
    return fails ? 1 : 0;
}
#endif /* XY_WEB */
