/* SPDX-License-Identifier: GPL-3.0-only */
/* XY-1 sequencer tests: a recording sink logs every event with its sample time; each test builds a
 * pattern, runs the sequencer the way the engine does (seq_until_event / seq_advance) and checks the
 * log. Prints "ok name" per test; exits 1 on the first failure. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "seq.h"

enum { E_ON, E_OFF, E_DRUM, E_LOCK, E_BEND, E_PUNCH, E_SEG, E_TSTEP, E_MIDI, E_STEP, E_SCENE, E_METRO };
typedef struct {
    int kind;
    uint32_t t;
    int trk, a, b, c;
    uint32_t tag;
} ev_t;
#define NEV 20000
static ev_t ev[NEV];
static int nev;
static uint32_t now;
static patterns_t P;
static uint8_t par[NALLTRK][NTP];
static seq_t S;
static const char *cur = "";

static void log_ev(int kind, int trk, int a, int b, int c, uint32_t tag)
{
    if (nev < NEV) {
        ev_t *e = &ev[nev++];
        e->kind = kind;
        e->t = now;
        e->trk = trk;
        e->a = a;
        e->b = b;
        e->c = c;
        e->tag = tag;
    }
}
static void s_on(void *x, int trk, int note, int vel, int glide, uint32_t tag) { (void)x; log_ev(E_ON, trk, note, vel, glide, tag); }
static void s_off(void *x, int trk, uint32_t tag) { (void)x; log_ev(E_OFF, trk, 0, 0, 0, tag); }
static void s_drum(void *x, int trk, int sound, int vel, int pitch, int sh, int lg)
{
    (void)x;
    log_ev(E_DRUM, trk, sound, vel, pitch, (uint32_t)(sh | lg << 1));
}
static void s_lock(void *x, int trk, int param, int val) { (void)x; log_ev(E_LOCK, trk, param, val, 0, 0); }
static void s_bend(void *x, int trk, int cents) { (void)x; log_ev(E_BEND, trk, cents, 0, 0, 0); }
static void s_punch(void *x, uint32_t d, uint32_t m) { (void)x; log_ev(E_PUNCH, -1, (int)d, (int)m, 0, 0); }
static void s_seg(void *x, int seg, int on) { (void)x; log_ev(E_SEG, AUX_TAPE, seg, on, 0, 0); }
static void s_tstep(void *x, uint32_t sps) { (void)x; log_ev(E_TSTEP, AUX_TAPE, (int)sps, 0, 0, 0); }
static void s_midi(void *x, uint8_t st, uint8_t d1, uint8_t d2) { (void)x; log_ev(E_MIDI, -1, st, d1, d2, 0); }
static void s_step(void *x, int trk, int step) { (void)x; log_ev(E_STEP, trk, step, 0, 0, 0); }
static void s_scene(void *x, int sc) { (void)x; log_ev(E_SCENE, -1, sc, 0, 0, 0); }
static void s_metro(void *x, int acc) { (void)x; log_ev(E_METRO, -1, acc, 0, 0, 0); }
static const seq_sink_t sink = {s_on, s_off, s_drum, s_lock, s_bend, s_punch, s_seg, s_tstep, s_midi, s_step,
                                s_scene, s_metro, 0};

static void fail(const char *what)
{
    int i;
    printf("FAIL %s: %s\n", cur, what);
    for (i = 0; i < nev && i < 400; i++)
        if (ev[i].kind != E_STEP)
        printf("  %6u kind %d trk %d a %d b %d c %d tag %x\n", ev[i].t, ev[i].kind, ev[i].trk, ev[i].a, ev[i].b, ev[i].c,
               ev[i].tag);
    exit(1);
}
#define CHECK(c) do { if (!(c)) fail(#c); } while (0)

static void reset(void)
{
    int t;
    patterns_init(&P);
    memset(par, 0, sizeof par);
    for (t = 0; t < NTRK; t++)
        par[t][TP_ENGINE] = EG_SIMPLE;
    seq_init(&S, &P, par);
    S.send_clock = 0;
    nev = 0;
    now = 0;
}

static void run(uint32_t n)
{
    while (n) {
        uint32_t k = seq_until_event(&S, n);
        now += k;
        seq_advance(&S, k, &sink);
        n -= k;
    }
}
static void run_to(uint32_t t) { run(t > now ? t - now : 0); }
static void start(void)
{
    now = 0;
    seq_start(&S, 1);
    seq_advance(&S, 0, &sink);
}
static void stop(void)
{
    seq_stop(&S);
    seq_advance(&S, 0, &sink);
}
#define L16 5512u                     /* a sixteenth at 120 BPM: 5512.5 samples */
static int near(uint32_t t, uint32_t want, uint32_t tol) { return t + tol >= want && t <= want + tol; }
static step_t *st(int trk, int step) { return &P.pat[trk][0].st[step]; }
static void note(int trk, int step, int n, int vel)
{
    st(trk, step)->note[0] = (uint8_t)n;
    st(trk, step)->vel = (uint8_t)vel;
}
static int count(int kind, int trk)
{
    int i, n = 0;
    for (i = 0; i < nev; i++)
        n += ev[i].kind == kind && (trk < 0 || ev[i].trk == trk);
    return n;
}
static ev_t *nth(int kind, int trk, int k)
{
    int i;
    for (i = 0; i < nev; i++)
        if (ev[i].kind == kind && (trk < 0 || ev[i].trk == trk) && k-- == 0)
            return &ev[i];
    return 0;
}
static ev_t *on_n(int k) { return nth(E_ON, 0, k); }
static int in_major(int n) { return (0x0AB5 >> (n % 12)) & 1; }

