/* SPDX-License-Identifier: GPL-3.0-only */
/* XY-1 drumline (drumline.h): eleven analytic drum models on NDVOICE voices.
 *
 * Every sound is a few oscillators, a noise source and a filter or two, shaped by exponential
 * envelopes (env *= k a sample): no samples, no tables, so a kit costs nothing in flash and a voice
 * stays well under 80 instructions a sample. The models are simplified from X0X's 909 / 808 ports
 * (drum909.c, drum808.c): the kick's pitch drop and click, the snare's two detuned sines over
 * band-passed noise, the hats' six square waves through high-passes, the clap's four bursts, and
 * the 808's conga and toms; the shaker, fm and resonator are our own.
 *
 * Levels: a sound's DP_LEVEL, DP_PAN and the hit's velocity are applied here; the track's TP_SEND1/2/T
 * scale the three send buffers (mono, pre track fader). The track's TP_LEVEL and TP_PAN (and the
 * punch-in mute) belong to the engine layer, which applies them to all five buffers after
 * drumline_render. Pan, level and the sends are re-read every SY_CHUNK samples and ramped across the
 * chunk, so a knob turn is heard without zipper noise.
 *
 * Threads: drumline_hit / drumline_choke and drumline_render run in one context (the audio
 * interrupt: the engine queues the keys' hits). par[] is written by the main loop a byte at a time. */
#include "drumline.h"
#include "synth.h"
#include "fastmath.h"

#define SR SY_SR
#define CR SY_CHUNK                   /* pan / level / sends re-read this often */
#define SUB 32                        /* a voice renders this many at once into a 128 B stack buffer */
#define RESO_LEN 512                  /* a resonator string, int16: 86 Hz at the longest */
#define NRESO 2                       /* strings: resonator sounds past two steal the older's */

const char *const DM_NAME[NDM] = {"kick", "snare", "hat", "open hat", "clap", "low tom", "high tom", "conga",
                                  "shaker", "fm", "resonator"};
const char *const DL_SOUND_NAME[DL_NSOUND] = {"kick", "snare", "hat", "open", "clap", "ltom", "htom", "conga",
                                              "shaker", "fm", "reso"};
/* DP_MODEL, DP_TUNE, DP_TONE, DP_DECAY, DP_PAN, DP_LEVEL */
const uint8_t DL_DEFAULT_KIT[DL_NSOUND][6] = {
    {DM_KICK, 64, 40, 55, 64, 100},
    {DM_SNARE, 64, 64, 50, 64, 90},
    {DM_CHAT, 64, 70, 40, 74, 70},
    {DM_OHAT, 64, 70, 50, 74, 70},
    {DM_CLAP, 64, 60, 50, 54, 80},
    {DM_LTOM, 64, 40, 55, 44, 85},
    {DM_HTOM, 64, 40, 50, 84, 85},
    {DM_CONGA, 64, 50, 45, 54, 80},
    {DM_SHAKER, 64, 50, 35, 80, 60},
    {DM_FM, 64, 50, 40, 64, 75},
    {DM_RESO, 64, 60, 50, 64, 75},
};
volatile uint8_t drumline_hitv[DL_NSOUND];

/* a model's base pitch (hats: a ratio of the 808's metal bank) and its decay range, seconds to -60 dB */
typedef struct { float base, dec_lo, dec_hi; } dm_spec_t;
static const dm_spec_t SPEC[NDM] = {
    [DM_KICK] = {55.0f, 0.08f, 1.6f},     [DM_SNARE] = {185.0f, 0.06f, 1.0f},  [DM_CHAT] = {1.0f, 0.02f, 0.35f},
    [DM_OHAT] = {1.0f, 0.15f, 2.5f},      [DM_CLAP] = {1200.0f, 0.05f, 1.2f},  [DM_LTOM] = {90.0f, 0.1f, 1.5f},
    [DM_HTOM] = {170.0f, 0.08f, 1.2f},    [DM_CONGA] = {220.0f, 0.05f, 0.9f},  [DM_SHAKER] = {4500.0f, 0.02f, 0.4f},
    [DM_FM] = {220.0f, 0.03f, 1.5f},      [DM_RESO] = {220.0f, 0.05f, 2.5f},
};
/* the 808's six metal oscillators (sc808: 205.3, 304.4, 369.6, 522.7, 540, 800 Hz) */
static const float METAL_HZ[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};

