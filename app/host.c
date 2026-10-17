/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 in fm1-base's host simulator (base/host/plat_host.c, which has the common commands): LOOM-8's platform
 * extras as the simulator has them, its checks and its commands. Included by app.c, inside plat_host.c (its statics:
 * now_ms, enc_acc, store, run_ms ...). Tracks, patterns, steps and scenes count from 1 in a script, as the screen
 * shows them; "expect step" reports the playhead 0-based (eng_step).
 *   param T P V                  parameter P (synth.h TP_*) of track T set to V, as a knob would
 *   load N                       every block costs N % (the overload guard's input); -1 = measured
 *   steps T PAT                  print track T's pattern PAT: each step's notes, component, condition
 *   step T PAT S NOTE VEL        write one note on step S (NOTE 0 clears the step)
 *   chord T PAT S LEN N1 N2 [N3 [N4]]  notes together on step S, LEN quarter steps long
 *   scene N                      switch to scene N now
 *   punch KEY on|off             a punch-in key (0..26 = F3..G5, or w0../b0..) held or released, on the group
 *   brainkey N on|off            a key on the BRAIN track (MIDI note N)
 *   bpm N                        the tempo (a decimal allowed: 0.1 BPM steps)
 *   cores N                      the second core on (1) or off (0): its job then runs in place
 *   samples FILE                 the user sample bank from FILE (an image from tools/loom_samples.py build), as an
 *                                upload would leave it (empty at boot)
 *   expect WHAT VALUE            besides fm1-base's: playing, scene, voices, guard, cores, dirty, mem (the last save's
 *                                share of a project slot, %), tempo, trk, punch_drum / punch_mel (the masks in effect,
 *                                PX_* bits), parT.P; and with more words: step T VALUE, page NAME, comp T S VALUE (prints
 *                                both components; checks the first), note T PAT S VALUE
 * LOOM_THEME=N: the screenshots in theme N (ui.c). The layout audit (gfx_loom.c): its findings fail the run. */
#include <time.h>

/* ---- LOOM-8's platform extras (app.h), simulated */
static uint32_t cpu_peak;
uint32_t plat_cpu_peak_pct(void)
{
    uint32_t p = cpu_peak;
    cpu_peak = 0;
    return p;
}
static double host_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}
static uint32_t render_t0(void) { return (uint32_t)(uint64_t)(host_ns() / 10.0); }
static uint32_t render_pct(uint32_t t0, uint32_t n)   /* the block's share of its time on this machine, % */
{
    uint32_t pct = (uint32_t)(100.0 * (double)(render_t0() - t0) * 10.0 / ((double)n * 1e9 / 44100.0));
    if (pct > cpu_peak)
        cpu_peak = pct;
    return pct;
}
uint32_t plat_cycles(void) { return render_t0(); }   /* the profiler's clock: nanoseconds / 10 here */

/* the store's copy before the current one (storage.c keeps two on the FM-1): kept here beside fm1-base's store */
static uint8_t store_prev[OBJ_NOBJ][PLAT_STORE_MAX];
static int store_prev_len[OBJ_NOBJ] = {[0 ... OBJ_NOBJ - 1] = -1};
int loom_store_save(uint32_t obj, const void *src, uint32_t len)
{
    int n = obj < OBJ_NOBJ ? plat_store_size(obj) : -1, rc;
    if (n > 0) {
        memcpy(store_prev[obj], store[obj], (size_t)n);
        store_prev_len[obj] = n;
    }
    rc = plat_store_save(obj, src, len);
    if (rc && n > 0)
        store_prev_len[obj] = -1;                   /* (a save refused: nothing moved) */
    return rc;
}
int plat_store_load_prev(uint32_t obj, void *dst, uint32_t max)
{
    int n;
    if (obj >= OBJ_NOBJ || (n = store_prev_len[obj]) < 0)
        return -1;
    if ((uint32_t)n > max)
        n = (int)max;
    memcpy(dst, store_prev[obj], (size_t)n);
    return n;
}

