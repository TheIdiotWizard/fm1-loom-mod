/* SPDX-License-Identifier: GPL-3.0-only */
/* XY-1 sequencer; see seq.h for the model.
 *
 * Time. Every timer is a count of samples left in Q8 (a 24.8 fixed point: the fraction keeps the
 * tempo exact across steps, X0X's trick), counted down by seq_advance and due when under 256. NEVER
 * means no event. The sixteenth timer is the master: it drives the metronome, the count-in, the bar
 * (scene cues, the song) and the punch ramp; the tracks run their own step timers from the same
 * tempo, so they stay in phase with it. A track's step fires on its grid plus an offset (swing or
 * groove, and the step's nudge); the offset is folded into the time to the next step, as X0X folds
 * swing, so no event needs a look-ahead.
 *
 * Keys. The main loop's keys (seq_key and friends) go into a small ring and are played inside the
 * interrupt at the next seq_advance, so the players, the brain and the recording all run in one
 * context and the sink is only ever called from the interrupt.
 *
 * Locks. A lane's value at a step is sent before the step's notes; it holds until the lane's next
 * locked step. With shape > 0, the value moves toward the next locked step's value across the gap:
 * flat for (100 - shape) % of the gap, then a straight line. A pattern start does not restore the
 * knob: if step 1 has no lock, the last value keeps holding. The knob gets its lane back (LOCK_NONE)
 * when the track leaves the pattern, and when the transport stops.
 *
 * Punch-in FX that change the sequence run here (mute, stutter, the fills, octaves, echo, short and
 * long hits, random sound, loop 4, ramp, random notes, half, double, reverse); the engine gets the
 * union of held and sequenced FX through the sink and does the rest (pan, sends, filter). */
#include "seq.h"

#define NEVER 0xFFFFFFFFu
#define TAG_KEY 0x100u                /* live keys: TAG_KEY | note */
#define TAG_ARP 0x200u
#define TAG_PLAYER 0x300u             /* maestro / hold: TAG_PLAYER | note */
#define TAG_STEP (1u << 16)            /* steps: TAG_STEP | step * 16 + note index + ratchet * 1024 */
#define FILL_VEL 100
#define RAMP_MAX 8                    /* lock lanes ramping at once, per track */

/* a track scale in half sixteenths: TS_HALF .. TS_16 */
static const uint8_t TS_HALVES[NTS] = {1, 2, 4, 6, 8, 12, 16, 32};
static const uint8_t SC_VEL[9] = {4, 8, 16, 32, 64, 100, 112, 127, 0};
static const uint8_t GM_DRUM[NDRUM] = {36, 38, 42, 46, 39, 45, 50, 63, 70, 67, 75};
/* grooves: a timing offset in ticks (of PPS a sixteenth) and a velocity offset for each sixteenth of the bar */
static const int8_t GROOVE_T[NGROOVE][16] = {
    {0},                                                          /* swing: the pattern's swing value */
    {0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4},             /* half shuffle */
    {0, 2, 0, 3, -2, 2, 0, 3, 0, 2, 0, 3, -2, 2, 0, 3},           /* bombora: beats 2 and 4 pushed */
    {0, 3, -3, 3, -3, 3, -3, 3, -3, 3, -3, 3, -3, 3, -3, 3},      /* wobbly */
    {0},                                                          /* gaussian: rolled */
    {0},                                                          /* accents: velocity only */
    {0, 2, 0, 2, 5, 2, 0, 2, 0, 2, 0, 2, 5, 2, 0, 2},             /* island nod: 2 and 4 laid back */
};
static const int8_t GROOVE_V[NGROOVE][16] = {
    {0},
    {0},
    {0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0},
    {0, -4, 0, -4, 0, -4, 0, -4, 0, -4, 0, -4, 0, -4, 0, -4},
    {0},
    {15, -8, 0, -8, 0, -8, 0, -8, 15, -8, 0, -8, 0, -8, 0, -8},
    {0, 0, 0, 0, -6, 0, 0, 0, 0, 0, 0, 0, -6, 0, 0, 0},
};
static const int8_t SIN32[32] = {0, 25, 49, 71, 90, 106, 117, 125, 127, 125, 117, 106, 90, 71, 49, 25,
                                 0, -25, -49, -71, -90, -106, -117, -125, -127, -125, -117, -106, -90, -71, -49, -25};

const char *const PUNCH_NAME[NPUNCH_FX] = {
    "mute", "stutter", "fill", "pan", "half fill", "oct up", "echo", "k/s fill", "short", "hats", "tails", "random",
    "", "", "", "",
    "mute", "stutter", "loop 4", "pan", "oct up", "oct down", "delay", "reverb", "filt down", "filt up",
    "ramp", "random", "half", "double", "reverse", ""};
const char *const BS_NAME[NBS] = {"major", "minor", "dorian", "mixolyd", "lydian", "phrygian", "harm min",
                                  "penta", "penta min", "chrom"};
const uint16_t BRAIN_SCALE[NBS] = {
    0x0AB5, /* major      C D E F G A B  */
    0x05AD, /* minor      C D Eb F G Ab Bb */
    0x06AD, /* dorian     C D Eb F G A Bb */
    0x06B5, /* mixolydian C D E F G A Bb */
    0x0AD5, /* lydian     C D E F# G A B */
    0x05AB, /* phrygian   C Db Eb F G Ab Bb */
    0x09AD, /* harm minor C D Eb F G Ab B */
    0x0295, /* penta      C D E G A */
    0x04A9, /* penta min  C Eb F G Bb */
    0x0FFF, /* chromatic */
};

/* ------------------------------------------------------------- scales --- */
static int mask_len(uint16_t m)
{
    int n = 0;
    while (m) {
        n += m & 1u;
        m >>= 1;
    }
    return n;
}

