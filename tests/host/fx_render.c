/* SPDX-License-Identifier: GPL-3.0-only */
/* Renders for listening (nothing here plays audio): the master's tape (TapeScam-lite) on a drum loop and on a pad
 * at a few settings, and every reverb mode (the plate, the PlayStation's SPU modes) on a short dry phrase, as
 * 16-bit stereo WAVs in build/fx_renders/, with an index (index.txt) of what each file is.
 *   tools/fx_renders.sh */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "fx.h"
#include "fastmath.h"

#define SR 44100
#define BLK 64
#define LEN (6 * SR)
#define DIR "build/fx_renders/"

static int16_t dly_l[FX_DLY_LEN], dly_r[FX_DLY_LEN], rv[FX_RV_LEN];
static float mtl[MST_TAPE_DLY], mtr[MST_TAPE_DLY];
static uint8_t fxb[FXSLOT_BYTES] __attribute__((aligned(8)));
static uint8_t msb[MASTER_BYTES] __attribute__((aligned(8)));
static fxslot_t *fx = (fxslot_t *)fxb;
static master_t *ms = (master_t *)msb;
static float src_l[LEN], src_r[LEN], out_l[LEN], out_r[LEN];
static FILE *idx;

static uint32_t rng = 1;
static float noise(void)
{
    rng = rng * 1664525u + 1013904223u;
    return (float)(int32_t)rng * (1.0f / 2147483648.0f);
}

static void wav(const char *name, const float *l, const float *r, int n, const char *what)
{
    static const uint8_t hdr[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,
        0x44,0xAC,0,0,0x10,0xB1,2,0,4,0,16,0,'d','a','t','a',0,0,0,0};
    char path[160];
    FILE *f;
    uint32_t v;
    int i;
    snprintf(path, sizeof path, DIR "%s", name);
    f = fopen(path, "wb");
    if (!f)
        return;
    fwrite(hdr, 1, 44, f);
    for (i = 0; i < n; i++) {
        int16_t s[2];
        s[0] = (int16_t)fm_clampf(l[i] * 32767.0f, -32767.0f, 32767.0f);
        s[1] = (int16_t)fm_clampf(r[i] * 32767.0f, -32767.0f, 32767.0f);
        fwrite(s, 2, 2, f);
    }
    v = (uint32_t)n * 4u + 36u;
    fseek(f, 4, SEEK_SET);
    fwrite(&v, 4, 1, f);
    v = (uint32_t)n * 4u;
    fseek(f, 40, SEEK_SET);
    fwrite(&v, 4, 1, f);
    fclose(f);
    fprintf(idx, "%-34s %s\n", name, what);
}

/* a drum loop: 120 BPM, kick on the beats, a snare on 2 and 4, closed hats on the eighths (stereo-ish) */
static void drum_loop(void)
{
    int i;
    float hp = 0.0f;
    memset(src_l, 0, sizeof src_l);
    memset(src_r, 0, sizeof src_r);
    for (i = 0; i < LEN; i++) {
        int s8 = i / (SR / 4), at = i % (SR / 4), beat = s8 / 2;
        float t = (float)at / SR, k = 0.0f, sn = 0.0f, h, n = noise(), ph;
        if (s8 % 2 == 0) {                                      /* the kick: a falling sine */
            ph = 45.0f * t + 1.6f * (1.0f - fm_expf(-t * 30.0f));
            k = 0.85f * fm_sin_turns(ph) * fm_expf(-t * 9.0f);
            if (beat % 2 == 1)                                   /* the snare: a tone and noise */
                sn = (0.25f * fm_sin_turns(190.0f * t) + 0.35f * n) * fm_expf(-t * 18.0f);
        }
        hp += 0.6f * (n - hp);                                   /* the hat: noise above ~8 kHz */
        h = (n - hp) * 0.22f * fm_expf(-t * 60.0f);
        src_l[i] = k + sn + h * 0.8f;
        src_r[i] = k + sn + h * 1.0f;
    }
}