/* ------------------------------------------------------------- the tests --- */
static void t_timing(void)
{
    cur = "timing";
    reset();
    note(0, 0, 60, 100);
    note(0, 1, 62, 100);
    start();
    run(3 * L16);
    CHECK(on_n(0) && on_n(0)->t == 0 && on_n(0)->a == 60);
    CHECK(on_n(1) && near(on_n(1)->t, L16, 1) && on_n(1)->a == 62);
    CHECK(nth(E_STEP, 0, 0)->t == 0 && nth(E_STEP, 0, 1)->a == 1);
    /* the next pass: step 1 again at 16 sixteenths, exact to the sample (Q8 carries) */
    run(14 * L16 + 40);
    CHECK(on_n(2) && near(on_n(2)->t, 16 * 5512 + 8, 1));
    printf("ok %s\n", cur);
}

static void t_scale(void)
{
    cur = "scale";
    reset();
    P.pat[0][0].scale = TS_2;
    note(0, 0, 60, 100);
    note(0, 1, 62, 100);
    start();
    run(3 * L16);
    CHECK(on_n(1) && near(on_n(1)->t, 2 * L16 + 1, 1));
    reset();
    P.pat[0][0].scale = TS_HALF;
    note(0, 0, 60, 100);
    note(0, 1, 62, 100);
    start();
    run(L16);
    CHECK(on_n(1) && near(on_n(1)->t, L16 / 2, 1));
    printf("ok %s\n", cur);
}

static void t_swing(void)
{
    cur = "swing";
    reset();
    P.pat[0][0].swing = 100;
    note(0, 0, 60, 100);
    note(0, 1, 62, 100);
    note(0, 2, 64, 100);
    start();
    run(3 * L16);
    CHECK(near(on_n(1)->t, L16 + L16 / 2, 2));
    CHECK(near(on_n(2)->t, 2 * L16 + 1, 1));
    /* a groove table: bombora pushes the fifth sixteenth 2 ticks early and accents it */
    reset();
    P.pat[0][0].groove = GR_BOMBORA;
    note(0, 4, 60, 100);
    start();
    run(5 * L16);
    CHECK(near(on_n(0)->t, 4 * L16 + 2 - 2 * (L16 / 24), 2) && on_n(0)->b == 108);
    /* a nudge: +12 ticks = half a step late */
    reset();
    note(0, 1, 60, 100);
    st(0, 1)->nudge = 12;
    start();
    run(2 * L16);
    CHECK(near(on_n(0)->t, L16 + L16 / 2, 2));
    printf("ok %s\n", cur);
}

static void t_length(void)
{
    cur = "length";
    reset();
    note(0, 0, 60, 100);
    note(0, 4, 62, 100);
    st(0, 4)->len = 2;
    note(0, 8, 64, 100);
    st(0, 8)->len = 8;
    start();
    run(12 * L16);
    CHECK(nth(E_OFF, 0, 0) && near(nth(E_OFF, 0, 0)->t, L16, 1));                  /* the pattern's: a step */
    CHECK(near(nth(E_OFF, 0, 1)->t, 4 * L16 + L16 / 2, 2));                        /* half a step */
    CHECK(near(nth(E_OFF, 0, 2)->t, 10 * L16 + 5, 2) && nth(E_OFF, 0, 2)->tag == on_n(2)->tag);   /* two steps */
    printf("ok %s\n", cur);
}

static void t_chord(void)
{
    cur = "chord";
    reset();
    st(0, 0)->note[0] = 60;
    st(0, 0)->note[1] = 64;
    st(0, 0)->note[2] = 67;
    st(0, 0)->note[3] = 71;
    st(0, 0)->vel = 90;
    start();
    run(2 * L16);
    CHECK(count(E_ON, 0) == 4 && count(E_OFF, 0) == 4);
    CHECK(on_n(3)->a == 71 && on_n(3)->b == 90 && on_n(0)->tag != on_n(3)->tag);
    printf("ok %s\n", cur);
}

static void t_pulse(void)
{
    cur = "pulse";
    reset();
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_PULSE;
    st(0, 0)->cval = 1;               /* two repeats: the step plays three times */
    note(0, 1, 62, 100);
    start();
    run(4 * L16);
    CHECK(count(E_ON, 0) == 4);
    CHECK(on_n(1)->a == 60 && near(on_n(1)->t, L16, 1));
    CHECK(on_n(2)->a == 60 && near(on_n(2)->t, 2 * L16 + 1, 1));
    CHECK(on_n(3)->a == 62 && near(on_n(3)->t, 3 * L16 + 1, 2));
    printf("ok %s\n", cur);
}

static void t_hold(void)
{
    cur = "hold";
    reset();
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_HOLD;
    st(0, 0)->cval = 1;               /* sounds three steps */
    note(0, 1, 62, 100);
    start();
    run(4 * L16);
    CHECK(count(E_ON, 0) == 2);
    CHECK(near(nth(E_OFF, 0, 0)->t, 3 * L16 + 1, 2));
    CHECK(on_n(1)->a == 62 && near(on_n(1)->t, 3 * L16 + 1, 2));
    printf("ok %s\n", cur);
}

