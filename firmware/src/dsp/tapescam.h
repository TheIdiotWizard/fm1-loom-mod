/* SPDX-License-Identifier: MIT */
/* TapeScam on the master: LOOM-8's "Master tape" page. Included by fx.c only (static code, no unit of its
 * own).
 *
 * Ported from schwung-tapescam (src/dsp/tapescam.c, 0.5.2), Copyright (c) 2025 Charles Vestal, MIT
 * License (the licence text: LICENSING.md), itself from TapeScamVST. Its chain, as there:
 *   input -> gain stage (two tanh clippers, a pre-EQ between them: high-pass 144 Hz, low-pass 5.72 kHz;
 *   a post low-pass 17.5 kHz) -> tape saturation (tanh, an HF roll-off 18 kHz .. 10 kHz with the drive)
 *   -> wow and flutter (a 5 / 5.5 ms line read by two sines and smoothed noise, cubic) -> hiss (white
 *   into pink) -> tone -> widen (the side x1.35) -> output (tanh)
 * Four knobs reach it: drive (the gain stage's drive), color (its character and the tape saturation),
 * wobble (wow and flutter), noise (hiss). The rest stays at the plugin's defaults: speed HIGH, age NEW
 * (no dropouts), compression OFF, tone 0.5 (flat: the shelves are left out), widen ON, input 0.7.
 *
 * For the FM-1 (the porting notes):
 *   - 44.1 kHz instead of 48 kHz (every coefficient from SR; the delays in ms);
 *   - float, fastmath.h: powf / expf / sinf / cosf -> fm_*; tanhf -> ts_tanh, a Pade approximant (1e-4);
 *   - the controls (and the filters' coefficients) move once a block of 32, as the plugin's moved once a
 *     process call; the wow and flutter sines (0.3 Hz, 4 Hz) and their noise are computed once a block
 *     and interpolated across it (the noise's one-poles step a block at a time with their per-sample
 *     variance kept: the input scaled by 1/sqrt(32)); the read head's slew stays per sample;
 *   - the line is MST_TAPE_DLY (512) floats a side in the pool, not 2048: 5.5 ms + the 4.2 ms clamp of
 *     the swing need 430 samples at 44.1 kHz;
 *   - the gain stage high-passes at 144 Hz before its second clipper (a pedal's voicing): on the master
 *     that took the kick away, so here what the high-pass removes, below ~100 Hz (a one-pole), goes around
 *     the second clipper and comes back level-matched, the sum soft-clipped (tanh); the rest is the plugin's;
 *   - the plugin's gain stage always runs (it is the sound: its band-pass voicing between the clippers);
 *     here drive = 0 takes it out, and the first quarter of the knob fades it in (a parallel blend),
 *     so a turn up from 0 does not jump; color = 0 takes the saturation out (as the plugin's own
 *     threshold does), wobble = 0 the line (as the plugin), noise = 0 the hiss; all four at 0 take the
 *     whole tape out, the widening and the output stage too: bit-transparent;
 *   - the output level is fixed so that the tape keeps a bus at -12 dBFS where it was: the plugin's
 *     -12..+6 dB output knob is gone, and what the gain stage and the saturation add to such a bus (their
 *     clipping counted) is trimmed away at the output; quieter sounds come up a little, louder ones are
 *     squeezed: the tape's compression. */

#define TS_SUB 32
#define TS_BASE_L (0.005f * SR)       /* the line's taps: 5 ms left, 5.5 ms right */
#define TS_BASE_R (0.0055f * SR)
#define TS_MAXDEV (0.0042f * SR)

typedef struct { float b0, b1, b2, a1, a2, z1, z2; } ts_bq_t;

