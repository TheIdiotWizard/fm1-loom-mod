/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 sequencer; see seq.h for the model.
 *
 * Time. Every timer is a count of samples left in Q8 (a 24.8 fixed point: the fraction keeps the
 * tempo exact across steps, X0X's trick), counted down by seq_advance and due when under 256. NEVER
 * means no event. The sixteenth timer is the master: it drives the metronome, the count-in, the bar
 * (scene cues, the song) and the punch fills; the tracks run their own step timers from the same
 * tempo, so they stay in phase with it. A track's step fires on its grid plus an offset (the groove
 * and the step's nudge); the offset is folded into the time to the next step, as X0X folds swing,
 * so no event needs a look-ahead.
 *
 * Keys. The main loop's keys (seq_key and friends, the punch keys too) go into a small ring and are
 * played inside the interrupt at the next seq_advance, so the players, the brain, the punch state
 * and the recording all run in one context and the sink is only ever called from the interrupt.
 *
 * Components. A step's two components both run; the three skips gate the trigger, the step's locks or
 * the step's other component by the pass count. The components in force on a step are resolved once
 * when the step is reached (tr->act) and reused by its pulse repeats and ratchets.
 *
 * Locks. A lane's value at a step is sent before the step's notes; it holds until the lane's next
 * locked step. With shape > 0, the value moves toward the next locked step's value across the gap:
 * flat for (100 - shape) % of the gap, then a straight line. A pattern start does not restore the
 * knob: if step 1 has no lock, the last value keeps holding. The knob gets its lane back (LOCK_NONE)
 * when the track leaves the pattern, and when the transport stops.
 *
 * Grooves. The project's type (seq.groove_type) picks a timing table (ticks a sixteenth) and a
 * velocity table over the bar's sixteenths; a pattern's signed amount scales them (a negative amount
 * mirrors the timing: early instead of late, narrow instead of wide). The random types roll each hit
 * within their table's range.
 *
 * Throws (punch-in FX) that change the sequence run here (mute, stutter, repeat, echo, the fills, octave,
 * chop and ring); the engine gets the union of held and sequenced FX through the sink (and seq_punch_mask per
 * track) and does the rest (wobble, pan). */
#include "seq.h"

#define NEVER 0xFFFFFFFFu
#define TAG_KEY 0x100u                /* live keys: TAG_KEY | note */
#define TAG_ARP 0x200u
#define TAG_PLAYER 0x300u             /* maestro / hold: TAG_PLAYER | note */
#define TAG_FILL 0x400u               /* punch fills: TAG_FILL | note */
#define TAG_STEP (1u << 16)            /* steps: TAG_STEP | step * 16 + note index + ratchet * 1024 */
#define FILL_VEL 100
#define RAMP_MAX 8                    /* lock lanes ramping at once, per track */
#define ECHO_DECAY 179                /* the echo loop's velocity each pass, Q8 (x 0.7) */

/* a track scale in half sixteenths: TS_HALF .. TS_16 */
static const uint8_t TS_HALVES[NTS] = {1, 2, 4, 6, 8, 10, 12, 16, 32};
static const uint8_t SC_VEL[9] = {4, 8, 16, 32, 64, 100, 112, 127, 0};
static const uint8_t GM_DRUM[NDRUM] = {36, 38, 42, 46, 39, 45, 50, 63, 70, 67, 75};
/* grooves: for each type a timing offset in ticks (PPS = 24 a sixteenth, late when positive) and a velocity
 * offset for each sixteenth of the bar (0-based: 0 = beat 1, 2 = its "and", 4 = beat 2 ...), at the full amount
 * (99). For the types in GROOVE_RND_T / GROOVE_RND_V the table entry is a range: the hit rolls a value in -x..x.
 * Swing at +50 puts the upbeats a quarter sixteenth late (the old swing 75 %); at +99 half a sixteenth. */
static const int8_t GROOVE_T[NGROOVE][16] = {
    {0, 12, 0, 12, 0, 12, 0, 12, 0, 12, 0, 12, 0, 12, 0, 12},     /* swing: every upbeat */
    {0, 6, 0, 4, 0, 6, 0, 4, 0, 6, 0, 4, 0, 6, 0, 4},             /* lilt: lighter */
    {0, 0, 8, 0, 0, 0, -4, 0, 0, 0, 8, 0, 0, 0, -4, 0},           /* weave: the "and" of 1 and 3 late, of 2 and 4 early */
    {0, 0, 0, 0, -2, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0},            /* stretch: 2 a touch early, 4 later: an uneven lean */
    {4, 6, 4, 6, 4, 6, 4, 6, 4, 6, 4, 6, 4, 6, 4, 6},             /* drunk: random, every beat */
    {2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3},             /* jitter: a subtle random wobble */
    {0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1},             /* accent: subtle off grid */
    {0, 1, 0, 1, 2, 1, 0, 1, 0, 1, 0, 1, 2, 1, 0, 1},             /* lazy: 2 and 4 laid back */
    {0, -2, 5, -2, 0, -2, 5, -2, 0, -2, 5, -2, 0, -2, 5, -2},     /* push pull: the "e" pushed, the "and" pulled */
    {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5},             /* sway: random push and pull */
    {0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4},             /* swell: a subtle swing */
};
static const int8_t GROOVE_V[NGROOVE][16] = {
    {0},                                                          /* swing: velocity unchanged */
    {4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4},             /* lilt: variable */
    {0, 0, 6, 0, 0, 0, -6, 0, 0, 0, 6, 0, 0, 0, -6, 0},           /* weave: the late ones over, the early ones under */
    {0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0},           /* stretch: 2 and 4 louder */
    {20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20},   /* drunk: aggressive random */
    {24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24},   /* jitter: a wide random range */
    {20, -6, -6, -6, 10, -6, -6, -6, 20, -6, -6, -6, 10, -6, -6, -6},   /* accent: the key beats */
    {-10, -16, -10, -16, -10, -16, -10, -16, -10, -16, -10, -16, -10, -16, -10, -16},   /* lazy: low */
    {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16},   /* push pull: high random variation */
    {8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},             /* sway: some random variation */
    {-20, -7, 7, 20, -20, -7, 7, 20, -20, -7, 7, 20, -20, -7, 7, 20},   /* swell: ramps across the beat */
};
#define GROOVE_RND_T ((1u << GR_DRUNK) | (1u << GR_JITTER) | (1u << GR_SWAY))
#define GROOVE_RND_V ((1u << GR_LILT) | (1u << GR_DRUNK) | (1u << GR_JITTER) | (1u << GR_PUSHPULL) | (1u << GR_SWAY))
static const int8_t SIN32[32] = {0, 25, 49, 71, 90, 106, 117, 125, 127, 125, 117, 106, 90, 71, 49, 25,
                                 0, -25, -49, -71, -90, -106, -117, -125, -127, -125, -117, -106, -90, -71, -49, -25};
/* the arp's speeds: the period in sixteenths as NUM / DEN */
static const uint8_t ARP_NUM[9] = {4, 8, 2, 4, 1, 2, 1, 1, 1}, ARP_DEN[9] = {1, 3, 1, 3, 1, 3, 2, 3, 4};

const char *const PUNCH_NAME[NPUNCH] = {"mute", "stutter", "repeat", "echo", "build", "floor", "jumble",
                                        "warble", "pan", "octave", "chop", "ring"};
/* an octave's keys in chromatic order (F F# G G# A A# B C C# D D# E) to their effect */
static const uint8_t KEY2PX[12] = {PX_MUTE, PX_WARBLE, PX_STUTTER, PX_PAN, PX_REPEAT, PX_OCTAVE, PX_ECHO,
                                   PX_BUILD, PX_CHOP, PX_FLOOR, PX_RING, PX_JUMBLE};
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

int punch_of_key(int key)
{
    if (key < 0 || key >= 24)
        return -1;
    return KEY2PX[key % 12] + (key >= 12 ? PX_MEL : 0);
}

int punch_key_of(int fx)
{
    int k;
    if (!PX_VALID(fx))
        return -1;
    for (k = 0; k < 12; k++)
        if (KEY2PX[k] == (fx & 15))
            return k + (fx >= PX_MEL ? 12 : 0);
    return -1;
}

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
static uint16_t scale_mask(int scale) { return BRAIN_SCALE[scale >= 0 && scale < NBS ? scale : BS_CHROMATIC]; }
static int in_scale(int note, int key, uint16_t mask) { return mask >> (((note - key) % 12 + 12) % 12) & 1u; }

int scale_step(int note, int key, int scale, int steps)
{
    uint16_t mask = scale_mask(scale);
    key = ((key % 12) + 12) % 12;
    return clamp_note(deg_to_note(note_to_deg(note, key, mask) + steps, key, mask));
}

int scale_snap(int note, int key, int scale) { return scale_step(note, key, scale, 0); }

int brain_shift(int note, int key, int scale, int degree, int mode)
{
    uint16_t mask;
    int d0;
    if (mode == 0 || degree == 0)
        return note;
    if (mode == 1)
        return scale_step(note, key, scale, degree);
    mask = scale_mask(scale);
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
static int ts_halves(const tpat_t *p) { return TS_HALVES[p->scale < NTS ? p->scale : TS_1]; }

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

/* ----------------------------------------------------------- punch-in --- */
int punch_group_of(const seq_t *s, int trk) { return is_drum(s, trk) ? 0 : 1; }
static int grp_oct(const seq_t *s, int t) { return is_drum(s, t) ? 0 : PX_MEL; }

/* a punch-in effect in force on track t: sequenced ones reach the whole group, held ones the group or
 * the track they are aimed at */
static int fx_on(const seq_t *s, int t, int px)
{
    int bit;
    uint32_t b;
    if (t >= NTRK)
        return 0;
    bit = px + grp_oct(s, t);
    b = 1u << bit;
    if (s->punch_seq & b)
        return 1;
    if (!(s->punch_keys & b))
        return 0;
    return s->punch_tgt[bit] == 0xFF || s->punch_tgt[bit] == t;
}

uint32_t seq_punch_mask(const seq_t *s, int trk)
{
    uint32_t m = 0;
    int px;
    if (trk < 0 || trk >= NTRK)
        return 0;
    for (px = 0; px < NPUNCH; px++)
        if (fx_on(s, trk, px))
            m |= 1u << px;
    return m;
}

static int punch_muted(const seq_t *s, int t) { return fx_on(s, t, PX_MUTE); }

/* the sixteenth of the bar a step of track t falls on (its scale), or -1 between sixteenths */
static int step_six(const tpat_t *p, int step)
{
    int h = ts_halves(p) * step;
    return (h & 1) ? -1 : (h >> 1) & 15;
}

/* ------------------------------------------------------------- grooves --- */
static int groove_amount(const tpat_t *tp)
{
    int a = tp->groove;
    return a < -99 ? -99 : a > 99 ? 99 : a;
}

static int groove_roll(seq_t *s, int t, int range)   /* -range .. range */
{
    if (range <= 0)
        return 0;
    return (int)(rng_next(&s->t[t].rng) % (uint32_t)(2 * range + 1)) - range;
}

/* the step's timing offset, Q8: the groove's ticks on its sixteenth scaled by the pattern's amount, plus its nudge */
static int32_t step_off(seq_t *s, int t, int p, uint32_t L)
{
    const tpat_t *tp = tpat(s, t);
    const step_t *st = &tp->st[p];
    int32_t off = (int32_t)(L / PPS) * st->nudge;
    int six, amt, g = s->groove_type < NGROOVE ? s->groove_type : 0, tk;
    if (ts_halves(tp) == 1)
        off /= 2;                     /* a half-sixteenth step: a full nudge would swallow it */
    six = step_six(tp, p);
    amt = groove_amount(tp);
    if (six < 0 || !amt)
        return off;
    tk = GROOVE_T[g][six];
    if (GROOVE_RND_T >> g & 1u)
        tk = groove_roll(s, t, tk);
    off += (int32_t)(six_q8(s) / PPS) * tk * amt / 99;
    return off;
}

static int groove_vel(seq_t *s, int t, int p, int vel)
{
    const tpat_t *tp = tpat(s, t);
    int six = step_six(tp, p), amt = groove_amount(tp), g = s->groove_type < NGROOVE ? s->groove_type : 0, v;
    if (six >= 0 && amt) {
        v = GROOVE_V[g][six];
        if (GROOVE_RND_V >> g & 1u)
            v = groove_roll(s, t, v);
        vel += v * (amt < 0 ? -amt : amt) / 99;
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
    case OFF_DRUM:
        if (o->drum_off)
            o->drum_off(o->ctx, t, (int)e->note - 1);
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

/* a routed track's note through the brain (steps and live keys alike) */
static int brain_note(const seq_t *s, int t, int note)
{
    int mode = t < NTRK ? s->par[t][TP_BRAIN] : 0;
    if (!mode)
        return note;
    return brain_shift(note, s->brain_key, s->brain_scale, brain_deg(s), mode);
}

/* the degree a chord root sets: its distance in scale degrees from the tonic in middle C's octave */
static void brain_set_root(seq_t *s, int root)
{
    uint16_t mask = scale_mask(s->brain_scale < NBS ? s->brain_scale : BS_MAJOR);
    int d = note_to_deg(root, s->brain_key, mask) - note_to_deg(60 + s->brain_key, s->brain_key, mask);
    d = d < -64 ? -64 : d > 63 ? 63 : d;
    s->brain_deg = (uint8_t)(int8_t)d;
}

/* the BRAIN track's knobs: manual mode takes key / scale from them when they change (a scene may have
 * set the sequencer's own); auto mode detects at the bar; E4 is the link track (0 off, 1..8) */
static void brain_follow_par(seq_t *s)
{
    const uint8_t *e = &s->par[AUX_BRAIN][TP_E1];
    uint16_t route = 0;
    int t;
    s->brain_mode = e[0];
    s->brain_link = e[3] <= NTRK ? e[3] : 0;
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

/* the tonality component on a note (tv = the option 0..9; 0 and 1 are the brain's business, see fire_notes) */
static int tonal(seq_t *s, int t, int note, int tv)
{
    int key = s->brain_key, sc = s->brain_scale < NBS ? s->brain_scale : BS_MAJOR, n;
    uint16_t mask = scale_mask(sc);
    switch (tv) {
    case 2: return note + 12;                                           /* octave up */
    case 3: return in_scale(note + 7, key, mask) ? note + 7 : scale_snap(note + 7, key, sc);   /* fifth up */
    case 4:                                                             /* third up: 4 or 3 by the scale */
        if (in_scale(note + 4, key, mask))
            return note + 4;
        if (in_scale(note + 3, key, mask))
            return note + 3;
        return scale_snap(note + 4, key, sc);
    case 5: return note + 1;
    case 6: return note - 1;
    case 7: case 8: case 9:                                             /* quantize 33 / 66 / 100 % */
        n = tv == 9 ? 100 : tv == 8 ? 66 : 33;
        if ((int)(rng_next(&s->t[t].rng) % 100u) < n)
            return scale_snap(note, key, sc);
        return note;
    default: return note;
    }
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

/* a drum hit, through the drum punch-in FX (pitch, short, long); len_q8 > 0 schedules its drum_off (the gated and
 * loop play modes end with it; a hit of the same sound under the same tag moves the off), 0 = the key holds it */
static void emit_drum(seq_t *s, int t, int sound, int vel, uint32_t tag, uint32_t len_q8, const seq_sink_t *o)
{
    if (sound < 0 || sound >= NDRUM)
        return;
    if (o->drum)
        o->drum(o->ctx, t, sound, vel, fx_on(s, t, PX_OCTAVE) ? 12 : 0, fx_on(s, t, PX_CHOP), fx_on(s, t, PX_RING));
    if (s->send_notes && s->par[t][TP_CHAN]) {
        midi_on(s, t, GM_DRUM[sound], vel, o);
        midi_off(s, t, GM_DRUM[sound], o);
    }
    if (len_q8)
        add_off(s, t, tag | (uint32_t)sound, OFF_DRUM, sound + 1, len_q8, o);
}

/* the echo loop's velocity */
static int echo_vel(const seq_t *s, int t, int vel)
{
    const seq_trk_t *tr = &s->t[t];
    if (tr->loop_on && fx_on(s, t, PX_ECHO)) {
        vel = vel * tr->echo_gain >> 8;
        if (vel < 1)
            vel = 1;
    }
    return vel;
}

/* ------------------------------------------------------------ the step --- */
/* the step's notes (synth) or hits (drums), with the components in tr->act; ratchet = the hit's index */
static void fire_notes(seq_t *s, int t, const step_t *st, int p, uint32_t L, int ratchet, const seq_sink_t *o)
{
    seq_trk_t *tr = &s->t[t];
    const tpat_t *tp = tpat(s, t);
    const uint8_t *act = tr->act;
    int vel = groove_vel(s, t, p, st->vel ? st->vel : 100), i, glide = 0, n = 0, tv = (int)act[SC_TONALITY] - 1;
    uint32_t len_q8;
    if (act[SC_VELOCITY]) {
        int k = act[SC_VELOCITY] - 1;
        if (k == SC_RANDOM_VAL)
            k = (int)(rng_next(&tr->rng) % 9u);
        vel = SC_VEL[k < 9 ? k : 8];
        if (!vel)
            return;                   /* velocity 0: a silent step */
    }
    vel = echo_vel(s, t, vel);
    if (st->len)
        len_q8 = L / 4u * st->len;
    else
        len_q8 = L / 100u * (tp->nlen > 100 ? 100u : tp->nlen);
    if (is_drum(s, t)) {
        if (act[SC_MULTIPLY] && tr->ratchets > 1 && len_q8 > L / tr->ratchets)
            len_q8 = L / tr->ratchets;
        for (i = 0; i < SN; i++)
            if (st->note[i] >= 1 && st->note[i] <= NDRUM && st->note[i] != tr->skip_note)
                emit_drum(s, t, st->note[i] - 1, vel, TAG_STEP | (uint32_t)(ratchet * 1024), len_q8, o);
        return;
    }
    if (fx_on(s, t, PX_CHOP))
        len_q8 = L / 4u;
    else if (fx_on(s, t, PX_RING))
        len_q8 = L * 2u;
    if (act[SC_HOLD])
        len_q8 = len_q8 > L * (tr->stall + 1u) ? len_q8 : L * (tr->stall + 1u);
    if (act[SC_MULTIPLY] && tr->ratchets > 1) {
        uint32_t sp = L / tr->ratchets;
        if (len_q8 > sp)
            len_q8 = sp;
    }
    if (act[SC_PORTAMENTO]) {
        int pv = act[SC_PORTAMENTO] - 1;
        if (pv == SC_RANDOM_VAL)
            pv = (int)(rng_next(&tr->rng) % 8u);
        if (pv < 8)                   /* 8 = off */
            glide = (int)((L >> 8) * (uint32_t)(pv * 10 + 10) / 4410u);   /* ms: samples * pct / 100 / 44.1 */
    }
    for (i = 0; i < SN; i++) {
        int note = st->note[i];
        uint32_t tag;
        if (!note || note == tr->skip_note)
            continue;
        if (tr->ramp_deg)
            note = scale_step(note, s->brain_key, s->brain_scale, tr->ramp_deg);
        if (tv == 1)                  /* transpose only: chromatic by the brain's degree (a routed track) */
            note = s->par[t][TP_BRAIN] ? brain_shift(note, s->brain_key, s->brain_scale, brain_deg(s), 2) : note;
        else if (tv != 0)             /* 0 = ignore the progression: this step sits out the brain */
            note = brain_note(s, t, note);
        if (tv >= 2)
            note = tonal(s, t, note, tv);
        if (s->par[t][TP_BRAIN])
            brain_hear(s, note);
        if (fx_on(s, t, PX_OCTAVE))
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

/* the aux tracks' steps: brain chords, punch-in bits, tape segments */
static void fire_aux(seq_t *s, int t, const step_t *st, int p, uint32_t L, const seq_sink_t *o)
{
    const tpat_t *tp = tpat(s, t);
    uint32_t len_q8 = st->len ? L / 4u * st->len : L / 100u * (tp->nlen > 100 ? 100u : tp->nlen);
    int i;
    (void)p;
    if (t == AUX_BRAIN) {             /* the chord's lowest note is its root */
        int root = 0;
        for (i = 0; i < SN; i++)
            if (st->note[i] && (!root || st->note[i] < root))
                root = st->note[i];
        if (root)
            brain_set_root(s, root);
        return;
    }
    for (i = 0; i < SN; i++) {
        int v = (int)st->note[i] - 1;
        if (v < 0)
            continue;
        if (t == AUX_PUNCH && PX_VALID(v)) {
            s->punch_seq |= 1u << v;
            add_off(s, t, (uint32_t)v, OFF_PUNCH, 0, len_q8, o);
        } else if (t == AUX_TAPE && v < 16) {
            if (o->tape_seg)
                o->tape_seg(o->ctx, v, 1);
            add_off(s, t, (uint32_t)v, OFF_TAPE, 0, len_q8, o);
        }
    }
}

/* the step's condition: does it play on this pass? */
static int cond_plays(seq_t *s, int t, const step_t *st)
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
    return 1;
}

/* a skip component's verdict for this pass: a cycle of n passes (SK_N 9 = a random length each pass); mode 0
 * plays on the cycle's last pass, 1 skips the last pass and plays the others, 2 plays only the first */
static int skip_plays(seq_t *s, int t, int v)
{
    seq_trk_t *tr = &s->t[t];
    int n = SK_N(v), k;
    n = n >= 9 ? 1 + (int)(rng_next(&tr->rng) % 9u) : n + 1;
    k = (int)(tr->pass % (uint32_t)n);
    switch (SK_MODE(v)) {
    case 1: return k != n - 1;
    case 2: return k == 0;
    default: return k == n - 1;
    }
}

/* the components in force on a fresh step: tr->act[SC_x] = value + 1; the skips are applied here (SC_SKIP_COMP
 * gates the step's other component), their own verdicts come back as flags */
static void resolve_comps(seq_t *s, int t, const step_t *st, int *trig_ok, int *lock_ok)
{
    seq_trk_t *tr = &s->t[t];
    int k, c[2], v[2], other_ok[2] = {1, 1};
    c[0] = st->comp;
    v[0] = st->cval;
    c[1] = st->comp2;
    v[1] = st->cval2;
    *trig_ok = *lock_ok = 1;
    for (k = 0; k < NSC; k++)
        tr->act[k] = 0;
    for (k = 0; k < 2; k++) {
        if (c[k] == SC_SKIP)
            *trig_ok &= skip_plays(s, t, v[k]);
        else if (c[k] == SC_SKIP_LOCK)
            *lock_ok &= skip_plays(s, t, v[k]);
        else if (c[k] == SC_SKIP_COMP)
            other_ok[1 - k] &= skip_plays(s, t, v[k]);
    }
    for (k = 0; k < 2; k++)
        if (c[k] > SC_NONE && c[k] < SC_SKIP_LOCK && other_ok[k])
            tr->act[c[k]] = (uint8_t)(v[k] + 1);
}

/* ramps and random: the degree offset for this pass. N = 2..6 iterations spread evenly over an octave (values
 * 0..4) or three octaves (5..8) of the brain's scale; the ramp resets after N passes, random picks one of the
 * N positions */
static int ramp_degree(seq_t *s, int t, int cv, int kind)
{
    seq_trk_t *tr = &s->t[t];
    int n, span, i;
    if (cv == SC_RANDOM_VAL)
        cv = (int)(rng_next(&tr->rng) % 9u);
    if (cv > 8)
        cv = 8;
    n = cv % 5 + 2;
    span = mask_len(scale_mask(s->brain_scale < NBS ? s->brain_scale : BS_MAJOR)) * (cv < 5 ? 1 : 3);
    if (kind == SC_RANDOM)
        i = (int)(rng_next(&tr->rng) % (uint32_t)n);
    else
        i = (int)(tr->pass % (uint32_t)n);
    i = i * span / n;
    return kind == SC_RAMP_DOWN ? -i : i;
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
            int num = (int)((r->age - r->hold) >> 4), den = (int)((r->len - r->hold) >> 4);   /* samples >> 4: the product stays in an int */
            int v = (int)r->from + ((int)r->to - (int)r->from) * num / (den > 0 ? den : 1);
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
static void bend_start(seq_t *s, int t, int sh, uint32_t L)
{
    seq_trk_t *tr = &s->t[t];
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
    uint32_t span = (uint32_t)pat_len(p) * (uint32_t)ts_halves(p) / 2u;
    s->bar_len = span < 16u ? 16u : span;
}

static int ext_tps(const seq_t *s, int t)   /* external clock ticks a step of track t */
{
    return 3 * ts_halves(tpat(s, t));
}

static void ext_arm(seq_t *s, int t) { s->t[t].ext_ticks = (uint8_t)(ext_tps(s, t) - 1); }   /* the next tick fires */

/* now, in half-sixteenths since the start, Q8: the clock every track's phase is counted on */
static uint32_t now_h8(const seq_t *s)
{
    uint32_t six = six_q8(s) >> 9, frac = 0;            /* (six: samples a 1/512 of a sixteenth, Q8) */
    if (s->six_to_next != NEVER && six)
        frac = s->six_to_next / six;
    if (frac > 512u)
        frac = 512u;
    return (s->step_tick << 9) - frac;
}
/* half-sixteenths (Q8) to samples (Q8), without overflowing */
static uint32_t h8_to_q8(const seq_t *s, uint32_t h8)
{
    uint32_t six = six_q8(s), hi = six >> 9, lo = six & 511u;
    return h8 * hi + (h8 * lo >> 9);
}
static void origins_zero(seq_t *s)                       /* the transport (re)starts: every track's step 1 is now */
{
    for (int t = 0; t < NSEQT; t++)
        s->t[t].origin_h8 = 0;
}

/* the scale or the length changed under a running track: put it where it would be had it always run so since
 * its step 1. On that grid now: that step plays now; between two of its steps: the next one, on time. */
static void resync(seq_t *s, int t)
{
    seq_trk_t *tr = &s->t[t];
    const tpat_t *tp = tpat(s, t);
    uint32_t h = (uint32_t)ts_halves(tp), len = (uint32_t)pat_len(tp), d = h << 8, e, g, rem, k;
    if (!tr->started || !tr->grid_h || (tr->grid_h == h && tr->grid_len == len))
        return;
    tr->grid_h = (uint8_t)h;
    tr->grid_len = (uint8_t)len;
    if (s->ext || tr->to_next == NEVER)
        return;
    e = now_h8(s) - tr->origin_h8;
    g = e / d;
    rem = e - g * d;
    k = rem < 16u ? g : g + 1u;              /* (just past one of its steps, by a block or so: that step, now) */
    tr->stall = 0;
    tr->loop_on = 0;
    tr->off_q8 = 0;
    tr->jump_to = (uint8_t)(k % len);
    {   /* timed from the next sixteenth, whose time is exact (a track's step 1 is always on a sixteenth) */
        uint32_t target = tr->origin_h8 + k * d, b6 = s->step_tick << 9, half = six_q8(s) >> 1;
        if (target >= b6)
            tr->to_next = s->six_to_next + h8_to_q8(s, target - b6);
        else
            tr->to_next = s->six_to_next > half ? s->six_to_next - half : 0u;   /* the half before it */
        if (rem < 16u)
            tr->to_next = 0;
    }
}

static void track_reset(seq_t *s, int t)
{
    seq_trk_t *tr = &s->t[t];
    int k;
    tr->started = 0;
    tr->pos = 0;
    tr->grid_h = tr->grid_len = 0;
    tr->origin_h8 = now_h8(s);
    tr->pass = 0;
    tr->stall = 0;
    tr->ratchet = tr->ratchets = 0;
    tr->jump_to = 0xFF;
    tr->loop_on = 0;
    tr->ramp_deg = 0;
    tr->skip_note = 0;
    tr->sub_to_next = NEVER;
    ext_arm(s, t);                    /* the first clock is the downbeat */
    tr->off_q8 = 0;
    tr->age = 0;
    for (k = 0; k < NSC; k++)
        tr->act[k] = 0;
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
/* the next step: a jump, the punch loop's span, or the next one; wrapped = a new pass begins (the
 * pattern's end, or the loop going back to its start) */
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
    if (t < NTRK && tr->loop_on) {
        int from = tr->loop_from % len, end = from + (tr->loop_span ? tr->loop_span : 1);
        n = p + 1;
        if (n >= len || n >= end || n <= from) {
            n = from;
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

static void jump_set(seq_t *s, int t, int cv, int p)
{
    seq_trk_t *tr = &s->t[t];
    int len = pat_len(tpat(s, t)), n;
    if (cv == SC_RANDOM_VAL)
        cv = (int)(rng_next(&tr->rng) % 9u);
    switch (cv) {
    case 0: case 1: case 2: case 3: n = cv * 4; break;
    case 4: n = p + 2; break;                                 /* one forward: skip the next */
    case 5: n = p - 1; break;                                 /* one back */
    case 6: n = (rng_next(&tr->rng) & 1u) ? p + 2 : p - 1; break;
    case 7: n = p; break;                                     /* stay */
    case 8: {                                                 /* align: where the bar says the track would be */
        int h = ts_halves(tpat(s, t));
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
    int p, wrapped = 0, retrig = 1, plays, np, dummy, trig_ok = 1, lock_ok = 1, kind;
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
        if (wrapped) {
            tr->pass++;
            if (tr->loop_on && fx_on(s, t, PX_ECHO))
                tr->echo_gain = (uint16_t)(tr->echo_gain * ECHO_DECAY >> 8);
        }
    }
    tp = tpat(s, t);
    if (s->rec && s->playing && !s->count_in && s->rec_replace && t == s->rec_target &&
        !(s->rec_written[t][p >> 5] >> (p & 31) & 1u)) {
        step_t *cl = &s->P->pat[t][s->ppat[t] % NPATT].st[p];   /* replace: what the playhead passes goes, but not what was just played */
        for (int j = 0; j < SN; j++)
            cl->note[j] = 0;
    }
    s->rec_written[t][p >> 5] &= ~(1u << (p & 31));
    st = &tp->st[p];
    tr->pos = (uint8_t)p;
    tr->grid_h = (uint8_t)ts_halves(tp);
    tr->grid_len = (uint8_t)pat_len(tp);
    tr->age = 0;
    L = seq_step_q8(s, t);
    tr->step_q8 = L;
    tr->ratchet = 0;
    tr->ratchets = 1;
    tr->sub_to_next = NEVER;
    if (retrig)
        resolve_comps(s, t, st, &trig_ok, &lock_ok);
    if (retrig && lock_ok)
        apply_locks(s, t, p, L, o);
    if (o->step)
        o->step(o->ctx, t, p);
    plays = retrig && !(s->mute >> t & 1u) && !punch_muted(s, t) && !tr->fill_on && !step_empty(st) && trig_ok &&
            cond_plays(s, t, st);
    if (retrig && tr->act[SC_JUMP])   /* a jump works on an empty step too */
        jump_set(s, t, tr->act[SC_JUMP] - 1, p);
    kind = tr->act[SC_PULSE] ? SC_PULSE : tr->act[SC_HOLD] ? SC_HOLD : 0;
    if (plays) {
        if (kind && !tr->stall && tr->stall_kind != kind) {   /* a fresh step: start the stall */
            int cv = tr->act[kind] - 1, n = cv == SC_RANDOM_VAL ? 1 + (int)(rng_next(&tr->rng) % 9u) : cv + 1;
            tr->stall = (uint8_t)n;
            tr->stall_kind = (uint8_t)kind;
        }
        if (tr->act[SC_MULTIPLY]) {
            int cv = tr->act[SC_MULTIPLY] - 1;
            tr->ratchets = (uint8_t)(cv == SC_RANDOM_VAL ? 1 + (int)(rng_next(&tr->rng) % 9u) : cv + 1);
            if (tr->ratchets > 1)
                tr->sub_to_next = carry + L / tr->ratchets;
        }
        if (tr->act[SC_RAMP_UP])
            tr->ramp_deg = (int16_t)ramp_degree(s, t, tr->act[SC_RAMP_UP] - 1, SC_RAMP_UP);
        else if (tr->act[SC_RAMP_DOWN])
            tr->ramp_deg = (int16_t)ramp_degree(s, t, tr->act[SC_RAMP_DOWN] - 1, SC_RAMP_DOWN);
        else if (tr->act[SC_RANDOM])
            tr->ramp_deg = (int16_t)ramp_degree(s, t, tr->act[SC_RANDOM] - 1, SC_RANDOM);
        else
            tr->ramp_deg = 0;
        if (tr->act[SC_BEND])
            bend_start(s, t, tr->act[SC_BEND] - 1, L);
        if (t < NTRK)
            fire_notes(s, t, st, p, L, 0, o);
        else
            fire_aux(s, t, st, p, L, o);
    } else
        tr->ramp_deg = 0;
    tr->skip_note = 0;
    if (!tr->stall)
        tr->stall_kind = 0;
    /* the next step: its grid plus its offset, less this step's */
    off = tr->off_q8;
    if (tr->stall)
        np = p;
    else {
        uint8_t save_j = tr->jump_to;
        np = next_pos(s, t, &dummy);  /* a peek: undo what it may have changed */
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
    if (t < NTRK && tr->act[SC_MULTIPLY])
        fire_notes(s, t, st, tr->pos, tr->step_q8, tr->ratchet, o);
    tr->sub_to_next = tr->ratchet + 1 < tr->ratchets ? carry + tr->step_q8 / tr->ratchets : NEVER;
}

/* ----------------------------------------------------------- the fills --- */
/* a synth track's fill notes: every distinct note of its pattern, sorted (up to 16) */
static void fill_collect(seq_t *s, int t)
{
    seq_trk_t *tr = &s->t[t];
    const tpat_t *tp = tpat(s, t);
    int len = pat_len(tp), p, i, j, k;
    tr->nfill = 0;
    for (p = 0; p < len; p++)
        for (i = 0; i < SN; i++) {
            int n = tp->st[p].note[i];
            if (!n)
                continue;
            for (j = 0; j < tr->nfill && tr->fill_notes[j] < n; j++)
                ;
            if (j < tr->nfill && tr->fill_notes[j] == n)
                continue;
            if (tr->nfill >= 16)
                continue;
            for (k = tr->nfill; k > j; k--)
                tr->fill_notes[k] = tr->fill_notes[k - 1];
            tr->fill_notes[j] = (uint8_t)n;
            tr->nfill++;
        }
}

/* the punch fills on this sixteenth of the bar, for every track in a fill */
static void fire_fills(seq_t *s, const seq_sink_t *o)
{
    int t, b = s->bar16 & 15;
    for (t = 0; t < NTRK; t++) {
        seq_trk_t *tr = &s->t[t];
        uint32_t r;
        int note, vel;
        if (!tr->fill_on || (s->mute >> t & 1u) || punch_muted(s, t))
            continue;
        if (is_drum(s, t)) {
            uint32_t half = six_q8(s) / 2u;
            if (fx_on(s, t, PX_BUILD)) {              /* snares: eighths, then sixteenths */
                if (b >= 8 || !(b & 1))
                    emit_drum(s, t, 1, echo_vel(s, t, b >= 8 ? 110 : 100), TAG_FILL, half, o);
            } else if (fx_on(s, t, PX_FLOOR)) {       /* a kick on the beats, hats in sixteenths */
                if (!(b & 3))
                    emit_drum(s, t, 0, echo_vel(s, t, 110), TAG_FILL, half, o);
                emit_drum(s, t, 2, echo_vel(s, t, 90), TAG_FILL, half, o);
            } else {
                r = rng_next(&tr->rng);
                if (r % 4u)
                    emit_drum(s, t, (int)(r >> 2 & 0xFFu) % 3, echo_vel(s, t, 70 + (int)(r >> 10) % 41), TAG_FILL,
                              half, o);
            }
            continue;
        }
        if (!tr->nfill)
            continue;
        vel = FILL_VEL;
        if (fx_on(s, t, PX_BUILD)) {                  /* climbing */
            note = tr->fill_notes[tr->fill_i++ % tr->nfill];
        } else if (fx_on(s, t, PX_FLOOR)) {           /* low, high, next low, next high ... */
            int i = tr->fill_i++ % tr->nfill;
            note = (i & 1) ? tr->fill_notes[tr->nfill - 1 - i / 2] : tr->fill_notes[i / 2];
        } else {
            r = rng_next(&tr->rng);
            if (!(r % 4u))
                continue;
            note = tr->fill_notes[(r >> 2) % tr->nfill];
            vel = 70 + (int)(r >> 10) % 41;
        }
        note = scale_snap(brain_note(s, t, note), s->brain_key, s->brain_scale);
        if (fx_on(s, t, PX_OCTAVE))
            note -= 12;
        note = clamp_note(note);
        emit_note(s, t, note, echo_vel(s, t, vel), 0, TAG_FILL | (uint32_t)note, six_q8(s) / 2u, o);
    }
}

/* ---------------------------------------------------------- the players --- */
static void player_note_on(seq_t *s, int t, int note, int vel, int glide_ms, uint32_t tag, const seq_sink_t *o)
{
    if (o->note_on)
        o->note_on(o->ctx, t, note, vel, glide_ms, tag);
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

static void arp_pan(seq_t *s, int t, int pan, const seq_sink_t *o)
{
    player_t *pl = &s->player[t];
    if (pan == pl->pan_sent)
        return;
    pl->pan_sent = (int8_t)pan;
    if (o->pan)
        o->pan(o->ctx, t, pan);
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
    pl->arp_side = 0;
    pl->nheld = 0;
    pl->latched = 0;
    arp_pan(s, t, 0, o);
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
    int sp = s->par[t][TP_P1];
    if (sp > 8)
        sp = 8;
    return six_q8(s) / ARP_DEN[sp] * ARP_NUM[sp];
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

/* the arp's base cycle: position k of the pattern's order over `total` notes (up, down, the two up-downs, as
 * played) */
static int arp_base(int pat, int k, int total)
{
    switch (pat) {
    case 1: return total - 1 - k;                                  /* down */
    case 2: return k < total ? k : 2 * total - 2 - k;              /* up-down 1: the ends once */
    case 3: return k < total ? k : 2 * total - 1 - k;              /* up-down 2: the ends repeated */
    default: return k;                                             /* up, as played */
    }
}

static int arp_cycle(int pat, int total)
{
    if (pat == 2)
        return total <= 1 ? 1 : 2 * total - 2;
    if (pat == 3)
        return 2 * total;
    return total;
}

/* the styles (TP_AP2, "a pattern within a pattern"): where in the base cycle the j-th note comes from.
 *   A: 1 2 1 3 1 4 ..  (the first note between every other)
 *   B: 1 3 2 4 3 5 ..  (pairs a third apart, climbing)
 *   C: 1 1 2 2 3 3 ..  (each note twice)
 *   D: 1 3 5 .. 2 4 6  (every other, then the ones skipped)
 *   E: 2 1 4 3 6 5 ..  (pairs reversed) */
static int arp_style_pos(int style, uint32_t j, int cyc)
{
    int k = (int)(j % (uint32_t)cyc), h;
    if (cyc <= 1)
        return 0;
    switch (style) {
    case 1: return (j & 1u) ? 1 + (int)((j >> 1) % (uint32_t)(cyc - 1)) : 0;
    case 2: return (int)(((j >> 1) + (j & 1u) * 2u) % (uint32_t)cyc);
    case 3: return (int)((j >> 1) % (uint32_t)cyc);
    case 4:
        h = (cyc + 1) / 2;
        return k < h ? 2 * k : 2 * (k - h) + 1;
    case 5: return (k ^ 1) < cyc ? k ^ 1 : k;
    default: return k;
    }
}

static void arp_fire(seq_t *s, int t, const seq_sink_t *o)
{
    player_t *pl = &s->player[t];
    uint32_t P = arp_period(s, t), carry = pl->arp_to_next < 256u ? pl->arp_to_next : 0, gate;
    uint8_t list[16];
    int n = pl->nheld, i, j, oct = s->par[t][TP_P3], range, pat = s->par[t][TP_P2], idx, note, total, cyc, style,
        len = s->par[t][TP_AP1], glide = s->par[t][TP_AP3], stereo = s->par[t][TP_AP4];
    arp_off(s, t, o);
    if (!n) {
        pl->arp_to_next = NEVER;
        return;
    }
    range = oct < 1 ? 1 : oct > 4 ? 4 : oct;
    for (i = 0; i < n; i++)           /* sorted, unless as played */
        list[i] = pl->held[i];
    if (pat != 4)
        for (i = 1; i < n; i++) {
            uint8_t v = list[i];
            for (j = i; j > 0 && list[j - 1] > v; j--)
                list[j] = list[j - 1];
            list[j] = v;
        }
    total = n * range;
    style = s->par[t][TP_AP2] > 5 ? 0 : s->par[t][TP_AP2];
    if (pat == 5)                     /* random: the style has nothing to reorder */
        idx = (int)(rng_next(&s->rng) % (uint32_t)total);
    else {
        cyc = arp_cycle(pat, total);
        idx = arp_base(pat, arp_style_pos(style, pl->arp_i, cyc), total);
        pl->arp_i++;
    }
    note = list[idx % n] + 12 * (idx / n);
    note = clamp_note(brain_note(s, t, note));
    if (stereo) {                     /* ping-pong: narrow +-20, wide +-60 */
        arp_pan(s, t, (pl->arp_side ? -1 : 1) * (stereo >= 2 ? 60 : 20), o);
        pl->arp_side ^= 1;
    } else
        arp_pan(s, t, 0, o);
    player_note_on(s, t, note, pl->arp_vel ? pl->arp_vel : 100, glide >= 2 ? 120 : glide == 1 ? 30 : 0,
                   TAG_ARP | (uint32_t)note, o);
    pl->arp_note = (uint8_t)note;
    gate = len >= 2 ? P / 10u * 9u : len == 1 ? P / 2u : P / 4u;   /* long 90 %, medium 50 %, short 25 % */
    pl->arp_to_off = carry + gate;
    pl->arp_to_next = carry + P;
}

/* the maestro: the chord moved so its lowest note is the key, in musical order (or reversed, or alternating,
 * or a random direction per strum), rolled: roll 63 = a note per sequencer step of the track, 0 = at once */
static void maestro_play(seq_t *s, int t, int key, int vel, const seq_sink_t *o)
{
    player_t *pl = &s->player[t];
    int n = pl->nchord, i, j, lo = 128, pat = s->par[t][TP_P2], roll = s->par[t][TP_P1], down;
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
    down = pat == 1 || (pat == 2 && !(pl->arp_dir ^= 1)) || (pat == 3 && (rng_next(&s->rng) & 1u));   /* up-down: up first */
    if (down)
        for (i = 0; i < n / 2; i++) {
            uint8_t v = list[i];
            list[i] = list[n - 1 - i];
            list[n - 1 - i] = v;
        }
    for (i = 0; i < n; i++)
        pl->roll[i] = list[i];
    pl->nroll = (uint8_t)n;
    pl->roll_i = 0;
    pl->roll_vel = (uint8_t)vel;
    pl->maestro_root = (uint8_t)key;
    if (!roll) {
        for (i = 0; i < n; i++) {
            player_note_on(s, t, list[i], vel, 0, TAG_PLAYER | list[i], o);
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
    if (roll > 63)
        roll = 63;
    if (pl->roll_i >= pl->nroll) {
        pl->roll_to_next = NEVER;
        pl->nroll = 0;
        return;
    }
    note = pl->roll[pl->roll_i++];
    player_note_on(s, t, note, pl->roll_vel, 0, TAG_PLAYER | (uint32_t)note, o);
    sounding_add(pl, note);
    if (pl->roll_i >= pl->nroll) {
        pl->roll_to_next = NEVER;
        pl->nroll = 0;
    } else                            /* roll / 63 of the track's step between the notes */
        pl->roll_to_next = carry + seq_step_q8(s, t) / 63u * (uint32_t)roll;
}

static void phys_set(uint32_t *phys, uint8_t *nphys, int note, int on)
{
    uint32_t b = 1u << (note & 31);
    int had = (phys[note >> 5] & b) != 0;
    if (on && !had) {
        phys[note >> 5] |= b;
        if (nphys)
            (*nphys)++;
    } else if (!on && had) {
        phys[note >> 5] &= ~b;
        if (nphys)
            (*nphys)--;
    }
}

/* ----------------------------------------------------------- recording --- */
/* live recording: the key into the track's pattern at the quantised step */
static void rec_key_on(seq_t *s, int t, int note, int vel)
{
    tpat_t *tp = tpat(s, t);
    seq_trk_t *tr = &s->t[t];
    uint32_t L = tr->step_q8 ? tr->step_q8 : seq_step_q8(s, t);
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
    if (!(s->rec_written[t][p >> 5] >> (p & 31) & 1u) && (t == AUX_BRAIN || (s->rec_replace && t == s->rec_target))) {
        for (i = 0; i < SN; i++)      /* a new chord on the conductor (or replace): the step's old notes go */
            st->note[i] = 0;
    }
    s->rec_written[t][p >> 5] |= 1u << (p & 31);
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
            uint32_t L = s->t[t].step_q8 ? s->t[t].step_q8 : seq_step_q8(s, t);
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

static void tracks_go(seq_t *s)
{
    int t;
    for (t = 0; t < NSEQT; t++)
        s->t[t].to_next = s->ext ? NEVER : 0;
}

/* the first key after REC + PLAY: the transport starts now, with the key on step 1 of its track (the step
 * fires right after and skips this note: the key itself is its sound) */
static void rec_go(seq_t *s, int t, int note, int vel, const seq_sink_t *o)
{
    tpat_t *tp = tpat(s, t);
    step_t *st = &tp->st[0];
    int i;
    s->rec_wait = 0;
    tracks_go(s);
    for (i = 0; i < NSEQT; i++)
        ext_arm(s, i);
    s->six_to_next = s->ext ? NEVER : 0;
    s->bar_six = 0;
    s->bar16 = 0;
    s->bar_fresh = 1;
    s->step_tick = 0;
    origins_zero(s);
    if (step_empty(st)) {
        st->note[0] = (uint8_t)note;
        st->vel = (uint8_t)vel;
        st->len = 0;
        st->nudge = 0;
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
            s->rec_step[i] = 0;
            s->rec_age[i] = 0;
            break;
        }
    s->t[t].skip_note = (uint8_t)note;
    if (!s->ext && s->send_clock)
        midi(o, 0xFA, 0, 0);
}

/* a key on an instrument track, inside the interrupt; raw = not through the brain (the link track) */
static void key_in(seq_t *s, int t, int note, int on, int vel, int raw, const seq_sink_t *o)
{
    player_t *pl;
    int player, had_phys, shifted;
    if (t < 0 || t >= NTRK || note < 1 || note > 127)
        return;
    pl = &s->player[t];
    player = s->par[t][TP_PLAYER];
    had_phys = pl->nphys;
    if (on && s->rec && s->playing && !s->count_in) {
        if (s->rec_wait)
            rec_go(s, t, note, vel, o);
        else
            rec_key_on(s, t, note, vel);
    } else if (!on)
        rec_key_off(s, t, note);      /* (a no-op unless the key was recorded) */
    if (is_drum(s, t)) {              /* a sound is held while its key is down */
        if (note > NDRUM)
            return;
        if (on) {
            pl->drums_down |= (uint16_t)(1u << (note - 1));
            emit_drum(s, t, note - 1, vel, TAG_KEY, 0, o);
        } else if (pl->drums_down >> (note - 1) & 1u) {
            pl->drums_down &= (uint16_t)~(1u << (note - 1));
            if (o->drum_off)
                o->drum_off(o->ctx, t, note - 1);
        }
        return;
    }
    phys_set(pl->phys, &pl->nphys, note, on);
    shifted = raw ? note : clamp_note(brain_note(s, t, note));
    switch (player) {
    case PL_ARP:
        if (on) {
            if (!had_phys && pl->latched) {   /* a new phrase replaces the latched one */
                pl->nheld = 0;
                pl->latched = 0;
            }
            held_add(pl, note);
            pl->arp_vel = (uint8_t)vel;
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
    case PL_STRUM:
        if (on)
            maestro_play(s, t, shifted, vel, o);
        else if (!s->par[t][TP_P4] && pl->maestro_root == shifted && !pl->nphys)
            sounding_off(s, t, o);
        break;
    case PL_HOLD:
        if (on) {
            if (!had_phys)
                sounding_off(s, t, o);
            player_note_on(s, t, shifted, vel, 0, TAG_PLAYER | (uint32_t)shifted, o);
            sounding_add(pl, shifted);
        }
        break;
    default:
        if (on)
            player_note_on(s, t, shifted, vel, 0, TAG_KEY | (uint32_t)note, o);
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
    for (i = 0; i < NDRUM; i++)       /* a drum track's held sounds let go */
        if (pl->drums_down >> i & 1u && o->drum_off)
            o->drum_off(o->ctx, t, i);
    pl->drums_down = 0;
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

/* a key on the BRAIN track: the lowest key held is the chord's root; the link track plays the key as pressed */
static void brain_key_in(seq_t *s, int note, int on, const seq_sink_t *o)
{
    int w, i, root = 0;
    if (note < 1 || note > 127)
        return;
    phys_set(s->brain_phys, 0, note, on);
    if (s->rec && s->playing && !s->count_in && !s->rec_wait) {   /* live record: the chords go on its steps */
        if (on)
            rec_key_on(s, AUX_BRAIN, note, 100);
        else
            rec_key_off(s, AUX_BRAIN, note);
    }
    if (on) {
        for (w = 0; w < 4 && !root; w++)
            for (i = 0; i < 32; i++)
                if (s->brain_phys[w] >> i & 1u) {
                    root = w * 32 + i;
                    break;
                }
        brain_set_root(s, root ? root : note);
    }
    if (s->brain_link >= 1 && s->brain_link <= NTRK)
        key_in(s, s->brain_link - 1, note, on, 100, 1, o);
}

/* a punch key: the bit moved to its target's octave; recorded into the PUNCH track while rec is on */
static void punch_in(seq_t *s, int fx, int on, int tgt)
{
    if (tgt >= 0 && tgt < NTRK)
        fx = (fx & 15) + grp_oct(s, tgt);
    if (!PX_VALID(fx))
        return;
    if (on) {
        s->punch_keys |= 1u << fx;
        s->punch_tgt[fx] = (uint8_t)(tgt >= 0 && tgt < NTRK ? tgt : 0xFF);
        if (s->rec && s->playing && !s->count_in && !s->rec_wait)
            rec_key_on(s, AUX_PUNCH, 1 + fx, 100);
    } else {
        s->punch_keys &= ~(1u << fx);
        rec_key_off(s, AUX_PUNCH, 1 + fx);
    }
}

/* the main loop's keys, played here */
enum { KQ_KEY, KQ_ALLOFF, KQ_BRAIN, KQ_TAPE, KQ_PUNCH };
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
        case KQ_KEY: key_in(s, trk, note, on, vel ? vel : 1, 0, o); break;
        case KQ_ALLOFF: if (trk < NTRK) keys_all_off(s, trk, o); break;
        case KQ_BRAIN: brain_key_in(s, note, on, o); break;
        case KQ_TAPE: if (o->tape_seg) o->tape_seg(o->ctx, note, on); break;
        case KQ_PUNCH: punch_in(s, note, on, trk == 15 ? -1 : trk); break;
        default: break;
        }
    }
}

/* ------------------------------------------------------------ transport --- */
/* the punch state: the engine's masks, and the loops and fills starting or ending on each track */
static void punch_update(seq_t *s, const seq_sink_t *o)
{
    uint32_t u = s->punch_keys | s->punch_seq, d = u & 0xFFFu, m = u >> 16 & 0xFFFu;
    int t;
    if (d != s->punch_sent_d || m != s->punch_sent_m) {
        s->punch_sent_d = d;
        s->punch_sent_m = m;
        if (o->punch)
            o->punch(o->ctx, d, m);
    }
    for (t = 0; t < NTRK; t++) {
        seq_trk_t *tr = &s->t[t];
        int lp = fx_on(s, t, PX_STUTTER) || fx_on(s, t, PX_REPEAT) || fx_on(s, t, PX_ECHO);
        int fl = fx_on(s, t, PX_BUILD) || fx_on(s, t, PX_FLOOR) || fx_on(s, t, PX_JUMBLE);
        if (lp) {
            int six = fx_on(s, t, PX_REPEAT) && !fx_on(s, t, PX_STUTTER) && !fx_on(s, t, PX_ECHO) ? 4 : 2, steps;
            if (!tr->loop_on) {       /* the punch-in point: the step playing */
                tr->loop_on = 1;
                tr->loop_from = tr->started ? tr->pos : 0;
                tr->echo_gain = 256;
            }
            steps = six * 2 / ts_halves(tpat(s, t));
            tr->loop_span = (uint8_t)(steps < 1 ? 1 : steps);
        } else
            tr->loop_on = 0;
        if (fl) {
            int kind = fx_on(s, t, PX_BUILD) ? 1 : fx_on(s, t, PX_FLOOR) ? 2 : 3;
            if (!tr->fill_on && !is_drum(s, t))
                fill_collect(s, t);
            if (!tr->fill_on || kind != tr->fill_kind)
                tr->fill_i = 0;
            tr->fill_on = 1;
            tr->fill_kind = (uint8_t)kind;
        } else
            tr->fill_on = 0;
    }
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
        origins_zero(s);
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
    if (!s->rec) {
        s->count_in = 0;
        s->rec_wait = 0;
    }
    if (s->count_in || s->rec_wait) {   /* nothing moves until the count-in is over / the first key comes */
        for (t = 0; t < NSEQT; t++)
            s->t[t].to_next = NEVER;
        if (s->rec_wait)
            s->six_to_next = NEVER;
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
    s->rec_wait = 0;
    for (t = 0; t < NSEQT; t++) {
        seq_trk_t *tr = &s->t[t];
        cancel_offs(s, t, o);
        tr->to_next = NEVER;
        tr->sub_to_next = NEVER;
        tr->stall = 0;
        tr->loop_on = 0;
        tr->fill_on = 0;
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

/* the sixteenth: the metronome, the count-in, the bar, the fills */
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
        origins_zero(s);
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
    fire_fills(s, o);
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
        if (!s->playing || s->rec_wait)
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
    s->rng = 0x2545F491u;
    for (i = 0; i < NPUNCH_FX; i++)
        s->punch_tgt[i] = 0xFF;
    for (t = 0; t < NSEQT; t++) {
        seq_trk_t *tr = &s->t[t];
        tr->rng = 0x9E3779B9u * (uint32_t)(t + 1);
        tr->to_next = NEVER;
        tr->sub_to_next = NEVER;
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
        if (s->playing && !s->rec_wait) {
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
    punch_update(s, o);               /* the keys just played may have started a loop or a fill */
    if (o->tape_step && s->playing) {
        uint32_t six = six_q8(s) >> 8;
        if (six != s->tape_last) {
            s->tape_last = six;
            o->tape_step(o->ctx, six);
        }
    }
    if (s->playing)                   /* a scale or length changed: back in phase */
        for (t = 0; t < NSEQT; t++)
            resync(s, t);
    /* move time */
    for (t = 0; t < NSEQT; t++) {
        seq_trk_t *tr = &s->t[t];
        dec(&tr->to_next, dq);
        dec(&tr->sub_to_next, dq);
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
            player_t *pl = &s->player[t];
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

void seq_punch_key(seq_t *s, int fx, int on, int target_trk)
{
    if (!PX_VALID(fx))
        return;
    key_queue(s, KQ_PUNCH, target_trk >= 0 && target_trk < NTRK ? target_trk : 15, fx, on, 0);
}

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

void seq_rec(seq_t *s, int on)
{
    s->rec = (uint8_t)(on != 0);
    s->rec_wait = (uint8_t)(on == 2 && !s->playing);
}

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

int seq_step_add_notes(seq_t *s, int trk, int step, const uint8_t *notes, int n, int vel)
{
    step_t *st;
    int i, k = 0;
    if (trk < 0 || trk >= NSEQT || step < 0 || step >= NSTEP)
        return 0;
    st = &tpat(s, trk)->st[step];
    for (i = 0; i < n; i++)
        step_toggle_note(st, notes[i]);
    if (vel > 0 && !step_empty(st))
        st->vel = (uint8_t)(vel > 127 ? 127 : vel);
    for (i = 0; i < SN; i++)
        k += st->note[i] != 0;
    return k;
}
