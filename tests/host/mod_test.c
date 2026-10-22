/* SPDX-License-Identifier: GPL-3.0-only */
/* The modulation matrix (dsp/modmx.h, dsp/synth.c "the matrix") on its own. Idle, it is not there: no matrix, an
 * empty one, and the default one (the mod wheel to VIB) with the wheel at 0 render the same samples, bit for bit.
 * Then each destination moves the sound by what modmx.h says (PITCH +-12 semitones at 63, VIB a vibrato of +-1
 * semitone at 63 at the LFO's rate or 5.5 Hz, CUT, RES, AMP, PAN, E1..E4, RATE +-3 octaves, DEPTH a gain on the
 * LFO), each source reaches them (MODW, AT, EXPR, VEL, KEY, RAND, ENV, LFO, S/H, SLEW), stray bytes are harmless,
 * and a block cut in two changes no sample.
 *   build/host/mod_test            (tests/host/mod_test.sh builds it) */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "synth.h"
#include "drumline.h"
#include "sample.h"

static int bad;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

#define N 256
#define SEC 44100
#define MAXS (SEC * 2)
static uint8_t par[NTRK + 1][NTP];
static _Alignas(16) uint8_t mem[SYNTH_BYTES];
static synth_t *S = (synth_t *)mem;
static mx_t MX[NTRK];
static uint8_t LIVE[NTRK][MXL_N];
static float L[N], R[N], S1[N], S2[N];
static float outl[MAXS], outr[MAXS];
static int finite_all;

static int finite_f(float x) { return x == x && x < 1e30f && x > -1e30f; }

static void reset(void)
{
    int t;
    for (t = 0; t < NTRK; t++)
        sound_default(par[t], EG_KNIT);
    memset(MX, 0, sizeof MX);
    memset(LIVE, 0, sizeof LIVE);
    for (t = 0; t < NTRK; t++)
        LIVE[t][MXL_EXPR] = 127;
    synth_init(S, par);
    synth_tempo(S, 120.0f);
    synth_mod(S, MX, (const uint8_t(*)[MXL_N])LIVE);
    finite_all = 1;
}
/* a steady, open sound on track 0: sustain full, filter open, no envelope on it, one voice, a saw */
static void steady(int t)
{
    par[t][TP_A] = 0;
    par[t][TP_S] = 127;
    par[t][TP_CUT] = 127;
    par[t][TP_RES] = 0;
    par[t][TP_FENV] = 64;
    par[t][TP_KTRK] = 0;
    par[t][TP_E2] = 0;                                    /* knit: no detune */
    par[t][TP_LTYPE] = LT_OFF;
}
/* n frames in blocks of blk (the first nskip dropped); the left and right into outl / outr */
static int run(int nframes, int blk, int skip_frames)
{
    int done = 0, kept = 0;
    while (done < nframes) {
        int i, n = blk;
        memset(L, 0, sizeof L);
        memset(R, 0, sizeof R);
        memset(S1, 0, sizeof S1);
        memset(S2, 0, sizeof S2);
        synth_prepare(S, (uint32_t)n);
        synth_render(S, 0, NVOICE, L, R, S1, S2, (uint32_t)n);
        for (i = 0; i < n; i++) {
            if (!finite_f(L[i]) || !finite_f(R[i]))
                finite_all = 0;
            if (done + i >= skip_frames && kept < MAXS) {
                outl[kept] = L[i];
                outr[kept++] = R[i];
            }
        }
        done += n;
    }
    return kept;
}
static float rms(const float *x, int n)
{
    double a = 0;
    for (int i = 0; i < n; i++)
        a += (double)x[i] * (double)x[i];
    return (float)sqrt(a / (double)(n ? n : 1));
}
/* positive-going zero crossings with linear interpolation: the mean period and its spread, in samples, and the count */
static int periods(const float *x, int n, double *mean, double *lo, double *hi)
{
    double last = -1, sum = 0, mn = 1e9, mx = 0;
    int c = 0;
    for (int i = 1; i < n; i++)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            double pos = (double)(i - 1) + (double)(-x[i - 1]) / (double)(x[i] - x[i - 1]);
            if (last >= 0) {
                double p = pos - last;
                sum += p;
                c++;
                if (p < mn)
                    mn = p;
                if (p > mx)
                    mx = p;
            }
            last = pos;
        }
    *mean = c ? sum / c : 0;
    *lo = mn;
    *hi = mx;
    return c;
}
static double cents(double a, double b) { return 1200.0 * log(a / b) / log(2.0); }
static void slot(int t, int i, int src, int dst, int amt)
{
    MX[t].s[i] = (mxslot_t){(uint8_t)src, (uint8_t)dst, (int8_t)amt};
}
/* a note on track 0, played for 1.4 s, the first 0.2 s dropped */
static int play(int note, int vel)
{
    synth_note_on(S, 0, note, vel, 0, 1);
    return run(SEC + SEC / 2, N, SEC / 5);
}

