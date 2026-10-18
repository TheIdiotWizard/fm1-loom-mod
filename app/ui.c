/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8's input: the panel's buttons, keys and encoders, what they do on each page, and the screen reader.
 * Part of the unity build, after pages.c; the host simulator includes it too. The gestures are the OP-XY
 * notebook's, with HOME as shift and a held step as "what is on it":
 *
 *   HOME   Arrange (again: Song); held: HOME + key = punch-in fx, + step + white key = a component (black
 *          key = its value), + REC undo, + PLAY redo, + SELECT tempo, + K1 fine tempo, + PRESETS scene,
 *          + white key on Arrange = cue the scene at the bar, + black key on Mix = mute
 *   SEQ    Steps (page 2 Bar)      EDIT Engine (Play; a drum key's pages)   ENV Envelope (filter envelope)
 *   FX     Filter (Sends)          LFO  LFO (LFO 2)        ARP  Players (its page 2, hold)
 *   GLO    Mix, Master, Setup, Status
 *   PLAY   start (from the top: every track's step 1) / stop; REC + PLAY = live recording (stopped: waits for the
 *          first key; playing: keeps REC on)
 *   REC    held while playing: records while held; stopped: a tap arms step record
 *   SAVE   save (held + white key: a slot, or a user preset on the sound pages)
 *   SELECT the button's pages     ALGORITHM the track      PRESETS the page's item     K1-K4 the page's four */

static void speak(void);
static void step_rec_start(void);
static void sp_msg(const char *a, const char *b, uint32_t flags);
static uint8_t sp_said;
static uint32_t sp_in_t;

static void say_as(const char *a, const char *b, uint32_t flags)
{
    uint32_t i;
    sp_msg(a, b, flags);
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    upcase(ui.msg[0]);
    upcase(ui.msg[1]);
    ui.msg_until = plat_ms() + 1300u;
    ui.msg_no = 0;
    {
        static const char *const DONE[] = {"SAVED", "CLEARED", "COPIED", "PASTED", "ROTATED", "DUPLICATED", "RECORDED",
                                           "CHORD RECORDED", "LOCKS CLEARED", "NEW PROJECT", "LOADED", "SCRAMBLED",
                                           "RESET", "INSERTED", "DELETED", "PRESET SAVED", "ALL NOTES OFF", "REMOVED", "WOVEN"};
        const char *m = ui.msg[0];
        ui.msg_kind = AL_INFO;
        if (m[0] == 'U' && m[1] == 'N' && m[2] == 'D' && m[3] == 'O')
            ui.msg_kind = AL_UNDO;
        else if (m[0] == 'R' && m[1] == 'E' && m[2] == 'D' && m[3] == 'O')
            ui.msg_kind = AL_REDO;
        else
            for (uint32_t k = 0; k < sizeof DONE / sizeof DONE[0]; k++) {
                const char *p = DONE[k], *q = m;
                while (*p && *p == *q)
                    p++, q++;
                if (!*p) {
                    ui.msg_kind = AL_DONE;
                    break;
                }
            }
    }
#if defined(PLAT_HOST) && !defined(PLAT_WEB)
    if (8 + text_w(&FONT_B, ui.msg[0]) + 8 + text_w(&FONT_B, ui.msg[1]) > 232) {
        printf("  FAIL message too wide for the header: %s %s\n", ui.msg[0], ui.msg[1]);
        msg_too_wide++;
    }
#endif
}
static void say(const char *a, const char *b) { say_as(a, b, ANN_NOW); }
static void say_no(const char *a, const char *b)
{
    say_as(a, b, ANN_NOW | ANN_KEEP);
    ui.msg_no = 1;
    ui.msg_until = plat_ms() + 2000u;
}
void ui_say(const char *a, const char *b) { say(a, b); }
const char *ui_page_name(void) { return PAGE_NAME[ui.page % NPAGES]; }

static void mark_dirty(void)
{
    ui.dirty = 1;
    ui.act_t = plat_ms();
}
int ui_dirty(void) { return ui.dirty; }

static uint8_t accel_off;
#ifdef PLAT_HOST
#define ACCEL_OFF (accel_off || plat_host_exact)  /* the simulator's "spin": exactly the steps given */
#else
#define ACCEL_OFF accel_off
#endif
static int32_t accel(int role, int32_t s)
{
    uint32_t now = plat_ms(), dt = now - ui.enc_t[role], a = (uint32_t)(s < 0 ? -s : s), m;
    ui.enc_t[role] = now;
    if (!a || ACCEL_OFF)
        return s;
    if (a > 1)
        dt /= a;
    m = dt < 15u ? 6u : dt < 30u ? 4u : dt < 60u ? 2u : 1u;
    return s * (int32_t)m;
}
static void touch(int k)
{
    ui.touched = (int8_t)k;
    ui.touch_until = plat_ms() + 900u;
}
static int ordinal(const uint8_t *t, int n, int k)
{
    for (int i = 0; i < n; i++)
        if (t[i] == k)
            return i;
    return -1;
}
static int key_note(int k) { return KEY_BASE + k + ui.oct * 12; }
/* a drum track's keys: the 11 keys on the black keys, spread over the white keys too (a key is note 1..11,
 * as the steps hold it) */
static int key_sound(int k)
{
    int b = ordinal(BLACK_K, 11, k);
    if (b >= 0)
        return b + 1;
    return ordinal(WHITE_K, 16, k) % DL_NSOUND + 1;
}

/* ---------------------------------------------------- the engine glue --- */
static void apply_brain(void)
{
    const uint8_t *p = proj.trk[AUX_BRAIN].par;
    uint16_t route = 0;
    seq.brain_mode = p[TP_E1] & 1u;
    seq.brain_key = p[TP_E2] % 12u;
    seq.brain_scale = p[TP_E3] % NBS;
    seq.brain_link = p[TP_E4] % 9u;
    for (int t = 0; t < NTRK; t++)
        if (proj.trk[t].par[TP_BRAIN] % 3u)
            route |= (uint16_t)(1u << t);
    seq.brain_route = route;
    pats.scene[cur_scene()].brain_key = seq.brain_key;
    pats.scene[cur_scene()].brain_scale = seq.brain_scale;
}
static void apply_fx(int trk)
{
    const uint8_t *p = proj.trk[trk].par;    /* the engine's layout: TP_ENGINE the effect, E1..E4 its knobs,
                                              * FENV the high-pass, CUT the low-pass (SEND2: the feed, by engine_set) */
    engine_fx(trk - AUX_FX1, p[TP_ENGINE] % NFXPICK, p + TP_E1, p[TP_FENV], p[TP_CUT]);
}
static void apply_aux(int trk)
{
    if (trk == AUX_BRAIN)
        apply_brain();
    else if (is_fx(trk))
        apply_fx(trk);
}
static void set_tp(int trk, int p, int v)
{
    uint8_t *par = proj.trk[trk].par;
    if (p < 0 || p >= NTP)
        return;
    if (!is_aux(trk)) {
        const tp_info_t *in = tp_info_par(p, par);
        v = clampi(v, in->lo, in->hi);
    } else
        v = clampi(v, 0, 127);
    if (par[p] == v)
        return;
    par[p] = (uint8_t)v;
    if (p == TP_LTYPE && !is_aux(trk)) {         /* another LFO type: its own knobs start from their defaults */
        static const uint8_t LQ[5] = {TP_LSPEED, TP_LAMT, TP_LDEST, TP_LX, TP_LX2};
        engine_set(trk, p, v);
        for (int i = 0; i < 5; i++) {
            par[LQ[i]] = tp_info_par(LQ[i], par)->def;
            engine_set(trk, LQ[i], par[LQ[i]]);
        }
        mark_dirty();
        return;
    }
    if (is_drum(trk) && p >= TP_DRUM0 && (p - TP_DRUM0) % DP_N == DP_MODEL) {   /* another model on a key: its
                                                                                * own knobs from its defaults */
        int key = (p - TP_DRUM0) / DP_N;
        engine_set(trk, p, v);
        for (int k = 0; k < 4; k++) {
            par[TP_DRUM(key, DP_K1 + k)] = DM_DEFAULT[v % NDM][k];
            engine_set(trk, TP_DRUM(key, DP_K1 + k), par[TP_DRUM(key, DP_K1 + k)]);
        }
        mark_dirty();
        return;
    }
    if (p == TP_ENGINE && !is_aux(trk)) {
        sound_default(par, v);
        memset(proj.trk[trk].name, 0, NAME_LEN);
        engine_set_all(trk);
        {                                        /* a kit has a page more under ENV: off it, back to the first */
            int g = page_group(ui.page);
            if (ui.page - PAGE_BTN[g].first >= group_n(g))
                ui.page = PAGE_BTN[g].first;
        }
    } else
        engine_set(trk, p, v);
    if (is_aux(trk))
        apply_aux(trk);
    if (p == TP_BRAIN)
        apply_brain();
    mark_dirty();
}
static void set_tempo_x10(int x10)
{
    x10 = clampi(x10, 400, 2200);
    proj.tempo_x10 = (uint16_t)x10;
    engine_tempo(x10);
    mark_dirty();
}
static void set_groove_type(int g)
{
    proj.groove = (uint8_t)clampi(g, 0, NGROOVE - 1);
    engine_groove(proj.groove);
    mark_dirty();
}
static void set_mute(int trk, int on)
{
    uint16_t m = seq.mute;
    if (on)
        m |= (uint16_t)(1u << trk);
    else
        m &= (uint16_t)~(1u << trk);
    seq.mute = m;
    pats.scene[cur_scene()].mute = m;
    mark_dirty();
}
/* a preset is the sound (its sends too); the track's level and pan stay its own */
static void load_preset(int trk, const preset_t *pr)
{
    uint8_t *par = proj.trk[trk].par;
    memcpy(par, pr->par, NTP_SOUND);
    for (int i = 0; i < NAME_LEN; i++)
        proj.trk[trk].name[i] = pr->name[i] ? pr->name[i] : 0;
    engine_set_all(trk);
    mark_dirty();
}
static int presets_all(void) { return NPRESETS + NUSER_PRESETS; }
static const char *preset_name(int i, char *t)
{
    if (i < NPRESETS)
        return PRESETS[i].name;
    {
        int u = i - NPRESETS;
        char *e = num_s((uint32_t)u + 1u, s_cat(t, "U"));
        if (user_presets[u].name[0]) {
            e = s_cat(e, " ");
            for (int k = 0; k < NAME_LEN && user_presets[u].name[k]; k++)
                *e++ = user_presets[u].name[k];
            *e = 0;
        }
        return t;
    }
}
static int preset_engine(int i)                  /* a preset's engine (a user slot never saved: -1) */
{
    if (i < NPRESETS)
        return PRESETS[i].par[TP_ENGINE];
    i -= NPRESETS;
    return user_presets[i].name[0] ? user_presets[i].engine : -1;
}
static int name_is(const char *a, const char *b) /* two preset names (NAME_LEN, maybe unterminated) */
{
    for (int i = 0; i < NAME_LEN; i++) {
        if (a[i] != b[i])
            return 0;
        if (!a[i])
            return 1;
    }
    return 1;
}
static int preset_apply(int trk, int i);
/* PRESETS: the next sound of the track's own engine (its factory presets, then the user presets saved from it);
 * the message says where: "PRESET 3/8 WIDE PAD" */
static void preset_step(int trk, int d)
{
    int eng = proj.trk[trk].par[TP_ENGINE], n = 0, at = -1, i, to, all;
    char a[16], b[24];
    for (i = 0; i < presets_all(); i++)
        if (preset_engine(i) == eng) {
            if (at < 0 && ((i < NPRESETS && name_is(PRESETS[i].name, proj.trk[trk].name)) || i == ui.preset_i))
                at = n;
            n++;
        }
    if (!n) {
        say_no("NO PRESETS", "for this engine");
        return;
    }
    all = n;
    to = at < 0 ? (d > 0 ? 0 : n - 1) : ((at + d) % n + n) % n;
    for (i = 0, n = 0; i < presets_all(); i++)
        if (preset_engine(i) == eng && n++ == to)
            break;
    preset_apply(trk, i);
    ui.preset_i = (uint8_t)i;
    ui.pick_preset = (uint8_t)(i < NPRESETS ? i : 0);
    num_s((uint32_t)all, s_cat(num_s((uint32_t)to + 1u, s_cat(a, "PRESET ")), "/"));
    say(a, preset_name(i, b));
}
static int preset_apply(int trk, int i)
{
    if (i < NPRESETS) {
        load_preset(trk, &PRESETS[i]);
        return 1;
    }
    {
        int u = i - NPRESETS;
        if (!user_presets[u].name[0] && !user_presets[u].par[TP_E1])
            return 0;
        memcpy(proj.trk[trk].par, user_presets[u].par, NTP_SOUND);
        proj.trk[trk].par[TP_ENGINE] = user_presets[u].engine;
        memcpy(proj.trk[trk].name, user_presets[u].name, NAME_LEN);
        engine_set_all(trk);
        mark_dirty();
        return 1;
    }
}

/* ------------------------------------------------------------- undo --- */
#define UNDO_RUN_MS 1000u
static void undo_keep(void)
{
    int trk = sel_trk(), pt = cur_pat(trk);
    uint32_t now = plat_ms();
    if (!is_seqt(trk))
        return;
    if (undo.valid && !undo.redo && undo.trk == trk && undo.pt == pt && now - undo.t < UNDO_RUN_MS) {
        undo.t = now;
        return;
    }
    undo.pat = pats.pat[trk][pt];
    undo.trk = (uint8_t)trk;
    undo.pt = (uint8_t)pt;
    undo.valid = 1;
    undo.redo = 0;
    undo.t = now;
}
static void do_undo(int redo)
{
    tpat_t tmp;
    if (!undo.valid || (redo && !undo.redo) || (!redo && undo.redo)) {
        say_no(redo ? "NOTHING TO REDO" : "NOTHING TO UNDO", 0);
        return;
    }
    tmp = pats.pat[undo.trk][undo.pt];
    pats.pat[undo.trk][undo.pt] = undo.pat;
    undo.pat = tmp;
    undo.redo = (uint8_t)!redo;
    undo.t = 0;
    mark_dirty();
    {
        char t[24];
        num_s((uint32_t)undo.pt + 1u, s_cat(s_cat(t, TRACK_NAME[undo.trk]), " P"));
        say(redo ? "REDO" : "UNDO", t);
    }
}

/* -------------------------------------------------------- transport --- */
#define PLAY_TWICE_MS 400u
static void rec_off(void)
{
    ui.rec_latched = 0;
    ui.rec_held = 0;
    if (seq.rec || seq.rec_wait)
        seq_rec(&seq, 0);
}
static void play_press(void)
{
    uint32_t now = plat_ms();
    int twice = now - ui.play_t < PLAY_TWICE_MS, rec_down = ui.btn >> B_REC & 1u;
    ui.play_t = now;
    if (playing()) {
        if (rec_down) {                          /* REC + PLAY while playing: recording stays on */
            ui.rec_latched = 1;
            ui.btn_used |= 1u << B_REC;
            seq_rec(&seq, 1);
            say("RECORDING", "latched");
            return;
        }
        engine_transport(0);
        rec_off();
        return;
    }
    if (ui.page == P_SONG) {
        if (!pats.song.len) {
            say_no("NO SONG", "white keys add scenes");
            return;
        }
        seq.song_on = 1;
        seq.song_pos = 0;
        seq.song_rep = 0;
        engine_scene(pats.song.slot[0].scene % NSCENE, 0);
    } else if (!twice)
        seq.song_on = 0;
    if (rec_down) {                              /* REC + PLAY stopped: live recording from step 1, at once */
        ui.btn_used |= 1u << B_REC;
        ui.rec_armed = 0;
        ui.rec_latched = 1;
        seq.count_in = (uint8_t)(proj.rsv[0] % 3u);   /* (the count-in: REC's knobs, Setup's list; 0, 1 or 2 bars) */
        seq_rec(&seq, 1);
        say(seq.count_in ? "COUNT-IN" : "RECORDING", seq.count_in ? "then recording" : "latched");
        engine_transport(3);
        return;
    }
    if (ui.rec_armed) {
        ui.rec_armed = 0;
        if (proj.rsv[0] % 3u) {
            seq.count_in = (uint8_t)(proj.rsv[0] % 3u);
            seq_rec(&seq, 1);
            ui.rec_latched = 1;
        }
    }
    engine_transport(3);                         /* from a stop, always from the top: every track's step 1 */
}
#define REC_TAP_MS 400u                          /* REC held longer (to see its knobs): not a tap */
static void rec_tap(void)                        /* REC released, unused, while stopped */
{
    int trk = sel_trk();
    if (plat_ms() - ui.btn_t[B_REC] >= REC_TAP_MS)
        return;
    if (ui.maestro_rec) {
        ui.maestro_rec = 0;
        say("RECORD OFF", 0);
        return;
    }
    if ((ui.page == P_PLAYERS || ui.page == P_PLAYERS2 || ui.page == P_PLAYERS3) && pl_type(trk) == PL_STRUM) {
        ui.maestro_rec = 1;
        say("HOLD A CHORD", "for strum");
        return;
    }
    if (playing())
        return;
    ui.rec_armed = (uint8_t)!ui.rec_armed;
    if (ui.rec_armed) {
        step_rec_start();
        say("STEP RECORD", "keys write steps");
    } else
        say("RECORD OFF", 0);
}
static void rec_down(void)                       /* REC pressed while playing: momentary recording */
{
    if (ui.maestro_rec || !playing())
        return;
    if (ui.rec_latched) {                        /* latched: a press turns it off */
        rec_off();
        say("RECORD OFF", 0);
        ui.btn_used |= 1u << B_REC;
        return;
    }
    ui.rec_held = 1;
    seq_rec(&seq, 1);
    ui.btn_used |= 1u << B_REC;
}
static void rec_up(void)
{
    if (ui.rec_held && !ui.rec_latched)
        seq_rec(&seq, 0);
    ui.rec_held = 0;
}
/* REC held: the knobs are the record settings (turning one means the release does nothing else) */
static void rec_knob(int k, int32_t e)
{
    int trk = sel_trk();
    ui.btn_used |= 1u << B_REC;
    if (k == 0) {
        proj.metronome = (uint8_t)clampi(proj.metronome + e, 0, 2);
        engine_metronome(proj.metronome);
    } else if (k == 1)
        proj.rsv[0] = (uint8_t)clampi(proj.rsv[0] % 3u + e, 0, 2);
    else if (k == 2) {
        if (!is_seqt(trk))
            return;
        tpat(trk)->quant = (uint8_t)clampi(tpat(trk)->quant + e, 0, 100);
    } else {
        proj.rsv[2] = (uint8_t)(e > 0);
        seq.rec_replace = proj.rsv[2];
    }
    mark_dirty();
}

/* ------------------------------------------------------------ steps --- */
static void step_say(int s, int on)
{
    char t[24];
    num_s((uint32_t)s + 1u, s_cat(t, "step "));
    s_cat(t + s_len(t), on ? " on" : " off");
    if (ann_on()) {
        ann_say(t, 0, ANN_NOW);
        sp_said = 1;
    }
}
static void step_toggle(int i)
{
    int trk = sel_trk(), s = step_shown(i);
    step_t *st = &tpat(trk)->st[s];
    if (!is_seqt(trk))
        return;
    undo_keep();
    if (is_drum(trk)) {
        int on = 0;
        step_toggle_note(st, ui.dsound + 1);
        for (int k = 0; k < SN; k++)
            on |= st->note[k] == ui.dsound + 1;
        if (!st->vel)
            st->vel = 100;
        step_say(s, on);
    } else if (step_empty(st)) {
        uint8_t one = 0;
        if (trk == AUX_PUNCH) {
            if (!ui.rem_fx) {
                say_no("NO EFFECT YET", "play a key on EDIT");
                return;
            }
            one = ui.rem_fx;
        }
        if (one)
            step_set_notes(st, &one, 1, 100);
        else
            step_set_notes(st, ui.rem, ui.nrem ? ui.nrem : 1, 100);
        step_say(s, 1);
    } else {
        memset(st, 0, sizeof *st);
        st->vel = 100;
        step_say(s, 0);
    }
    if (s >= tpat(trk)->len)
        tpat(trk)->len = (uint8_t)(s + 1);
    mark_dirty();
}
static void step_length_to(int j)
{
    int trk = sel_trk(), s = step_shown(ui.held_first), e = step_shown(j);
    step_t *st = &tpat(trk)->st[s];
    char t[24];
    if (e <= s)
        return;
    undo_keep();
    st->len = (uint8_t)clampi((e - s + 1) * 4, 1, 64);
    ui.held_used = 1;
    num_s((uint32_t)(e - s + 1), s_cat(t, "to step "));
    say("LENGTH", t + 3);
    mark_dirty();
}
/* HOME + a step + white key c: the component on the steps held (again: off); a step holds two */
static void comp_toggle(int c)
{
    int trk = sel_trk(), any = 0;
    undo_keep();
    for (int i = 0; i < 16; i++)
        if (ui.cstep >> i & 1u) {
            step_t *st = &tpat(trk)->st[step_shown(i)];
            /* the interrupt reads comp then cval as two bytes: the kind goes off before its value changes and
             * on after, so it never pairs a kind with another's value */
            if (st->comp == c) {
                uint8_t k2 = st->comp2, v2 = st->cval2;
                st->comp = SC_NONE;
                st->comp2 = SC_NONE;
                st->cval2 = 0;
                st->cval = v2;
                st->comp = k2;
            } else if (st->comp2 == c) {
                st->comp2 = SC_NONE;
                st->cval2 = 0;
            } else if (!st->comp) {
                st->cval = SC_DEFAULT[c];
                st->comp = (uint8_t)c;
                any = 1;
            } else {
                st->comp2 = SC_NONE;                 /* (a third replaces the second) */
                st->cval2 = SC_DEFAULT[c];
                st->comp2 = (uint8_t)c;
                any = 1;
            }
        }
    ui.ccomp = (uint8_t)(any ? c : 0);
    say(any ? COMP_NAME[c] : "TRICK OFF", any ? "on the step" : COMP_NAME[c]);
    mark_dirty();
}
/* HOME + a step + black key b: the value of the component last added (0..8 = 1..9, 9 = random; black key 11 cycles
 * the skips' mode) */
static void comp_value(int b)
{
    int trk = sel_trk(), c = ui.ccomp, any = 0;
    char t[16];
    const char *vt = 0;
    undo_keep();
    for (int i = 0; i < 16; i++)
        if (ui.cstep >> i & 1u) {
            step_t *st = &tpat(trk)->st[step_shown(i)];
            uint8_t *v = 0;
            if (!c)                                  /* no component just added: the step's last one */
                c = st->comp2 ? st->comp2 : st->comp;
            if (st->comp2 == c)
                v = &st->cval2;
            else if (st->comp == c)
                v = &st->cval;
            if (!v)
                continue;
            if (c >= SC_SKIP_LOCK) {
                if (b == 10)
                    *v = SK_VAL(SK_N(*v), (SK_MODE(*v) + 1u) % 3u);
                else
                    *v = SK_VAL((uint32_t)(b == 9 ? SC_RANDOM_VAL : b), SK_MODE(*v));
            } else if (b < 10)
                *v = (uint8_t)b;
            vt = comp_val_text(c, *v, t);
            any = 1;
        }
    if (any)
        say(COMP_NAME[c % NSC], s_eq(vt, COMP_NAME[c % NSC]) ? "any" : vt);
    else
        say_no("NO TRICK", "white key first");
    mark_dirty();
}
static void step_knob(int k, int32_t e)
{
    int trk = sel_trk();
    undo_keep();
    for (int i = 0; i < 16; i++)
        if (ui.held >> i & 1u) {
            step_t *st = &tpat(trk)->st[step_shown(i)];
            if (k == 0)
                st->vel = (uint8_t)clampi(st->vel + e, 1, 127);
            else if (k == 1)
                st->len = (uint8_t)clampi(st->len + e, 0, 64);
            else if (k == 2) {
                int c = st->cond;
                c = c < COND_AB ? c + (int)e : COND_AB + clampi(c - COND_AB + (int)e, -1, COND_NAB);
                if (c < 0)
                    c = 0;
                if (c >= 100 && c < COND_AB)
                    c = COND_AB;
                if (c == COND_AB - 1 && e < 0)
                    c = 99;
                if (c >= COND_AB + COND_NAB)
                    c = COND_AB + COND_NAB - 1;
                st->cond = (uint8_t)c;
            } else
                st->nudge = (int8_t)clampi(st->nudge + e, -12, 12);
        }
    ui.held_used = 1;
    touch(k);
    mark_dirty();
}
static void step_nudge(int d)                    /* a step held + OCT-/OCT+ */
{
    char t[8], *e = t;
    step_knob(3, d);
    {
        const step_t *st = &tpat(sel_trk())->st[step_shown(ui.held_first)];
        if (st->nudge > 0)
            e = s_cat(e, "+");
        num_i(st->nudge, e);
    }
    say("NUDGE", t);
}
static void lock_knob(int k, int32_t e)
{
    int trk = sel_trk(), pt = cur_pat(trk), p = tp_page_param(ui.lockpg - 1, k, trk), ln;
    const tp_info_t *in;
    if (p < 0)
        return;
    in = tp_info_par(p, proj.trk[trk].par);
    ln = lane_find(pats.lane, trk, pt, p);
    if (ln < 0)
        ln = lane_alloc(pats.lane, trk, pt, p);
    if (ln < 0) {
        say_no("NO LANE LEFT", "clear locks first");
        return;
    }
    for (int i = 0; i < 16; i++)
        if (ui.held >> i & 1u) {
            int s = step_shown(i), v = pats.lane[ln].val[s];
            if (v == LOCK_NONE)
                v = proj.trk[trk].par[p];
            pats.lane[ln].val[s] = (uint8_t)clampi(v + e, in->lo, in->hi);
        }
    ui.held_used = 1;
    touch(k);
    mark_dirty();
}
static void step_clear_locks_held(void)
{
    int trk = sel_trk(), pt = cur_pat(trk);
    for (int i = 0; i < 16; i++)
        if (ui.held >> i & 1u)
            step_clear_locks(pats.lane, trk, pt, step_shown(i));
    ui.held_used = 1;
    say("LOCKS CLEARED", 0);
    mark_dirty();
}
/* step record: the cursor starts after the pattern's last written step (step 1 if it is empty or full) */
static void step_rec_start(void)
{
    int trk = sel_trk(), last = -1;
    const tpat_t *tp;
    ui.cursor = 0;
    ui.srec_down = 0;
    if (!is_seqt(trk))
        return;
    tp = tpat(trk);
    for (int i = 0; i < tp->len; i++)
        if (!step_empty(&tp->st[i]))
            last = i;
    if (last >= 0 && last < tp->len - 1)
        ui.cursor = (uint8_t)(last + 1);
    ui.bar = (uint8_t)(ui.cursor / 16);
}
/* a key while step record is armed: the first of a chord replaces the cursor step (a drum track adds its sound),
 * the others held with it join the same step; v is what the step holds (a note, a sound, an effect + 1, a
 * segment + 1) */
static void step_rec_key(int v)
{
    int trk = sel_trk();
    step_t *st;
    if (!is_seqt(trk) || v <= 0 || v > 255)
        return;
    if (ui.cursor >= tpat(trk)->len)
        ui.cursor = 0;
    st = &tpat(trk)->st[ui.cursor];
    undo_keep();
    if (!ui.srec_down && !is_drum(trk)) {
        memset(st, 0, sizeof *st);                    /* (the step's components and locks go with it: a new note) */
        st->vel = 100;
    }
    {
        int has = 0, k;
        for (k = 0; k < SN; k++)
            has |= st->note[k] == v;
        for (k = 0; !has && k < SN; k++)
            if (!st->note[k]) {
                st->note[k] = (uint8_t)v;
                break;
            }
        if (!st->vel)
            st->vel = 100;
    }
    ui.srec_down++;
    mark_dirty();
}
static void step_rec_move(int d)                  /* the cursor a step on (a rest, or the chord written) or back */
{
    int trk = sel_trk(), len;
    char t[16];
    if (!is_seqt(trk))
        return;
    len = tpat(trk)->len ? tpat(trk)->len : 1;
    ui.cursor = (uint8_t)((ui.cursor + d + len) % len);
    ui.bar = (uint8_t)(ui.cursor / 16);
    if (ann_on()) {
        num_s((uint32_t)ui.cursor + 1u, s_cat(t, "step "));
        ann_later(t, 0, 150u, plat_ms());
    }
}
static void step_rec_key_up(void)
{
    if (ui.srec_down && !--ui.srec_down)
        step_rec_move(1);
}

/* ------------------------------------------------------------- keys --- */
static void chord_add(int note)
{
    for (int i = 0; i < ui.nchord; i++)
        if (ui.chord[i] == note)
            return;
    if (ui.nchord < 8)
        ui.chord[ui.nchord++] = (uint8_t)note;
}
static void chord_del(int note)
{
    for (int i = 0; i < ui.nchord; i++)
        if (ui.chord[i] == note) {
            for (int j = i; j + 1 < ui.nchord; j++)
                ui.chord[j] = ui.chord[j + 1];
            ui.nchord--;
            return;
        }
}
/* a punch-in key: on the PUNCH track's page the octave's group; from an instrument track (HOME + key) the lower
 * octave aims at the selected track, the upper at its group */
static void punch_key(int k, int on)
{
    int fx = punch_of_key(k), trk = sel_trk(), target = -1;
    int latch = proj.trk[AUX_PUNCH].par[TP_E1] && trk == AUX_PUNCH && !home_held();
    if (fx < 0)
        return;
    if (trk != AUX_PUNCH) {
        if (k < 12)
            target = trk;
        else
            fx = (fx & 15) + (punch_group_of(&seq, trk) ? PX_MEL : 0);
    }
    if (on && trk == AUX_PUNCH)
        ui.rem_fx = (uint8_t)(fx + 1);
    if (latch) {
        if (!on)
            return;
        if (ui.punch_latched >> fx & 1u) {
            ui.punch_latched &= ~(1u << fx);
            engine_punch(fx, 0, -1);
        } else {
            ui.punch_latched |= 1u << fx;
            engine_punch(fx, 1, -1);
        }
        return;
    }
    engine_punch(fx, on, target);
}
static void scene_go(int n, int cued)
{
    int cur = cur_scene();
    char t[16];
    if (!pats.scene[n].used) {
        pats.scene[n] = pats.scene[cur];
        pats.scene[n].used = 1;
        mark_dirty();
        num_s((uint32_t)n + 1u, s_cat(t, "S"));
        say("NEW", t);
    }
    engine_scene(n, cued && playing());
    proj.scene = (uint8_t)n;
}
static void track_select(int t)
{
    t = (t % NALLTRK + NALLTRK) % NALLTRK;
    if (t == sel_trk())
        return;
    engine_keys_off(sel_trk());
    ui.nchord = 0;
    ui.held = 0;
    ui.cstep = 0;
    ui.lockpg = 0;
    proj.trk_sel = (uint8_t)t;
    if (!is_aux(t))
        ui.last_inst = (uint8_t)t;
    {                                            /* a track with fewer pages: back to the button's first */
        int g = page_group(ui.page);
        if (ui.page - PAGE_BTN[g].first >= group_n(g))
            ui.page = PAGE_BTN[g].first;
    }
    ui.touched = -1;
    if (ui.rec_armed)
        step_rec_start();
    if (ui.dsound >= DL_NSOUND)
        ui.dsound = 0;
}
static void solo_begin(int trk)
{
    ui.solo = (uint8_t)(trk + 1);
    ui.solo_mute = seq.mute;
    seq.mute = (uint16_t)(((1u << NSEQT) - 1u) & ~(1u << trk));
}
static void solo_end(void)
{
    if (!ui.solo)
        return;
    seq.mute = ui.solo_mute;
    ui.solo = 0;
}
/* the keys as a keyboard for the selected track (the play pages; Steps with SEQ held or while recording): they
 * play it and set what a step press writes; step record writes the cursor step */
static void keyboard_down(int k)
{
    int trk = sel_trk(), b = ordinal(BLACK_K, 11, k), srec = ui.rec_armed && !playing();
    if (trk == AUX_BRAIN) {
        ui.kmode[k] = KM_BRAIN;
        ui.knote[k] = (uint8_t)key_note(k);
        engine_brain_key(key_note(k), 1);
        chord_add(key_note(k));
        for (int i = 0; i < SN && i < ui.nchord; i++)
            ui.rem[i] = ui.chord[i];
        ui.nrem = (uint8_t)(ui.nchord > SN ? SN : ui.nchord);
        if (srec)
            step_rec_key(key_note(k));
        return;
    }
    if (trk == AUX_PUNCH) {
        ui.kmode[k] = KM_PUNCH;
        punch_key(k, 1);
        if (srec && punch_of_key(k) >= 0)
            step_rec_key(punch_of_key(k) + 1);
        return;
    }
    if (is_aux(trk))
        trk = inst_trk();                        /* FX tracks: the keys audition the instrument selected last */
    {
        int note = is_drum(trk) ? key_sound(k) : key_note(k);
        ui.kmode[k] = KM_PLAY;
        ui.knote[k] = (uint8_t)note;
        engine_key(trk, note, 1, 127);
        chord_add(note);
        if (ui.nchord > ui.nchord_max || ui.nchord == 1) {
            memcpy(ui.chord_max, ui.chord, 8);
            ui.nchord_max = ui.nchord;
        }
        if (is_drum(trk)) {                      /* a drum key: its sound is the one shown (Steps with SEQ held; a
                                                  * black key on the sound pages picks the key the knobs edit) */
            if ((ui.page == P_STEPS || b >= 0) && ui.dsound != note - 1) {
                ui.dsound = (uint8_t)(note - 1);
                if (ui.page != P_STEPS || !playing())
                    say("KEY", DL_SOUND_NAME[ui.dsound]);
            }
        } else if (!is_aux(sel_trk())) {
            for (int i = 0; i < SN && i < ui.nchord; i++)
                ui.rem[i] = ui.chord[i];
            ui.nrem = (uint8_t)(ui.nchord > SN ? SN : ui.nchord);
        }
        if (srec && !is_aux(sel_trk()))
            step_rec_key(note);
    }
}
static void key_down(int k)
{
    int trk = sel_trk(), w = ordinal(WHITE_K, 16, k), b = ordinal(BLACK_K, 11, k), p = ui.page;
    ui.kmode[k] = KM_NONE;
    ui.key_t[k] = plat_ms();
    if (ui.ask)
        return;
    if (ui.btn >> B_SAVE & 1u) {
        ui.kmode[k] = KM_SAVE;
        ui.knote[k] = (uint8_t)(w < 0 ? 255 : w);
        return;
    }
    if (lst.open)
        return;
    if (home_held()) {                           /* the shift layer */
        if ((p == P_STEPS || p == P_BAR) && is_seqt(trk)) {
            if (w >= 0 && !ui.cstep) {
                ui.kmode[k] = KM_CSTEP;
                ui.cstep = (uint16_t)(1u << w);
                ui.held_first = (uint8_t)w;
                ui.ccomp = 0;
                ui.touched = -1;
            } else if (w >= 0 && w < NSC - 1) {
                ui.kmode[k] = KM_COMP;
                comp_toggle(w + 1);
            } else if (b >= 0 && ui.cstep) {
                ui.kmode[k] = KM_COMP;
                comp_value(b);
            }
            return;
        }
        if (p == P_ARRANGE && w >= 0) {
            ui.kmode[k] = KM_SCENE;
            ui.knote[k] = (uint8_t)w;
            scene_go(w, 1);
            return;
        }
        if (p == P_MIX && b >= 0 && b < NSEQT) {
            ui.kmode[k] = KM_MUTE;
            set_mute(b, !(seq.mute >> b & 1u));
            say(seq.mute >> b & 1u ? "MUTED" : "UNMUTED", TRACK_NAME[b]);
            return;
        }
        if (k < 24 && p != P_SONG) {
            ui.kmode[k] = KM_PUNCH;
            punch_key(k, 1);
        }
        return;
    }
    if (p == P_ARRANGE || p == P_SONG) {
        if (w >= 0) {
            if (p == P_SONG) {
                song_t *sg = &pats.song;
                int at = ui.name_slot <= sg->len ? ui.name_slot : sg->len;
                if (sg->len >= NSONG) {
                    say_no("SONG FULL", "64 slots");
                    return;
                }
                for (int i = sg->len; i > at; i--)
                    sg->slot[i] = sg->slot[i - 1];
                sg->slot[at].scene = (uint8_t)w;
                sg->slot[at].rep = 1;
                sg->len++;
                ui.name_slot = (uint8_t)(at + 1);
                mark_dirty();
                {
                    char t[16];
                    num_s((uint32_t)w + 1u, s_cat(t, "S"));
                    say("INSERTED", t);
                }
                return;
            }
            ui.kmode[k] = KM_SCENE;
            ui.knote[k] = (uint8_t)w;
            scene_go(w, 0);
        } else if (b >= 0 && b < NSEQT) {
            ui.kmode[k] = KM_MUTE;
            set_mute(b, !(seq.mute >> b & 1u));
            say(seq.mute >> b & 1u ? "MUTED" : "UNMUTED", TRACK_NAME[b]);
        }
        return;
    }
    if (p == P_BAR) {
        if (b >= 0 && b < 9 && is_seqt(trk)) {
            undo_keep();
            tpat(trk)->scale = BAR_SCALE_KEY[b];
            ui.kmode[k] = KM_SCALE;
            say("SCALE", TS_NAME[BAR_SCALE_KEY[b]]);
            mark_dirty();
        }
        return;
    }
    if (p == P_STEPS) {
        if (!is_seqt(trk))
            return;
        if ((ui.btn >> B_SEQ & 1u) || ui.rec_armed || seq.rec || seq.rec_wait) {   /* a keyboard: SEQ held, recording */
            keyboard_down(k);
            return;
        }
        if (w >= 0) {
            if (ui.held) {
                ui.kmode[k] = KM_LENGTH;
                step_length_to(w);
                return;
            }
            ui.kmode[k] = KM_STEP;
            ui.held = (uint16_t)(1u << w);
            ui.held_first = (uint8_t)w;
            ui.held_used = 0;
            ui.touched = -1;
            return;
        }
        if (b >= 0 && ui.held && is_drum(trk)) {
            int s = step_shown(ui.held_first), on = 0;
            step_t *st = &tpat(trk)->st[s];
            undo_keep();
            step_toggle_note(st, b + 1);
            for (int i = 0; i < SN; i++)
                on |= st->note[i] == b + 1;
            if (!st->vel)
                st->vel = 100;
            ui.kmode[k] = KM_STEPSOUND;
            ui.held_used = 1;
            say(DL_SOUND_NAME[b], on ? "on the step" : "off the step");
            mark_dirty();
            return;
        }
        if (b >= 0 && is_drum(trk) && !ui.held) {   /* a drum track: pick (and hear) the sound */
            ui.kmode[k] = KM_SOUND;
            ui.dsound = (uint8_t)b;
            engine_key(trk, b + 1, 1, 127);
            ui.knote[k] = (uint8_t)(b + 1);
            return;
        }
        ui.kmode[k] = KM_IDLE;                   /* (the other tracks' black keys: nothing, ALGORITHM picks a track) */
        return;
    }
    if (p == P_MIX) {
        if (b >= 0) {
            static const uint8_t T[11] = {0, 1, 2, 3, 4, 5, 6, 7, AUX_FX1, AUX_FX2, 0xFF};
            if (T[b] == 0xFF)
                return;
            ui.kmode[k] = KM_SOLO;
            track_select(T[b]);
            if (T[b] < NSEQT && !ui.solo)
                solo_begin(T[b]);
        }
        return;
    }
    if (p >= P_MASTER)
        return;
    if (trk == AUX_BRAIN && p == P_PLAY) {       /* the brain's routing page: black keys toggle the followers */
        if (b >= 0 && b < NTRK) {
            ui.kmode[k] = KM_ROUTE;
            set_tp(b, TP_BRAIN, proj.trk[b].par[TP_BRAIN] ? 0 : 1);
            say(TRACK_NAME[b], proj.trk[b].par[TP_BRAIN] ? "follows" : "on its own");
        }
        return;
    }
    keyboard_down(k);
}
static void key_up(int k)
{
    int trk = sel_trk(), m = ui.kmode[k];
    ui.kmode[k] = KM_NONE;
    switch (m) {
    case KM_PLAY:
        engine_key(is_aux(trk) ? inst_trk() : trk, ui.knote[k], 0, 0);
        chord_del(ui.knote[k]);
        if (ui.rec_armed && !playing())
            step_rec_key_up();
        if (!ui.nchord) {
            if (ui.maestro_rec && !is_aux(trk)) {   /* the chord as it was at its largest (up to 8 keys) */
                seq_maestro_record(&seq, trk, ui.chord_max, ui.nchord_max);
                ui.maestro_rec = 0;
                say("CHORD RECORDED", 0);
            }
        }
        break;
    case KM_STEP: {
        int w = ordinal(WHITE_K, 16, k);
        if (w < 0)
            break;
        if (!ui.held_used)
            step_toggle(w);
        ui.held &= (uint16_t)~(1u << w);
        if (!ui.held)
            ui.lockpg = 0;
        else
            for (int i = 0; i < 16; i++)
                if (ui.held >> i & 1u) {
                    ui.held_first = (uint8_t)i;
                    break;
                }
        ui.touched = -1;
        break;
    }
    case KM_CSTEP: {
        int w = ordinal(WHITE_K, 16, k);
        if (w >= 0)
            ui.cstep &= (uint16_t)~(1u << w);
        ui.touched = -1;
        break;
    }
    case KM_PUNCH:
        punch_key(k, 0);
        if (ui.rec_armed && !playing() && trk == AUX_PUNCH && !home_held())
            step_rec_key_up();
        break;
    case KM_BRAIN:
        engine_brain_key(ui.knote[k], 0);
        chord_del(ui.knote[k]);
        if (ui.rec_armed && !playing())
            step_rec_key_up();
        break;
    case KM_SOUND:
        engine_key(trk, ui.knote[k], 0, 0);
        break;
    case KM_SOLO:
        if (ui.solo) {
            int any = 0;
            for (int i = 0; i < NKEYS; i++)
                any |= ui.kmode[i] == KM_SOLO;
            if (!any)
                solo_end();
        }
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------- buttons --- */
static void list_open(void);
static void list_sel(void);
static void ask_end(int yes);
static int save_project(int slot);
static void save_user_preset(int slot);
static void page_button(int b)
{
    for (uint32_t i = 0; i < NPAGE_BTN; i++)
        if (PAGE_BTN[i].btn == b) {
            if (is_aux(sel_trk()) && (b == B_ENV || b == B_LFO || b == B_ARP)) {
                say_no(TRACK_NAME[sel_trk()], "EDIT has its page");
                return;
            }
            ui.page = (uint8_t)(b == B_HOME && ui.page == P_ARRANGE ? P_SONG : PAGE_BTN[i].first);
            ui.touched = -1;
            ui.held = 0;
            ui.cstep = 0;
            ui.lockpg = 0;
            return;
        }
}
static void page_turn(int d)
{
    int g = page_group(ui.page), f = PAGE_BTN[g].first, n = group_n(g);
    ui.page = (uint8_t)(f + ((ui.page - f + d) % n + n) % n);
    ui.touched = -1;
}
static void button_down(int b)
{
    if (b == B_PLAY)
        play_press();
}
static void button_up(int b)
{
    if (lst.open && b == PAGE_BTN[page_group(ui.page)].btn && b != B_HOME) {
        ui.ask = 0;
        lst.open = 0;
        return;
    }
    if (b != B_HOME && b != B_SEL && b != B_SAVE && b != B_REC && b != B_PLAY && b != B_OCTDN && b != B_OCTUP) {
        ui.ask = 0;
        lst.open = 0;
        page_button(b);
        return;
    }
    switch (b) {
    case B_REC:
        rec_tap();
        break;
    case B_SEL:
        if (ui.ask)
            ask_end(1);
        else if (lst.open)
            list_sel();
        else
            list_open();
        break;
    case B_HOME:
        if (ui.ask)
            ask_end(0);
        else if (lst.open)
            lst.open = 0;
        else
            page_button(B_HOME);
        break;
    case B_SAVE:
        if (save_project(proj_slot) == 0)
            say("SAVED", 0);
        break;
    case B_OCTDN: case B_OCTUP: {
        int d = b == B_OCTUP ? 1 : -1;
        char t[8];
        if (ui.held && ui.page == P_STEPS && is_seqt(sel_trk())) {   /* a step held: its nudge */
            step_nudge(d);
            break;
        }
        if (ui.rec_armed && !playing() && is_seqt(sel_trk())) {   /* OCT+ a rest, OCT- back a step */
            step_rec_move(d);
            num_s((uint32_t)ui.cursor + 1u, t);
            say(d > 0 ? "REST" : "BACK", t);
            break;
        }
        if (ui.page == P_STEPS || ui.page == P_BAR) {
            ui.bar = (uint8_t)clampi(ui.bar + d, 0, NBAR - 1);
            num_s((uint32_t)ui.bar + 1u, t);
            say("BAR", t);
            break;
        }
        ui.oct = (int8_t)clampi(ui.oct + d, -4, 4);
        t[0] = ui.oct < 0 ? '-' : '+';
        t[1] = (char)('0' + (ui.oct < 0 ? -ui.oct : ui.oct));
        t[2] = 0;
        say("OCTAVE", t);
        break;
    }
    default:
        break;
    }
}
#define CARD_MS 900u
#define KEY_LONG_MS 900u
static void act_clear_scene(void);
static void holds(void)
{
    uint32_t now = plat_ms();
    for (int b = 0; b < NB; b++)
        if (ui.btn == 1u << b && !ui.keys && b != B_HOME && !(ui.btn_used >> b & 1u) && now - ui.btn_t[b] >= CARD_MS) {
            ui.card = (uint8_t)(b + 1);
            ui.btn_used |= 1u << b;
        }
    if (home_held() && now - ui.btn_t[B_HOME] >= CARD_MS)
        ui.btn_used |= 1u << B_HOME;
    for (int k = 0; k < NKEYS; k++)
        if ((ui.keys >> k & 1u) && ui.kmode[k] == KM_SCENE && now - ui.key_t[k] >= KEY_LONG_MS) {
            char q[32];
            ui.kmode[k] = KM_NONE;
            ui.ask_arg = ui.knote[k];
            num_s((uint32_t)ui.ask_arg + 1u, s_cat(q, "CLEAR SCENE "));
            s_cat(q + s_len(q), "?");
            {
                uint32_t i;
                for (i = 0; i < sizeof ui.ask_q - 1u && q[i]; i++)
                    ui.ask_q[i] = q[i];
                ui.ask_q[i] = 0;
            }
            ui.ask = 1;
            ui.ask_yes = act_clear_scene;
            ui.ask_no = 0;
            ui.msg_until = now;
        }
}

/* -------------------------------------------------------------- knobs --- */
static void arrange_knob(int k, int32_t e)
{
    int trk = sel_trk();
    if (k == 0)
        set_tempo_x10((int)proj.tempo_x10 + (home_held() ? (int)e : (int)e * 10));
    else if (k == 1)
        set_groove_type(proj.groove + (int)e);
    else if (k == 2) {
        if (!is_seqt(trk))
            return;
        undo_keep();
        tpat(trk)->groove = (int8_t)clampi(tpat(trk)->groove + e, -99, 99);
        mark_dirty();
    } else {
        proj.metronome = (uint8_t)clampi(proj.metronome + e, 0, 2);
        engine_metronome(proj.metronome);
        mark_dirty();
    }
}
static void song_knob(int k, int32_t e)
{
    song_t *sg = &pats.song;
    int cur = ui.name_slot <= sg->len ? ui.name_slot : sg->len;
    if (k == 0) {
        ui.name_slot = (uint8_t)clampi(cur + e, 0, sg->len);
        return;
    }
    if (k == 3) {
        sg->loop = (uint8_t)(e > 0);
        mark_dirty();
        return;
    }
    if (cur >= sg->len)
        return;
    if (k == 1)
        sg->slot[cur].scene = (uint8_t)clampi(sg->slot[cur].scene + e, 0, NSCENE - 1);
    else
        sg->slot[cur].rep = (uint8_t)clampi(sg->slot[cur].rep + e, 1, SONG_BARS);
    mark_dirty();
}
static int cued_pat(int trk)                     /* the pattern cued to come next, or the one playing */
{
    return playing() && seq.pcue[trk] < NPATT ? seq.pcue[trk] : cur_pat(trk);
}
static void pattern_pick(int trk, int p)
{
    p = clampi(p, 0, NPATT - 1);
    if (p == cued_pat(trk))
        return;
    pats.scene[cur_scene()].pat[trk] = (uint8_t)p;
    if (playing() && p == cur_pat(trk))
        seq.pcue[trk] = 0xFF;                    /* back to the one playing: nothing to cue */
    else
        seq_cue_pattern(&seq, trk, p);
    ui.bar = 0;
    mark_dirty();
}
static void steps_knob(int k, int32_t e)
{
    int trk = sel_trk();
    tpat_t *tp = tpat(trk);
    if (!is_seqt(trk))
        return;
    if (k == 0) {
        pattern_pick(trk, cued_pat(trk) + (int)e);
        return;
    }
    undo_keep();
    if (k == 1) {
        tp->len = (uint8_t)clampi(tp->len + e, 1, NSTEP);
        if (ui.bar * 16 >= tp->len)
            ui.bar = (uint8_t)((tp->len - 1) / 16);
    } else if (k == 2)
        tp->scale = (uint8_t)clampi(tp->scale + e, 0, NTS - 1);
    else
        tp->groove = (int8_t)clampi(tp->groove + e, -99, 99);
    mark_dirty();
}
static void bar_knob(int k, int32_t e)
{
    int trk = sel_trk();
    tpat_t *tp = tpat(trk);
    if (!is_seqt(trk))
        return;
    undo_keep();
    if (k == 0)
        tp->quant = (uint8_t)clampi(tp->quant + e, 0, 100);
    else if (k == 1)
        tp->nlen = (uint8_t)clampi(tp->nlen + e, 0, 100);
    else if (k == 2)
        tp->groove = (int8_t)clampi(tp->groove + e, -99, 99);
    else
        tp->shape = (uint8_t)clampi(tp->shape + e, 0, 100);
    mark_dirty();
}
static void master_knob(int k, int32_t e)
{
    int m = (ui.page - P_MASTER) * 4 + k;
    proj.master[m] = (uint8_t)clampi(proj.master[m] + e, 0, 127);
    engine_master(m, proj.master[m]);
    mark_dirty();
}
static void setup_knob(int k, int32_t e)
{
    if (k == 0)
        proj.lights = (uint8_t)clampi(proj.lights + e, 0, 2);
    else if (k == 1)
        seq.send_clock = proj.send_clock = (uint8_t)(e > 0);
    else if (k == 2)
        seq.send_notes = proj.send_notes = (uint8_t)(e > 0);
    else {
        proj.one_core = (uint8_t)(e > 0);
        plat_cpu2_set(!proj.one_core);
    }
    mark_dirty();
}
static void fx_pick(int trk, int d)              /* the effect (the reverb's modes among them): only those the slot can do */
{
    int t = proj.trk[trk].par[TP_ENGINE] % NFXPICK;
    for (int tries = 0; tries < NFXPICK; tries++) {
        t = clampi(t + d, 0, NFXPICK - 1);
        if (engine_fx_has(trk - AUX_FX1, t))
            break;
    }
    if (engine_fx_has(trk - AUX_FX1, t)) {
        set_tp(trk, TP_ENGINE, t);
        say("EFFECT", fx_label(t));
    }
}
static void aux_knob(int trk, int k, int32_t e)
{
    const uint8_t *par = proj.trk[trk].par;
    int p = tp_page_param(ui.page, k, trk), hi = 127;
    if (p < 0)
        return;
    if (trk == AUX_BRAIN)
        hi = k == 0 ? 1 : k == 1 ? 11 : k == 2 ? NBS - 1 : 8;
    else if (trk == AUX_PUNCH)
        hi = 1;
    set_tp(trk, p, clampi(par[p] + e, 0, hi));
}
static void knob(int k, int32_t e)
{
    int trk = sel_trk(), p;
    e = accel(EN_K1 + k, e);
    touch(k);
    if ((ui.btn >> B_REC & 1u) && !home_held()) {
        rec_knob(k, e);
        return;
    }
    if (ui.lockpg) {
        lock_knob(k, e);
        return;
    }
    if (ui.page == P_STEPS && ui.held) {
        step_knob(k, e);
        return;
    }
    if (ui.page == P_STEPS && ui.cstep)
        return;
    switch (ui.page) {
    case P_ARRANGE: arrange_knob(k, e); return;
    case P_SONG: song_knob(k, e); return;
    case P_STEPS: steps_knob(k, e); return;
    case P_BAR: bar_knob(k, e); return;
    case P_MASTER: case P_MASTER2: case P_MASTER3: master_knob(k, e); return;
    case P_SETUP: setup_knob(k, e); return;
    case P_STATUS: return;
    default: break;
    }
    if (fx_page(ui.page)) {                      /* an effect's knobs: the effect track's own */
        int ft = ui.page == P_FX1 ? AUX_FX1 : AUX_FX2;
        set_tp(ft, TP_E1 + k, proj.trk[ft].par[TP_E1 + k] + (int)e);
        return;
    }
    if (ui.page == P_SENDS) {
        trk = inst_trk();
        p = tp_page_param(P_SENDS, k, trk);
        if (p >= 0)
            set_tp(trk, p, proj.trk[trk].par[p] + (int)e);
        return;
    }
    if (ui.page == P_CHARACTER) {                /* the instrument's (an effect track selected: the last one played) */
        trk = inst_trk();
        p = tp_page_param(P_CHARACTER, k, trk);
        if (p < 0)
            return;
        set_tp(trk, p, proj.trk[trk].par[p] + (int)e);
        if (seq.rec && playing() && trk == sel_trk())
            seq_rec_knob(&seq, trk, p, proj.trk[trk].par[p]);
        return;
    }
    if (is_aux(trk) && ui.page != P_MIX) {
        aux_knob(trk, k, e);
        return;
    }
    p = tp_page_param(ui.page, k, trk);
    if (p < 0)
        return;
    set_tp(trk, p, proj.trk[trk].par[p] + (int)e);
    if (seq.rec && playing() && !is_aux(trk))
        seq_rec_knob(&seq, trk, p, proj.trk[trk].par[p]);
}
static void presets(int32_t e)
{
    int trk = sel_trk(), d = e > 0 ? 1 : -1;
    char t[24];
    if (lst.open) {
        list_sel();
        return;
    }
    switch (ui.page) {
    case P_ARRANGE:
        scene_go(((cur_scene() + d) % NSCENE + NSCENE) % NSCENE, 0);
        break;
    case P_SONG:
        song_knob(0, d);
        break;
    case P_STEPS: case P_BAR:
        if (is_drum(trk)) {
            ui.dsound = (uint8_t)((ui.dsound + d + DL_NSOUND) % DL_NSOUND);
            say("SOUND", DL_SOUND_NAME[ui.dsound]);
        } else if (is_seqt(trk))
            pattern_pick(trk, cur_pat(trk) + d);
        break;
    case P_MIX:
        track_select(trk + d);
        break;
    case P_FX1: case P_FX2:
        fx_pick(ui.page == P_FX1 ? AUX_FX1 : AUX_FX2, d);
        break;
    case P_CHARACTER:                            /* the character's type */
        trk = inst_trk();
        if (!is_drum(trk)) {
            const uint8_t *par = proj.trk[trk].par;
            set_tp(trk, TP_CTYPE, (par[TP_CTYPE] + d + NCT) % NCT);
            say("CHARACTER", tp_info_par(TP_CTYPE, par)->names[par[TP_CTYPE] % NCT]);
        }
        break;
    case P_FILTER: case P_KITF:                  /* the filter type (a kit: the key's; Kit filter: the bus on / off) */
        if (!is_aux(trk)) {
            const uint8_t *par = proj.trk[trk].par;
            int q = ftype_param(trk, ui.page), n = tp_info_par(q, par)->hi + 1;
            if (is_drum(trk) && q == TP_FTYPE) {
                set_tp(trk, TP_FTYPE, par[TP_FTYPE] == FT_DYNAMIC ? FT_LP24 : FT_DYNAMIC);
                say("KIT FILTER", par[TP_FTYPE] == FT_DYNAMIC ? "on" : "off");
                break;
            }
            set_tp(trk, q, (par[q] + d + n) % n);
            say("FILTER", tp_info_par(q, par)->names[par[q] % n]);
        }
        break;
    default:
        if (is_fx(trk))
            fx_pick(trk, d);
        else if (is_drum(trk) && (ui.page == P_ENGINE || ui.page == P_PLAY || ui.page == P_ENV)) {
            ui.dsound = (uint8_t)((ui.dsound + d + DL_NSOUND) % DL_NSOUND);
            say("KEY", DL_SOUND_NAME[ui.dsound]);
        } else if (!is_aux(trk) && ui.page >= P_ENGINE && ui.page <= P_PLAYERS3)
            preset_step(trk, d);
        (void)t;
        break;
    }
}

/* ------------------------------------------------------------- lists --- */
static void ask(const char *q, void (*yes)(void))
{
    uint32_t i;
    for (i = 0; i < sizeof ui.ask_q - 1u && q[i]; i++)
        ui.ask_q[i] = q[i];
    ui.ask_q[i] = 0;
    ui.ask = 1;
    ui.ask_yes = yes;
    ui.ask_no = 0;
    ui.msg_until = plat_ms();
}
static void ask_end(int yes)
{
    void (*f)(void) = ui.ask_yes, (*n)(void) = ui.ask_no;
    ui.ask = 0;
    ui.ask_yes = 0;
    ui.ask_no = 0;
    if (yes) {
        lst.open = 0;
        if (f)
            f();
    } else if (n)
        n();
}
static void list_open(void)
{
    int a;
    lst.open = 1;
    lst.sel = lst.top = 0;
    ui.pick_engine = proj.trk[sel_trk()].par[TP_ENGINE] % NENGINE;
    if (is_fx(sel_trk()))
        ui.pick_fx = proj.trk[sel_trk()].par[TP_ENGINE] % NFXPICK;
    if (fx_page(ui.page))
        ui.pick_fx = proj.trk[ui.page == P_FX1 ? AUX_FX1 : AUX_FX2].par[TP_ENGINE] % NFXPICK;
    list_build();
    if (!lst.n) {
        lst.open = 0;
        say_no("LIST", "nothing here");
        return;
    }
    for (a = 0; a < lst.n && lst.row[a].kind != LK_ACTION && lst.row[a].kind != LK_PICK; a++)
        ;
    if (a >= lst.n)
        return;
    lst.sel = (uint8_t)a;
    lst.top = (uint8_t)(lst.n - a >= LIST_SHOWN ? a : lst.n > LIST_SHOWN ? lst.n - LIST_SHOWN : 0);
}
static void list_move(int d)
{
    int s = lst.sel;
    list_build();
    s = clampi(s + d, 0, lst.n ? lst.n - 1 : 0);
    lst.sel = (uint8_t)s;
    if (lst.sel < lst.top)
        lst.top = lst.sel;
    if (lst.sel >= lst.top + LIST_SHOWN)
        lst.top = (uint8_t)(lst.sel - LIST_SHOWN + 1);
}
static int list_follow_trk(void)
{
    const char *nm = lst.row[lst.sel].name;
    if (nm[0] == 'T' && nm[1] >= '1' && nm[1] <= '8' && nm[2] == ' ')
        return nm[1] - '1';
    return sel_trk();
}
static void list_change(int32_t e)
{
    int d = e > 0 ? 1 : -1;
    list_build();
    if (!lst.n)
        return;
    switch (lst.row[lst.sel].kind) {
    case LK_KNOB: {
        uint8_t keep = ui.page;
        ui.page = (uint8_t)(lst.row[lst.sel].id >> 2);
        knob(lst.row[lst.sel].id & 3, e);
        ui.page = keep;
        ui.touched = -1;
        break;
    }
    case LK_PICK:
        switch (lst.row[lst.sel].id) {
        case A_ENGINE: ui.pick_engine = (uint8_t)clampi(ui.pick_engine + d, 0, NENGINE - 1); break;
        case A_WEAVE: ui.pick_weave = (uint8_t)clampi(ui.pick_weave + d, 0, NWV - 1); break;
        case A_EFFECT: {
            int t = ui.pick_fx, trk = fx_page(ui.page) ? (ui.page == P_FX1 ? AUX_FX1 : AUX_FX2) : sel_trk();
            for (int tries = 0; tries < NFXPICK; tries++) {
                t = clampi(t + d, 0, NFXPICK - 1);
                if (engine_fx_has(trk - AUX_FX1, t))
                    break;
            }
            if (engine_fx_has(trk - AUX_FX1, t))
                ui.pick_fx = (uint8_t)t;
            break;
        }
        case A_PRESET: ui.pick_preset = (uint8_t)((ui.pick_preset + d + NPRESETS) % (NPRESETS ? NPRESETS : 1)); break;
        case A_SAVE_PRESET: ui.pick_slot = (uint8_t)((ui.pick_slot + d + NUSER_PRESETS) % NUSER_PRESETS); break;
        case A_LOAD_PROJ: ui.pick_slot = (uint8_t)((ui.pick_slot + d + NPROJ) % NPROJ); break;
        case A_USB_AUDIO:
            if (proj.usb_full != (uint8_t)(d > 0)) {
                proj.usb_full = (uint8_t)(d > 0);
                mark_dirty();
            }
            break;
        case A_COUNT_IN:
            proj.rsv[0] = (uint8_t)clampi(proj.rsv[0] % 3u + d, 0, 2);
            mark_dirty();
            break;
        case A_THEME:
            proj.rsv[1] = (uint8_t)((proj.rsv[1] + NTHEMES + (uint32_t)d) % NTHEMES);
            mark_dirty();
            break;
        case A_PLAYMODE: {
            int t = sel_trk(), q = TP_DRUM(ui.dsound % DL_NSOUND, DP_MODE);
            set_tp(t, q, clampi(proj.trk[t].par[q] + d, 0, NPM - 1));
            break;
        }
        case A_FTYPE: {
            int t = sel_trk(), q = ftype_param(t, ui.page);
            if (is_drum(t) && q == TP_FTYPE)          /* the kit's bus: on (dynamic) / off */
                set_tp(t, TP_FTYPE, d > 0 ? FT_DYNAMIC : FT_LP24);
            else
                set_tp(t, q, clampi(proj.trk[t].par[q] + d, 0, tp_info_par(q, proj.trk[t].par)->hi));
            break;
        }
        case A_FX_HP: case A_FX_LP: case A_FX_SEND: {
            int ft = ui.page == P_FX2 ? AUX_FX2 : AUX_FX1, id = lst.row[lst.sel].id;
            int q = id == A_FX_HP ? TP_FENV : id == A_FX_LP ? TP_CUT : TP_SEND2;
            set_tp(ft, q, proj.trk[ft].par[q] + (int)accel(EN_ALGO, e));
            break;
        }
        case A_FOLLOW: {
            int t = list_follow_trk();
            set_tp(t, TP_BRAIN, (proj.trk[t].par[TP_BRAIN] % 3 + d + 3) % 3);
            break;
        }
        default: break;
        }
        break;
    default:
        break;
    }
}
static void act_clear_notes(void)
{
    tpat_t *tp = tpat(sel_trk());
    undo_keep();
    for (int i = 0; i < NSTEP; i++) {
        memset(&tp->st[i], 0, sizeof tp->st[i]);
        tp->st[i].vel = 100;
    }
    mark_dirty();
}
static void act_clear_locks(void)
{
    pattern_clear_locks(pats.lane, sel_trk(), cur_pat(sel_trk()));
    mark_dirty();
}
static void act_rotate(int d)
{
    tpat_t *tp = tpat(sel_trk());
    int trk = sel_trk(), pt = cur_pat(trk), n = tp->len;
    step_t keep;
    if (n < 2)
        return;
    undo_keep();
    if (d > 0) {
        keep = tp->st[n - 1];
        for (int i = n - 1; i > 0; i--)
            tp->st[i] = tp->st[i - 1];
        tp->st[0] = keep;
    } else {
        keep = tp->st[0];
        for (int i = 0; i + 1 < n; i++)
            tp->st[i] = tp->st[i + 1];
        tp->st[n - 1] = keep;
    }
    for (int l = 0; l < NLANE; l++)
        if (pats.lane[l].used && pats.lane[l].trk == trk && pats.lane[l].pat == pt) {
            uint8_t *v = pats.lane[l].val, k;
            if (d > 0) {
                k = v[n - 1];
                for (int i = n - 1; i > 0; i--)
                    v[i] = v[i - 1];
                v[0] = k;
            } else {
                k = v[0];
                for (int i = 0; i + 1 < n; i++)
                    v[i] = v[i + 1];
                v[n - 1] = k;
            }
        }
    mark_dirty();
}
/* the pattern woven with the copied one (pattern_weave): bars or steps in turn, or one's rhythm with the
 * other's notes; HOME + REC undoes it */
static void act_weave(void)
{
    static tpat_t out PLAT_POOL;
    static const char *const SAID[NWV] = {"bars in turn", "steps in turn", "copied notes", "copied rhythm"};
    int m = ui.pick_weave % NWV;
    undo_keep();
    pattern_weave(&out, tpat(sel_trk()), &clip, m);
    *tpat(sel_trk()) = out;
    if (ui.bar * 16 >= out.len)
        ui.bar = 0;
    mark_dirty();
    say("WOVEN", SAID[m]);
}
static void act_dup_bar(void)
{
    tpat_t *tp = tpat(sel_trk());
    int b = ui.bar;
    if (b >= NBAR - 1) {
        say_no("NO ROOM", "four bars at most");
        return;
    }
    undo_keep();
    for (int i = 0; i < 16; i++)
        tp->st[(b + 1) * 16 + i] = tp->st[b * 16 + i];
    if (tp->len < (b + 2) * 16)
        tp->len = (uint8_t)((b + 2) * 16);
    ui.bar = (uint8_t)(b + 1);
    mark_dirty();
    say("DUPLICATED", "bar");
}
static void act_scramble(void)                   /* a random sound: the engine stays (a drum key keeps its model) */
{
    int trk = sel_trk();
    uint8_t *par = proj.trk[trk].par;
    static const uint8_t P[] = {TP_E1, TP_E2, TP_E3, TP_E4, TP_CUT, TP_RES, TP_A, TP_D, TP_S, TP_R, TP_FENV, TP_LAMT};
    uint32_t r = plat_ms() * 2654435761u + ui.frame;
    int n = (int)sizeof P;
    if (is_drum(trk)) {
        int key = ui.dsound % DL_NSOUND;
        for (int k = 0; k < 4; k++) {
            const tp_info_t *in = tp_info_par(TP_DRUM(key, DP_K1 + k), par);
            r = r * 1664525u + 1013904223u;
            par[TP_DRUM(key, DP_K1 + k)] = (uint8_t)(in->lo + (int)((r >> 8) % (uint32_t)(in->hi - in->lo + 1)));
        }
        engine_set_all(trk);
        mark_dirty();
        return;
    }
    for (int i = 0; i < n; i++) {
        const tp_info_t *in = tp_info_par(P[i], par);
        int span = in->hi - in->lo, v;
        r = r * 1664525u + 1013904223u;
        v = in->lo + (int)((r >> 8) % (uint32_t)(span + 1));
        if (P[i] == TP_A || P[i] == TP_S)
            v = in->lo + (v - in->lo) / 2 + (P[i] == TP_S ? span / 3 : 0);
        par[P[i]] = (uint8_t)clampi(v, in->lo, in->hi);
    }
    memset(proj.trk[trk].name, 0, NAME_LEN);
    engine_set_all(trk);
    mark_dirty();
}
static void do_save_preset(void) { save_user_preset(ui.ask_arg); }
static void save_user_preset(int slot)
{
    int trk = sel_trk();
    char t[16];
    if (is_aux(trk))
        return;
    user_presets[slot].engine = proj.trk[trk].par[TP_ENGINE];
    memcpy(user_presets[slot].par, proj.trk[trk].par, NTP_SOUND);
    memcpy(user_presets[slot].name, proj.trk[trk].name, NAME_LEN);
    if (!user_presets[slot].name[0])
        s_cat(user_presets[slot].name, EG_NAME[proj.trk[trk].par[TP_ENGINE] % NENGINE]);
    if (presets_save() != 0) {
        say_no("NOT SAVED", "flash error");
        return;
    }
    num_s((uint32_t)slot + 1u, s_cat(t, "U"));
    say("PRESET SAVED", t);
}
static void act_save_preset(int slot)
{
    ui.ask_arg = (uint8_t)slot;
    if (user_presets[slot].name[0]) {
        char q[32];
        num_s((uint32_t)slot + 1u, s_cat(q, "REPLACE USER PRESET "));
        s_cat(q + s_len(q), "?");
        ask(q, do_save_preset);
    } else
        do_save_preset();
}
static void do_clear_song(void)
{
    memset(&pats.song, 0, sizeof pats.song);
    ui.name_slot = 0;
    mark_dirty();
    say("CLEARED", "song");
}
static void act_clear_scene(void)
{
    int n = ui.ask_arg % NSCENE;
    char t[16];
    if (n == cur_scene()) {
        say_no("SCENE IN USE", "pick another first");
        return;
    }
    pats.scene[n].used = 0;
    mark_dirty();
    num_s((uint32_t)n + 1u, s_cat(t, "S"));
    say("CLEARED", t);
}
static void act_clear_scene_ask(void)
{
    char q[32];
    ui.ask_arg = (uint8_t)cur_scene();
    num_s((uint32_t)cur_scene() + 1u, s_cat(q, "CLEAR SCENE "));
    s_cat(q + s_len(q), "? (it is playing)");
    ask(q, act_clear_scene);
}
static int save_project(int slot)
{
    int rc;
    if (playing() && slot != proj_slot) {
        say_no("STOP FIRST", "to change slots");
        return -1;
    }
    if (seq.rec || seq.rec_wait) {    /* a recording writes the patterns while they are packed: the copy would not match its CRC */
        say_no("STOP RECORDING", "to save");
        return -1;
    }
    rc = project_save(slot);
    if (rc != 0) {
        say_no("NOT SAVED", "flash error");
        return rc;
    }
    proj_slot = (uint8_t)slot;
    ui.dirty = 0;
    return 0;
}
static void do_save_slot(void)
{
    char t[16];
    if (save_project(ui.ask_arg) == 0) {
        num_s((uint32_t)ui.ask_arg + 1u, s_cat(t, "slot "));
        say("SAVED", t);
    }
}
static void act_save_slot(int slot)
{
    ui.ask_arg = (uint8_t)slot;
    if (project_exists(slot) && slot != proj_slot) {
        char q[32];
        num_s((uint32_t)slot + 1u, s_cat(q, "REPLACE PROJECT "));
        s_cat(q + s_len(q), "?");
        ask(q, do_save_slot);
    } else
        do_save_slot();
}
static void after_load(void)
{
    project_apply();
    apply_brain();
    engine_tempo(proj.tempo_x10);
    engine_groove(proj.groove);
    undo.valid = 0;
    ui.held = 0;
    ui.cstep = 0;
    ui.lockpg = 0;
    ui.bar = 0;
}
static void do_load_proj(void)
{
    int slot = ui.ask_arg % NPROJ;
    char t[16];
    engine_transport(0);
    engine_panic();
    if (project_load(slot) != 0)
        say_no("EMPTY SLOT", "defaults loaded");
    else {
        num_s((uint32_t)slot + 1u, s_cat(t, "slot "));
        say("LOADED", t);
    }
    proj_slot = (uint8_t)slot;
    after_load();
    ui.dirty = 0;
}
static void do_new_proj(void)
{
    engine_transport(0);
    engine_panic();
    project_defaults();
    after_load();
    ui.dirty = 1;
    say("NEW PROJECT", "unsaved");
}
static void act_remove_note(int row)             /* a step held: the row's note off the step */
{
    int trk = sel_trk();
    step_t *st = &tpat(trk)->st[step_shown(ui.held_first)];
    if (row < 0 || row >= SN || !st->note[row])
        return;
    undo_keep();
    step_toggle_note(st, st->note[row]);
    ui.held_used = 1;
    mark_dirty();
    say("REMOVED", 0);
}
static void action_run(int a)
{
    song_t *sg = &pats.song;
    int cur = ui.name_slot <= sg->len ? ui.name_slot : sg->len;
    switch (a) {
    case A_CLEAR_NOTES: act_clear_notes(); say("CLEARED", "notes"); break;
    case A_CLEAR_LOCKS: act_clear_locks(); say("CLEARED", "locks"); break;
    case A_CLEAR_BOTH: act_clear_notes(); act_clear_locks(); say("CLEARED", "notes and locks"); break;
    case A_COPY_PAT: clip = *tpat(sel_trk()); clip_ok = 1; say("COPIED", "pattern"); break;
    case A_PASTE_PAT: undo_keep(); *tpat(sel_trk()) = clip; mark_dirty(); say("PASTED", "pattern"); break;
    case A_WEAVE: act_weave(); break;
    case A_ROT_FWD: act_rotate(1); say("ROTATED", "forward"); break;
    case A_ROT_BACK: act_rotate(-1); say("ROTATED", "back"); break;
    case A_DUP_BAR: act_dup_bar(); break;
    case A_ENGINE: {                             /* another engine: its first sound */
        int f = preset_first(ui.pick_engine);
        set_tp(sel_trk(), TP_ENGINE, ui.pick_engine);
        if (f >= 0) {
            preset_apply(sel_trk(), f);
            ui.preset_i = (uint8_t)f;
        }
        if (ui.pick_engine == EG_SLICE)          /* its notes pick slices: the conductor's chords would move them */
            set_tp(sel_trk(), TP_BRAIN, 0);
        say("ENGINE", EG_NAME[ui.pick_engine % NENGINE]);
        break;
    }
    case A_EFFECT: {
        int ft = fx_page(ui.page) ? (ui.page == P_FX1 ? AUX_FX1 : AUX_FX2) : sel_trk();
        set_tp(ft, TP_ENGINE, ui.pick_fx % NFXPICK);
        say("EFFECT", fx_label(ui.pick_fx % NFXPICK));
        break;
    }
    case A_PRESET:
        if (NPRESETS) {
            load_preset(sel_trk(), &PRESETS[ui.pick_preset % NPRESETS]);
            ui.preset_i = ui.pick_preset;
            say("PRESET", PRESETS[ui.pick_preset % NPRESETS].name);
        }
        break;
    case A_SCRAMBLE: act_scramble(); say("SCRAMBLED", 0); break;
    case A_SAVE_PRESET: act_save_preset(ui.pick_slot % NUSER_PRESETS); return;
    case A_INIT:
        sound_default(proj.trk[sel_trk()].par, proj.trk[sel_trk()].par[TP_ENGINE]);
        memset(proj.trk[sel_trk()].name, 0, NAME_LEN);
        engine_set_all(sel_trk());
        mark_dirty();
        say("RESET", EG_NAME[proj.trk[sel_trk()].par[TP_ENGINE] % NENGINE]);
        break;
    case A_FOLLOW: case A_USB_AUDIO: case A_COUNT_IN: case A_THEME: case A_PLAYMODE: case A_FTYPE: case A_FX_HP: case A_FX_LP:
    case A_FX_SEND: return;                      /* (ALGORITHM sets them; SEL leaves the list) */
    case A_REMOVE_NOTE: act_remove_note(lst.sel); break;
    case A_INSERT_SLOT:
        if (sg->len >= NSONG) {
            say_no("SONG FULL", "64 slots");
            return;
        }
        for (int i = sg->len; i > cur; i--)
            sg->slot[i] = sg->slot[i - 1];
        sg->slot[cur].scene = (uint8_t)cur_scene();
        sg->slot[cur].rep = 1;
        sg->len++;
        mark_dirty();
        say("INSERTED", "this scene");
        break;
    case A_DELETE_SLOT:
        if (cur >= sg->len) {
            say_no("NO SLOT HERE", 0);
            return;
        }
        for (int i = cur; i + 1 < sg->len; i++)
            sg->slot[i] = sg->slot[i + 1];
        sg->len--;
        mark_dirty();
        say("DELETED", "slot");
        break;
    case A_CLEAR_SONG: ask("CLEAR THE SONG?", do_clear_song); return;
    case A_PLAY_SONG:
        ui.page = P_SONG;
        if (playing())
            engine_transport(0);
        play_press();
        break;
    case A_LOAD_PROJ:
        ui.ask_arg = ui.pick_slot % NPROJ;
        if (ui.dirty)
            ask("LOSE UNSAVED CHANGES?", do_load_proj);
        else
            do_load_proj();
        return;
    case A_NEW_PROJ: ask("NEW PROJECT?", do_new_proj); return;
    case A_CLEAR_SCENE: act_clear_scene_ask(); return;
    case A_PANIC: engine_panic(); say("ALL NOTES OFF", 0); break;
    default: break;
    }
    lst.open = 0;
}
static void list_sel(void)
{
    list_build();
    if (!lst.n)
        return;
    if (lst.row[lst.sel].kind == LK_ACTION || lst.row[lst.sel].kind == LK_PICK)
        action_run(lst.row[lst.sel].id);
}

/* ------------------------------------------------------------- input --- */
static void save_key(int k)
{
    int w = ui.knote[k];
    if (w > 15)
        return;
    if (ui.page >= P_ENGINE && ui.page <= P_PLAYERS3 && !is_aux(sel_trk())) {
        act_save_preset(w);
        return;
    }
    if (w >= NPROJ) {
        say_no("THREE SLOTS", "white keys 1 to 3");
        return;
    }
    act_save_slot(w);
}
static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), ch, i;
    int32_t e, ev[NE];
    ch = btn ^ ui.btn;
    ui.btn = btn;
    if (ch || keys != ui.keys)
        sp_in_t = plat_ms();
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        int hold = i != B_PLAY;
        if (!(ch & m))
            continue;
        if (btn & m) {
            ui.any_btn = 1;
            ui.card = 0;
            ui.btn_used |= btn & ~m;
            ui.btn_used &= ~m;
            ui.btn_t[i] = plat_ms();
            if (i == B_HOME && ui.ask) {
                ask_end(0);
                ui.btn_used |= m;
                continue;
            }
            if ((i == B_REC || i == B_PLAY) && home_held() && !ui.ask) {
                do_undo(i == B_PLAY);
                ui.btn_used |= m | 1u << B_HOME;
                continue;
            }
            if (i == B_SAVE && ui.held && ui.page == P_STEPS) {
                step_clear_locks_held();
                ui.btn_used |= m;
                continue;
            }
            if (ui.held && ui.page == P_STEPS && (i == B_EDIT || i == B_ENV || i == B_FX || i == B_LFO) &&
                !is_aux(sel_trk())) {
                ui.lockpg = (uint8_t)(1 + (i == B_EDIT ? P_ENGINE : i == B_ENV ? P_ENV
                                           : i == B_FX ? (is_drum(sel_trk()) ? P_SENDS : P_CHARACTER) : P_LFO));
                if (is_drum(sel_trk())) {           /* a kit's locks reach the key the step plays: the one shown
                                                     * if the step has it, else the step's first */
                    const step_t *st = &tpat(sel_trk())->st[step_shown(ui.held_first)];
                    int has = 0, first = -1;
                    for (int k = 0; k < SN; k++)
                        if (st->note[k] >= 1 && st->note[k] <= DL_NSOUND) {
                            has |= st->note[k] == ui.dsound + 1;
                            if (first < 0)
                                first = st->note[k] - 1;
                        }
                    if (!has && first >= 0)
                        ui.dsound = (uint8_t)first;
                }
                ui.held_used = 1;
                ui.touched = -1;
                ui.btn_used |= m;
                continue;
            }
            if (i == B_REC && playing() && !ui.ask) {
                rec_down();
                continue;
            }
            if (!hold)
                button_down((int)i);
        } else {
            if (ui.card == i + 1u)
                ui.card = 0;
            if (i == B_REC && ui.rec_held) {
                rec_up();
                continue;
            }
            if (hold && !(ui.btn_used & m))
                button_up((int)i);
        }
    }
    if ((btn & (1u << B_OCTDN | 1u << B_OCTUP)) == (1u << B_OCTDN | 1u << B_OCTUP))
        ui.btn_used |= 1u << B_OCTDN | 1u << B_OCTUP;
    if (!home_held() && ui.cstep) {              /* HOME let go: the component selection ends */
        ui.cstep = 0;
        ui.touched = -1;
    }
    holds();
    ch = keys ^ ui.keys;
    ui.keys = keys;
    for (i = 0; i < NKEYS; i++)
        if (ch >> i & 1u) {
            if (keys >> i & 1u) {
                ui.card = 0;
                ui.btn_used |= btn;
                key_down((int)i);
            } else {
                if (ui.kmode[i] == KM_SAVE) {
                    ui.kmode[i] = KM_NONE;
                    save_key((int)i);
                } else
                    key_up((int)i);
            }
        }
    for (i = 0; i < NE; i++)
        if ((ev[i] = plat_enc((int)i)) != 0) {
            sp_in_t = plat_ms();
            ui.card = 0;
            ui.btn_used |= btn;
        }
    if (btn || keys || ch)
        ui.act_t = plat_ms();
    if (ui.ask)
        return;
    if ((e = ev[EN_SELECT]) != 0 && ui.lockpg) {  /* the lock editor: the module's other pages (the track's own) */
        int g = page_group(ui.lockpg - 1), f = PAGE_BTN[g].first, n = group_n(g), pg = ui.lockpg - 1 - f;
        if (PAGE_BTN[g].btn == B_FX) {           /* the track's own: Character and Sends (a kit: Sends) */
            f = is_drum(sel_trk()) ? P_SENDS : P_CHARACTER;
            n = is_drum(sel_trk()) ? 1 : 2;
            pg = ui.lockpg - 1 - f;
        }
        pg = ((pg + (e > 0 ? 1 : -1)) % n + n) % n;
        ui.lockpg = (uint8_t)(f + pg + 1);
        e = 0;
    }
    if (e) {
        if (home_held())
            set_tempo_x10((int)proj.tempo_x10 + accel(EN_SELECT, e) * 10);
        else if (lst.open)
            list_move(e);
        else
            page_turn(e > 0 ? 1 : -1);
    }
    if ((e = ev[EN_ALGO]) != 0) {
        if (lst.open)
            list_change(e);
        else
            track_select(sel_trk() + (int)e);
    }
    if ((e = ev[EN_PRESET]) != 0) {
        if (home_held())
            scene_go(((cur_scene() + (e > 0 ? 1 : -1)) % NSCENE + NSCENE) % NSCENE, 0);
        else
            presets(e);
    }
    for (i = 0; i < 4; i++)
        if ((e = ev[EN_K1 + i]) != 0) {
            if (lst.open && !ui.card)
                continue;
            knob((int)i, e);
        }
}