typedef struct {
    uint8_t on, sound, model, line;   /* line: the resonator's string, 0..NRESO-1 */
    uint32_t age, rng;
    int32_t cnt, bursts;              /* the clap: samples to the next burst, bursts left */
    float env, k;                     /* the amplitude envelope and its per-sample multiplier */
    float env2, k2;                   /* a second envelope: click, body, burst, attack, FM index */
    float ph, inc, ph2, inc2;         /* oscillators, phase in turns [-0.5, 0.5) */
    float pd, pdk, pda;               /* pitch drop: inc * (1 + pda * pd), pd decays by pdk */
    float f1, f2, f3, f4;             /* filter states */
    float c1, c2, c3;                 /* filter coefficients */
    float tone, g;                    /* tone 0..1, the model's output gain */
    float gl, gr, gs1, gs2, gst;      /* gains now: left, right, the three sends (set each chunk) */
    float dgl, dgr;                   /* left and right ramp toward the chunk's targets */
    uint32_t mp[6], mi[6];            /* the hats' six square phases and increments */
    int32_t len, w;                   /* the resonator: string length, write position */
    float fr, lp, fb, damp, pk;       /* its fractional length, loop filter, feedback, damping, peak */
} dvoice_t;

struct drumline {
    const uint8_t *par;
    dvoice_t v[NDVOICE];
    int8_t line_owner[NRESO];         /* the voice holding each string, -1 */
    int16_t line[NRESO][RESO_LEN];
};
_Static_assert(sizeof(struct drumline) <= DRUMLINE_BYTES, "drumline_t does not fit DRUMLINE_BYTES");

static inline float noise(uint32_t *r)
{
    *r = *r * 1664525u + 1013904223u;
    return (float)(int32_t)*r * (1.0f / 2147483648.0f);
}
/* the multiplier that takes an envelope to -60 dB in t seconds */
static inline float dec_k(float t) { return fm_expf(-6.9078f / (fm_maxf(t, 0.001f) * SR)); }
/* a two-pole resonator (bandpass) at hz, bandwidth bw: y = x + c1 y1 + c2 y2, gain c3 */
static void reso_set(dvoice_t *v, float hz, float bw)
{
    float r = fm_expf(-FM_PI * bw / SR);
    hz = fm_minf(hz, 18000.0f);
    v->c1 = 2.0f * r * fm_cosf(FM_TWO_PI * hz / SR);
    v->c2 = -r * r;
    v->c3 = (1.0f - r) * 2.0f;
}
/* a sine of a phase in turns [-0.5, 0.5): the parabola 4x(1 - |x|) with Blinn's correction, harmonics
 * under -60 dB, a third of fm_sin_pi's cost: these are drums */
static inline float sin_t(float ph)
{
    float x = ph + ph, y = 4.0f * x * (1.0f - fm_fabsf(x));
    return y + 0.225f * (y * fm_fabsf(y) - y);
}
static inline float sin_t_any(float ph)                        /* any phase in turns */
{
    return sin_t(ph - fm_floorf(ph + 0.5f));
}
#define WRAP(ph) do { if ((ph) >= 0.5f) (ph) -= 1.0f; } while (0)

void drumline_init(drumline_t *d, const uint8_t *par)
{
    uint32_t i;
    uint8_t *z = (uint8_t *)d;
    for (i = 0; i < sizeof *d; i++)
        z[i] = 0;
    d->par = par;
    for (i = 0; i < NRESO; i++)
        d->line_owner[i] = -1;
    for (i = 0; i < NDVOICE; i++)
        d->v[i].rng = 0x9E3779B9u * (i + 1);
}

