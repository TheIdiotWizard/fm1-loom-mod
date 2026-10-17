/* SPDX-License-Identifier: GPL-3.0-only */
/* The effects (dsp/fx.c) on their own: every send effect takes an impulse and a noise burst without
 * NaN and stays bounded, at its defaults and at mid knobs; the delay's first echo lands on the beat
 * at 120 BPM (time 1/4 = 0.5 s, dotted 1/8 = 0.375 s) and ping-pongs; the delay and the reverb return
 * only the wet (plain send / return); the delay's tone brightens its repeats, the reverb's damp darkens
 * its tail; the reverb decays; the distortion's edge hardens the clip; the lofi's width spreads the
 * sides; the tape loops the segment that was asked for, its dry
 * gain follows mix, pitch +1 octave and speed 200 % double its rate; the master is bit-transparent at
 * EQ blend 0 (apart from the limiter's 64-sample lookahead), its limiter never passes 1.0, its
 * compressor pulls loud input down, its EQ moves the bands; and the cost of each in instructions a
 * sample (macOS: proc_pid_rusage). Each effect's burst at its defaults is written to
 * build/host/fx_<name>.wav for inspection (nothing here plays audio).
 *   tests/host/fx_test.sh */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "fx.h"
#include "fastmath.h"
#ifdef __APPLE__
#include <unistd.h>
#include <libproc.h>
#include <sys/resource.h>
static uint64_t instr_now(void)
{
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
    return 0;
}
#else
static uint64_t instr_now(void) { return 0; }
#endif

