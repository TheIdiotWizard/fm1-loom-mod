/* SPDX-License-Identifier: MIT */
/* TapeScam-lite (after TapeScam, MIT) on the master: LOOM-8's "Master tape" page. Included by fx.c only
 * (static code, no unit of its own).
 *
 * After schwung-tapescam (src/dsp/tapescam.c, 0.5.2), Copyright (c) 2025 Charles Vestal, MIT License (the
 * licence text: LICENSING.md), itself from TapeScamVST: its numbers (the gain stage's drive, trim and
 * softness, the saturation's drive and roll-off, wow and flutter, the hiss) at the plugin's speed HIGH,
 * age NEW, compression OFF, tone flat, widen ON, input 0.7; its chain made lighter and cleaner. The chain:
 *   input 0.7 -> split at TS_XO (the highs: a 2nd-order high-pass, two one-poles; the lows: the input
 *   minus the highs, so the two always sum back to the input) ->
 *     lows: a gentle cubic, at the gain the highs have at a loud bus's level (level-matched);
 *     highs: the gain stage's 5 kHz voicing (drive's first quarter fades it in) -> up to 2x (polyphase
 *     IIR half-band) -> ONE clipper for the gain stage and the tape saturation, with first-order ADAA ->
 *     down to 1x (half-band) -> the saturation's HF roll-off (18 kHz .. 10 kHz with colour) ->
 *   lows + highs -> wow and flutter (one swing for both sides: one tape) -> hiss -> widen (the side
 *   x1.35) -> trim -> a cubic soft clip (slope 1).
 * Four knobs reach it: drive (the gain stage), color (its character and the tape saturation), wobble
 * (wow and flutter), noise (hiss).
 *
 * What it does differently from the plugin, and why:
 *   - aliasing (what made the full port metallic on the FM-1): the plugin's three clippers (the gain
 *     stage's two, the saturation's tanh) in series, with only a 5 kHz low-pass between the first two,
 *     are taken as one clipper (the product of their gains; the 5 kHz low-pass moved before it): one
 *     memoryless curve, which then runs at 2x with ADAA. The curve is a cubic smoothstep (flat at +-1
 *     from |x| = 1.5) for its polynomial antiderivative: tanh's would need a log-cosh. 2x alone or ADAA
 *     alone left -18..-31 dB of aliases at heavy settings; both, about -40;
 *   - the lows: the gain stage's 144 Hz high-pass (a pedal's voicing) took the kick, and the plugin's wow,
 *     separate for the two sides (0.3 / 0.309 Hz and their own noise, on 5 / 5.5 ms lines), made the
 *     sides' lows drift apart in phase: summed to mono (the FM-1's speaker) the kick lost 3-4 dB more.
 *     Here the lows go around the clipper, and the two sides share one swing and one 5 ms tap (one tape
 *     moving past two heads);
 *   - the level: the trim is reckoned at TS_REF, a loud bus's RMS, so a turn of drive or color changes
 *     the character, not the loudness; the output's soft clip has slope 1;
 *   - the gain stage's asymmetry (its v^2 term) is gone: on the clipped highs it made mostly DC, and the
 *     DC blocker it would need cost more than the asymmetry gave;
 *   - 44.1 kHz (every coefficient from SR, the delays in ms); float, fastmath.h; the controls move once a
 *     block of 32, the swing is computed once a block and interpolated across it (its noise's one-poles
 *     stepped a block at a time, their per-sample variance kept), the read head's slew per sample;
 *   - the line is MST_TAPE_DLY (512) floats a side in the pool: 5 ms + the 4.2 ms clamp of the swing
 *     need 406 samples;
 *   - drive = 0 takes the gain stage out (the first quarter of the knob fades it in, so a turn up from 0
 *     does not jump), color = 0 the saturation, wobble = 0 the line, noise = 0 the hiss; all four at 0
 *     take the whole tape out, the widening and the output stage too: bit-transparent. */