/* a voice for the sound: its own (a retrigger), a free one, else the oldest */
static dvoice_t *alloc(drumline_t *d, int sound)
{
    int i, best = -1;
    uint32_t oldest = 0;
    for (i = 0; i < NDVOICE; i++)
        if (d->v[i].on && d->v[i].sound == sound)
            return &d->v[i];
    for (i = 0; i < NDVOICE; i++)
        if (!d->v[i].on)
            return &d->v[i];
    for (i = 0; i < NDVOICE; i++)
        if (d->v[i].age >= oldest) {
            oldest = d->v[i].age;
            best = i;
        }
    return &d->v[best];
}

static void fade_out(dvoice_t *v)                     /* a choke: 5 ms to silence */
{
    float k = dec_k(0.005f);
    v->k = k;
    v->k2 = k;
    if (v->model == DM_RESO) {
        v->fb = 0.3f;
        v->env2 = 0.0f;
    }
}

void drumline_choke(drumline_t *d)
{
    int i;
    for (i = 0; i < NDVOICE; i++)
        if (d->v[i].on)
            fade_out(&d->v[i]);
}

void drumline_hit(drumline_t *d, int sound, int vel, int pitch_semi, int short_gate, int long_tail)
{
    const uint8_t *p;
    const dm_spec_t *sp;
    dvoice_t *v;
    int model, i;
    float tune, tone, dec, hz, decay_pos;
    if (sound < 0 || sound >= DL_NSOUND)
        return;
    if (vel < 1) vel = 1;
    if (vel > 127) vel = 127;
    p = d->par + TP_DRUM(sound, 0);
    model = p[DP_MODEL] % NDM;
    sp = &SPEC[model];
    tune = ((float)p[DP_TUNE] - 64.0f) * (24.0f / 64.0f) + (float)pitch_semi;   /* +-2 octaves */
    tone = (float)p[DP_TONE] * (1.0f / 127.0f);
    decay_pos = (float)p[DP_DECAY] * (1.0f / 127.0f);
    dec = sp->dec_lo * fm_powf(sp->dec_hi / sp->dec_lo, decay_pos);
    if (short_gate)
        dec = fm_minf(dec, 0.06f);
    if (long_tail)
        dec *= 4.0f;
    hz = sp->base * fm_exp2f(tune * (1.0f / 12.0f));
    drumline_hitv[sound] = (uint8_t)(vel >= 127 ? 255 : vel * 2);

    if (model == DM_CHAT)                             /* the closed hat chokes the open one */
        for (i = 0; i < NDVOICE; i++)
            if (d->v[i].on && d->v[i].model == DM_OHAT)
                fade_out(&d->v[i]);

    v = alloc(d, sound);
    if (v->on && v->model == DM_RESO && model != DM_RESO)
        d->line_owner[v->line] = -1;                  /* it leaves its string */
    {   /* the voice's constant part: the gains start at the chunk's first control read */
        uint32_t rng = v->rng;
        uint8_t line = v->line;
        int keep_line = v->on && v->model == DM_RESO && model == DM_RESO;
        uint8_t *z = (uint8_t *)v;
        uint32_t n;
        for (n = 0; n < sizeof *v; n++)
            z[n] = 0;
        v->rng = rng;
        v->line = keep_line ? line : 0;
        v->on = 1;
        v->sound = (uint8_t)sound;
        v->model = (uint8_t)model;
        v->tone = tone;
        v->env = 1.0f;
        v->k = dec_k(dec);
        v->env2 = 1.0f;
        v->k2 = v->k;
        v->g = (float)vel * (1.0f / 127.0f);          /* the velocity: a gain; the chunk adds level / pan */
        if (!keep_line && model == DM_RESO) {         /* a string: a free one, else the older owner's */
            int li = -1, oldest = -1;
            for (i = 0; i < NRESO; i++)
                if (d->line_owner[i] < 0) {
                    li = i;
                    break;
                }
            if (li < 0) {
                uint32_t age = 0;
                for (i = 0; i < NRESO; i++) {
                    dvoice_t *o = &d->v[d->line_owner[i]];
                    if (o->age >= age) {
                        age = o->age;
                        li = i;
                        oldest = d->line_owner[i];
                    }
                }
                d->v[oldest].on = 0;
            }
            d->line_owner[li] = (int8_t)(v - d->v);
            v->line = (uint8_t)li;
            for (i = 0; i < RESO_LEN; i++)
                d->line[li][i] = 0;
        }
    }
    v->inc = hz / SR;
    switch (model) {
    case DM_KICK:                                     /* a sine falling 4x in ~35 ms; tone = click and drive */
        v->pda = 4.0f;
        v->pd = 1.0f;
        v->pdk = dec_k(0.035f * (0.6f + 0.8f * tone));
        v->k2 = dec_k(0.0025f);                       /* the click: a 2.5 ms noise burst */
        break;
    case DM_SNARE:                                    /* two sines (1 : 1.58) over band-passed noise */
        v->inc2 = v->inc * 1.58f;
        v->pda = 0.6f;
        v->pd = 1.0f;
        v->pdk = dec_k(0.012f);
        v->k2 = dec_k(dec * 0.5f);                    /* the body falls faster than the noise */
        reso_set(v, 1500.0f * fm_exp2f(tone * 2.0f), 2500.0f);
        break;
    case DM_CHAT:
    case DM_OHAT: {                                   /* six squares, two high-passes: tone = their cutoff */
        float ratio = fm_exp2f(tune * (1.0f / 12.0f)), fc = 2000.0f * fm_exp2f(tone * 2.0f);
        for (i = 0; i < 6; i++) {
            v->mi[i] = (uint32_t)(METAL_HZ[i] * ratio * (4294967296.0f / SR));
            v->mp[i] = v->rng * (uint32_t)(i * 0x3C6EF372u);
        }
        v->c1 = 1.0f - fm_expf(-FM_TWO_PI * fc / SR);
        v->k2 = dec_k(model == DM_OHAT ? 0.004f : 0.002f);   /* a touch of click at the start */
        /* the high-passes take most of the squares: make up for it, less where they pass more
         * (a low cutoff, a high tuning) */
        v->g *= (2.5f + 4.0f * tone) * fm_rsqrtf(ratio);
        break;
    }
    case DM_CLAP:                                     /* four 8 ms bursts 10 ms apart, then the tail */
        v->k2 = dec_k(0.008f);
        v->cnt = 441;
        v->bursts = 3;
        reso_set(v, hz * fm_exp2f(tone * 1.5f - 0.5f), 900.0f);
        break;
    case DM_LTOM:
    case DM_HTOM:                                     /* a sine falling an octave over 60 ms, a little noise */
        v->pda = 1.0f;
        v->pd = 1.0f;
        v->pdk = dec_k(0.06f);
        v->k2 = dec_k(0.03f + 0.1f * tone);
        v->c1 = 1.0f - fm_expf(-FM_TWO_PI * (600.0f + 4000.0f * tone) / SR);
        break;
    case DM_CONGA:                                    /* a short pitch drop, a second partial (tone) */
        v->inc2 = v->inc * 2.4f;
        v->pda = 0.4f;
        v->pd = 1.0f;
        v->pdk = dec_k(0.006f);
        v->k2 = dec_k(dec * 0.35f);
        break;
    case DM_SHAKER:                                   /* noise through a resonator; tone = its resonance */
        v->k2 = dec_k(0.004f);                        /* the attack: 4 ms */
        reso_set(v, hz, 3000.0f - 2700.0f * tone);
        v->g *= 1.0f + 0.8f * tone;                   /* a narrow band is quieter in RMS, peakier */
        break;
    case DM_FM:                                       /* 2-op: tone = the modulator's ratio and index */
        v->inc2 = v->inc * 0.5f * fm_exp2f(tone * 4.3f);   /* 0.5 .. 10 x */
        v->pda = (1.5f + 6.0f * tone) * (1.0f / FM_TWO_PI);   /* the index (radians), falling with env2 */
        v->k2 = dec_k(dec * 0.4f);
        break;
    case DM_RESO: {                                   /* a noise burst into a Karplus-Strong string */
        float len = SR / hz;
        if (len > (float)(RESO_LEN - 2))
            len = (float)(RESO_LEN - 2);
        if (len < 4.0f)
            len = 4.0f;
        v->len = (int32_t)len;
        v->fr = len - (float)v->len;
        v->fb = fm_exp2f(-9.966f / (dec * hz));       /* -60 dB after dec seconds of round trips */
        if (v->fb > 0.999f)
            v->fb = 0.999f;
        v->damp = 0.15f + 0.85f * tone;               /* the loop's low-pass: tone = brightness */
        v->k2 = dec_k(fm_maxf(0.004f, 1.5f * len / SR));   /* the burst: at least a period, so the
                                                              * string fills all the way round */
        v->pk = 1.0f;
        break;
    }
    }
}