typedef struct {
    float *dl, *dr;                   /* the wow / flutter line, MST_TAPE_DLY a side */
    uint32_t w;
    uint32_t rng;
    /* the knobs, 0..1 */
    float drive, color, wobble, noise;
    uint8_t on;                       /* any knob up: the tape is in the path */
    /* gain stage */
    float trim, chan, drv_scale, soft1, soft2, asym2, char_, drive_n, gs_mix, gs_mix_c, gs_low;
    ts_bq_t pre_hp[2], pre_lp[2], post_lp[2];
    float low_lp[2];                  /* the lows kept around the second clipper: a one-pole, 100 Hz */
    /* tape saturation */
    float sat_t, sat_s, sat_f;
    ts_bq_t sat_lp[2];
    /* wow / flutter */
    float wf_t, wf_s, wf_ramp, wf_xf, wf_shape, wf_slew, wf_wow, wf_flt;
    float ph_wl, ph_wr, ph_fl, ph_fr;
    float rd_l, rd_r;
    float nz_wl, nz_wr, nz_fl, nz_fr;
    float jit_l, jit_r, jit_depth, jit_alpha;
    float dev_l0, dev_r0;             /* the swing at the last block's end (interpolated from) */
    /* hiss */
    float hiss_t, hiss_s, hiss_lev, hiss_col;
    float pink[2][7];
    float out_gain;
    float pole[7];                    /* ts_swing's block-rate poles, for blocks of pole_m */
    int32_t pole_m;
} tscam_t;

/* constants of the plugin's speed HIGH, age NEW */
#define TS_HEADROOM_DB 1.25f          /* age (0 - 0.5) x 1.5 + speed HIGH's 2 */
#define TS_SAT_MUL 0.9f               /* age 1.0 x speed HIGH 0.9 */
#define TS_WOW_SCALE 0.85f            /* (1 + 1.6 x age 0) x 0.85 */
#define TS_FLT_SCALE 0.85f
#define TS_DEPTH_MUL 0.8f             /* age's 0.8 + 0.5 x 0: wow randomness 0.2, flutter turbulence 0.1 */
#define TS_WOW_RAND 0.2f
#define TS_FLT_TURB 0.1f
#define TS_INPUT 0.7f
#define TS_LOW_K 0.014145f            /* 1 - exp(-2 pi 100 / 44100) */
#define TS_REF 0.5f                   /* the level the output trim keeps: a peak at -6 dBFS */

static void ts_lowpass(ts_bq_t *q, float hz)
{
    float w0 = FM_TWO_PI * hz / SR, c = fm_cosf(w0), al = fm_sinf(w0) / (2.0f * 0.7071f), ia = 1.0f / (1.0f + al);
    q->b0 = (1.0f - c) * 0.5f * ia;
    q->b1 = (1.0f - c) * ia;
    q->b2 = q->b0;
    q->a1 = -2.0f * c * ia;
    q->a2 = (1.0f - al) * ia;
}
static void ts_highpass(ts_bq_t *q, float hz)
{
    float w0 = FM_TWO_PI * hz / SR, c = fm_cosf(w0), al = fm_sinf(w0) / (2.0f * 0.7071f), ia = 1.0f / (1.0f + al);
    q->b0 = (1.0f + c) * 0.5f * ia;
    q->b1 = -(1.0f + c) * ia;
    q->b2 = q->b0;
    q->a1 = -2.0f * c * ia;
    q->a2 = (1.0f - al) * ia;
}
static inline float ts_bq(ts_bq_t *q, float x)          /* transposed direct form II, as the plugin's */
{
    float y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2;
    q->z2 = q->b2 * x - q->a2 * y;
    return y;
}
static inline void ts_bq_flush(ts_bq_t *q)
{
    q->z1 = fm_flush(q->z1);
    q->z2 = fm_flush(q->z2);
}
static inline float ts_rand(tscam_t *t)                  /* the plugin's LCG: -1..1 */
{
    t->rng = t->rng * 1664525u + 1013904223u;
    return (float)t->rng * 4.6566128731e-10f - 1.0f;
}

/* the knobs into the gain stage's numbers (v2_GS_SetParams), the saturation's target (v2_TapeSat_SetDrive),
 * the line's and the hiss's targets, and the output trim */
