/* SPDX-License-Identifier: GPL-3.0-only */
/* The factory presets for tools/render_presets.sh: for each preset a script for build/host/xy_host that
 * loads it onto track 2 of a new project (the default FX I delay and FX II reverb, the track's tape send
 * as the project sets it) and plays a short phrase: three notes, then a four-note chord held, then its
 * tail; basses an octave down. And the same phrase through the voices alone (dsp/synth.c, dry, the
 * track's level at 100), its peak, RMS and loudest 100 ms in dBFS into OUTDIR/dry.txt: the levels to balance the presets
 * by, before the master's compressor and limiter even them out. Also checks every sample is finite.
 *   preset_scripts OUTDIR       writes OUTDIR/NN-engine-name.xy and OUTDIR/dry.txt, prints the names */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "synth.h"

#define N 256
static uint8_t par[NTRK][NTP];
static _Alignas(16) uint8_t mem[SYNTH_BYTES];
static float L[N], R[N], S1[N], S2[N], ST[N];
static double acc, peak, win[17], loud;
static long frames, bad, nblk;

static int is_bass(const char *n) { return strstr(n, "bass") != 0 || strstr(n, "sub") != 0; }

static void run(synth_t *s, int ms, int measure)
{
    long n = (long)ms * 441 / 10, i;
    for (; n > 0; n -= N) {
        memset(L, 0, sizeof L);
        memset(R, 0, sizeof R);
        memset(S1, 0, sizeof S1);
        memset(S2, 0, sizeof S2);
        memset(ST, 0, sizeof ST);
        synth_prepare(s, N);
        synth_render(s, 0, NVOICE, L, R, S1, S2, ST, N);
        win[nblk++ % 17] = 0.0;
        for (i = 0; i < N; i++) {
            win[(nblk - 1) % 17] += (double)L[i] * L[i] + (double)R[i] * R[i];
            if (!(L[i] == L[i]) || !(R[i] == R[i]) || fabs((double)L[i]) > 1e6 || fabs((double)R[i]) > 1e6)
                bad++;
            if (fabs((double)L[i]) > peak)
                peak = fabs((double)L[i]);
            if (fabs((double)R[i]) > peak)
                peak = fabs((double)R[i]);
            if (measure) {
                acc += (double)L[i] * L[i] + (double)R[i] * R[i];
                frames++;
            }
        }
        if (nblk >= 17) {                        /* the loudest 100 ms (17 blocks) */
            double e = 0.0;
            for (i = 0; i < 17; i++)
                e += win[i];
            if (e > loud)
                loud = e;
        }
    }
}

/* the phrase: C E G, then C E G B held (the scripts' keys 7 11 14 18 from F3) */
static void dry(int k, double *pk, double *rms, double *ld)
{
    static const int NOTE[4] = {60, 64, 67, 71};
    synth_t *s = (synth_t *)mem;
    int t, j, o = is_bass(PRESETS[k].name) ? -12 : 0;
    for (t = 0; t < NTRK; t++)
        sound_default(par[t], EG_STACK);
    memcpy(par[1], PRESETS[k].par, NTP_SOUND);
    synth_init(s, par);
    synth_tempo(s, 120.0f);
    acc = peak = loud = 0.0;
    frames = nblk = 0;
    for (j = 0; j < 3; j++) {
        synth_note_on(s, 1, NOTE[j] + o, 100, 0, (uint32_t)j);
        run(s, 260, 1);
        synth_note_off(s, 1, (uint32_t)j);
        run(s, 40, 1);
    }
    for (j = 0; j < 4; j++)
        synth_note_on(s, 1, NOTE[j] + o, 100, 0, (uint32_t)(10 + j));
    run(s, 1300, 1);
    for (j = 0; j < 4; j++)
        synth_note_off(s, 1, (uint32_t)(10 + j));
    run(s, 1000, 0);
    *pk = peak > 0.0 ? 20.0 * log10(peak) : -200.0;
    *rms = acc > 0.0 ? 10.0 * log10(acc / (2.0 * (double)frames)) : -200.0;
    *ld = loud > 0.0 ? 10.0 * log10(loud / (2.0 * 17.0 * N)) : -200.0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/presets";
    char path[256];
    FILE *lv;
    int k, p;
    snprintf(path, sizeof path, "%s/dry.txt", dir);
    lv = fopen(path, "w");
    if (!lv)
        return 1;
    for (k = 0; k < NPRESETS; k++) {
        const preset_t *pr = &PRESETS[k];
        char base[96], *c;
        double pk, rms, ld;
        FILE *f;
        snprintf(base, sizeof base, "%02d-%s-%s", k + 1, EG_NAME[pr->par[TP_ENGINE]], pr->name);
        for (c = base; *c; c++)
            if (*c == ' ')
                *c = '_';
        snprintf(path, sizeof path, "%s/%s.xy", dir, base);
        f = fopen(path, "w");
        if (!f)
            return 1;
        fprintf(f, "# preset %d: %s\nwait 200\nspin ALGO 1\nexpect trk 2\ntap EDIT\n", k + 1, pr->name);
        fprintf(f, "param 2 %d %d\n", TP_ENGINE, pr->par[TP_ENGINE]);
        fprintf(f, "param 2 %d %d\n", TP_LTYPE, pr->par[TP_LTYPE]);
        for (p = 0; p < NTP_SOUND; p++)
            if (p != TP_ENGINE && p != TP_LTYPE && p != TP_SENDT)
                fprintf(f, "param 2 %d %d\n", p, pr->par[p]);
        if (is_bass(pr->name))
            fprintf(f, "tap OCT-\n");
        fprintf(f, "wait 100\nwav %s.wav\n", base);
        fprintf(f, "key 7 down\nwait 260\nkey 7 up\nwait 40\nkey 11 down\nwait 260\nkey 11 up\nwait 40\n"
                   "key 14 down\nwait 260\nkey 14 up\nwait 40\n"
                   "key 7 down\nkey 11 down\nkey 14 down\nkey 18 down\nwait 1300\n"
                   "key 7 up\nkey 11 up\nkey 14 up\nkey 18 up\nwait 1000\nwavstop\n");
        fclose(f);
        dry(k, &pk, &rms, &ld);
        fprintf(lv, "%s %.2f %.2f %.2f\n", base, pk, rms, ld);
        printf("%s\n", base);
    }
    fclose(lv);
    if (bad) {
        fprintf(stderr, "preset_scripts: %ld samples not finite\n", bad);
        return 1;
    }
    return 0;
}