/* a pad: a minor seventh chord of detuned saws, slowly low-passed */
static void pad(void)
{
    static const float F[4] = {110.0f, 130.81f, 164.81f, 196.0f};
    float ph[4][3] = {{0}}, lp_l = 0.0f, lp_r = 0.0f;
    int i, n, v;
    for (i = 0; i < LEN; i++) {
        float t = (float)i / SR, env = fm_minf(t * 2.0f, 1.0f) * (t > 5.0f ? fm_maxf(0.0f, 1.0f - (t - 5.0f) * 2.0f) : 1.0f);
        float l = 0.0f, r = 0.0f, k = 0.02f + 0.06f * (0.5f + 0.5f * fm_sin_turns(t * 0.2f));
        for (n = 0; n < 4; n++)
            for (v = 0; v < 3; v++) {
                float s;
                ph[n][v] += F[n] * (1.0f + 0.004f * (float)(v - 1)) / SR;
                ph[n][v] -= fm_floorf(ph[n][v]);
                s = 2.0f * ph[n][v] - 1.0f;
                if (v != 2) l += s;
                if (v != 0) r += s;
            }
        lp_l += k * (l - lp_l);
        lp_r += k * (r - lp_r);
        src_l[i] = 0.09f * lp_l * env;
        src_r[i] = 0.09f * lp_r * env;
    }
}

/* a dry phrase: eight plucked notes (a saw, a decaying low-pass) in 1.5 s, then silence for the tail (mono) */
static void phrase(void)
{
    static const float F[8] = {261.63f, 329.63f, 392.0f, 523.25f, 493.88f, 392.0f, 329.63f, 293.66f};
    float ph = 0.0f, lp = 0.0f;
    int i;
    for (i = 0; i < LEN; i++) {
        int k = i / (SR * 3 / 16);
        float t = (float)(i % (SR * 3 / 16)) / SR, s = 0.0f;
        if (k < 8) {
            ph += F[k] / SR;
            ph -= fm_floorf(ph);
            lp += (0.05f + 0.4f * fm_expf(-t * 20.0f)) * ((2.0f * ph - 1.0f) - lp);
            s = 0.4f * lp * fm_expf(-t * 7.0f);
        }
        src_l[i] = src_r[i] = s;
    }
}

static void master_render(const char *name, const uint8_t tape[4], const char *what)
{
    static float pl[BLK], pr[BLK], mll[BLK], mlr[BLK];
    int b, i;
    master_init(ms, mtl, mtr);
    for (i = 0; i < 4; i++)
        master_set(ms, MS_TP_DRIVE + i, tape[i]);
    for (b = 0; b < LEN / BLK; b++) {
        for (i = 0; i < BLK; i++) {
            pl[i] = pr[i] = 0.0f;
            mll[i] = src_l[b * BLK + i];
            mlr[i] = src_r[b * BLK + i];
        }
        master_process(ms, pl, pr, mll, mlr, out_l + b * BLK, out_r + b * BLK, BLK);
    }
    wav(name, out_l, out_r, LEN, what);
}

/* the phrase dry, the reverb's return under it (the send at full, the return at 0.7: an FX track at level 84) */
static void reverb_render(const char *name, int pick, const uint8_t kn[4], const char *what)
{
    static float in[BLK], ol[BLK], orr[BLK];
    int b, i;
    fx_init(fx, dly_l, dly_r, rv);
    fx_tempo(fx, 120.0f);
    fx_set(fx, pick, kn);
    fx_filter(fx, 20, 127);                                     /* the default project's FX II filter */
    for (b = 0; b < 40; b++) {                                  /* an SPU mode clears its area first */
        memset(in, 0, sizeof in);
        fx_process(fx, in, ol, orr, BLK);
    }
    for (b = 0; b < LEN / BLK; b++) {
        for (i = 0; i < BLK; i++)
            in[i] = src_l[b * BLK + i];
        memset(ol, 0, sizeof ol);
        memset(orr, 0, sizeof orr);
        fx_process(fx, in, ol, orr, BLK);
        for (i = 0; i < BLK; i++) {
            out_l[b * BLK + i] = in[i] + 0.7f * ol[i];
            out_r[b * BLK + i] = in[i] + 0.7f * orr[i];
        }
    }
    wav(name, out_l, out_r, LEN, what);
}