static void ts_set(tscam_t *t, float drive, float color, float wobble, float noise)
{
    float ds = drive * drive, sat, g_gs, g_sat;     /* (g_gs, g_sat: the level's reckoning, below) */
    t->drive = drive;
    t->color = color;
    t->wobble = wobble;
    t->noise = noise;
    t->trim = fm_db2lin(-2.0f + ds * 34.0f + TS_HEADROOM_DB);
    t->chan = fm_db2lin(-2.0f + drive * 26.0f + TS_HEADROOM_DB);
    t->drive_n = drive;
    t->char_ = color;
    t->drv_scale = 0.8f + 1.2f * color;
    t->soft1 = 0.8f + 0.4f * (1.0f - color);
    t->soft2 = 1.05f + 1.1f * color;
    t->asym2 = 0.02f + 0.08f * color;
    t->gs_mix = fm_minf(drive * 4.0f, 1.0f);
    t->sat_t = color > 0.0f ? fm_powf(color, 0.8f) : 0.0f;
    t->wf_t = wobble;
    t->hiss_t = noise;
    /* the jitter of speed HIGH (v2_WF_SetBaselineMotion: wobble x 0.15 of 0.6 ms); age NEW: no drift */
    t->jit_depth = wobble * 0.15f * 0.0006f;
    t->jit_alpha = t->jit_depth > 0.0f ? 0.0008f + 0.0025f * wobble * 0.15f : 0.0f;
    /* the level: a peak at TS_REF followed through the chain (the input, the gain stage's two clippers and
     * its blend, the saturation: each clips, so their small-signal gains would overstate a loud bus), and
     * the output trimmed to give it back at TS_REF */
    {
        float d1 = 1.0f + drive * t->drv_scale, x = TS_REF * TS_INPUT, s1, s2, y;
        s1 = fm_tanhf(t->trim * d1 * t->soft1 * x);
        s2 = fm_minf(fm_tanhf(t->chan * d1 * t->soft2 * s1), 0.98f);
        t->gs_low = s2 / s1;                                      /* the lows around the second clipper */
        y = x + t->gs_mix * (fm_tanhf(s2) - x);                   /* (the sum's soft clip) */
        sat = fm_clampf(t->sat_t * 1.35f * TS_SAT_MUL, 0.0f, 2.0f);
        if (sat >= 0.01f)
            y = fm_tanhf((1.0f + sat * 2.5f) * y);
        g_gs = y;
        g_sat = 0.95f;                                            /* (the output tanh's 0.95) */
        t->out_gain = TS_REF / (g_gs * g_sat);
    }
    t->on = drive > 0.0f || color > 0.0f || wobble > 0.0f || noise > 0.0f;
    t->pole_m = 0;                    /* (the jitter's pole follows wobble) */
}

static void ts_init(tscam_t *t, float *dl, float *dr)
{
    uint32_t i;
    uint8_t *z = (uint8_t *)t;
    for (i = 0; i < sizeof *t; i++)
        z[i] = 0;
    t->dl = dl;
    t->dr = dr;
    for (i = 0; i < MST_TAPE_DLY; i++)
        dl[i] = dr[i] = 0.0f;
    t->rng = 0x1234567u ^ 44100u;
    for (i = 0; i < 2; i++) {
        ts_highpass(&t->pre_hp[i], 144.0f);                        /* speed HIGH */
        ts_lowpass(&t->pre_lp[i], 5720.0f);
        ts_lowpass(&t->post_lp[i], 17500.0f);
        ts_lowpass(&t->sat_lp[i], 18000.0f);
    }
    t->rd_l = TS_BASE_L;
    t->rd_r = TS_BASE_R;
    t->wf_slew = 0.9f;
    t->ph_wr = 0.25f;
    t->ph_fr = 0.33f;
    ts_set(t, 0.0f, 0.0f, 0.0f, 0.0f);
}

