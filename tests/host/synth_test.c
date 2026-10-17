/* SPDX-License-Identifier: GPL-3.0-only */
/* The voices (dsp/synth.c) on their own: the parameter tables and their texts (the OP-XY's printed
 * defaults, "2:1", "809Hz", the organ's eight models, the nine tables, the LFO's knobs per type); every
 * engine and preset renders a note that stays finite and at a sensible level and dies in its release;
 * the five filter types; the voice modes' retrigger rules; the pool's limit and stealing; locks; the
 * LFO (regular vs free, the tremolo's envelope and shapes, the random's unipolar amount, the duck's
 * hold and release); and that splitting a block by voices or by frames changes no sample. Then a
 * benchmark: instructions per sample per voice, 16 voices of each engine (macOS: the task's own
 * instruction count, as fm1-sph's sophie_bench.c; elsewhere clock()).
 *   build/host/synth_test            (tests/host/synth_test.sh builds it) */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#ifdef __APPLE__
#include <unistd.h>
#include <libproc.h>
#include <sys/resource.h>
#endif
#include "synth.h"
#include "drumline.h"

/* dsp/drumline.c is not linked here: its tables, for the drum keys' names and defaults */
const char *const DM_NAME[NDM] = {"kick", "snare", "hihat", "clap", "shaker", "tamb", "resonator", "congas", "tom", "fm"};
const char *const DM_KNOB[NDM][4] = {
    {"pitch", "length", "punch", "tone"}, {"pitch", "length", "snare", "punch"}, {"pitch", "length", "era", "tone"},
    {"tone", "length", "spread", "contour"}, {"color", "length", "attack", "onset"}, {"tone", "length", "shake", "ring"},
    {"pitch", "length", "tone", "strike"}, {"pitch", "length", "slap", "mush"}, {"pitch", "length", "sweep", "rate"},
    {"pitch", "length", "mod", "feedback"}};
const uint8_t DM_DEFAULT[NDM][4] = {{100, 40, 64, 64}, {100, 40, 64, 64}, {93, 23, 64, 88}, {100, 64, 64, 38},
                                    {31, 64, 64, 101}, {93, 23, 64, 88}, {69, 54, 78, 122}, {100, 46, 64, 0},
                                    {101, 46, 38, 64}, {93, 100, 46, 15}};
const char *const PM_NAME[NPM] = {"one-shot", "gated", "loop", "choke 1", "choke 2", "choke 3"};
const uint8_t DL_DEFAULT_KIT[DL_NSOUND][9] = {
    {DM_KICK, 100, 40, 64, 64, 0, 64, 76, 0},  {DM_SNARE, 100, 40, 64, 64, 0, 64, 76, 0}, {DM_HIHAT, 93, 23, 64, 88, 0, 64, 76, 3},
    {DM_HIHAT, 93, 80, 64, 88, 0, 64, 76, 3},  {DM_CLAP, 100, 64, 64, 38, 0, 64, 76, 0},  {DM_TOM, 80, 46, 38, 64, 0, 64, 76, 0},
    {DM_TOM, 110, 46, 38, 64, 0, 64, 76, 0},   {DM_CONGA, 100, 46, 64, 0, 0, 64, 76, 0},  {DM_SHAKER, 31, 64, 64, 101, 0, 64, 76, 0},
    {DM_FM, 93, 100, 46, 15, 0, 64, 76, 0},    {DM_RESO, 69, 54, 78, 122, 0, 64, 76, 0}};

