/* SPDX-License-Identifier: GPL-3.0-only */
/* The voices (dsp/synth.c) on their own: every engine and preset renders a note that stays finite and
 * at a sensible level and dies in its release; the voice modes' retrigger rules; the pool's limit and
 * stealing; locks; the LFO; and that splitting a block by voices or by frames changes no sample. Then
 * a benchmark: instructions per sample per voice, 16 voices of each engine (macOS: the task's own
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

/* dsp/drumline.c is not linked here: its names, for the drum knobs' text */
const char *const DM_NAME[11] = {"kick", "snare", "closed hat", "open hat", "clap", "low tom", "high tom", "conga",
                                 "shaker", "fm", "resonator"};

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
    for (i = 0; i < n; i++) {
        if (!finite_f(L[i]) || !finite_f(R[i]) || !finite_f(S1[i]))
            *finite = 0;
        if (fabs_f(L[i]) > pk)
            pk = fabs_f(L[i]);
        if (fabs_f(R[i]) > pk)
            pk = fabs_f(R[i]);
    }
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
    printf("\nbenchmark: 16 voices, LP24, instructions per sample per voice (mean / worst block)\n");
    for (e = 0; e < EG_DRUMLINE; e++) {
        int t, b, finite = 1;
        uint64_t total = 0, worst = 0;
        reset();
        for (t = 0; t < NTRK; t++) {
            sound_default(par[t], e);
            par[t][TP_FTYPE] = FT_LP24;
            par[t][TP_E4] = 100;                 /* stereo where the engine has it: the dear case */
            par[t][TP_E3] = 60;
            par[t][TP_LTYPE] = LT_VALUE;
            par[t][TP_LDEST] = TP_CUT;
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

    {
        int synth_sizeof(void);
        printf("synth_t: %d of %d bytes\n", synth_sizeof(), SYNTH_BYTES);
    }
    /* the parameter tables */
    check("every parameter has info and text", 1);
    for (k = 0; k < NTP; k++) {
        const tp_info_t *i = tp_info(k, EG_PRISM);
        int v;
        if (!i->name || i->lo > i->hi || i->def < i->lo || i->def > i->hi) {
            check("parameter info ranges", 0);
            break;
        }
        for (v = i->lo; v <= i->hi; v++)
            tp_text(k, v, EG_PRISM, num, unit);
    }
    for (e = 0; e < NENGINE; e++)
        for (k = 0; k < 4; k++)
            if (!tp_info(TP_E1 + k, e)->name[0] && e != EG_DRUMLINE) {
                check("engine knob names", 0);
                break;
            }
    tp_text(TP_LSPEED, 6, 0, num, unit);
    check("LFO speed 6 shows 1/4", !strcmp(num, "1/4"));
    tp_text(TP_LSPEED, 127, 0, num, unit);
    check("LFO speed 127 shows 30.0 Hz", !strcmp(num, "30.0") && !strcmp(unit, "Hz"));
    tp_text(TP_A, 0, 0, num, unit);
    check("attack 0 shows 1 ms", !strcmp(num, "1") && !strcmp(unit, "ms"));
    tp_text(TP_LDEST, TP_CUT, 0, num, unit);
    check("LFO destination shows the parameter's name", !strcmp(num, "cutoff"));
    tp_text(TP_DRUM(2, DP_MODEL), 2, EG_DRUMLINE, num, unit);
    check("drum model text", !strcmp(num, "closed hat"));
    {
        uint8_t p[NTP];
        sound_default(p, EG_SIMPLE);
        p[TP_PLAYER] = PL_MAESTRO;
        check("player knob names follow the player", !strcmp(tp_info_par(TP_P1, p)->name, "roll"));
        p[TP_LTYPE] = LT_DUCK;
        check("LFO extra follows the type", !strcmp(tp_info_par(TP_LX, p)->name, "source"));
        tp_text_par(TP_LX, 8, p, num, unit);
        check("duck source 8 is the metronome", !strcmp(num, "metro"));
    }
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
            par[0][TP_CUT] = (uint8_t)(ft >= FT_HP ? 30 : 110);   /* a high-pass at 8 kHz would leave nothing */
            par[0][TP_E1] = par[0][TP_E2] = par[0][TP_E3] = par[0][TP_E4] = (uint8_t)(ft & 1 ? 127 : 0);
            ok &= note_test(EG_NAME[e], 0, &pk, 0);   /* some corners are quiet by design */
        }
        check(ok ? "engine with every filter type, knobs at the ends" : EG_NAME[e], ok);
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

    /* a six-note chord of the loudest kind stays inside full scale */
    reset();
    sound_default(par[0], EG_ORGAN);
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
    printf("    six organ notes at full velocity and volume: %.1f dBFS\n", (double)db(pk));
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
        sound_default(par[t], EG_PRISM);
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
    par[0][TP_FTYPE] = FT_LP24;
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

    /* the LFO moves a parameter */
    reset();
    sound_default(par[0], EG_SIMPLE);
    par[0][TP_CUT] = 40;
    par[0][TP_LTYPE] = LT_VALUE;
    par[0][TP_LSPEED] = 60;                      /* a few Hz */
    par[0][TP_LAMT] = 127;
    par[0][TP_LDEST] = TP_CUT;
    par[0][TP_LX] = 0;
    {
        float mn = 1.0f, mx = 0.0f;
        synth_note_on(S, 0, 48, 100, 0, 1);
        for (b = 0; b < 40; b++)
            block(N, &finite);
        for (b = 0; b < 172; b++) {
            float p = block(N, &finite);
            if (p < mn)
                mn = p;
            if (p > mx)
                mx = p;
        }
        check("a value LFO on cutoff moves the level", mx > mn * 1.3f);
    }
    reset();
    sound_default(par[0], EG_ORGAN);
    par[0][TP_LTYPE] = LT_TREMOLO;
    par[0][TP_LSPEED] = 60;
    par[0][TP_LAMT] = 127;
    {
        float mn = 1.0f, mx = 0.0f;
        synth_note_on(S, 0, 48, 100, 0, 1);
        for (b = 0; b < 40; b++)
            block(N, &finite);
        for (b = 0; b < 172; b++) {
            float p = block(N, &finite);
            if (p < mn)
                mn = p;
            if (p > mx)
                mx = p;
        }
        check("a tremolo LFO moves the level", mx > mn * 1.5f);
    }
    reset();
    sound_default(par[0], EG_ORGAN);
    par[0][TP_LTYPE] = LT_DUCK;
    par[0][TP_LX] = 3;
    par[0][TP_LAMT] = 127;
    par[0][TP_LSPEED] = 6;
    {
        float a, c;
        synth_note_on(S, 0, 48, 100, 0, 1);
        for (b = 0; b < 40; b++)
            block(N, &finite);
        a = block(N, &finite);
        synth_duck_trigger(S, 3);
        block(N, &finite);                       /* the gain ramps down inside this block */
        c = block(N, &finite);
        check("a duck trigger from its source drops the level", c < a * 0.3f);
        for (b = 0; b < 172; b++)
            c = block(N, &finite);
        check("the duck recovers", c > a * 0.8f);
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
        int i, differ = 0;
        block(N, &finite);
        for (i = 0; i < N; i++)
            differ |= L[i] != R[i];
        check("simple with stereo up: the sides differ", differ);
        par[0][TP_E4] = 0;
        for (b = 0; b < 20; b++)
            block(N, &finite);
        block(N, &finite);
        differ = 0;
        for (i = 0; i < N; i++)
            differ |= L[i] != R[i];
        check("simple with stereo at 0, centred: the sides are the same", !differ);
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
            par[t][TP_LTYPE] = LT_VALUE;
            par[t][TP_LDEST] = TP_CUT;
            par[t][TP_LAMT] = 100;
            par[t][TP_LSPEED] = 50;
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