/* a block's controls (the plugin's *_UpdateControls, once a process call there) */
static void ts_controls(tscam_t *t)
{
    float cut, amt, act;
    /* saturation: its drive glides, the HF roll-off follows it (18 kHz .. 10 kHz; speed HIGH's 18 kHz cap) */
    t->sat_s += (t->sat_t - t->sat_s) * 0.3f;
    if (fm_fabsf(t->sat_t - t->sat_s) < 1e-4f)
        t->sat_s = t->sat_t;
    t->sat_f = fm_clampf(t->sat_s * 1.35f * TS_SAT_MUL, 0.0f, 2.0f);
    cut = fm_minf(18000.0f - t->sat_s * 8000.0f, 18000.0f);
    if (t->sat_s >= 0.01f) {
        ts_lowpass(&t->sat_lp[0], cut);
        t->sat_lp[1].b0 = t->sat_lp[0].b0; t->sat_lp[1].b1 = t->sat_lp[0].b1; t->sat_lp[1].b2 = t->sat_lp[0].b2;
        t->sat_lp[1].a1 = t->sat_lp[0].a1; t->sat_lp[1].a2 = t->sat_lp[0].a2;
    }
    /* wow / flutter (v2_WF_UpdateControls) */
    t->wf_s += (t->wf_t - t->wf_s) * 0.3f;
    act = t->wf_t > 0.0005f ? 1.0f : 0.0f;
    t->wf_ramp += (act - t->wf_ramp) * 0.4f;
    amt = t->wf_s * t->wf_ramp;
    t->wf_shape = amt * amt;
    {
        float slew = amt <= 0.0005f ? 0.97f : fm_clampf(0.94f - 0.28f * t->wf_shape, 0.7f, 0.95f);
        t->wf_slew += (slew - t->wf_slew) * 0.2f;
    }
    t->wf_wow = (0.05f + 0.95f * t->wf_shape) * t->wf_ramp;
    t->wf_flt = (0.03f + 0.97f * t->wf_shape) * t->wf_ramp;
    /* hiss: -60 .. -6 dB, white into pink with the amount */
    t->hiss_s += (t->hiss_t - t->hiss_s) * 0.5f;
    if (t->hiss_s < 0.0005f) {
        t->hiss_lev = 0.0f;
        t->hiss_col = 0.0f;
    } else {
        t->hiss_lev = fm_db2lin(-60.0f + t->hiss_s * 54.0f);
        t->hiss_col = t->hiss_s;
    }
    t->gs_mix_c += (t->gs_mix - t->gs_mix_c) * 0.3f;
}

/* tanh as a [7/6] Pade approximant (Lambert's continued fraction), held at +-4.97 where it reaches 1:
 * within 1e-4 of tanh everywhere, and no exponential: the tape's eight a sample are most of its cost */
static inline float ts_tanh(float x)
{
    float a = fm_clampf(x, -4.97f, 4.97f), x2 = a * a;
    return a * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2))) / (135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2)));
}

static inline float ts_cubic(const float *b, float idx)                 /* WF_InterpolateCubic */
{
    int32_t i1 = (int32_t)idx;
    float fr = idx - (float)i1, y0, y1, y2, y3, c1, c2, c3;
    y0 = b[(uint32_t)(i1 - 1) & (MST_TAPE_DLY - 1)];
    y1 = b[(uint32_t)i1 & (MST_TAPE_DLY - 1)];
    y2 = b[(uint32_t)(i1 + 1) & (MST_TAPE_DLY - 1)];
    y3 = b[(uint32_t)(i1 + 2) & (MST_TAPE_DLY - 1)];
    c1 = 0.5f * (y2 - y0);
    c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * fr + c2) * fr + c1) * fr + y1;
}

static inline float ts_pink(float *s, float white)                     /* v2_Hiss_ProcessPink */
{
    float pink;
    s[0] = 0.99886f * s[0] + white * 0.0555179f;
    s[1] = 0.99332f * s[1] + white * 0.0750759f;
    s[2] = 0.96900f * s[2] + white * 0.1538520f;
    s[3] = 0.86650f * s[3] + white * 0.3104856f;
    s[4] = 0.55000f * s[4] + white * 0.5329522f;
    s[5] = -0.7616f * s[5] - white * 0.0168980f;
    pink = s[0] + s[1] + s[2] + s[3] + s[4] + s[5] + s[6] + white * 0.5362f;
    s[6] = white * 0.115926f;
    return pink * 0.11f;
}

/* a per-sample one-pole of white noise (coefficient a) stepped once a block of m instead: its coefficient
 * and the input's scale that keep the output's variance (a / (2 - a) of the input's) */
static inline void ts_block_pole(float a, float fm, float *A, float *g)
{
    *A = 1.0f - fm_powf(1.0f - a, fm);
    *g = fm_sqrtf(a * (2.0f - *A) / (*A * (2.0f - a)));
}