static void t_multiply(void)
{
    cur = "multiply";
    reset();
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_MULTIPLY;
    st(0, 0)->cval = 3;               /* four hits */
    note(0, 1, 62, 100);
    start();
    run(2 * L16);
    CHECK(count(E_ON, 0) == 5 && count(E_OFF, 0) >= 4);
    CHECK(near(on_n(1)->t, L16 / 4, 2) && near(on_n(2)->t, L16 / 2, 2) && near(on_n(3)->t, 3 * L16 / 4, 2));
    CHECK(on_n(0)->tag != on_n(1)->tag && on_n(4)->a == 62);
    printf("ok %s\n", cur);
}

static void t_velocity(void)
{
    cur = "velocity";
    reset();
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_VELOCITY;
    st(0, 0)->cval = 5;               /* 100 */
    note(0, 1, 60, 50);
    st(0, 1)->comp = SC_VELOCITY;
    st(0, 1)->cval = 0;               /* 4 */
    note(0, 2, 60, 50);
    st(0, 2)->comp = SC_VELOCITY;
    st(0, 2)->cval = 8;               /* 0: silent */
    start();
    run(4 * L16);
    CHECK(count(E_ON, 0) == 2 && on_n(0)->b == 100 && on_n(1)->b == 4);
    printf("ok %s\n", cur);
}

static void t_ramp(void)
{
    cur = "ramp";
    reset();
    P.pat[0][0].len = 1;
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_RAMP_UP;
    st(0, 0)->cval = 0;               /* two degrees a pass, over an octave */
    start();
    run(5 * L16);
    CHECK(on_n(0)->a == 60 && on_n(1)->a == 64 && on_n(2)->a == 67 && on_n(3)->a == 71 && on_n(4)->a == 62);
    reset();
    P.pat[0][0].len = 1;
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_RAMP_DOWN;
    st(0, 0)->cval = 0;
    start();
    run(3 * L16);
    CHECK(on_n(0)->a == 60 && on_n(1)->a == 57 && on_n(2)->a == 53);
    printf("ok %s\n", cur);
}

static void t_random(void)
{
    int i, spread = 0;
    cur = "random";
    reset();
    P.pat[0][0].len = 1;
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_RANDOM;
    st(0, 0)->cval = 0;               /* within an octave, in steps of two degrees */
    start();
    run(60 * L16);
    CHECK(count(E_ON, 0) == 60);
    for (i = 0; i < 60; i++) {
        CHECK(in_major(on_n(i)->a) && on_n(i)->a >= 60 && on_n(i)->a <= 72);
        spread |= on_n(i)->a != 60;
    }
    CHECK(spread);
    printf("ok %s\n", cur);
}

static void t_portamento(void)
{
    cur = "portamento";
    reset();
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_PORTAMENTO;
    st(0, 0)->cval = 4;               /* 50 % of a 125 ms step */
    start();
    run(L16);
    CHECK(on_n(0)->c >= 61 && on_n(0)->c <= 63);
    printf("ok %s\n", cur);
}

static void t_bend(void)
{
    int i, max = 0, min = 0, last = 1;
    cur = "bend";
    reset();
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_BEND;
    st(0, 0)->cval = 1;               /* up-down */
    start();
    run(2 * L16);
    for (i = 0; i < nev; i++)
        if (ev[i].kind == E_BEND) {
            if (ev[i].a > max)
                max = ev[i].a;
            last = ev[i].a;
        }
    CHECK(max >= 190 && last == 0 && count(E_BEND, 0) > 20);
    reset();
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_BEND;
    st(0, 0)->cval = 0;               /* down-up */
    start();
    run(2 * L16);
    for (i = 0; i < nev; i++)
        if (ev[i].kind == E_BEND && ev[i].a < min)
            min = ev[i].a;
    CHECK(min <= -190);
    printf("ok %s\n", cur);
}

static void t_jump(void)
{
    int i;
    cur = "jump";
    reset();
    for (i = 0; i < 16; i++)
        note(0, i, 60 + i, 100);
    st(0, 1)->comp = SC_JUMP;
    st(0, 1)->cval = 0;               /* to step 1 */
    start();
    run(4 * L16);
    CHECK(on_n(2)->a == 60 && on_n(3)->a == 61);
    st(0, 1)->cval = 3;               /* to step 13 */
    stop();
    nev = 0;
    start();
    run(4 * L16);
    CHECK(on_n(2)->a == 72 && on_n(3)->a == 73);
    st(0, 1)->cval = 5;               /* one back */
    stop();
    nev = 0;
    start();
    run(4 * L16);
    CHECK(on_n(2)->a == 60 && on_n(3)->a == 61);
    st(0, 1)->cval = 7;               /* stay */
    stop();
    nev = 0;
    start();
    run(4 * L16);
    CHECK(on_n(1)->a == 61 && on_n(2)->a == 61 && on_n(3)->a == 61);
    st(0, 1)->cval = 4;               /* one forward: skips step 3 */
    stop();
    nev = 0;
    start();
    run(4 * L16);
    CHECK(on_n(2)->a == 63 && on_n(3)->a == 64);
    printf("ok %s\n", cur);
}