/* ------------------------------------------------------------- lights --- */
static void leds(void)
{
    uint32_t b = 0, k = 0, dim = 0, i;
    int trk = sel_trk(), rec = recording();
    if (playing())
        b |= 1u << B_PLAY;
    if (rec)
        b |= 1u << B_REC;
    if (ui.dirty)
        b |= 1u << B_SAVE;
    for (i = 0; i < NPAGE_BTN; i++)
        if (ui.page >= PAGE_BTN[i].first && ui.page < PAGE_BTN[i].first + PAGE_BTN[i].n)
            b |= 1u << PAGE_BTN[i].btn;
    if (proj.lights) {
        if ((ui.page == P_STEPS || ui.page == P_BAR) && is_seqt(trk) && ui.cstep) {   /* HOME + a step: its components */
            const step_t *st = &tpat(trk)->st[step_shown(ui.held_first)];
            if (st->comp)
                k |= 1u << WHITE_K[st->comp - 1];
            if (st->comp2)
                k |= 1u << WHITE_K[st->comp2 - 1];
            k |= ui.keys;
        } else if ((ui.page == P_STEPS || ui.page == P_BAR) && is_seqt(trk) && !home_held()) {
            const tpat_t *tp = tpat(trk);
            int ph = eng_step[trk];
            for (i = 0; i < 16; i++) {
                int s = ui.bar * 16 + (int)i;
                if (step_on(&tp->st[s], trk) && s < tp->len)
                    dim |= 1u << WHITE_K[i];
                if (playing() && s == ph)
                    k |= 1u << WHITE_K[i];
            }
            if (is_drum(trk))
                k |= 1u << BLACK_K[ui.dsound % 11];
            k |= ui.keys;
        } else if (ui.page == P_ARRANGE && !home_held()) {
            for (i = 0; i < NSCENE; i++)
                if (pats.scene[i].used)
                    dim |= 1u << WHITE_K[i];
            k |= 1u << WHITE_K[cur_scene()];
            for (i = 0; i < NSEQT; i++)
                if (seq.mute >> i & 1u)
                    dim |= 1u << BLACK_K[i];
        } else
            k |= ui.keys;
        if (home_held() && ui.page != P_STEPS && ui.page != P_BAR) {
            for (i = 0; i < 24; i++) {
                int fx = punch_of_key((int)i);
                if (fx >= 0 && (fx < PX_MEL ? (eng_punch_drum >> fx & 1u) : (eng_punch_mel >> (fx - PX_MEL) & 1u)))
                    k |= 1u << i;
            }
        }
    }
    loom_lights(b, k, proj.lights == 1, dim & ~k, rec);
}

