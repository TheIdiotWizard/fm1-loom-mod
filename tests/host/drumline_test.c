/* SPDX-License-Identifier: GPL-3.0-only */
/* The drumline (dsp/drumline.c) on its own: every model hits, stays finite, peaks in -20..0 dBFS at
 * level 100 and velocity 127, falls to silence within its decay; a sound retriggers its own voice,
 * the closed hat chokes the open hat, short gates cut and long tails stretch; eight voices at once
 * stay bounded; and the cost in instructions a sample (macOS: proc_pid_rusage). Each sound's hit is
 * written to build/host/drum_<name>.wav for inspection (nothing here plays audio).
 *   tests/host/drumline_test.sh */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "drumline.h"
#include "synth.h"
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

static uint8_t par[NTP];
static uint8_t dbuf[DRUMLINE_BYTES] __attribute__((aligned(8)));
static drumline_t *dl = (drumline_t *)dbuf;
static float L[BLK], R[BLK], S1[BLK], S2[BLK], ST[BLK];

static void kit_default(void)
{
    int s, k;
    memset(par, 0, sizeof par);
    for (s = 0; s < DL_NSOUND; s++)
        for (k = 0; k < DP_N; k++)
            par[TP_DRUM(s, k)] = DL_DEFAULT_KIT[s][k];
    par[TP_SEND1] = 64;
    par[TP_SEND2] = 32;
    par[TP_SENDT] = 127;
}

static int nan_seen;
static void render(void)
{
    int i;
    memset(L, 0, sizeof L); memset(R, 0, sizeof R);
    memset(S1, 0, sizeof S1); memset(S2, 0, sizeof S2); memset(ST, 0, sizeof ST);
    drumline_render(dl, L, R, S1, S2, ST, BLK);
    for (i = 0; i < BLK; i++)
        if (L[i] != L[i] || R[i] != R[i] || S1[i] != S1[i] || S2[i] != S2[i] || ST[i] != ST[i])
            nan_seen = 1;
}

