/* SPDX-License-Identifier: GPL-3.0-only */
/* XY-1 pattern store: the plain-data helpers of pattern.h (main loop side; the sequencer only reads). */
#include "pattern.h"

/* the value a component starts with (the notebook p. 139, 0-based): pulse 4, hold 4, multiply 2, velocity 64,
 * ramps 4 iterations, random 4, portamento 40 %, bend down-up, tonality 4 (fifth up), jump to 13, skips: a
 * cycle of 2 (mode 0: the step plays on the cycle's last pass) */
const uint8_t SC_DEFAULT[NSC] = {0, 3, 3, 1, 4, 3, 3, 3, 3, 0, 3, 3, SK_VAL(1, 0), SK_VAL(1, 0), SK_VAL(1, 0)};
const char *const GR_NAME[NGROOVE] = {"shuffle", "half shuf", "danish", "bombora", "wobbly", "gaussian", "accents",
                                      "island", "disfunk", "rollover", "prophetic"};
const char *const GR_SHORT[NGROOVE] = {"SH", "HS", "DA", "BO", "WO", "GA", "AC", "IN", "DF", "RO", "PR"};

static void zero(void *p, uint32_t n)
{
    uint8_t *b = (uint8_t *)p;
    while (n--)
        *b++ = 0;
}

void pattern_init(tpat_t *p)
{
    zero(p, sizeof *p);
    p->len = 16;
    p->scale = TS_1;
    p->groove = 0;                    /* no groove amount */
    p->quant = 100;
    p->nlen = 50;                     /* half a step */
    p->shape = 0;
}

void patterns_init(patterns_t *P)
{
    int t, k;
    zero(P, sizeof *P);
    for (t = 0; t < NSEQT; t++)
        for (k = 0; k < NPATT; k++) {
            pattern_init(&P->pat[t][k]);
            if (t == AUX_BRAIN)       /* a brain step is a beat: a chord progression at four to the bar */
                P->pat[t][k].scale = TS_4;
        }
    for (k = 0; k < NLANE; k++)
        P->lane[k].used = 0;
    P->scene[0].used = 1;             /* scene 1: pattern 1 on every track (pat[] is already 0) */
    P->song.len = 0;
    P->song.loop = 1;
}

/* ----------------------------------------------------------------- lanes --- */
int lane_find(const lane_t *L, int trk, int pat, int param)
{
    int k;
    for (k = 0; k < NLANE; k++)
        if (L[k].used && L[k].trk == trk && L[k].pat == pat && L[k].param == param)
            return k;
    return -1;
}

int lane_alloc(lane_t *L, int trk, int pat, int param)
{
    int k, i;
    k = lane_find(L, trk, pat, param);
    if (k >= 0)
        return k;
    for (k = 0; k < NLANE; k++)
        if (!L[k].used)
            break;
    if (k == NLANE)
        return -1;
    L[k].trk = (uint8_t)trk;
    L[k].pat = (uint8_t)pat;
    L[k].param = (uint8_t)param;
    for (i = 0; i < NSTEP; i++)
        L[k].val[i] = LOCK_NONE;
    L[k].gen++;                       /* the interrupt sees a new generation: a fresh runtime */
    __asm__ volatile("" ::: "memory");
    L[k].used = 1;
    return k;
}

void lane_clear(lane_t *L, int k)
{
    if (k >= 0 && k < NLANE)
        L[k].used = 0;
}

int lane_count(const lane_t *L)
{
    int k, n = 0;
    for (k = 0; k < NLANE; k++)
        n += L[k].used != 0;
    return n;
}

int step_has_lock(const lane_t *L, int trk, int pat, int step)
{
    int k;
    if (step < 0 || step >= NSTEP)
        return 0;
    for (k = 0; k < NLANE; k++)
        if (L[k].used && L[k].trk == trk && L[k].pat == pat && L[k].val[step] != LOCK_NONE)
            return 1;
    return 0;
}

void step_clear_locks(lane_t *L, int trk, int pat, int step)
{
    int k;
    if (step < 0 || step >= NSTEP)
        return;
    for (k = 0; k < NLANE; k++)
        if (L[k].used && L[k].trk == trk && L[k].pat == pat)
            L[k].val[step] = LOCK_NONE;
}

void pattern_clear_locks(lane_t *L, int trk, int pat)
{
    int k;
    for (k = 0; k < NLANE; k++)
        if (L[k].used && L[k].trk == trk && L[k].pat == pat)
            L[k].used = 0;
}

/* COND_AB + k -> A:B in the order 1:2 2:2 1:3 2:3 3:3 1:4 ... 8:8 (X0X's) */
void cond_ab(int k, int *a, int *b)
{
    int n = 2;
    if (k < 0)
        k = 0;
    if (k >= COND_NAB)
        k = COND_NAB - 1;
    while (k >= n) {
        k -= n;
        n++;
    }
    *a = k + 1;
    *b = n;
}

/* ----------------------------------------------------------------- steps --- */
void step_set_notes(step_t *st, const uint8_t *notes, int n, int vel)
{
    int i;
    for (i = 0; i < SN; i++)
        st->note[i] = i < n ? notes[i] : 0;
    st->vel = (uint8_t)(vel < 1 ? 1 : vel > 127 ? 127 : vel);
}

void step_toggle_note(step_t *st, int note)
{
    int i;
    if (note <= 0 || note > 127)
        return;
    for (i = 0; i < SN; i++)
        if (st->note[i] == note) {    /* there: remove it, close the gap */
            for (; i < SN - 1; i++)
                st->note[i] = st->note[i + 1];
            st->note[SN - 1] = 0;
            return;
        }
    for (i = 0; i < SN; i++)
        if (!st->note[i]) {
            st->note[i] = (uint8_t)note;
            if (!st->vel)
                st->vel = 100;
            return;
        }
}

int step_empty(const step_t *st)
{
    int i;
    for (i = 0; i < SN; i++)
        if (st->note[i])
            return 0;
    return 1;
}
