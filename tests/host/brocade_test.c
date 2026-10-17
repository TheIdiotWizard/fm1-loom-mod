/* SPDX-License-Identifier: GPL-3.0-only */
/* brocade, the supersaw engine (dsp/synth.c, its saw bank after NuSaw, MIT), on its own: bounded and finite at
 * every saw count, knob corner and note; the saw count changes the sound, one saw being one saw; detune 0
 * keeps every partial on the note's harmonics (the saws in unison), detune up spreads them; spread makes it
 * stereo and 0 leaves it mono; the guard's thinning (synth_saws) fades the outer saws in steps, no click, the
 * level kept; the pool keeps to brocade's voice cap (six, five at seven or eight saws, four at nine). Then the
 * cost: instructions a sample of one voice (lp24, the whole voice) by saw count, mono and stereo, against an
 * ordinary engine's, and what that makes the cores' split units (synth_voice_cost).
 *   tests/host/brocade_test.sh */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
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
static uint64_t instr_now(void) { return (uint64_t)clock(); }
#endif
#include "synth.h"
int synth_sizeof(void);

static int bad;
static void check(const char *what, int ok)
{
    printf("%-64s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

#define N 256
#define SR 44100
static uint8_t par[NTRK][NTP];
static _Alignas(16) uint8_t mem[SYNTH_BYTES], mem2[SYNTH_BYTES];
static synth_t *S = (synth_t *)mem;
static float L[N], R[N], S1[N], S2[N];
static int finite_ok = 1;
static float peak;

static int finite_f(float x) { return x == x && x < 1e30f && x > -1e30f; }
static void block_of(synth_t *s, uint32_t n)
{
    uint32_t i;
    memset(L, 0, sizeof L);
    memset(R, 0, sizeof R);
    memset(S1, 0, sizeof S1);
    memset(S2, 0, sizeof S2);
    synth_prepare(s, n);
    synth_render(s, 0, NVOICE, L, R, S1, S2, n);
    for (i = 0; i < n; i++) {
        if (!finite_f(L[i]) || !finite_f(R[i]) || !finite_f(S1[i]) || !finite_f(S2[i]))
            finite_ok = 0;
        if (fabsf(L[i]) > peak)
            peak = fabsf(L[i]);
        if (fabsf(R[i]) > peak)
            peak = fabsf(R[i]);
    }
}
static void block(void) { block_of(S, N); }

/* a plain brocade sound: the filter wide open (lp12 at 20 kHz), no envelope on it, full sustain, no LFO,
 * no character: what comes out is the saw bank */
static void reset(int saws, int detune, int mix, int spread)
{
    int t;
    for (t = 0; t < NTRK; t++)
        sound_default(par[t], EG_BROCADE);
    for (t = 0; t < NTRK; t++) {
        par[t][TP_E1] = (uint8_t)saws;
        par[t][TP_E2] = (uint8_t)detune;
        par[t][TP_E3] = (uint8_t)mix;
        par[t][TP_E4] = (uint8_t)spread;
        par[t][TP_FTYPE] = FT_LP12;
        par[t][TP_CUT] = 127;
        par[t][TP_RES] = 0;
        par[t][TP_FENV] = 64;
        par[t][TP_KTRK] = 0;
        par[t][TP_A] = 0;
        par[t][TP_S] = 127;
        par[t][TP_LTYPE] = LT_OFF;
        par[t][TP_CTYPE] = CT_OFF;
        par[t][TP_VOLUME] = 127;
        par[t][TP_LEVEL] = 127;
        par[t][TP_PAN] = 64;
    }
    synth_init(S, par);
    synth_tempo(S, 120.0f);
}

/* hold a note for nb blocks after skip blocks; the left channel into buf (nb * N samples) */
static void held(int note, int skip, int nb, float *buf, float *rbuf)
{
    int b;
    synth_note_on(S, 0, note, 127, 0, 1);
    for (b = 0; b < skip; b++)
        block();
    for (b = 0; b < nb; b++) {
        block();
        if (buf)
            memcpy(buf + b * N, L, sizeof L);
        if (rbuf)
            memcpy(rbuf + b * N, R, sizeof R);
    }
}

static double rms(const float *x, int n)
{
    double a = 0.0;
    int i;
    for (i = 0; i < n; i++)
        a += (double)x[i] * (double)x[i];
    return sqrt(a / n);
}
/* the energy at hz (a Hann-windowed DFT bin) */
static double bin(const float *x, int n, double hz)
{
    double re = 0.0, im = 0.0, w = 2.0 * M_PI * hz / SR;
    int i;
    for (i = 0; i < n; i++) {
        double h = 0.5 - 0.5 * cos(2.0 * M_PI * i / (n - 1));
        re += h * (double)x[i] * cos(w * i);
        im += h * (double)x[i] * sin(w * i);
    }
    return re * re + im * im;
}

/* the energy off the note's harmonics against that on them: bins at h + 1/4, 1/2, 3/4 of f, h 1..60 */
static double off_harmonic(const float *x, int n, double f)
{
    double on = 0.0, off = 0.0;
    int h;
    for (h = 1; h <= 60 && h * f < 18000.0; h++) {
        on += bin(x, n, f * h);
        off += bin(x, n, f * (h + 0.25)) + bin(x, n, f * (h + 0.5)) + bin(x, n, f * (h + 0.75));
    }
    return off / on;
}

#define NB 64                                    /* 16384 samples, 0.37 s */
static float A[NB * N], B[NB * N], C[NB * N];

int main(void)
{
    int n, k, b;
    printf("synth_t: %d of %d bytes\n", synth_sizeof(), SYNTH_BYTES);

    /* bounded and finite: every count, the knobs' corners, notes from the bottom to the top */
    {
        float worst = 0.0f;
        int c;
        for (n = 1; n <= 9; n++)
            for (c = 0; c < 16; c++) {
                static const int NOTES[4] = {0, 36, 84, 127};
                int nt;
                for (nt = 0; nt < 4; nt++) {
                    reset(n, c & 1 ? 127 : 0, c & 2 ? 127 : 0, c & 4 ? 127 : 0);
                    if (c & 8) {                 /* resonance up, a ladder */
                        par[0][TP_FTYPE] = FT_LADDER;
                        par[0][TP_RES] = 127;
                        par[0][TP_CUT] = 90;
                    }
                    peak = 0.0f;
                    held(NOTES[nt], 0, 12, 0, 0);
                    if (peak > worst)
                        worst = peak;
                }
            }
        printf("    the loudest corner (one note, full volume): %.2f\n", (double)worst);
        check("bounded and finite: saws 1..9, knobs at the ends, notes 0..127", finite_ok && worst < 4.0f && worst > 0.1f);
    }

    /* the saw count changes the sound (each count a wave of its own, and more saws more energy off the
     * harmonics: the beating); one saw is a plain saw */
    {
        double r[10], spread_[10], corr_max = 0.0;
        int ok = 1, more = 1;
        for (n = 1; n <= 9; n++) {
            reset(n, 60, 100, 0);
            held(45, 8, NB, n == 1 ? C : A, 0);   /* 110 Hz */
            r[n] = rms(n == 1 ? C : A, NB * N);
            spread_[n] = off_harmonic(n == 1 ? C : A, NB * N, 110.0);
            if (n > 1) {                         /* against the count below: a different wave */
                double xy = 0.0, xx = 0.0, yy = 0.0, cc;
                int i;
                for (i = 0; i < NB * N; i++) {
                    xy += (double)A[i] * (double)B[i];
                    xx += (double)A[i] * (double)A[i];
                    yy += (double)B[i] * (double)B[i];
                }
                cc = xy / sqrt(xx * yy + 1e-30);
                if (cc > corr_max)
                    corr_max = cc;
            }
            memcpy(B, n == 1 ? C : A, sizeof A);
        }
        for (n = 2; n <= 9; n++)
            ok &= r[n] > 0.25 * r[1] && r[n] < 2.0 * r[1];
        for (n = 2; n <= 9; n++)                 /* (not in order: three saws hold one pair, at the widest offset) */
            more &= spread_[n] > 1000.0 * spread_[1];
        printf("    rms by saws 1..9:");
        for (n = 1; n <= 9; n++)
            printf(" %.3f", r[n]);
        printf("\n    off the harmonics by saws 1..9:");
        for (n = 1; n <= 9; n++)
            printf(" %.1e", spread_[n]);
        printf("\n    the most alike neighbours: %.3f\n", corr_max);
        check("each saw count its own wave (neighbours differ)", corr_max < 0.99);
        check("two saws and more beat (energy off the harmonics), one does not", more);
        check("the level stays about one saw's at every count", ok);
        {   /* one saw: its harmonics fall as 1/h (the 2nd 6 dB under the 1st, the 4th 12 dB) */
            double h1 = bin(C, NB * N, 110.0), h2 = bin(C, NB * N, 220.0), h4 = bin(C, NB * N, 440.0);
            double d2 = 10.0 * log10(h2 / h1), d4 = 10.0 * log10(h4 / h1);
            printf("    one saw: 2nd %.1f dB, 4th %.1f dB under the 1st\n", d2, d4);
            check("one saw is a saw (1/h harmonics)", fabs(d2 + 6.0) < 1.5 && fabs(d4 + 12.0) < 2.0);
        }
    }

    /* detune 0: nine saws in unison, every partial on the note's harmonics; detune up: between them too */
    {
        double r0, r1;
        reset(9, 0, 127, 0);
        held(45, 8, NB, A, 0);
        r0 = off_harmonic(A, NB * N, 110.0);
        reset(9, 64, 127, 0);
        held(45, 8, NB, B, 0);
        r1 = off_harmonic(B, NB * N, 110.0);
        printf("    off the harmonics / on them: detune 0 %.2e, detune 64 %.2e\n", r0, r1);
        check("detune 0: one saw's partials only (the saws in unison)", r0 < 1e-5);
        check("detune up: partials between the harmonics", r1 > 1000.0 * r0 && r1 > 1e-4);
    }

    /* spread: 0 mono, up stereo, the two sides alike in level */
    {
        double d0, d1, l1, r1;
        int i;
        reset(9, 60, 100, 0);
        held(57, 8, 16, A, B);
        for (i = 0, d0 = 0.0; i < 16 * N; i++)
            d0 += fabs((double)A[i] - (double)B[i]);
        reset(9, 60, 100, 127);
        held(57, 8, 16, A, B);
        for (i = 0, d1 = 0.0; i < 16 * N; i++)
            d1 += fabs((double)A[i] - (double)B[i]);
        l1 = rms(A, 16 * N);
        r1 = rms(B, 16 * N);
        check("spread 0 is mono", d0 < 1e-6);
        printf("    spread 127: L %.3f R %.3f, |L - R| %.1f\n", l1, r1, d1 / (16 * N));
        check("spread 127 is stereo, the sides within 1.5 dB", d1 > 0.05 * 16 * N * l1 && fabs(20.0 * log10(l1 / r1)) < 1.5);
    }

    /* the guard's thinning: 9 -> 3 saws. The same voice rendered with and without (a copy of the synth):
     * their difference rises gradually (no step), takes the six saws' 72 ms, and the level holds */
    {
        static float D[NB * N];
        double first, later, before, after;
        int i;
        reset(9, 64, 110, 80);
        held(48, 20, 0, 0, 0);
        memcpy(mem2, mem, sizeof mem);
        synth_saws(S, 3);
        for (b = 0; b < NB; b++) {
            block();
            memcpy(A + b * N, L, sizeof L);
        }
        {
            synth_t *S2_ = (synth_t *)mem2;
            for (b = 0; b < NB; b++) {
                block_of(S2_, N);
                memcpy(B + b * N, L, sizeof L);
            }
        }
        for (i = 0; i < NB * N; i++)
            D[i] = A[i] - B[i];
        first = rms(D, 44);                      /* the first millisecond */
        later = rms(D + 4410, 441);              /* at 100 ms: the six gone */
        before = rms(B + 4410, 8820);
        after = rms(A + 4410, 8820);
        printf("    thinning 9 -> 3: the difference %.4f in the first ms, %.4f at 100 ms; level %.1f dB\n", first, later,
               20.0 * log10(after / before));
        check("thinning: no step (the first ms under 10 % of the full change)", first < 0.1 * later);
        {   /* how far it has gone: 0..10 ms, 30..40 ms, 90..110 ms (a saw every 12 ms) */
            double w1 = rms(D, 441), w2 = rms(D + 1323, 441), w3 = rms(D + 3969, 882);
            printf("    the difference over 0..10 ms %.4f, 30..40 ms %.4f, 90..110 ms %.4f\n", w1, w2, w3);
            check("thinning: gradual (the first 10 ms under half the change)", w1 < 0.5 * (w2 > w3 ? w2 : w3));
        }
        check("thinning: the level held (within 2 dB)", fabs(20.0 * log10(after / before)) < 2.0);
        synth_saws(S, 9);                        /* back: the saws fade in */
        peak = 0.0f;
        for (b = 0; b < 16; b++)
            block();
        check("thinning undone: bounded", finite_ok && peak < 4.0f);
    }

    /* the voice cap: eight notes at once on one track, then another track's */
    {
        static const int SAWS[3] = {9, 7, 5}, CAP[3] = {4, 5, 6};
        int ok = 1;
        for (k = 0; k < 3; k++) {
            reset(SAWS[k], 64, 100, 60);
            for (n = 0; n < 8; n++)
                synth_note_on(S, 0, 48 + 3 * n, 100, 0, (uint32_t)n + 1);
            for (b = 0; b < 20; b++)
                block();
            for (n = 0; n < 3; n++)
                synth_note_on(S, 1, 60 + 4 * n, 100, 0, (uint32_t)n + 100);
            for (b = 0; b < 20; b++)
                block();
            printf("    %d saws: %d voices sounding (cap %d)\n", SAWS[k], synth_voices_used(S), CAP[k]);
            ok &= synth_voices_used(S) == CAP[k];
        }
        check("brocade keeps to its cap (6, 5 at 7..8 saws, 4 at 9)", ok);
        reset(9, 64, 100, 60);                   /* other engines are not held to it */
        par[1][TP_ENGINE] = EG_WEFT;
        for (n = 0; n < 4; n++)
            synth_note_on(S, 0, 48 + 3 * n, 100, 0, (uint32_t)n + 1);
        for (n = 0; n < 4; n++)
            synth_note_on(S, 1, 60 + 3 * n, 100, 0, (uint32_t)n + 10);
        for (b = 0; b < 20; b++)
            block();
        check("other engines beside it keep their voices", synth_voices_used(S) == 8);
    }

    /* the cost: one voice held, lp24, its instructions a sample */
    {
        double base = 0.0, c1 = 0.0, c9 = 0.0;
        int st;
        printf("\ncost: one voice, lp24, instructions a sample (the whole voice: engine, filter, envelopes, outputs)\n");
        for (k = 0; k < 2; k++) {               /* an ordinary engine's, for scale: weft and stack */
            uint64_t t0, t;
            int e = k ? EG_STACK : EG_WEFT;
            reset(9, 64, 100, 60);
            sound_default(par[0], e);
            par[0][TP_FTYPE] = FT_LP24;
            par[0][TP_S] = 127;
            synth_note_on(S, 0, 57, 100, 0, 1);
            for (b = 0; b < 10; b++)
                block();
            t0 = instr_now();
            for (b = 0; b < 400; b++)
                block();
            t = instr_now() - t0;
            printf("  %-8s (default)   %6.1f\n", EG_NAME[e], (double)t / (400.0 * N));
            base += (double)t / (800.0 * N);
        }
        for (st = 0; st < 2; st++)
            for (n = 1; n <= 9; n++) {
                uint64_t t0, t;
                double c;
                reset(n, 64, 100, st ? 70 : 0);
                par[0][TP_FTYPE] = FT_LP24;
                synth_note_on(S, 0, 57, 100, 0, 1);
                for (b = 0; b < 10; b++)
                    block();
                t0 = instr_now();
                for (b = 0; b < 400; b++)
                    block();
                t = instr_now() - t0;
                c = (double)t / (400.0 * N);
                if (st) {
                    if (n == 1)
                        c1 = c;
                    if (n == 9)
                        c9 = c;
                }
                printf("  brocade %d saw%s %s %6.1f  (%.2f x the ordinary voice)\n", n, n > 1 ? "s" : " ",
                       st ? "stereo" : "mono  ", c, base > 0.0 ? c / base : 0.0);
            }
        if (base > 0.0) {
            double per = (c9 - c1) / 8.0;
            printf("  a saw costs %.1f (stereo); in the split's units (an ordinary voice 4): %.2f + %.2f a saw\n", per,
                   4.0 * (c1 - per) / base, 4.0 * per / base);
        }
    }
    printf(bad ? "\n%d FAILED\n" : "\nall ok\n", bad);
    return bad != 0;
}