#define SR 44100
#define BLK 64
static int bad;
static void check(const char *what, int ok)
{
    printf("%-64s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static int16_t dly_l[FX_DLY_LEN], dly_r[FX_DLY_LEN], rv[FX_RV_LEN], tape_buf[TAPE_LEN];
static uint8_t fxb[FXSLOT_BYTES] __attribute__((aligned(8)));
static uint8_t tpb[TAPE_BYTES] __attribute__((aligned(8)));
static uint8_t msb[MASTER_BYTES] __attribute__((aligned(8)));
static fxslot_t *fx = (fxslot_t *)fxb;
static tape_t *tp = (tape_t *)tpb;
static master_t *ms = (master_t *)msb;

static uint32_t rng = 12345;
static float noise(void)
{
    rng = rng * 1664525u + 1013904223u;
    return (float)(int32_t)rng * (1.0f / 2147483648.0f);
}

static void wav_open(FILE **f, const char *name)
{
    static const uint8_t hdr[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,
        0x44,0xAC,0,0,0x10,0xB1,2,0,4,0,16,0,'d','a','t','a',0,0,0,0};
    *f = fopen(name, "wb");
    if (*f)
        fwrite(hdr, 1, 44, *f);
}
static void wav_put(FILE *f, const float *l, const float *r, int n)
{
    int i;
    for (i = 0; f && i < n; i++) {
        int16_t s[2];
        s[0] = (int16_t)fm_clampf(l[i] * 32767.0f, -32767.0f, 32767.0f);
        s[1] = (int16_t)fm_clampf(r[i] * 32767.0f, -32767.0f, 32767.0f);
        fwrite(s, 2, 2, f);
    }
}
static void wav_close(FILE *f)
{
    uint32_t len;
    if (!f)
        return;
    len = (uint32_t)ftell(f);
    fseek(f, 4, SEEK_SET);
    { uint32_t v = len - 8; fwrite(&v, 4, 1, f); }
    fseek(f, 40, SEEK_SET);
    { uint32_t v = len - 44; fwrite(&v, 4, 1, f); }
    fclose(f);
}

/* run an effect on an impulse then 0.2 s of noise at 0.5, 3 s in all: peak, NaN, the RMS of the
 * second after the noise and of the last second, where the left's and right's peaks fall */
typedef struct { float peak, rms, rms_after, rms_end; int nan; int peak_l_at, peak_r_at; } run_t;
static void run_fx(int type, const uint8_t knob[4], run_t *r, const char *wav)
{
    static float in[BLK], ol[BLK], orr[BLK];
    int b, i, nb = 3 * SR / BLK;
    float pl = 0.0f, pr = 0.0f;
    FILE *w = 0;
    memset(r, 0, sizeof *r);
    fx_init(fx, dly_l, dly_r, rv);
    fx_tempo(fx, 120.0f);
    fx_set(fx, type, knob);
    if (wav)
        wav_open(&w, wav);
    for (b = 0; b < nb; b++) {
        int t0 = b * BLK;
        for (i = 0; i < BLK; i++) {
            int t = t0 + i;
            in[i] = t == 0 ? 1.0f : (t >= 2205 && t < 2205 + SR / 5) ? 0.5f * noise() : 0.0f;
        }
        memset(ol, 0, sizeof ol);
        memset(orr, 0, sizeof orr);
        fx_process(fx, in, ol, orr, BLK);
        wav_put(w, ol, orr, BLK);
        for (i = 0; i < BLK; i++) {
            int t = t0 + i;
            float al = fm_fabsf(ol[i]), ar = fm_fabsf(orr[i]);
            if (ol[i] != ol[i] || orr[i] != orr[i])
                r->nan = 1;
            r->rms += 0.5f * (ol[i] * ol[i] + orr[i] * orr[i]);
            if (al > r->peak) r->peak = al;
            if (ar > r->peak) r->peak = ar;
            if (t >= 1000 && t < 2205) {              /* before the noise: only the impulse's echoes */
                if (al > pl) { pl = al; r->peak_l_at = t; }
                if (ar > pr) { pr = ar; r->peak_r_at = t; }
            }
            if (t >= 2205 + SR / 5 && t < 2205 + SR / 5 + SR)
                r->rms_after += ol[i] * ol[i];
            if (t >= 2 * SR)
                r->rms_end += ol[i] * ol[i];
        }
    }
    wav_close(w);
    r->rms = fm_sqrtf(r->rms / (float)(nb * BLK));
    r->rms_after = fm_sqrtf(r->rms_after / (float)SR);
    r->rms_end = fm_sqrtf(r->rms_end / (float)SR);
}

static float db(float x) { return 20.0f * 0.30103f * fm_log2f(x + 1e-12f); }

/* an impulse into the slot, then n samples of L and R */
static void impulse(int type, const uint8_t knob[4], float *l, float *r, int n)
{
    static float in[BLK], ol[BLK], orr[BLK];
    int b, i;
    fx_init(fx, dly_l, dly_r, rv);
    fx_tempo(fx, 120.0f);
    fx_set(fx, type, knob);
    for (b = 0; b < n / BLK; b++) {
        memset(in, 0, sizeof in);
        if (b == 0)
            in[0] = 1.0f;
        memset(ol, 0, sizeof ol);
        memset(orr, 0, sizeof orr);
        fx_process(fx, in, ol, orr, BLK);
        for (i = 0; i < BLK; i++) {
            l[b * BLK + i] = ol[i];
            r[b * BLK + i] = orr[i];
        }
    }
}
/* brightness: the first difference's energy over the energy, in a window */
static float bright(const float *y, int from, int to)
{
    float e = 0.0f, d = 0.0f;
    int i;
    for (i = from; i < to; i++) {
        e += y[i] * y[i];
        d += (y[i] - y[i - 1]) * (y[i] - y[i - 1]);
    }
    return d / (e + 1e-20f);
}
static float imp_l[3 * SR], imp_r[3 * SR];

int main(void)
{
    static float in[BLK], ol[BLK], orr[BLK];
    int i, b, t;
    char name[96];
    run_t r;

    printf("-- every effect at its defaults on an impulse and a noise burst (peak, RMS of the 3 s, the second\n"
           "   after the burst, the last second; dBFS), written to build/host/fx_<name>.wav\n");
    for (t = 0; t < NFXT; t++) {
        snprintf(name, sizeof name, "build/host/fx_%s.wav", FXT_NAME[t]);
        run_fx(t, FXT_DEFAULT[t], &r, name);
        snprintf(name, sizeof name, "  %-10s %6.1f  %6.1f  %6.1f  %6.1f", FXT_NAME[t], (double)db(r.peak),
                 (double)db(r.rms), (double)db(r.rms_after), (double)db(r.rms_end));
        check(name, !r.nan && r.peak > 0.001f && r.peak < 4.0f);
    }
    printf("-- and at mid knobs (64)\n");
    for (t = 0; t < NFXT; t++) {
        static const uint8_t mid[4] = {64, 64, 64, 64};
        run_fx(t, mid, &r, 0);
        snprintf(name, sizeof name, "  %-10s %6.1f  %6.1f  %6.1f  %6.1f", FXT_NAME[t], (double)db(r.peak),
                 (double)db(r.rms), (double)db(r.rms_after), (double)db(r.rms_end));
        check(name, !r.nan && r.peak > 0.001f && r.peak < 4.0f);
    }
    {   /* the defaults and the knobs' names */
        static const uint8_t want[NFXT][4] = {{50, 86, 20, 127}, {72, 64, 56, 64}, {40, 32, 40, 100},
                                              {80, 80, 64, 40},  {50, 90, 50, 80}, {100, 40, 44, 72}};
        static const char *const names[NFXT][4] = {
            {"rate", "depth", "feedback", "width"},  {"time", "fine", "repeats", "tone"},
            {"drive", "edge", "low cut", "high cut"}, {"rate", "bits", "smooth", "width"},
            {"freq", "depth", "rate", "feedback"},    {"size", "motion", "tone", "damp"}};
        int ok = 1, k;
        for (t = 0; t < NFXT; t++)
            for (k = 0; k < 4; k++)
                ok &= FXT_DEFAULT[t][k] == want[t][k];
        check("FXT_DEFAULT: chorus 50 86 20 127, delay 72 64 56 64, ... reverb 100 40 44 72", ok);
        ok = !strcmp(MS_NAME[MS_SAT_BLEND], "mix");
        for (t = 0; t < NFXT; t++)
            for (k = 0; k < 4; k++)
                ok &= !strcmp(FXT_KNOB[t][k], names[t][k]);
        check("knobs: delay time fine repeats tone; reverb size motion tone damp; ...", ok);
        ok = 1;
        for (t = 0; t < NFXT; t++)                       /* the strip fits a name in 46 pixels: 8 letters */
            for (k = 0; k < 4; k++)
                ok &= strlen(FXT_KNOB[t][k]) <= 8;
        check("knob names at most 8 letters", ok);
    }
    check("fx_has: every effect with its lines", fx_has(fx, FXT_REVERB) && fx_has(fx, FXT_DELAY) && fx_has(fx, FXT_LOFI));
    fx_init(fx, dly_l, dly_r, 0);
    check("fx_has: no reverb without its line", !fx_has(fx, FXT_REVERB) && fx_has(fx, FXT_CHORUS));
    {   /* the knobs at their ends, every effect: still finite and bounded */
        int ok = 1, k;
        for (t = 0; t < NFXT && ok; t++)
            for (k = 0; k < 2 && ok; k++) {
                uint8_t kn[4] = {(uint8_t)(k ? 127 : 0), (uint8_t)(k ? 127 : 0), (uint8_t)(k ? 0 : 127), (uint8_t)(k ? 127 : 0)};
                run_fx(t, kn, &r, 0);
                ok = !r.nan && r.peak < 8.0f;
                if (!ok)
                    printf("  %s knobs %d: peak %.2f nan %d\n", FXT_NAME[t], k, (double)r.peak, r.nan);
            }
        check("knobs at their ends: finite, under +18 dBFS", ok);
    }

    printf("-- delay at 120 BPM: time 1/4 = 0.5 s, dotted 1/8 = 0.375 s\n");
    {
        /* time 100: (100 * 8) >> 7 = 6, 1/4; time 85: 5, 1/8D; fine 64 (none); repeats 0.47; tone open.
         * The impulse's echoes are looked for in a run longer than run_fx's window */
        static const uint8_t kn[2][4] = {{100, 64, 64, 127}, {85, 64, 64, 127}};
        static const int at[2] = {22050, 16538};
        int s;
        for (s = 0; s < 2; s++) {
            float pl = 0.0f, pr = 0.0f;
            int at_l = 0, at_r = 0;
            fx_init(fx, dly_l, dly_r, rv);
            fx_tempo(fx, 120.0f);
            fx_set(fx, FXT_DELAY, kn[s]);
            for (b = 0; b < 50000 / BLK; b++) {
                memset(in, 0, sizeof in);
                if (b == 0)
                    in[0] = 1.0f;
                memset(ol, 0, sizeof ol);
                memset(orr, 0, sizeof orr);
                fx_process(fx, in, ol, orr, BLK);
                for (i = 0; i < BLK; i++) {
                    int tt = b * BLK + i;
                    if (tt > 100 && fm_fabsf(ol[i]) > pl) { pl = fm_fabsf(ol[i]); at_l = tt; }
                    if (tt > 100 && fm_fabsf(orr[i]) > pr) { pr = fm_fabsf(orr[i]); at_r = tt; }
                }
            }
            snprintf(name, sizeof name, "  %s: first echo left at %d (%d), right at %d (%d)", s ? "1/8D" : "1/4 ",
                     at_l, at[s], at_r, 2 * at[s]);
            check(name, at_l >= at[s] - 2 && at_l <= at[s] + 3 && at_r >= 2 * at[s] - 2 && at_r <= 2 * at[s] + 4 &&
                        pl > 0.3f && pr > 0.1f);
        }
        {   /* the delay returns only its echoes: the send itself is not passed through */
            static const uint8_t kd[4] = {100, 64, 127, 127};
            impulse(FXT_DELAY, kd, imp_l, imp_r, 2048);
            check("delay: no dry in the return", imp_l[0] == 0.0f && imp_r[0] == 0.0f);
        }
        {   /* tone: the right's first echo (once through the repeats' low-pass) is brighter at 110 than at 20 */
            static const uint8_t kt[2][4] = {{72, 64, 100, 20}, {72, 64, 100, 110}};
            float br[2];
            int s2;
            for (s2 = 0; s2 < 2; s2++) {                     /* 1/8 at 120 BPM: 11025 samples; right at 22050 */
                impulse(FXT_DELAY, kt[s2], imp_l, imp_r, 24064);
                br[s2] = bright(imp_r, 22050 - 16, 22050 + 2000);
            }
            snprintf(name, sizeof name, "  tone 20 -> 110: the repeats brighter (%.4f -> %.4f)", (double)br[0], (double)br[1]);
            check(name, br[1] > br[0] * 4.0f);
        }
    }

    printf("-- reverb decays, and is silent when left alone; no dry; its damp\n");
    {
        static const uint8_t kn[4] = {64, 40, 40, 72};
        int d;
        float br[2];
        for (d = 0; d < 2; d++) {                                /* damp 0 and 127: the send never passes */
            uint8_t kd[4] = {64, 40, 64, (uint8_t)(d ? 127 : 0)};
            impulse(FXT_REVERB, kd, imp_l, imp_r, SR);           /* the combs answer ~550 samples later */
            check(d ? "  reverb damp 127: no dry in the return" : "  reverb damp 0: no dry in the return",
                  imp_l[0] == 0.0f && imp_r[0] == 0.0f);
            br[d] = bright(imp_l, 4410, SR);                     /* the tail from 0.1 s */
        }
        snprintf(name, sizeof name, "  damp 0 -> 127: the tail darker (%.4f -> %.4f)", (double)br[0], (double)br[1]);
        check(name, br[1] < br[0] * 0.5f);
        run_fx(FXT_REVERB, kn, &r, 0);
        snprintf(name, sizeof name, "  after the burst %.1f dB, two seconds on %.1f dB", (double)db(r.rms_after),
                 (double)db(r.rms_end));
        check(name, r.rms_after > 0.001f && r.rms_end < r.rms_after * 0.1f);
        for (b = 0; b < 20 * SR / BLK; b++) {                    /* 20 s of silence: it must empty */
            memset(in, 0, sizeof in);
            memset(ol, 0, sizeof ol);
            memset(orr, 0, sizeof orr);
            fx_process(fx, in, ol, orr, BLK);
        }
        {
            float pk = 0.0f;
            for (i = 0; i < BLK; i++)
                pk = fm_maxf(pk, fm_fabsf(ol[i]));
            check("reverb tail gone after 20 s", pk < 1e-5f);
        }
    }

    printf("-- the slot's filter page and a change of effect\n");
    {
        static const uint8_t kn[4] = {0, 0, 0, 127};               /* the distortion at drive 1, cuts open: near a wire */
        float lo = 0.0f, hi = 0.0f, lo2 = 0.0f, hi2 = 0.0f;
        fx_init(fx, dly_l, dly_r, rv);
        fx_set(fx, FXT_DIST, kn);
        fx_filter(fx, 0, 127);
        for (b = 0; b < 40; b++) {
            for (i = 0; i < BLK; i++)
                in[i] = 0.3f * fm_sin_turns((float)(b * BLK + i) * (60.0f / SR)) +
                        0.3f * fm_sin_turns((float)(b * BLK + i) * (8000.0f / SR));
            memset(ol, 0, sizeof ol);
            memset(orr, 0, sizeof orr);
            fx_process(fx, in, ol, orr, BLK);
        }
        fx_filter(fx, 100, 127);                                 /* a high-pass at ~1.6 kHz */
        for (b = 0; b < 60; b++) {
            for (i = 0; i < BLK; i++)
                in[i] = 0.3f * fm_sin_turns((float)(b * BLK + i) * (60.0f / SR));
            memset(ol, 0, sizeof ol);
            memset(orr, 0, sizeof orr);
            fx_process(fx, in, ol, orr, BLK);
            if (b >= 40)
                for (i = 0; i < BLK; i++)
                    lo += ol[i] * ol[i];
        }
        for (b = 0; b < 60; b++) {
            for (i = 0; i < BLK; i++)
                in[i] = 0.3f * fm_sin_turns((float)(b * BLK + i) * (8000.0f / SR));
            memset(ol, 0, sizeof ol);
            memset(orr, 0, sizeof orr);
            fx_process(fx, in, ol, orr, BLK);
            if (b >= 40)
                for (i = 0; i < BLK; i++)
                    hi += ol[i] * ol[i];
        }
        check("high-pass 100: 60 Hz under 8 kHz by 20 dB", lo < hi * 0.01f && hi > 0.01f);
        fx_filter(fx, 0, 40);                                    /* a low-pass at ~165 Hz */
        for (b = 0; b < 60; b++) {
            for (i = 0; i < BLK; i++)
                in[i] = 0.3f * fm_sin_turns((float)(b * BLK + i) * (60.0f / SR));
            memset(ol, 0, sizeof ol);
            memset(orr, 0, sizeof orr);
            fx_process(fx, in, ol, orr, BLK);
            if (b >= 40)
                for (i = 0; i < BLK; i++)
                    lo2 += ol[i] * ol[i];
        }
        for (b = 0; b < 60; b++) {
            for (i = 0; i < BLK; i++)
                in[i] = 0.3f * fm_sin_turns((float)(b * BLK + i) * (8000.0f / SR));
            memset(ol, 0, sizeof ol);
            memset(orr, 0, sizeof orr);
            fx_process(fx, in, ol, orr, BLK);
            if (b >= 40)
                for (i = 0; i < BLK; i++)
                    hi2 += ol[i] * ol[i];
        }
        check("low-pass 40: 8 kHz under 60 Hz by 20 dB", hi2 < lo2 * 0.01f && lo2 > 0.01f);
        {   /* switch to the reverb mid-stream: no NaN, no leftover */
            static const uint8_t kr[4] = {64, 64, 64, 127};
            int ok = 1;
            fx_set(fx, FXT_REVERB, kr);
            for (b = 0; b < 100; b++) {
                memset(in, 0, sizeof in);
                memset(ol, 0, sizeof ol);
                memset(orr, 0, sizeof orr);
                fx_process(fx, in, ol, orr, BLK);
                for (i = 0; i < BLK; i++)
                    ok &= ol[i] == ol[i] && fm_fabsf(ol[i]) < 0.01f;
            }
            check("a change of effect starts quiet", ok);
        }
    }

    printf("-- distortion: edge hardens the clip; lofi: width spreads the sides\n");
    {
        float e[2] = {0.0f, 0.0f}, pk[2] = {0.0f, 0.0f};
        int c;
        for (c = 0; c < 2; c++) {                                /* a 0.4 sine, drive 24: tanh, then a hard clip */
            uint8_t kn[4] = {24, (uint8_t)(c ? 127 : 0), 0, 127};
            fx_init(fx, dly_l, dly_r, rv);
            fx_set(fx, FXT_DIST, kn);
            for (b = 0; b < 60; b++) {
                for (i = 0; i < BLK; i++)
                    in[i] = 0.4f * fm_sin_turns((float)(b * BLK + i) * (200.0f / SR));
                memset(ol, 0, sizeof ol);
                memset(orr, 0, sizeof orr);
                fx_process(fx, in, ol, orr, BLK);
                if (b >= 30)
                    for (i = 0; i < BLK; i++) {
                        e[c] += ol[i] * ol[i];
                        pk[c] = fm_maxf(pk[c], fm_fabsf(ol[i]));
                    }
            }
        }
        snprintf(name, sizeof name, "  edge 127 over edge 0: %.1f dB more RMS, the same peak (%.2f, %.2f)",
                 (double)(10.0f * 0.30103f * fm_log2f(e[1] / e[0])), (double)pk[0], (double)pk[1]);
        check(name, e[1] > e[0] * 1.1f && pk[1] > pk[0] * 1.1f && pk[1] < 1.0f);
        for (c = 0; c < 2; c++) {                                /* width 0: L = R exactly; width 127: they differ */
            uint8_t kn[4] = {64, 93, 78, (uint8_t)(c ? 127 : 0)};
            float diff = 0.0f, e2 = 0.0f;
            int same = 1;
            fx_init(fx, dly_l, dly_r, rv);
            fx_set(fx, FXT_LOFI, kn);
            for (b = 0; b < 60; b++) {
                for (i = 0; i < BLK; i++)
                    in[i] = 0.3f * noise();
                memset(ol, 0, sizeof ol);
                memset(orr, 0, sizeof orr);
                fx_process(fx, in, ol, orr, BLK);
                for (i = 0; i < BLK; i++) {
                    same &= ol[i] == orr[i];
                    diff += (ol[i] - orr[i]) * (ol[i] - orr[i]);
                    e2 += ol[i] * ol[i];
                }
            }
            snprintf(name, sizeof name, "  lofi width %d: the sides %s (L-R at %.1f dB of L)", c ? 127 : 0,
                     c ? "differ" : "are the same", (double)(10.0f * 0.30103f * fm_log2f(diff / e2 + 1e-12f)));
            check(name, c ? (!same && diff > e2 * 0.5f) : same);
        }
    }

    printf("-- tape: 16 steps of markers at 120 BPM, then segments loop\n");
    {
        /* step k holds a 300 Hz sine at amplitude (k + 1) / 16: a segment is known by its level.
         * A step is a sixteenth at 120 BPM (125 ms); the ring holds 16 of them down to 90 BPM */
        const uint32_t step = 5512;
        float pk, dry = 1.0f;
        int seg;
        tape_init(tp, tape_buf);
        tape_set(tp, 64, 64, 0, 127);
        for (b = 0; b < (int)(16 * step / BLK); b++) {
            int k = (b * BLK) / (int)step;
            if ((b * BLK) % (int)step == 0)
                tape_bar(tp, step);
            for (i = 0; i < BLK; i++)
                in[i] = ((float)k + 1.0f) / 16.0f * fm_sin_turns((float)(b * BLK + i) * (300.0f / SR));
            memset(ol, 0, sizeof ol);
            memset(orr, 0, sizeof orr);
            dry = tape_process(tp, in, ol, orr, BLK);
        }
        tape_bar(tp, step);                                  /* the boundary after step 16 */
        check("recording: dry gain 1, not active", dry == 1.0f && !tape_active(tp));
        for (seg = 4; seg <= 15; seg += 11) {
            tape_segment(tp, seg, 1);
            pk = 0.0f;
            for (b = 0; b < 60; b++) {                       /* 87 ms: a segment is 125 ms */
                memset(in, 0, sizeof in);
                memset(ol, 0, sizeof ol);
                memset(orr, 0, sizeof orr);
                dry = tape_process(tp, in, ol, orr, BLK);
                if (b >= 10)
                    for (i = 0; i < BLK; i++)
                        pk = fm_maxf(pk, fm_fabsf(ol[i]));
            }
            snprintf(name, sizeof name, "  segment %d plays at %.3f (%.3f): dry %.2f", seg, (double)pk,
                     (double)((float)(seg + 1) / 16.0f), (double)dry);
            check(name, tape_active(tp) && pk > (float)(seg + 1) / 16.0f * 0.85f && pk < (float)(seg + 1) / 16.0f * 1.1f &&
                        dry < 0.01f);
            tape_segment(tp, seg, 0);
            for (b = 0; b < 10; b++) {
                memset(in, 0, sizeof in);
                memset(ol, 0, sizeof ol);
                memset(orr, 0, sizeof orr);
                dry = tape_process(tp, in, ol, orr, BLK);
            }
            check("  released: dry back to 1", dry == 1.0f && !tape_active(tp));
        }
        {   /* mix 64: half dry; length 127: all sixteen segments, the loop runs on through them */
            float pk2 = 0.0f, mn = 1.0f;
            tape_set(tp, 64, 64, 127, 64);
            tape_segment(tp, 0, 1);
            for (b = 0; b < (int)(16 * step / BLK); b++) {
                memset(in, 0, sizeof in);
                memset(ol, 0, sizeof ol);
                memset(orr, 0, sizeof orr);
                dry = tape_process(tp, in, ol, orr, BLK);
                if (b > 10) {
                    float p = 0.0f;
                    for (i = 0; i < BLK; i++)
                        p = fm_maxf(p, fm_fabsf(ol[i]));
                    if (b * BLK < (int)(2 * step)) mn = fm_minf(mn, p);
                    pk2 = fm_maxf(pk2, p);
                }
            }
            snprintf(name, sizeof name, "  mix 64: dry %.2f; length 16 runs from 1/16 (%.2f) up to 16/16 (%.2f)",
                     (double)dry, (double)mn, (double)pk2);
            check(name, dry > 0.45f && dry < 0.55f && mn < 0.1f && pk2 > 0.4f);
            tape_segment(tp, 0, 0);
        }
        {   /* pitch +1 octave (knob 96: the fourth of five zones), or speed 200 % (knob 127): the loop plays
             * twice as fast (a 300 Hz marker becomes 600 Hz: twice the zero crossings) */
            int zc[3] = {0, 0, 0}, p;
            float last;
            for (p = 0; p < 3; p++) {
                tape_set(tp, p == 1 ? 96 : 64, p == 2 ? 127 : 64, 0, 127);
                tape_segment(tp, 15, 1);
                last = 0.0f;
                for (b = 0; b < 60; b++) {
                    memset(in, 0, sizeof in);
                    memset(ol, 0, sizeof ol);
                    memset(orr, 0, sizeof orr);
                    tape_process(tp, in, ol, orr, BLK);
                    if (b >= 20)
                        for (i = 0; i < BLK; i++) {
                            if ((ol[i] > 0.0f) != (last > 0.0f))
                                zc[p] += 1;
                            last = ol[i];
                        }
                }
                tape_segment(tp, 15, 0);
                for (b = 0; b < 10; b++) {
                    memset(in, 0, sizeof in);
                    tape_process(tp, in, ol, orr, BLK);
                }
            }
            snprintf(name, sizeof name, "  pitch +1 octave doubles the rate (%d -> %d zero crossings)", zc[0], zc[1]);
            check(name, zc[1] > zc[0] * 18 / 10 && zc[1] < zc[0] * 22 / 10);
            snprintf(name, sizeof name, "  speed 200 %% doubles the rate (%d -> %d zero crossings)", zc[0], zc[2]);
            check(name, zc[2] > zc[0] * 18 / 10 && zc[2] < zc[0] * 22 / 10);
            {   /* the five pitch zones: 0..25 -2, 26..51 -1, 52..76 x1, 77..102 +1, 103..127 +2 */
                int z[3] = {0, 0, 0}, q;
                for (q = 0; q < 3; q++) {
                    tape_set(tp, q == 0 ? 25 : q == 1 ? 52 : 76, 64, 0, 127);
                    tape_segment(tp, 15, 1);
                    last = 0.0f;
                    for (b = 0; b < 60; b++) {
                        memset(in, 0, sizeof in);
                        memset(ol, 0, sizeof ol);
                        memset(orr, 0, sizeof orr);
                        tape_process(tp, in, ol, orr, BLK);
                        if (b >= 20)
                            for (i = 0; i < BLK; i++) {
                                if ((ol[i] > 0.0f) != (last > 0.0f))
                                    z[q] += 1;
                                last = ol[i];
                            }
                    }
                    tape_segment(tp, 15, 0);
                    for (b = 0; b < 10; b++) {
                        memset(in, 0, sizeof in);
                        tape_process(tp, in, ol, orr, BLK);
                    }
                }
                snprintf(name, sizeof name, "  pitch 25 is -2 octaves (%d), 52 and 76 are x1 (%d, %d)", z[0], z[1], z[2]);
                check(name, z[0] > zc[0] * 2 / 10 && z[0] < zc[0] * 3 / 10 && z[1] == zc[0] && z[2] == zc[0]);
            }
        }
    }

    printf("-- master\n");
    {
        static float pl[BLK], pr[BLK], mll[BLK], mlr[BLK], ol2[BLK], or2[BLK], hist_l[BLK * 4], hist_r[BLK * 4];
        int ok = 1, n = 0;
        master_init(ms);
        memset(hist_l, 0, sizeof hist_l);
        memset(hist_r, 0, sizeof hist_r);
        for (b = 0; b < 200; b++) {
            for (i = 0; i < BLK; i++) {
                pl[i] = 0.3f * noise(); pr[i] = 0.3f * noise(); mll[i] = 0.3f * noise(); mlr[i] = 0.3f * noise();
                hist_l[(n + i) % (BLK * 4)] = pl[i] + mll[i];
                hist_r[(n + i) % (BLK * 4)] = pr[i] + mlr[i];
            }
            master_process(ms, pl, pr, mll, mlr, ol2, or2, BLK);
            for (i = 0; i < BLK; i++) {
                int t0 = n + i - 64;                               /* the limiter looks 64 samples ahead */
                if (t0 >= 0)
                    ok &= ol2[i] == hist_l[t0 % (BLK * 4)] && or2[i] == hist_r[t0 % (BLK * 4)];
            }
            n += BLK;
        }
        check("defaults: bit-transparent (perc + mel, 64 samples late)", ok);
        /* an EQ set but blend 0: still transparent; blend 127: the bands move */
        master_set(ms, MS_EQ_LO, 127);
        master_set(ms, MS_EQ_HI, 0);
        ok = 1;
        for (b = 0; b < 50; b++) {
            for (i = 0; i < BLK; i++) {
                pl[i] = pr[i] = 0.3f * noise();
                mll[i] = mlr[i] = 0.0f;
                hist_l[(n + i) % (BLK * 4)] = pl[i];
            }
            master_process(ms, pl, pr, mll, mlr, ol2, or2, BLK);
            for (i = 0; i < BLK; i++) {
                int t0 = n + i - 64;
                if (t0 >= 0 && b > 4)
                    ok &= ol2[i] == hist_l[t0 % (BLK * 4)];
            }
            n += BLK;
        }
        check("EQ set, blend 0: still transparent", ok);
        {
            float lo0 = 0.0f, lo1 = 0.0f, hi0 = 0.0f, hi1 = 0.0f;
            int pass;
            for (pass = 0; pass < 2; pass++) {
                master_set(ms, MS_EQ_BLEND, pass ? 127 : 0);
                for (b = 0; b < 120; b++) {
                    for (i = 0; i < BLK; i++) {
                        pl[i] = pr[i] = 0.2f * fm_sin_turns((float)(b * BLK + i) * (60.0f / SR));
                        mll[i] = mlr[i] = 0.0f;
                    }
                    master_process(ms, pl, pr, mll, mlr, ol2, or2, BLK);
                    if (b >= 60)
                        for (i = 0; i < BLK; i++)
                            *(pass ? &lo1 : &lo0) += ol2[i] * ol2[i];
                }
                for (b = 0; b < 120; b++) {
                    for (i = 0; i < BLK; i++) {
                        pl[i] = pr[i] = 0.2f * fm_sin_turns((float)(b * BLK + i) * (10000.0f / SR));
                        mll[i] = mlr[i] = 0.0f;
                    }
                    master_process(ms, pl, pr, mll, mlr, ol2, or2, BLK);
                    if (b >= 60)
                        for (i = 0; i < BLK; i++)
                            *(pass ? &hi1 : &hi0) += ol2[i] * ol2[i];
                }
            }
            snprintf(name, sizeof name, "EQ blend 127: low +12 dB (%.1f dB), high -12 dB (%.1f dB)",
                     (double)(10.0f * 0.30103f * fm_log2f(lo1 / lo0)), (double)(10.0f * 0.30103f * fm_log2f(hi1 / hi0)));
            check(name, lo1 > lo0 * 10.0f && lo1 < lo0 * 20.0f && hi1 < hi0 * 0.1f && hi1 > hi0 * 0.05f);
        }
        master_init(ms);
        {   /* the limiter: a signal at 4x full scale never passes 1.0 */
            float pk = 0.0f;
            master_set(ms, MS_SAT_BLEND, 127);
            master_set(ms, MS_SAT_GAIN, 127);
            master_set(ms, MS_LEVEL, 127);
            for (b = 0; b < 400; b++) {
                for (i = 0; i < BLK; i++) {
                    pl[i] = pr[i] = 2.0f * noise();
                    mll[i] = mlr[i] = 2.0f * ((b & 8) ? 1.0f : -1.0f);
                }
                master_process(ms, pl, pr, mll, mlr, ol2, or2, BLK);
                for (i = 0; i < BLK; i++) {
                    ok &= ol2[i] == ol2[i];
                    pk = fm_maxf(pk, fm_maxf(fm_fabsf(ol2[i]), fm_fabsf(or2[i])));
                }
            }
            snprintf(name, sizeof name, "limiter + saturator on a 4x signal: peak %.3f, no NaN", (double)pk);
            check(name, ok && pk <= 1.0f);
        }
        master_init(ms);
        {   /* the compressor at 100: a loud sine is pulled down, the meter shows it */
            float g0 = 0.0f, g1 = 0.0f;
            master_set(ms, MS_COMP, 100);
            for (b = 0; b < 400; b++) {
                for (i = 0; i < BLK; i++) {
                    pl[i] = pr[i] = 0.0f;
                    mll[i] = mlr[i] = 0.5f * fm_sin_turns((float)(b * BLK + i) * (200.0f / SR));
                }
                master_process(ms, pl, pr, mll, mlr, ol2, or2, BLK);
                if (b >= 300)
                    for (i = 0; i < BLK; i++)
                        g1 += ol2[i] * ol2[i];
            }
            snprintf(name, sizeof name, "compressor 100 on a -6 dB sine: %.1f dB of reduction shown", (double)master_gr_db(ms));
            check(name, master_gr_db(ms) > 3.0f);
            master_set(ms, MS_COMP, 0);
            for (b = 0; b < 400; b++) {
                for (i = 0; i < BLK; i++) {
                    pl[i] = pr[i] = 0.0f;
                    mll[i] = mlr[i] = 0.05f * fm_sin_turns((float)(b * BLK + i) * (200.0f / SR));
                }
                master_process(ms, pl, pr, mll, mlr, ol2, or2, BLK);
                if (b >= 300)
                    for (i = 0; i < BLK; i++)
                        g0 += ol2[i] * ol2[i];
            }
            master_set(ms, MS_COMP, 100);
            g1 = 0.0f;
            for (b = 0; b < 400; b++) {
                for (i = 0; i < BLK; i++) {
                    pl[i] = pr[i] = 0.0f;
                    mll[i] = mlr[i] = 0.05f * fm_sin_turns((float)(b * BLK + i) * (200.0f / SR));
                }
                master_process(ms, pl, pr, mll, mlr, ol2, or2, BLK);
                if (b >= 300)
                    for (i = 0; i < BLK; i++)
                        g1 += ol2[i] * ol2[i];
            }
            check("compressor 100 lifts a quiet signal (makeup)", g1 > g0 * 2.0f);
        }
        {   /* group levels: perc at 0 mutes the percussion group */
            float e = 0.0f;
            master_init(ms);
            master_set(ms, MS_PERC, 0);
            for (b = 0; b < 200; b++) {
                for (i = 0; i < BLK; i++) {
                    pl[i] = pr[i] = 0.3f * noise();
                    mll[i] = mlr[i] = 0.0f;
                }
                master_process(ms, pl, pr, mll, mlr, ol2, or2, BLK);
                if (b >= 150)                                      /* the level glides ~10 ms, then lands */
                    for (i = 0; i < BLK; i++)
                        e += ol2[i] * ol2[i];
            }
            check("perc level 0 silences the percussion group (after its glide)", e == 0.0f);
        }
    }

    printf("-- cost, instructions a sample\n");
    {
        static float in2[256], a[256], c[256], p1[256], p2[256], m1[256], m2[256];
        uint64_t i0, di;
        for (t = 0; t < NFXT; t++) {
            static const uint8_t mid[4] = {64, 64, 64, 64};
            fx_init(fx, dly_l, dly_r, rv);
            fx_set(fx, t, mid);
            for (b = 0; b < 20; b++) {
                for (i = 0; i < 256; i++)
                    in2[i] = 0.3f * noise();
                fx_process(fx, in2, a, c, 256);
            }
            i0 = instr_now();
            for (b = 0; b < 20; b++)
                fx_process(fx, in2, a, c, 256);
            di = instr_now() - i0;
            printf("  %-11s %5.0f\n", FXT_NAME[t], (double)di / (20.0 * 256.0));
        }
        tape_init(tp, tape_buf);
        tape_set(tp, 70, 60, 64, 100);
        for (b = 0; b < 40; b++) {
            for (i = 0; i < 256; i++)
                in2[i] = 0.3f * noise();
            tape_process(tp, in2, a, c, 256);
        }
        i0 = instr_now();
        for (b = 0; b < 20; b++)
            tape_process(tp, in2, a, c, 256);
        di = instr_now() - i0;
        printf("  tape record %5.0f\n", (double)di / (20.0 * 256.0));
        tape_segment(tp, 3, 1);
        tape_process(tp, in2, a, c, 256);
        i0 = instr_now();
        for (b = 0; b < 20; b++)
            tape_process(tp, in2, a, c, 256);
        di = instr_now() - i0;
        printf("  tape loop   %5.0f\n", (double)di / (20.0 * 256.0));
        master_init(ms);
        for (i = 0; i < 256; i++) {
            p1[i] = p2[i] = 0.3f * noise();
            m1[i] = m2[i] = 0.3f * noise();
        }
        for (b = 0; b < 10; b++)
            master_process(ms, p1, p2, m1, m2, a, c, 256);
        i0 = instr_now();
        for (b = 0; b < 20; b++)
            master_process(ms, p1, p2, m1, m2, a, c, 256);
        di = instr_now() - i0;
        printf("  master off  %5.0f  (levels and the limiter)\n", (double)di / (20.0 * 256.0));
        master_set(ms, MS_EQ_LO, 80); master_set(ms, MS_EQ_BLEND, 127);
        master_set(ms, MS_SAT_BLEND, 64); master_set(ms, MS_SAT_GAIN, 64);
        master_set(ms, MS_COMP, 80);
        for (b = 0; b < 10; b++)
            master_process(ms, p1, p2, m1, m2, a, c, 256);
        i0 = instr_now();
        for (b = 0; b < 20; b++)
            master_process(ms, p1, p2, m1, m2, a, c, 256);
        di = instr_now() - i0;
        printf("  master all  %5.0f  (EQ, saturator, compressor, limiter)\n", (double)di / (20.0 * 256.0));
    }
    printf("%s\n", bad ? "FX TESTS FAILED" : "fx tests passed");
    return bad != 0;
}