static void wav_open(FILE **f, const char *name)
{
    static const uint8_t hdr[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,
        0x44,0xAC,0,0,0x88,0x58,1,0,2,0,16,0,'d','a','t','a',0,0,0,0};
    *f = fopen(name, "wb");
    if (*f)
        fwrite(hdr, 1, 44, *f);
}
static void wav_put(FILE *f, const float *l, const float *r, int n)
{
    int i;
    for (i = 0; f && i < n; i++) {
        float v = (l[i] + r[i]) * 0.7071f * 32767.0f;
        int16_t s = (int16_t)fm_clampf(v, -32767.0f, 32767.0f);
        fwrite(&s, 2, 1, f);
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

/* one hit of a sound: its peak, the time it falls under -60 dB of the peak, the time it stops */
static void one_hit(int sound, int vel, int pitch, int sg, int lt, float *peak, float *t60, float *tend, FILE *wav)
{
    int b, i, nb = SR * 12 / BLK, last_loud = 0;
    float pk = 0.0f;
    drumline_init(dl, par);
    drumline_hit(dl, sound, vel, pitch, sg, lt);
    *tend = -1.0f;
    for (b = 0; b < nb; b++) {
        render();
        wav_put(wav, L, R, BLK);
        for (i = 0; i < BLK; i++) {
            float a = fm_maxf(fm_fabsf(L[i]), fm_fabsf(R[i]));
            if (a > pk)
                pk = a;
            if (a > pk * 0.001f)
                last_loud = b * BLK + i;
        }
        if (!drumline_sounding(dl)) {
            *tend = (float)((b + 1) * BLK) / (float)SR;
            break;
        }
    }
    *peak = pk;
    *t60 = (float)last_loud / (float)SR;
}

int main(void)
{
    int s, i;
    float peak, t60, tend;
    char name[64];
    kit_default();

    printf("-- every sound of the default kit, level 100, velocity 127 (peak dBFS, -60 dB at, ends at)\n");
    for (s = 0; s < DL_NSOUND; s++) {
        FILE *w;
        float db;
        par[TP_DRUM(s, DP_LEVEL)] = 100;
        snprintf(name, sizeof name, "build/host/drum_%s.wav", DL_SOUND_NAME[s]);
        wav_open(&w, name);
        one_hit(s, 127, 0, 0, 0, &peak, &t60, &tend, w);
        wav_close(w);
        db = 20.0f * fm_log2f(peak + 1e-9f) * 0.30103f;
        snprintf(name, sizeof name, "  %-7s %-10s %6.1f dBFS  %5.2f s  %5.2f s", DL_SOUND_NAME[s], DM_NAME[DL_DEFAULT_KIT[s][0]],
                 (double)db, (double)t60, (double)tend);
        check(name, !nan_seen && db >= -20.0f && db <= 0.0f && tend > 0.0f && t60 < 4.0f);
    }
    check("no NaN anywhere", !nan_seen);

    printf("-- decay: the knob's shortest and longest (ends within 1 s / 12 s), gates\n");
    for (s = 0; s < DL_NSOUND; s++) {
        float p2, t2, e2;
        par[TP_DRUM(s, DP_DECAY)] = 0;
        one_hit(s, 127, 0, 0, 0, &peak, &t60, &tend, 0);
        par[TP_DRUM(s, DP_DECAY)] = 127;
        one_hit(s, 127, 0, 0, 0, &p2, &t2, &e2, 0);
        par[TP_DRUM(s, DP_DECAY)] = DL_DEFAULT_KIT[s][DP_DECAY];
        snprintf(name, sizeof name, "  %-7s short %5.2f s, long %5.2f s", DL_SOUND_NAME[s], (double)t60, (double)t2);
        check(name, tend > 0.0f && tend <= 1.0f && e2 > 0.0f && e2 <= 12.0f && t2 > t60);
    }
    {
        float p2, t2, e2;
        par[TP_DRUM(0, DP_DECAY)] = 127;
        one_hit(0, 127, 0, 1, 0, &peak, &t60, &tend, 0);
        check("short gate: a long kick is under -60 dB by 150 ms", t60 < 0.15f);
        par[TP_DRUM(0, DP_DECAY)] = 0;
        one_hit(0, 127, 0, 0, 0, &peak, &t60, &tend, 0);
        one_hit(0, 127, 0, 0, 1, &p2, &t2, &e2, 0);
        check("long tail: the shortest kick rings at least twice as long", t2 >= 2.0f * t60);
        par[TP_DRUM(0, DP_DECAY)] = DL_DEFAULT_KIT[0][DP_DECAY];
        one_hit(0, 127, 12, 0, 0, &p2, &t2, &e2, 0);
        check("pitch +12: still in range", p2 > 0.1f && p2 <= 1.0f);
        one_hit(0, 40, 0, 0, 0, &p2, &t2, &e2, 0);
        check("velocity 40 is quieter than 127", p2 < peak);
    }

    printf("-- tune and tone sweeps stay finite and bounded\n");
    {
        int ok = 1, t;
        for (s = 0; s < DL_NSOUND && ok; s++)
            for (t = 0; t <= 127 && ok; t += 127) {
                par[TP_DRUM(s, DP_TUNE)] = (uint8_t)t;
                par[TP_DRUM(s, DP_TONE)] = (uint8_t)(127 - t);
                one_hit(s, 127, 0, 0, 0, &peak, &t60, &tend, 0);
                ok = !nan_seen && peak <= 1.5f && peak > 0.01f;
                if (!ok)
                    printf("  %s tune %d tone %d: peak %.3f, nan %d\n", DL_SOUND_NAME[s], t, 127 - t, (double)peak, nan_seen);
                par[TP_DRUM(s, DP_TUNE)] = 64;
                par[TP_DRUM(s, DP_TONE)] = DL_DEFAULT_KIT[s][DP_TONE];
            }
        check("tune 0 / 127 with tone 127 / 0 on every sound", ok);
    }

    printf("-- voices and chokes\n");
    {
        float lpk, lpk2;
        int b;
        /* the open hat hard left, the closed hat hard right: the left shows the open hat alone */
        par[TP_DRUM(3, DP_PAN)] = 0;
        par[TP_DRUM(2, DP_PAN)] = 127;
        par[TP_DRUM(3, DP_DECAY)] = 127;
        drumline_init(dl, par);
        drumline_hit(dl, 3, 127, 0, 0, 0);
        lpk = 0.0f;
        for (b = 0; b < 70; b++) {                            /* 100 ms */
            render();
            for (i = 0; i < BLK; i++)
                lpk = fm_maxf(lpk, fm_fabsf(L[i]));
        }
        drumline_hit(dl, 2, 127, 0, 0, 0);
        for (b = 0; b < 8; b++)                               /* 11 ms later */
            render();
        lpk2 = 0.0f;
        for (b = 0; b < 8; b++) {
            render();
            for (i = 0; i < BLK; i++)
                lpk2 = fm_maxf(lpk2, fm_fabsf(L[i]));
        }
        check("the closed hat chokes the open hat (-40 dB in 11 ms)", lpk2 < lpk * 0.01f && lpk > 0.01f);
        par[TP_DRUM(3, DP_PAN)] = DL_DEFAULT_KIT[3][DP_PAN];
        par[TP_DRUM(2, DP_PAN)] = DL_DEFAULT_KIT[2][DP_PAN];
        par[TP_DRUM(3, DP_DECAY)] = DL_DEFAULT_KIT[3][DP_DECAY];

        /* a retrigger uses the sound's own voice: the kick twice is one kick's level, not two */
        drumline_init(dl, par);
        drumline_hit(dl, 0, 127, 0, 0, 0);
        for (b = 0; b < 4; b++)
            render();
        drumline_hit(dl, 0, 127, 0, 0, 0);
        lpk = 0.0f;
        for (b = 0; b < 40; b++) {
            render();
            for (i = 0; i < BLK; i++)
                lpk = fm_maxf(lpk, fm_fabsf(L[i]));
        }
        one_hit(0, 127, 0, 0, 0, &peak, &t60, &tend, 0);
        check("a sound retriggers its own voice (one kick's level)", lpk <= peak * 1.05f);

        /* all eleven at once, repeatedly: finite and bounded; the eighth takes the oldest voice */
        drumline_init(dl, par);
        lpk = 0.0f;
        nan_seen = 0;
        for (b = 0; b < 400; b++) {
            if (b % 20 == 0)
                for (s = 0; s < DL_NSOUND; s++)
                    drumline_hit(dl, s, 127, 0, 0, 0);
            render();
            for (i = 0; i < BLK; i++)
                lpk = fm_maxf(lpk, fm_fabsf(L[i]) + fm_fabsf(S1[i]));
        }
        printf("  eleven at once: peak %.2f\n", (double)lpk);
        check("eleven sounds hit together, 20 times: finite, under eight voices' worth", !nan_seen && lpk < 8.0f);
        drumline_choke(dl);
        for (b = 0; b < 40; b++)
            render();
        check("drumline_choke silences everything within 60 ms", !drumline_sounding(dl));
        check("drumline_hitv shows the last hit", drumline_hitv[0] == 255);
    }

    printf("-- sends and pan\n");
    {
        float sl = 0.0f, sr = 0.0f, s1 = 0.0f, s2 = 0.0f, st = 0.0f;
        int b;
        par[TP_DRUM(0, DP_PAN)] = 127;
        drumline_init(dl, par);
        drumline_hit(dl, 0, 127, 0, 0, 0);
        for (b = 0; b < 40; b++) {
            render();
            for (i = 0; i < BLK; i++) {
                sl += L[i] * L[i]; sr += R[i] * R[i]; s1 += S1[i] * S1[i]; s2 += S2[i] * S2[i]; st += ST[i] * ST[i];
            }
        }
        check("pan 127: the right side only", sr > 0.0f && sl < sr * 1e-6f);
        check("sends follow TP_SEND1 (64) / TP_SEND2 (32) / TP_SENDT (127)",
              s1 > s2 * 3.5f && s1 < s2 * 4.5f && st > s1 * 3.5f && st < s1 * 4.5f);
        par[TP_DRUM(0, DP_PAN)] = 64;
    }

    printf("-- cost, instructions a sample (each model alone, then eight voices)\n");
    {
        static float l[256], r[256], a[256], b2[256], c[256];
        uint64_t i0, di;
        for (s = 0; s < DL_NSOUND; s++) {
            par[TP_DRUM(s, DP_DECAY)] = 127;
            drumline_init(dl, par);
            drumline_hit(dl, s, 127, 0, 0, 0);
            memset(l, 0, sizeof l);
            drumline_render(dl, l, r, a, b2, c, 256);
            i0 = instr_now();
            drumline_render(dl, l, r, a, b2, c, 256);
            drumline_render(dl, l, r, a, b2, c, 256);
            di = instr_now() - i0;
            printf("  %-10s %5.0f\n", DM_NAME[DL_DEFAULT_KIT[s][0]], (double)di / 512.0);
            par[TP_DRUM(s, DP_DECAY)] = DL_DEFAULT_KIT[s][DP_DECAY];
        }
        for (s = 0; s < DL_NSOUND; s++)
            par[TP_DRUM(s, DP_DECAY)] = 127;
        drumline_init(dl, par);
        for (s = 0; s < 8; s++)
            drumline_hit(dl, s + 3, 127, 0, 0, 0);          /* clap .. reso, the open hat too */
        {
            uint64_t total = 0, worst = 0;
            int blk;
            for (blk = 0; blk < 60; blk++) {
                memset(l, 0, sizeof l);
                i0 = instr_now();
                drumline_render(dl, l, r, a, b2, c, 256);
                di = instr_now() - i0;
                if (blk > 4) {
                    total += di;
                    if (di > worst)
                        worst = di;
                }
            }
            printf("  8 voices:  %5.0f mean, %5.0f worst block\n", (double)total / (55.0 * 256.0),
                   (double)worst / 256.0);
            check("eight voices under 640 instructions a sample (80 a voice)", total / (55 * 256) < 640);
        }
    }
    printf("%s\n", bad ? "DRUMLINE TESTS FAILED" : "drumline tests passed");
    return bad != 0;
}
