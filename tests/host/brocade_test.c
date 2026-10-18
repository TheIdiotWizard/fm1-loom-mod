/* SPDX-License-Identifier: GPL-3.0-only */
/* brocade, the supersaw engine (dsp/synth.c, its saw bank after NuSaw, MIT), on its own: bounded and finite at
 * every saw count, knob corner and note; the saw count changes the sound, one saw being one saw; detune 0
 * keeps every partial on the note's harmonics (the saws in unison), detune up spreads them; spread makes it
 * stereo and 0 leaves it mono; the pool keeps to brocade's voice cap (six, five at seven saws). Then the
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
static _Alignas(16) uint8_t mem[SYNTH_BYTES];
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
        par[t][TP_LEVEL] = 108;          /* unity: tp_level_gain(108) x 100/127 = 0.99 */
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

/* a sine's frequency from its rising zero crossings (interpolated), the first to the last */
static double zc_hz(const float *x, int n)
{
    double first = -1.0, last = -1.0;
    int i, cycles = -1;
    for (i = 1; i < n; i++)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            double t = (double)(i - 1) + (double)x[i - 1] / ((double)x[i - 1] - (double)x[i]);
            if (first < 0.0)
                first = t;
            last = t;
            cycles++;
        }
    return cycles > 0 ? (double)cycles * SR / (last - first) : 0.0;
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
        for (n = 1; n <= 7; n++)
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
        check("bounded and finite: saws 1..9 (8, 9: played as 7), knobs at the ends, notes 0..127", finite_ok && worst < 4.0f && worst > 0.1f);
    }

    /* the saw count changes the sound (each count a wave of its own, and more saws more energy off the
     * harmonics: the beating); one saw is a plain saw */
    {
        double r[10], spread_[10], corr_max = 0.0;
        int ok = 1, more = 1;
        for (n = 1; n <= 7; n++) {
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
        for (n = 2; n <= 7; n++)
            ok &= r[n] > 0.25 * r[1] && r[n] < 2.0 * r[1];
        for (n = 2; n <= 7; n++)                 /* (not in order: three saws hold one pair, at the widest offset) */
            more &= spread_[n] > 1000.0 * spread_[1];
        printf("    rms by saws 1..7:");
        for (n = 1; n <= 7; n++)
            printf(" %.3f", r[n]);
        printf("\n    off the harmonics by saws 1..7:");
        for (n = 1; n <= 7; n++)
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

    /* detune 0: seven saws in unison, every partial on the note's harmonics; detune up: between them too */
    {
        double r0, r1;
        reset(7, 0, 127, 0);
        held(45, 8, NB, A, 0);
        r0 = off_harmonic(A, NB * N, 110.0);
        reset(7, 64, 127, 0);
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
        reset(7, 60, 100, 0);
        held(57, 8, 16, A, B);
        for (i = 0, d0 = 0.0; i < 16 * N; i++)
            d0 += fabs((double)A[i] - (double)B[i]);
        reset(7, 60, 100, 127);
        held(57, 8, 16, A, B);
        for (i = 0, d1 = 0.0; i < 16 * N; i++)
            d1 += fabs((double)A[i] - (double)B[i]);
        l1 = rms(A, 16 * N);
        r1 = rms(B, 16 * N);
        check("spread 0 is mono", d0 < 1e-6);
        printf("    spread 127: L %.3f R %.3f, |L - R| %.1f\n", l1, r1, d1 / (16 * N));
        check("spread 127 is stereo, the sides within 1.5 dB", d1 > 0.05 * 16 * N * l1 && fabs(20.0 * log10(l1 / r1)) < 1.5);
    }

    /* the voice cap: eight notes at once on one track, then another track's */
    {
        static const int SAWS[3] = {7, 5, 9}, CAP[3] = {5, 6, 5};   /* (9: an old project's, played as 7) */
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
        check("brocade keeps to its cap (6, 5 at seven saws)", ok);
        reset(7, 64, 100, 60);                   /* other engines are not held to it */
        par[1][TP_ENGINE] = EG_WEFT;
        for (n = 0; n < 4; n++)
            synth_note_on(S, 0, 48 + 3 * n, 100, 0, (uint32_t)n + 1);
        for (n = 0; n < 4; n++)
            synth_note_on(S, 1, 60 + 3 * n, 100, 0, (uint32_t)n + 10);
        for (b = 0; b < 20; b++)
            block();
        check("other engines beside it keep their voices", synth_voices_used(S) == 8);
    }

    /* the sub (the Sub page: TP_X1 level, TP_X2 offset): NuSaw's sine at the note plus the offset, the same into both
     * sides, added before the filter. Level 0 renders exactly as no sub (whatever the offset); the sub alone (the
     * render with it less the render without: the same saws, the filter wide open) is a sine at the note's
     * frequency x 2^(offset / 12), as loud against one saw as NuSaw's (its sine's peak at the saw's: rms 1.22 x
     * the saw's); it follows the bend, and it starts when its level comes up mid-note */
    {
        static const int OFF[3] = {-24, -19, -12};
        static float D[NB * N], E[NB * N];
        int i, same = 1, mid_ok;
        double f_note = 220.0, r_saw, r_sub = 0.0;
        reset(7, 60, 100, 127);
        held(57, 8, NB, A, B);
        reset(7, 60, 100, 127);
        for (n = 0; n < NTRK; n++)
            par[n][TP_X2] = 40;                  /* an offset, the level 0: nothing changes */
        held(57, 8, NB, C, D);
        for (i = 0; i < NB * N; i++)
            same &= A[i] == C[i] && B[i] == D[i];
        check("sub level 0: the same samples as no sub, at any offset", same);
        reset(1, 0, 0, 0);                       /* one saw alone, for the level */
        held(57, 8, NB, A, 0);
        r_saw = rms(A, NB * N);
        for (k = 0; k < 3; k++) {
            double f_want = f_note * pow(2.0, OFF[k] / 12.0), f_got, lr = 0.0;
            reset(1, 0, 0, 0);
            held(57, 8, NB, A, B);
            reset(1, 0, 0, 0);
            par[0][TP_X1] = 127;
            par[0][TP_X2] = (uint8_t)(64 + OFF[k]);
            held(57, 8, NB, C, D);
            for (i = 0; i < NB * N; i++) {
                D[i] -= B[i];
                C[i] -= A[i];
                lr = fmax(lr, fabs((double)C[i] - (double)D[i]));
            }
            f_got = zc_hz(C, NB * N);
            if (OFF[k] == -12)
                r_sub = rms(C, NB * N);
            printf("    sub %+d st: %.2f Hz (want %.2f), |L - R| %.1e\n", OFF[k], f_got, f_want, lr);
            check(OFF[k] == -24 ? "the sub at -24 st: two octaves under the note"
                  : OFF[k] == -19 ? "the sub at -19 st: an octave and a fifth under"
                                  : "the sub at -12 st: an octave under", fabs(f_got / f_want - 1.0) < 0.002);
            check("  the same on both sides", lr < 1e-4);
            check("  a sine (no harmonics: the 2nd and 3rd 50 dB under)",
                  bin(C, NB * N, 2.0 * f_want) < 1e-5 * bin(C, NB * N, f_want) &&
                  bin(C, NB * N, 3.0 * f_want) < 1e-5 * bin(C, NB * N, f_want));
        }
        printf("    sub rms / one saw's rms: %.3f (NuSaw's: 1.22)\n", r_sub / r_saw);
        check("the sub as loud against a saw as NuSaw's", fabs(r_sub / r_saw / 1.2247 - 1.0) < 0.1);
        /* the bend: an octave up takes the sub with it */
        reset(1, 0, 0, 0);
        held(57, 8, NB, A, 0);
        reset(1, 0, 0, 0);
        par[0][TP_X1] = 127;
        par[0][TP_X2] = 64 - 19;
        synth_bend(S, 0, 1200);
        held(57, 8, NB, C, 0);
        reset(1, 0, 0, 0);                       /* (the saw bent as well, without the sub) */
        synth_bend(S, 0, 1200);
        held(57, 8, NB, A, 0);
        for (i = 0; i < NB * N; i++)
            C[i] -= A[i];
        printf("    bent an octave up, -19 st: %.2f Hz (want %.2f)\n", zc_hz(C, NB * N), 2.0 * f_note * pow(2.0, -19.0 / 12.0));
        check("the sub follows the bend", fabs(zc_hz(C, NB * N) / (2.0 * f_note * pow(2.0, -19.0 / 12.0)) - 1.0) < 0.002);
        /* the level up mid-note: the sub starts (its increment made then) */
        reset(1, 0, 0, 0);
        held(57, 8, NB, A, 0);
        reset(1, 0, 0, 0);
        synth_note_on(S, 0, 57, 127, 0, 1);
        for (b = 0; b < 8; b++)
            block();
        par[0][TP_X1] = 127;
        for (b = 0; b < NB; b++) {
            block();
            memcpy(E + b * N, L, sizeof L);
        }
        for (i = 0; i < NB * N; i++)
            E[i] -= A[i];
        mid_ok = fabs(zc_hz(E, NB * N) / (f_note * 0.5) - 1.0) < 0.002;
        printf("    the level up mid-note: %.2f Hz\n", zc_hz(E, NB * N));
        check("the sub starts when its level comes up mid-note", mid_ok);
    }

    /* the cost: one voice held, lp24, its instructions a sample */
    {
        double base = 0.0, c1 = 0.0, c9 = 0.0;
        int st;
        printf("\ncost: one voice, lp24, instructions a sample (the whole voice: engine, filter, envelopes, outputs)\n");
        for (k = 0; k < 2; k++) {               /* an ordinary engine's, for scale: weft and stack */
            uint64_t t0, t;
            int e = k ? EG_STACK : EG_WEFT;
            reset(7, 64, 100, 60);
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
            for (n = 1; n <= 7; n++) {
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
                    if (n == 7)
                        c9 = c;
                }
                printf("  brocade %d saw%s %s %6.1f  (%.2f x the ordinary voice)\n", n, n > 1 ? "s" : " ",
                       st ? "stereo" : "mono  ", c, base > 0.0 ? c / base : 0.0);
            }
        {                                       /* the sub on: seven saws, stereo */
            uint64_t t0, t;
            reset(7, 64, 100, 70);
            par[0][TP_FTYPE] = FT_LP24;
            par[0][TP_X1] = 100;
            synth_note_on(S, 0, 57, 100, 0, 1);
            for (b = 0; b < 10; b++)
                block();
            t0 = instr_now();
            for (b = 0; b < 400; b++)
                block();
            t = instr_now() - t0;
            printf("  brocade 7 saws stereo + the sub %6.1f  (the sub %+.1f; the split counts it a unit)\n",
                   (double)t / (400.0 * N), (double)t / (400.0 * N) - c9);
        }
        if (base > 0.0) {
            double per = (c9 - c1) / 6.0;   /* (c9: seven saws', the most) */
            printf("  a saw costs %.1f (stereo); in the split's units (an ordinary voice 4): %.2f + %.2f a saw\n", per,
                   4.0 * (c1 - per) / base, 4.0 * per / base);
        }
    }
    printf(bad ? "\n%d FAILED\n" : "\nall ok\n", bad);
    return bad != 0;
}