static int bad;
static void check(const char *what, int ok)
{
    printf("%-64s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

#define N 256
static uint8_t par[NTRK][NTP];
static _Alignas(16) uint8_t mem[SYNTH_BYTES];
static synth_t *S = (synth_t *)mem;
static float L[N], R[N], S1[N], S2[N], ST[N];
static float last_rms;                           /* the last block's RMS (left) */

static int finite_f(float x) { return x == x && x < 1e30f && x > -1e30f; }
static float fabs_f(float x) { return x < 0.0f ? -x : x; }

/* render n frames in one range, return the block's peak and whether all was finite */
static float block(uint32_t n, int *finite)
{
    uint32_t i;
    float pk = 0.0f;
    memset(L, 0, sizeof L);
    memset(R, 0, sizeof R);
    memset(S1, 0, sizeof S1);
    memset(S2, 0, sizeof S2);
    memset(ST, 0, sizeof ST);
    synth_prepare(S, n);
    synth_render(S, 0, NVOICE, L, R, S1, S2, ST, n);
    last_rms = 0.0f;
    for (i = 0; i < n; i++) {
        if (!finite_f(L[i]) || !finite_f(R[i]) || !finite_f(S1[i]))
            *finite = 0;
        if (fabs_f(L[i]) > pk)
            pk = fabs_f(L[i]);
        if (fabs_f(R[i]) > pk)
            pk = fabs_f(R[i]);
        last_rms += L[i] * L[i];
    }
    last_rms = (float)sqrt((double)(last_rms / (float)n));
    return pk;
}

static void reset(void)
{
    int t;
    for (t = 0; t < NTRK; t++)
        sound_default(par[t], EG_SIMPLE);
    synth_init(S, par);
    synth_tempo(S, 120.0f);
}

static void load_preset(int t, int k)
{
    memcpy(par[t], PRESETS[k].par, NTP_SOUND);
}

static float db(float x) { return x > 0.0f ? 20.0f * (float)log10((double)x) : -200.0f; }

/* a one-second note, then its release: peak in range, finite, silent at the end */
static int note_test(const char *name, int t, float *peak_out, int level_check)
{
    int finite = 1, b, ok;
    float pk = 0.0f, tail = 0.0f;
    synth_all_off(S, -1);
    for (b = 0; b < 20; b++)
        block(N, &finite);                       /* let any old release die */
    synth_note_on(S, t, 60, 100, 0, 1);
    for (b = 0; b < 172; b++) {                  /* 1 s */
        float p = block(N, &finite);
        if (p > pk)
            pk = p;
    }
    synth_note_off(S, t, 1);
    for (b = 0; b < 172 * 12; b++) {             /* up to 12 s of release (10 s is the longest) */
        int i;
        block(N, &finite);
        if (!synth_sounding(S, 0, NVOICE)) {     /* the end of the last block: silence by then */
            tail = 0.0f;
            for (i = N - 16; i < N; i++)
                if (fabs_f(L[i]) > tail)
                    tail = fabs_f(L[i]);
            break;
        }
    }
    ok = finite && (pk > 0.0316f || !level_check) && pk <= 1.0f && !synth_sounding(S, 0, NVOICE) && tail < 1e-3f;
    if (!ok)
        printf("    %s: peak %.1f dBFS finite %d sounding %d tail %g\n", name, (double)db(pk), finite,
               synth_sounding(S, 0, NVOICE), (double)tail);
    *peak_out = pk;
    return ok;
}

/* the level swing (max / min block RMS) over nblocks of a held note, after skipping some */
static float swing(int skip, int nblocks, int *finite)
{
    int b;
    float mn = 1e9f, mx = 0.0f;
    for (b = 0; b < skip; b++)
        block(N, finite);
    for (b = 0; b < nblocks; b++) {
        float p;
        block(N, finite);
        p = last_rms;
        if (p < mn)
            mn = p;
        if (p > mx)
            mx = p;
    }
    return mn > 0.0f ? mx / mn : 1e9f;
}

/* the first block's samples of a one-note render into out[N] (a fingerprint of the sound) */
static void fingerprint(int t, float *out)
{
    int finite = 1, b;
    synth_all_off(S, -1);
    for (b = 0; b < 400 && synth_sounding(S, 0, NVOICE); b++)
        block(N, &finite);
    synth_note_on(S, t, 60, 100, 0, 1);
    for (b = 0; b < 20; b++)
        block(N, &finite);
    memcpy(out, L, sizeof L);
    synth_note_off(S, t, 1);
}
static int differ(const float *a, const float *b)
{
    int i;
    for (i = 0; i < N; i++)
        if (fabs_f(a[i] - b[i]) > 1e-4f)
            return 1;
    return 0;
}

#ifdef __APPLE__
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

static void bench(void)
{
    int e;
    printf("\nbenchmark: 16 voices, svf, instructions per sample per voice (mean / worst block)\n");
    for (e = 0; e < EG_DRUMLINE; e++) {
        int t, b, finite = 1;
        uint64_t total = 0, worst = 0;
        reset();
        for (t = 0; t < NTRK; t++) {
            sound_default(par[t], e);
            par[t][TP_FTYPE] = FT_SVF;
            par[t][TP_E4] = e == EG_ORGAN ? 8 : 100;   /* stereo where the engine has it: the dear case */
            par[t][TP_E3] = e == EG_ORGAN ? 8 : 60;
            par[t][TP_LTYPE] = LT_VALUE;
            par[t][TP_LDEST] = LD_FILTER;
            par[t][TP_LX] = 0;
        }
        for (t = 0; t < NVOICE; t++)
            synth_note_on(S, t % NTRK, 48 + t * 2, 100, 0, (uint32_t)t);
        for (b = 0; b < 400; b++) {
            uint64_t i0 = instr_now(), di;
            block(N, &finite);
            di = instr_now() - i0;
            if (b >= 20) {
                total += di;
                if (di > worst)
                    worst = di;
            }
        }
#ifdef __APPLE__
        printf("  %-10s %6.1f / %6.1f   (%d voices sounding)\n", EG_NAME[e], (double)total / (380.0 * N * NVOICE),
               (double)worst / (double)(N * NVOICE), synth_voices_used(S));
#else
        printf("  %-10s %6.3f us / sample / voice (clock)\n", EG_NAME[e],
               (double)total / (380.0 * N * NVOICE) / (double)CLOCKS_PER_SEC * 1e6);
#endif
    }
}

int main(void)
{
    int e, k, t, finite = 1, b;
    float pk;
    char num[16], unit[8];
    static float fp1[N], fp2[N];

    {
        int synth_sizeof(void);
        printf("synth_t: %d of %d bytes\n", synth_sizeof(), SYNTH_BYTES);
    }
    /* the parameter tables */
    check("every parameter has info and text", 1);
    for (k = 0; k < NTP; k++) {
        const tp_info_t *i = tp_info(k, EG_STACK);
        int v;
        if (!i->name || i->lo > i->hi || i->def < i->lo || i->def > i->hi) {
            printf("    parameter %d\n", k);
            check("parameter info ranges", 0);
            break;
        }
        for (v = i->lo; v <= i->hi; v++)
            tp_text(k, v, EG_STACK, num, unit);
    }
    for (e = 0; e < NENGINE; e++)
        for (k = 0; k < 4; k++) {
            const tp_info_t *i = tp_info(TP_E1 + k, e);
            int v;
            if (!i->name[0] && e != EG_DRUMLINE) {
                check("engine knob names", 0);
                break;
            }
            if (i->def < i->lo || i->def > i->hi)
                check("engine knob defaults in range", 0);
            for (v = i->lo; v <= i->hi; v++)
                tp_text(TP_E1 + k, v, e, num, unit);
        }
    tp_text(TP_LSPEED, 3, 0, num, unit);
    check("LFO speed 3 shows 1/4", !strcmp(num, "1/4"));
    tp_text(TP_LSPEED, 0, 0, num, unit);
    check("LFO speed 0 shows 2/1", !strcmp(num, "2/1"));
    tp_text(TP_LSPEED, 8, 0, num, unit);
    check("LFO speed 8 shows 0.1 Hz", !strcmp(num, "0.1") && !strcmp(unit, "Hz"));
    tp_text(TP_LSPEED, 127, 0, num, unit);
    check("LFO speed 127 shows 30.0 Hz", !strcmp(num, "30.0") && !strcmp(unit, "Hz"));
    tp_text(TP_A, 0, 0, num, unit);
    check("attack 0 shows 1 ms", !strcmp(num, "1") && !strcmp(unit, "ms"));
    tp_text(TP_LDEST, LD_FILTER, 0, num, unit);
    check("LFO destination shows the module", !strcmp(num, "filter"));
    tp_text(TP_LDEST, LD_FILTER + LD_FREE, 0, num, unit);
    check("LFO destination's free version", !strcmp(num, "filter free"));
    tp_text(TP_DRUM(2, DP_MODEL), 2, EG_DRUMLINE, num, unit);
    check("drum model text", !strcmp(num, "hihat"));
    {
        uint8_t p[NTP];
        sound_default(p, EG_DRUMLINE);
        check("a drum key's knobs take the model's names", !strcmp(tp_info_par(TP_DRUM(4, DP_K3), p)->name, "spread"));
        check("a drum key's default kit", p[TP_DRUM(0, DP_MODEL)] == DM_KICK && p[TP_DRUM(0, DP_VOLUME)] == 76);
        tp_text_par(TP_DRUM(0, DP_VOLUME), 76, p, num, unit);
        check("drum volume 76 is 0 dB", !strcmp(num, "0") && !strcmp(unit, "dB"));
        check("a drum track's filter is dynamic", p[TP_FTYPE] == FT_DYNAMIC && !strcmp(tp_info(TP_CUT, EG_DRUMLINE)->name, "lowcut"));
        check("a drum track's envelope is lag / attack / random / length", !strcmp(tp_info(TP_A, EG_DRUMLINE)->name, "lag"));
    }
    {
        uint8_t p[NTP];
        sound_default(p, EG_STACK);
        p[TP_PLAYER] = PL_STRUM;
        check("player knob names follow the player", !strcmp(tp_info_par(TP_P1, p)->name, "roll"));
        p[TP_PLAYER] = PL_ARP;
        check("the arp's second page", !strcmp(tp_info_par(TP_AP2, p)->name, "style"));
        p[TP_LTYPE] = LT_VALUE;
        p[TP_LDEST] = LD_FILTER;
        tp_text_par(TP_LX, 0, p, num, unit);
        check("value LFO: the filter module's knob 0 is cutoff", !strcmp(num, "cutoff"));
        p[TP_LDEST] = LD_SYNTH + LD_FREE;
        tp_text_par(TP_LX, 1, p, num, unit);
        check("value LFO: the synth module's knobs are the engine's", !strcmp(num, "ratio"));
        p[TP_LDEST] = LD_AMP;
        tp_text_par(TP_LX, 1, p, num, unit);
        check("value LFO: the amp module's knob 1 is pitch", !strcmp(num, "pitch"));
        tp_text_par(TP_LX2, 2, p, num, unit);
        check("value LFO: shape 2 is ramp", !strcmp(num, "ramp") && !strcmp(tp_info_par(TP_LX2, p)->name, "shape"));
        p[TP_LTYPE] = LT_DUCK;
        check("duck: source / amount / hold / release", !strcmp(tp_info_par(TP_LSPEED, p)->name, "source")
                                                             && !strcmp(tp_info_par(TP_LDEST, p)->name, "hold")
                                                             && !strcmp(tp_info_par(TP_LX, p)->name, "release"));
        tp_text_par(TP_LSPEED, 11, p, num, unit);
        check("duck source 11 is the metronome", !strcmp(num, "metro"));
        tp_text_par(TP_LSPEED, 8, p, num, unit);
        check("duck source 8 is the conductor", !strcmp(num, "conductor"));
        p[TP_LTYPE] = LT_TREMOLO;
        check("tremolo: vibrato / volume / envelope / shape", !strcmp(tp_info_par(TP_LAMT, p)->name, "vibrato")
                                                               && !strcmp(tp_info_par(TP_LDEST, p)->name, "volume")
                                                               && !strcmp(tp_info_par(TP_LX, p)->name, "envelope")
                                                               && !strcmp(tp_info_par(TP_LX2, p)->name, "shape"));
        tp_text_par(TP_LX2, 2, p, num, unit);
        check("tremolo shape 2 is exponential", !strcmp(num, "expo"));
        tp_text_par(TP_LX, 32, p, num, unit);
        check("envelope 32 shows in 50 %", !strcmp(num, "in 50") && !strcmp(unit, "%"));
        tp_text_par(TP_LX, 64, p, num, unit);
        check("envelope 64 shows off", !strcmp(num, "off"));
        p[TP_LTYPE] = LT_RANDOM;
        check("random: amount is unipolar, envelope on x2", tp_info_par(TP_LAMT, p)->kind == PK_PCT
                                                               && !strcmp(tp_info_par(TP_LX2, p)->name, "envelope"));
    }
    /* the engines' printed defaults and displays */
    {
        static const uint8_t PRINTED[8][4] = {{29, 33, 20, 99}, {15, 2, 5, 22}, {0, 46, 99, 0}, {40, 81, 25, 20},
                                              {0, 0, 41, 53},   {1, 3, 1, 4},   {13, 8, 99, 18}, {0, 9, 32, 0}};
        int ok = 1;
        for (e = 0; e < EG_DRUMLINE; e++) {
            uint8_t p[NTP];
            sound_default(p, e);
            for (k = 0; k < 4; k++) {
                int shown;
                tp_text(TP_E1 + k, p[TP_E1 + k], e, num, unit);
                shown = atoi(num);
                if (e == EG_WAVETABLE && k == 0)
                    shown = strcmp(num, "shapes") == 0 ? 0 : -1;
                if (shown != PRINTED[e][k]) {
                    printf("    %s knob %d shows %s, the notebook prints %d\n", EG_NAME[e], k + 1, num, PRINTED[e][k]);
                    ok = 0;
                }
            }
        }
        check("the engines' defaults display as the notebook prints them", ok);
        tp_text(TP_E1, 51, EG_ORBIT, num, unit);
        check("engine knobs show two digits 00..99", !strcmp(num, "40") && !unit[0]);
        tp_text(TP_E1, 0, EG_ORBIT, num, unit);
        check("engine knob 0 shows 00", !strcmp(num, "00"));
        tp_text(TP_E2, 2, EG_STACK, num, unit);
        check("stack ratio shows N:1", !strcmp(num, "2:1") && tp_info(TP_E2, EG_STACK)->lo == 1 && tp_info(TP_E2, EG_STACK)->hi == 8);
        check("organ knobs are discrete 1..8", tp_info(TP_E1, EG_ORGAN)->lo == 1 && tp_info(TP_E1, EG_ORGAN)->hi == 8
                                                   && tp_info(TP_E4, EG_ORGAN)->hi == 8);
        check("nine wavetables", tp_info(TP_E1, EG_WAVETABLE)->hi == 8);
        tp_text(TP_E1, 8, EG_WAVETABLE, num, unit);
        check("the last table is chirp", !strcmp(num, "chirp"));
        tp_text(TP_E1, 3, EG_WAVETABLE, num, unit);
        check("table 3 is vocal", !strcmp(num, "vocal"));
    }
    /* the filter and play page texts */
    tp_text(TP_CUT, 0, 0, num, unit);
    check("cutoff 0 shows 50 Hz", !strcmp(num, "50") && !strcmp(unit, "Hz"));
    tp_text(TP_CUT, 127, 0, num, unit);
    check("cutoff 127 shows 20.0 kHz", !strcmp(num, "20.0") && !strcmp(unit, "kHz"));
    tp_text(TP_CUT, 59, 0, num, unit);
    check("cutoff 59 shows about 800Hz", !strcmp(unit, "Hz") && atoi(num) > 780 && atoi(num) < 840);
    tp_text(TP_FTYPE, FT_LADDER, 0, num, unit);
    check("filter types are the OP-XY's names", !strcmp(num, "ladder"));
    tp_text(TP_BEND, 0, 0, num, unit);
    check("bend 0 is off", !strcmp(num, "off"));
    tp_text(TP_BEND, 8, 0, num, unit);
    check("bend 8 is an octave", !strcmp(num, "octave") && tp_bend_semi(8) == 12 && tp_bend_semi(5) == 5);
    tp_text(TP_PORTA, 0, 0, num, unit);
    check("portamento 0 is off", !strcmp(num, "off"));
    tp_text(TP_PORTA, 50, 0, num, unit);
    check("portamento shows 0..127", !strcmp(num, "50") && !unit[0]);

    check("64 presets, 8 an engine", NPRESETS == 64);
    for (e = 0; e < EG_DRUMLINE; e++) {
        int n = 0;
        for (k = 0; k < NPRESETS; k++)
            n += PRESETS[k].par[TP_ENGINE] == e;
        if (n != 8) {
            printf("    engine %s has %d presets\n", EG_NAME[e], n);
            check("8 presets an engine", 0);
        }
        if (preset_first(e) < 0 || PRESETS[preset_first(e)].par[TP_ENGINE] != e)
            check("preset_first", 0);
    }
    {
        int ok = 1;
        for (k = 0; k < NPRESETS; k++) {
            uint8_t p[NTP];
            int q;
            memcpy(p, PRESETS[k].par, NTP_SOUND);
            memset(p + NTP_SOUND, 0, NTP - NTP_SOUND);
            for (q = 0; q < NTP_SOUND; q++) {
                const tp_info_t *i = tp_info_par(q, p);
                if ((i->hi > i->lo || i->kind == PK_NAMES) && (p[q] < i->lo || p[q] > i->hi)) {
                    printf("    preset %s: parameter %d = %d outside %d..%d\n", PRESETS[k].name, q, p[q], i->lo, i->hi);
                    ok = 0;
                }
            }
        }
        check("every preset value is inside its knob's range", ok);
    }

    /* every engine, default sound */
    reset();
    for (e = 0; e < EG_DRUMLINE; e++) {
        char what[64];
        sound_default(par[0], e);
        sprintf(what, "engine %s renders a note (default sound)", EG_NAME[e]);
        check(what, note_test(what, 0, &pk, 1));
        printf("    %-10s peak %6.1f dBFS\n", EG_NAME[e], (double)db(pk));
    }
    for (e = 0; e < EG_DRUMLINE; e++) {          /* and with every filter type, resonance up, knobs at the ends */
        int ft, ok = 1;
        for (ft = 0; ft < NFT; ft++) {
            sound_default(par[0], e);
            par[0][TP_FTYPE] = (uint8_t)ft;
            par[0][TP_RES] = 127;
            par[0][TP_CUT] = (uint8_t)(ft == FT_ZHP ? 30 : 110);   /* a high-pass at 8 kHz would leave nothing */
            par[0][TP_E1] = par[0][TP_E2] = par[0][TP_E3] = par[0][TP_E4] = (uint8_t)(ft & 1 ? 127 : 0);
            ok &= note_test(EG_NAME[e], 0, &pk, 0);   /* some corners are quiet by design */
        }
        check(ok ? "engine with every filter type, knobs at the ends" : EG_NAME[e], ok);
    }
    {   /* the filter types behave as their names say, on a saw at a low cutoff */
        float lv[NFT];
        int ft;
        for (ft = 0; ft < NFT; ft++) {
            reset();
            sound_default(par[0], EG_SIMPLE);
            par[0][TP_E4] = 0;
            par[0][TP_FTYPE] = (uint8_t)ft;
            par[0][TP_CUT] = 20;
            par[0][TP_RES] = 0;
            synth_note_on(S, 0, 60, 100, 0, 1);
            for (b = 0; b < 30; b++)
                block(N, &finite);
            lv[ft] = block(N, &finite);
        }
        printf("    saw at cutoff 20: svf %.1f ladder %.1f z lowpass %.1f z hipass %.1f dynamic %.1f dBFS\n",
               (double)db(lv[0]), (double)db(lv[1]), (double)db(lv[2]), (double)db(lv[3]), (double)db(lv[4]));
        check("svf (24 dB) is darker than z lowpass (12 dB)", lv[FT_SVF] < lv[FT_ZLP] * 0.8f);
        check("ladder is darker than z lowpass", lv[FT_LADDER] < lv[FT_ZLP] * 0.9f);
        check("dynamic on a synth track is a bypass (louder than any low-pass)", lv[FT_DYNAMIC] > lv[FT_ZLP] * 1.5f);
        check("z hipass at a low cutoff keeps most of the saw", lv[FT_ZHP] > lv[FT_ZLP] * 1.5f);
        check("every filter type stays finite", finite);
        reset();                                 /* the ladder with its feedback at full self-limits */
        sound_default(par[0], EG_SIMPLE);
        par[0][TP_FTYPE] = FT_LADDER;
        par[0][TP_RES] = 127;
        par[0][TP_CUT] = 80;
        synth_note_on(S, 0, 48, 127, 0, 1);
        pk = 0.0f;
        for (b = 0; b < 100; b++) {
            float p = block(N, &finite);
            if (p > pk)
                pk = p;
        }
        check("the ladder at full resonance stays finite and inside full scale", finite && pk <= 1.0f);
    }

    /* every preset */
    {
        int ok = 1;
        float lo = 1.0f, hi = 0.0f;
        for (k = 0; k < NPRESETS; k++) {
            load_preset(0, k);
            if (!note_test(PRESETS[k].name, 0, &pk, 1))
                ok = 0;
            if (pk < lo)
                lo = pk;
            if (pk > hi)
                hi = pk;
        }
        check("every preset renders a 1 s note: finite, -30..0 dBFS, release ends", ok);
        printf("    preset peaks %.1f .. %.1f dBFS\n", (double)db(lo), (double)db(hi));
    }

    /* the organ's eight models and the nine tables each sound, and differently */
    {
        int ok = 1;
        reset();
        sound_default(par[0], EG_ORGAN);
        for (k = 1; k <= 8; k++) {
            par[0][TP_E1] = (uint8_t)k;
            fingerprint(0, k & 1 ? fp1 : fp2);
            if (k > 1 && !differ(fp1, fp2))
                ok = 0;
        }
        check("organ: eight models, each its own sound", ok);
        ok = 1;
        sound_default(par[0], EG_ORGAN);
        par[0][TP_E3] = 8;
        par[0][TP_E4] = 8;
        synth_note_on(S, 0, 60, 100, 0, 1);
        check("organ: tremolo amount 8 at speed 8 moves the level", swing(20, 60, &finite) > 1.3f);
        synth_all_off(S, -1);
        sound_default(par[0], EG_WAVETABLE);
        for (k = 0; k < 9; k++) {
            par[0][TP_E1] = (uint8_t)k;
            fingerprint(0, k & 1 ? fp1 : fp2);
            if (k > 0 && !differ(fp1, fp2))
                ok = 0;
        }
        check("wavetable: nine tables, each its own sound", ok);
    }

    /* a six-note chord of the loudest kind stays inside full scale */
    reset();
    sound_default(par[0], EG_ORGAN);
    par[0][TP_E1] = 8;
    par[0][TP_VOLUME] = 127;
    synth_all_off(S, -1);
    for (k = 0; k < 6; k++)
        synth_note_on(S, 0, 48 + k * 4, 127, 0, (uint32_t)k);
    pk = 0.0f;
    for (b = 0; b < 100; b++) {
        float p = block(N, &finite);
        if (p > pk)
            pk = p;
    }
    printf("    six church organ notes at full velocity and volume: %.1f dBFS\n", (double)db(pk));
    check("a six-note chord at full volume stays finite", finite);

    /* mono / legato */
    reset();
    sound_default(par[0], EG_SIMPLE);
    par[0][TP_VMODE] = VM_MONO;
    par[0][TP_A] = 100;                          /* a slow attack: a retrigger shows as a dip */
    synth_note_on(S, 0, 60, 100, 0, 1);
    for (b = 0; b < 40; b++)
        block(N, &finite);
    check("mono: one voice", synth_voices_used(S) == 1);
    synth_note_on(S, 0, 64, 100, 0, 2);
    check("mono: a second note takes no new voice", synth_voices_used(S) == 1);
    {
        float a = block(N, &finite), c;
        for (b = 0; b < 40; b++)
            c = block(N, &finite);
        check("mono: the new note retriggers (level climbs again)", c > a);
        synth_note_off(S, 0, 2);
        for (b = 0; b < 4; b++)
            block(N, &finite);
        check("mono: releasing the top note returns to the held one", synth_sounding(S, 0, NVOICE));
        synth_note_off(S, 0, 1);
        for (b = 0; b < 400 && synth_sounding(S, 0, NVOICE); b++)
            block(N, &finite);
        check("mono: releasing the last note ends the voice", !synth_sounding(S, 0, NVOICE));
    }
    reset();
    sound_default(par[0], EG_SIMPLE);
    par[0][TP_VMODE] = VM_LEGATO;
    par[0][TP_A] = 100;
    synth_note_on(S, 0, 60, 100, 0, 1);
    for (b = 0; b < 100; b++)
        block(N, &finite);
    {
        float before = block(N, &finite), after, mn = 1.0f;
        synth_note_on(S, 0, 67, 100, 0, 2);
        for (b = 0; b < 10; b++) {
            after = block(N, &finite);
            if (after < mn)
                mn = after;
        }
        check("legato: a note on a held note does not retrigger", mn > before * 0.7f && synth_voices_used(S) == 1);
    }

    /* the pool: 16 voices, stealing */
    reset();
    for (t = 0; t < NTRK; t++)
        sound_default(par[t], EG_STACK);
    for (k = 0; k < 16; k++)
        synth_note_on(S, k % NTRK, 40 + k, 100, 0, (uint32_t)k);
    check("16 notes: 16 voices", synth_voices_used(S) == 16);
    synth_note_on(S, 0, 90, 100, 0, 100);
    check("a 17th note steals: still 16 voices", synth_voices_used(S) == 16);
    for (b = 0; b < 10; b++)
        block(N, &finite);
    {
        synth_note_off(S, 0, 100);                 /* the stolen voice plays the new note after its fade */
        for (b = 0; b < 2; b++)
            block(N, &finite);
        check("the stolen voice took the new note and stays finite", finite);
    }
    reset();
    for (k = 0; k < 8; k++)
        synth_note_on(S, 0, 40 + k, 100, 0, (uint32_t)k);
    check("one track takes at most NVOICE_TRK voices (the rest steal its own)", synth_voices_used(S) <= NVOICE_TRK + 1);
    synth_limit(S, 4);
    synth_all_off(S, -1);
    for (b = 0; b < 400 && synth_sounding(S, 0, NVOICE); b++)
        block(N, &finite);
    for (k = 0; k < 8; k++)
        synth_note_on(S, k, 40 + k, 100, 0, (uint32_t)k);
    check("synth_limit(4): four voices", synth_voices_used(S) == 4);
    synth_limit(S, NVOICE);

    /* locks override the knob and release */
    reset();
    sound_default(par[0], EG_SIMPLE);
    par[0][TP_CUT] = 127;
    par[0][TP_A] = 0;
    par[0][TP_FTYPE] = FT_SVF;
    {
        float open, locked, back;
        synth_note_on(S, 0, 48, 100, 0, 1);
        for (b = 0; b < 20; b++)
            block(N, &finite);
        open = block(N, &finite);
        synth_lock(S, 0, TP_CUT, 10);
        for (b = 0; b < 20; b++)
            block(N, &finite);
        locked = block(N, &finite);
        synth_lock(S, 0, TP_CUT, LOCK_NONE);
        for (b = 0; b < 20; b++)
            block(N, &finite);
        back = block(N, &finite);
        check("a cutoff lock closes the filter (quieter)", locked < open * 0.5f);
        check("LOCK_NONE returns to the knob", back > locked * 1.5f);
    }

    /* the value LFO moves a parameter */
    reset();
    sound_default(par[0], EG_SIMPLE);
    par[0][TP_CUT] = 40;
    par[0][TP_LTYPE] = LT_VALUE;
    par[0][TP_LSPEED] = 60;                      /* a few Hz */
    par[0][TP_LAMT] = 127;
    par[0][TP_LDEST] = LD_FILTER;
    par[0][TP_LX] = 0;
    par[0][TP_LX2] = 1;
    synth_note_on(S, 0, 48, 100, 0, 1);
    check("a value LFO on the filter's cutoff moves the level", swing(40, 172, &finite) > 1.3f);
    {   /* on an engine knob with a narrow range: the organ's type sweeps its 1..8 without leaving it */
        int ok = 1;
        reset();
        sound_default(par[0], EG_ORGAN);
        par[0][TP_LTYPE] = LT_VALUE;
        par[0][TP_LSPEED] = 60;
        par[0][TP_LAMT] = 127;
        par[0][TP_LDEST] = LD_SYNTH;
        par[0][TP_LX] = 0;
        synth_note_on(S, 0, 48, 100, 0, 1);
        for (b = 0; b < 200; b++)
            block(N, &finite);
        ok = finite;
        check("a value LFO on a discrete knob stays finite", ok);
    }
    {   /* on the pitch: the amp module's knob 1 */
        reset();
        sound_default(par[0], EG_SIMPLE);
        par[0][TP_LTYPE] = LT_VALUE;
        par[0][TP_LSPEED] = 40;
        par[0][TP_LAMT] = 127;
        par[0][TP_LDEST] = LD_AMP;
        par[0][TP_LX] = 1;
        par[0][TP_LX2] = 1;
        synth_note_on(S, 0, 48, 100, 0, 1);
        for (b = 0; b < 10; b++)
            block(N, &finite);
        fingerprint(0, fp1);
        par[0][TP_LAMT] = 64;
        fingerprint(0, fp2);
        check("a value LFO on the pitch changes the sound", differ(fp1, fp2) && finite);
    }
    {   /* regular vs free: a square on the volume, +100 %, from a half-level knob */
        float reg, fre;
        int pass;
        for (pass = 0; pass < 2; pass++) {
            float a, c;
            reset();
            sound_default(par[0], EG_SIMPLE);
            par[0][TP_VOLUME] = 64;
            par[0][TP_A] = 0;
            par[0][TP_LTYPE] = LT_VALUE;
            par[0][TP_LSPEED] = 12;              /* slow: 0.13 Hz, a half cycle is about 4 s */
            par[0][TP_LAMT] = 127;
            par[0][TP_LDEST] = (uint8_t)(LD_AMP + (pass ? LD_FREE : 0));
            par[0][TP_LX] = 0;
            par[0][TP_LX2] = 0;                  /* square: +1 for the first half cycle */
            synth_note_on(S, 0, 48, 100, 0, 1);
            for (b = 0; b < 20; b++)
                block(N, &finite);
            a = block(N, &finite);
            synth_note_off(S, 0, 1);
            for (b = 0; b < 172 * 5; b++)        /* 5 s on: the LFO is in its low half now */
                block(N, &finite);
            synth_note_on(S, 0, 48, 100, 0, 2);
            for (b = 0; b < 20; b++)
                block(N, &finite);
            c = block(N, &finite);
            if (pass == 0)
                reg = c / a;
            else
                fre = c / a;
        }
        printf("    second note's level over the first's: regular %.2f, free %.2f\n", (double)reg, (double)fre);
        check("regular: the LFO restarts on a note (the same level)", reg > 0.9f && reg < 1.1f);
        check("free: the LFO runs on (a different level)", fre < 0.6f);
    }
    /* tremolo: volume, vibrato, envelope, shapes */
    reset();
    sound_default(par[0], EG_ORGAN);
    par[0][TP_LTYPE] = LT_TREMOLO;
    par[0][TP_LSPEED] = 70;
    par[0][TP_LAMT] = 64;
    par[0][TP_LDEST] = 127;
    par[0][TP_LX] = 64;
    synth_note_on(S, 0, 48, 100, 0, 1);
    check("a tremolo's volume amount moves the level", swing(40, 172, &finite) > 1.5f);
    {
        int sh, ok = 1;
        for (sh = 0; sh < 5; sh++) {
            reset();
            sound_default(par[0], EG_ORGAN);
            par[0][TP_LTYPE] = LT_TREMOLO;
            par[0][TP_LSPEED] = 70;
            par[0][TP_LAMT] = 64;
            par[0][TP_LDEST] = 127;
            par[0][TP_LX] = 64;
            par[0][TP_LX2] = (uint8_t)sh;
            synth_note_on(S, 0, 48, 100, 0, 1);
            if (swing(40, 172, &finite) < 1.3f || !finite)
                ok = 0;
        }
        check("every tremolo shape moves the level", ok);
    }
    {
        reset();
        sound_default(par[0], EG_ORGAN);
        par[0][TP_LTYPE] = LT_TREMOLO;
        par[0][TP_LSPEED] = 90;
        par[0][TP_LAMT] = 127;                   /* vibrato only */
        par[0][TP_LDEST] = 64;
        synth_note_on(S, 0, 48, 100, 0, 1);
        for (b = 0; b < 10; b++)
            block(N, &finite);
        fingerprint(0, fp1);
        par[0][TP_LAMT] = 64;
        fingerprint(0, fp2);
        check("a tremolo's vibrato amount changes the sound", differ(fp1, fp2));
    }
    {
        float early, late;
        reset();
        sound_default(par[0], EG_ORGAN);
        par[0][TP_LTYPE] = LT_TREMOLO;
        par[0][TP_LSPEED] = 90;
        par[0][TP_LAMT] = 64;
        par[0][TP_LDEST] = 127;
        par[0][TP_LX] = 0;                       /* fade in over 3 s */
        synth_note_on(S, 0, 72, 100, 0, 1);
        early = swing(8, 20, &finite);           /* 0.05 .. 0.16 s */
        late = swing(172 * 3, 40, &finite);      /* after 3 s */
        printf("    tremolo fade in: swing %.2f early, %.2f late\n", (double)early, (double)late);
        check("tremolo envelope: fade in (little early, full late)", early < 1.15f && late > 1.5f);
        reset();
        sound_default(par[0], EG_ORGAN);
        par[0][TP_LTYPE] = LT_TREMOLO;
        par[0][TP_LSPEED] = 90;
        par[0][TP_LAMT] = 64;
        par[0][TP_LDEST] = 127;
        par[0][TP_LX] = 80;                      /* fade out over about 2.2 s */
        synth_note_on(S, 0, 72, 100, 0, 1);
        early = swing(8, 40, &finite);
        late = swing(172 * 3, 40, &finite);
        printf("    tremolo fade out: swing %.2f early, %.2f late\n", (double)early, (double)late);
        check("tremolo envelope: fade out (full early, none late)", early > 1.5f && late < 1.15f);
    }
    /* random: unipolar */
    {
        float quiet, loud;
        reset();
        sound_default(par[0], EG_ORGAN);
        par[0][TP_VOLUME] = 0;
        synth_note_on(S, 0, 48, 100, 0, 1);
        for (b = 0; b < 20; b++)
            block(N, &finite);
        quiet = block(N, &finite);
        par[0][TP_LTYPE] = LT_RANDOM;
        par[0][TP_LSPEED] = 90;
        par[0][TP_LAMT] = 127;
        par[0][TP_LDEST] = LD_AMP + LD_FREE;
        par[0][TP_LX] = 0;
        par[0][TP_LX2] = 64;
        loud = 0.0f;
        for (b = 0; b < 100; b++) {
            float p = block(N, &finite);
            if (p > loud)
                loud = p;
        }
        check("random: a 100 % amount on a volume of 0 only adds (audible)", quiet < 1e-4f && loud > 0.01f && finite);
        par[0][TP_LAMT] = 0;
        for (b = 0; b < 40; b++)
            block(N, &finite);
        check("random: amount 0 adds nothing", block(N, &finite) < 1e-4f);
    }
    /* duck: source, hold, release */
    {
        float a, c, held, rel;
        reset();
        sound_default(par[0], EG_ORGAN);
        par[0][TP_LTYPE] = LT_DUCK;
        par[0][TP_LSPEED] = 3;                   /* source: track 4 */
        par[0][TP_LAMT] = 127;
        par[0][TP_LDEST] = 0;                    /* hold 1 ms */
        par[0][TP_LX] = 76;                      /* release about 250 ms */
        synth_note_on(S, 0, 48, 100, 0, 1);
        for (b = 0; b < 40; b++)
            block(N, &finite);
        a = block(N, &finite);
        synth_duck_trigger(S, 5);
        block(N, &finite);
        c = block(N, &finite);
        check("a duck trigger from another source does nothing", c > a * 0.9f);
        synth_duck_trigger(S, 3);
        block(N, &finite);                       /* the gain ramps down inside this block */
        c = block(N, &finite);
        check("a duck trigger from its source drops the level", c < a * 0.3f);
        for (b = 0; b < 172; b++)
            c = block(N, &finite);
        check("the duck recovers after its release", c > a * 0.8f);
        par[0][TP_LDEST] = 115;                  /* hold about 3 s */
        synth_duck_trigger(S, 3);
        for (b = 0; b < 172; b++)
            held = block(N, &finite);
        check("a long hold keeps the duck down a second later", held < a * 0.3f);
        par[0][TP_LDEST] = 0;
        par[0][TP_LX] = 0;                       /* release 1 ms */
        for (b = 0; b < 172 * 4; b++)
            block(N, &finite);
        synth_duck_trigger(S, 3);
        block(N, &finite);
        for (b = 0; b < 8; b++)
            rel = block(N, &finite);
        check("a short release lets go within 50 ms", rel > a * 0.8f);
        par[0][TP_LSPEED] = 11;
        par[0][TP_LX] = 76;
        synth_duck_trigger(S, 11);
        block(N, &finite);
        c = block(N, &finite);
        check("the metronome (source 11) ducks too", c < a * 0.3f);
    }

    /* perf: mute, pitch */
    reset();
    sound_default(par[0], EG_SIMPLE);
    synth_note_on(S, 0, 48, 100, 0, 1);
    for (b = 0; b < 20; b++)
        block(N, &finite);
    synth_perf(S, 0)->mute = 1;
    for (b = 0; b < 20; b++)
        block(N, &finite);
    check("perf mute silences the track (voices keep running)", block(N, &finite) < 1e-4f && synth_sounding(S, 0, NVOICE));
    synth_perf(S, 0)->mute = 0;

    /* stereo engines differ between the sides, mono ones do not */
    reset();
    sound_default(par[0], EG_SIMPLE);
    par[0][TP_E4] = 100;
    par[0][TP_PAN] = 64;
    synth_note_on(S, 0, 48, 100, 0, 1);
    for (b = 0; b < 20; b++)
        block(N, &finite);
    {
        int i, d = 0;
        block(N, &finite);
        for (i = 0; i < N; i++)
            d |= L[i] != R[i];
        check("simple with stereo up: the sides differ", d);
        par[0][TP_E4] = 0;
        for (b = 0; b < 20; b++)
            block(N, &finite);
        block(N, &finite);
        d = 0;
        for (i = 0; i < N; i++)
            d |= L[i] != R[i];
        check("simple with stereo at 0, centred: the sides are the same", !d);
    }

    /* a drumline track takes no synth voice */
    reset();
    sound_default(par[0], EG_DRUMLINE);
    synth_note_on(S, 0, 60, 100, 0, 1);
    check("a drumline track takes no synth voice", synth_voices_used(S) == 0);

    /* splitting by voices and by frames changes nothing */
    {
        static float L2[N], R2[N], S12[N], S22[N], ST2[N];
        static float Lr[N], Rr[N];
        static uint8_t mem2[SYNTH_BYTES];
        int same = 1, i;
        reset();
        for (t = 0; t < NTRK; t++) {
            load_preset(t, t * 8 + 3);
            par[t][TP_LTYPE] = (uint8_t)(t & 1 ? LT_TREMOLO : LT_VALUE);
            par[t][TP_LDEST] = (uint8_t)(t & 1 ? 100 : LD_FILTER);
            par[t][TP_LAMT] = 100;
            par[t][TP_LSPEED] = 50;
            par[t][TP_LX] = (uint8_t)(t & 1 ? 20 : 0);
            par[t][TP_FTYPE] = (uint8_t)(t % NFT);
        }
        for (k = 0; k < 16; k++)
            synth_note_on(S, k % NTRK, 40 + k * 2, 90 + k, 0, (uint32_t)k);
        for (b = 0; b < 10; b++)
            block(N, &finite);
        synth_note_on(S, 0, 100, 100, 0, 77);   /* a steal in flight */
        memcpy(mem2, mem, SYNTH_BYTES);          /* the same state twice (par is shared) */
        /* A: one range, one block */
        memset(L, 0, sizeof L);
        memset(R, 0, sizeof R);
        memset(S1, 0, sizeof S1);
        memset(S2, 0, sizeof S2);
        memset(ST, 0, sizeof ST);
        synth_prepare(S, N);
        synth_render(S, 0, NVOICE, L, R, S1, S2, ST, N);
        memcpy(Lr, L, sizeof L);
        memcpy(Rr, R, sizeof R);
        /* B: two voice ranges */
        memcpy(mem, mem2, SYNTH_BYTES);
        memset(L2, 0, sizeof L2);
        memset(R2, 0, sizeof R2);
        memset(S12, 0, sizeof S12);
        memset(S22, 0, sizeof S22);
        memset(ST2, 0, sizeof ST2);
        synth_prepare(S, N);
        synth_render(S, 0, 8, L2, R2, S12, S22, ST2, N);
        synth_render(S, 8, NVOICE, L2, R2, S12, S22, ST2, N);
        for (i = 0; i < N; i++)
            if (L2[i] != Lr[i] || R2[i] != Rr[i])
                same = 0;
        check("voices 0..8 + 8..16 sum bit-identically to 0..16", same);
        /* the other core's half into its own buffers, added after: the same within float rounding */
        memcpy(mem, mem2, SYNTH_BYTES);
        memset(L2, 0, sizeof L2);
        memset(R2, 0, sizeof R2);
        memset(L, 0, sizeof L);
        memset(R, 0, sizeof R);
        synth_prepare(S, N);
        synth_render(S, 8, NVOICE, L2, R2, S12, S22, ST2, N);
        synth_render(S, 0, 8, L, R, S12, S22, ST2, N);
        same = 1;
        for (i = 0; i < N; i++)
            if (fabs_f(L2[i] + L[i] - Lr[i]) > 1e-6f || fabs_f(R2[i] + R[i] - Rr[i]) > 1e-6f)
                same = 0;
        check("the halves in separate buffers, summed, match within 1e-6", same);
        /* C: 100 + 156 frames */
        memcpy(mem, mem2, SYNTH_BYTES);
        memset(L2, 0, sizeof L2);
        memset(R2, 0, sizeof R2);
        memset(S12, 0, sizeof S12);
        memset(S22, 0, sizeof S22);
        memset(ST2, 0, sizeof ST2);
        synth_prepare(S, 100);
        synth_render(S, 0, NVOICE, L2, R2, S12, S22, ST2, 100);
        synth_prepare(S, 156);
        synth_render(S, 0, NVOICE, L2 + 100, R2 + 100, S12 + 100, S22 + 100, ST2 + 100, 156);
        same = 1;
        for (i = 0; i < N; i++)
            if (L2[i] != Lr[i] || R2[i] != Rr[i])
                same = 0;
        check("100 + 156 frames render bit-identically to 256", same);
        /* and the two together, with one prepare for both renders */
        memcpy(mem, mem2, SYNTH_BYTES);
        memset(L2, 0, sizeof L2);
        memset(R2, 0, sizeof R2);
        memset(S12, 0, sizeof S12);
        memset(S22, 0, sizeof S22);
        memset(ST2, 0, sizeof ST2);
        synth_prepare(S, N);
        synth_render(S, 0, 8, L2, R2, S12, S22, ST2, 100);
        synth_render(S, 8, NVOICE, L2, R2, S12, S22, ST2, 100);
        synth_render(S, 0, 8, L2 + 100, R2 + 100, S12 + 100, S22 + 100, ST2 + 100, 156);
        synth_render(S, 8, NVOICE, L2 + 100, R2 + 100, S12 + 100, S22 + 100, ST2 + 100, 156);
        same = 1;
        for (i = 0; i < N; i++)
            if (L2[i] != Lr[i] || R2[i] != Rr[i])
                same = 0;
        check("ranges and frame splits combined, one prepare", same);
    }

    bench();
    printf("%s\n", bad ? "SYNTH TESTS FAILED" : "all synth tests passed");
    return bad != 0;
}