/* ------------------------------------------------------ screen reader --- */
#define SP_IN_MS 400u
static struct {
    uint32_t page, trk, kh[4], lh;
    int16_t tempo;
    int8_t playing;
    uint8_t started, step, scene;
} sp PLAT_POOL;
static char sp_buf[ANN_MAX + 1] PLAT_POOL;
static uint32_t sp_n;
static void sp_cat(const char *s)
{
    while (s && *s && sp_n < ANN_MAX)
        sp_buf[sp_n++] = *s++;
    sp_buf[sp_n] = 0;
}
static void sp_int(int v)
{
    char b[12];
    if (v < 0) {
        sp_cat("minus ");
        v = -v;
    }
    num_s((uint32_t)v, b);
    sp_cat(b);
}
static void sp_end(const char *s)
{
    while (sp_n && sp_buf[sp_n - 1] == ' ')
        sp_n--;
    sp_buf[sp_n] = 0;
    if (sp_n && sp_buf[sp_n - 1] != '.' && sp_buf[sp_n - 1] != '?' && sp_buf[sp_n - 1] != ':')
        sp_cat(s);
    else if (sp_n)
        sp_cat(" ");
}
static void sp_text(const char *s)
{
    for (; s && *s && sp_n < ANN_MAX; s++)
        if (*s == '>' || *s == '|')
            sp_cat(", ");
        else if (*s != ' ' || (sp_n && sp_buf[sp_n - 1] != ' '))
            sp_buf[sp_n++] = *s;
    while (sp_n && sp_buf[sp_n - 1] == ' ')
        sp_n--;
    sp_buf[sp_n] = 0;
}
static void sp_note(const char *s)
{
    int k = 1, ok = s[0] >= 'A' && s[0] <= 'G';
    if (ok && (s[1] == '#' || s[1] == 'b'))
        k = 2;
    for (int j = k; ok && s[j]; j++)
        ok = (s[j] >= '0' && s[j] <= '9') || (s[j] == '-' && j == k);
    if (!ok || !s[k]) {
        sp_text(s);
        return;
    }
    sp_buf[sp_n < ANN_MAX ? sp_n++ : sp_n] = s[0];
    sp_buf[sp_n] = 0;
    if (k == 2)
        sp_cat(s[1] == '#' ? " sharp" : " flat");
    sp_cat(s[k] == '-' ? " minus " : " ");
    sp_cat(s + k + (s[k] == '-'));
}
static void sp_msg(const char *a, const char *b, uint32_t flags)
{
    char t[100];
    uint32_t i, k = 0;
    if (!ann_on())
        return;
    for (i = 0; i < 48u && a && a[i]; i++)
        t[k++] = a[i];
    if (b && b[0] && k)
        t[k++] = ' ';
    for (i = 0; i < 48u && b && b[i]; i++)
        t[k++] = b[i];
    while (k && t[k - 1] == '.')
        k--;
    t[k] = 0;
    sp_n = 0;
    sp_text(t);
    ann_say(sp_buf, 0, flags);
    sp_said = 1;
}
static void sp_value(const knob_t *k)
{
    const char *v = k->val;
    if (s_eq(v, "-")) {
        sp_cat("none");
        return;
    }
    if (k->name && s_eq(k->name, "pan")) {
        if (v[0] == 'C' && !v[1])
            sp_cat("center");
        else if (v[0] == 'L' || v[0] == 'R') {
            sp_cat(v[0] == 'L' ? "left " : "right ");
            sp_cat(v + 1);
        } else
            sp_text(v);
        return;
    }
    sp_note(v);
}
static void sp_knob(const knob_t *k)
{
    sp_text(k->name);
    sp_cat(" ");
    sp_value(k);
}
static void sp_parts(const char *s)
{
    while (*s) {
        uint32_t n = 0, c = ctl_len(s), colon = 0;
        while (s[n] && s[n] != '|')
            n++;
        for (uint32_t j = c; j < n; j++)
            colon |= s[j] == ':';
        if (c) {
            for (uint32_t a = 0, j = 0; j <= c; j++)
                if (j == c || (s[j] == '+' && !(j == 3 && s[0] == 'O'))) {
                    char w[8];
                    uint32_t m = j - a < 7u ? j - a : 7u;
                    for (uint32_t q = 0; q < m; q++)
                        w[q] = s[a + q];
                    w[m] = 0;
                    if (a)
                        sp_cat(" and ");
                    sp_cat(s_eq(w, "KEYS") ? "keys" : s_eq(w, "WHITE") ? "white keys" : s_eq(w, "BLACK") ? "black keys"
                           : s_eq(w, "PRESET") ? "PRESETS" : s_eq(w, "KNOBS") ? "knobs" : w);
                    a = j + 1;
                }
            if (!colon)
                sp_cat(":");
        }
        for (uint32_t j = c; j < n && sp_n < ANN_MAX; j++)
            sp_buf[sp_n++] = s[j];
        sp_buf[sp_n] = 0;
        sp_end(". ");
        s += n;
        if (*s == '|')
            s++;
    }
}
static void sp_page(void)
{
    int g = page_group(ui.page), n = group_n(g), d = ui.page - PAGE_BTN[g].first, any = 0;
    char t[80];
    knob_t k[4];
    sp_cat(page_name_of(ui.page, sel_trk()));
    if (n > 1) {
        sp_cat(", page ");
        sp_int(d + 1);
        sp_cat(" of ");
        sp_int(n);
    } else
        sp_cat(" page");
    page_tag(t);
    if (*t) {
        sp_cat(". ");
        sp_text(t);
    }
    page_knobs(k);
    for (int i = 0; i < 4; i++)
        if (k[i].name) {
            sp_cat(any++ ? ", " : ": ");
            sp_knob(&k[i]);
        }
}
static void sp_state(void)
{
    sp_cat(playing() ? "playing" : "stopped");
    if (seq.rec || seq.rec_wait)
        sp_cat(", recording");
    if (ui.rec_armed)
        sp_cat(", step record");
    sp_cat(", tempo ");
    sp_int((int)(proj.tempo_x10 / 10u));
    sp_cat(", scene ");
    sp_int(cur_scene() + 1);
}
static void sp_main(void)
{
    int trk = sel_trk();
    if ((ui.page == P_STEPS || ui.page == P_BAR) && is_seqt(trk)) {
        const tpat_t *tp = tpat(trk);
        int n = 0;
        sp_cat("steps on: ");
        for (int s = 0; s < tp->len; s++)
            if (step_on(&tp->st[s], trk) && n < 16) {
                if (n++)
                    sp_cat(", ");
                sp_int(s + 1);
            }
        if (!n)
            sp_cat("none");
    } else if (ui.page == P_ARRANGE) {
        int n = 0;
        sp_cat("muted: ");
        for (int t = 0; t < NSEQT; t++)
            if (seq.mute >> t & 1u) {
                if (n++)
                    sp_cat(", ");
                sp_cat(TRACK_NAME[t]);
            }
        if (!n)
            sp_cat("none");
    } else if (ui.page == P_ENGINE && !is_aux(trk)) {
        sp_cat("engine ");
        sp_cat(EG_NAME[proj.trk[trk].par[TP_ENGINE] % NENGINE]);
    } else if (ui.page == P_STATUS) {
        sp_cat("audio load ");
        sp_int((int)plat_cpu_pct());
        sp_cat(" percent, voices ");
        sp_int(eng_voices);
    }
}
static int sp_list(void) { return lst.open && lst.n && !ui.card; }
static void sp_row(void)
{
    int i = lst.sel, n = lst.n;
    const char *v = lst.row[i].val;
    if (lst.row[i].kind == LK_KNOB) {
        knob_t kb;
        memset(&kb, 0, sizeof kb);
        kb.name = lst.row[i].name;
        s_cat(kb.val, v);
        sp_knob(&kb);
    } else {
        sp_text(lst.row[i].name);
        if (*v) {
            sp_cat(" ");
            sp_text(v);
        }
        if (lst.row[i].kind == LK_ACTION)
            sp_cat(", action");
    }
    sp_cat(", ");
    sp_int(i + 1);
    sp_cat(" of ");
    sp_int(n);
}
static void sp_front(int all)
{
    sp_n = 0;
    sp_buf[0] = 0;
    if (ui.ask) {
        sp_text(ui.ask_q);
        sp_end(". ");
        sp_parts("SEL yes|HOME no");
    } else if (ui.card) {
        int c = card_find(ui.card - 1);
        sp_cat(BTN_LABEL[(ui.card - 1) % NB]);
        sp_cat(" button");
        if (c >= 0) {
            sp_end(". ");
            sp_cat(CARD[c].title);
            sp_end(". ");
            sp_cat(CARD[c].l1);
            if (CARD[c].l2) {
                sp_end(". ");
                sp_cat(CARD[c].l2);
            }
        }
    } else if (sp_list()) {
        sp_cat(page_name_of(ui.page, sel_trk()));
        sp_cat(" list. ");
        sp_row();
        return;
    } else {
        sp_page();
        if (all) {
            sp_end(". ");
            sp_state();
            sp_end(". ");
            sp_main();
        }
    }
    sp_end(".");
    while (sp_n && sp_buf[sp_n - 1] == ' ')
        sp_n--;
    sp_buf[sp_n] = 0;
}
static void sp_controls(void)
{
    char t[80];
    sp_n = 0;
    sp_buf[0] = 0;
    if (ui.ask || ui.card)
        return;
    page_footer(t);
    sp_parts(t);
    if (!sp_list()) {
        enc_line(t);
        sp_parts(t);
    }
    while (sp_n && sp_buf[sp_n - 1] == ' ')
        sp_n--;
    sp_buf[sp_n] = 0;
}
static uint32_t sp_page_sig(void)
{
    uint32_t h = hash_u(2166136261u, ui.page);
    h = hash_u(h, (uint32_t)sp_list() | (uint32_t)ui.card << 3 | (uint32_t)ui.ask << 8 | (uint32_t)ui.lockpg << 12 |
                      (uint32_t)(ui.held != 0) << 16 | (uint32_t)(ui.cstep != 0) << 17);
    return ui.ask ? hash_s(h, ui.ask_q) : h;
}
static uint32_t sp_trk_sig(void) { return ui.page >= P_STEPS && ui.page <= P_MIX ? hash_u(2166136261u, (uint32_t)sel_trk()) : 0u; }
static uint32_t sp_knob_sig(const knob_t *k) { return k->name ? hash_s(hash_s(2166136261u, k->name), k->val) : 0u; }
static uint32_t sp_row_sig(void)
{
    if (!sp_list())
        return 0;
    return hash_s(hash_s(hash_u(2166136261u, lst.sel), lst.row[lst.sel].name), lst.row[lst.sel].val);
}
static void sp_snap(void)
{
    knob_t k[4];
    page_knobs(k);
    for (int i = 0; i < 4; i++)
        sp.kh[i] = sp_knob_sig(&k[i]);
    if (lst.open)
        list_build();
    sp.lh = sp_row_sig();
    sp.page = sp_page_sig();
    sp.trk = sp_trk_sig();
    sp.tempo = (int16_t)(proj.tempo_x10 / 10u);
    sp.scene = (uint8_t)cur_scene();
}
static int sp_knobs_changed(int input)
{
    knob_t k[4];
    uint32_t now = plat_ms();
    int any = 0;
    page_knobs(k);
    sp_n = 0;
    sp_buf[0] = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t h = sp_knob_sig(&k[i]);
        if (h == sp.kh[i])
            continue;
        sp.kh[i] = h;
        if (!k[i].name || !(input || (ui.touched == i && (int32_t)(ui.touch_until - now) > 0)))
            continue;
        if (any++)
            sp_cat(", ");
        sp_knob(&k[i]);
    }
    return any;
}
static void speak(void)
{
    uint32_t now = plat_ms(), pg, tr;
    int where = ann_where(), said = sp_said, input = now - sp_in_t < SP_IN_MS;
    sp_said = 0;
    if (!ann_on()) {
        sp.started = 0;
        sp.step = 0;
        return;
    }
    if (!sp.started || where) {
        sp.started = 1;
        sp.playing = (int8_t)playing();
        sp_snap();
        sp_front(1);
        ann_say(sp_buf, 0, ANN_NOW);
        sp.step = 1;
        return;
    }
    if (sp.step) {
        sp_controls();
        sp.step = 0;
        if (sp_n)
            ann_say(sp_buf, 0, 0);
    }
    if (playing() != sp.playing) {
        sp.playing = (int8_t)playing();
        ann_say(playing() ? (seq.count_in ? "Play, count-in" : "Play") : "Stop", 0, said ? 0 : ANN_NOW);
    }
    if (cur_scene() != sp.scene) {
        sp.scene = (uint8_t)cur_scene();
        sp_n = 0;
        sp_cat("Scene ");
        sp_int(sp.scene + 1);
        ann_say(sp_buf, 0, 0);
    }
    pg = sp_page_sig();
    tr = sp_trk_sig();
    if (said) {
        if (pg != sp.page && !ui.ask) {
            sp_front(0);
            ann_later(sp_buf, 0, 200u, now);
        } else if (pg == sp.page && tr == sp.trk && !ui.ask && !ui.card && !sp_list() && input && sp_knobs_changed(0))
            ann_later(sp_buf, 0, 120u, now);
        sp_snap();
        return;
    }
    if (pg != sp.page || tr != sp.trk) {
        int at_once = (ui.ask || ui.card) && pg != sp.page;
        sp_snap();
        sp_front(0);
        if (at_once)
            ann_say(sp_buf, 0, ANN_NOW);
        else
            ann_later(sp_buf, ANN_NOW, 200u, now);
        sp.step = 0;
        return;
    }
    if (ui.ask || ui.card)
        return;
    if (home_held()) {
        int tempo = (int)(proj.tempo_x10 / 10u);
        if (tempo != sp.tempo) {
            sp.tempo = (int16_t)tempo;
            sp_n = 0;
            sp_cat("Tempo ");
            sp_int(tempo);
            ann_later(sp_buf, ANN_NOW, 120u, now);
        }
        return;
    }
    sp.tempo = (int16_t)(proj.tempo_x10 / 10u);
    if (sp_list()) {
        uint32_t h;
        list_build();
        h = sp_row_sig();
        if (h != sp.lh) {
            sp.lh = h;
            sp_n = 0;
            sp_buf[0] = 0;
            sp_row();
            ann_later(sp_buf, ANN_NOW, 120u, now);
        }
        return;
    }
    if (sp_knobs_changed(input))
        ann_later(sp_buf, ANN_NOW, 120u, now);
}