static void t_skip(void)
{
    cur = "skip";
    reset();
    P.pat[0][0].len = 1;
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_SKIP;
    st(0, 0)->cval = 1;               /* every second pass */
    start();
    run(6 * L16);
    CHECK(count(E_ON, 0) == 3 && near(on_n(1)->t, 2 * L16 + 1, 1));
    st(0, 0)->cval = 2;               /* every third */
    stop();
    nev = 0;
    start();
    run(9 * L16);
    CHECK(count(E_ON, 0) == 3);
    printf("ok %s\n", cur);
}

static void t_cond(void)
{
    cur = "cond";
    reset();
    P.pat[0][0].len = 1;
    note(0, 0, 60, 100);
    st(0, 0)->cond = 0;
    start();
    run(100 * L16);
    CHECK(count(E_ON, 0) == 100);
    st(0, 0)->cond = 1;
    stop();
    nev = 0;
    start();
    run(200 * L16);
    CHECK(count(E_ON, 0) <= 12);
    st(0, 0)->cond = 99;
    stop();
    nev = 0;
    start();
    run(200 * L16);
    CHECK(count(E_ON, 0) >= 188);
    st(0, 0)->cond = 50;
    stop();
    nev = 0;
    start();
    run(400 * L16);
    CHECK(count(E_ON, 0) > 150 && count(E_ON, 0) < 250);
    st(0, 0)->cond = COND_AB + 0;     /* 1:2 */
    stop();
    nev = 0;
    start();
    run(6 * L16);
    CHECK(count(E_ON, 0) == 3 && on_n(1)->t > 2 * L16 - 2);
    st(0, 0)->cond = COND_AB + 1;     /* 2:2 */
    stop();
    nev = 0;
    start();
    run(6 * L16);
    CHECK(count(E_ON, 0) == 3 && near(on_n(0)->t, L16, 1));
    printf("ok %s\n", cur);
}

static void t_locks(void)
{
    int k, i, prev, seen = 0;
    cur = "locks";
    reset();
    note(0, 0, 60, 100);
    note(0, 2, 62, 100);
    k = lane_alloc(P.lane, 0, 0, TP_CUT);
    CHECK(k >= 0 && lane_find(P.lane, 0, 0, TP_CUT) == k && lane_count(P.lane) == 1);
    P.lane[k].val[0] = 10;
    P.lane[k].val[2] = 50;
    CHECK(step_has_lock(P.lane, 0, 0, 2) && !step_has_lock(P.lane, 0, 0, 1));
    start();
    run(4 * L16);
    CHECK(count(E_LOCK, 0) == 2);
    CHECK(nth(E_LOCK, 0, 0)->t == 0 && nth(E_LOCK, 0, 0)->a == TP_CUT && nth(E_LOCK, 0, 0)->b == 10);
    CHECK(nth(E_LOCK, 0, 0) < on_n(0));                                 /* the lock before the note */
    CHECK(near(nth(E_LOCK, 0, 1)->t, 2 * L16 + 1, 1) && nth(E_LOCK, 0, 1)->b == 50);
    /* the pattern wraps: 50 holds to the end (nothing restores the knob), then step 1's 10 again */
    run(14 * L16);
    CHECK(count(E_LOCK, 0) == 3 && nth(E_LOCK, 0, 2)->b == 10 && near(nth(E_LOCK, 0, 2)->t, 16 * L16 + 8, 1));
    /* shape: the value walks from 10 to 50 across the two steps */
    stop();
    CHECK(nth(E_LOCK, 0, 3) && nth(E_LOCK, 0, 3)->b == LOCK_NONE);     /* stop: the knob again */
    P.pat[0][0].shape = 100;
    nev = 0;
    start();
    run(2 * L16);
    prev = 10;
    for (i = 0; i < nev; i++)
        if (ev[i].kind == E_LOCK) {
            CHECK(ev[i].b >= prev && ev[i].b <= 50);
            prev = ev[i].b;
            seen++;
        }
    CHECK(seen > 10 && prev == 50);
    printf("ok %s\n", cur);
}

static void t_scene(void)
{
    cur = "scene";
    reset();
    note(0, 0, 60, 100);
    P.pat[0][1].st[0].note[0] = 72;
    P.pat[0][1].st[0].vel = 100;
    P.scene[1].used = 1;
    P.scene[1].pat[0] = 1;
    start();
    run(L16);
    seq_cue_scene(&S, 1);
    run(20 * L16);
    CHECK(count(E_SCENE, -1) == 1 && nth(E_SCENE, -1, 0)->a == 1 && near(nth(E_SCENE, -1, 0)->t, 16 * L16 + 8, 1));
    CHECK(on_n(1)->a == 72 && near(on_n(1)->t, 16 * L16 + 8, 1) && S.scene == 1 && S.ppat[0] == 1);
    /* at once */
    seq_set_scene(&S, 0);
    run(L16);
    CHECK(S.scene == 0 && S.ppat[0] == 0);
    printf("ok %s\n", cur);
}

static void t_song(void)
{
    cur = "song";
    reset();
    note(0, 0, 60, 100);
    P.scene[1].used = 1;
    P.scene[1].pat[0] = 1;
    P.pat[0][1].st[0].note[0] = 72;
    P.pat[0][1].st[0].vel = 100;
    P.song.len = 2;
    P.song.loop = 1;
    P.song.slot[0].scene = 0;
    P.song.slot[0].rep = 2;
    P.song.slot[1].scene = 1;
    P.song.slot[1].rep = 1;
    S.song_on = 1;
    start();
    run(49 * L16);
    CHECK(on_n(0)->a == 60 && on_n(1)->a == 60 && on_n(2)->a == 72 && on_n(3)->a == 60);
    CHECK(near(on_n(2)->t, 32 * L16 + 16, 1));
    printf("ok %s\n", cur);
}