/* the swing of the line at the end of a block of m samples (seconds -> samples), and the phases moved on */
static void ts_swing(tscam_t *t, int m, float *dev_l, float *dev_r)
{
    const float fm = (float)m, isr = fm / SR;
    const float wow = t->wf_wow * TS_DEPTH_MUL * TS_WOW_SCALE, flt = t->wf_flt * TS_DEPTH_MUL * TS_FLT_SCALE;
    float ds = fm_sqrtf(t->wf_shape), wmix, fmix, sh_wl, sh_wr, sh_fl, sh_fr;
    float *pc = t->pole;
    if (t->pole_m != m) {             /* the block-rate poles for this length (almost always 32): once */
        t->pole_m = m;
        ts_block_pole(0.00018f * (1.0f + TS_WOW_RAND), fm, &pc[0], &pc[1]);
        ts_block_pole(0.00025f * (1.0f + TS_FLT_TURB), fm, &pc[2], &pc[3]);
        if (t->jit_alpha > 0.0f)
            ts_block_pole(t->jit_alpha, fm, &pc[4], &pc[5]);
        pc[6] = fm_powf(0.9996f, fm);
    }
    t->nz_wl += (ts_rand(t) * pc[1] - t->nz_wl) * pc[0];
    t->nz_wr += (ts_rand(t) * pc[1] - t->nz_wr) * pc[0];
    t->nz_fl += (ts_rand(t) * pc[3] - t->nz_fl) * pc[2];
    t->nz_fr += (ts_rand(t) * pc[3] - t->nz_fr) * pc[2];
    if (t->jit_alpha > 0.0f) {        /* speed HIGH's flutter jitter: towards a fresh random target */
        float tl = ts_rand(t) * pc[5] * t->jit_depth, tr = tl + 0.08f * ts_rand(t) * pc[5] * t->jit_depth;
        t->jit_l += (tl - t->jit_l) * pc[4];
        t->jit_r += (tr - t->jit_r) * pc[4];
    } else {
        t->jit_l *= pc[6];
        t->jit_r *= pc[6];
    }
    t->ph_wl += 0.3f * isr;
    t->ph_fl += 4.0f * isr;
    t->ph_wr += 0.3f * 1.03f * isr;
    t->ph_fr += 4.0f * 0.97f * isr;
    t->ph_wl -= fm_floorf(t->ph_wl);
    t->ph_fl -= fm_floorf(t->ph_fl);
    t->ph_wr -= fm_floorf(t->ph_wr);
    t->ph_fr -= fm_floorf(t->ph_fr);
    wmix = fm_clampf(TS_WOW_RAND * (0.10f + 0.45f * ds), 0.0f, 0.65f);
    fmix = fm_clampf(TS_FLT_TURB * (0.12f + 0.45f * ds), 0.0f, 0.7f);
    sh_wl = (1.0f - wmix) * fm_sin_turns(t->ph_wl) + wmix * t->nz_wl;
    sh_fl = (1.0f - fmix) * fm_sin_turns(t->ph_fl) + fmix * t->nz_fl;
    sh_wr = (1.0f - wmix) * fm_sin_turns(t->ph_wr) + wmix * t->nz_wr;
    sh_fr = (1.0f - fmix) * fm_sin_turns(t->ph_fr) + fmix * t->nz_fr;
    *dev_l = fm_clampf((t->jit_l + 0.035f * wow * sh_wl + 0.002f * flt * sh_fl) * SR, -TS_MAXDEV, TS_MAXDEV);
    *dev_r = fm_clampf((t->jit_r + 0.035f * wow * 1.03f * sh_wr + 0.002f * flt * 0.97f * sh_fr) * SR, -TS_MAXDEV,
                       TS_MAXDEV);
}