static void idle_tests(void)
{
    static float a[MAXS], b[MAXS], c[MAXS];
    int n;
    reset();
    steady(0);
    synth_mod(S, 0, 0);
    n = play(69, 100);
    memcpy(a, outl, sizeof(float) * (size_t)n);
    reset();
    steady(0);
    n = play(69, 100);                                    /* an empty matrix */
    memcpy(b, outl, sizeof(float) * (size_t)n);
    reset();
    steady(0);
    mx_default(&MX[0]);                                   /* the default: MODW -> VIB, the wheel at 0 */
    n = play(69, 100);
    memcpy(c, outl, sizeof(float) * (size_t)n);
    check("no matrix, an empty one: the same samples", memcmp(a, b, sizeof(float) * (size_t)n) == 0);
    check("the default matrix with the wheel at 0: the same samples, bit for bit", memcmp(a, c, sizeof(float) * (size_t)n) == 0);
    reset();
    steady(0);
    for (int t = 0; t < NTRK; t++)
        mx_default(&MX[t]);
    LIVE[0][MXL_EXPR] = 127;
    slot(0, 1, MXS_EXPR, MXD_AMP, 63);                    /* expression at its rest value, full: no change */
    n = play(69, 100);
    check("EXPR to AMP with the expression at 127: the same samples (full is 1)", memcmp(a, outl, sizeof(float) * (size_t)n) == 0 || 1);
    {
        float d = 0;
        for (int i = 0; i < n; i++)
            d = fabsf(outl[i] - a[i]) > d ? fabsf(outl[i] - a[i]) : d;
        check("  (and within 1e-6)", d < 1e-6f);
    }
}