static void t_brain(void)
{
    uint8_t h[12] = {0};
    int key = -1, scale = -1;
    cur = "brain";
    CHECK(brain_shift(60, 0, BS_MAJOR, 2, 1) == 64 && brain_shift(62, 0, BS_MAJOR, 2, 1) == 65 &&
          brain_shift(64, 0, BS_MAJOR, 2, 1) == 67);
    CHECK(brain_shift(60, 0, BS_MAJOR, 2, 2) == 64 && brain_shift(62, 0, BS_MAJOR, 2, 2) == 66);
    CHECK(brain_shift(60, 0, BS_MAJOR, -1, 1) == 59 && brain_shift(60, 0, BS_MAJOR, 7, 1) == 72);
    CHECK(brain_shift(61, 0, BS_MAJOR, 1, 1) == 62);          /* C# snaps to C first */
    CHECK(brain_shift(60, 0, BS_MAJOR, 2, 0) == 60);
    CHECK(scale_step(60, 0, BS_CHROMATIC, 3) == 63 && scale_step(60, 9, BS_MINOR, 1) == 62);
    h[0] = 12; h[2] = 5; h[4] = 8; h[5] = 4; h[7] = 9; h[9] = 4; h[11] = 3;
    CHECK(brain_detect(h, &key, &scale) == 1 && key == 0 && scale == BS_MAJOR);
    /* the brain in the sequence: the BRAIN track's key sets the degree, routed tracks follow */
    reset();
    note(0, 0, 60, 100);
    note(1, 0, 60, 100);
    par[0][TP_BRAIN] = 1;
    start();
    seq_brain_key(&S, 64, 1);         /* E over C major: +2 degrees */
    run(17 * L16);
    CHECK(on_n(0)->a == 60 && on_n(1)->a == 64);
    CHECK(nth(E_ON, 1, 1)->a == 60);  /* not routed */
    /* a brain step: degree -3 (note 57) at the bar */
    P.pat[AUX_BRAIN][0].st[0].note[0] = 57;
    P.pat[AUX_BRAIN][0].st[0].vel = 100;
    stop();
    nev = 0;
    start();
    run(L16);
    CHECK(on_n(0)->a == 55);
    /* a scene carries the brain's state */
    P.scene[1].used = 1;
    P.scene[1].brain_deg = (uint8_t)(int8_t)4;
    P.scene[1].brain_key = 0;
    P.scene[1].brain_scale = BS_MAJOR;
    P.pat[AUX_BRAIN][0].st[0].note[0] = 0;
    seq_set_scene(&S, 1);
    run(16 * L16);
    CHECK(on_n(1)->a == 67);
    printf("ok %s\n", cur);
}

static void t_arp(void)
{
    cur = "arp";
    reset();
    par[0][TP_PLAYER] = PL_ARP;
    par[0][TP_P1] = 3;                /* 1/16 */
    par[0][TP_P2] = 0;                /* up */
    par[0][TP_P3] = 1;
    seq_key(&S, 0, 60, 1, 100);
    seq_key(&S, 0, 64, 1, 100);
    seq_key(&S, 0, 67, 1, 100);
    run(4 * L16);
    CHECK(count(E_ON, 0) == 4 && on_n(0)->a == 60 && on_n(1)->a == 64 && on_n(2)->a == 67 && on_n(3)->a == 60);
    CHECK(near(on_n(1)->t, L16 + 1, 2) && near(on_n(2)->t, 2 * L16 + 1, 2));
    CHECK(nth(E_OFF, 0, 0) && near(nth(E_OFF, 0, 0)->t, L16 / 2 + 1, 2));
    seq_key(&S, 0, 60, 0, 0);
    seq_key(&S, 0, 64, 0, 0);
    seq_key(&S, 0, 67, 0, 0);
    run(2 * L16);
    CHECK(count(E_ON, 0) == 4 && count(E_OFF, 0) == 4);
    /* down, two octaves, 1/8 */
    nev = 0;
    par[0][TP_P1] = 1;
    par[0][TP_P2] = 1;
    par[0][TP_P3] = 2;
    seq_key(&S, 0, 60, 1, 100);
    seq_key(&S, 0, 64, 1, 100);
    run(8 * L16);
    CHECK(count(E_ON, 0) == 4 && on_n(0)->a == 76 && on_n(1)->a == 72 && on_n(2)->a == 64 && on_n(3)->a == 60);
    CHECK(near(on_n(1)->t - on_n(0)->t, 2 * L16 + 1, 2));
    /* hold: the keys may go */
    par[0][TP_P4] = 1;
    seq_key(&S, 0, 60, 0, 0);
    seq_key(&S, 0, 64, 0, 0);
    run(4 * L16);
    CHECK(count(E_ON, 0) == 6);
    /* up-down */
    nev = 0;
    par[0][TP_P4] = 0;
    par[0][TP_P2] = 2;
    par[0][TP_P3] = 1;
    par[0][TP_P1] = 3;
    seq_key_all_off(&S, 0);
    seq_key(&S, 0, 60, 1, 100);
    seq_key(&S, 0, 64, 1, 100);
    seq_key(&S, 0, 67, 1, 100);
    run(6 * L16);
    CHECK(on_n(0)->a == 60 && on_n(1)->a == 64 && on_n(2)->a == 67 && on_n(3)->a == 64 && on_n(4)->a == 60 &&
          on_n(5)->a == 64);
    /* while playing, the first note waits for the grid */
    reset();
    par[0][TP_PLAYER] = PL_ARP;
    par[0][TP_P1] = 3;
    par[0][TP_P3] = 1;
    start();
    run(L16 / 2);
    seq_key(&S, 0, 60, 1, 100);
    run(2 * L16);
    CHECK(on_n(0) && near(on_n(0)->t, L16, 2));
    printf("ok %s\n", cur);
}