#ifdef PLAT_HOST
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void speak_dump(void)
{
    static const char *const MSG[][2] = {
        {"SAVED", 0}, {"NOT SAVED", "flash error"}, {"UNDO", "T1 P1"}, {"REDO", "T1 P1"}, {"NOTHING TO UNDO", 0},
        {"NOTHING TO REDO", 0}, {"COUNT-IN", "then recording"}, {"RECORDING", "latched"}, {"RECORDING", "play to start"},
        {"RECORD OFF", 0}, {"STEP RECORD", "keys write steps"}, {"HOLD A CHORD", "for strum"}, {"CHORD RECORDED", 0},
        {"LENGTH", "to step 4"}, {"TRICK OFF", "pulse"}, {"NO TRICK", "white key first"}, {"NUDGE", "+2"},
        {"NO LANE LEFT", "clear locks first"}, {"LOCKS CLEARED", 0}, {"NEW", "S3"}, {"MUTED", "T1"}, {"UNMUTED", "T1"},
        {"INSERTED", "S2"}, {"T3", 0}, {"SOUND", "snare"}, {"KEY", "snare"}, {"SCALE", "1/2"}, {"BAR", "2"}, {"CURSOR", "5"},
        {"OCTAVE", "+1"}, {"PRESET", "U3 stack"}, {"EFFECT", "delay"}, {"CLEARED", "notes"}, {"COPIED", "pattern"},
        {"PASTED", "pattern"}, {"WOVEN", "copied rhythm"}, {"ROTATED", "forward"}, {"DUPLICATED", "bar"}, {"NO ROOM", "four bars at most"},
        {"SCRAMBLED", 0}, {"PRESET SAVED", "U2"}, {"RESET", "weft"}, {"SONG FULL", "64 slots"}, {"NO SLOT HERE", 0},
        {"NO SONG", "white keys add scenes"}, {"LOADED", "slot 2"}, {"EMPTY SLOT", "defaults loaded"},
        {"NEW PROJECT", "unsaved"}, {"SCENE IN USE", "pick another first"}, {"ALL NOTES OFF", 0},
        {"STOP FIRST", "to change slots"}, {"THREE SLOTS", "white keys 1 to 3"}, {"ENGINE", "stack"}, {"LIST", "nothing here"},
        {"DELETED", "slot"}, {"REMOVED", 0}, {"NO EFFECT YET", "play a key on EDIT"}, {"NO SEGMENT YET", "play a key on EDIT"},
        {"snare", "on the step"}, {"snare", "off the step"}, {"T2", "follows"}, {"T2", "on its own"},
        {"UPDATE MODE IN", "3.."}, {"UPDATE MODE", "CANCELLED"}};
    static const char *const ASK[] = {"CLEAR SCENE 3?", "REPLACE USER PRESET 2?", "REPLACE PROJECT 2?",
                                      "LOSE UNSAVED CHANGES?", "NEW PROJECT?", "CLEAR THE SONG?"};
    uint8_t keep_page = ui.page, keep_trk = proj.trk_sel, keep_lst = lst.open, keep_card = ui.card;
    knob_t k[4];
    char t[64];
    for (int trk = 0; trk < NALLTRK; trk += (trk == 1 ? NTRK - 1 : 1)) {
        proj.trk_sel = (uint8_t)trk;
        for (int p = 0; p < NPAGES; p++) {
            ui.page = (uint8_t)p;
            sp_front(0);
            sp_print("SCREEN", sp_buf);
            page_knobs(k);
            for (int i = 0; i < 4; i++) {
                if (!k[i].name)
                    continue;
                for (int d = -1; d <= 1; d += 2) {
                    accel_off = 1;
                    knob(i, d);
                    page_knobs(k);
                    sp_n = 0;
                    sp_knob(&k[i]);
                    sp_print("KNOB", sp_buf);
                    knob(i, -d);
                    accel_off = 0;
                }
            }
            lst.open = 1;
            list_build();
            for (int r = 0; r < lst.n; r++) {
                lst.sel = (uint8_t)r;
                sp_n = 0;
                sp_row();
                sp_print("ROW", sp_buf);
            }
            lst.open = 0;
            for (int b = 0; b < NB; b++)
                if (card_find(b) >= 0) {
                    ui.card = (uint8_t)(b + 1);
                    sp_front(0);
                    sp_print("CARD", sp_buf);
                }
            ui.card = 0;
        }
    }
    proj.trk_sel = keep_trk;
    ui.page = keep_page;
    for (uint32_t i = 0; i < sizeof MSG / sizeof MSG[0]; i++) {
        char *e = s_cat(t, MSG[i][0]);
        if (MSG[i][1])
            s_cat(s_cat(e, " "), MSG[i][1]);
        sp_n = 0;
        sp_text(t);
        sp_print("MSG", sp_buf);
    }
    for (uint32_t i = 0; i < sizeof ASK / sizeof ASK[0]; i++) {
        sp_n = 0;
        sp_text(ASK[i]);
        sp_end(". ");
        sp_parts("SEL yes|HOME no");
        sp_print("ASK", sp_buf);
    }
    for (int c = 1; c < NSC; c++) {                  /* the components and every value */
        sp_n = 0;
        sp_text(COMP_NAME[c]);
        sp_cat(" on the step");
        sp_print("MSG", sp_buf);
        for (int v = 0; v < 10; v++) {
            char vt[16];
            sp_n = 0;
            const char *val = comp_val_text(c, c >= SC_SKIP_LOCK ? (int)SK_VAL((uint32_t)v, 0) : v, vt);
            sp_text(COMP_NAME[c]);
            sp_cat(" ");
            sp_text(s_eq(val, COMP_NAME[c]) ? "any" : val);
            sp_print("MSG", sp_buf);
        }
    }
    for (int c = SC_SKIP_LOCK; c < NSC; c++)
        for (int m = 1; m < 3; m++) {
            char vt[16];
            sp_n = 0;
            sp_text(COMP_NAME[c]);
            sp_cat(" ");
            sp_text(comp_val_text(c, (int)SK_VAL(3u, (uint32_t)m), vt));
            sp_print("MSG", sp_buf);
        }
    for (int i = 0; i < NPUNCH; i++) {
        sp_n = 0;
        sp_text(PUNCH_NAME[i]);
        sp_print("MSG", sp_buf);
    }
    sp_front(1);
    sp_print("WHERE", sp_buf);
    lst.open = keep_lst;
    ui.card = keep_card;
    ui.dirty = 0;
}
#endif

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    memset(&lst, 0, sizeof lst);
    ui.touched = -1;
    ui.page = P_STEPS;
    ui.rem[0] = 60;
    ui.nrem = 1;
    ui.act_t = plat_ms();
    ui.scene_seen = 0xFF;
    ui.last_inst = (uint8_t)(is_aux(sel_trk()) ? 0 : sel_trk());
    apply_brain();
    engine_tempo(proj.tempo_x10);
    engine_groove(proj.groove);