/* ---- one voice, m samples (<= SUB) of mono into t[] ---- */
static void render_voice(dvoice_t *v, int16_t *line, float *t, int m)
{
    int i;
    float env = v->env, k = v->k, env2 = v->env2, k2 = v->k2, ph = v->ph, inc = v->inc, g = v->g;
    uint32_t rng = v->rng;
    switch (v->model) {
    case DM_KICK: {
        float pd = v->pd, pdk = v->pdk, pda = v->pda, drive = 1.0f + 2.5f * v->tone, click = 0.6f * v->tone;
        for (i = 0; i < m; i++) {
            float y;
            pd *= pdk;
            ph += inc * (1.0f + pda * pd);
            WRAP(ph);
            y = sin_t(ph) * env * drive + noise(&rng) * env2 * click;
            y = y > 1.0f ? 1.0f : y < -1.0f ? -1.0f : y * (1.5f - 0.5f * y * y);   /* a cubic clip */
            t[i] = y * 0.8f * g;
            env *= k;
            env2 *= k2;
        }
        v->pd = pd;
        break;
    }
    case DM_SNARE: {
        float pd = v->pd, pdk = v->pdk, pda = v->pda, ph2 = v->ph2, inc2 = v->inc2;
        float c1 = v->c1, c2 = v->c2, c3 = v->c3, f1 = v->f1, f2 = v->f2;
        float body = (1.0f - v->tone) * 1.1f, snap = (0.25f + v->tone) * 0.9f;
        for (i = 0; i < m; i++) {
            float r = 1.0f + pda * pd, y, n;
            pd *= pdk;
            ph += inc * r;
            ph2 += inc2 * r;
            WRAP(ph);
            WRAP(ph2);
            n = noise(&rng) * c3 + c1 * f1 + c2 * f2;
            f2 = f1;
            f1 = n;
            y = (sin_t(ph) * 0.6f + sin_t(ph2) * 0.4f) * env2 * body + n * env * snap;
            t[i] = y * g;
            env *= k;
            env2 *= k2;
        }
        v->pd = pd;
        v->ph2 = ph2;
        v->f1 = f1;
        v->f2 = f2;
        break;
    }
    case DM_CHAT:
    case DM_OHAT: {
        uint32_t p0 = v->mp[0], p1 = v->mp[1], p2 = v->mp[2], p3 = v->mp[3], p4 = v->mp[4], p5 = v->mp[5];
        const uint32_t i0 = v->mi[0], i1 = v->mi[1], i2 = v->mi[2], i3 = v->mi[3], i4 = v->mi[4], i5 = v->mi[5];
        float c = v->c1, f1 = v->f1, f2 = v->f2;
        for (i = 0; i < m; i++) {
            uint32_t cnt;
            float x, y;
            p0 += i0; p1 += i1; p2 += i2; p3 += i3; p4 += i4; p5 += i5;
            cnt = (p0 >> 31) + (p1 >> 31) + (p2 >> 31) + (p3 >> 31) + (p4 >> 31) + (p5 >> 31);
            x = (float)(int32_t)(6 - 2 * (int32_t)cnt) * (1.0f / 6.0f);
            f1 += c * (x - f1);                       /* two one-pole high-passes */
            y = x - f1;
            f2 += c * (y - f2);
            y -= f2;
            t[i] = y * (env + 0.5f * env2) * g;
            env *= k;
            env2 *= k2;
        }
        v->mp[0] = p0; v->mp[1] = p1; v->mp[2] = p2; v->mp[3] = p3; v->mp[4] = p4; v->mp[5] = p5;
        v->f1 = f1;
        v->f2 = f2;
        break;
    }
    case DM_CLAP: {
        float c1 = v->c1, c2 = v->c2, c3 = v->c3, f1 = v->f1, f2 = v->f2;
        int32_t cnt = v->cnt, bursts = v->bursts;
        for (i = 0; i < m; i++) {
            float n = noise(&rng) * c3 + c1 * f1 + c2 * f2;
            f2 = f1;
            f1 = n;
            if (bursts && --cnt == 0) {               /* the next burst */
                cnt = 441;
                bursts--;
                env2 = 1.0f;
            }
            t[i] = n * (env2 + 0.5f * env) * 0.45f * g;
            env *= k;
            env2 *= k2;
        }
        v->cnt = cnt;
        v->bursts = bursts;
        v->f1 = f1;
        v->f2 = f2;
        break;
    }
    case DM_LTOM:
    case DM_HTOM: {
        float pd = v->pd, pdk = v->pdk, pda = v->pda, c = v->c1, f1 = v->f1, nz = 0.1f + 0.4f * v->tone;
        for (i = 0; i < m; i++) {
            float y;
            pd *= pdk;
            ph += inc * (1.0f + pda * pd);
            WRAP(ph);
            f1 += c * (noise(&rng) - f1);
            y = sin_t(ph) * env + f1 * env2 * nz;
            t[i] = y * 0.85f * g;
            env *= k;
            env2 *= k2;
        }
        v->pd = pd;
        v->f1 = f1;
        break;
    }
    case DM_CONGA: {
        float pd = v->pd, pdk = v->pdk, pda = v->pda, ph2 = v->ph2, inc2 = v->inc2, p2g = 0.7f * v->tone;
        for (i = 0; i < m; i++) {
            float r = 1.0f + pda * pd, y;
            pd *= pdk;
            ph += inc * r;
            ph2 += inc2 * r;
            WRAP(ph);
            WRAP(ph2);
            y = sin_t(ph) * env + sin_t(ph2) * env2 * p2g;
            t[i] = y * 0.8f * g;
            env *= k;
            env2 *= k2;
        }
        v->pd = pd;
        v->ph2 = ph2;
        break;
    }
    case DM_SHAKER: {
        float c1 = v->c1, c2 = v->c2, c3 = v->c3, f1 = v->f1, f2 = v->f2;
        for (i = 0; i < m; i++) {
            float n = noise(&rng) * c3 + c1 * f1 + c2 * f2;
            f2 = f1;
            f1 = n;
            t[i] = n * env * (1.0f - env2) * 0.8f * g;
            env *= k;
            env2 *= k2;
        }
        v->f1 = f1;
        v->f2 = f2;
        break;
    }
    case DM_FM: {
        float ph2 = v->ph2, inc2 = v->inc2, idx = v->pda;
        for (i = 0; i < m; i++) {
            float mo;
            ph += inc;
            ph2 += inc2;
            WRAP(ph);
            WRAP(ph2);
            mo = sin_t(ph2) * idx * env2;                 /* the index in turns */
            t[i] = sin_t_any(ph + mo) * env * 0.7f * g;
            env *= k;
            env2 *= k2;
        }
        v->ph2 = ph2;
        break;
    }
    case DM_RESO: {
        int32_t w = v->w, len = v->len;
        float fr = v->fr, lp = v->lp, fb = v->fb, damp = v->damp, pk = 0.0f;
        for (i = 0; i < m; i++) {
            int32_t r0 = w - len, r1 = r0 - 1;
            float a, b, y, x;
            if (r0 < 0) r0 += RESO_LEN;
            if (r1 < 0) r1 += RESO_LEN;
            a = (float)line[r0];
            b = (float)line[r1];
            y = (a + (b - a) * fr) * (1.0f / 16384.0f);
            lp += damp * (y - lp);                    /* the loop's low-pass: the string darkens */
            x = noise(&rng) * env2 * 0.5f + lp * fb;
            x *= 16384.0f;
            line[w] = (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x);
            if (++w >= RESO_LEN)
                w = 0;
            t[i] = y * 1.4f * g;
            pk = fm_maxf(pk, fm_fabsf(y));
            env2 *= k2;
        }
        v->w = w;
        v->lp = fm_flush(lp);
        v->pk = fm_maxf(pk, v->pk * 0.7f);            /* held over a period: a quiet stretch of the
                                                       * string is not its end */
        break;
    }
    default:
        for (i = 0; i < m; i++)
            t[i] = 0.0f;
        env = 0.0f;
        break;
    }
    v->env = env;
    v->env2 = env2;
    v->ph = ph;
    v->rng = rng;
    v->age += (uint32_t)m;
    if (v->model == DM_RESO) {
        if (env2 < 1e-3f && v->pk < 1e-4f && v->age > 4u * (uint32_t)v->len)
            v->on = 0;
    } else if (env < 1e-4f && env2 < 1e-4f) {
        v->on = 0;
    }
}