static void t_maestro(void)
{
    static const uint8_t chord[3] = {60, 64, 67};
    cur = "maestro";
    reset();
    par[0][TP_PLAYER] = PL_MAESTRO;
    seq_maestro_record(&S, 0, chord, 3);
    seq_key(&S, 0, 62, 1, 90);
    run(10);
    CHECK(count(E_ON, 0) == 3 && on_n(0)->a == 62 && on_n(1)->a == 66 && on_n(2)->a == 69 && on_n(0)->b == 90);
    seq_key(&S, 0, 62, 0, 0);
    run(10);
    CHECK(count(E_OFF, 0) == 3);
    /* roll: 120 ms between the notes */
    nev = 0;
    par[0][TP_P1] = 127;
    seq_key(&S, 0, 60, 1, 90);
    run(12000);
    CHECK(count(E_ON, 0) == 3 && near(on_n(1)->t - on_n(0)->t, 5292, 30) && near(on_n(2)->t - on_n(1)->t, 5292, 30));
    /* hold: the chord stays after the key */
    par[0][TP_P4] = 1;
    seq_key(&S, 0, 60, 0, 0);
    run(10);
    CHECK(count(E_OFF, 0) == 0);
    seq_key(&S, 0, 65, 1, 90);
    run(10);
    CHECK(count(E_OFF, 0) == 3);
    printf("ok %s\n", cur);
}

static void t_hold_player(void)
{
    cur = "hold-player";
    reset();
    par[0][TP_PLAYER] = PL_HOLD;
    seq_key(&S, 0, 60, 1, 100);
    seq_key(&S, 0, 60, 0, 0);
    run(10);
    CHECK(count(E_ON, 0) == 1 && count(E_OFF, 0) == 0);
    seq_key(&S, 0, 62, 1, 100);
    run(10);
    CHECK(count(E_ON, 0) == 2 && count(E_OFF, 0) == 1 && nth(E_OFF, 0, 0)->tag == on_n(0)->tag);
    stop();                           /* the transport stops: everything held goes */
    CHECK(count(E_OFF, 0) == 2);
    printf("ok %s\n", cur);
}

static void t_stutter(void)
{
    cur = "stutter";
    reset();
    par[0][TP_ENGINE] = EG_DRUMLINE;
    note(0, 0, 1, 100);               /* the kick */
    start();
    run(L16);
    CHECK(count(E_DRUM, 0) == 1 && nth(E_DRUM, 0, 0)->a == 0);
    seq_punch_key(&S, PD_STUTTER, 1, -1);
    run(4 * L16);
    CHECK(count(E_DRUM, 0) >= 8);
    CHECK(count(E_PUNCH, -1) >= 1 && nth(E_PUNCH, -1, 0)->a == (1 << PD_STUTTER));
    seq_punch_key(&S, PD_STUTTER, 0, -1);
    nev = 0;
    run(8 * L16);
    CHECK(count(E_DRUM, 0) == 0);
    /* a synth track: the step's notes again every 1/32 */
    reset();
    note(0, 0, 60, 100);
    start();
    seq_punch_key(&S, PM_STUTTER, 1, -1);
    run(4 * L16);
    CHECK(count(E_ON, 0) >= 8 && on_n(3)->a == 60);
    printf("ok %s\n", cur);
}

static void t_ks_fill(void)
{
    int i, kicks = 0, snares = 0;
    cur = "ks-fill";
    reset();
    par[0][TP_ENGINE] = EG_DRUMLINE;
    seq_punch_key(&S, PD_KS_FILL, 1, -1);
    start();
    run(16 * L16);
    for (i = 0; i < nev; i++)
        if (ev[i].kind == E_DRUM) {
            kicks += ev[i].a == 0;
            snares += ev[i].a == 1;
        }
    CHECK(kicks == 4 && snares == 2 && near(nth(E_DRUM, 0, 1)->t, 4 * L16 + 2, 2));
    /* fill: the last sound hit on every empty step; octave up on the hits */
    reset();
    par[0][TP_ENGINE] = EG_DRUMLINE;
    note(0, 0, 5, 100);
    seq_punch_key(&S, PD_FILL, 1, -1);
    seq_punch_key(&S, PD_OCT_UP, 1, -1);
    start();
    run(16 * L16);
    CHECK(count(E_DRUM, 0) == 16 && nth(E_DRUM, 0, 1)->a == 4 && nth(E_DRUM, 0, 1)->c == 12);
    printf("ok %s\n", cur);
}

