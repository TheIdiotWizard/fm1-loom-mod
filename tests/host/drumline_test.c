/* SPDX-License-Identifier: GPL-3.0-only */
/* The drumline (dsp/drumline.c) on its own: every key of the default kit hits, stays finite, peaks in
 * -20..0 dBFS at volume 0 dB and velocity 127, falls to silence within its length; every model with
 * its four knobs at their ends stays finite and in range; the play modes (gated stops on release,
 * loop repeats while held, choke groups cut); the key's random and the track's random vary hits;
 * the track envelope's funk delays the onset, attack fades in, length scales; the dynamic filter
 * bounds its output and bypasses on the other types; volume in dB, pan, sends; eight voices at once
 * stay bounded; and the cost in instructions a sample (macOS: proc_pid_rusage). Each key's hit is
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
#define VOL0 76                                       /* DP_VOLUME = 0 dB */
static int bad;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static uint8_t par[NTP];
static uint8_t dbuf[DRUMLINE_BYTES] __attribute__((aligned(8)));
static drumline_t *dl = (drumline_t *)dbuf;
static float L[BLK], R[BLK], S1[BLK], S2[BLK], ST[BLK];

/* the project's neutral drum envelope: funk 0, attack 0, random 0, length 64 (1x) */
static void kit_default(void)
{
    int s, k;
    memset(par, 0, sizeof par);
    for (s = 0; s < DL_NSOUND; s++)
        for (k = 0; k < DP_N; k++)
            par[TP_DRUM(s, k)] = DL_DEFAULT_KIT[s][k];
    par[TP_A] = 0;
    par[TP_D] = 0;
    par[TP_S] = 0;
    par[TP_R] = 64;
    par[TP_SEND1] = 64;
    par[TP_SEND2] = 32;
    par[TP_SENDT] = 127;
}
static void key_model(int s, int model)               /* key s plays a model with its defaults */
{
    int k;
    par[TP_DRUM(s, DP_MODEL)] = (uint8_t)model;
    for (k = 0; k < 4; k++)
        par[TP_DRUM(s, DP_K1 + k)] = DM_DEFAULT[model][k];
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
static float blk_peak(void)
{
    int i;
    float pk = 0.0f;
    for (i = 0; i < BLK; i++)
        pk = fm_maxf(pk, fm_maxf(fm_fabsf(L[i]), fm_fabsf(R[i])));
    return pk;
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

/* one hit of a key: its peak, the time it falls under -60 dB of the peak, the time it stops, the first
 * sample above -40 dBFS */
typedef struct { float peak, t60, tend, t_on; } hit_t;
static hit_t one_hit(int sound, int vel, int pitch, int sg, int lt, FILE *wav)
{
    int b, i, nb = SR * 12 / BLK, last_loud = 0, first = -1;
    float pk = 0.0f;
    hit_t h;
    drumline_init(dl, par);
    drumline_hit(dl, sound, vel, pitch, sg, lt);
    h.tend = -1.0f;
    for (b = 0; b < nb; b++) {
        render();
        wav_put(wav, L, R, BLK);
        for (i = 0; i < BLK; i++) {
            float a = fm_maxf(fm_fabsf(L[i]), fm_fabsf(R[i]));
            if (a > pk)
                pk = a;
            if (a > pk * 0.001f)
                last_loud = b * BLK + i;
            if (first < 0 && a > 0.01f)
                first = b * BLK + i;
        }
        if (!drumline_sounding(dl)) {
            h.tend = (float)((b + 1) * BLK) / (float)SR;
            break;
        }
    }
    h.peak = pk;
    h.t60 = (float)last_loud / (float)SR;
    h.t_on = first < 0 ? -1.0f : (float)first / (float)SR;
    return h;
}
static float dbfs(float x) { return 20.0f * fm_log2f(x + 1e-9f) * 0.30103f; }

int main(void)
{
    int s, i, k, m;
    hit_t h, h2;
    char name[96];
    kit_default();

    printf("-- the default kit, volume 0 dB, velocity 127 (peak dBFS, -60 dB at, ends at)\n");
    for (s = 0; s < DL_NSOUND; s++) {
        FILE *w;
        float db;
        snprintf(name, sizeof name, "build/host/drum_%s.wav", DL_SOUND_NAME[s]);
        wav_open(&w, name);
        h = one_hit(s, 127, 0, 0, 0, w);
        wav_close(w);
        db = dbfs(h.peak);
        snprintf(name, sizeof name, "  %-7s %-10s %6.1f dBFS  %5.2f s  %5.2f s", DL_SOUND_NAME[s],
                 DM_NAME[DL_DEFAULT_KIT[s][0]], (double)db, (double)h.t60, (double)h.tend);
        check(name, !nan_seen && db >= -20.0f && db <= 0.0f && h.tend > 0.0f && h.t60 < 4.0f);
    }
    check("no NaN anywhere", !nan_seen);
    check("the default kit's hats share choke group 1",
          DL_DEFAULT_KIT[2][DP_MODE] == PM_CHOKE1 && DL_DEFAULT_KIT[3][DP_MODE] == PM_CHOKE1 &&
          DL_DEFAULT_KIT[2][DP_MODEL] == DM_HIHAT && DL_DEFAULT_KIT[3][DP_MODEL] == DM_HIHAT);

    printf("-- every model: its defaults, then each knob at 0 and 127 (finite, -20..0 dBFS at the defaults, in range\n");
    printf("   at the ends; length 0 ends within 1.5 s, 127 within 12 s)\n");
    for (m = 0; m < NDM; m++) {
        int ok = 1;
        float db, worst_lo = 0.0f, worst_hi = 0.0f;
        key_model(0, m);
        h = one_hit(0, 127, 0, 0, 0, 0);
        db = dbfs(h.peak);
        ok = !nan_seen && db >= -20.0f && db <= 0.0f;
        for (k = 0; k < 4 && ok; k++) {
            int e;
            for (e = 0; e <= 127; e += 127) {
                float d2;
                par[TP_DRUM(0, DP_K1 + k)] = (uint8_t)e;
                h2 = one_hit(0, 127, 0, 0, 0, 0);
                d2 = dbfs(h2.peak);
                if (d2 < worst_lo || k == 0)
                    worst_lo = fm_minf(worst_lo, d2);
                worst_hi = fm_maxf(worst_hi, d2);
                if (nan_seen || d2 < -26.0f || d2 > 1.0f) {
                    printf("  %s %s=%d: %.1f dBFS, nan %d\n", DM_NAME[m], DM_KNOB[m][k], e, (double)d2, nan_seen);
                    ok = 0;
                }
                if (k == 1 && ((e == 0 && (h2.tend < 0.0f || h2.tend > 1.5f)) || (e == 127 && (h2.tend < 0.0f || h2.tend > 12.0f)))) {
                    printf("  %s length=%d ends at %.2f s\n", DM_NAME[m], e, (double)h2.tend);
                    ok = 0;
                }
                par[TP_DRUM(0, DP_K1 + k)] = DM_DEFAULT[m][k];
            }
        }
        snprintf(name, sizeof name, "  %-10s %s/%s/%s/%s  default %5.1f dBFS, ends %5.1f .. %5.1f", DM_NAME[m],
                 DM_KNOB[m][0], DM_KNOB[m][1], DM_KNOB[m][2], DM_KNOB[m][3], (double)db, (double)worst_lo,
                 (double)worst_hi);
        check(name, ok);
    }
    kit_default();
    {
        par[TP_DRUM(0, DP_K2)] = 127;
        h = one_hit(0, 127, 0, 1, 0, 0);
        check("short gate: a long kick is under -60 dB by 150 ms", h.t60 < 0.15f);
        par[TP_DRUM(0, DP_K2)] = 0;
        h = one_hit(0, 127, 0, 0, 0, 0);
        h2 = one_hit(0, 127, 0, 0, 1, 0);
        check("long tail: the shortest kick rings at least twice as long", h2.t60 >= 2.0f * h.t60);
        par[TP_DRUM(0, DP_K2)] = DL_DEFAULT_KIT[0][DP_K2];
        h = one_hit(0, 127, 0, 0, 0, 0);
        h2 = one_hit(0, 127, 12, 0, 0, 0);
        check("pitch +12: still in range", h2.peak > 0.1f && h2.peak <= 1.0f);
        h2 = one_hit(0, 40, 0, 0, 0, 0);
        check("velocity 40 is quieter than 127", h2.peak < h.peak);
        h2 = one_hit(10, 127, 7, 0, 0, 0);
        check("fm: pitch +7 steps still in range", h2.peak > 0.1f && h2.peak <= 1.0f && !nan_seen);
    }

    printf("-- the shift page: volume in dB, pan\n");
    {
        float p0, p6, p20, p30;
        h = one_hit(0, 127, 0, 0, 0, 0);
        p0 = h.peak;
        par[TP_DRUM(0, DP_VOLUME)] = 127;
        p20 = one_hit(0, 127, 0, 0, 0, 0).peak;
        par[TP_DRUM(0, DP_VOLUME)] = 1;
        p30 = one_hit(0, 127, 0, 0, 0, 0).peak;
        par[TP_DRUM(0, DP_VOLUME)] = 61;                /* -6 dB */
        p6 = one_hit(0, 127, 0, 0, 0, 0).peak;
        par[TP_DRUM(0, DP_VOLUME)] = 0;
        h2 = one_hit(0, 127, 0, 0, 0, 0);
        par[TP_DRUM(0, DP_VOLUME)] = VOL0;
        snprintf(name, sizeof name, "  volume 127 = +20 dB (%.1f), 61 = -6 dB (%.1f), 1 = -30 dB (%.1f), 0 silent",
                 (double)dbfs(p20 / p0), (double)dbfs(p6 / p0), (double)dbfs(p30 / p0));
        check(name, fm_fabsf(dbfs(p20 / p0) - 20.0f) < 0.5f && fm_fabsf(dbfs(p6 / p0) + 6.0f) < 0.5f &&
              fm_fabsf(dbfs(p30 / p0) + 30.0f) < 0.5f && h2.peak == 0.0f);
    }
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

    printf("-- play modes\n");
    {
        int b;
        float pk;
        /* gated: the open hat (long) stops 5 ms after its release */
        par[TP_DRUM(3, DP_MODE)] = PM_GATED;
        drumline_init(dl, par);
        drumline_hit(dl, 3, 127, 0, 0, 0);
        for (b = 0; b < 35; b++)                          /* 50 ms */
            render();
        pk = blk_peak();
        drumline_release(dl, 3);
        for (b = 0; b < 8; b++)                           /* 11 ms */
            render();
        check("gated: the open hat is -40 dB 11 ms after drumline_release", pk > 0.01f && blk_peak() < pk * 0.01f);
        for (b = 0; b < 40; b++)
            render();
        check("gated: and off within 70 ms", !drumline_sounding(dl));
        /* one-shot: a release changes nothing */
        par[TP_DRUM(3, DP_MODE)] = PM_ONESHOT;
        drumline_init(dl, par);
        drumline_hit(dl, 3, 127, 0, 0, 0);
        for (b = 0; b < 35; b++)
            render();
        pk = blk_peak();
        drumline_release(dl, 3);
        for (b = 0; b < 8; b++)
            render();
        check("one-shot: a release does not cut it", blk_peak() > pk * 0.3f);
        /* loop: the shortest kick restarts while held, so it is still sounding after a second; released, it ends */
        par[TP_DRUM(0, DP_MODE)] = PM_LOOP;
        par[TP_DRUM(0, DP_K2)] = 0;
        drumline_init(dl, par);
        drumline_hit(dl, 0, 127, 0, 0, 0);
        {
            int hits = 0, was_quiet = 0;
            for (b = 0; b < SR / BLK; b++) {
                float p;
                render();
                p = blk_peak();
                if (p < 0.02f)
                    was_quiet = 1;
                else if (was_quiet) {
                    hits++;
                    was_quiet = 0;
                }
            }
            snprintf(name, sizeof name, "  loop: a 50 ms kick restarted %d times in a second while held", hits);
            check(name, hits >= 5 && drumline_sounding(dl));
        }
        drumline_release(dl, 0);
        for (b = 0; b < SR / BLK / 2; b++)
            render();
        check("loop: released, it finishes and stops", !drumline_sounding(dl));
        par[TP_DRUM(0, DP_MODE)] = PM_ONESHOT;
        par[TP_DRUM(0, DP_K2)] = DL_DEFAULT_KIT[0][DP_K2];
        /* choke: the open hat hard left, the closed hat hard right: the left shows the open hat alone */
        par[TP_DRUM(3, DP_MODE)] = DL_DEFAULT_KIT[3][DP_MODE];
        par[TP_DRUM(3, DP_PAN)] = 0;
        par[TP_DRUM(2, DP_PAN)] = 127;
        drumline_init(dl, par);
        drumline_hit(dl, 3, 127, 0, 0, 0);
        pk = 0.0f;
        for (b = 0; b < 70; b++) {                        /* 100 ms */
            render();
            for (i = 0; i < BLK; i++)
                pk = fm_maxf(pk, fm_fabsf(L[i]));
        }
        drumline_hit(dl, 2, 127, 0, 0, 0);
        for (b = 0; b < 8; b++)                           /* 11 ms later */
            render();
        {
            float lpk2 = 0.0f;
            for (b = 0; b < 8; b++) {
                render();
                for (i = 0; i < BLK; i++)
                    lpk2 = fm_maxf(lpk2, fm_fabsf(L[i]));
            }
            check("choke 1: the closed hat chokes the open hat (-40 dB in 11 ms)", lpk2 < pk * 0.01f && pk > 0.01f);
        }
        /* a different group does not */
        par[TP_DRUM(2, DP_MODE)] = PM_CHOKE2;
        drumline_init(dl, par);
        drumline_hit(dl, 3, 127, 0, 0, 0);
        for (b = 0; b < 70; b++)
            render();
        drumline_hit(dl, 2, 127, 0, 0, 0);
        for (b = 0; b < 16; b++)
            render();
        {
            float lpk2 = 0.0f;
            for (i = 0; i < BLK; i++)
                lpk2 = fm_maxf(lpk2, fm_fabsf(L[i]));
            check("choke 2 against choke 1: the open hat keeps ringing", lpk2 > pk * 0.05f);
        }
        par[TP_DRUM(2, DP_MODE)] = PM_CHOKE1;
        par[TP_DRUM(3, DP_PAN)] = DL_DEFAULT_KIT[3][DP_PAN];
        par[TP_DRUM(2, DP_PAN)] = DL_DEFAULT_KIT[2][DP_PAN];
    }

    printf("-- random: the key's and the track's vary the hits; the kick's pitch keeps out of the track's\n");
    {
        float t1, t2, t3, p1, p2;
        h = one_hit(1, 127, 0, 0, 0, 0);
        h2 = one_hit(1, 127, 0, 0, 0, 0);
        check("random 0: two snare hits are the same", h.peak == h2.peak && h.t60 == h2.t60);
        par[TP_DRUM(1, DP_RANDOM)] = 127;
        drumline_init(dl, par);
        drumline_hit(dl, 1, 127, 0, 0, 0);
        {
            float a = 0.0f, b2 = 0.0f, c = 0.0f;
            int b;
            for (b = 0; b < 300; b++) { render(); a = fm_maxf(a, blk_peak()); }
            drumline_hit(dl, 1, 127, 0, 0, 0);
            for (b = 0; b < 300; b++) { render(); b2 = fm_maxf(b2, blk_peak()); }
            drumline_hit(dl, 1, 127, 0, 0, 0);
            for (b = 0; b < 300; b++) { render(); c = fm_maxf(c, blk_peak()); }
            t1 = a; t2 = b2; t3 = c;
        }
        check("key random 127: three snare hits differ", !(t1 == t2 && t2 == t3) && !nan_seen);
        par[TP_DRUM(1, DP_RANDOM)] = 0;
        par[TP_S] = 127;
        drumline_init(dl, par);
        drumline_hit(dl, 1, 127, 0, 0, 0);
        {
            float a = 0.0f, b2 = 0.0f;
            int b;
            for (b = 0; b < 300; b++) { render(); a = fm_maxf(a, blk_peak()); }
            drumline_hit(dl, 1, 127, 0, 0, 0);
            for (b = 0; b < 300; b++) { render(); b2 = fm_maxf(b2, blk_peak()); }
            check("track random 127: two snare hits differ", a != b2 && !nan_seen);
        }
        /* the kick's pitch: with the track's random its zero crossings in the body are those of random 0 */
        {
            int zc0 = 0, zc1 = 0, b;
            par[TP_S] = 0;
            par[TP_DRUM(0, DP_K3)] = 0;                   /* no click, no drive: a plain sine */
            par[TP_DRUM(0, DP_K2)] = 127;
            drumline_init(dl, par);
            drumline_hit(dl, 0, 127, 0, 0, 0);
            for (b = 0; b < 100; b++) render();
            for (b = 0; b < 300; b++) {
                render();
                for (i = 1; i < BLK; i++)
                    zc0 += (L[i] > 0.0f) != (L[i - 1] > 0.0f);
            }
            par[TP_S] = 127;
            p1 = p2 = 0.0f;
            for (k = 0; k < 4; k++) {
                drumline_init(dl, par);
                drumline_hit(dl, 0, 127, 0, 0, 0);
                for (b = 0; b < 100; b++) render();
                zc1 = 0;
                for (b = 0; b < 300; b++) {
                    render();
                    for (i = 1; i < BLK; i++)
                        zc1 += (L[i] > 0.0f) != (L[i - 1] > 0.0f);
                }
                p1 = fm_maxf(p1, fm_fabsf((float)(zc1 - zc0)));
            }
            snprintf(name, sizeof name, "  track random: the kick's pitch holds (zero crossings %d, off by at most %.0f)", zc0, (double)p1);
            check(name, zc0 > 50 && p1 <= 2.0f);
            par[TP_S] = 0;
            par[TP_DRUM(0, DP_K3)] = DL_DEFAULT_KIT[0][DP_K3];
            par[TP_DRUM(0, DP_K2)] = DL_DEFAULT_KIT[0][DP_K2];
        }
        (void)t3;
    }

    printf("-- the track envelope: funk, attack, length\n");
    {
        float on0, on1, t0;
        h = one_hit(1, 127, 0, 0, 0, 0);
        on0 = h.t_on;
        t0 = h.t60;
        par[TP_A] = 127;
        h2 = one_hit(1, 127, 0, 0, 0, 0);
        on1 = h2.t_on;
        snprintf(name, sizeof name, "  funk 127: the snare starts %.1f ms later (55..65)", (double)((on1 - on0) * 1000.0f));
        check(name, on1 - on0 > 0.055f && on1 - on0 < 0.065f && h2.peak > 0.1f);
        par[TP_A] = 0;
        {
            int b, a;
            float early[2], late[2];
            for (a = 0; a < 2; a++) {                     /* the open hat without, then with the attack */
                par[TP_D] = (uint8_t)(a ? 127 : 0);
                drumline_init(dl, par);
                drumline_hit(dl, 3, 127, 0, 0, 0);
                early[a] = late[a] = 0.0f;
                for (b = 0; b < 7; b++) { render(); early[a] = fm_maxf(early[a], blk_peak()); }   /* the first 10 ms */
                for (b = 0; b < 62; b++) render();
                for (b = 0; b < 7; b++) { render(); late[a] = fm_maxf(late[a], blk_peak()); }    /* at 100 ms */
            }
            snprintf(name, sizeof name, "  attack 127: the open hat's first 10 ms %.3f -> %.3f, at 100 ms %.3f -> %.3f",
                     (double)early[0], (double)early[1], (double)late[0], (double)late[1]);
            check(name, early[1] < early[0] * 0.25f && late[1] > late[0] * 0.7f);
        }
        par[TP_D] = 0;
        par[TP_R] = 0;
        h = one_hit(1, 127, 0, 0, 0, 0);
        par[TP_R] = 127;
        h2 = one_hit(1, 127, 0, 0, 0, 0);
        par[TP_R] = 64;
        snprintf(name, sizeof name, "  length 0 / 64 / 127: the snare's -60 dB at %.2f / %.2f / %.2f s", (double)h.t60,
                 (double)t0, (double)h2.t60);
        check(name, h.t60 < t0 * 0.5f && h2.t60 > t0 * 2.0f);
    }

    printf("-- the dynamic filter\n");
    {
        int b;
        float pk_dry, pk_f, pk_lo, pk_hi, e_lo, e_hi;
        /* bypass: any other type leaves the sound as it was */
        h = one_hit(0, 127, 0, 0, 0, 0);
        pk_dry = h.peak;
        drumline_init(dl, par);
        drumline_filter(dl, FT_LP24, 127, 127, 127, 0);
        drumline_hit(dl, 0, 127, 0, 0, 0);
        pk_f = 0.0f;
        for (b = 0; b < 300; b++) { render(); pk_f = fm_maxf(pk_f, blk_peak()); }
        check("type lp24 with every knob up: bypass, the kick unchanged", pk_f == pk_dry);
        /* neutral settings: close to dry */
        drumline_init(dl, par);
        drumline_filter(dl, FT_DYNAMIC, 0, 0, 0, 127);
        drumline_hit(dl, 0, 127, 0, 0, 0);
        pk_f = 0.0f;
        for (b = 0; b < 300; b++) { render(); pk_f = fm_maxf(pk_f, blk_peak()); }
        check("dynamic at low cut 0, comp 0, drive 0, hi cut 127: within 1 dB of dry", fm_fabsf(dbfs(pk_f / pk_dry)) < 1.0f);
        /* the low cut at 2 kHz takes the kick down; the hi cut at 300 Hz takes the closed hat down */
        drumline_init(dl, par);
        drumline_filter(dl, FT_DYNAMIC, 127, 0, 0, 127);
        drumline_hit(dl, 0, 127, 0, 0, 0);
        pk_lo = 0.0f;
        for (b = 0; b < 300; b++) { render(); pk_lo = fm_maxf(pk_lo, blk_peak()); }
        e_lo = 0.0f;
        drumline_init(dl, par);
        drumline_filter(dl, FT_DYNAMIC, 0, 0, 0, 0);
        drumline_hit(dl, 2, 127, 0, 0, 0);
        pk_hi = 0.0f;
        for (b = 0; b < 100; b++) { render(); pk_hi = fm_maxf(pk_hi, blk_peak()); }
        h2 = one_hit(2, 127, 0, 0, 0, 0);
        snprintf(name, sizeof name, "  low cut 127 on the kick %.1f dB, hi cut 0 on the hat %.1f dB", (double)dbfs(pk_lo / pk_dry),
                 (double)dbfs(pk_hi / h2.peak));
        check(name, dbfs(pk_lo / pk_dry) < -12.0f && dbfs(pk_hi / h2.peak) < -12.0f);
        (void)e_lo; (void)e_hi;
        /* compression evens out: a velocity 40 kick and a 127 kick end up closer than 3:1 */
        drumline_init(dl, par);
        drumline_filter(dl, FT_DYNAMIC, 0, 127, 0, 127);
        drumline_hit(dl, 0, 127, 0, 0, 0);
        pk_f = 0.0f;
        for (b = 0; b < 300; b++) { render(); pk_f = fm_maxf(pk_f, blk_peak()); }
        drumline_hit(dl, 0, 40, 0, 0, 0);
        pk_lo = 0.0f;
        for (b = 0; b < 300; b++) { render(); pk_lo = fm_maxf(pk_lo, blk_peak()); }
        h = one_hit(0, 40, 0, 0, 0, 0);
        snprintf(name, sizeof name, "  comp 127: kick 127 / 40 = %.1f dB apart (dry %.1f dB), output under 0 dBFS (%.2f)",
                 (double)dbfs(pk_f / pk_lo), (double)dbfs(pk_dry / h.peak), (double)pk_f);
        check(name, dbfs(pk_f / pk_lo) < dbfs(pk_dry / h.peak) - 3.0f && pk_f <= 1.0f && !nan_seen);
        /* drive: everything at once, eight voices, drive 127 and +20 dB on every key, stays under 1.0 */
        for (s = 0; s < DL_NSOUND; s++)
            par[TP_DRUM(s, DP_VOLUME)] = 127;
        drumline_init(dl, par);
        drumline_filter(dl, FT_DYNAMIC, 0, 127, 127, 127);
        pk_f = 0.0f;
        for (b = 0; b < 400; b++) {
            if (b % 20 == 0)
                for (s = 0; s < DL_NSOUND; s++)
                    drumline_hit(dl, s, 127, 0, 0, 0);
            render();
            pk_f = fm_maxf(pk_f, blk_peak());
        }
        snprintf(name, sizeof name, "  drive 127 + comp 127, eleven keys at +20 dB: peak %.2f, bounded by 1", (double)pk_f);
        check(name, pk_f <= 1.0f && pk_f > 0.3f && !nan_seen);
        for (s = 0; s < DL_NSOUND; s++)
            par[TP_DRUM(s, DP_VOLUME)] = VOL0;
    }

    printf("-- voices\n");
    {
        float lpk;
        int b;
        /* a retrigger uses the sound's own voice: the kick twice is one kick's level, not two */
        drumline_init(dl, par);
        drumline_hit(dl, 0, 127, 0, 0, 0);
        for (b = 0; b < 4; b++)
            render();
        drumline_hit(dl, 0, 127, 0, 0, 0);
        lpk = 0.0f;
        for (b = 0; b < 40; b++) {
            render();
            lpk = fm_maxf(lpk, blk_peak());
        }
        h = one_hit(0, 127, 0, 0, 0, 0);
        check("a sound retriggers its own voice (one kick's level)", lpk <= h.peak * 1.05f);

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
        /* two resonators share the two strings; a third takes the older's */
        for (s = 0; s < 3; s++)
            key_model(s, DM_RESO);
        drumline_init(dl, par);
        for (s = 0; s < 3; s++) {
            drumline_hit(dl, s, 127, s * 5, 0, 0);
            for (b = 0; b < 10; b++)
                render();
        }
        lpk = (float)drumline_sounding(dl);
        for (b = 0; b < 200; b++)
            render();
        check("three resonators: finite, the third steals a string", !nan_seen && lpk > 0.0f);
        kit_default();
    }

    printf("-- cost, instructions a sample (each model alone, then eight voices, then with the filter)\n");
    {
        static float l[256], r[256], a[256], b2[256], c[256];
        uint64_t i0, di;
        for (m = 0; m < NDM; m++) {
            key_model(0, m);
            par[TP_DRUM(0, DP_K2)] = 127;
            drumline_init(dl, par);
            drumline_hit(dl, 0, 127, 0, 0, 0);
            memset(l, 0, sizeof l);
            drumline_render(dl, l, r, a, b2, c, 256);
            i0 = instr_now();
            drumline_render(dl, l, r, a, b2, c, 256);
            drumline_render(dl, l, r, a, b2, c, 256);
            di = instr_now() - i0;
            printf("  %-10s %5.0f\n", DM_NAME[m], (double)di / 512.0);
        }
        kit_default();
        for (s = 0; s < DL_NSOUND; s++)
            par[TP_DRUM(s, DP_K2)] = 127;
        for (k = 0; k < 2; k++) {
            uint64_t total = 0, worst = 0;
            int blk;
            drumline_init(dl, par);
            if (k)
                drumline_filter(dl, FT_DYNAMIC, 64, 64, 64, 64);
            for (s = 0; s < 8; s++)
                drumline_hit(dl, s + 3, 127, 0, 0, 0);      /* clap .. fm, the open hat too */
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
            printf("  8 voices%s:  %5.0f mean, %5.0f worst block\n", k ? " + dynamic filter" : "",
                   (double)total / (55.0 * 256.0), (double)worst / 256.0);
            check(k ? "eight voices with the dynamic filter under 760 instructions a sample (the filter ~120)"
                    : "eight voices under 640 instructions a sample (80 a voice)",
                  total / (55 * 256) < (k ? 760u : 640u));
        }
        kit_default();
    }
    printf("%s\n", bad ? "DRUMLINE TESTS FAILED" : "drumline tests passed");
    return bad != 0;
}