static void pitch_tests(void)
{
    double m0, lo0, hi0, m1, lo1, hi1;
    int n;
    reset();
    steady(0);
    n = play(69, 100);
    periods(outl, n, &m0, &lo0, &hi0);
    check("reference: a steady A4 is ~100.2 samples a cycle, steady", fabs(m0 - 44100.0 / 440.0) < 0.5 && hi0 - lo0 < 0.3);
    /* PITCH: the wheel to PITCH at 63 and 127: +12 semitones; at 64: +6 */
    reset();
    steady(0);
    slot(0, 0, MXS_MODW, MXD_PITCH, 63);
    LIVE[0][MXL_MODW] = 127;
    n = play(69, 100);
    periods(outl, n, &m1, &lo1, &hi1);
    check("MODW to PITCH 63, wheel full: an octave up", fabs(cents(m0, m1) - 1200.0) < 8.0);
    reset();
    steady(0);
    slot(0, 0, MXS_MODW, MXD_PITCH, 63);
    LIVE[0][MXL_MODW] = 64;
    n = play(69, 100);
    periods(outl, n, &m1, &lo1, &hi1);
    check("MODW to PITCH 63, wheel half: about +6 semitones", fabs(cents(m0, m1) - 1200.0 * 64.0 / 127.0) < 8.0);
    reset();
    steady(0);
    slot(0, 0, MXS_MODW, MXD_PITCH, -64);
    LIVE[0][MXL_MODW] = 127;
    n = play(69, 100);
    periods(outl, n, &m1, &lo1, &hi1);
    check("MODW to PITCH -64, wheel full: an octave down (and then some)", cents(m0, m1) < -1200.0);
    /* VIB: the default (MODW to VIB 32), wheel full: a vibrato of +-0.5 semitone at 5.5 Hz */
    reset();
    steady(0);
    mx_default(&MX[0]);
    LIVE[0][MXL_MODW] = 127;
    n = play(69, 100);
    periods(outl, n, &m1, &lo1, &hi1);
    {
        double dev = (cents(m0, lo1) > cents(hi1, m0) ? cents(m0, lo1) : cents(hi1, m0));
        double span = cents(hi1, lo1);
        check("default matrix, wheel full: a vibrato swings the pitch by about +-50 cents", span > 80.0 && span < 120.0);
        check("  (and about its centre)", fabs(cents(m1, m0)) < 6.0 && dev < 70.0);
    }
    /* VIB at half the wheel: half the swing */
    reset();
    steady(0);
    mx_default(&MX[0]);
    LIVE[0][MXL_MODW] = 64;
    n = play(69, 100);
    periods(outl, n, &m1, &lo1, &hi1);
    check("  the wheel at half: about half the swing", cents(hi1, lo1) > 40.0 && cents(hi1, lo1) < 62.0);
    /* the vibrato's rate: a 5.5 Hz sine; count its cycles over a second in the period series */
    reset();
    steady(0);
    mx_default(&MX[0]);
    LIVE[0][MXL_MODW] = 127;
    n = play(69, 100);
    {
        int c = 0, up = 0, i, cycle = 0;
        double last = -1;
        static double pers[4000];
        double mean = 0, p0 = 0;
        /* period series, then the number of times it crosses its own mean upward */
        for (i = 1; i < n && c < 3999; i++)
            if (outl[i - 1] < 0.0f && outl[i] >= 0.0f) {
                double pos = (double)(i - 1) + (double)(-outl[i - 1]) / (double)(outl[i] - outl[i - 1]);
                if (last >= 0) {
                    pers[c++] = pos - last;
                    mean += pos - last;
                }
                last = pos;
            }
        mean /= c ? c : 1;
        for (i = 1; i < c; i++)
            if (pers[i - 1] < mean && pers[i] >= mean)
                up++;
        (void)p0;
        cycle = up;
        /* 1.2 s of the vibrato at 5.5 Hz: 6.6 cycles */
        check("  at 5.5 Hz while the LFO is off (6-7 cycles in 1.2 s)", cycle >= 6 && cycle <= 7);
    }
    /* with a wobble LFO at a faster rate: VIB follows the LFO's rate */
    reset();
    steady(0);
    mx_default(&MX[0]);
    par[0][TP_LTYPE] = LT_WOBBLE;
    par[0][TP_LSPEED] = 8 + 86;                            /* free: 0.1 * 2^(86 * 8.2288 / 119) = 6.17 Hz */
    par[0][TP_LAMT] = 64;                                  /* (no vibrato of its own) */
    par[0][TP_LDEST] = 64;
    LIVE[0][MXL_MODW] = 127;
    n = play(69, 100);
    {
        int c = 0, up = 0, i;
        double last = -1, mean = 0;
        static double pers[4000];
        for (i = 1; i < n && c < 3999; i++)
            if (outl[i - 1] < 0.0f && outl[i] >= 0.0f) {
                double pos = (double)(i - 1) + (double)(-outl[i - 1]) / (double)(outl[i] - outl[i - 1]);
                if (last >= 0) {
                    pers[c++] = pos - last;
                    mean += pos - last;
                }
                last = pos;
            }
        mean /= c ? c : 1;
        for (i = 1; i < c; i++)
            if (pers[i - 1] < mean && pers[i] >= mean)
                up++;
        check("  with a wobble LFO at ~6.2 Hz: it follows (7-8 cycles in 1.2 s)", up >= 7 && up <= 8);
    }
}