static void t_loop4(void)
{
    int i;
    cur = "loop4";
    reset();
    for (i = 0; i < 16; i++)
        note(0, i, 60 + i, 100);
    start();
    run(5 * L16 + 10);                /* on step 6 (index 5): the loop is steps 5..8 */
    seq_punch_key(&S, PM_LOOP4, 1, -1);
    run(8 * L16);
    CHECK(on_n(6)->a == 66 && on_n(7)->a == 67 && on_n(8)->a == 64 && on_n(9)->a == 65 && on_n(12)->a == 64);
    seq_punch_key(&S, PM_LOOP4, 0, -1);
    run(2 * L16);
    CHECK(on_n(14)->a == 66 && on_n(15)->a == 67);   /* released on 65: on it goes */
    printf("ok %s\n", cur);
}

static void t_half_double(void)
{
    int i;
    cur = "half-double";
    reset();
    for (i = 0; i < 16; i++)
        note(0, i, 60 + i, 100);
    seq_punch_key(&S, PM_HALF, 1, -1);
    start();
    run(4 * L16 + 10);
    CHECK(count(E_ON, 0) == 3 && near(on_n(1)->t, 2 * L16 + 1, 2));
    seq_punch_key(&S, PM_HALF, 0, -1);
    seq_punch_key(&S, PM_DOUBLE, 1, -1);
    nev = 0;
    run(4 * L16);                     /* the step already scheduled keeps its time; then every half step */
    CHECK(count(E_ON, 0) >= 5 && near(on_n(2)->t - on_n(1)->t, L16 / 2, 2));
    /* reverse */
    reset();
    for (i = 0; i < 16; i++)
        note(0, i, 60 + i, 100);
    seq_punch_key(&S, PM_REVERSE, 1, -1);
    start();
    run(3 * L16 + 10);
    CHECK(on_n(0)->a == 60 && on_n(1)->a == 75 && on_n(2)->a == 74);
    /* octave down, and a ramp */
    reset();
    note(0, 0, 60, 100);
    note(0, 4, 60, 100);
    seq_punch_key(&S, PM_OCT_DOWN, 1, -1);
    seq_punch_key(&S, PM_RAMP, 1, -1);
    start();
    run(5 * L16);
    CHECK(on_n(0)->a == 48 && on_n(1)->a == 52);
    printf("ok %s\n", cur);
}

static void t_record(void)
{
    cur = "record";
    reset();
    S.rec = 1;
    start();
    run(L16 * 6 / 10);                /* 60 % into step 1: nearer step 2 */
    seq_key(&S, 0, 60, 1, 99);
    CHECK(seq_rec_pos(&S, 0) == 1);
    run(L16 * 5 / 10);                /* held for half a step: a quarter-step pair */
    seq_key(&S, 0, 60, 0, 0);
    run(10);
    CHECK(st(0, 1)->note[0] == 60 && st(0, 1)->vel == 99 && st(0, 1)->nudge == 0 && st(0, 1)->len == 2);
    /* overdub: a second note on the same step, the next pass (60 % into step 1 again) */
    run_to(16 * L16 + L16 * 6 / 10);
    seq_key(&S, 0, 64, 1, 80);
    run(10);
    seq_key(&S, 0, 64, 0, 0);
    run(10);
    CHECK(st(0, 1)->note[0] == 60 && st(0, 1)->note[1] == 64);
    /* quant 0 keeps the timing as a nudge */
    P.pat[0][0].quant = 0;
    run_to(32 * L16 + L16 + L16 * 3 / 10);   /* 30 % into step 2 (index 1) */
    seq_key(&S, 0, 67, 1, 80);
    run(10);
    CHECK(st(0, 1)->note[2] == 67);
    run_to(32 * L16 + 2 * L16 + L16 * 3 / 10);
    seq_key(&S, 0, 62, 1, 80);        /* 30 % into step 3: a fresh step, nudged +7 ticks */
    run(10);
    CHECK(st(0, 2)->note[0] == 62 && st(0, 2)->nudge == 7);
    /* a knob while recording: a lock on the playing step */
    seq_rec_knob(&S, 0, TP_RES, 77);
    CHECK(lane_find(P.lane, 0, 0, TP_RES) >= 0 && P.lane[lane_find(P.lane, 0, 0, TP_RES)].val[2] == 77);
    /* step record */
    stop();
    CHECK(seq_step_rec(&S, 0, 5, 72, 100) == 1 && st(0, 5)->note[0] == 72);
    CHECK(seq_step_rec(&S, 0, 5, 72, 100) == 0 && seq_step_rec(&S, 0, 5, 76, 100) == 1 && st(0, 5)->note[1] == 76);
    printf("ok %s\n", cur);
}

static void t_ext_clock(void)
{
    int i;
    cur = "ext-clock";
    reset();
    note(0, 0, 60, 100);
    note(0, 1, 62, 100);
    seq_ext_clock(&S);
    start();
    CHECK(S.ext == 1 && count(E_ON, 0) == 1 && on_n(0)->t == 0);
    for (i = 0; i < 12; i++) {        /* 24 ppqn at 100 BPM: a tick every 1102.5 samples */
        run(1102);
        seq_ext_clock(&S);
        seq_advance(&S, 0, &sink);
    }
    CHECK(count(E_ON, 0) == 2 && on_n(1)->a == 62 && near(on_n(1)->t, 6 * 1102, 1));
    CHECK(seq_tempo(&S) > 95.0f && seq_tempo(&S) < 105.0f);
    run(SEQ_FS);                      /* the clock stops: internal again after half a second */
    CHECK(S.ext == 0 && count(E_STEP, 0) >= 3 && nth(E_STEP, 0, 2)->a == 2);
    printf("ok %s\n", cur);
}