static int floor_div(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/* the note's degree (octave * scale length + index) from the key's tonic, snapping a note outside the
 * scale to the nearest scale note (ties go down) */
static int note_to_deg(int note, int key, uint16_t mask)
{
    int pc = ((note - key) % 12 + 12) % 12, d, idx = 0, i, oct;
    if (!(mask >> pc & 1u)) {
        for (d = 1; d < 12; d++) {
            if (mask >> ((pc - d + 12) % 12) & 1u) {
                note -= d;
                break;
            }
            if (mask >> ((pc + d) % 12) & 1u) {
                note += d;
                break;
            }
        }
        pc = ((note - key) % 12 + 12) % 12;
    }
    for (i = 0; i < pc; i++)
        idx += mask >> i & 1u;
    oct = floor_div(note - key, 12);
    return oct * mask_len(mask) + idx;
}

static int deg_to_note(int deg, int key, uint16_t mask)
{
    int len = mask_len(mask), oct = floor_div(deg, len), idx = deg - oct * len, pc = 0, i;
    for (i = 0; i < 12; i++)
        if (mask >> i & 1u) {
            if (idx == 0) {
                pc = i;
                break;
            }
            idx--;
        }
    return key + oct * 12 + pc;
}

static int clamp_note(int n) { return n < 0 ? 0 : n > 127 ? 127 : n; }

int scale_step(int note, int key, int scale, int steps)
{
    uint16_t mask = BRAIN_SCALE[scale >= 0 && scale < NBS ? scale : BS_CHROMATIC];
    key = ((key % 12) + 12) % 12;
    return clamp_note(deg_to_note(note_to_deg(note, key, mask) + steps, key, mask));
}

int brain_shift(int note, int key, int scale, int degree, int mode)
{
    uint16_t mask;
    int d0;
    if (mode == 0 || degree == 0)
        return note;
    if (mode == 1)
        return scale_step(note, key, scale, degree);
    mask = BRAIN_SCALE[scale >= 0 && scale < NBS ? scale : BS_CHROMATIC];
    key = ((key % 12) + 12) % 12;
    d0 = note_to_deg(60 + key, key, mask);    /* transpose only: the degree's distance from the tonic, in semitones */
    return clamp_note(note + deg_to_note(d0 + degree, key, mask) - deg_to_note(d0, key, mask));
}

/* the best key and scale for a pitch-class histogram: every (key, scale) scores its notes (the tonic
 * and the fifth count more) against the notes outside it; confident when the runner-up is clearly behind */
int brain_detect(const uint8_t *hist12, int *key, int *scale)
{
    int best = -0x7FFFFFFF, second = -0x7FFFFFFF, bk = 0, bs = 0, k, sc, pc, total = 0;
    for (pc = 0; pc < 12; pc++)
        total += hist12[pc];
    if (!total)
        return 0;
    for (sc = 0; sc < BS_CHROMATIC; sc++) {
        uint16_t m = BRAIN_SCALE[sc];
        for (k = 0; k < 12; k++) {
            int score = 0;
            for (pc = 0; pc < 12; pc++) {
                int rel = (pc - k + 12) % 12, w;
                if (rel == 0)             /* the tonic and the fifth tell the modes of one note set apart */
                    w = 6;
                else if (rel == 7 && (m >> 7 & 1u))
                    w = 4;
                else if (m >> rel & 1u)
                    w = 2;
                else
                    w = -3;
                score += w * (int)hist12[pc];
            }
            if (score > best) {
                second = best;
                best = score;
                bk = k;
                bs = sc;
            } else if (score > second)
                second = score;
        }
    }
    *key = bk;
    *scale = bs;
    return best > 0 && best - second >= (total + 3) / 4;   /* the runner-up always shares most notes: a
                                                           * quarter of what was heard must separate them */
}

/* ------------------------------------------------------------ helpers --- */
static uint32_t rng_next(uint32_t *r)
{
    uint32_t x = *r ? *r : 1;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *r = x;
}

static void midi(const seq_sink_t *o, uint8_t st, uint8_t d1, uint8_t d2)
{
    if (o->midi)
        o->midi(o->ctx, st, d1, d2);
}

static tpat_t *tpat(const seq_t *s, int t) { return &s->P->pat[t][s->ppat[t] < NPATT ? s->ppat[t] : 0]; }
static int pat_len(const tpat_t *p) { return p->len < 1 ? 1 : p->len > NSTEP ? NSTEP : p->len; }
static int is_drum(const seq_t *s, int t) { return t < NTRK && s->par[t][TP_ENGINE] == EG_DRUMLINE; }
static int is_synth(const seq_t *s, int t) { return t < NTRK && s->par[t][TP_ENGINE] != EG_DRUMLINE; }

float seq_tempo(const seq_t *s) { return s->ext ? s->ext_bpm : s->bpm; }

/* samples per sixteenth, Q8 */
static uint32_t six_q8(const seq_t *s)
{
    float bpm = seq_tempo(s), q;
    if (bpm < 20.0f)
        bpm = 20.0f;
    if (bpm > 300.0f)
        bpm = 300.0f;
    q = (float)SEQ_FS * 60.0f / bpm / 4.0f;
    return (uint32_t)(q * 256.0f);
}

uint32_t seq_step_q8(const seq_t *s, int trk)
{
    int sc = trk >= 0 && trk < NSEQT ? tpat(s, trk)->scale : TS_1;
    if (sc >= NTS)
        sc = TS_1;
    return six_q8(s) / 2u * TS_HALVES[sc];
}

int seq_bar_step(const seq_t *s) { return s->bar16 & 15; }

/* a punch-in effect in force on track t: sequenced ones reach the whole group, held ones the group or
 * the target track */
static int fx_on(const seq_t *s, int t, int bit)
{
    uint32_t b = 1u << bit;
    if (s->punch_seq & b)
        return 1;
    if (!(s->punch_keys & b))
        return 0;
    return s->punch_target == 0xFF || s->punch_target == t;
}

static int punch_muted(const seq_t *s, int t)
{
    if (t >= NTRK)
        return 0;
    return is_drum(s, t) ? fx_on(s, t, PD_MUTE) : fx_on(s, t, PM_MUTE);
}

/* the track's step length now (half / double time) */
static uint32_t step_len(const seq_t *s, int t)
{
    uint32_t L = seq_step_q8(s, t);
    if (is_synth(s, t)) {
        if (fx_on(s, t, PM_HALF))
            L *= 2u;
        if (fx_on(s, t, PM_DOUBLE))
            L /= 2u;
    }
    return L;
}

/* the sixteenth of the bar a step of track t falls on (its scale), or -1 between sixteenths */
static int step_six(const tpat_t *p, int step)
{
    int h = TS_HALVES[p->scale < NTS ? p->scale : TS_1] * step;
    return (h & 1) ? -1 : (h >> 1) & 15;
}

/* the step's timing offset, Q8: swing or the groove's table on its sixteenth, plus its nudge */
static int32_t step_off(seq_t *s, int t, int p, uint32_t L)
{
    const tpat_t *tp = tpat(s, t);
    const step_t *st = &tp->st[p];
    int32_t off = (int32_t)(L / PPS) * st->nudge, six = step_six(tp, p);
    uint32_t L16 = six_q8(s);
    if (six < 0)
        return off;
    switch (tp->groove) {
    case GR_SWING:
        if ((six & 1) && tp->swing > 50)
            off += (int32_t)(L16 / 100u * ((tp->swing > 100 ? 100u : tp->swing) - 50u));   /* 100: half a sixteenth late */
        break;
    case GR_GAUSS:
        off += (int32_t)(L16 / PPS) * ((int32_t)(rng_next(&s->t[t].rng) % 5u) - 2);
        break;
    default:
        off += (int32_t)(L16 / PPS) * GROOVE_T[tp->groove < NGROOVE ? tp->groove : 0][six];
        break;
    }
    return off;
}

static int groove_vel(seq_t *s, int t, int p, int vel)
{
    const tpat_t *tp = tpat(s, t);
    int six = step_six(tp, p);
    if (six >= 0) {
        if (tp->groove == GR_GAUSS)
            vel += (int)(rng_next(&s->t[t].rng) % 13u) - 6;
        else if (tp->groove < NGROOVE)
            vel += GROOVE_V[tp->groove][six];
    }
    return vel < 1 ? 1 : vel > 127 ? 127 : vel;
}

/* --------------------------------------------------------- note offs --- */
static void fire_off(seq_t *s, int t, seq_off_t *e, const seq_sink_t *o)
{
    e->used = 0;
    switch (e->kind) {
    case OFF_PUNCH:
        s->punch_seq &= ~(1u << e->tag);
        break;
    case OFF_TAPE:
        if (o->tape_seg)
            o->tape_seg(o->ctx, (int)e->tag, 0);
        break;
    default:
        if (o->note_off)
            o->note_off(o->ctx, t, e->tag);
        if (e->note && s->send_notes && s->par[t][TP_CHAN])
            midi(o, (uint8_t)(0x80 | ((s->par[t][TP_CHAN] - 1) & 15)), e->note, 0);
        break;
    }
}

static void add_off(seq_t *s, int t, uint32_t tag, int kind, int note, uint32_t at_q8, const seq_sink_t *o)
{
    seq_trk_t *tr = &s->t[t];
    seq_off_t *e = 0, *soon = 0;
    int i;
    for (i = 0; i < SEQ_PENDING; i++) {
        seq_off_t *c = &tr->off[i];
        if (c->used && c->tag == tag && c->kind == kind) {   /* the same note again: its off moves */
            e = c;
            break;
        }
        if (!c->used && !e)
            e = c;
        if (c->used && (!soon || c->off_at < soon->off_at))
            soon = c;
    }
    if (!e) {                         /* full: the soonest goes now */
        fire_off(s, t, soon, o);
        e = soon;
    }
    e->off_at = at_q8 < 256u ? 256u : at_q8;
    e->tag = tag;
    e->kind = (uint8_t)kind;
    e->note = (uint8_t)note;
    e->used = 1;
}

static void cancel_offs(seq_t *s, int t, const seq_sink_t *o)   /* everything pending goes off now */
{
    int i;
    for (i = 0; i < SEQ_PENDING; i++)
        if (s->t[t].off[i].used)
            fire_off(s, t, &s->t[t].off[i], o);
}

/* ----------------------------------------------------------- the brain --- */
static int brain_deg(const seq_t *s) { return (int8_t)s->brain_deg; }

static void brain_hear(seq_t *s, int note)
{
    uint8_t *h = &s->brain_hist[((note % 12) + 12) % 12];
    if (*h < 255)
        (*h)++;
}

/* a routed track's note through the brain (steps always; live keys when link is on) */
static int brain_note(const seq_t *s, int t, int note, int live)
{
    int mode = t < NTRK ? s->par[t][TP_BRAIN] : 0;
    if (!mode || (live && !s->brain_link))
        return note;
    return brain_shift(note, s->brain_key, s->brain_scale, brain_deg(s), mode);
}

/* the BRAIN track's knobs: manual mode takes key / scale from them when they change (a scene may have
 * set the sequencer's own); auto mode detects at the bar */
static void brain_follow_par(seq_t *s)
{
    const uint8_t *e = &s->par[AUX_BRAIN][TP_E1];
    uint16_t route = 0;
    int t;
    s->brain_mode = e[0];
    s->brain_link = e[3];
    if (e[1] != s->brain_seen[1])
        s->brain_key = (uint8_t)(e[1] % 12);
    if (e[2] != s->brain_seen[2])
        s->brain_scale = e[2] < NBS ? e[2] : BS_MAJOR;
    for (t = 0; t < 4; t++)
        s->brain_seen[t] = e[t];
    for (t = 0; t < NTRK; t++)
        if (s->par[t][TP_BRAIN] && !is_drum(s, t))
            route |= (uint16_t)(1u << t);
    s->brain_route = route;
}

static void brain_bar(seq_t *s)
{
    int key, scale, pc;
    if (s->brain_mode) {
        s->brain_auto_ok = (uint8_t)brain_detect(s->brain_hist, &key, &scale);
        if (s->brain_auto_ok) {
            s->brain_key = (uint8_t)key;
            s->brain_scale = (uint8_t)scale;
        }
    }
    for (pc = 0; pc < 12; pc++)       /* the histogram forgets: half each bar */
        s->brain_hist[pc] >>= 1;
}

/* ------------------------------------------------------------- emitting --- */
static void midi_on(seq_t *s, int t, int note, int vel, const seq_sink_t *o)
{
    if (s->send_notes && s->par[t][TP_CHAN])
        midi(o, (uint8_t)(0x90 | ((s->par[t][TP_CHAN] - 1) & 15)), (uint8_t)note, (uint8_t)vel);
}

static void midi_off(seq_t *s, int t, int note, const seq_sink_t *o)
{
    if (s->send_notes && s->par[t][TP_CHAN])
        midi(o, (uint8_t)(0x80 | ((s->par[t][TP_CHAN] - 1) & 15)), (uint8_t)note, 0);
}

/* a note of a synth track with a scheduled off */
static void emit_note(seq_t *s, int t, int note, int vel, int glide_ms, uint32_t tag, uint32_t len_q8,
                      const seq_sink_t *o)
{
    if (o->note_on)
        o->note_on(o->ctx, t, note, vel, glide_ms, tag);
    midi_on(s, t, note, vel, o);
    add_off(s, t, tag, OFF_NOTE, note, len_q8, o);
}

/* a drum hit, through the drum punch-in FX */
static void emit_drum(seq_t *s, int t, int sound, int vel, const seq_sink_t *o)
{
    seq_trk_t *tr = &s->t[t];
    int pitch = fx_on(s, t, PD_OCT_UP) ? 12 : 0;
    if (fx_on(s, t, PD_RANDOM))
        sound = (int)(rng_next(&tr->rng) % NDRUM);
    if (sound < 0 || sound >= NDRUM)
        return;
    if (o->drum)
        o->drum(o->ctx, t, sound, vel, pitch, fx_on(s, t, PD_SHORT), fx_on(s, t, PD_TAILS));
    if (s->send_notes && s->par[t][TP_CHAN]) {
        midi_on(s, t, GM_DRUM[sound], vel, o);
        midi_off(s, t, GM_DRUM[sound], o);
    }
    tr->held_drum = (uint8_t)(sound + 1);
    if (fx_on(s, t, PD_ECHO) && tr->echo_n < SN && tr->echo_to_next == NEVER) {
        tr->echo_sound[tr->echo_n++] = (uint8_t)sound;
        tr->echo_vel = (uint8_t)(vel / 2 < 1 ? 1 : vel / 2);
    }
}

/* ------------------------------------------------------------ the step --- */
/* the step's notes (synth) or hits (drums); ratchet = the hit's index within the step */
static void fire_notes(seq_t *s, int t, const step_t *st, int p, uint32_t L, int ratchet, const seq_sink_t *o)
{
    seq_trk_t *tr = &s->t[t];
    const tpat_t *tp = tpat(s, t);
    int vel = groove_vel(s, t, p, st->vel ? st->vel : 100), i, glide = 0, n = 0;
    int cv = st->cval, comp = st->comp;
    uint32_t len_q8;
    if (comp == SC_VELOCITY) {
        int k = cv == SC_RANDOM_VAL ? (int)(rng_next(&tr->rng) % 9u) : cv;
        vel = SC_VEL[k < 9 ? k : 8];
        if (!vel)
            return;                   /* velocity 0: a silent step */
    }
    if (is_drum(s, t)) {
        for (i = 0; i < SN; i++)
            if (st->note[i] >= 1 && st->note[i] <= NDRUM)
                emit_drum(s, t, st->note[i] - 1, vel, o);
        return;
    }
    len_q8 = L / 4u * (st->len ? st->len : tp->nlen);
    if (comp == SC_HOLD)
        len_q8 = len_q8 > L * (tr->stall + 1u) ? len_q8 : L * (tr->stall + 1u);
    if (comp == SC_MULTIPLY && tr->ratchets > 1) {
        uint32_t sp = L / tr->ratchets;
        if (len_q8 > sp)
            len_q8 = sp;
    }
    if (comp == SC_PORTAMENTO) {
        int pct = cv == SC_RANDOM_VAL ? (int)(rng_next(&tr->rng) % 9u) : cv;
        pct = (pct < 9 ? pct : 8) * 10 + 10;
        glide = (int)((L >> 8) * (uint32_t)pct / 4410u);   /* ms: samples * pct / 100 / 44.1 */
    }
    for (i = 0; i < SN; i++) {
        int note = st->note[i];
        uint32_t tag;
        if (!note)
            continue;
        if (tr->ramp_deg)
            note = scale_step(note, s->brain_key, s->brain_scale, tr->ramp_deg);
        if (fx_on(s, t, PM_RANDOM))
            note = scale_step(note, s->brain_key, s->brain_scale, (int)(rng_next(&tr->rng) % 15u) - 7);
        note = brain_note(s, t, note, 0);
        if (s->par[t][TP_BRAIN])
            brain_hear(s, note);
        note += tr->ramp_semi;
        if (fx_on(s, t, PM_OCT_UP))
            note += 12;
        if (fx_on(s, t, PM_OCT_DOWN))
            note -= 12;
        note = clamp_note(note);
        tag = TAG_STEP | (uint32_t)(p * 16 + i + ratchet * 1024);
        emit_note(s, t, note, vel, glide, tag, len_q8, o);
        tr->last_note[n++] = (uint8_t)note;
    }
    tr->nlast = (uint8_t)n;
    tr->last_step = (uint8_t)p;
    tr->last_vel = (uint8_t)vel;
    tr->last_glide = (uint8_t)(glide > 255 ? 255 : glide);
}

/* the drum fills of the punch-in FX on this step (they ignore the step's condition) */
static void fire_fills(seq_t *s, int t, const step_t *st, int p, const seq_sink_t *o)
{
    seq_trk_t *tr = &s->t[t];
    int empty = step_empty(st), b = p & 15;
    if (fx_on(s, t, PD_KS_FILL)) {
        if (b == 0 || b == 4 || b == 8 || b == 12)
            emit_drum(s, t, 0, 110, o);
        if (b == 4 || b == 12)
            emit_drum(s, t, 1, 110, o);
    }
    if (fx_on(s, t, PD_HATS))
        emit_drum(s, t, 2, 90, o);
    if (empty && tr->held_drum) {
        if (fx_on(s, t, PD_FILL) || (fx_on(s, t, PD_HALF_FILL) && (p & 1)))
            emit_drum(s, t, tr->held_drum - 1, FILL_VEL, o);
    }
}

/* the aux tracks' steps: brain degrees, punch-in bits, tape segments */
static void fire_aux(seq_t *s, int t, const step_t *st, int p, uint32_t L, const seq_sink_t *o)
{
    const tpat_t *tp = tpat(s, t);
    uint32_t len_q8 = L / 4u * (st->len ? st->len : tp->nlen);
    int i;
    (void)p;
    if (t == AUX_BRAIN) {
        if (st->note[0])
            s->brain_deg = (uint8_t)(int8_t)((int)st->note[0] - 60);
        return;
    }
    for (i = 0; i < SN; i++) {
        int v = (int)st->note[i] - 1;
        if (v < 0)
            continue;
        if (t == AUX_PUNCH && v < NPUNCH_FX && PUNCH_NAME[v][0]) {
            s->punch_seq |= 1u << v;
            add_off(s, t, (uint32_t)v, OFF_PUNCH, 0, len_q8, o);
        } else if (t == AUX_TAPE && v < 16) {
            if (o->tape_seg)
                o->tape_seg(o->ctx, v, 1);
            add_off(s, t, (uint32_t)v, OFF_TAPE, 0, len_q8, o);
        }
    }
}

/* the condition and the skip component: does this step play on this pass? */
static int step_plays(seq_t *s, int t, const step_t *st)
{
    seq_trk_t *tr = &s->t[t];
    int c = st->cond;
    if (c > 0 && c < COND_AB) {
        if (rng_next(&tr->rng) % 100u >= (uint32_t)c)
            return 0;
    } else if (c >= COND_AB && c < COND_AB + COND_NAB) {
        int a, b;
        cond_ab(c - COND_AB, &a, &b);
        if ((int)(tr->pass % (uint32_t)b) != a - 1)
            return 0;
    }
    if (st->comp == SC_SKIP) {
        int n = st->cval == SC_RANDOM_VAL ? 1 + (int)(rng_next(&tr->rng) % 9u) : st->cval + 1;
        if (tr->pass % (uint32_t)n)
            return 0;
    }
    return 1;
}

/* ramps and random: the degree offset for this pass */
static int ramp_degree(seq_t *s, int t, const step_t *st)
{
    seq_trk_t *tr = &s->t[t];
    int cv = st->cval == SC_RANDOM_VAL ? (int)(rng_next(&tr->rng) % 9u) : st->cval, inc, range, d;
    if (cv > 8)
        cv = 8;
    inc = cv % 5 + 2;
    range = mask_len(BRAIN_SCALE[s->brain_scale < NBS ? s->brain_scale : BS_MAJOR]) * (cv < 5 ? 1 : 3);
    if (st->comp == SC_RANDOM) {
        int k = range / inc + 1;
        return (int)(rng_next(&tr->rng) % (uint32_t)k) * inc;
    }
    d = (int)(tr->pass * (uint32_t)inc % (uint32_t)range);
    return st->comp == SC_RAMP_DOWN ? -d : d;
}

/* ------------------------------------------------------------ the locks --- */
static void lock_put(seq_t *s, int t, const lane_t *l, lane_rt_t *r, int v, const seq_sink_t *o)
{
    (void)s;
    if (r->applied == v)
        return;
    r->applied = (uint8_t)v;
    if (o->lock)
        o->lock(o->ctx, t, l->param, v);
}

/* a lane that is gone (cleared, or its slot reused) still had a value on the engine: send it back to the knob */
static void lane_forget(seq_t *s, lane_rt_t *r, const seq_sink_t *o)
{
    (void)s;
    r->ramp = 0;
    if (r->applied != LOCK_NONE) {
        r->applied = LOCK_NONE;
        if (o->lock)
            o->lock(o->ctx, r->trk, r->param, LOCK_NONE);
    }
}

static void apply_locks(seq_t *s, int t, int p, uint32_t L, const seq_sink_t *o)
{
    const tpat_t *tp = tpat(s, t);
    int k, len = pat_len(tp), nramp = 0;
    for (k = 0; k < NLANE; k++) {
        const lane_t *l = &s->P->lane[k];
        lane_rt_t *r = &s->lrt[k];
        int v, q, d;
        if (!l->used || l->trk != t)
            continue;
        if (r->gen != l->gen) {
            lane_forget(s, r, o);     /* the slot was reused: the old lane's value goes back to the knob */
            r->gen = l->gen;
            r->trk = l->trk;
            r->param = l->param;
        }
        if (l->pat != s->ppat[t]) {   /* another pattern's lane: the knob has it */
            lock_put(s, t, l, r, LOCK_NONE, o);
            r->ramp = 0;
            continue;
        }
        v = l->val[p];
        if (v == LOCK_NONE)
            continue;                 /* holds (or keeps ramping toward the next lock) */
        r->ramp = 0;
        lock_put(s, t, l, r, v, o);
        if (!tp->shape || nramp >= RAMP_MAX)
            continue;
        for (d = 1; d < len; d++) {   /* the next locked step */
            q = (p + d) % len;
            if (l->val[q] != LOCK_NONE)
                break;
        }
        if (d >= len || l->val[q] == v)
            continue;
        r->from = (uint8_t)v;
        r->to = l->val[q];
        r->len = (L >> 8) * (uint32_t)d;
        r->hold = r->len / 100u * (100u - (tp->shape > 100 ? 100u : tp->shape));
        r->age = 0;
        r->ramp = 1;
        nramp++;
    }
}

static void locks_tick(seq_t *s, uint32_t k, const seq_sink_t *o)
{
    int i;
    for (i = 0; i < NLANE; i++) {
        lane_rt_t *r = &s->lrt[i];
        const lane_t *l = &s->P->lane[i];
        if (!l->used || r->gen != l->gen) {
            lane_forget(s, r, o);
            continue;
        }
        if (!r->ramp)
            continue;
        r->age += k;
        if (r->age >= r->len) {
            lock_put(s, l->trk, l, r, r->to, o);
            r->ramp = 0;
        } else if (r->age > r->hold && r->len > r->hold) {
            int v = (int)r->from + ((int)r->to - (int)r->from) * (int)(r->age - r->hold) / (int)(r->len - r->hold);
            lock_put(s, l->trk, l, r, v, o);
        }
    }
}

static void locks_release(seq_t *s, const seq_sink_t *o)
{
    int i;
    for (i = 0; i < NLANE; i++) {
        lane_rt_t *r = &s->lrt[i];
        const lane_t *l = &s->P->lane[i];
        if (l->used && r->gen == l->gen) {
            r->ramp = 0;
            lock_put(s, l->trk, l, r, LOCK_NONE, o);
        } else
            lane_forget(s, r, o);
    }
}

/* ------------------------------------------------------------- the bend --- */
static void bend_start(seq_t *s, int t, const step_t *st, uint32_t L)
{
    seq_trk_t *tr = &s->t[t];
    int sh = st->cval;
    tr->bend_amp = 200;
    if (sh == 8)
        sh = (int)(rng_next(&tr->rng) % 8u);
    else if (sh >= 9) {
        sh = 0;
        tr->bend_amp = (int16_t)((int)(rng_next(&tr->rng) % 401u) - 200);
    }
    tr->bend_shape = (uint8_t)sh;
    tr->bend_t = 0;
    tr->bend_len = L >> 8;
    tr->bend_on = 1;
}

static int bend_cents(const seq_trk_t *tr)
{
    uint32_t x = tr->bend_len ? tr->bend_t * 256u / tr->bend_len : 255u;
    int tri, amp = tr->bend_amp, c = 0;
    if (x > 255u)
        x = 255u;
    tri = x < 128u ? (int)x * 2 : (int)(255u - x) * 2;
    switch (tr->bend_shape) {
    case 0: c = -amp * tri / 256; break;                      /* down-up */
    case 1: c = amp * tri / 256; break;                       /* up-down */
    case 2: c = -amp * (tri * tri / 256) / 256; break;        /* bump down */
    case 3: c = amp * (tri * tri / 256) / 256; break;         /* bump up */
    case 4: c = amp * SIN32[(x * 3u >> 3) & 31u] * (int)x / (127 * 256); break;            /* spring out */
    case 5: c = amp * SIN32[(x * 3u >> 3) & 31u] * (int)(255u - x) / (127 * 256); break;   /* spring in */
    case 6: c = -amp * (int)x / 256; break;                   /* fade down */
    default: c = amp * (int)x / 256; break;                   /* fade up */
    }
    return c;
}

static void bend_tick(seq_t *s, int t, uint32_t k, const seq_sink_t *o)
{
    seq_trk_t *tr = &s->t[t];
    int c;
    if (!tr->bend_on)
        return;
    tr->bend_t += k;
    if (tr->bend_t >= tr->bend_len) {
        tr->bend_on = 0;
        c = 0;
    } else
        c = bend_cents(tr);
    if (c != tr->bend_last || !tr->bend_last_set) {
        tr->bend_last = (int16_t)c;
        tr->bend_last_set = 1;
        if (o->bend)
            o->bend(o->ctx, t, c);
    }
}

/* ------------------------------------------------------------ the bar --- */
static void scene_read(seq_t *s, int k, uint8_t *next)
{
    const scene_t *sc = &s->P->scene[k];
    int t;
    for (t = 0; t < NSEQT; t++)
        next[t] = sc->pat[t] < NPATT ? sc->pat[t] : 0;
    s->mute = sc->mute;
    s->brain_key = (uint8_t)(sc->brain_key % 12);
    s->brain_scale = sc->brain_scale < NBS ? sc->brain_scale : BS_MAJOR;
    s->brain_deg = sc->brain_deg;
    s->scene = (uint8_t)k;
}

static int song_plays(const seq_t *s) { return s->song_on && s->P->song.len > 0; }

/* the start, or the bar's end: the song, a cued scene or cued patterns decide each track's next
 * pattern. Returns the tracks whose pattern changed (bit t). */
static uint32_t bar_end(seq_t *s, int start, const seq_sink_t *o)
{
    uint8_t next[NSEQT];
    uint32_t changed = 0;
    int t, scene_now = -1;
    for (t = 0; t < NSEQT; t++)
        next[t] = s->ppat[t];
    if (song_plays(s)) {
        const song_t *sg = &s->P->song;
        int k = start ? 0 : s->song_pos;
        if (start) {
            s->song_rep = sg->slot[0].rep ? sg->slot[0].rep : 1;
        } else if (s->song_rep > 1) {
            s->song_rep--;
        } else {
            k = s->song_pos + 1;
            if (k >= sg->len) {
                if (!sg->loop) {
                    s->req_stop = 1;
                    k = s->song_pos;
                } else
                    k = 0;
            }
            s->song_rep = sg->slot[k].rep ? sg->slot[k].rep : 1;
        }
        s->song_pos = (uint8_t)k;
        scene_now = sg->slot[k].scene < NSCENE ? sg->slot[k].scene : 0;
        s->cue = 0xFF;
    } else if (s->cue < NSCENE) {
        scene_now = s->cue;
        s->cue = 0xFF;
    }
    if (scene_now >= 0 && (start || scene_now != s->scene || song_plays(s))) {
        scene_read(s, scene_now, next);
        if (o->scene)
            o->scene(o->ctx, scene_now);
    }
    for (t = 0; t < NSEQT; t++)
        if (s->pcue[t] < NPATT) {
            next[t] = s->pcue[t];
            s->pcue[t] = 0xFF;
        }
    for (t = 0; t < NSEQT; t++)
        if (next[t] != s->ppat[t]) {
            s->ppat[t] = next[t];
            changed |= 1u << t;
        }
    return changed;
}

static void bar_len_update(seq_t *s)
{
    const tpat_t *p = tpat(s, 0);
    uint32_t span = (uint32_t)pat_len(p) * TS_HALVES[p->scale < NTS ? p->scale : TS_1] / 2u;
    s->bar_len = span < 16u ? 16u : span;
}

static int ext_tps(const seq_t *s, int t)   /* external clock ticks a step of track t */
{
    int sc = tpat(s, t)->scale;
    return 3 * TS_HALVES[sc < NTS ? sc : TS_1];
}

static void ext_arm(seq_t *s, int t) { s->t[t].ext_ticks = (uint8_t)(ext_tps(s, t) - 1); }   /* the next tick fires */

static void track_reset(seq_t *s, int t)
{
    seq_trk_t *tr = &s->t[t];
    tr->started = 0;
    tr->pos = 0;
    tr->pass = 0;
    tr->stall = 0;
    tr->ratchet = tr->ratchets = 0;
    tr->jump_to = 0xFF;
    tr->loop4_on = 0;
    tr->ramp_deg = 0;
    tr->sub_to_next = NEVER;
    ext_arm(s, t);                    /* the first clock is the downbeat */
    tr->off_q8 = 0;
    tr->age = 0;
}

static void restart(seq_t *s, uint32_t tracks)   /* tracks whose pattern switched: from step 1 */
{
    int t;
    for (t = 0; t < NSEQT; t++) {
        if (!(tracks >> t & 1u))
            continue;
        track_reset(s, t);
        s->t[t].to_next = s->ext ? NEVER : 0;
    }
    if (tracks & 1u) {
        bar_len_update(s);
        s->bar_six = 0;
        s->bar_fresh = 1;
    }
}

/* ------------------------------------------------------------- a step --- */
static int next_pos(seq_t *s, int t, int *wrapped)
{
    seq_trk_t *tr = &s->t[t];
    int len = pat_len(tpat(s, t)), p = tr->pos, n;
    *wrapped = 0;
    if (tr->jump_to != 0xFF) {
        n = tr->jump_to % len;
        tr->jump_to = 0xFF;
        *wrapped = n == 0 && p != 0;
        return n;
    }
    if (is_synth(s, t) && fx_on(s, t, PM_LOOP4)) {
        if (!tr->loop4_on) {
            tr->loop4_on = 1;
            tr->loop4_from = (uint8_t)(p & ~3);
        }
        n = p + 1;
        if (n >= len || (n & 3) == 0)
            n = tr->loop4_from % len;
        return n;
    }
    tr->loop4_on = 0;
    if (is_synth(s, t) && fx_on(s, t, PM_REVERSE)) {
        n = p - 1;
        if (n < 0) {
            n = len - 1;
            *wrapped = 1;
        }
        return n;
    }
    n = p + 1;
    if (n >= len) {
        n = 0;
        *wrapped = 1;
    }
    return n;
}

static void jump_set(seq_t *s, int t, const step_t *st, int p)
{
    seq_trk_t *tr = &s->t[t];
    int len = pat_len(tpat(s, t)), cv = st->cval, n;
    if (cv == 9)
        cv = (int)(rng_next(&tr->rng) % 9u);
    switch (cv) {
    case 0: case 1: case 2: case 3: n = cv * 4; break;
    case 4: n = p + 2; break;                                 /* one forward: skip the next */
    case 5: n = p - 1; break;                                 /* one back */
    case 6: n = (rng_next(&tr->rng) & 1u) ? p + 2 : p - 1; break;
    case 7: n = p; break;                                     /* stay */
    case 8: {                                                 /* align: where the bar says the track would be */
        int h = TS_HALVES[tpat(s, t)->scale < NTS ? tpat(s, t)->scale : TS_1];
        n = (int)(((s->bar_six + 1u) * 2u) / (uint32_t)h);
        break;
    }
    default: n = (int)(rng_next(&tr->rng) % (uint32_t)len); break;
    }
    tr->jump_to = (uint8_t)(((n % len) + len) % len);
}

static void fire_step(seq_t *s, int t, const seq_sink_t *o)
{
    seq_trk_t *tr = &s->t[t];
    const tpat_t *tp;
    const step_t *st;
    uint32_t L, carry = tr->to_next < 256u ? tr->to_next : 0, nL;
    int p, wrapped = 0, retrig = 1, plays, np, dummy;
    int32_t off, noff;
    if (!tr->started) {
        tr->started = 1;
        p = 0;
        tr->stall = 0;
    } else if (tr->stall) {           /* pulse / hold: the same step again */
        tr->stall--;
        p = tr->pos;
        retrig = tr->stall_kind == SC_PULSE;
    } else {
        p = next_pos(s, t, &wrapped);
        if (wrapped)
            tr->pass++;
    }
    tp = tpat(s, t);
    st = &tp->st[p];
    tr->pos = (uint8_t)p;
    tr->age = 0;
    L = step_len(s, t);
    tr->step_q8 = L;
    tr->ratchet = 0;
    tr->ratchets = 1;
    tr->sub_to_next = NEVER;
    if (retrig)
        apply_locks(s, t, p, L, o);
    if (o->step)
        o->step(o->ctx, t, p);
    plays = retrig && !(s->mute >> t & 1u) && !punch_muted(s, t) && !step_empty(st) && step_plays(s, t, st);
    if (plays) {
        switch (st->comp) {
        case SC_PULSE:
        case SC_HOLD:
            if (!tr->stall && retrig && tr->stall_kind != st->comp) {   /* a fresh step: start the stall */
                int n = st->cval == SC_RANDOM_VAL ? 1 + (int)(rng_next(&tr->rng) % 9u) : st->cval + 1;
                tr->stall = (uint8_t)n;
                tr->stall_kind = st->comp;
            }
            break;
        case SC_MULTIPLY:
            tr->ratchets = (uint8_t)(st->cval == SC_RANDOM_VAL ? 1 + (int)(rng_next(&tr->rng) % 9u) : st->cval + 1);
            if (tr->ratchets > 1)
                tr->sub_to_next = carry + L / tr->ratchets;
            break;
        case SC_RAMP_UP:
        case SC_RAMP_DOWN:
        case SC_RANDOM:
            tr->ramp_deg = (int16_t)ramp_degree(s, t, st);
            break;
        case SC_BEND:
            bend_start(s, t, st, L);
            break;
        case SC_JUMP:
            jump_set(s, t, st, p);
            break;
        default:
            break;
        }
        if (st->comp != SC_RAMP_UP && st->comp != SC_RAMP_DOWN && st->comp != SC_RANDOM)
            tr->ramp_deg = 0;
        if (t < NTRK)
            fire_notes(s, t, st, p, L, 0, o);
        else
            fire_aux(s, t, st, p, L, o);
    } else
        tr->ramp_deg = 0;
    if (!tr->stall)
        tr->stall_kind = 0;
    if (is_drum(s, t) && !(s->mute >> t & 1u) && !punch_muted(s, t))
        fire_fills(s, t, st, p, o);
    /* the next step: its grid plus its offset, less this step's */
    off = tr->off_q8;
    if (tr->stall)
        np = p;
    else {
        uint8_t save_l4 = tr->loop4_on, save_j = tr->jump_to;
        np = next_pos(s, t, &dummy);  /* a peek: undo what it may have changed */
        tr->loop4_on = save_l4;
        tr->jump_to = save_j;
    }
    noff = step_off(s, t, np, L);
    tr->off_q8 = noff;
    if (s->ext) {
        tr->to_next = NEVER;          /* the clock arms the next one */
        return;
    }
    nL = L;
    if ((int32_t)nL + noff - off < 256)
        nL = 256u;
    else
        nL = (uint32_t)((int32_t)nL + noff - off);
    tr->to_next = carry + nL;
}

/* a ratchet hit */
static void fire_sub(seq_t *s, int t, const seq_sink_t *o)
{
    seq_trk_t *tr = &s->t[t];
    const step_t *st = &tpat(s, t)->st[tr->pos];
    uint32_t carry = tr->sub_to_next < 256u ? tr->sub_to_next : 0;
    tr->ratchet++;
    if (t < NTRK && st->comp == SC_MULTIPLY)
        fire_notes(s, t, st, tr->pos, tr->step_q8, tr->ratchet, o);
    tr->sub_to_next = tr->ratchet + 1 < tr->ratchets ? carry + tr->step_q8 / tr->ratchets : NEVER;
}

/* the stutter FX: the step's notes again every 1/32 */
static void fire_stutter(seq_t *s, int t, const seq_sink_t *o)
{
    seq_trk_t *tr = &s->t[t];
    uint32_t carry = tr->stut_to_next < 256u ? tr->stut_to_next : 0;
    int i;
    if (punch_muted(s, t) || (s->mute >> t & 1u)) {
        /* muted: silent */
    } else if (is_drum(s, t)) {
        if (tr->held_drum)
            emit_drum(s, t, tr->held_drum - 1, tr->last_vel ? tr->last_vel : FILL_VEL, o);
    } else {
        for (i = 0; i < tr->nlast; i++)
            emit_note(s, t, tr->last_note[i], tr->last_vel, 0, TAG_STEP | (uint32_t)(tr->last_step * 16 + i),
                      six_q8(s) / 4u, o);
    }
    tr->stut_to_next = carry + six_q8(s) / 2u;
}

static void fire_echo(seq_t *s, int t, const seq_sink_t *o)
{
    seq_trk_t *tr = &s->t[t];
    int i, n = tr->echo_n;
    tr->echo_to_next = NEVER;
    tr->echo_n = 0;
    for (i = 0; i < n; i++)
        if (o->drum)
            o->drum(o->ctx, t, tr->echo_sound[i], tr->echo_vel, fx_on(s, t, PD_OCT_UP) ? 12 : 0,
                    fx_on(s, t, PD_SHORT), fx_on(s, t, PD_TAILS));
}

/* ---------------------------------------------------------- the players --- */
static void player_note_on(seq_t *s, int t, int note, int vel, uint32_t tag, const seq_sink_t *o)
{
    if (o->note_on)
        o->note_on(o->ctx, t, note, vel, 0, tag);
    midi_on(s, t, note, vel, o);
}

static void player_note_off(seq_t *s, int t, int note, uint32_t tag, const seq_sink_t *o)
{
    if (o->note_off)
        o->note_off(o->ctx, t, tag);
    midi_off(s, t, note, o);
}

static void sounding_add(player_t *pl, int note)
{
    if (pl->nsounding < 16)
        pl->sounding[pl->nsounding++] = (uint8_t)note;
}

static void sounding_off(seq_t *s, int t, const seq_sink_t *o)
{
    player_t *pl = &s->player[t];
    int i;
    for (i = 0; i < pl->nsounding; i++)
        player_note_off(s, t, pl->sounding[i], TAG_PLAYER | pl->sounding[i], o);
    pl->nsounding = 0;
    pl->roll_to_next = NEVER;
    pl->nroll = 0;
    pl->maestro_root = 0;
}

static void arp_off(seq_t *s, int t, const seq_sink_t *o)
{
    player_t *pl = &s->player[t];
    if (pl->arp_note) {
        player_note_off(s, t, pl->arp_note, TAG_ARP | pl->arp_note, o);
        pl->arp_note = 0;
    }
    pl->arp_to_off = NEVER;
}

static void arp_stop(seq_t *s, int t, const seq_sink_t *o)
{
    player_t *pl = &s->player[t];
    arp_off(s, t, o);
    pl->arp_to_next = NEVER;
    pl->arp_i = 0;
    pl->arp_oct = 0;
    pl->arp_dir = 0;
    pl->nheld = 0;
    pl->latched = 0;
}

static void held_add(player_t *pl, int note)
{
    int i;
    for (i = 0; i < pl->nheld; i++)
        if (pl->held[i] == note)
            return;
    if (pl->nheld < 16)
        pl->held[pl->nheld++] = (uint8_t)note;
}

static void held_remove(player_t *pl, int note)
{
    int i, j;
    for (i = 0; i < pl->nheld; i++)
        if (pl->held[i] == note) {
            for (j = i; j < pl->nheld - 1; j++)
                pl->held[j] = pl->held[j + 1];
            pl->nheld--;
            return;
        }
}

/* the arp's period in samples Q8 from P1 */
static uint32_t arp_period(const seq_t *s, int t)
{
    static const uint8_t NUM[6] = {4, 2, 4, 1, 2, 1}, DEN[6] = {1, 1, 3, 1, 3, 2};
    int sp = s->par[t][TP_P1];
    if (sp > 5)
        sp = 5;
    return six_q8(s) / DEN[sp] * NUM[sp];
}

/* samples to the next grid line of the arp's period while playing (0 when stopped) */
static uint32_t arp_align(const seq_t *s, int t)
{
    uint32_t P = arp_period(s, t), six = six_q8(s), T, into, next;
    if (!s->playing || !P)
        return 0;
    into = six > s->six_to_next ? six - s->six_to_next : 0;
    T = (s->step_tick & 63u) * six + into;
    next = (T / P + 1u) * P;
    if (next - T < P / 8u)
        return 0;
    return next - T;
}

static void arp_fire(seq_t *s, int t, const seq_sink_t *o)
{
    player_t *pl = &s->player[t];
    uint32_t P = arp_period(s, t), carry = pl->arp_to_next < 256u ? pl->arp_to_next : 0;
    uint8_t list[16];
    int n = pl->nheld, i, j, oct = s->par[t][TP_P3], range, pat = s->par[t][TP_P2], idx, note, total;
    arp_off(s, t, o);
    if (!n) {
        pl->arp_to_next = NEVER;
        return;
    }
    range = oct < 1 ? 1 : oct > 4 ? 4 : oct;
    for (i = 0; i < n; i++)           /* sorted, unless played order */
        list[i] = pl->held[i];
    if (pat != 4)
        for (i = 1; i < n; i++) {
            uint8_t v = list[i];
            for (j = i; j > 0 && list[j - 1] > v; j--)
                list[j] = list[j - 1];
            list[j] = v;
        }
    total = n * range;
    switch (pat) {
    case 1:                           /* down */
        idx = total - 1 - (int)(pl->arp_i % (uint32_t)total);
        pl->arp_i++;
        break;
    case 2:                           /* up-down, the ends once */
        if (total <= 1)
            idx = 0;
        else {
            int cyc = total * 2 - 2, k = (int)(pl->arp_i % (uint32_t)cyc);
            idx = k < total ? k : cyc - k;
        }
        pl->arp_i++;
        break;
    case 3:                           /* random */
        idx = (int)(rng_next(&s->rng) % (uint32_t)total);
        break;
    default:                          /* up, played */
        idx = (int)(pl->arp_i % (uint32_t)total);
        pl->arp_i++;
        break;
    }
    note = list[idx % n] + 12 * (idx / n);
    note = clamp_note(brain_note(s, t, note, 1));
    player_note_on(s, t, note, 100, TAG_ARP | (uint32_t)note, o);
    pl->arp_note = (uint8_t)note;
    pl->arp_to_off = carry + P / 2u;
    pl->arp_to_next = carry + P;
}

static void maestro_play(seq_t *s, int t, int key, int vel, const seq_sink_t *o)
{
    player_t *pl = &s->player[t];
    int n = pl->nchord, i, j, lo = 128, pat = s->par[t][TP_P2], roll = s->par[t][TP_P1];
    uint8_t list[SN * 2];
    sounding_off(s, t, o);
    if (!n)
        return;
    for (i = 0; i < n; i++)
        if (pl->chord[i] < lo)
            lo = pl->chord[i];
    for (i = 0; i < n; i++) {         /* the chord moved so its lowest note is the key, sorted up */
        uint8_t v = (uint8_t)clamp_note(pl->chord[i] - lo + key);
        for (j = i; j > 0 && list[j - 1] > v; j--)
            list[j] = list[j - 1];
        list[j] = v;
    }
    if (pat == 1 || (pat == 2 && (pl->arp_dir ^= 1)))      /* down, or every second strum */
        for (i = 0; i < n / 2; i++) {
            uint8_t v = list[i];
            list[i] = list[n - 1 - i];
            list[n - 1 - i] = v;
        }
    else if (pat == 3)
        for (i = n - 1; i > 0; i--) {
            int k = (int)(rng_next(&s->rng) % (uint32_t)(i + 1));
            uint8_t v = list[i];
            list[i] = list[k];
            list[k] = v;
        }
    for (i = 0; i < n; i++)
        pl->roll[i] = list[i];
    pl->nroll = (uint8_t)n;
    pl->roll_i = 0;
    pl->roll_vel = (uint8_t)vel;
    pl->maestro_root = (uint8_t)key;
    if (!roll) {
        for (i = 0; i < n; i++) {
            player_note_on(s, t, list[i], vel, TAG_PLAYER | list[i], o);
            sounding_add(pl, list[i]);
        }
        pl->nroll = 0;
        pl->roll_to_next = NEVER;
    } else
        pl->roll_to_next = 0;         /* the first note now, the rest spaced by the roll */
}

static void roll_fire(seq_t *s, int t, const seq_sink_t *o)
{
    player_t *pl = &s->player[t];
    uint32_t carry = pl->roll_to_next < 256u ? pl->roll_to_next : 0;
    int roll = s->par[t][TP_P1], note;
    if (pl->roll_i >= pl->nroll) {
        pl->roll_to_next = NEVER;
        pl->nroll = 0;
        return;
    }
    note = pl->roll[pl->roll_i++];
    player_note_on(s, t, note, pl->roll_vel, TAG_PLAYER | (uint32_t)note, o);
    sounding_add(pl, note);
    if (pl->roll_i >= pl->nroll) {
        pl->roll_to_next = NEVER;
        pl->nroll = 0;
    } else                            /* 0..127 -> 0..120 ms between the notes (11290 = a ms in samples Q8) */
        pl->roll_to_next = carry + (uint32_t)roll * 120u / 127u * 11290u;
}

static void phys_set(player_t *pl, int note, int on)
{
    uint32_t b = 1u << (note & 31);
    int had = (pl->phys[note >> 5] & b) != 0;
    if (on && !had) {
        pl->phys[note >> 5] |= b;
        pl->nphys++;
    } else if (!on && had) {
        pl->phys[note >> 5] &= ~b;
        pl->nphys--;
    }
}

/* live recording: the key into the track's pattern at the quantised step */
static void rec_key_on(seq_t *s, int t, int note, int vel)
{
    tpat_t *tp = tpat(s, t);
    seq_trk_t *tr = &s->t[t];
    uint32_t L = tr->step_q8 ? tr->step_q8 : step_len(s, t);
    int len = pat_len(tp), p = tr->pos, i, q = tp->quant > 100 ? 100 : tp->quant;
    int32_t age = (int32_t)(tr->age > L ? L : tr->age), ticks;
    step_t *st;
    if (!tr->started)
        return;
    if ((uint32_t)age > L / 2u) {     /* nearer the next step */
        p = (p + 1) % len;
        age -= (int32_t)L;
    }
    ticks = age * PPS / (int32_t)L;
    ticks = ticks * (100 - q) / 100;
    ticks = ticks < -12 ? -12 : ticks > 12 ? 12 : ticks;
    st = &tp->st[p];
    if (step_empty(st)) {
        st->note[0] = (uint8_t)note;
        st->vel = (uint8_t)vel;
        st->len = 0;
        st->nudge = (int8_t)ticks;
    } else {
        for (i = 0; i < SN; i++)
            if (st->note[i] == note)
                break;
        if (i == SN)
            for (i = 0; i < SN; i++)
                if (!st->note[i]) {
                    st->note[i] = (uint8_t)note;
                    break;
                }
    }
    for (i = 0; i < 8; i++)
        if (!s->rec_note[i]) {
            s->rec_note[i] = (uint8_t)note;
            s->rec_trk[i] = (uint8_t)t;
            s->rec_step[i] = (uint8_t)p;
            s->rec_age[i] = 0;
            break;
        }
}

static void rec_key_off(seq_t *s, int t, int note)
{
    int i, j;
    for (i = 0; i < 8; i++) {
        if (s->rec_note[i] != note || s->rec_trk[i] != t)
            continue;
        if (s->rec_step[i] < NSTEP) {
            tpat_t *tp = tpat(s, t);
            step_t *st = &tp->st[s->rec_step[i]];
            uint32_t L = s->t[t].step_q8 ? s->t[t].step_q8 : step_len(s, t);
            int len = (int)((s->rec_age[i] + L / 8u) / (L / 4u));   /* quarter steps, rounded */
            len = len < 1 ? 1 : len > 16 ? 16 : len;
            for (j = 0; j < SN; j++)
                if (st->note[j] == note)
                    st->len = (uint8_t)len;
        }
        s->rec_note[i] = 0;
        return;
    }
}

/* a key on an instrument track, inside the interrupt */
static void key_in(seq_t *s, int t, int note, int on, int vel, const seq_sink_t *o)
{
    player_t *pl;
    int player, had_phys, shifted;
    if (t < 0 || t >= NTRK || note < 1 || note > 127)
        return;
    pl = &s->player[t];
    player = s->par[t][TP_PLAYER];
    had_phys = pl->nphys;
    if (s->rec && s->playing && !s->count_in) {
        if (on)
            rec_key_on(s, t, note, vel);
        else
            rec_key_off(s, t, note);
    }
    if (is_drum(s, t)) {
        if (on && note <= NDRUM)
            emit_drum(s, t, note - 1, vel, o);
        return;
    }
    phys_set(pl, note, on);
    shifted = clamp_note(brain_note(s, t, note, 1));
    switch (player) {
    case PL_ARP:
        if (on) {
            if (!had_phys && pl->latched) {   /* a new phrase replaces the latched one */
                pl->nheld = 0;
                pl->latched = 0;
            }
            held_add(pl, note);
            if (pl->arp_to_next == NEVER) {
                pl->arp_i = 0;
                pl->arp_to_next = arp_align(s, t);
            }
        } else {
            if (s->par[t][TP_P4])
                pl->latched = 1;
            else {
                held_remove(pl, note);
                if (!pl->nheld)
                    arp_stop(s, t, o);
            }
        }
        break;
    case PL_MAESTRO:
        if (on)
            maestro_play(s, t, shifted, vel, o);
        else if (!s->par[t][TP_P4] && pl->maestro_root == shifted && !pl->nphys)
            sounding_off(s, t, o);
        break;
    case PL_HOLD:
        if (on) {
            if (!had_phys)
                sounding_off(s, t, o);
            player_note_on(s, t, shifted, vel, TAG_PLAYER | (uint32_t)shifted, o);
            sounding_add(pl, shifted);
        }
        break;
    default:
        if (on)
            player_note_on(s, t, shifted, vel, TAG_KEY | (uint32_t)note, o);
        else
            player_note_off(s, t, shifted, TAG_KEY | (uint32_t)note, o);
        break;
    }
    if (on && s->par[t][TP_BRAIN])
        brain_hear(s, shifted);
}

static void keys_all_off(seq_t *s, int t, const seq_sink_t *o)
{
    player_t *pl = &s->player[t];
    int i, w;
    if (s->par[t][TP_PLAYER] == PL_OFF)   /* direct keys: their notes; a player's notes are in sounding[] */
        for (w = 0; w < 4; w++)
            for (i = 0; i < 32; i++)
                if (pl->phys[w] >> i & 1u)
                    player_note_off(s, t, w * 32 + i, TAG_KEY | (uint32_t)(w * 32 + i), o);
    for (w = 0; w < 4; w++)
        pl->phys[w] = 0;
    pl->nphys = 0;
    sounding_off(s, t, o);
    arp_stop(s, t, o);
}

static void brain_key_in(seq_t *s, int note, int on)
{
    uint16_t mask = BRAIN_SCALE[s->brain_scale < NBS ? s->brain_scale : BS_MAJOR];
    int d;
    if (!on)
        return;
    d = note_to_deg(note, s->brain_key, mask) - note_to_deg(60 + s->brain_key, s->brain_key, mask);
    d = d < -64 ? -64 : d > 63 ? 63 : d;
    s->brain_deg = (uint8_t)(int8_t)d;
}

/* the main loop's keys, played here */
enum { KQ_KEY, KQ_ALLOFF, KQ_BRAIN, KQ_TAPE };
static void key_queue(seq_t *s, int kind, int trk, int note, int on, int vel)
{
    uint8_t w = s->kq_w, n = (uint8_t)((w + 1) & 31);
    if (n == s->kq_r)
        return;                       /* full: the key is lost */
    s->kq[w] = (uint32_t)kind << 28 | (uint32_t)(trk & 15) << 20 | (uint32_t)(note & 127) << 8 |
               (uint32_t)(on ? 0x80 : 0) | (uint32_t)(vel & 127);
    s->kq_w = n;
}

static void keys_play(seq_t *s, const seq_sink_t *o)
{
    while (s->kq_r != s->kq_w) {
        uint32_t e = s->kq[s->kq_r];
        int kind = (int)(e >> 28), trk = (int)(e >> 20 & 15), note = (int)(e >> 8 & 127), on = (e & 0x80) != 0,
            vel = (int)(e & 127);
        s->kq_r = (uint8_t)((s->kq_r + 1) & 31);
        switch (kind) {
        case KQ_KEY: key_in(s, trk, note, on, vel ? vel : 1, o); break;
        case KQ_ALLOFF: if (trk < NTRK) keys_all_off(s, trk, o); break;
        case KQ_BRAIN: brain_key_in(s, note, on); break;
        case KQ_TAPE: if (o->tape_seg) o->tape_seg(o->ctx, note, on); break;
        default: break;
        }
    }
}

/* ------------------------------------------------------------ transport --- */
static void punch_update(seq_t *s, const seq_sink_t *o)
{
    uint32_t u = s->punch_keys | s->punch_seq, d = u & 0xFFFu, m = u >> 16 & 0x7FFFu;
    int t;
    if (d != s->punch_sent_d || m != s->punch_sent_m) {
        s->punch_sent_d = d;
        s->punch_sent_m = m;
        if (o->punch)
            o->punch(o->ctx, d, m);
    }
    for (t = 0; t < NTRK; t++) {      /* the stutter timers and the ramp follow the state */
        seq_trk_t *tr = &s->t[t];
        int drum = is_drum(s, t), st = drum ? fx_on(s, t, PD_STUTTER) : fx_on(s, t, PM_STUTTER);
        if (st && !tr->stut_on) {
            tr->stut_on = 1;
            tr->stut_to_next = six_q8(s) / 2u;
        } else if (!st && tr->stut_on) {
            tr->stut_on = 0;
            tr->stut_to_next = NEVER;
        }
        if (!drum && !fx_on(s, t, PM_RAMP))
            tr->ramp_semi = 0;
    }
}

static void tracks_go(seq_t *s)
{
    int t;
    for (t = 0; t < NSEQT; t++)
        s->t[t].to_next = s->ext ? NEVER : 0;
}

static void do_start(seq_t *s, int from_top, const seq_sink_t *o)
{
    int t;
    if (from_top) {
        bar_end(s, 1, o);
        for (t = 0; t < NSEQT; t++)
            track_reset(s, t);
        s->bar_six = 0;
        s->bar16 = 0;
        s->step_tick = 0;
    }
    bar_len_update(s);
    for (t = 0; t < NSEQT; t++)
        ext_arm(s, t);
    s->six_to_next = s->ext ? NEVER : 0;
    s->ext_six = 5;
    s->bar_fresh = 1;
    s->clk_to_next = 0;
    s->playing = 1;
    s->countin_six = 0;
    if (!s->rec)
        s->count_in = 0;
    if (s->count_in) {
        for (t = 0; t < NSEQT; t++)
            s->t[t].to_next = NEVER;
    } else {
        tracks_go(s);
        if (!s->ext && s->send_clock)
            midi(o, from_top ? 0xFA : 0xFB, 0, 0);
    }
    if (o->tape_step) {
        s->tape_last = six_q8(s) >> 8;
        o->tape_step(o->ctx, s->tape_last);
    }
}

static void do_stop(seq_t *s, const seq_sink_t *o)
{
    int t;
    s->playing = 0;
    s->count_in = 0;
    for (t = 0; t < NSEQT; t++) {
        seq_trk_t *tr = &s->t[t];
        cancel_offs(s, t, o);
        tr->to_next = NEVER;
        tr->sub_to_next = NEVER;
        tr->echo_to_next = NEVER;
        tr->echo_n = 0;
        tr->stall = 0;
        if (tr->bend_on) {
            tr->bend_on = 0;
            if (o->bend)
                o->bend(o->ctx, t, 0);
            tr->bend_last = 0;
        }
    }
    for (t = 0; t < NTRK; t++)
        keys_all_off(s, t, o);
    for (t = 0; t < 8; t++)
        s->rec_note[t] = 0;
    s->punch_seq = 0;
    punch_update(s, o);
    locks_release(s, o);
    s->six_to_next = NEVER;
    if (!s->ext && s->send_clock)
        midi(o, 0xFC, 0, 0);
}

/* the sixteenth: the metronome, the count-in, the bar */
static void six_tick(seq_t *s, const seq_sink_t *o)
{
    int click = s->metronome == 2 || (s->metronome == 1 && (s->rec || s->count_in));
    uint32_t carry = s->six_to_next < 256u ? s->six_to_next : 0;
    if (s->count_in) {
        if (s->countin_six >= 16) {
            s->countin_six = 0;
            s->count_in--;
        }
        if (s->count_in) {
            if (click && (s->countin_six & 3) == 0 && o->metro)
                o->metro(o->ctx, s->countin_six == 0);
            s->countin_six++;
            s->bar16 = (uint8_t)(s->countin_six & 15);
            s->six_to_next = s->ext ? NEVER : carry + six_q8(s);
            return;
        }
        tracks_go(s);                 /* the count-in is over: this sixteenth is the downbeat, recording begins */
        s->bar_six = 0;
        s->bar_fresh = 1;
        s->step_tick = 0;
        if (!s->ext && s->send_clock)
            midi(o, 0xFA, 0, 0);
    }
    if (s->bar_six >= s->bar_len)
        s->bar_six = 0;
    if (s->bar_six == 0 && !s->bar_fresh) {   /* the bar: cues, the song, the brain's detection */
        uint32_t ch = bar_end(s, 0, o);
        brain_bar(s);
        if (ch)
            restart(s, ch);
    }
    s->bar_fresh = 0;
    s->bar16 = (uint8_t)(s->bar_six & 15);
    if (click && (s->bar16 & 3) == 0 && o->metro)
        o->metro(o->ctx, s->bar16 == 0);
    s->ramp_ticks++;                  /* the ramp FX rises after this sixteenth's steps have fired */
    s->bar_six++;
    s->step_tick++;
    s->six_to_next = s->ext ? NEVER : carry + six_q8(s);
}

/* external clock ticks: arm the sixteenth and the steps they complete */
static void ext_ticks(seq_t *s)
{
    uint32_t n = s->ext_pending;
    int t;
    s->ext_pending = 0;
    for (; n; n--) {
        if (s->ext_since > 0 && s->ext_since < SEQ_FS) {
            uint32_t q = s->ext_since << 8;
            s->ext_period = s->ext_period ? s->ext_period - (s->ext_period >> 3) + (q >> 3) : q;
            s->ext_bpm = (float)SEQ_FS * 60.0f * 256.0f / (24.0f * (float)s->ext_period);
        }
        s->ext_since = 0;
        if (!s->playing)
            continue;
        if (++s->ext_six >= 6) {
            s->ext_six = 0;
            s->six_to_next = 0;
        }
        if (s->count_in)
            continue;
        for (t = 0; t < NSEQT; t++) {
            seq_trk_t *tr = &s->t[t];
            if (++tr->ext_ticks < ext_tps(s, t))
                continue;
            tr->ext_ticks = 0;
            if (tr->to_next == NEVER) {
                int32_t off = tr->off_q8;   /* this step's own offset, never early */
                tr->to_next = off > 0 ? (uint32_t)off : 0;
            }
        }
    }
}

/* ------------------------------------------------------------- the API --- */
void seq_init(seq_t *s, patterns_t *P, uint8_t (*par)[NTP_ALL])
{
    uint32_t i;
    uint8_t *z = (uint8_t *)s;
    int t;
    for (i = 0; i < sizeof *s; i++)
        z[i] = 0;
    s->P = P;
    s->par = par;
    s->bpm = 120.0f;
    s->ext_bpm = 120.0f;
    s->send_clock = 1;
    s->cue = 0xFF;
    s->req_scene = 0xFF;
    s->punch_target = 0xFF;
    s->rng = 0x2545F491u;
    for (t = 0; t < NSEQT; t++) {
        seq_trk_t *tr = &s->t[t];
        tr->rng = 0x9E3779B9u * (uint32_t)(t + 1);
        tr->to_next = NEVER;
        tr->sub_to_next = NEVER;
        tr->stut_to_next = NEVER;
        tr->echo_to_next = NEVER;
        tr->jump_to = 0xFF;
        s->ppat[t] = P->scene[0].pat[t] < NPATT ? P->scene[0].pat[t] : 0;
        s->pcue[t] = 0xFF;
    }
    for (t = 0; t < NTRK; t++) {
        s->player[t].arp_to_next = NEVER;
        s->player[t].arp_to_off = NEVER;
        s->player[t].roll_to_next = NEVER;
    }
    for (i = 0; i < NLANE; i++)
        s->lrt[i].applied = LOCK_NONE;
    s->mute = P->scene[0].mute;
    s->brain_scale = BS_MAJOR;
    s->six_to_next = NEVER;
    s->clk_to_next = NEVER;
    bar_len_update(s);
}

uint32_t seq_until_event(seq_t *s, uint32_t n)
{
    uint32_t m = n, e;
    int t, i;
    if (s->kq_r != s->kq_w || s->req_start || s->req_cont || s->req_stop || s->req_scene != 0xFF)
        return 1;                     /* keys or requests wait: play them now */
    e = s->punch_keys | s->punch_seq;
    if ((e & 0xFFFu) != s->punch_sent_d || (e >> 16 & 0x7FFFu) != s->punch_sent_m)
        return 1;                     /* a punch-in key changed */
    for (t = 0; t < NTRK; t++) {      /* the players run with the transport stopped too */
        const player_t *pl = &s->player[t];
        if (pl->arp_to_next != NEVER && (e = pl->arp_to_next >> 8) < m)
            m = e;
        if (pl->arp_to_off != NEVER && (e = pl->arp_to_off >> 8) < m)
            m = e;
        if (pl->roll_to_next != NEVER && (e = pl->roll_to_next >> 8) < m)
            m = e;
    }
    for (t = 0; t < NSEQT; t++) {
        const seq_trk_t *tr = &s->t[t];
        if (tr->stut_to_next != NEVER && (e = tr->stut_to_next >> 8) < m)
            m = e;
        if (tr->echo_to_next != NEVER && (e = tr->echo_to_next >> 8) < m)
            m = e;
        for (i = 0; i < SEQ_PENDING; i++)
            if (tr->off[i].used && (e = tr->off[i].off_at >> 8) < m)
                m = e;
    }
    if (!s->playing)
        return m ? m : 1;
    for (t = 0; t < NSEQT; t++) {
        const seq_trk_t *tr = &s->t[t];
        if (tr->to_next != NEVER && (e = tr->to_next >> 8) < m)
            m = e;
        if (tr->sub_to_next != NEVER && (e = tr->sub_to_next >> 8) < m)
            m = e;
    }
    if (s->six_to_next != NEVER && (e = s->six_to_next >> 8) < m)
        m = e;
    if (!s->ext && s->send_clock && (e = s->clk_to_next >> 8) < m)
        m = e;
    if (s->chunk_cap && m > SY_CHUNK)
        m = SY_CHUNK;
    return m ? m : 1;
}

static void dec(uint32_t *v, uint32_t dq)
{
    if (*v != NEVER)
        *v = *v > dq ? *v - dq : 0;
}

void seq_advance(seq_t *s, uint32_t k, const seq_sink_t *o)
{
    uint32_t dq = k << 8;
    int t, i, i2, guard, cap = 0;
    s->ext_since += k;
    if (s->ext_pending)
        s->ext = 1;
    else if (s->ext && s->ext_since > SEQ_FS / 2u) {   /* the clock gone for 0.5 s: back to internal */
        s->ext = 0;
        if (s->playing) {
            for (t = 0; t < NSEQT; t++)
                if (s->t[t].to_next == NEVER && !s->count_in)
                    s->t[t].to_next = 0;
            if (s->six_to_next == NEVER)
                s->six_to_next = 0;
        }
    }
    brain_follow_par(s);
    if (s->req_stop) {
        s->req_stop = 0;
        s->req_start = s->req_cont = 0;
        if (s->playing)
            do_stop(s, o);
        else                          /* stopped already: the players let go (hold, latched arps) */
            for (t = 0; t < NTRK; t++)
                keys_all_off(s, t, o);
    }
    if (s->req_start) {
        s->req_start = 0;
        do_start(s, 1, o);
    }
    if (s->req_cont) {
        s->req_cont = 0;
        do_start(s, 0, o);
    }
    if (s->req_scene != 0xFF) {       /* a scene now */
        uint8_t next[NSEQT];
        uint32_t ch = 0;
        int k2 = s->req_scene;
        s->req_scene = 0xFF;
        if (k2 < NSCENE) {
            scene_read(s, k2, next);
            if (o->scene)
                o->scene(o->ctx, k2);
            for (t = 0; t < NSEQT; t++)
                if (next[t] != s->ppat[t]) {
                    s->ppat[t] = next[t];
                    ch |= 1u << t;
                }
            if (ch && s->playing)
                restart(s, ch);
            else if (ch)
                bar_len_update(s);
        }
    }
    if (s->ext_pending)
        ext_ticks(s);
    punch_update(s, o);
    keys_play(s, o);
    if (o->tape_step && s->playing) {
        uint32_t six = six_q8(s) >> 8;
        if (six != s->tape_last) {
            s->tape_last = six;
            o->tape_step(o->ctx, six);
        }
    }
    /* move time */
    for (t = 0; t < NSEQT; t++) {
        seq_trk_t *tr = &s->t[t];
        dec(&tr->to_next, dq);
        dec(&tr->sub_to_next, dq);
        dec(&tr->stut_to_next, dq);
        dec(&tr->echo_to_next, dq);
        tr->age += dq;
        for (i = 0; i < SEQ_PENDING; i++)
            if (tr->off[i].used)
                dec(&tr->off[i].off_at, dq);
    }
    for (t = 0; t < NTRK; t++) {
        dec(&s->player[t].arp_to_next, dq);
        dec(&s->player[t].arp_to_off, dq);
        dec(&s->player[t].roll_to_next, dq);
    }
    for (i = 0; i < 8; i++)
        if (s->rec_note[i])
            s->rec_age[i] += dq;
    dec(&s->six_to_next, dq);
    if (!s->ext && s->send_clock)
        dec(&s->clk_to_next, dq);
    /* fire what is due */
    for (guard = 0; guard < 8; guard++) {
        int fired = 0;
        if (s->playing && s->six_to_next < 256u) {   /* the sixteenth first: its bar may restart tracks */
            six_tick(s, o);
            fired = 1;
        }
        for (i2 = 0; i2 < NSEQT; i2++) {       /* the aux tracks first: their steps set what the others hear */
            seq_trk_t *tr;
            t = i2 < NSEQT - NTRK ? NTRK + i2 : i2 - (NSEQT - NTRK);
            tr = &s->t[t];
            for (i = 0; i < SEQ_PENDING; i++)    /* note-offs before a new step at the same time */
                if (tr->off[i].used && tr->off[i].off_at < 256u) {
                    fire_off(s, t, &tr->off[i], o);
                    fired = 1;
                }
            if (tr->echo_to_next < 256u) {
                fire_echo(s, t, o);
                fired = 1;
            }
            if (!s->playing)
                continue;
            if (tr->to_next < 256u) {
                fire_step(s, t, o);
                fired = 1;
            }
            if (tr->sub_to_next < 256u) {
                fire_sub(s, t, o);
                fired = 1;
            }
        }
        for (t = 0; t < NTRK; t++) {
            seq_trk_t *tr = &s->t[t];
            player_t *pl = &s->player[t];
            if (tr->stut_to_next < 256u) {
                fire_stutter(s, t, o);
                fired = 1;
            }
            if (pl->arp_to_off < 256u) {
                arp_off(s, t, o);
                fired = 1;
            }
            if (pl->arp_to_next < 256u) {
                arp_fire(s, t, o);
                fired = 1;
            }
            if (pl->roll_to_next < 256u) {
                roll_fire(s, t, o);
                fired = 1;
            }
        }
        if (!fired)
            break;
    }
    for (; s->ramp_ticks; s->ramp_ticks--)
        for (t = 0; t < NTRK; t++)
            if (is_synth(s, t) && fx_on(s, t, PM_RAMP) && s->t[t].ramp_semi < 48)
                s->t[t].ramp_semi++;
    if (s->playing && !s->ext && s->send_clock && s->clk_to_next < 256u) {
        midi(o, 0xF8, 0, 0);
        s->clk_to_next += six_q8(s) / 6u;           /* 24 ppqn */
    }
    /* the smooth things: bends and lock ramps, each chunk */
    for (t = 0; t < NSEQT; t++) {
        bend_tick(s, t, k, o);
        cap |= s->t[t].bend_on;
    }
    locks_tick(s, k, o);
    for (i = 0; i < NLANE; i++)
        cap |= s->lrt[i].ramp;
    punch_update(s, o);
    s->chunk_cap = (uint8_t)cap;
    if (!s->playing && !s->ext)
        s->clk_to_next = 0;
}

void seq_start(seq_t *s, int from_start)
{
    if (from_start)
        s->req_start = 1;
    else
        s->req_cont = 1;
}
void seq_stop(seq_t *s) { s->req_stop = 1; }
void seq_continue(seq_t *s) { s->req_cont = 1; }
void seq_ext_clock(seq_t *s) { s->ext_pending++; }

void seq_cue_scene(seq_t *s, int scene)
{
    if (scene < 0 || scene >= NSCENE)
        return;
    if (s->playing)
        s->cue = (uint8_t)scene;
    else
        s->req_scene = (uint8_t)scene;
}

void seq_set_scene(seq_t *s, int scene)
{
    if (scene >= 0 && scene < NSCENE)
        s->req_scene = (uint8_t)scene;
}

void seq_cue_pattern(seq_t *s, int trk, int pat)
{
    if (trk < 0 || trk >= NSEQT || pat < 0 || pat >= NPATT)
        return;
    if (s->playing)
        s->pcue[trk] = (uint8_t)pat;
    else {
        s->ppat[trk] = (uint8_t)pat;
        if (trk == 0)
            bar_len_update(s);
    }
}

void seq_key(seq_t *s, int trk, int note, int on, int vel) { key_queue(s, KQ_KEY, trk, note, on, vel); }
void seq_key_all_off(seq_t *s, int trk) { key_queue(s, KQ_ALLOFF, trk, 0, 0, 0); }
void seq_brain_key(seq_t *s, int note, int on) { key_queue(s, KQ_BRAIN, AUX_BRAIN, note, on, 0); }
void seq_tape_key(seq_t *s, int seg, int on) { key_queue(s, KQ_TAPE, AUX_TAPE, seg & 15, on, 0); }

void seq_maestro_record(seq_t *s, int trk, const uint8_t *notes, int n)
{
    player_t *pl;
    int i;
    if (trk < 0 || trk >= NTRK)
        return;
    pl = &s->player[trk];
    if (n > SN * 2)
        n = SN * 2;
    pl->nchord = 0;
    for (i = 0; i < n; i++)
        pl->chord[i] = notes[i];
    pl->nchord = (uint8_t)n;
}

void seq_punch_key(seq_t *s, int fx, int on, int target_trk)
{
    if (fx < 0 || fx >= NPUNCH_FX)
        return;
    if (on) {
        s->punch_keys |= 1u << fx;
        s->punch_target = (uint8_t)(target_trk >= 0 && target_trk < NTRK ? target_trk : 0xFF);
    } else
        s->punch_keys &= ~(1u << fx);
}

void seq_rec(seq_t *s, int on) { s->rec = (uint8_t)(on != 0); }

int seq_rec_pos(const seq_t *s, int trk)
{
    const seq_trk_t *tr;
    const tpat_t *tp;
    uint32_t L;
    if (trk < 0 || trk >= NSEQT)
        return 0;
    tr = &s->t[trk];
    tp = tpat(s, trk);
    if (!s->playing || !tr->started)
        return 0;
    L = tr->step_q8 ? tr->step_q8 : seq_step_q8(s, trk);
    return tr->age > L / 2u ? (tr->pos + 1) % pat_len(tp) : tr->pos;
}

void seq_rec_knob(seq_t *s, int trk, int param, int val)
{
    int k;
    if (trk < 0 || trk >= NSEQT)
        return;
    k = lane_alloc(s->P->lane, trk, s->ppat[trk], param);
    if (k < 0)
        return;
    s->P->lane[k].val[seq_rec_pos(s, trk)] = (uint8_t)(val > 254 ? 254 : val < 0 ? 0 : val);
}

int seq_step_rec(seq_t *s, int trk, int step, int note, int vel)
{
    step_t *st;
    int i;
    if (trk < 0 || trk >= NSEQT || step < 0 || step >= NSTEP || note < 1 || note > 127)
        return 0;
    st = &tpat(s, trk)->st[step];
    if (step_empty(st)) {
        st->note[0] = (uint8_t)note;
        st->vel = (uint8_t)(vel < 1 ? 1 : vel > 127 ? 127 : vel);
        st->len = 0;
        return 1;
    }
    for (i = 0; i < SN; i++)
        if (st->note[i] == note)
            return 0;
    for (i = 0; i < SN; i++)
        if (!st->note[i]) {
            st->note[i] = (uint8_t)note;
            return 1;
        }
    return 0;
}