#if defined(PLAT_HOST) && !defined(PLAT_WEB)
    if (getenv("LOOM_THEME"))                        /* the host's screenshots in another theme */
        proj.rsv[1] = (uint8_t)((uint32_t)atoi(getenv("LOOM_THEME")) % NTHEMES);
    theme = THEME_OF_PROJ();
#endif
    lcd_fill(0, 0, 240, 240, K_BG);
}

void ui_frame(void)
{
    input();
    if (playing() != ui.playing) {
        ui.playing = (uint8_t)playing();
        if (!ui.playing)
            rec_off();
    }
    if (cur_scene() != ui.scene_seen) {
        ui.scene_seen = (uint8_t)cur_scene();
        proj.trk[AUX_BRAIN].par[TP_E2] = pats.scene[cur_scene()].brain_key % 12u;
        proj.trk[AUX_BRAIN].par[TP_E3] = pats.scene[cur_scene()].brain_scale % NBS;
        if (ui.bar * 16 >= tpat(sel_trk())->len)
            ui.bar = 0;
    }
    seq.rec_target = (uint8_t)(sel_trk() == AUX_BRAIN ? AUX_BRAIN : inst_trk());   /* where live recording writes */
    seq.rec_replace = (uint8_t)(proj.rsv[2] & 1u);
    ink_follow();
    draw_header();
    draw_page();
    draw_rec_strip();
    if (!(lst.open && !ui.card)) {
        draw_tag();
        draw_knobs();
    }
    draw_footer();
    leds();
    speak();
    ann_tick(plat_ms());
    ui.frame++;
}

void ui_input_only(void) { input(); }