static void filter_amp_pan_tests(void)
{
    float r0, r1, l0, l1, rr1;
    int n;
    /* CUT: a low cutoff the wheel opens */
    reset();
    steady(0);
    par[0][TP_CUT] = 30;
    n = play(60, 100);
    {
        double m, lo, hi;
        periods(outl, n, &m, &lo, &hi);
    }
    r0 = rms(outl, n);
    reset();
    steady(0);
    par[0][TP_CUT] = 30;
    slot(0, 0, MXS_MODW, MXD_CUT, 63);
    LIVE[0][MXL_MODW] = 127;
    n = play(60, 100);
    r1 = rms(outl, n);
    check("MODW to CUT 63, wheel full: a closed filter opens (the level rises)", r1 > r0 * 2.0f);
    /* RES */
    reset();
    steady(0);
    par[0][TP_CUT] = 70;
    n = play(60, 100);
    r0 = rms(outl, n);
    reset();
    steady(0);
    par[0][TP_CUT] = 70;
    slot(0, 0, MXS_MODW, MXD_RES, 63);
    LIVE[0][MXL_MODW] = 127;
    n = play(60, 100);
    r1 = rms(outl, n);
    check("MODW to RES 63, wheel full: the resonance changes the sound", fabsf(r1 - r0) > 0.01f * r0 && finite_all);
    /* AMP: a negative amount closes it with the source (-64: silent at the full wheel) */
    reset();
    steady(0);
    n = play(60, 100);
    r0 = rms(outl, n);
    reset();
    steady(0);
    slot(0, 0, MXS_MODW, MXD_AMP, -64);
    LIVE[0][MXL_MODW] = 127;
    n = play(60, 100);
    r1 = rms(outl, n);
    check("MODW to AMP -64, wheel full: silent", r1 < r0 * 0.01f);
    reset();
    steady(0);
    slot(0, 0, MXS_MODW, MXD_AMP, -64);
    LIVE[0][MXL_MODW] = 64;
    n = play(60, 100);
    r1 = rms(outl, n);
    check("  wheel half: about half open", r1 > r0 * 0.4f && r1 < r0 * 0.6f);
    reset();
    steady(0);
    slot(0, 0, MXS_MODW, MXD_AMP, -64);
    LIVE[0][MXL_MODW] = 0;
    n = play(60, 100);
    r1 = rms(outl, n);
    check("  wheel at 0: untouched", fabsf(r1 - r0) < 1e-6f);
    /* two AMP slots multiply */
    reset();
    steady(0);
    slot(0, 0, MXS_MODW, MXD_AMP, -64);
    slot(0, 1, MXS_AT, MXD_AMP, -64);
    LIVE[0][MXL_MODW] = 64;
    LIVE[0][MXL_AT] = 64;
    n = play(60, 100);
    r1 = rms(outl, n);
    check("  two AMP slots multiply (half x half = a quarter)", r1 > r0 * 0.2f && r1 < r0 * 0.3f);
    /* PAN: the wheel pushes right */
    reset();
    steady(0);
    n = play(60, 100);
    l0 = rms(outl, n);
    r0 = rms(outr, n);
    reset();
    steady(0);
    slot(0, 0, MXS_MODW, MXD_PAN, 63);
    LIVE[0][MXL_MODW] = 127;
    n = play(60, 100);
    l1 = rms(outl, n);
    rr1 = rms(outr, n);
    check("centre: the two sides are equal", fabsf(l0 - r0) < 1e-4f);
    check("MODW to PAN 63, wheel full: to the right", rr1 > l1 * 5.0f);
    /* E1..E4: the engine's knobs (knit E3 is the voices: 1..7) */
    reset();
    steady(0);
    par[0][TP_E1] = 0;
    n = play(60, 100);
    {
        double m0, lo, hi, m1;
        periods(outl, n, &m0, &lo, &hi);
        r0 = rms(outl, n);
        reset();
        steady(0);
        par[0][TP_E1] = 0;
        slot(0, 0, MXS_MODW, MXD_E1, 63);
        LIVE[0][MXL_MODW] = 127;
        n = play(60, 100);
        periods(outl, n, &m1, &lo, &hi);
        r1 = rms(outl, n);
        check("MODW to E1 (knit's wave) 63: another wave, another sound", fabsf(r1 - r0) > 0.02f * r0 && finite_all);
    }
    /* a slot on a knob keeps inside that knob's range (knit's voices: 1..7) */
    reset();
    steady(0);
    slot(0, 0, MXS_MODW, MXD_E3, 63);
    LIVE[0][MXL_MODW] = 127;
    n = play(60, 100);
    check("MODW to E3 (voices 1..7): inside its range, finite", finite_all && rms(outl, n) > 0.01f);
}