/* the second core, simulated: a job runs at once, in order (the split path, the same sums) */
static int cpu2_on = 1;
int plat_cpu2_set(int on) { cpu2_on = on; return 0; }
int plat_cpu2_on(void) { return cpu2_on; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (!cpu2_on)
        return 0;
    fn(arg);
    return 1;
}
int plat_cpu2_wait(void) { return 0; }
int plat_cpu2_gave_up(void) { return 0; }
uint32_t plat_cpu2_bad(void) { return 0; }
int plat_cpu2_barred(void) { return 0; }
int plat_vdc14_mv(void) { return 1600; }
int plat_sysvdd_mv(void) { return 1380; }
int plat_last_crash(uint32_t v[5]) { (void)v; return 0; }

/* ---- the script */
static void run_ms(uint32_t ms);                        /* (plat_host.c's, after this) */
static int host_fails;
static int key_of(const char *s);

static tpat_t *pat_of(int t, int p)             /* track T, pattern P (1-based; P 0 = the one playing) */
{
    if (t < 1 || t > NSEQT)
        return 0;
    if (p == 0)
        p = seq.ppat[t - 1] + 1;
    if (p < 1 || p > NPATT)
        return 0;
    return &pats.pat[t - 1][p - 1];
}

static void steps_print(int t, int p)
{
    tpat_t *tp = pat_of(t, p);
    int s, k;
    if (!tp) {
        printf("steps: no track %d pattern %d\n", t, p);
        return;
    }
    printf("  track %d pattern %d: %d steps, scale %d, nlen %d\n", t, p, tp->len, tp->scale, tp->nlen);
    for (s = 0; s < tp->len; s++) {
        const step_t *st = &tp->st[s];
        if (step_empty(st) && !st->comp && !st->comp2 && !st->cond)
            continue;
        printf("    %2d:", s + 1);
        for (k = 0; k < SN; k++)
            if (st->note[k])
                printf(" %d", st->note[k]);
        printf("  vel %d len %d nudge %d comp %d/%d comp2 %d/%d cond %d\n", st->vel, st->len, st->nudge, st->comp,
               st->cval, st->comp2, st->cval2, st->cond);
    }
}

/* fm1-base's expect, for LOOM-8's own state with one value */
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "playing"))
        *got = seq.playing;
    else if (!strcmp(what, "scene"))
        *got = seq.scene + 1;
    else if (!strcmp(what, "voices"))
        *got = eng_voices;
    else if (!strcmp(what, "guard"))
        *got = eng_voices_max;
    else if (!strcmp(what, "cores"))
        *got = engine_core2_state();
    else if (!strcmp(what, "dirty"))
        *got = ui_dirty();
    else if (!strcmp(what, "mem"))                  /* the last save's share of a project slot, % */
        *got = project_mem_pct();
    else if (!strcmp(what, "tempo"))
        *got = (int)(seq_tempo(&seq) + 0.5f);
    else if (!strcmp(what, "trk"))
        *got = proj.trk_sel + 1;
    else if (!strcmp(what, "punch_drum"))           /* the punch masks in effect (seq.h PX_* bits) */
        *got = (int)eng_punch_drum;
    else if (!strcmp(what, "punch_mel"))
        *got = (int)eng_punch_mel;
    else if (!strncmp(what, "par", 3) && strchr(what, '.')) {   /* parT.P: parameter P of track T */
        int t = atoi(what + 3) - 1, p = atoi(strchr(what, '.') + 1);
        if (t < 0 || t >= NALLTRK || p < 0 || p >= NTP)
            return 0;
        *got = proj.trk[t].par[p];
    } else
        return 0;
    return 1;
}