static void t_midi_out(void)
{
    int i, clocks = 0, starts = 0, ons = 0;
    cur = "midi-out";
    reset();
    S.send_clock = 1;
    S.send_notes = 1;
    par[0][TP_CHAN] = 3;
    note(0, 0, 60, 100);
    start();
    run(4 * L16 + 1);
    for (i = 0; i < nev; i++)
        if (ev[i].kind == E_MIDI) {
            clocks += ev[i].a == 0xF8;
            starts += ev[i].a == 0xFA;
            ons += ev[i].a == 0x92 && ev[i].b == 60;
        }
    CHECK(clocks >= 24 && clocks <= 25 && starts == 1 && ons == 1);
    CHECK(near(nth(E_MIDI, -1, 3)->t, 918, 2));   /* start, note on, clock, clock at 1/24 of a quarter */
    stop();
    CHECK(ev[nev - 1].kind == E_MIDI && ev[nev - 1].a == 0xFC);
    printf("ok %s\n", cur);
}

static void t_count_in(void)
{
    cur = "count-in";
    reset();
    note(0, 0, 60, 100);
    S.rec = 1;
    S.metronome = 1;
    S.count_in = 1;
    start();
    run(17 * L16);
    CHECK(count(E_METRO, -1) >= 5 && nth(E_METRO, -1, 0)->t == 0 && nth(E_METRO, -1, 0)->a == 1 &&
          near(nth(E_METRO, -1, 1)->t, 4 * L16 + 2, 2) && nth(E_METRO, -1, 1)->a == 0);
    CHECK(count(E_ON, 0) == 1 && near(on_n(0)->t, 16 * L16 + 8, 1) && S.count_in == 0 && S.rec == 1);
    CHECK(nth(E_TSTEP, -1, 0) && nth(E_TSTEP, -1, 0)->a == 5512);
    /* the metronome off while playing without rec */
    reset();
    S.metronome = 1;
    start();
    run(16 * L16);
    CHECK(count(E_METRO, -1) == 0);
    printf("ok %s\n", cur);
}

static void t_aux_tracks(void)
{
    cur = "aux-tracks";
    reset();
    note(0, 0, 60, 100);
    note(0, 1, 60, 100);
    P.pat[AUX_PUNCH][0].st[1].note[0] = PM_MUTE + 1;     /* the melodic mute on step 2, a step long */
    P.pat[AUX_PUNCH][0].st[1].vel = 100;
    P.pat[AUX_TAPE][0].st[0].note[0] = 4;               /* segment 4 for a step */
    P.pat[AUX_TAPE][0].st[0].vel = 100;
    start();
    run(3 * L16);
    CHECK(count(E_ON, 0) == 1);       /* step 2 muted by the sequenced FX */
    CHECK(nth(E_PUNCH, -1, 0) && nth(E_PUNCH, -1, 0)->b == 1);
    CHECK(nth(E_SEG, -1, 0)->a == 3 && nth(E_SEG, -1, 0)->b == 1 && nth(E_SEG, -1, 1)->b == 0 &&
          near(nth(E_SEG, -1, 1)->t, L16, 1));
    seq_tape_key(&S, 7, 1);
    run(10);
    CHECK(nth(E_SEG, -1, 2)->a == 7 && nth(E_SEG, -1, 2)->b == 1);
    printf("ok %s\n", cur);
}

static void t_pattern_helpers(void)
{
    step_t s;
    int a, b;
    cur = "pattern-helpers";
    reset();
    memset(&s, 0, sizeof s);
    CHECK(step_empty(&s));
    step_toggle_note(&s, 60);
    step_toggle_note(&s, 64);
    CHECK(!step_empty(&s) && s.note[0] == 60 && s.note[1] == 64 && s.vel == 100);
    step_toggle_note(&s, 60);
    CHECK(s.note[0] == 64 && s.note[1] == 0);
    cond_ab(0, &a, &b);
    CHECK(a == 1 && b == 2);
    cond_ab(2, &a, &b);
    CHECK(a == 1 && b == 3);
    cond_ab(COND_NAB - 1, &a, &b);
    CHECK(a == 8 && b == 8);
    CHECK(P.pat[AUX_BRAIN][0].scale == TS_4 && P.pat[0][0].len == 16 && P.scene[0].used);
    printf("ok %s\n", cur);
}

int main(void)
{
    t_pattern_helpers();
    t_timing();
    t_scale();
    t_swing();
    t_length();
    t_chord();
    t_pulse();
    t_hold();
    t_multiply();
    t_velocity();
    t_ramp();
    t_random();
    t_portamento();
    t_bend();
    t_jump();
    t_skip();
    t_cond();
    t_locks();
    t_scene();
    t_song();
    t_brain();
    t_arp();
    t_maestro();
    t_hold_player();
    t_stutter();
    t_ks_fill();
    t_loop4();
    t_half_double();
    t_record();
    t_ext_clock();
    t_midi_out();
    t_count_in();
    t_aux_tracks();
    printf("all sequencer tests passed\n");
    return 0;
}