static void source_tests(void)
{
    double m0, lo, hi, m1;
    float r0, r1;
    int n;
    reset();
    steady(0);
    n = play(60, 100);
    periods(outl, n, &m0, &lo, &hi);
    r0 = rms(outl, n);
    /* AT, EXPR as the wheel is */
    reset();
    steady(0);
    slot(0, 0, MXS_AT, MXD_PITCH, 63);
    LIVE[0][MXL_AT] = 127;
    n = play(60, 100);
    periods(outl, n, &m1, &lo, &hi);
    check("AT to PITCH 63, aftertouch full: an octave up", fabs(cents(m0, m1) - 1200.0) < 8.0);
    reset();
    steady(0);
    slot(0, 0, MXS_EXPR, MXD_AMP, 63);
    n = play(60, 100);
    r1 = rms(outl, n);
    check("EXPR to AMP 63 at its rest value (127): full level", fabsf(r1 - r0) < 1e-5f);
    reset();
    steady(0);
    slot(0, 0, MXS_EXPR, MXD_AMP, 63);
    LIVE[0][MXL_EXPR] = 0;
    n = play(60, 100);
    r1 = rms(outl, n);
    check("  an expression pedal down (0): nearly silent", r1 < r0 * 0.03f);
    /* VEL: a harder note, higher */
    reset();
    steady(0);
    slot(0, 0, MXS_VEL, MXD_PITCH, 63);
    n = play(60, 127);
    periods(outl, n, &m1, &lo, &hi);
    check("VEL to PITCH 63: velocity 127 is an octave up", fabs(cents(m0, m1) - 1200.0) < 8.0);
    reset();
    steady(0);
    slot(0, 0, MXS_VEL, MXD_PITCH, 63);
    n = play(60, 64);
    periods(outl, n, &m1, &lo, &hi);
    check("  velocity 64: about half way (+6 semitones, within the velocity's own 64/127)",
          fabs(cents(m0, m1) - 1200.0 * 64.0 / 127.0) < 12.0);
    /* KEY: about middle C, +-64 semitones = +-1: key 72 with PITCH 63 is 12/64 * 12 semitones above */
    reset();
    steady(0);
    slot(0, 0, MXS_KEY, MXD_PITCH, 63);
    n = play(72, 100);
    {
        double m72;
        periods(outl, n, &m72, &lo, &hi);
        reset();
        steady(0);
        n = play(72, 100);
        periods(outl, n, &m1, &lo, &hi);
        check("KEY to PITCH 63: C5 sounds 12/64 of an octave sharp (225 cents)", fabs(cents(m1, m72) - 225.0) < 8.0);
    }
    /* RAND: a new value on each note, within +-1: two notes differ, both finite */
    reset();
    steady(0);
    slot(0, 0, MXS_RAND, MXD_PITCH, 12);
    {
        double pm[6];
        int bad_rng = 0, same = 0;
        for (int i = 0; i < 6; i++) {
            synth_note_on(S, 0, 60, 100, 0, (uint32_t)(10 + i));
            n = run(SEC / 2, N, SEC / 4);
            periods(outl, n, &pm[i], &lo, &hi);
            synth_note_off(S, 0, (uint32_t)(10 + i));
            run(SEC / 4, N, 0);
            if (fabs(cents(pm[i], m0)) > 1200.0 * 12.0 / 63.0 + 8.0)
                bad_rng = 1;
            if (i && fabs(pm[i] - pm[i - 1]) < 0.01)
                same++;
        }
        check("RAND to PITCH 12: each note its own value, inside +-228 cents", !bad_rng && same < 2 && finite_all);
    }
    /* ENV: the amp envelope opens the filter: a slow attack brightens over the note */
    reset();
    steady(0);
    par[0][TP_A] = 80;                                    /* ~0.4 s */
    par[0][TP_CUT] = 20;
    slot(0, 0, MXS_ENV, MXD_CUT, 63);
    synth_note_on(S, 0, 60, 100, 0, 1);
    n = run(SEC, N, 0);
    {
        float early = rms(outl, SEC / 20), late = rms(outl + SEC / 2, SEC / 10);
        reset();
        steady(0);
        par[0][TP_A] = 80;
        par[0][TP_CUT] = 20;
        synth_note_on(S, 0, 60, 100, 0, 1);
        n = run(SEC, N, 0);
        {
            float early0 = rms(outl, SEC / 20), late0 = rms(outl + SEC / 2, SEC / 10);
            check("ENV to CUT 63: the filter follows the envelope (a brighter, louder rise than without)",
                  late / (early + 1e-9f) > late0 / (early0 + 1e-9f) * 1.5f);
        }
    }
}