static int expect_val(const char *what, int got, const char *val)   /* (fm1-base's expect's way with a value) */
{
    if (val[0] == '<' || val[0] == '>') {
        int lim = (int)strtol(val + 1, 0, 0);
        if (val[0] == '<' ? got >= lim : got <= lim) {
            printf("FAIL expect %s %s, got %d\n", what, val, got);
            return 1;
        }
        printf("  ok %s == %d (%s)\n", what, got, val);
        return 0;
    }
    if (got != (int)strtol(val, 0, 0)) {
        printf("FAIL expect %s == %s, got %d\n", what, val, got);
        return 1;
    }
    printf("  ok %s == %d\n", what, got);
    return 0;
}

/* the expects with more words than fm1-base's: 1 = handled (a failure counted in host_fails) */
static int expect_more(const char *what, const char *a, const char *b, const char *c)
{
    if (!strcmp(what, "step")) {                    /* expect step T VALUE: the playhead of track T (0-based) */
        host_fails += expect_val(what, eng_step[(atoi(a) - 1) % NSEQT], b);
    } else if (!strcmp(what, "page")) {             /* expect page NAME */
        const char *p = ui_page_name();
        int bad = strcmp(p, a) != 0;
        printf("%s page \"%s\"%s%s\n", bad ? "FAIL" : "  ok", p, bad ? ", expected " : "", bad ? a : "");
        host_fails += bad;
    } else if (!strcmp(what, "comp")) {             /* expect comp T S VALUE: step S's component on the playing pattern */
        tpat_t *tp = pat_of(atoi(a), 0);
        int s = atoi(b) - 1;
        if (!tp || s < 0 || s >= NSTEP) {
            printf("FAIL expect comp: no track %s step %s\n", a, b);
            host_fails++;
            return 1;
        }
        printf("  comp %s step %s: %d/%d and %d/%d\n", a, b, tp->st[s].comp, tp->st[s].cval, tp->st[s].comp2,
               tp->st[s].cval2);
        host_fails += expect_val(what, tp->st[s].comp, c);
    } else if (!strcmp(what, "note")) {             /* expect note T PAT S VALUE: five words */
        char t[16], pt[16], st[16], v[16];
        tpat_t *tp;
        int s, got;
        if (sscanf(plat_host_line, "%*s %*s %15s %15s %15s %15s", t, pt, st, v) != 4) {
            printf("FAIL expect note: T PAT S VALUE\n");
            host_fails++;
            return 1;
        }
        tp = pat_of(atoi(t), atoi(pt));
        s = atoi(st) - 1;
        got = tp && s >= 0 && s < NSTEP ? tp->st[s].note[0] : -1;
        if (got != atoi(v)) {
            printf("FAIL expect note %s %s %s == %s, got %d\n", t, pt, st, v, got);
            host_fails++;
        } else
            printf("  ok note %s %s %s == %d\n", t, pt, st, got);
    } else if (!strncmp(what, "par", 3) && !strchr(what, '.')) {
        printf("FAIL unknown expect %s\n", what);
        host_fails++;
    } else
        return 0;
    return 1;
}

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    if (!strcmp(cmd, "expect"))
        return expect_more(a, b, c, d);
    if (!strcmp(cmd, "param")) {                    /* param T P V */
        int t = atoi(a) - 1, p = atoi(b), v = atoi(c);
        if (t < 0 || t >= NALLTRK || p < 0 || p >= NTP) {
            printf("no param %s %s\n", a, b);
            exit(2);
        }
        proj.trk[t].par[p] = (uint8_t)v;
        engine_set(t, p, v);
        run_ms(1);
    } else if (!strcmp(cmd, "samples")) {           /* samples FILE: the user bank as uploaded */
        static uint8_t img[SMP_BANK_SIZE];
        FILE *bf = fopen(a, "rb");
        size_t nb;
        uint32_t o;
        if (!bf) {
            printf("no bank %s\n", a);
            exit(2);
        }
        memset(img, 0xFF, sizeof img);
        nb = fread(img, 1, sizeof img, bf);
        fclose(bf);
        smp_user_drop();
        for (o = 0; o < SMP_BANK_SIZE; o += 0x1000u) {   /* as written: the bank's sectors erased, then the image */
            plat_data_erase(SMP_BANK_BASE + o);
            plat_data_write(SMP_BANK_BASE + o, img + o, 0x1000u);
        }
        printf("samples: %s, %d B, %d user samples\n", a, (int)nb, smp_user_scan());
        run_ms(1);
    } else if (!strcmp(cmd, "load"))
        load_force = atoi(a);
    else if (!strcmp(cmd, "steps"))
        steps_print(atoi(a), atoi(b));
    else if (!strcmp(cmd, "step")) {                /* step T PAT S NOTE [VEL] */
        char e[16] = {0};
        tpat_t *tp = pat_of(atoi(a), atoi(b));
        int s = atoi(c) - 1, note = atoi(d), vel;
        sscanf(plat_host_line, "%*s %*s %*s %*s %*s %15s", e);
        vel = e[0] ? atoi(e) : 100;
        if (!tp || s < 0 || s >= NSTEP) {
            printf("no step %s %s %s\n", a, b, c);
            exit(2);
        }
        if (note <= 0) {
            memset(&tp->st[s], 0, sizeof tp->st[s]);
        } else {
            uint8_t n1 = (uint8_t)note;
            step_set_notes(&tp->st[s], &n1, 1, vel);
        }
    } else if (!strcmp(cmd, "chord")) {             /* chord T PAT S LEN N1 N2 [N3 [N4]]: notes on one step */
        char w[4][16] = {{0}};
        tpat_t *tp = pat_of(atoi(a), atoi(b));
        int s = atoi(c) - 1, n = 0;
        uint8_t notes[SN];
        sscanf(plat_host_line, "%*s %*s %*s %*s %*s %15s %15s %15s %15s", w[0], w[1], w[2], w[3]);
        for (int i = 0; i < SN; i++)
            if (w[i][0] && atoi(w[i]) > 0)
                notes[n++] = (uint8_t)atoi(w[i]);
        if (!tp || s < 0 || s >= NSTEP || !n) {
            printf("no step %s %s %s\n", a, b, c);
            exit(2);
        }
        step_set_notes(&tp->st[s], notes, n, 90);
        tp->st[s].len = (uint8_t)atoi(d);
    } else if (!strcmp(cmd, "scene"))
        engine_scene((atoi(a) - 1) & (NSCENE - 1), 0);
    else if (!strcmp(cmd, "punch")) {               /* punch KEY on|off: through the key's punch bit, on the group */
        int fx = punch_of_key(key_of(a));
        if (fx < 0) {
            printf("key %s has no punch-in fx\n", a);
            exit(2);
        }
        engine_punch(fx, !strcmp(b, "on"), -1);
        run_ms(1);
    } else if (!strcmp(cmd, "brainkey")) {          /* brainkey NOTE on|off */
        engine_brain_key(atoi(a), !strcmp(b, "on"));
        run_ms(1);
    } else if (!strcmp(cmd, "bpm"))
        engine_tempo((int)(atof(a) * 10.0 + 0.5));
    else if (!strcmp(cmd, "cores"))
        plat_cpu2_set(atoi(a));
    else
        return 0;
    return 1;
}

void app_speak_dump(void) { speak_dump(); }
void app_host_reboot(void) { memset(&proj, 0, sizeof proj); }   /* what RAM forgets (the flash keeps the rest) */
int app_host_fails(void)
{
#ifdef GFX_AUDIT
    if (ga_findings)                                /* the layout audit (gfx_loom.c): labels must not clip, touch or overlap */
        printf("LAYOUT: %u findings\n", (unsigned)ga_findings);
    return host_fails + (ga_findings != 0);
#else
    return host_fails;
#endif
}
#ifdef GFX_AUDIT
static const char *gfx_audit_where(void)          /* the layout audit's "where": the page and the track */
{
    static char w[64];
    snprintf(w, sizeof w, "%s, track %d", ui_page_name(), (int)proj.trk_sel + 1);
    return w;
}
#endif