#define TS_SUB 32
#define TS_BASE (0.005f * SR)         /* the line's tap: 5 ms, both sides */
#define TS_MAXDEV (0.0042f * SR)
#define TS_XO 120.0f                  /* the split, Hz */
#define TS_REF 0.08f                  /* the bus level (RMS, after the input's 0.7) the trim and the lows are matched at */

typedef struct {
    float a1, d1;                     /* the split's two one-poles */
    float lz;                         /* the gain stage's 5 kHz low-pass */
    float xc, fc;                     /* ADAA: the clipper's last input and its antiderivative there */
    float up[6], dn[8];               /* the half-bands' allpass states (x, y a section) */
    float sz;                         /* the saturation's roll-off */
    float pk[3];                      /* the hiss's pink filter */
} ts_side_t;

typedef struct {
    float *dl, *dr;                   /* the wow / flutter line, MST_TAPE_DLY a side */
    uint32_t w;
    uint32_t rng;
    /* the knobs, 0..1 */
    float drive, color, wobble, noise;
    uint8_t on;                       /* any knob up: the tape is in the path */
    /* the gain stage and the saturation */
    float trim, chan, drv_scale, soft1, soft2, gs_mix, gs_mix_c;
    float k, glo, lsat, kx, kl;       /* the clipper's gain (both of the gain stage's), the lows' gain and drive, the split's and the voicing's coefficients */
    float sat_t, sat_s, sat_f, sat_k;
    /* wow / flutter */
    float wf_t, wf_s, wf_ramp, wf_xf, wf_shape, wf_slew, wf_wow, wf_flt;
    float ph_w, ph_f, rd, nz_w, nz_f, jit, jit_depth, jit_alpha, dev0;
    float pole[7];                    /* ts_swing's block-rate poles, for blocks of pole_m */
    int32_t pole_m;
    /* hiss */
    float hiss_t, hiss_s, hiss_lev, hiss_col;
    float out_gain;
    ts_side_t s[2];
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

/* the clipper: a cubic, slope 1 at 0, flat at +-1 from |x| = 1.5 (C1 there), and its antiderivative */
static inline float ts_q(float x)
{
    float u = fm_minf(fm_maxf(x, -1.5f), 1.5f);
    return u * (1.0f - 0.148148148f * u * u);
}
static inline float ts_qf(float ax)                      /* of |x| (it is even) */
{
    float c = fm_minf(ax, 1.5f), s = c * c;
    return s * (0.5f - 0.037037037f * s) + ax - c;
}
/* first-order ADAA: the clipper averaged over the straight line from the last input to this one; inputs
 * close together (relative to their size, for float's sake) take the clipper at their midpoint */
static inline float ts_adaa(float x, float *xp, float *fp)
{
    float ax = fm_fabsf(x), f = ts_qf(ax), d = x - *xp, y;
    if (fm_fabsf(d) > 1.0e-3f + 1.0e-3f * ax)
        y = (f - *fp) / d;
    else
        y = ts_q(0.5f * (x + *xp));
    *xp = x;
    *fp = f;
    return y;
}
/* the half-bands: polyphase IIR, two allpass paths (de Soras' hiir form, coefficients from its designer),
 * transition 0.06 of the 2x rate: up 3 coefficients (images -43 dB), down 4 (-57 dB, flat to 16.8 kHz) */
#define TS_AP(c, x, xm, ym) do { float y_ = (c) * ((x) - (ym)) + (xm); (xm) = (x); (ym) = y_; (x) = y_; } while (0)

static inline float ts_rand(tscam_t *t)                  /* the plugin's LCG: -1..1 */
{
    t->rng = t->rng * 1664525u + 1013904223u;
    return (float)t->rng * 4.6566128731e-10f - 1.0f;
}

/* the knobs into the gain stage's numbers (v2_GS_SetParams), the saturation's target (v2_TapeSat_SetDrive),
 * the line's and the hiss's targets, and the level */
static void ts_set(tscam_t *t, float drive, float color, float wobble, float noise)
{
    float ds = drive * drive, d1, sat, dsat, x = TS_REF, y = x;
    t->drive = drive;
    t->color = color;
    t->wobble = wobble;
    t->noise = noise;
    t->trim = fm_db2lin(-2.0f + ds * 34.0f + TS_HEADROOM_DB);
    t->chan = fm_db2lin(-2.0f + drive * 26.0f + TS_HEADROOM_DB);
    t->drv_scale = 0.8f + 1.2f * color;
    t->soft1 = 0.8f + 0.4f * (1.0f - color);
    t->soft2 = 1.05f + 1.1f * color;
    t->gs_mix = fm_minf(drive * 4.0f, 1.0f);
    t->sat_t = color > 0.0f ? fm_powf(color, 0.8f) : 0.0f;
    t->wf_t = wobble;
    t->hiss_t = noise;
    /* the jitter of speed HIGH (v2_WF_SetBaselineMotion: wobble x 0.15 of 0.6 ms); age NEW: no drift */
    t->jit_depth = wobble * 0.15f * 0.0006f;
    t->jit_alpha = t->jit_depth > 0.0f ? 0.0008f + 0.0025f * wobble * 0.15f : 0.0f;
    /* the level: TS_REF through the highs' section (statically: the 5 kHz low-pass at its DC gain); the
     * trim gives it back where it came in, the lows get the same gain */
    d1 = 1.0f + drive * t->drv_scale;
    t->k = t->trim * d1 * t->soft1 * t->chan * d1 * t->soft2;
    sat = fm_clampf(t->sat_t * 1.35f * TS_SAT_MUL, 0.0f, 2.0f);
    dsat = sat >= 0.01f ? 1.0f + sat * 2.5f : 1.0f;
    if (t->gs_mix > 0.0f)
        y = x + t->gs_mix * (t->k * x - x);
    if (t->gs_mix > 0.0f || sat >= 0.01f)
        y = ts_q(dsat * y);
    t->glo = y / x;
    t->lsat = 0.6f + 1.2f * drive;
    t->out_gain = (x / TS_INPUT) / y;
    t->on = drive > 0.0f || color > 0.0f || wobble > 0.0f || noise > 0.0f;
    t->pole_m = 0;                    /* (the jitter's pole follows wobble) */
}

/* the tape taken out (all four knobs at 0): back at rest, so it comes in clean */
static void ts_rest(tscam_t *t)
{
    int i;
    uint8_t *z = (uint8_t *)t->s;
    for (i = 0; i < (int)sizeof t->s; i++)
        z[i] = 0;
    for (i = 0; i < MST_TAPE_DLY; i++)
        t->dl[i] = t->dr[i] = 0.0f;
    t->sat_s = t->wf_s = t->wf_ramp = t->wf_xf = t->hiss_s = t->gs_mix_c = 0.0f;
    t->jit = t->dev0 = 0.0f;
    t->rd = TS_BASE;
}

static void ts_init(tscam_t *t, float *dl, float *dr)
{
    uint32_t i;
    uint8_t *z = (uint8_t *)t;
    for (i = 0; i < sizeof *t; i++)
        z[i] = 0;
    t->dl = dl;
    t->dr = dr;
    t->rng = 0x1234567u ^ 44100u;
    t->kx = 1.0f - fm_expf(-FM_TWO_PI * TS_XO / SR);
    t->kl = 1.0f - fm_expf(-FM_TWO_PI * 5000.0f / SR);
    t->wf_slew = 0.9f;
    ts_rest(t);
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
    if (t->sat_s >= 0.01f)
        t->sat_k = 1.0f - fm_expf(-FM_TWO_PI * cut / SR);
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

/* a per-sample one-pole of white noise (coefficient a) stepped once a block of m instead: its coefficient
 * and the input's scale that keep the output's variance (a / (2 - a) of the input's) */
static inline void ts_block_pole(float a, float fm, float *A, float *g)
{
    *A = 1.0f - fm_powf(1.0f - a, fm);
    *g = fm_sqrtf(a * (2.0f - *A) / (*A * (2.0f - a)));
}

/* the swing of the line at the end of a block of m samples (seconds -> samples), the phases moved on: the
 * plugin's left side's, for both */
static float ts_swing(tscam_t *t, int m)
{
    const float fm = (float)m, isr = fm / SR;
    const float wow = t->wf_wow * TS_DEPTH_MUL * TS_WOW_SCALE, flt = t->wf_flt * TS_DEPTH_MUL * TS_FLT_SCALE;
    float ds = fm_sqrtf(t->wf_shape), wmix, fmix, sh_w, sh_f;
    float *pc = t->pole;
    if (t->pole_m != m) {             /* the block-rate poles for this length (almost always 32): once */
        t->pole_m = m;
        ts_block_pole(0.00018f * (1.0f + TS_WOW_RAND), fm, &pc[0], &pc[1]);
        ts_block_pole(0.00025f * (1.0f + TS_FLT_TURB), fm, &pc[2], &pc[3]);
        if (t->jit_alpha > 0.0f)
            ts_block_pole(t->jit_alpha, fm, &pc[4], &pc[5]);
        pc[6] = fm_powf(0.9996f, fm);
    }
    t->nz_w += (ts_rand(t) * pc[1] - t->nz_w) * pc[0];
    t->nz_f += (ts_rand(t) * pc[3] - t->nz_f) * pc[2];
    if (t->jit_alpha > 0.0f)          /* speed HIGH's flutter jitter: towards a fresh random target */
        t->jit += (ts_rand(t) * pc[5] * t->jit_depth - t->jit) * pc[4];
    else
        t->jit *= pc[6];
    t->ph_w += 0.3f * isr;
    t->ph_f += 4.0f * isr;
    t->ph_w -= fm_floorf(t->ph_w);
    t->ph_f -= fm_floorf(t->ph_f);
    wmix = fm_clampf(TS_WOW_RAND * (0.10f + 0.45f * ds), 0.0f, 0.65f);
    fmix = fm_clampf(TS_FLT_TURB * (0.12f + 0.45f * ds), 0.0f, 0.7f);
    sh_w = (1.0f - wmix) * fm_sin_turns(t->ph_w) + wmix * t->nz_w;
    sh_f = (1.0f - fmix) * fm_sin_turns(t->ph_f) + fmix * t->nz_f;
    return fm_clampf((t->jit + 0.035f * wow * sh_w + 0.002f * flt * sh_f) * SR, -TS_MAXDEV, TS_MAXDEV);
}

/* the gain stage and the saturation on one side, in four passes (every state in a register): (1) at 1x
 * the split, the lows, the 5 kHz voicing and the blend; (2) up to 2x; (3) the clipper, ADAA; (4) down,
 * the roll-off, the bands summed. (The two buffers: 384 bytes of the interrupt's stack.) */
static void ts_side(tscam_t *t, float *x, int m, ts_side_t *s, int gs, int sat)
{
    const float kx = t->kx, kl = t->kl, mx = t->gs_mix_c, kg = t->k;
    const float dsat = sat ? 1.0f + t->sat_f * 2.5f : 1.0f, ks = t->sat_k;
    const float ls = t->lsat, lg = t->glo / t->lsat, ki = TS_INPUT * dsat;
    float lo[TS_SUB], u[2 * TS_SUB];
    int i;
    {   /* (1) */
        float a1 = s->a1, d1 = s->d1, lz = s->lz;
        for (i = 0; i < m; i++) {
            float in = x[i], hp1, hi, v;           /* (the input's 0.7 folded into ki and the lows' drive) */
            a1 += kx * (in - a1);
            hp1 = in - a1;
            d1 += kx * (hp1 - d1);
            hi = hp1 - d1;
            v = fm_minf(fm_maxf((in - hi) * (ls * TS_INPUT), -1.5f), 1.5f);
            lo[i] = lg * v * (1.0f - v * v * 0.148148148f);
            if (gs) {
                lz += kl * (hi - lz);
                hi += mx * (kg * lz - hi);
            }
            u[2 * i] = ki * hi;
        }
        s->a1 = fm_flush(a1);
        s->d1 = fm_flush(d1);
        s->lz = fm_flush(lz);
    }
    {   /* (2) path 0: c0 c2, path 1: c1 */
        float x0 = s->up[0], y0 = s->up[1], x2 = s->up[2], y2 = s->up[3], x1 = s->up[4], y1 = s->up[5];
        for (i = 0; i < m; i++) {
            float e = u[2 * i], o = e;
            TS_AP(0.1726362110f, e, x0, y0);
            TS_AP(0.8434655794f, e, x2, y2);
            TS_AP(0.5199743642f, o, x1, y1);
            u[2 * i] = e;
            u[2 * i + 1] = o;
        }
        s->up[0] = fm_flush(x0); s->up[1] = fm_flush(y0); s->up[2] = fm_flush(x2);
        s->up[3] = fm_flush(y2); s->up[4] = fm_flush(x1); s->up[5] = fm_flush(y1);
    }
    {   /* (3) */
        float xc = s->xc, fc = s->fc;
        for (i = 0; i < 2 * m; i++)
            u[i] = ts_adaa(u[i], &xc, &fc);
        s->xc = xc;
        s->fc = fc;
    }
    {   /* (4) path 0: c0 c2 on the odd samples, path 1: c1 c3 on the even */
        float x0 = s->dn[0], y0 = s->dn[1], x2 = s->dn[2], y2 = s->dn[3];
        float x1 = s->dn[4], y1 = s->dn[5], x3 = s->dn[6], y3 = s->dn[7], sz = s->sz;
        for (i = 0; i < m; i++) {
            float e = u[2 * i + 1], o = u[2 * i], h;
            TS_AP(0.1091060418f, e, x0, y0);
            TS_AP(0.6334772202f, e, x2, y2);
            TS_AP(0.3616325825f, o, x1, y1);
            TS_AP(0.8774434190f, o, x3, y3);
            h = 0.5f * (e + o);
            if (sat) {
                sz += ks * (h - sz);
                h = sz;
            }
            x[i] = lo[i] + h;
        }
        s->dn[0] = fm_flush(x0); s->dn[1] = fm_flush(y0); s->dn[2] = fm_flush(x2); s->dn[3] = fm_flush(y2);
        s->dn[4] = fm_flush(x1); s->dn[5] = fm_flush(y1); s->dn[6] = fm_flush(x3); s->dn[7] = fm_flush(y3);
        s->sz = fm_flush(sz);
    }
}

/* wow and flutter, both sides on one swing (d0 -> d1 across the block), the head's slew, linear */
static void ts_wf(tscam_t *t, float *l, float *r, int m, float d1, float xt)
{
    const float dd = (d1 - t->dev0) / (float)m, sl = t->wf_slew, isl = 1.0f - sl;
    float *bl = t->dl, *br = t->dr, d = t->dev0, rd = t->rd, xf = t->wf_xf;
    uint32_t w = t->w;
    const int fade = fm_fabsf(xt - xf) > 1.0e-4f || xt < 1.0f;
    int i;
    for (i = 0; i < m; i++) {
        float vl = l[i], vr = r[i], fr, yl, yr;
        int32_t ri;
        uint32_t p, p1;
        d += dd;
        rd = sl * rd + isl * (TS_BASE + d);       /* (>= 5 - 4.2 ms: no floor needed) */
        ri = (int32_t)rd;
        fr = rd - (float)ri;
        p = (w - (uint32_t)ri) & (MST_TAPE_DLY - 1);
        p1 = (p - 1u) & (MST_TAPE_DLY - 1);
        yl = bl[p] + fr * (bl[p1] - bl[p]);
        yr = br[p] + fr * (br[p1] - br[p]);
        bl[w] = vl;
        br[w] = vr;
        w = (w + 1u) & (MST_TAPE_DLY - 1);
        if (fade) {
            xf += (xt - xf) * 0.004f;
            yl = vl + xf * (yl - vl);
            yr = vr + xf * (yr - vr);
        }
        l[i] = yl;
        r[i] = yr;
    }
    t->w = w;
    t->rd = rd;
    t->dev0 = d1;
    t->wf_xf = fade ? xf : xt;
}

/* a block (m <= TS_SUB) of the bus in place */
static void ts_block(tscam_t *t, float *l, float *r, int m)
{
    const float og = t->out_gain;
    int i;
    ts_controls(t);
    {   /* the gain stage (faded in by drive's first quarter) and the saturation */
        const int gs = t->gs_mix_c > 0.0005f, sat = t->sat_s >= 0.01f && t->sat_f >= 0.01f;
        if (gs || sat) {
            ts_side(t, l, m, &t->s[0], gs, sat);
            ts_side(t, r, m, &t->s[1], gs, sat);
        } else {
            for (i = 0; i < m; i++) {
                l[i] *= TS_INPUT;
                r[i] *= TS_INPUT;
            }
        }
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
            const float xt = (t->wf_ramp > 0.001f || drift) ? 1.0f : t->wf_ramp;
            ts_wf(t, l, r, m, ts_swing(t, m), xt);
        }
    }
    if (t->hiss_lev > 0.0f) {         /* hiss, stereo: white + Kellet's economy pink, the constants folded in */
        const float sc = 4.6566128731e-10f, lev = t->hiss_lev, col = t->hiss_col, pk = lev * col * 0.11f * sc;
        const float bw = lev * (1.0f - col) * sc + 0.1848f * pk, b0 = 0.0990460f * pk, b1 = 0.2965164f * pk,
                    b2 = 1.0526913f * pk;
        float l0 = t->s[0].pk[0], l1 = t->s[0].pk[1], l2 = t->s[0].pk[2];
        float r0 = t->s[1].pk[0], r1 = t->s[1].pk[1], r2 = t->s[1].pk[2];
        uint32_t rn = t->rng;
        for (i = 0; i < m; i++) {
            float wl, wr;
            rn = rn * 1664525u + 1013904223u;
            wl = (float)(int32_t)rn;
            rn = rn * 1664525u + 1013904223u;
            wr = (float)(int32_t)rn;
            l0 = 0.99765f * l0 + b0 * wl;
            l1 = 0.96300f * l1 + b1 * wl;
            l2 = 0.57000f * l2 + b2 * wl;
            r0 = 0.99765f * r0 + b0 * wr;
            r1 = 0.96300f * r1 + b1 * wr;
            r2 = 0.57000f * r2 + b2 * wr;
            l[i] += bw * wl + (l0 + l1 + l2);
            r[i] += bw * wr + (r0 + r1 + r2);
        }
        t->s[0].pk[0] = fm_flush(l0); t->s[0].pk[1] = fm_flush(l1); t->s[0].pk[2] = fm_flush(l2);
        t->s[1].pk[0] = fm_flush(r0); t->s[1].pk[1] = fm_flush(r1); t->s[1].pk[2] = fm_flush(r2);
        t->rng = rn;
    }
    for (i = 0; i < m; i++) {         /* widen (the side x1.35), the trim, a cubic soft clip at slope 1 */
        float mid = 0.5f * (l[i] + r[i]), side = 0.675f * (l[i] - r[i]);
        float a = fm_minf(fm_maxf((mid + side) * og, -1.5f), 1.5f), b = fm_minf(fm_maxf((mid - side) * og, -1.5f), 1.5f);
        l[i] = a * (1.0f - 0.148148148f * a * a);
        r[i] = b * (1.0f - 0.148148148f * b * b);
    }
}