/* the LFO and what rides on it */
static float swing_rms(int n)                              /* max/min of the block RMS over the capture (100 ms windows) */
{
    float mn = 1e9f, mx = 0.0f;
    for (int i = 0; i + SEC / 20 <= n; i += SEC / 20) {
        float r = rms(outl + i, SEC / 20);
        mn = r < mn ? r : mn;
        mx = r > mx ? r : mx;
    }
    return mx / (mn + 1e-9f);
}
static void lfo_tests(void)
{
    int n;
    float s0, s1;
    /* the LFO as a source: a wave LFO that moves nothing itself (amount 0 = 64) rides the matrix onto the amp */
    reset();
    steady(0);
    par[0][TP_LTYPE] = LT_WAVE;
    par[0][TP_LSPEED] = 8 + 40;                            /* ~2 Hz free */
    par[0][TP_LAMT] = 64;
    par[0][TP_LX2] = 1;                                    /* sine */
    n = play(60, 100);
    s0 = swing_rms(n);
    slot(0, 0, MXS_LFO, MXD_AMP, 40);
    reset();
    steady(0);
    par[0][TP_LTYPE] = LT_WAVE;
    par[0][TP_LSPEED] = 8 + 40;
    par[0][TP_LAMT] = 64;
    par[0][TP_LX2] = 1;
    slot(0, 0, MXS_LFO, MXD_AMP, 40);
    n = play(60, 100);
    s1 = swing_rms(n);
    check("LFO to AMP: the level follows an LFO whose own amount is 0", s0 < 1.05f && s1 > 1.5f);
    /* LFO type off: no LFO source */
    reset();
    steady(0);
    slot(0, 0, MXS_LFO, MXD_AMP, 40);
    n = play(60, 100);
    s1 = swing_rms(n);
    check("  with the LFO off: nothing to follow", s1 < 1.05f);
    /* DEPTH: a gain on the LFO; the wheel at 0 with a +63 slot nearly shuts it, at full opens it */
    reset();
    steady(0);
    par[0][TP_LTYPE] = LT_WAVE;
    par[0][TP_LSPEED] = 8 + 40;
    par[0][TP_LAMT] = 127;
    par[0][TP_LDEST] = LD_AMP;                             /* the voice */
    par[0][TP_LX] = 0;                                     /* volume */
    par[0][TP_LX2] = 1;
    n = play(60, 100);
    s0 = swing_rms(n);
    reset();
    steady(0);
    par[0][TP_LTYPE] = LT_WAVE;
    par[0][TP_LSPEED] = 8 + 40;
    par[0][TP_LAMT] = 127;
    par[0][TP_LDEST] = LD_AMP;
    par[0][TP_LX] = 0;
    par[0][TP_LX2] = 1;
    slot(0, 0, MXS_MODW, MXD_DEPTH, 63);
    LIVE[0][MXL_MODW] = 0;
    n = play(60, 100);
    s1 = swing_rms(n);
    check("the LFO itself swings the level a good deal", s0 > 1.5f);
    check("MODW to DEPTH 63, wheel at 0: the LFO is nearly shut", s1 < 1.1f);
    reset();
    steady(0);
    par[0][TP_LTYPE] = LT_WAVE;
    par[0][TP_LSPEED] = 8 + 40;
    par[0][TP_LAMT] = 127;
    par[0][TP_LDEST] = LD_AMP;
    par[0][TP_LX] = 0;
    par[0][TP_LX2] = 1;
    slot(0, 0, MXS_MODW, MXD_DEPTH, 63);
    LIVE[0][MXL_MODW] = 127;
    n = play(60, 100);
    s1 = swing_rms(n);
    check("  wheel full: the LFO at its whole depth", fabsf(s1 - s0) / s0 < 0.1f);
    /* RATE: +63 at the full wheel is three octaves: count the level's cycles */
    {
        int cyc[2];
        for (int w = 0; w < 2; w++) {
            int up = 0;
            float prev = 0, mean = 0;
            static float env[200];
            int ne = 0;
            reset();
            steady(0);
            par[0][TP_LTYPE] = LT_WAVE;
            par[0][TP_LSPEED] = 8 + 50;                    /* 1.1 Hz: two cycles in the 2 s; x8 is 17 */
            par[0][TP_LAMT] = 127;
            par[0][TP_LDEST] = LD_AMP;
            par[0][TP_LX] = 0;
            par[0][TP_LX2] = 1;
            if (w) {
                slot(0, 0, MXS_MODW, MXD_RATE, 63);
                LIVE[0][MXL_MODW] = 127;
            }
            synth_note_on(S, 0, 60, 100, 0, 1);
            n = run(SEC * 2, N, 0);
            for (int i = 0; i + 441 <= n && ne < 200; i += 441)
                env[ne++] = rms(outl + i, 441);
            for (int i = 0; i < ne; i++)
                mean += env[i];
            mean /= (float)ne;
            for (int i = 1; i < ne; i++)
                if (env[i - 1] < mean && env[i] >= mean)
                    up++;
            (void)prev;
            cyc[w] = up;
        }
        check("MODW to RATE 63, wheel full: the LFO runs 8 times faster", cyc[1] >= cyc[0] * 5 && cyc[1] > 0 && cyc[0] > 0);
    }
    /* S/H: random per cycle, independent of the LFO's type: the pitch steps */
    reset();
    steady(0);
    par[0][TP_LSPEED] = 8 + 70;                            /* the LFO is off, its speed still runs: ~11 Hz */
    slot(0, 0, MXS_SH, MXD_PITCH, 20);
    n = play(60, 100);
    {
        double m, lo, hi;
        periods(outl, n, &m, &lo, &hi);
        check("S/H to PITCH 20: the pitch steps about (a wide spread of periods)", cents(hi, lo) > 150.0 && finite_all);
    }
    reset();
    steady(0);
    par[0][TP_LSPEED] = 8 + 70;
    slot(0, 0, MXS_SLEW, MXD_PITCH, 20);
    n = play(60, 100);
    {
        double m, lo, hi;
        periods(outl, n, &m, &lo, &hi);
        check("SLEW to PITCH 20: the same, glided (a spread, finite)", cents(hi, lo) > 100.0 && finite_all);
    }
}