int main(void)
{
    static const struct { const char *name; uint8_t k[4]; const char *what; } T[] = {
        {"off", {0, 0, 0, 0}, "the tape out (all four at 0): the bus as it was"},
        {"color24", {0, 24, 0, 0}, "color 24 (the saturation alone, trimmed; a new project's tape is off)"},
        {"color64", {0, 64, 0, 0}, "color 64"},
        {"drive40", {40, 24, 0, 0}, "drive 40, color 24: the gain stage in (its 5 kHz voicing, the lows around it)"},
        {"drive100_color60", {100, 60, 0, 0}, "drive 100, color 60: hard"},
        {"wobble60", {0, 24, 60, 0}, "color 24, wobble 60"},
        {"wobble127", {0, 24, 127, 0}, "color 24, wobble 127: seasick"},
        {"noise50", {0, 24, 0, 50}, "color 24, noise 50"},
        {"all", {50, 50, 50, 40}, "drive 50, color 50, wobble 50, noise 40"},
    };
    char name[64], what[160];
    int i, m;
    idx = fopen(DIR "index.txt", "w");
    if (!idx) {
        fprintf(stderr, "fx_render: no " DIR "\n");
        return 1;
    }
    fprintf(idx, "LOOM-8 effect renders (tests/host/fx_render.c, tools/fx_renders.sh): 44.1 kHz, 16-bit stereo, 6 s each.\n\n"
                 "The master's tape (Master tape page: drive color wobble noise), through the whole master (EQ off, comp 0,\n"
                 "level unity, the limiter) on a 120 BPM drum loop and on a pad:\n");
    drum_loop();
    for (i = 0; i < (int)(sizeof T / sizeof T[0]); i++) {
        snprintf(name, sizeof name, "tape_drums_%s.wav", T[i].name);
        master_render(name, T[i].k, T[i].what);
    }
    pad();
    for (i = 0; i < (int)(sizeof T / sizeof T[0]); i++) {
        snprintf(name, sizeof name, "tape_pad_%s.wav", T[i].name);
        master_render(name, T[i].k, T[i].what);
    }
    fprintf(idx, "\nThe reverb's modes on FX II, on a dry phrase (8 plucked notes in 1.5 s, then the tail): the dry, and the\n"
                 "return at 0.7 under it. The SPU modes at their defaults (size 127 = the console's delays, decay 64 = its own\n"
                 "feedback, tone flat, damp 0):\n");
    phrase();
    wav("reverb_dry.wav", src_l, src_r, LEN, "the phrase alone");
    for (m = 0; m < NRV; m++) {
        int pick = FXT_REVERB + m;
        snprintf(name, sizeof name, "reverb_%02d_%s.wav", m, fx_pick_name(pick));
        for (i = 0; name[i]; i++)
            if (name[i] == ' ')
                name[i] = '_';
        snprintf(what, sizeof what, "%s%s", fx_pick_name(pick), m ? "" : " (the plate, its defaults 100 40 44 72)");
        reverb_render(name, pick, m ? RV_SPU_DEFAULT : FXT_DEFAULT[FXT_REVERB], what);
    }
    {
        static const uint8_t k0[4] = {0, 64, 64, 0}, kd[4] = {127, 110, 64, 0}, kdark[4] = {127, 64, 30, 70};
        reverb_render("reverb_hall_size0.wav", FXT_REVERB + RV_HALL, k0, "hall at size 0: schwung-psxverb's scale (x1)");
        reverb_render("reverb_hall_decay110.wav", FXT_REVERB + RV_HALL, kd, "hall, decay 110: longer");
        reverb_render("reverb_space_echo_dark.wav", FXT_REVERB + RV_SPACE_ECHO, kdark, "space echo, tone 30, damp 70: darker");
    }
    fclose(idx);
    printf("renders in " DIR " (index.txt)\n");
    return 0;
}