/* the tape taken out (all four knobs at 0): back at rest, so it comes in clean */
static void ts_rest(tscam_t *t)
{
    int i, k;
    ts_bq_t *q[8] = {&t->pre_hp[0], &t->pre_hp[1], &t->pre_lp[0], &t->pre_lp[1], &t->post_lp[0], &t->post_lp[1],
                     &t->sat_lp[0], &t->sat_lp[1]};
    for (i = 0; i < 8; i++)
        q[i]->z1 = q[i]->z2 = 0.0f;
    for (i = 0; i < MST_TAPE_DLY; i++)
        t->dl[i] = t->dr[i] = 0.0f;
    for (i = 0; i < 2; i++)
        for (k = 0; k < 7; k++)
            t->pink[i][k] = 0.0f;
    t->low_lp[0] = t->low_lp[1] = 0.0f;
    t->sat_s = t->wf_s = t->wf_ramp = t->wf_xf = t->hiss_s = t->gs_mix_c = 0.0f;
    t->jit_l = t->jit_r = t->dev_l0 = t->dev_r0 = 0.0f;
    t->rd_l = TS_BASE_L;
    t->rd_r = TS_BASE_R;
}

/* the gain stage on one side (v2_GS_Process): the states in locals (the stores to x would otherwise
 * make the compiler reload them every sample) */
static void ts_gs_side(tscam_t *t, float *x, int m, int side)
{
    ts_bq_t hp = t->pre_hp[side], lp = t->pre_lp[side], po = t->post_lp[side];
    float ll = t->low_lp[side];
    const float k1 = t->trim * (1.0f + t->drive_n * t->drv_scale) * t->soft1;
    const float k2 = t->chan * (1.0f + t->drive_n * t->drv_scale) * t->soft2;
    const float as = 0.01f + 0.03f * t->char_, a2 = side ? -t->asym2 : t->asym2, mx = t->gs_mix_c, gl = t->gs_low;
    int i;
    for (i = 0; i < m; i++) {
        float in = x[i] * TS_INPUT, v, y, low;
        v = ts_tanh(in * k1);
        v += as * v * fm_fabsf(v);                     /* (v * v * sign v) */
        y = hp.b0 * v + hp.z1;                         /* the pre-EQ: high-pass 144 Hz, low-pass 5.72 kHz */
        hp.z1 = hp.b1 * v - hp.a1 * y + hp.z2;
        hp.z2 = hp.b2 * v - hp.a2 * y;
        ll += TS_LOW_K * ((v - y) - ll);               /* what the high-pass took, its lows: kept */
        low = ll;
        v = lp.b0 * y + lp.z1;
        lp.z1 = lp.b1 * y - lp.a1 * v + lp.z2;
        lp.z2 = lp.b2 * y - lp.a2 * v;
        v = ts_tanh(v * k2);
        v += as * v * fm_fabsf(v);
        v += a2 * v * v;                               /* the second stage's asymmetry, opposite a side */
        y = po.b0 * v + po.z1;                         /* the post low-pass 17.5 kHz */
        po.z1 = po.b1 * v - po.a1 * y + po.z2;
        po.z2 = po.b2 * v - po.a2 * y;
        y = gl > 0.0f ? ts_tanh(fm_clampf(y, -0.98f, 0.98f) + gl * low) : fm_clampf(y, -0.98f, 0.98f);
        x[i] = in + mx * (y - in);
    }
    ts_bq_flush(&hp);
    ts_bq_flush(&lp);
    ts_bq_flush(&po);
    t->low_lp[side] = fm_flush(ll);
    t->pre_hp[side] = hp;
    t->pre_lp[side] = lp;
    t->post_lp[side] = po;
}

/* the tape saturation on one side (v2_TapeSat_Process) */
static void ts_sat_side(tscam_t *t, float *x, int m, int side)
{
    ts_bq_t q = t->sat_lp[side];
    const float d = 1.0f + t->sat_f * 2.5f;
    int i;
    for (i = 0; i < m; i++) {
        float v = ts_tanh(x[i] * d), y = q.b0 * v + q.z1;
        q.z1 = q.b1 * v - q.a1 * y + q.z2;
        q.z2 = q.b2 * v - q.a2 * y;
        x[i] = y;
    }
    ts_bq_flush(&q);
    t->sat_lp[side] = q;
}