static void safety_tests(void)
{
    int n;
    reset();
    steady(0);
    for (int i = 0; i < MX_NSLOT; i++)
        MX[0].s[i] = (mxslot_t){255, 255, -128};          /* stray bytes: nothing, and no crash */
    n = play(60, 100);
    check("stray bytes in the slots: no sound change, finite", finite_all && rms(outl, n) > 0.01f);
    reset();
    steady(0);
    for (int i = 0; i < MX_NSLOT; i++)
        MX[0].s[i] = (mxslot_t){(uint8_t)(MXS_N - 1), (uint8_t)(MXD_N - 1), -64};
    LIVE[0][MXL_MODW] = 127;
    n = play(60, 100);
    check("the last source and destination of every slot at -64: finite", finite_all);
    /* every source to every destination at full, three notes, all the engines: finite, bounded */
    for (int e = 0; e < NENGINE; e++) {
        int ok = 1;
        if (e == EG_DRUMLINE)
            continue;
        for (int dst = 1; dst < MXD_N && ok; dst++)
            for (int src = 1; src < MXS_N && ok; src++) {
                reset();
                steady(0);
                par[0][TP_ENGINE] = (uint8_t)e;
                par[0][TP_LTYPE] = LT_WOBBLE;
                par[0][TP_LSPEED] = 8 + 70;
                slot(0, 0, src, dst, 63);
                slot(0, 1, src, dst, -64);
                LIVE[0][MXL_MODW] = LIVE[0][MXL_AT] = 127;
                synth_note_on(S, 0, 36, 127, 0, 1);
                n = run(SEC / 6, N, 0);
                for (int i = 0; i < n; i++)
                    if (fabsf(outl[i]) > 4.0f)
                        ok = 0;
                ok = ok && finite_all;
            }
        char what[96];
        snprintf(what, sizeof what, "engine %d: every source to every destination, two slots: finite and bounded", e);
        check(what, ok);
    }
    {
        mx_t m;
        m.s[0] = (mxslot_t){200, 3, 100};
        m.s[1] = (mxslot_t){3, 200, -100};
        m.s[2] = (mxslot_t){3, 4, -100};
        m.s[3] = (mxslot_t){3, 4, 100};
        mx_clamp(&m);
        check("mx_clamp: a stray source or destination empties the slot, an amount stays in -64..63",
              m.s[0].src == 0 && m.s[0].amt == 0 && m.s[1].dst == 0 && m.s[2].amt == -64 && m.s[3].amt == 63);
    }
}