/* the chunk's targets from par[]: level, pan, the track's sends; ramped over m samples */
static void control(const uint8_t *par, dvoice_t *v, int m)
{
    const uint8_t *p = par + TP_DRUM(v->sound, 0);
    float lv = (float)p[DP_LEVEL], a = (float)p[DP_PAN] * (1.0f / 127.0f), lin, l, r, s1, s2, st, im;
    lin = lv <= 100.0f ? lv * lv * 0.0001f : 1.0f + (lv - 100.0f) * (1.0f / 27.0f);   /* 100 = unity, 127 = 2 */
    l = lin * fm_sin_turns(0.25f * (1.0f - a));       /* constant power: cos, sin of a quarter turn */
    r = lin * fm_sin_turns(0.25f * a);
    s1 = lin * (float)par[TP_SEND1] * (1.0f / 127.0f);
    s2 = lin * (float)par[TP_SEND2] * (1.0f / 127.0f);
    st = lin * (float)par[TP_SENDT] * (1.0f / 127.0f);
    if (v->age == 0) {                                /* a fresh voice starts at its gains */
        v->gl = l; v->gr = r; v->gs1 = s1; v->gs2 = s2; v->gst = st;
    }
    im = 1.0f / (float)m;
    v->dgl = (l - v->gl) * im;
    v->dgr = (r - v->gr) * im;
    v->gs1 = s1;                                      /* the sends step: a send's zipper is buried in the effect */
    v->gs2 = s2;
    v->gst = st;
}