/* the wow and flutter line on one side: the swing (d0 -> d1 across the block), the head's slew, cubic */
static float ts_wf_side(float *x, float *b, uint32_t w0, int m, float base, float d0, float d1, float rd, float sl, float xf0,
                        float xt, float *xf_out)
{
    const float dd = (d1 - d0) / (float)m, isl = 1.0f - sl;
    uint32_t w = w0;
    float d = d0, xf = xf0;
    int i;
    for (i = 0; i < m; i++) {
        float tg, idx, y, v = x[i];
        d += dd;
        xf += (xt - xf) * 0.004f;
        tg = fm_maxf(base + d, 0.0002f * SR);
        rd = sl * rd + isl * tg;
        idx = (float)w - rd;
        if (idx < 0.0f)
            idx += (float)MST_TAPE_DLY;
        y = ts_cubic(b, idx);
        b[w] = v;
        w = (w + 1u) & (MST_TAPE_DLY - 1);
        x[i] = v + xf * (y - v);
    }
    *xf_out = xf;
    return rd;
}

/* a block (m <= TS_SUB) of the bus in place */
static void ts_block(tscam_t *t, float *l, float *r, int m)
{
    const float og = t->out_gain * 0.95f;
    int i;
    ts_controls(t);
    if (t->gs_mix_c > 0.0005f) {      /* the gain stage, faded in by drive's first quarter */
        ts_gs_side(t, l, m, 0);
        ts_gs_side(t, r, m, 1);
    } else {
        for (i = 0; i < m; i++) {
            l[i] *= TS_INPUT;
            r[i] *= TS_INPUT;
        }
    }
    if (t->sat_s >= 0.01f && t->sat_f >= 0.01f) {           /* the tape saturation */
        ts_sat_side(t, l, m, 0);
        ts_sat_side(t, r, m, 1);
    }
    {   /* wow and flutter: bypassed (the line still written) while it rests, as the plugin */
        const int drift = t->jit_alpha > 0.0f;
        if (!drift && t->wf_ramp < 1.0e-4f && t->wf_xf < 1.0e-3f) {
            uint32_t w = t->w;
            for (i = 0; i < m; i++) {
                t->dl[w] = l[i];
                t->dr[w] = r[i];
                w = (w + 1u) & (MST_TAPE_DLY - 1);
            }
            t->w = w;
            t->wf_xf = 0.0f;
        } else {
            float dl1, dr1, xf;
            const float xt = (t->wf_ramp > 0.001f || drift) ? 1.0f : t->wf_ramp;
            ts_swing(t, m, &dl1, &dr1);
            t->rd_l = ts_wf_side(l, t->dl, t->w, m, TS_BASE_L, t->dev_l0, dl1, t->rd_l, t->wf_slew, t->wf_xf, xt, &xf);
            t->rd_r = ts_wf_side(r, t->dr, t->w, m, TS_BASE_R, t->dev_r0, dr1, t->rd_r, t->wf_slew, t->wf_xf, xt, &xf);
            t->w = (t->w + (uint32_t)m) & (MST_TAPE_DLY - 1);
            t->dev_l0 = dl1;
            t->dev_r0 = dr1;
            t->wf_xf = xf;
        }
    }
    if (t->hiss_lev > 0.0f) {         /* hiss, stereo (widen on) */
        const float lev = t->hiss_lev, col = t->hiss_col, wh = lev * (1.0f - col), pk = lev * col;
        uint32_t rn = t->rng;
        float pl[7], pr[7];
        int k;
        for (k = 0; k < 7; k++) {
            pl[k] = t->pink[0][k];
            pr[k] = t->pink[1][k];
        }
        for (i = 0; i < m; i++) {
            float wl, wr;
            rn = rn * 1664525u + 1013904223u;
            wl = (float)rn * 4.6566128731e-10f - 1.0f;
            rn = rn * 1664525u + 1013904223u;
            wr = (float)rn * 4.6566128731e-10f - 1.0f;
            l[i] += wh * wl + pk * ts_pink(pl, wl);
            r[i] += wh * wr + pk * ts_pink(pr, wr);
        }
        for (k = 0; k < 7; k++) {
            t->pink[0][k] = fm_flush(pl[k]);
            t->pink[1][k] = fm_flush(pr[k]);
        }
        t->rng = rn;
    }
    for (i = 0; i < m; i++) {         /* widen (the side x1.35), the output trim and its tanh */
        float mid = 0.5f * (l[i] + r[i]), side = 0.675f * (l[i] - r[i]);
        l[i] = ts_tanh((mid + side) * og);
        r[i] = ts_tanh((mid - side) * og);
    }
}