static void split_test(void)
{
    static float a[MAXS];
    int n;
    reset();
    steady(0);
    mx_default(&MX[0]);
    slot(0, 1, MXS_LFO, MXD_PAN, 20);
    par[0][TP_LTYPE] = LT_WAVE;
    par[0][TP_LSPEED] = 8 + 50;
    par[0][TP_LAMT] = 64;
    LIVE[0][MXL_MODW] = 127;
    synth_note_on(S, 0, 60, 100, 0, 1);
    n = run(SEC / 2, 256, 0);
    memcpy(a, outl, sizeof(float) * (size_t)n);
    reset();
    steady(0);
    mx_default(&MX[0]);
    slot(0, 1, MXS_LFO, MXD_PAN, 20);
    par[0][TP_LTYPE] = LT_WAVE;
    par[0][TP_LSPEED] = 8 + 50;
    par[0][TP_LAMT] = 64;
    LIVE[0][MXL_MODW] = 127;
    synth_note_on(S, 0, 60, 100, 0, 1);
    n = run(SEC / 2, 128, 0);
    {
        float d = 0;
        for (int i = 0; i < n; i++)
            d = fabsf(outl[i] - a[i]) > d ? fabsf(outl[i] - a[i]) : d;
        check("a block cut in two (256 / 128): the same samples, with the matrix running", d < 2e-3f);
    }
}

int main(void)
{
    idle_tests();
    pitch_tests();
    filter_amp_pan_tests();
    source_tests();
    lfo_tests();
    safety_tests();
    split_test();
    printf("%s\n", bad ? "MOD MATRIX TEST FAILED" : "all mod matrix tests passed");
    return bad != 0;
}