void drumline_render(drumline_t *d, float *l, float *r, float *s1, float *s2, float *st, uint32_t n)
{
    uint32_t c;
    for (c = 0; c < n; c += CR) {
        int m = (int)(n - c < CR ? n - c : CR), vi;
        for (vi = 0; vi < NDVOICE; vi++) {
            dvoice_t *v = &d->v[vi];
            int o;
            if (!v->on)
                continue;
            control(d->par, v, m);
            for (o = 0; o < m && v->on; o += SUB) {
                float t[SUB];
                int mm = m - o < SUB ? m - o : SUB, i;
                float gl = v->gl, gr = v->gr;
                const float gs1 = v->gs1, gs2 = v->gs2, gst = v->gst, dgl = v->dgl, dgr = v->dgr;
                float *pl = l + c + o, *pr = r + c + o, *p1 = s1 + c + o, *p2 = s2 + c + o, *pt = st + c + o;
                render_voice(v, d->line[v->line], t, mm);
                for (i = 0; i < mm; i++) {
                    float x = t[i];
                    gl += dgl;
                    gr += dgr;
                    pl[i] += x * gl;
                    pr[i] += x * gr;
                    p1[i] += x * gs1;
                    p2[i] += x * gs2;
                    pt[i] += x * gst;
                }
                v->gl = gl;
                v->gr = gr;
            }
            if (!v->on && v->model == DM_RESO && d->line_owner[v->line] == vi)
                d->line_owner[v->line] = -1;
        }
    }
}

int drumline_sounding(const drumline_t *d)
{
    int i;
    for (i = 0; i < NDVOICE; i++)
        if (d->v[i].on)
            return 1;
    return 0;
}
