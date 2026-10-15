/* SPDX-License-Identifier: GPL-3.0-only */
/* XY-1's pages: the state the input layer (ui.c) and the screen share, and the drawing. Part of the unity
 * build, after project.c and before ui.c; the host simulator includes it too. Polly's framework: a header
 * (the page, the tempo, REC, unsaved; messages and questions in an alert box), the main band (the page's
 * picture), the tag (where you are: track > pattern > scene, the page dots), the four knobs' strip (with
 * the OP-XY's four shades beside the names), the encoders' line and the footer (what the keys do here).
 * Each band is drawn only when what it shows changed (a hash of it). Over the main band: a help card (a
 * button held alone), a list (SEL). */

/* the colours, one meaning each (Polly's paper): teal = sounding now (the playhead, the scene playing, a
 * voice sounding), coral = you (the cursor, the held step, the knob last turned, REC, messages), ink and
 * its neutrals = what stays put */
#define K_BG RGB(250, 244, 232)
#define K_PANEL RGB(236, 228, 212)
#define K_PANEL2 RGB(222, 211, 192)
#define K_LINE RGB(216, 205, 186)
#define K_DIM RGB(112, 98, 84)
#define K_FAINT RGB(150, 136, 118)
#define K_TEXT RGB(72, 58, 48)
#define K_ACCENT RGB(242, 120, 72)
#define K_ACCENT_L RGB(248, 196, 172)
#define K_TEAL RGB(38, 166, 154)
#define K_TEAL_T RGB(196, 230, 222)
#define K_TEAL_M RGB(118, 196, 186)
#define K_REC K_ACCENT
/* the knobs' four shades, as the OP-XY's encoders: dark, mid, light grey, white (outlined) */
#define K_SH0 RGB(60, 56, 54)
#define K_SH1 RGB(128, 122, 118)
#define K_SH2 RGB(188, 182, 176)
#define K_SH3 RGB(255, 255, 255)

/* the pages (the spec's Buttons table): a button opens its first page; SELECT turns them */
enum { P_ARRANGE, P_SONG, P_STEPS, P_BAR, P_ENGINE, P_PLAY, P_ENV, P_ENV2, P_FILTER, P_SENDS, P_LFO, P_LFO2,
       P_PLAYERS, P_PLAYERS2, P_MIX, P_MASTER, P_MASTER2, P_MASTER3, P_SETUP, P_STATUS, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"Arrange", "Song", "Steps", "Bar", "Engine", "Play", "Envelope",
                                              "Filter env", "Filter", "Sends", "LFO", "LFO 2", "Players", "Players 2",
                                              "Mix", "Master EQ", "Master drive", "Master out", "Setup", "Status"};
static const struct { uint8_t btn, first, n; } PAGE_BTN[] = {
    {B_HOME, P_ARRANGE, 2}, {B_SEQ, P_STEPS, 2}, {B_EDIT, P_ENGINE, 2}, {B_ENV, P_ENV, 2}, {B_FX, P_FILTER, 2},
    {B_LFO, P_LFO, 2}, {B_ARP, P_PLAYERS, 2}, {B_GLO, P_MIX, 6}};
#define NPAGE_BTN (sizeof PAGE_BTN / sizeof PAGE_BTN[0])
static int page_group(int p)
{
    for (uint32_t i = 0; i < NPAGE_BTN; i++)
        if (p >= PAGE_BTN[i].first && p < PAGE_BTN[i].first + PAGE_BTN[i].n)
            return (int)i;
    return 0;
}
static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                          "PLAY", "REC", "OCT-", "OCT+"};

/* the tracks: 1-8, then the aux tracks (the tag's short names, the pages' long ones) */
static const char *const TRACK_NAME[NALLTRK] = {"T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8", "BRAIN", "PUNCH",
                                                "TAPE", "FX I", "FX II"};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
/* the step components (SC_*), their letter on the grid and their name */
static const char *const COMP_NAME[NSC] = {"none", "pulse", "hold", "multiply", "velocity", "ramp up", "ramp down",
                                           "random", "portamento", "bend", "jump", "skip"};
static const char *const COMP_GLYPH[NSC] = {"", "P", "H", "M", "V", "^", "v", "?", "~", "B", "J", "S"};
static const char *const VEL_VAL[10] = {"4", "8", "16", "32", "64", "100", "112", "127", "0", "random"};
static const char *const RAMP_VAL[10] = {"2/oct", "3/oct", "4/oct", "5/oct", "6/oct", "2/3oct", "3/3oct", "4/3oct",
                                         "5/3oct", "random"};
static const char *const PORTA_VAL[10] = {"10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "random"};
static const char *const BEND_VAL[10] = {"down-up", "up-down", "bump dn", "bump up", "spring out", "spring in",
                                         "fade dn", "fade up", "random 1", "random 2"};
static const char *const JUMP_VAL[10] = {"to 1", "to 5", "to 9", "to 13", "forward", "back", "fwd/back", "stay",
                                         "align", "random"};
static const char *const SKIP_VAL[10] = {"1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th", "9th", "random"};
static const char *const TS_NAME[NTS] = {"1/2", "1", "2", "3", "4", "6", "8", "16"};
static const char *const GROOVE_NAME[NGROOVE] = {"swing", "half", "bombora", "wobbly", "gauss", "accents", "island"};
static const char *const VM_NAME[3] = {"poly", "mono", "legato"};
static const char *const FT_NAME[NFT] = {"LP12", "LP24", "HP", "BP"};
static const char *const LT_NAME[NLT] = {"off", "value", "tremolo", "random", "duck"};
static const char *const LX_NAME[NLT] = {"-", "shape", "vibrato", "smooth", "source"};
static const char *const PL_NAME[NPL] = {"off", "arp", "maestro", "hold"};
static const char *const ARP_SPEED[6] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const char *const ARP_PAT[5] = {"up", "down", "updown", "random", "played"};
static const char *const MAE_PAT[4] = {"up", "down", "updown", "random"};
static const char *const LIGHTS_NAME[3] = {"off", "on", "keys"};
static const char *const METRO_NAME[3] = {"off", "rec", "on"};
static const char *const ONOFF[2] = {"off", "on"};
static const char *const BRAIN_MODE[2] = {"manual", "auto"};
static const char *const FOLLOW_NAME[3] = {"no", "diatonic", "transpose"};
static const char *const CORE2_NAME[4] = {"one core", "two cores", "lost", "barred"};
static const char *const USBA_NAME[2] = {"follows knob", "full"};
/* the aux tracks' Engine pages: their knobs (the aux track's par[TP_E1..E4]; FX pages 2: TP_P1..P4) */
static const char *const BRAIN_KNOB[4] = {"mode", "key", "scale", "link"};
static const char *const PUNCH_KNOB[4] = {"latch", 0, 0, 0};
static const char *const TAPE_KNOB[4] = {"pitch", "speed", "length", "mix"};
static const char *const FX2_KNOB[4] = {0, "hp", "lp", "to fx II"};

/* the keys: 0 = F3; the white and the black ones in order */
static const uint8_t WHITE_K[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_K[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
#define KEY_BASE 53                                  /* F3 at octave 0 */
enum { KM_NONE, KM_PLAY, KM_STEP, KM_COMP, KM_SCENE, KM_MUTE, KM_PUNCH, KM_TAPE, KM_BRAIN, KM_SOUND, KM_TRACK,
       KM_SAVE, KM_LENGTH };                         /* what a held key does (ui.kmode) */

/* the input's and the screen's state */
static struct {
    uint32_t btn, keys, btn_used, btn_t[NB];
    uint32_t enc_t[NE];
    uint32_t act_t;
    char msg[2][24];
    uint32_t msg_until;
    uint8_t msg_kind, msg_no;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint8_t page;
    uint8_t any_btn;
    uint8_t card;                                    /* a help card on screen: its button + 1 */
    uint8_t ask;                                     /* a question on screen (SEL yes, HOME no) */
    char ask_q[48];
    void (*ask_yes)(void);
    void (*ask_no)(void);
    uint8_t ask_arg;                                 /* what the question is about (a scene, a slot) */
    uint8_t dirty;                                   /* the project changed since it was saved */
    uint8_t bar;                                     /* Steps: the bar shown 0..3 */
    uint8_t dsound;                                  /* a drum track: the sound shown 0..10 */
    int8_t oct;                                      /* the keys' octave -3..3 */
    uint8_t rem[SN], nrem;                           /* the remembered notes (the last keys held together) */
    uint8_t chord[8], nchord;                        /* the keys held together now (notes) */
    uint16_t held;                                   /* Steps: the white keys down (bit = step of the bar) */
    uint8_t held_first;                              /* the first of them (the step edited) */
    uint8_t held_used;                               /* a held step was edited: its release toggles nothing */
    uint8_t lockpg;                                  /* a step held + EDIT / ENV / FX / LFO: the lock editor's page + 1 */
    uint8_t cursor;                                  /* step record: the step the keys write */
    uint8_t rec_armed;                               /* REC while stopped: step record */
    uint8_t maestro_rec;                             /* REC on Players with maestro: the next chord is recorded */
    uint8_t kmode[NKEYS], knote[NKEYS];              /* each held key: KM_*, its note / scene / fx */
    uint32_t key_t[NKEYS];
    uint32_t play_t;                                 /* PLAY's last press (twice quickly: from the top) */
    uint32_t punch_latched;                          /* PUNCH: the FX kept on after their keys (latch) */
    uint8_t preset_i;                                /* Engine: the preset last stepped to (PRESETS) */
    uint8_t pick_engine, pick_preset, pick_slot, pick_follow;   /* the lists' choices */
    uint8_t name_slot;
    uint8_t playing;                                 /* seq.playing as last seen */
    uint8_t scene_seen;
    uint32_t sig[6];                                 /* the bands' hashes: header, main, knobs, tag, footer, list */
} ui;

/* SEL: the screen as a list: the page's knobs, then its actions */
enum { LK_KNOB, LK_MORE, LK_ACTION, LK_PICK };
enum { A_CLEAR_NOTES, A_CLEAR_LOCKS, A_CLEAR_BOTH, A_COPY_PAT, A_PASTE_PAT, A_ROT_FWD, A_ROT_BACK, A_DUP_BAR,
       A_ENGINE, A_PRESET, A_SCRAMBLE, A_SAVE_PRESET, A_INIT, A_FOLLOW, A_INSERT_SLOT, A_DELETE_SLOT, A_CLEAR_SONG,
       A_PLAY_SONG, A_LOAD_PROJ, A_NEW_PROJ, A_USB_AUDIO, A_CLEAR_SCENE, A_PANIC, NACTIONS };
static const char *const ACTION_NAME[NACTIONS] = {
    "CLEAR NOTES", "CLEAR LOCKS", "CLEAR NOTES AND LOCKS", "COPY PATTERN", "PASTE PATTERN", "ROTATE FORWARD",
    "ROTATE BACK", "DUPLICATE BAR", "ENGINE", "PRESET", "SCRAMBLE", "SAVE AS PRESET", "RESET SOUND", "FOLLOWS BRAIN",
    "INSERT SCENE", "DELETE SLOT", "CLEAR SONG", "PLAY SONG", "LOAD PROJECT", "NEW PROJECT", "USB AUDIO", "CLEAR SCENE",
    "ALL NOTES OFF"};
#define LIST_ROWS 28
#define LIST_SHOWN 10
static struct {
    uint8_t open, n, sel, top;
    struct { const char *name; char val[24]; uint8_t kind, id; } row[LIST_ROWS];
} lst;

/* the undo: one stage, the selected track's pattern as it was before the last edit (HOME + REC swaps it
 * back, HOME + PLAY swaps again: redo). Locks (the lanes) are not part of it */
static struct {
    tpat_t pat;
    uint8_t valid, trk, pt, redo;
    uint32_t t;
} undo XY_POOL;
static tpat_t clip XY_POOL;                          /* COPY PATTERN's copy */
static uint8_t clip_ok;

/* ---------------------------------------------------------- helpers --- */
static char *num_s(uint32_t v, char *b)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}
static char *num_i(int32_t v, char *b)
{
    if (v < 0) {
        *b++ = '-';
        v = -v;
    }
    return num_s((uint32_t)v, b);
}
static char *s_cat(char *b, const char *s)
{
    while (*s)
        *b++ = *s++;
    *b = 0;
    return b;
}
static uint32_t hash_u(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static uint32_t hash_s(uint32_t h, const char *s)
{
    while (s && *s)
        h = hash_u(h, (uint8_t)*s++);
    return h;
}
static uint32_t s_len(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}
static int s_eq(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}
static char *upcase(char *s)
{
    for (char *c = s; *c; c++)
        if (*c >= 'a' && *c <= 'z')
            *c = (char)(*c - 32);
    return s;
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static char *note_name(int n, char *b)               /* "C4" (60), "Eb3" */
{
    char *e = s_cat(b, NOTE_NAME[(n % 12 + 12) % 12]);
    return num_i(n / 12 - 1, e);
}
/* sin(2 pi k / 32) x 100 */
static const int8_t SIN32[32] = {0, 20, 38, 56, 71, 83, 92, 98, 100, 98, 92, 83, 71, 56, 38, 20,
                                 0, -20, -38, -56, -71, -83, -92, -98, -100, -98, -92, -83, -71, -56, -38, -20};
static int32_t sin100(uint32_t k) { return SIN32[k & 31u]; }
static int32_t cos100(uint32_t k) { return SIN32[(k + 8u) & 31u]; }

static void cv_round(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    static const uint8_t IN[7][6] = {{0}, {1}, {1, 0}, {2, 1, 0}, {2, 1, 0, 0}, {3, 2, 1, 0, 0}, {4, 2, 1, 1, 0, 0}};
    int32_t j;
    if (r > 6)
        r = 6;
    if (r > h / 2)
        r = h / 2;
    cv_rect(x, y + r, w, h - 2 * r, c);
    for (j = 0; j < r; j++) {
        int32_t d = IN[r][j];
        cv_rect(x + d, y + j, w - 2 * d, 1, c);
        cv_rect(x + d, y + h - 1 - j, w - 2 * d, 1, c);
    }
}
static void cv_frame(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c, uint16_t in)
{
    cv_round(x, y, w, h, r, c);
    cv_round(x + 1, y + 1, w - 2, h - 2, r > 0 ? r - 1 : 0, in);
}
static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}
static void text_r(int32_t xr, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(xr - text_w(f, s), y, f, s, c);
}
/* the y to draw f at so its capitals sit centred in rows top .. top + h - 1 (XS 3-11, S and B 2-12, M 3-18,
 * L 4-24) */
static int32_t cap_y(int32_t top, int32_t h, const felucca_font_t *f)
{
    int32_t c2 = f == &FONT_L ? 28 : f == &FONT_M ? 21 : 14;
    return (2 * top + h - 1 - c2) >> 1;
}
static int32_t text_fit(int32_t x, int32_t y, const felucca_font_t *f, const char *s, int32_t w, uint16_t c)
{
    char b[48];
    uint32_t n;
    for (n = 0; n < sizeof b - 1u && s[n]; n++)
        b[n] = s[n];
    b[n] = 0;
    while (n && text_w(f, b) > w)
        b[--n] = 0;
    return cv_text(x, y, f, b, c);
}
static int upper_only(const char *s)
{
    while (*s)
        if ((uint8_t)*s++ > 95u)
            return 0;
    return 1;
}
static int32_t pill(int32_t x, int32_t y, int32_t w, const char *s, uint16_t bg, uint16_t fg)
{
    if (!w)
        w = text_w(&FONT_S, s) + 16;
    cv_round(x, y, w, 20, 6, bg);
    text_c(x + w / 2, y + 2, &FONT_S, s, fg);
    return x + w;
}
static int32_t pill_o(int32_t x, int32_t y, int32_t w, const char *s, uint16_t c, uint16_t fg)
{
    if (!w)
        w = text_w(&FONT_S, s) + 16;
    cv_round(x, y, w, 20, 6, c);
    cv_round(x + 1, y + 1, w - 2, 18, 5, K_BG);
    text_c(x + w / 2, y + 2, &FONT_S, s, fg);
    return x + w;
}
/* s in lines of w pixels (at spaces), from y; the y after (draw 0: only measured) */
static int32_t wrap_do(int32_t x, int32_t y, int32_t w, const felucca_font_t *f, const char *s, uint16_t c, int draw)
{
    char line[48];
    while (*s) {
        uint32_t n = 0, cut = 0;
        while (s[n] && n < sizeof line - 1u) {
            line[n] = s[n];
            line[n + 1] = 0;
            if (text_w(f, line) > w)
                break;
            n++;
            if (!s[n] || s[n] == ' ')
                cut = n;
        }
        if (!cut)
            cut = n ? n : 1;
        line[cut] = 0;
        if (draw)
            cv_text(x, y, f, line, c);
        y += 17;
        s += cut;
        while (*s == ' ')
            s++;
    }
    return y;
}
static int32_t wrap(int32_t x, int32_t y, int32_t w, const felucca_font_t *f, const char *s, uint16_t c)
{
    return wrap_do(x, y, w, f, s, c, 1);
}
/* text in parts: "KEYS play|REC record" draws a dot between the parts, "T3>P2" a chevron */
#define SEP_W 13
static int32_t segs_w(const felucca_font_t *f, const char *s)
{
    char b[48];
    int32_t w = 0;
    uint32_t n = 0;
    for (;; s++) {
        if (*s == '|' || *s == '>' || !*s) {
            b[n] = 0;
            w += text_w(f, b);
            n = 0;
            if (!*s)
                return w;
            w += SEP_W;
        } else if (n < sizeof b - 1u)
            b[n++] = *s;
    }
}
static int32_t segs(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    char b[48];
    uint32_t n = 0;
    for (;; s++) {
        if (*s == '|' || *s == '>' || !*s) {
            b[n] = 0;
            x = cv_text(x, y, f, b, c);
            n = 0;
            if (!*s)
                return x;
            if (*s == '>')
                for (int j = 0; j < 7; j++)
                    cv_rect(x + 4 + (j < 4 ? j : 6 - j), y + 6 + j, 2, 1, K_FAINT);
            else
                cv_round(x + 5, y + 8, 3, 3, 1, c);
            x += SEP_W;
        } else if (n < sizeof b - 1u)
            b[n++] = *s;
    }
}

/* --------------------------------------------------------- the model --- */
#define FLASH_MS 500u
#define HDR_H 24
#define MAIN_Y 24
#define MAIN_H 120
#define TAG_Y 144
#define TAG_H 12
#define KNB_Y 156
#define KNB_H 52
#define ENC_Y 208
#define FOOT_Y 224
enum { AL_INFO, AL_DONE, AL_UNDO, AL_REDO, AL_ASK, AL_NO };

static int sel_trk(void) { return proj.trk_sel < NALLTRK ? proj.trk_sel : 0; }
static int is_aux(int t) { return t >= NTRK; }
static int is_seqt(int t) { return t < NSEQT; }
static int is_drum(int t) { return t < NTRK && proj.trk[t].par[TP_ENGINE] == EG_DRUMLINE; }
static int cur_scene(void) { return seq.scene < NSCENE ? seq.scene : 0; }
static int cur_pat(int t) { return is_seqt(t) ? seq.ppat[t] % NPATT : 0; }
static tpat_t *tpat(int t) { return &pats.pat[is_seqt(t) ? t : 0][cur_pat(t)]; }
static int playing(void) { return seq.playing; }
static int recording(void) { return seq.rec || ui.rec_armed || ui.maestro_rec; }
static int step_shown(int i) { return ui.bar * 16 + i; }   /* the bar's cell i: the pattern's step */
static int page_is_play(int p)                       /* the keys play the track here */
{
    return p >= P_ENGINE && p <= P_PLAYERS2;
}
static int tp_page_param(int p, int k, int trk)      /* the TP_* a page's knob k edits, -1 none */
{
    static const int8_t M[][4] = {
        {TP_E1, TP_E2, TP_E3, TP_E4}, {TP_VMODE, TP_PORTA, TP_BEND, TP_VOLUME}, {TP_A, TP_D, TP_S, TP_R},
        {TP_FA, TP_FD, TP_FS, TP_FR}, {TP_CUT, TP_RES, TP_FENV, TP_KTRK}, {TP_FTYPE, TP_SEND1, TP_SEND2, TP_SENDT},
        {TP_LTYPE, TP_LSPEED, TP_LAMT, TP_LDEST}, {TP_LX, -1, -1, -1}, {TP_PLAYER, TP_P1, TP_P2, TP_P3},
        {TP_P4, -1, -1, -1}, {TP_LEVEL, TP_PAN, TP_SEND1, TP_SEND2}};
    if (p < P_ENGINE || p > P_MIX || k < 0 || k > 3)
        return -1;
    if (is_aux(trk) && p < P_MIX) {                  /* an aux track's Engine page: its own four, page 2: TP_P1.. */
        if (p == P_ENGINE)
            return TP_E1 + k;
        if (p == P_PLAY && trk >= AUX_FX1)
            return TP_P1 + k;
        return -1;
    }
    if (p == P_MIX && is_aux(trk))
        return k == 0 ? TP_LEVEL : -1;
    if (p == P_ENGINE && is_drum(trk))               /* a drum track: the shown sound's model, tune, tone, decay */
        return TP_DRUM(ui.dsound % DL_NSOUND, k);
    return M[p - P_ENGINE][k];
}

/* ---------------------------------------------------------- alerts --- */
#define AL_X 28
static void alert_box(int kind)
{
    uint16_t fill = kind == AL_ASK ? K_ACCENT : kind == AL_NO ? K_TEXT : K_PANEL;
    uint16_t edge = kind == AL_ASK ? K_TEXT : kind == AL_NO ? K_ACCENT : K_DIM;
    uint16_t badge = kind == AL_ASK ? K_BG : kind == AL_NO ? K_ACCENT : K_TEXT;
    uint16_t mark = kind == AL_ASK ? K_ACCENT : K_BG;
    int32_t bx = 6, by = 4;
    cv_round(1, 1, 238, HDR_H - 2, 7, edge);
    cv_round(2, 2, 236, HDR_H - 4, 6, fill);
    cv_round(bx, by, 16, 16, 8, badge);
    if (kind == AL_DONE) {
        for (int j = 0; j < 3; j++)
            cv_rect(bx + 4 + j, by + 7 + j, 2, 2, mark);
        for (int j = 0; j < 5; j++)
            cv_rect(bx + 7 + j, by + 9 - j, 2, 2, mark);
    } else if (kind == AL_UNDO || kind == AL_REDO) {
        int f = kind == AL_REDO, o = f ? -1 : 1;
        cv_rect(bx + 5 + o, by + 5, 6, 2, mark);
        cv_rect(bx + (f ? 4 : 10) + o, by + 5, 2, 6, mark);
        cv_rect(bx + 5 + o, by + 9, 6, 2, mark);
        for (int j = 0; j < 3; j++)
            cv_rect(bx + (f ? 13 - j : 2 + j) + o, by + 6 - j, 1, 2 * j + 1, mark);
    } else
        text_c(bx + 8, cap_y(by, 16, &FONT_B), &FONT_B, kind == AL_ASK ? "?" : kind == AL_NO ? "!" : "i", mark);
}
static void draw_header(void)
{
    char t[40];
    int msg = plat_ms() < ui.msg_until, rec = recording(), ext = seq.ext, ask = ui.ask;
    int tempo = (int)((proj.tempo_x10 + 5u) / 10u), cnt = seq.count_in;
    uint32_t h = hash_u(2166136261u, ui.page | (uint32_t)msg << 8 | (uint32_t)ui.dirty << 9 | (uint32_t)rec << 11 |
                                         (uint32_t)ext << 12 | (uint32_t)ask << 14 | (uint32_t)tempo << 16);
    h = hash_u(h, (uint32_t)cnt | (uint32_t)playing() << 8);
    if (msg)
        h = hash_u(h, ui.msg_until ^ (uint32_t)ui.msg_no << 31);
    if (ask)
        h = hash_s(h, ui.ask_q);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (ask) {
        alert_box(AL_ASK);
        if (text_w(&FONT_B, ui.ask_q) <= 206)
            cv_text(AL_X, 4, &FONT_B, ui.ask_q, K_BG);
        else if (text_w(&FONT_S, ui.ask_q) <= 208)
            cv_text(AL_X, 5, &FONT_S, ui.ask_q, K_BG);
        else
            text_fit(AL_X, 5, &FONT_XS, ui.ask_q, 232 - AL_X, K_BG);
    } else if (cnt && playing()) {
        int32_t x = cv_text(8, 4, &FONT_B, "Count-in", K_TEXT);
        num_s((uint32_t)cnt, t);
        cv_text(x + 8, 4, &FONT_B, t, K_ACCENT);
        cv_round(222, 8, 9, 9, 4, K_REC);
    } else if (msg) {
        int no = ui.msg_no;
        int32_t x;
        alert_box(no ? AL_NO : ui.msg_kind);
        x = cv_text(AL_X, 4, &FONT_B, ui.msg[0], no ? K_BG : K_TEXT);
        text_fit(x + 8, 4, &FONT_B, ui.msg[1], 232 - (x + 8), no ? K_ACCENT_L : K_ACCENT);
    } else {
        int32_t r = 232, x = 8;
        const char *bn = BTN_LABEL[PAGE_BTN[page_group(ui.page)].btn];
        int32_t w = text_w(&FONT_XS, bn) + 6;
        cv_round(x, 6, w, 13, 2, K_DIM);
        cv_round(x + 1, 7, w - 2, 11, 1, K_BG);
        cv_text(x + 3, cap_y(6, 13, &FONT_XS), &FONT_XS, bn, K_DIM);
        x += w + 6;
        cv_text(x, 4, &FONT_B, PAGE_NAME[ui.page], K_TEXT);
        if (ext)
            s_cat(t, "ext");
        else
            num_s((uint32_t)tempo, t);
        r -= text_w(&FONT_S, t);
        cv_text(r, 4, &FONT_S, t, K_TEXT);
        r -= 6;
        if (rec) {
            r -= 10;
            cv_round(r, 8, 9, 9, 4, K_REC);
            r -= 6;
        }
        if (ui.dirty)
            cv_text(r - text_w(&FONT_XS, "unsaved"), 5, &FONT_XS, "unsaved", K_DIM);
    }
    cv_blit(0, 0);
}

/* ----------------------------------------------------------- knobs --- */
typedef struct {
    const char *name;
    char val[24];
    int32_t num, max;                                /* the bar: num of max (max 0: no bar) */
} knob_t;
static void kv(knob_t *k, const char *name, const char *val, int32_t num, int32_t max)
{
    uint32_t i;
    k->name = name;
    for (i = 0; i < sizeof k->val - 1u && val[i]; i++)
        k->val[i] = val[i];
    k->val[i] = 0;
    k->num = num;
    k->max = max;
}
static void kn(knob_t *k, const char *name, int32_t v, int32_t max)
{
    k->name = name;
    num_i(v, k->val);
    k->num = v;
    k->max = max;
}
static void kname(knob_t *k, const char *name, const char *const *names, int v, int n)
{
    kv(k, name, names[clampi(v, 0, n - 1)], v, 0);
}
/* a track parameter's knob: its name and value as synth.c prints them */
static void ktp(knob_t *k, int trk, int p)
{
    const tp_info_t *in = tp_info(p, proj.trk[trk].par[TP_ENGINE]);
    char num[12], unit[6], v[24], *e;
    int val = proj.trk[trk].par[p];
    tp_text(p, val, proj.trk[trk].par[TP_ENGINE], num, unit);
    e = s_cat(v, num);
    if (unit[0]) {
        if (unit[0] != '%')
            e = s_cat(e, " ");
        s_cat(e, unit);
    }
    kv(k, in->name, v, val - in->lo, in->kind == PK_NAMES || in->kind == PK_SWITCH ? 0 : in->hi - in->lo);
}
/* a step's value knobs (a step held on Steps) */
static void step_len_text(int len, char *t)         /* quarter steps: "2", "2.5", "1.25"; 0: the pattern's */
{
    static const char *const Q[4] = {"", ".25", ".5", ".75"};
    if (!len) {
        s_cat(t, "pattern");
        return;
    }
    s_cat(num_s((uint32_t)len / 4u, t), Q[len & 3]);
}
static void cond_text(int c, char *t)
{
    if (!c)
        s_cat(t, "always");
    else if (c < COND_AB)
        s_cat(num_s((uint32_t)c, t), "%");
    else {
        int a, b;
        cond_ab(c - COND_AB, &a, &b);
        num_s((uint32_t)b, s_cat(num_s((uint32_t)a, t), ":"));
    }
}
static const char *comp_val_text(int comp, int v, char *t)
{
    v = clampi(v, 0, 9);
    switch (comp) {
    case SC_VELOCITY: return VEL_VAL[v];
    case SC_RAMP_UP: case SC_RAMP_DOWN: case SC_RANDOM: return RAMP_VAL[v];
    case SC_PORTAMENTO: return PORTA_VAL[v];
    case SC_BEND: return BEND_VAL[v];
    case SC_JUMP: return JUMP_VAL[v];
    case SC_SKIP: return SKIP_VAL[v];
    default:
        if (v == SC_RANDOM_VAL)
            return "random";
        num_s((uint32_t)v + 1u, t);
        return t;
    }
}
static void step_knobs(knob_t *k)
{
    const step_t *st = &tpat(sel_trk())->st[step_shown(ui.held_first)];
    char t[16];
    kn(&k[0], "VEL", st->vel, 127);
    t[0] = 0;
    step_len_text(st->len, t);
    kv(&k[1], "LENGTH", t, st->len, 64);
    t[0] = 0;
    cond_text(st->cond, t);
    kv(&k[2], "CHANCE", t, 0, 0);
    if (st->comp)                                    /* a component on the step: K4 is its value */
        kv(&k[3], COMP_NAME[st->comp % NSC], comp_val_text(st->comp, st->cval, t), st->cval, 0);
    else {
        char *e = t;
        if (st->nudge > 0)
            e = s_cat(e, "+");
        num_i(st->nudge, e);
        kv(&k[3], "NUDGE", t, st->nudge + 12, 24);
    }
}
/* the lock editor: a step held + EDIT / ENV / FX / LFO: that page's four, with the lock's value ("-": none) */
static void lock_knobs(knob_t *k)
{
    int trk = sel_trk(), pg = ui.lockpg - 1, step = step_shown(ui.held_first);
    for (int i = 0; i < 4; i++) {
        int p = tp_page_param(pg, i, trk), ln;
        if (p < 0)
            continue;
        ktp(&k[i], trk, p);                          /* (the name, the range) */
        ln = lane_find(pats.lane, trk, cur_pat(trk), p);
        if (ln < 0 || pats.lane[ln].val[step] == LOCK_NONE) {
            kv(&k[i], k[i].name, "-", 0, 0);         /* no lock: the knob's own value plays */
        } else {
            char num[12], unit[6], *e;
            tp_text(p, pats.lane[ln].val[step], proj.trk[trk].par[TP_ENGINE], num, unit);
            e = s_cat(k[i].val, num);
            if (unit[0])
                s_cat(e, unit);
            k[i].num = pats.lane[ln].val[step] - tp_info(p, proj.trk[trk].par[TP_ENGINE])->lo;
        }
    }
}
static void aux_engine_knobs(knob_t *k, int trk)
{
    const uint8_t *par = proj.trk[trk].par;
    if (trk == AUX_BRAIN) {
        kname(&k[0], BRAIN_KNOB[0], BRAIN_MODE, par[TP_E1], 2);
        kname(&k[1], BRAIN_KNOB[1], NOTE_NAME, par[TP_E2], 12);
        kname(&k[2], BRAIN_KNOB[2], BS_NAME, par[TP_E3], NBS);
        kname(&k[3], BRAIN_KNOB[3], ONOFF, par[TP_E4], 2);
    } else if (trk == AUX_PUNCH) {
        kname(&k[0], PUNCH_KNOB[0], ONOFF, par[TP_E1], 2);
    } else if (trk == AUX_TAPE) {
        for (int i = 0; i < 4; i++)
            kn(&k[i], TAPE_KNOB[i], par[TP_E1 + i], 127);
    } else {
        int type = par[TP_E1] % NFXT;
        kname(&k[0], "effect", FXT_NAME, type, NFXT);
        for (int i = 0; i < 3; i++)
            kn(&k[1 + i], FXT_KNOB[type][i], par[TP_E2 + i], 127);
    }
}
static void aux_engine2_knobs(knob_t *k, int trk)
{
    const uint8_t *par = proj.trk[trk].par;
    int type = par[TP_E1] % NFXT;
    kn(&k[0], FXT_KNOB[type][3], par[TP_P1], 127);
    kn(&k[1], FX2_KNOB[1], par[TP_P2], 127);
    kn(&k[2], FX2_KNOB[2], par[TP_P3], 127);
    if (trk == AUX_FX1)
        kn(&k[3], FX2_KNOB[3], par[TP_P4], 127);
}
static void players_knobs(knob_t *k, int trk)
{
    const uint8_t *par = proj.trk[trk].par;
    int pl = par[TP_PLAYER] % NPL;
    kname(&k[0], "PLAYER", PL_NAME, pl, NPL);
    if (pl == PL_ARP) {
        kname(&k[1], "SPEED", ARP_SPEED, par[TP_P1], 6);
        kname(&k[2], "PATTERN", ARP_PAT, par[TP_P2], 5);
        kn(&k[3], "RANGE", clampi(par[TP_P3], 1, 4), 4);
    } else if (pl == PL_MAESTRO) {
        kn(&k[1], "ROLL", par[TP_P1], 127);
        kname(&k[2], "PATTERN", MAE_PAT, par[TP_P2], 4);
    }
}
static void page_knobs(knob_t *k)
{
    int trk = sel_trk(), p = ui.page;
    tpat_t *tp = tpat(trk);
    char t[16];
    memset(k, 0, 4 * sizeof k[0]);
    if (ui.lockpg) {
        lock_knobs(k);
        return;
    }
    if (p == P_STEPS && ui.held && is_seqt(trk)) {
        step_knobs(k);
        return;
    }
    switch (p) {
    case P_ARRANGE:
        kn(&k[0], "TEMPO", (int)((proj.tempo_x10 + 5u) / 10u), 300);
        kn(&k[1], "SWING", proj.swing, 100);
        kname(&k[2], "GROOVE", GROOVE_NAME, proj.groove, NGROOVE);
        kname(&k[3], "CLICK", METRO_NAME, proj.metronome, 3);
        break;
    case P_SONG: {
        const song_t *sg = &pats.song;
        int cur = ui.name_slot < sg->len ? ui.name_slot : sg->len;
        kn(&k[0], "SLOT", cur + 1, NSONG);
        if (cur < sg->len) {
            kn(&k[1], "SCENE", sg->slot[cur].scene + 1, NSCENE);
            kn(&k[2], "REPEAT", sg->slot[cur].rep, 16);
        } else {
            kv(&k[1], "SCENE", "-", 0, 0);
            kv(&k[2], "REPEAT", "-", 0, 0);
        }
        kv(&k[3], "AT END", sg->loop ? "loop" : "stop", 0, 0);
        break;
    }
    case P_STEPS:
        if (!is_seqt(trk))
            break;
        kn(&k[0], "PATTERN", cur_pat(trk) + 1, NPATT);
        kn(&k[1], "LENGTH", tp->len, NSTEP);
        kname(&k[2], "SCALE", TS_NAME, tp->scale, NTS);
        kn(&k[3], "SWING", tp->swing, 100);
        break;
    case P_BAR:
        if (!is_seqt(trk))
            break;
        kn(&k[0], "QUANTISE", tp->quant, 100);
        t[0] = 0;
        step_len_text(tp->nlen, t);
        kv(&k[1], "NOTE LEN", t, tp->nlen, 16);
        kname(&k[2], "GROOVE", GROOVE_NAME, tp->groove, NGROOVE);
        kn(&k[3], "SHAPE", tp->shape, 100);
        break;
    case P_ENGINE:
        if (is_aux(trk))
            aux_engine_knobs(k, trk);
        else
            for (int i = 0; i < 4; i++)
                ktp(&k[i], trk, tp_page_param(p, i, trk));
        break;
    case P_PLAY:
        if (trk >= AUX_FX1)
            aux_engine2_knobs(k, trk);
        else if (!is_aux(trk))
            for (int i = 0; i < 4; i++)
                ktp(&k[i], trk, tp_page_param(p, i, trk));
        break;
    case P_PLAYERS:
        if (!is_aux(trk))
            players_knobs(k, trk);
        break;
    case P_PLAYERS2:
        if (!is_aux(trk) && proj.trk[trk].par[TP_PLAYER] % NPL != PL_OFF)
            kname(&k[0], "HOLD", ONOFF, proj.trk[trk].par[TP_P4] != 0, 2);
        break;
    case P_LFO2:
        if (!is_aux(trk) && proj.trk[trk].par[TP_LTYPE] % NLT != LT_OFF) {
            ktp(&k[0], trk, TP_LX);
            k[0].name = LX_NAME[proj.trk[trk].par[TP_LTYPE] % NLT];
        }
        break;
    case P_ENV: case P_ENV2: case P_FILTER: case P_SENDS: case P_LFO:
        if (!is_aux(trk))
            for (int i = 0; i < 4; i++)
                ktp(&k[i], trk, tp_page_param(p, i, trk));
        break;
    case P_MIX:
        ktp(&k[0], trk, TP_LEVEL);
        if (!is_aux(trk)) {
            ktp(&k[1], trk, TP_PAN);
            ktp(&k[2], trk, TP_SEND1);
            ktp(&k[3], trk, TP_SEND2);
            k[2].name = "FX I";
            k[3].name = "FX II";
        }
        break;
    case P_MASTER: case P_MASTER2: case P_MASTER3:
        for (int i = 0; i < 4; i++) {
            int m = (p - P_MASTER) * 4 + i;
            kn(&k[i], MS_NAME[m], proj.master[m], 127);
        }
        break;
    case P_SETUP:
        kname(&k[0], "LIGHTS", LIGHTS_NAME, proj.lights, 3);
        kname(&k[1], "CLOCK", ONOFF, proj.send_clock != 0, 2);   /* (MIDI out: clock, notes) */
        kname(&k[2], "NOTES", ONOFF, proj.send_notes != 0, 2);
        kv(&k[3], "CORES", proj.one_core ? "one" : "two", 0, 0);
        break;
    default:
        break;
    }
}
static int knobs_shown(void)
{
    knob_t k[4];
    page_knobs(k);
    return k[0].name || k[1].name || k[2].name || k[3].name;
}
static void draw_knobs(void)
{
    knob_t k[4];
    uint32_t h = hash_u(2166136261u, ui.page | (uint32_t)ui.touched << 8), i;
    int now_touch = plat_ms() < ui.touch_until;
    static const uint16_t SHADE[4] = {K_SH0, K_SH1, K_SH2, K_SH3};
    page_knobs(k);
    for (i = 0; i < 4; i++) {
        h = hash_s(h, k[i].val);
        h = hash_s(h, k[i].name);
        h = hash_u(h, (uint32_t)k[i].num << 8 | (uint32_t)(k[i].name != 0));
    }
    h = hash_u(h, (uint32_t)now_touch);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, KNB_H, K_BG);
    if (!(k[0].name || k[1].name || k[2].name || k[3].name)) {
        cv_blit(0, KNB_Y);
        return;
    }
    cv_round(4, 0, 232, KNB_H - 2, 6, K_PANEL);
    for (i = 0; i < 4; i++) {
        int32_t x = (int32_t)i * 60, cx = x + 30, nw, nx;
        int hot = now_touch && ui.touched == (int)i;
        if (i)
            cv_rect(x, 10, 1, KNB_H - 22, K_LINE);
        if (!k[i].name)
            continue;
        nw = text_w(&FONT_XS, k[i].name);
        if (nw > 46)
            nw = 46;
        nx = cx - (nw + 9) / 2 + 9;                  /* the shade swatch, then the name */
        cv_round(nx - 9, 4, 7, 7, 2, i == 3 ? K_DIM : SHADE[i]);
        if (i == 3)
            cv_round(nx - 8, 5, 5, 5, 1, K_SH3);
        text_fit(nx, 0, &FONT_XS, k[i].name, 46, hot ? K_TEXT : K_DIM);
        if (upper_only(k[i].val) && text_w(&FONT_M, k[i].val) <= 56)
            text_c(cx, cap_y(12, 28, &FONT_M), &FONT_M, k[i].val, hot ? K_ACCENT : K_TEXT);
        else if (text_w(&FONT_B, k[i].val) <= 54)
            text_c(cx, cap_y(12, 28, &FONT_B), &FONT_B, k[i].val, hot ? K_ACCENT : K_TEXT);
        else if (text_w(&FONT_XS, k[i].val) <= 54)
            text_c(cx, cap_y(12, 28, &FONT_XS), &FONT_XS, k[i].val, hot ? K_ACCENT : K_TEXT);
        else
            text_fit(x + 3, cap_y(12, 28, &FONT_XS), &FONT_XS, k[i].val, 54, hot ? K_ACCENT : K_TEXT);
        if (k[i].max > 0) {
            int32_t bw = 44 * k[i].num / k[i].max;
            cv_round(cx - 22, 41, 44, 6, 3, K_LINE);
            if (bw > 0)
                cv_round(cx - 22, 41, bw < 6 ? 6 : bw, 6, 3, hot ? K_ACCENT : K_FAINT);
        }
    }
    cv_blit(0, KNB_Y);
}

/* ------------------------------------------------- tag, footer, cards --- */
static void track_crumb(char *t)                     /* "T3 prism>P2>S1" */
{
    int trk = sel_trk();
    char *e = s_cat(t, TRACK_NAME[trk]);
    if (!is_aux(trk)) {
        e = s_cat(e, " ");
        e = s_cat(e, EG_NAME[proj.trk[trk].par[TP_ENGINE] % NENGINE]);
    }
    if (is_seqt(trk)) {
        e = num_s((uint32_t)cur_pat(trk) + 1u, s_cat(e, ">P"));
    }
    num_s((uint32_t)cur_scene() + 1u, s_cat(e, ">S"));
}
static void page_tag(char *t)
{
    char *e;
    *t = 0;
    switch (ui.page) {
    case P_ARRANGE:
        e = num_s((uint32_t)cur_scene() + 1u, s_cat(t, "S"));
        if (seq.cue != 0xFFu && seq.cue != seq.scene)
            num_s((uint32_t)seq.cue + 1u, s_cat(e, ">next S"));
        break;
    case P_SONG:
        num_s((uint32_t)pats.song.len, s_cat(t, "song>slots "));
        break;
    case P_STEPS:
        track_crumb(t);
        if (ui.lockpg) {                             /* "T3 prism>P2>step 5>Filter locks" */
            e = num_s((uint32_t)step_shown(ui.held_first) + 1u, s_cat(t + s_len(t), ">step "));
            s_cat(s_cat(e, ">"), PAGE_NAME[ui.lockpg - 1]);
        } else if (ui.held)
            num_s((uint32_t)step_shown(ui.held_first) + 1u, s_cat(t + s_len(t), ">step "));
        else if (is_drum(sel_trk()))
            s_cat(s_cat(t + s_len(t), ">"), DL_SOUND_NAME[ui.dsound % DL_NSOUND]);
        break;
    case P_MASTER: case P_MASTER2: case P_MASTER3: case P_SETUP: case P_STATUS:
        s_cat(t, "all tracks");
        break;
    default:
        track_crumb(t);
        break;
    }
}
static void draw_tag(void)
{
    char t[80], *v = t;
    int g = page_group(ui.page), n = PAGE_BTN[g].n, d = ui.page - PAGE_BTN[g].first;
    int32_t r = 232, x, w;
    uint32_t h;
    page_tag(t);
    h = hash_s(hash_u(2166136261u, ui.page), t);
    if (h == ui.sig[3])
        return;
    ui.sig[3] = h;
    cv_begin(240, TAG_H, K_BG);
    if (n > 1) {
        r -= n * 9 - 3;
        for (int i = 0; i < n; i++)
            cv_round(r + i * 9, 3, 6, 6, 3, i == d ? K_TEXT : K_LINE);
        r -= 8;
    }
    while ((w = segs_w(&FONT_XS, v)) > r - 16 && *v) {
        char *c = v;
        while (*c && *c != '>')
            c++;
        if (*c)
            v = c + 1;
        else
            v[s_len(v) - 1] = 0;
    }
    x = r - w;
    segs(x, -2, &FONT_XS, v, K_TEXT);
    if (x - 16 >= 24)
        cv_rect(8, 6, x - 16, 1, K_LINE);
    cv_blit(0, TAG_Y);
}

/* the footer: what the keys do here; a list, a question, a card: what does what */
static void page_footer(char *t)
{
    int trk = sel_trk();
    *t = 0;
    if (ui.ask) {
        if (plat_ms() < ui.msg_until) {
            s_cat(s_cat(s_cat(t, ui.msg[0]), " "), ui.msg[1]);
            upcase(t);
        } else
            s_cat(t, "SEL yes|HOME no");
        return;
    }
    if (ui.card) {
        s_cat(t, "let go: nothing changes");
        return;
    }
    if (lst.open && lst.n) {
        int k = lst.row[lst.sel].kind;
        s_cat(t, k == LK_ACTION ? "SELECT move|SEL do it|HOME back"
               : k == LK_PICK ? "ALGO pick|SEL do it|HOME back" : "SELECT move|ALGO set|HOME back");
        return;
    }
    if (!ui.any_btn && ui.page == P_STEPS) {
        s_cat(t, "WHITE steps|PLAY play|hold a button: help");
        return;
    }
    if (ui.btn >> B_HOME & 1u) {
        s_cat(t, "HOME+KEYS punch fx|REC undo|PLAY redo");
        return;
    }
    switch (ui.page) {
    case P_ARRANGE:
        s_cat(t, "WHITE scene|BLACK mute");
        break;
    case P_SONG:
        s_cat(t, "WHITE add scene|PLAY song");
        break;
    case P_STEPS: case P_BAR:
        if (!is_seqt(trk))
            s_cat(t, "ALGO track: no steps here");
        else if (ui.lockpg)
            s_cat(t, "KNOBS lock|SAVE clear locks|let go: back");
        else if (ui.held)
            s_cat(t, "BLACK component|WHITE length|EDIT ENV FX LFO lock");
        else if (ui.rec_armed)
            s_cat(t, "KEYS write step|OCT cursor|REC off");
        else if (trk == AUX_BRAIN)
            s_cat(t, "WHITE steps|BLACK degree");
        else if (is_drum(trk))
            s_cat(t, "WHITE steps|BLACK sound|hold: edit");
        else
            s_cat(t, "WHITE steps|BLACK track|hold: edit");
        break;
    case P_MIX:
        s_cat(t, "BLACK track");
        break;
    case P_MASTER: case P_MASTER2: case P_MASTER3: case P_SETUP: case P_STATUS:
        break;
    default:
        if (trk == AUX_BRAIN)
            s_cat(t, "KEYS degree");
        else if (trk == AUX_PUNCH)
            s_cat(t, "KEYS fx|REC record");
        else if (trk == AUX_TAPE)
            s_cat(t, "WHITE segment|REC record");
        else if (is_aux(trk))
            s_cat(t, "KEYS play T1");
        else if (ui.maestro_rec)
            s_cat(t, "KEYS the chord|REC cancel");
        else if (ui.page == P_PLAYERS && proj.trk[trk].par[TP_PLAYER] % NPL == PL_MAESTRO)
            s_cat(t, "KEYS play|REC record chord");
        else if (ui.rec_armed)
            s_cat(t, "KEYS write step|OCT cursor|REC off");
        else
            s_cat(t, seq.rec ? "KEYS play|REC stop" : "KEYS play|REC record");
        break;
    }
}
/* the encoders' line: what turning SELECT, ALGORITHM, PRESETS changes here */
static void enc_line(char *t)
{
    int g = page_group(ui.page), n = PAGE_BTN[g].n, p = ui.page, trk = sel_trk();
    char *e = t;
    *t = 0;
    if (ui.ask || (lst.open && lst.n) || ui.card)
        return;
    if (ui.btn >> B_HOME & 1u) {
        s_cat(t, "SELECT tempo|PRESET scene");
        return;
    }
    if (n > 1)
        e = s_cat(t, "SELECT page");
    if (p != P_ARRANGE && p != P_SONG && p < P_MASTER)
        e = s_cat(e, *t ? "|ALGO track" : "ALGO track");
    {
        const char *a = p == P_ARRANGE ? "scene" : p == P_SONG ? "slot"
                        : (p == P_STEPS || p == P_BAR) && is_drum(trk) ? "sound"
                        : (p == P_STEPS || p == P_BAR) && is_seqt(trk) ? "pattern"
                        : p == P_ENGINE && !is_aux(trk) ? "preset"
                        : p == P_ENGINE && trk >= AUX_FX1 ? "effect"
                        : p == P_MIX ? "track" : 0;
        if (a)
            s_cat(s_cat(e, *t ? "|PRESET " : "PRESET "), a);
    }
}
#define BOT_LH 16
#define BOT_PH 15
#define BOT_PAD 3
#define BOT_PAD_E 5
#define BOT_JOB 3
#define BOT_GAP 10
#define BOT_X 6
#define BOT_W 228
#define BOT_PARTS 8
static const char *const CTL_WORD[] = {"SELECT", "ALGO", "PRESET", "SEL", "HOME", "REC", "PLAY", "OCT", "KEYS", "WHITE",
                                       "BLACK", "SAVE", "EDIT", "SEQ", "ENV", "FX", "LFO", "ARP", "GLO", "KNOBS"};
static int ctl_is(const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < sizeof CTL_WORD / sizeof CTL_WORD[0]; i++) {
        const char *w = CTL_WORD[i];
        uint32_t j = 0;
        while (j < n && w[j] && w[j] == s[j])
            j++;
        if (j == n && !w[j])
            return 1;
    }
    return 0;
}
static uint32_t ctl_len(const char *s)
{
    uint32_t n = 0, a = 0;
    for (;; n++)
        if (s[n] == '+' || s[n] == ' ' || !s[n]) {
            if (n == a || !ctl_is(s + a, n - a))
                return 0;
            if (s[n] != '+')
                return n;
            a = n + 1;
        }
}
static int32_t bot_part(int32_t x, int32_t y, const char *s, uint32_t n, const felucca_font_t *job, int draw)
{
    char b[48];
    uint32_t c = ctl_len(s), a = 0, i;
    int32_t x0 = x;
    if (c > n)
        c = 0;
    for (i = 0; c && i <= c; i++)
        if (i == c || s[i] == '+') {
            uint32_t k;
            int32_t w;
            for (k = 0; k < i - a && k < sizeof b - 1u; k++)
                b[k] = s[a + k];
            b[k] = 0;
            int enc = s_eq(b, "SELECT") || s_eq(b, "ALGO") || s_eq(b, "PRESET") || s_eq(b, "KNOBS");
            int32_t pad = enc ? BOT_PAD_E : BOT_PAD;
            w = text_w(&FONT_XS, b) + 2 * pad;
            if (draw) {
                cv_round(x, y, w, BOT_PH, enc ? 7 : 2, K_TEXT);
                cv_round(x + 1, y + 1, w - 2, BOT_PH - 2, enc ? 6 : 1, K_BG);
                cv_text(x + pad, cap_y(y, BOT_PH, &FONT_XS), &FONT_XS, b, K_TEXT);
            }
            x += w;
            if (i < c) {
                if (draw)
                    cv_text(x + 2, cap_y(y, BOT_PH, &FONT_XS), &FONT_XS, "+", K_TEXT);
                x += text_w(&FONT_XS, "+") + 4;
            }
            a = i + 1;
        }
    if (c) {
        c++;
        if (c >= n)
            return x - x0;
        x += BOT_JOB;
    }
    for (i = 0; c + i < n && i < sizeof b - 1u; i++)
        b[i] = s[c + i];
    b[i] = 0;
    if (draw)
        cv_text(x, cap_y(y, BOT_PH, job), job, b, c ? K_TEXT : K_DIM);
    return x + text_w(job, b) - x0;
}
static int ne0(const char *e)
{
    int n = *e != 0;
    for (; *e; e++)
        n += *e == '|';
    return n;
}
typedef struct { const char *s[BOT_PARTS]; uint32_t n[BOT_PARTS]; int32_t w[BOT_PARTS]; int cnt; } bot_t;
static void bot_split(bot_t *p, const char *t, const felucca_font_t *job)
{
    while (*t && p->cnt < BOT_PARTS) {
        uint32_t n = 0;
        while (t[n] && t[n] != '|')
            n++;
        p->s[p->cnt] = t;
        p->n[p->cnt] = n;
        p->w[p->cnt] = bot_part(0, 0, t, n, job, 0);
        p->cnt++;
        t += n;
        if (*t)
            t++;
    }
}
static int32_t bot_w(const bot_t *p, int a, int b)
{
    int32_t w = 0;
    for (int i = a; i < b; i++)
        w += p->w[i] + (i > a ? BOT_GAP : 0);
    return w;
}
static void bot_line(const bot_t *p, int a, int b, int32_t y, const felucca_font_t *job)
{
    int32_t tw = 0, gap = 0, x = BOT_X;
    for (int i = a; i < b; i++)
        tw += p->w[i];
    if (b - a > 1) {
        gap = (BOT_W - tw) / (b - a - 1);
        gap = gap > 28 ? 28 : gap;
    }
    for (int i = a; i < b; i++)
        x += bot_part(x, y, p->s[i], p->n[i], job, 1) + gap;
}
#ifdef XY_HOST
static int msg_too_wide;
#endif
static void draw_footer(void)
{
    char t[80], e[64];
    int list = lst.open && !ui.card, ne = 0, split = -1, k;
    const felucca_font_t *job = &FONT_S;
    bot_t p;
    uint32_t h = hash_u(2166136261u, (uint32_t)list);
    page_footer(t);
    enc_line(e);
    if (list)
        *e = 0;
    h = hash_s(h, t);
    h = hash_s(h ^ 0x80u, e);
    if (h == ui.sig[4])
        return;
    ui.sig[4] = h;
    for (int f = 0; f < 2 && split < 0; f++)
        for (int drop = 0; drop <= ne0(e) && split < 0; drop++) {
            job = f ? &FONT_XS : &FONT_S;
            memset(&p, 0, sizeof p);
            bot_split(&p, e, job);
            p.cnt -= drop;
            ne = p.cnt;
            bot_split(&p, t, job);
            if (bot_w(&p, 0, p.cnt) <= BOT_W)
                split = 0;
            else if (!list)
                for (int d = 0; d < p.cnt && split < 0; d++)
                    for (int sg = -1; sg <= 1 && split < 0; sg += 2) {
                        k = ne + sg * d;
                        if (k > 0 && k < p.cnt && bot_w(&p, 0, k) <= BOT_W && bot_w(&p, k, p.cnt) <= BOT_W)
                            split = k;
                    }
        }
#if defined(XY_HOST) && !defined(XY_WEB)
    if (split < 0 || job != &FONT_S) {
        printf("  FAIL bottom lines too wide: %s / %s\n", e, t);
        msg_too_wide++;
    }
#endif
    if (split < 0)
        split = p.cnt > 1 ? ne > 0 && ne < p.cnt ? ne : 1 : 0;
    if (list) {
        cv_begin(240, BOT_LH, K_BG);
        bot_line(&p, 0, p.cnt, 0, job);
        cv_blit(0, FOOT_Y);
        return;
    }
    cv_begin(240, 2 * BOT_LH, K_BG);
    if (!split)
        bot_line(&p, 0, p.cnt, BOT_LH / 2, job);
    else {
        bot_line(&p, 0, split, 0, job);
        bot_line(&p, split, p.cnt, BOT_LH, job);
    }
    cv_blit(0, ENC_Y);
}

/* the help cards: a button held alone for a second says what it does on this page */
#define PG(p) (1u << (p))
#define PG_ANY 0xFFFFFFFFu
#define PG_STEPS (PG(P_STEPS) | PG(P_BAR))
#define PG_SOUND (PG(P_ENGINE) | PG(P_PLAY) | PG(P_ENV) | PG(P_ENV2) | PG(P_FILTER) | PG(P_SENDS) | PG(P_LFO) | PG(P_LFO2))
static const struct { uint8_t btn; uint32_t pages; const char *title, *l1, *l2; } CARD[] = {
    {B_SEQ, PG_ANY, "Steps", "The bar's 16 steps on the white keys. A step toggles the notes.",
     "Hold a step to see it; a black key adds a component."},
    {B_EDIT, PG(P_STEPS), "Lock", "With a step held, EDIT, ENV, FX or LFO show that page's knobs.",
     "Turn one to lock a value on the step. SAVE clears its locks."},
    {B_EDIT, PG_ANY, "Engine", "The track's engine and its four knobs. PRESETS: the next sound.",
     "SEL lists engines, presets, scramble and reset. Page 2: Play."},
    {B_ENV, PG_ANY, "Envelope", "Attack, decay, sustain and release of the amp.", "Page 2 is the filter's envelope."},
    {B_FX, PG_ANY, "Filter", "Cutoff, resonance, envelope amount and key tracking.",
     "Page 2: the filter type and the sends to FX I, FX II and tape."},
    {B_LFO, PG_ANY, "LFO", "Type, speed, amount and destination.", "Page 2 has the type's own setting."},
    {B_ARP, PG_ANY, "Players", "Arp, maestro or hold on the track's keys.",
     "With maestro, REC records the chord you hold next. A key plays it."},
    {B_GLO, PG_ANY, "Mix", "Level, pan and sends of the track. Black keys pick a track.",
     "SELECT: Master, Setup and Status. SEL has the projects."},
    {B_HOME, PG_ANY, "Arrange", "Each track's pattern in the scene. White keys: scenes, black: mutes.",
     "Hold HOME + a key: punch-in. HOME + REC undoes, HOME + PLAY redoes."},
    {B_SEL, PG_ANY, "Lists", "The page's knobs, then its actions. SEL runs the one picked.",
     "SELECT moves, ALGORITHM changes, HOME goes back."},
    {B_SAVE, PG(P_STEPS), "Save", "Tap to save the project. With a step held, clears its locks.",
     "Hold SAVE and press white key 1 to 4 to save to that slot."},
    {B_SAVE, PG_SOUND, "Save", "Tap to save the project.",
     "Hold SAVE and press a white key: the sound as that user preset."},
    {B_SAVE, PG_ANY, "Save", "Tap to save the project.", "Hold SAVE and press white key 1 to 4 to save to that slot."},
    {B_REC, PG(P_PLAYERS) | PG(P_PLAYERS2), "Record", "With maestro, the chord you hold next becomes its chord.",
     "Else: playing, the keys write steps; stopped, step by step."},
    {B_REC, PG_ANY, "Record", "Playing: the keys write steps as you play. Stopped: step record.",
     "A key writes the cursor's step. REC then PLAY counts in."},
    {B_PLAY, PG(P_SONG), "Play", "Plays the song from slot 1. PLAY again stops.", "HOME + PLAY redoes."},
    {B_PLAY, PG_ANY, "Play", "Starts and stops. Twice quickly: from the top.", "HOME + PLAY redoes."},
    {B_OCTDN, PG_STEPS, "Bar", "Shows the bar before. Step record: the cursor back a step.",
     "Hold both for 5 seconds for update mode."},
    {B_OCTUP, PG_STEPS, "Bar", "Shows the next bar. Step record: the cursor on a step.",
     "Hold both for 5 seconds for update mode."},
    {B_OCTDN, PG_ANY, "Octave", "The keys an octave down.", "Hold both for 5 seconds for update mode."},
    {B_OCTUP, PG_ANY, "Octave", "The keys an octave up.", "Hold both for 5 seconds for update mode."},
};
#define NCARDS (sizeof CARD / sizeof CARD[0])
static int card_find(int btn)
{
    for (uint32_t i = 0; i < NCARDS; i++)
        if (CARD[i].btn == btn && (CARD[i].pages >> ui.page & 1u))
            return (int)i;
    return -1;
}
static void draw_card(void)
{
    int i = card_find(ui.card - 1);
    uint32_t h = hash_u(2166136261u, 0xCA000u + ui.card + ((uint32_t)ui.page << 8));
    int32_t x, y;
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    cv_round(8, 4, 224, MAIN_H - 8, 8, K_PANEL);
    {
        int32_t n = i < 0 ? 0 : wrap_do(0, 0, 200, &FONT_S, CARD[i].l1, 0, 0) / 17 +
                                (CARD[i].l2 ? wrap_do(0, 0, 200, &FONT_S, CARD[i].l2, 0, 0) / 17 : 0);
        int32_t bh = i < 0 ? 20 : 42 + (n - 1) * 17 + (CARD[i].l2 ? 4 : 0), top = 4 + (MAIN_H - 8 - bh) / 2;
        if (top < 10)
            top = 10;
        x = pill_o(18, top, 0, BTN_LABEL[(ui.card - 1) % NB], K_TEXT, K_TEXT);
        y = top + 30;
    }
    if (i >= 0) {
        cv_text(x + 10, cap_y(y - 30, 20, &FONT_B), &FONT_B, CARD[i].title, K_TEXT);
        y = wrap(20, y, 200, &FONT_S, CARD[i].l1, K_TEXT);
        if (CARD[i].l2)
            y = wrap(20, y + 4, 200, &FONT_S, CARD[i].l2, K_DIM);
#if defined(XY_HOST) && !defined(XY_WEB)
        if (y - 1 > MAIN_H - 4) {
            printf("  FAIL card too long: %s\n", CARD[i].title);
            msg_too_wide++;
        }
#endif
    }
    cv_blit(0, MAIN_Y);
}

/* ------------------------------------------------------------- lists --- */
static void list_add(const char *name, const char *val, int kind, int id)
{
    if (lst.n >= LIST_ROWS)
        return;
    lst.row[lst.n].name = name;
    lst.row[lst.n].val[0] = 0;
    if (val)
        for (int i = 0; i < 23 && val[i]; i++)
            lst.row[lst.n].val[i] = val[i], lst.row[lst.n].val[i + 1] = 0;
    lst.row[lst.n].kind = (uint8_t)kind;
    lst.row[lst.n].id = (uint8_t)id;
    lst.n++;
}
static void proj_slot_text(int slot, char *t)        /* "1: Summer", "2: empty" */
{
    char *e = num_s((uint32_t)slot + 1u, t);
    e = s_cat(e, ": ");
    if (project_exists(slot)) {
        char nm[NAME_LEN + 1];
        project_name_of(slot, nm);
        nm[NAME_LEN] = 0;
        s_cat(e, nm[0] ? nm : "project");
    } else
        s_cat(e, "empty");
}
static void list_build(void)
{
    int keep = ui.page, g = page_group(keep), f = PAGE_BTN[g].first, n = PAGE_BTN[g].n, trk = sel_trk();
    knob_t k[4];
    static char slot_t[24], fol_t[24];
    lst.n = 0;
    if (keep >= P_MIX)                               /* GLO: the list of the page alone (six pages of knobs is too many) */
        f = keep, n = 1;
    for (int p = f; p < f + n; p++) {
        ui.page = (uint8_t)p;
        page_knobs(k);
        for (int i = 0; i < 4; i++) {
            int seen = 0;
            for (int j = 0; j < lst.n; j++)
                seen |= k[i].name && s_eq(lst.row[j].name, k[i].name) && s_eq(lst.row[j].val, k[i].val);
            if (k[i].name && !seen)
                list_add(k[i].name, k[i].val, LK_KNOB, p << 2 | i);
        }
    }
    ui.page = (uint8_t)keep;
    if (keep == P_STEPS || keep == P_BAR) {
        if (is_seqt(trk)) {
            list_add(ACTION_NAME[A_CLEAR_NOTES], 0, LK_ACTION, A_CLEAR_NOTES);
            list_add(ACTION_NAME[A_CLEAR_LOCKS], 0, LK_ACTION, A_CLEAR_LOCKS);
            list_add(ACTION_NAME[A_CLEAR_BOTH], 0, LK_ACTION, A_CLEAR_BOTH);
            list_add(ACTION_NAME[A_COPY_PAT], 0, LK_ACTION, A_COPY_PAT);
            if (clip_ok)
                list_add(ACTION_NAME[A_PASTE_PAT], 0, LK_ACTION, A_PASTE_PAT);
            list_add(ACTION_NAME[A_ROT_FWD], 0, LK_ACTION, A_ROT_FWD);
            list_add(ACTION_NAME[A_ROT_BACK], 0, LK_ACTION, A_ROT_BACK);
            list_add(ACTION_NAME[A_DUP_BAR], 0, LK_ACTION, A_DUP_BAR);
        }
    } else if (keep == P_ARRANGE) {
        list_add(ACTION_NAME[A_CLEAR_SCENE], 0, LK_ACTION, A_CLEAR_SCENE);
        list_add(ACTION_NAME[A_PANIC], 0, LK_ACTION, A_PANIC);
    } else if (keep == P_SONG) {
        list_add(ACTION_NAME[A_INSERT_SLOT], 0, LK_ACTION, A_INSERT_SLOT);
        list_add(ACTION_NAME[A_DELETE_SLOT], 0, LK_ACTION, A_DELETE_SLOT);
        list_add(ACTION_NAME[A_CLEAR_SONG], 0, LK_ACTION, A_CLEAR_SONG);
        list_add(ACTION_NAME[A_PLAY_SONG], 0, LK_ACTION, A_PLAY_SONG);
    } else if (keep >= P_ENGINE && keep <= P_PLAYERS2) {
        if (trk == AUX_BRAIN) {                      /* each track: follows the brain how */
            for (int t = 0; t < NTRK; t++) {
                static char nm[NTRK][16];        /* "T3 FOLLOWS" (ui.c list_follow_trk reads the digit) */
                s_cat(num_s((uint32_t)t + 1u, s_cat(nm[t], "T")), " FOLLOWS");
                list_add(nm[t], FOLLOW_NAME[proj.trk[t].par[TP_BRAIN] % 3], LK_PICK, A_FOLLOW);
            }
        } else if (!is_aux(trk)) {
            list_add(ACTION_NAME[A_ENGINE], EG_NAME[ui.pick_engine % NENGINE], LK_PICK, A_ENGINE);
            if (NPRESETS > 0)
                list_add(ACTION_NAME[A_PRESET], PRESETS[ui.pick_preset % NPRESETS].name, LK_PICK, A_PRESET);
            list_add(ACTION_NAME[A_SCRAMBLE], 0, LK_ACTION, A_SCRAMBLE);
            num_s((uint32_t)ui.pick_slot % NUSER_PRESETS + 1u, s_cat(slot_t, "U"));
            list_add(ACTION_NAME[A_SAVE_PRESET], slot_t, LK_PICK, A_SAVE_PRESET);
            list_add(ACTION_NAME[A_INIT], 0, LK_ACTION, A_INIT);
            s_cat(fol_t, FOLLOW_NAME[proj.trk[trk].par[TP_BRAIN] % 3]);
            list_add(ACTION_NAME[A_FOLLOW], fol_t, LK_PICK, A_FOLLOW);
        }
    } else if (keep == P_MIX) {
        if (!is_aux(trk)) {
            s_cat(fol_t, FOLLOW_NAME[proj.trk[trk].par[TP_BRAIN] % 3]);
            list_add(ACTION_NAME[A_FOLLOW], fol_t, LK_PICK, A_FOLLOW);
        }
        list_add(ACTION_NAME[A_PANIC], 0, LK_ACTION, A_PANIC);
    } else if (keep == P_SETUP || keep == P_STATUS) {
        proj_slot_text(ui.pick_slot % NPROJ, slot_t);
        list_add(ACTION_NAME[A_LOAD_PROJ], slot_t, LK_PICK, A_LOAD_PROJ);
        list_add(ACTION_NAME[A_NEW_PROJ], 0, LK_ACTION, A_NEW_PROJ);
        list_add(ACTION_NAME[A_USB_AUDIO], USBA_NAME[proj.usb_full & 1u], LK_PICK, A_USB_AUDIO);
    }
    if (lst.sel >= lst.n)
        lst.sel = (uint8_t)(lst.n ? lst.n - 1 : 0);
}
static void glyph_enter(int32_t x, int32_t y, uint16_t c)
{
    cv_rect(x + 8, y, 2, 7, c);
    cv_rect(x + 2, y + 5, 8, 2, c);
    cv_rect(x + 2, y + 3, 1, 6, c);
    cv_rect(x + 1, y + 4, 1, 4, c);
    cv_rect(x, y + 5, 1, 2, c);
}
static void glyph_pick(int32_t x, int32_t y, uint16_t c)
{
    for (int j = 0; j < 4; j++)
        cv_rect(x + j, y + j, 1, 9 - 2 * j, c);
}
#define LIST_RH 20
#define LIST_Y 24
#define LIST_H (FOOT_Y - LIST_Y)
static void list_paint(int32_t oy)
{
    uint32_t i;
    for (i = lst.top; i < lst.n && i < lst.top + LIST_SHOWN; i++) {
        int32_t y = 1 + (int32_t)(i - lst.top) * LIST_RH - oy, ty = cap_y(y, LIST_RH - 1, &FONT_S);
        int on = i == lst.sel, k = lst.row[i].kind;
        uint16_t fg = on ? K_BG : K_TEXT, dim = on ? K_LINE : K_DIM;
        const char *v = lst.row[i].val;
        cv_round(6, y, 222, LIST_RH - 1, 5, on ? K_TEXT : K_PANEL);
        if (k == LK_KNOB) {
            cv_text(14, ty, &FONT_S, lst.row[i].name, dim);
            text_fit(218 - text_w(&FONT_B, v), ty, &FONT_B, v, 120, fg);
        } else {
            int32_t ne = cv_text(14, ty, &FONT_B, lst.row[i].name, fg);
            if (k == LK_PICK) {
                int32_t room = 206 - (ne + 6), w = text_w(&FONT_S, v);
                text_fit(206 - (w < room ? w : room), ty, &FONT_S, v, room, fg);
                glyph_pick(213, ty + 3, fg);
            } else {
                if (*v && 202 - text_w(&FONT_S, v) >= ne + 6)
                    cv_text(202 - text_w(&FONT_S, v), ty, &FONT_S, v, dim);
                glyph_enter(208, ty + 3, fg);
            }
        }
    }
    if (lst.n > LIST_SHOWN) {
        int32_t th = LIST_SHOWN * LIST_RH - 2, bh = th * LIST_SHOWN / lst.n, by = th * lst.top / lst.n;
        cv_round(232, 1 - oy, 4, th, 2, K_PANEL);
        cv_round(232, 1 + by - oy, 4, bh, 2, K_FAINT);
    }
}
static void draw_list(void)
{
    uint32_t h = hash_u(2166136261u, 0x4C000u | lst.sel | (uint32_t)lst.top << 8), i;
    list_build();
    h = hash_u(h, lst.n);
    for (i = 0; i < lst.n; i++) {
        h = hash_u(h, lst.row[i].kind | (uint32_t)lst.row[i].id << 4);
        h = hash_s(h, lst.row[i].name);
        h = hash_s(h, lst.row[i].val);
    }
    ui.sig[2] = ui.sig[3] = ui.sig[5] = 0;
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    list_paint(0);
    cv_blit(0, LIST_Y);
    cv_begin(240, LIST_H - MAIN_H, K_BG);
    list_paint(MAIN_H);
    cv_blit(0, LIST_Y + MAIN_H);
}

/* ------------------------------------------------------- the pages --- */
/* a band's hash, then its drawing: begin_main() returns 0 when nothing changed */
static int begin_main(uint32_t h)
{
    if (h == ui.sig[1])
        return 0;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    return 1;
}
static uint32_t hash_pat(uint32_t h, const tpat_t *p)
{
    const uint32_t *w = (const uint32_t *)(const void *)p;
    for (uint32_t i = 0; i < sizeof *p / 4u; i++)
        h = hash_u(h, w[i]);
    return h;
}

/* Arrange: eleven rows (the tracks), each its pattern and a playhead, mutes dimmed; the scene big at the right */
static void draw_arrange(void)
{
    int sc = cur_scene(), cue = seq.cue;
    uint32_t h = hash_u(2166136261u, 0xA0000u | (uint32_t)sc | (uint32_t)cue << 8 | (uint32_t)playing() << 16);
    char t[16];
    for (int tr = 0; tr < NSEQT; tr++)
        h = hash_u(h, (uint32_t)seq.ppat[tr] | (uint32_t)eng_step[tr] << 8 | (uint32_t)(seq.mute >> tr & 1u) << 16 |
                          (uint32_t)tpat(tr)->len << 20);
    for (int i = 0; i < NSCENE; i++)
        h = hash_u(h, pats.scene[i].used);
    if (!begin_main(h))
        return;
    for (int tr = 0; tr < NSEQT; tr++) {
        int32_t y = 2 + tr * 10 + (tr >= NTRK ? 4 : 0);
        int muted = seq.mute >> tr & 1u, pt = seq.ppat[tr] % NPATT, len = tpat(tr)->len, pos = eng_step[tr];
        uint16_t ink = muted ? K_FAINT : K_TEXT;
        char nm[8];
        if (tr < NTRK)
            num_s((uint32_t)tr + 1u, nm);
        else
            s_cat(nm, tr == AUX_BRAIN ? "BR" : tr == AUX_PUNCH ? "PU" : "TP");
        text_r(22, y - 3, &FONT_XS, nm, tr == sel_trk() ? K_ACCENT : ink);
        num_s((uint32_t)pt + 1u, s_cat(t, "P"));
        cv_round(27, y, 20, 8, 3, muted ? K_LINE : K_PANEL2);
        text_c(37, y - 3, &FONT_XS, t, ink);
        cv_rect(52, y + 3, 96, 2, muted ? K_LINE : K_PANEL2);   /* the pattern's length, its playhead */
        if (len > 0 && len <= NSTEP)
            cv_rect(52, y + 3, 96 * len / NSTEP, 2, muted ? K_LINE : K_LINE);
        if (playing() && !muted && len > 0)
            cv_round(52 + 94 * (pos % len) / (len > 1 ? len - 1 : 1), y + 1, 3, 6, 1, K_TEAL);
        if (muted)
            cv_text(152, y - 3, &FONT_XS, "mute", K_FAINT);
    }
    cv_rect(168, 4, 1, 112, K_LINE);
    text_c(204, 8, &FONT_XS, cue != 0xFF && cue != sc ? "scene, next" : "scene", K_DIM);
    num_s((uint32_t)sc + 1u, t);
    text_c(204, 24, &FONT_L, t, playing() ? K_TEAL : K_TEXT);
    if (cue != 0xFF && cue != sc) {
        num_s((uint32_t)cue + 1u, t);
        text_c(204, 62, &FONT_B, t, K_ACCENT);
    }
    for (int i = 0; i < NSCENE; i++) {              /* the 16 scenes: used ones filled, the current ringed */
        int32_t x = 176 + (i % 8) * 7, y = 92 + (i / 8) * 9;
        cv_round(x, y, 5, 6, 1, pats.scene[i].used ? (i == sc ? K_TEAL : K_DIM) : K_LINE);
        if (i == cue && cue != sc)
            cv_round(x, y, 5, 6, 1, K_ACCENT);
    }
    cv_blit(0, MAIN_Y);
}

/* Song: the chain of scene slots, 16 to a screen, the cursor ringed, the slot playing teal */
static void draw_song(void)
{
    const song_t *sg = &pats.song;
    int cur = ui.name_slot <= sg->len ? ui.name_slot : sg->len, top = (cur / 16) * 16, pp = seq.song_pos;
    uint32_t h = hash_u(2166136261u, (5u << 16) | (uint32_t)cur | (uint32_t)sg->len << 8 | (uint32_t)sg->loop << 16 |
                                          (uint32_t)(seq.song_on && playing() ? pp + 1 : 0) << 20);
    char t[16];
    for (int i = top; i < top + 16 && i < sg->len; i++)
        h = hash_u(h, sg->slot[i].scene | (uint32_t)sg->slot[i].rep << 8);
    if (!begin_main(h))
        return;
    if (!sg->len) {
        text_c(120, 40, &FONT_B, "No scenes in the song yet", K_TEXT);
        text_c(120, 62, &FONT_S, "Press a white key to add scene 1 to 16", K_DIM);
        cv_blit(0, MAIN_Y);
        return;
    }
    for (int i = top; i < top + 16 && i <= sg->len; i++) {
        int32_t x = 8 + (i - top) % 4 * 57, y = 6 + (i - top) / 4 * 27;
        int on = seq.song_on && playing() && i == pp;
        if (i == sg->len) {                          /* the end: an outlined "end" / "loop" cell */
            cv_frame(x, y, 52, 22, 5, i == cur ? K_ACCENT : K_LINE, K_BG);
            text_c(x + 26, y + 3, &FONT_XS, sg->loop ? "loop" : "end", K_DIM);
            continue;
        }
        cv_round(x, y, 52, 22, 5, on ? K_TEAL : K_PANEL);
        if (i == cur) {
            cv_round(x, y, 52, 22, 5, K_ACCENT);
            cv_round(x + 2, y + 2, 48, 18, 4, on ? K_TEAL : K_PANEL);
        }
        num_s((uint32_t)sg->slot[i].scene + 1u, s_cat(t, "S"));
        cv_text(x + 6, y + 3, &FONT_B, t, on ? K_BG : K_TEXT);
        num_s((uint32_t)sg->slot[i].rep, s_cat(t, "x"));
        text_r(x + 47, y + 4, &FONT_XS, t, on ? K_BG : K_DIM);
    }
    if (sg->len >= 16) {
        num_s((uint32_t)sg->len, s_cat(num_s((uint32_t)top + 1u, s_cat(t, "slots ")), " to "));
        text_r(232, 112, &FONT_XS, t, K_FAINT);
    }
    cv_blit(0, MAIN_Y);
}

/* Steps: the remembered notes and the bars' dots, then the bar's 16 cells */
#define ST_X 4
#define ST_CW 29
#define ST_Y 22
#define ST_CH 46
static void cell_notes(const step_t *st, int drum, char *t)
{
    char *e = t;
    *t = 0;
    if (drum)                                        /* the shown sound first, when the step has it */
        for (int i = 0; i < SN; i++)
            if (st->note[i] == ui.dsound + 1) {
                s_cat(t, DL_SOUND_NAME[ui.dsound % DL_NSOUND]);
                return;
            }
    if (sel_trk() == AUX_BRAIN && st->note[0]) {     /* a brain step: the degree, 60 = 0 */
        int d = st->note[0] - 60;
        if (d > 0)
            e = s_cat(e, "+");
        num_i(d, e);
        return;
    }
    for (int i = 0; i < SN && st->note[i]; i++) {
        if (i)
            e = s_cat(e, " ");
        if (drum)
            e = s_cat(e, DL_SOUND_NAME[(st->note[i] - 1) % DL_NSOUND]);
        else
            e = note_name(st->note[i], e);
    }
}
static void draw_steps(void)
{
    int trk = sel_trk(), drum = is_drum(trk), pt = cur_pat(trk), ph = eng_step[trk];
    const tpat_t *tp = tpat(trk);
    uint32_t h = hash_u(2166136261u, 0x57000u | (uint32_t)trk | (uint32_t)pt << 4 | (uint32_t)ui.bar << 8 |
                                          (uint32_t)ui.held << 10 | (uint32_t)ui.dsound << 26);
    char t[40];
    h = hash_u(h, (uint32_t)ph | (uint32_t)playing() << 8 | (uint32_t)ui.rec_armed << 9 | (uint32_t)ui.cursor << 16 |
                      (uint32_t)ui.lockpg << 24);
    h = hash_pat(h, tp);
    for (int i = 0; i < 4; i++)
        h = hash_u(h, ui.rem[i]);
    h = hash_u(h, lane_count(pats.lane));
    if (!is_seqt(trk)) {
        h = hash_u(h, 0xFEu);
        if (!begin_main(h))
            return;
        text_c(120, 40, &FONT_B, TRACK_NAME[trk], K_TEXT);
        text_c(120, 62, &FONT_S, "has no steps. Turn ALGORITHM for a track.", K_DIM);
        cv_blit(0, MAIN_Y);
        return;
    }
    for (int i = 0; i < 16; i++)
        h = hash_u(h, step_has_lock(pats.lane, trk, pt, ui.bar * 16 + i));
    if (!begin_main(h))
        return;
    /* the top line: what a press writes (the remembered notes, the drum sound), the bars */
    if (drum)
        s_cat(s_cat(t, "sound: "), DL_SOUND_NAME[ui.dsound % DL_NSOUND]);
    else if (trk == AUX_BRAIN)
        s_cat(t, "steps: the brain's degree");
    else if (trk == AUX_PUNCH)
        s_cat(t, "steps: punch-in fx");
    else if (trk == AUX_TAPE)
        s_cat(t, "steps: tape segments");
    else {
        char *e = s_cat(t, "notes: ");
        for (int i = 0; i < ui.nrem && i < SN; i++) {
            if (i)
                e = s_cat(e, " ");
            e = note_name(ui.rem[i], e);
        }
    }
    cv_text(ST_X + 2, 0, &FONT_XS, t, K_DIM);
    {
        int nb = (tp->len + 15) / 16, pb = ph / 16;
        for (int b = 0; b < NBAR; b++) {            /* the bars: the one shown ringed, the one playing teal */
            int32_t x = 176 + b * 14;
            uint16_t c = b < nb ? K_DIM : K_LINE;
            cv_round(x, 3, 10, 8, 2, b == pb && playing() ? K_TEAL : c);
            if (b == ui.bar)
                cv_frame(x - 2, 1, 14, 12, 3, K_ACCENT, b == pb && playing() ? K_TEAL : c);
            num_s((uint32_t)b + 1u, t);
            text_c(x + 5, -1, &FONT_XS, t, b == ui.bar || (b == pb && playing()) ? K_BG : K_TEXT);
        }
    }
    for (int i = 0; i < 16; i++) {
        int s = ui.bar * 16 + i, in = s < tp->len, held = ui.held >> i & 1u, on;
        const step_t *st = &tp->st[s];
        int32_t x = ST_X + (i % 8) * ST_CW, y = ST_Y + (i / 8) * (ST_CH + 4);
        uint16_t bg;
        on = drum ? 0 : !step_empty(st);
        if (drum)
            for (int k = 0; k < SN; k++)
                on |= st->note[k] == ui.dsound + 1;
        bg = !in ? K_BG : on ? K_PANEL2 : K_PANEL;
        if (playing() && s == ph && in)
            bg = on ? K_TEAL : K_TEAL_T;
        cv_round(x, y, ST_CW - 2, ST_CH, 4, bg);
        if (!in)
            cv_frame(x, y, ST_CW - 2, ST_CH, 4, K_LINE, K_BG);
        if (held)
            cv_frame(x, y, ST_CW - 2, ST_CH, 4, K_ACCENT, bg), cv_frame(x + 1, y + 1, ST_CW - 4, ST_CH - 2, 3, K_ACCENT, bg);
        if (ui.rec_armed && s == ui.cursor)
            cv_frame(x, y, ST_CW - 2, ST_CH, 4, K_REC, bg);
        num_s((uint32_t)s + 1u, t);
        cv_text(x + 3, y - 1, &FONT_XS, t, bg == K_TEAL ? K_BG : K_FAINT);
        if (drum && !step_empty(st) && !on)           /* another sound on the step: a small mark */
            cv_round(x + ST_CW - 9, y + 3, 4, 4, 2, K_FAINT);
        if (on || (drum && !step_empty(st))) {
            cell_notes(st, drum, t);
            if (drum) {                              /* the sound's first letters */
                t[3] = 0;
                text_c(x + (ST_CW - 2) / 2, y + 15, &FONT_XS, t, bg == K_TEAL ? K_BG : on ? K_TEXT : K_FAINT);
            } else if (trk == AUX_BRAIN) {           /* a degree */
                text_c(x + (ST_CW - 2) / 2, y + 15, &FONT_S, t, bg == K_TEAL ? K_BG : K_TEXT);
            } else {
                char l1[12], *e = l1;
                int n = 0;
                for (int k = 0; k < SN && st->note[k]; k++) {   /* two notes a line */
                    e = note_name(st->note[k], e);
                    n++;
                    if (n == 2 || !st->note[k + 1] || k == SN - 1) {
                        text_c(x + (ST_CW - 2) / 2, y + (n <= 2 && k < 2 ? 13 : 25), &FONT_XS, l1,
                               bg == K_TEAL ? K_BG : K_TEXT);
                        e = l1;
                        if (k >= 1)
                            n = 3;
                    } else
                        e = s_cat(e, " ");
                }
            }
        }
        if (st->comp)
            text_r(x + ST_CW - 5, y + ST_CH - 14, &FONT_XS, COMP_GLYPH[st->comp % NSC], bg == K_TEAL ? K_BG : K_ACCENT);
        if (st->cond)
            cv_text(x + 3, y + ST_CH - 14, &FONT_XS, "%", bg == K_TEAL ? K_BG : K_DIM);
        if (step_has_lock(pats.lane, trk, pt, s))
            cv_round(x + ST_CW - 9, y + 3, 4, 4, 2, bg == K_TEAL ? K_BG : K_ACCENT);
    }
    cv_blit(0, MAIN_Y);
}

/* Bar: the pattern's settings in words, and its bars as blocks */
static void draw_bar(void)
{
    int trk = sel_trk();
    const tpat_t *tp = tpat(trk);
    uint32_t h = hash_u(2166136261u, 0xBA000u | (uint32_t)trk | (uint32_t)cur_pat(trk) << 4 | (uint32_t)tp->len << 8 |
                                          (uint32_t)tp->scale << 16 | (uint32_t)eng_step[trk] << 20 | (uint32_t)playing() << 28);
    char t[40], *e;
    if (!begin_main(h))
        return;
    if (!is_seqt(trk)) {
        text_c(120, 50, &FONT_S, "No steps on this track", K_DIM);
        cv_blit(0, MAIN_Y);
        return;
    }
    num_s((uint32_t)tp->len, s_cat(t, "Length "));
    e = s_cat(t + s_len(t), " steps, a step is ");
    e = s_cat(e, TS_NAME[tp->scale % NTS]);
    s_cat(e, tp->scale == TS_1 ? " sixteenth" : " sixteenths");
    cv_text(12, 6, &FONT_S, t, K_TEXT);
    {
        int nb = (tp->len + 15) / 16, ph = eng_step[trk];
        for (int b = 0; b < NBAR; b++) {
            int32_t x = 12 + b * 56, w = 50;
            int steps = tp->len - b * 16;
            steps = steps > 16 ? 16 : steps < 0 ? 0 : steps;
            cv_round(x, 34, w, 40, 5, b < nb ? K_PANEL2 : K_PANEL);
            if (steps < 16 && steps > 0)
                cv_round(x + w * steps / 16, 34, w - w * steps / 16, 40, 5, K_PANEL);
            if (playing() && ph / 16 == b)
                cv_round(x + 2 + 46 * (ph % 16) / 16, 36, 4, 36, 2, K_TEAL);
            num_s((uint32_t)b + 1u, t);
            text_c(x + w / 2, 46, &FONT_B, t, b < nb ? K_TEXT : K_FAINT);
        }
    }
    s_cat(s_cat(t, "Groove "), GROOVE_NAME[tp->groove % NGROOVE]);
    e = num_s((uint32_t)tp->quant, s_cat(t + s_len(t), ", quantise "));
    s_cat(e, "%");
    cv_text(12, 86, &FONT_XS, t, K_DIM);
    cv_text(12, 100, &FONT_XS, "SEL: clear, copy, paste, rotate, duplicate a bar", K_FAINT);
    cv_blit(0, MAIN_Y);
}

/* the engines' pictures: one each, simple, animated by the frame and the track's level */
static void pic_simple(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    for (int k = 0; k < 5; k++) {                   /* slinky arcs, bending by shape, sliding by the frame */
        int32_t cy = y0 + h / 2, amp = 8 + p[TP_E1] * (h / 2 - 10) / 127;
        for (int32_t x = 0; x < w - 1; x += 2) {
            int32_t ph = (x * 32 / w + f / 2 + k * 6) & 31;
            int32_t y = cy + sin100((uint32_t)ph) * amp / 100 + (k - 2) * 3;
            cv_rect(x0 + x, y, 2, 2, k == 2 ? K_TEAL : K_DIM);
        }
    }
}
static void pic_prism(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    int32_t cx = x0 + w / 2, top = y0 + 6, bot = y0 + h - 6, spread = 6 + p[TP_E2] * 30 / 127;
    (void)f;
    cv_line(cx, top, cx - 36, bot, K_TEXT);
    cv_line(cx, top, cx + 36, bot, K_TEXT);
    cv_line(cx - 36, bot, cx + 36, bot, K_TEXT);
    cv_line(x0 + 4, y0 + h / 2 + 10, cx - 14, y0 + h / 2 - 4, K_DIM);   /* the ray in */
    for (int k = -1; k <= 1; k++)                    /* three out, spread by the ratio */
        cv_line(cx + 14, y0 + h / 2 - 4 + k * 2, x0 + w - 4, y0 + h / 2 + k * spread, k ? K_ACCENT : K_TEAL);
}
static void pic_wave(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    uint32_t r = eng_scope_w;
    int32_t cy = y0 + h / 2, py = cy;
    (void)p; (void)f;
    for (int32_t x = 0; x < w; x++) {
        int32_t s = eng_scope[(r + SCOPE_N - (uint32_t)w + (uint32_t)x) % SCOPE_N];
        int32_t y = cy - s * (h / 2 - 4) / 32768;
        cv_line(x0 + x, py, x0 + x, y, K_TEAL);
        py = y;
    }
    cv_rect(x0, cy, w, 1, K_LINE);
}
static void pic_axis(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    int32_t cx = x0 + w / 2, cy = y0 + h / 2, r1 = h / 2 - 6, r2 = 6 + p[TP_E2] * 14 / 127;
    for (uint32_t k = 0; k < 32; k++)                /* the carrier's ring, the modulator running round it */
        cv_rect(cx + cos100(k) * r1 / 100, cy + sin100(k) * r1 / 100, 2, 2, K_DIM);
    {
        uint32_t k = f / 2;
        int32_t mx = cx + cos100(k) * r1 / 100, my = cy + sin100(k) * r1 / 100;
        for (uint32_t j = 0; j < 32; j += 2)
            cv_rect(mx + cos100(j + f) * r2 / 100, my + sin100(j + f) * r2 / 100, 2, 2, K_TEAL);
        cv_line(cx, cy, mx, my, K_ACCENT);
    }
}
static void pic_epiano(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    int32_t kw = w / 8;
    (void)f;
    cv_round(x0 + 4, y0 + 6, w - 8, 10, 3, K_DIM);   /* the tine bar */
    cv_round(x0 + 4 + p[TP_E4] * (w - 20) / 127, y0 + 4, 12, 14, 4, K_ACCENT);
    for (int k = 0; k < 8; k++)
        cv_round(x0 + k * kw + 1, y0 + 24, kw - 2, h - 28, 2, k % 7 == 1 || k % 7 == 3 || k % 7 == 6 ? K_TEXT : K_PANEL2);
}
static void pic_organ(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    (void)f;
    for (int k = 0; k < 4; k++) {                    /* four drawbars: type, bass, tremolo, speed */
        int32_t x = x0 + 20 + k * (w - 40) / 3 - 6, v = p[TP_E1 + k] * (h - 20) / 127;
        cv_round(x, y0 + 4, 12, h - 8, 3, K_PANEL2);
        cv_round(x, y0 + h - 4 - v - 8, 12, v + 8, 3, k ? K_DIM : K_TEAL);
    }
}
static void pic_hardsync(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    int32_t cy = y0 + h / 2, period = 10 + (127 - p[TP_E1]) * 50 / 127, py = cy + h / 2 - 6;
    (void)f;
    for (int32_t x = 0; x < w; x++) {               /* a saw reset every period: the notch */
        int32_t y = cy + h / 2 - 6 - (x % period) * (h - 12) / period;
        if (x % period == 0)
            cv_line(x0 + x, py, x0 + x, y, K_ACCENT);
        else
            cv_line(x0 + x, py, x0 + x, y, K_TEAL);
        py = y;
    }
}
static void pic_dissolve(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    uint32_t r = 0x9E3779B9u ^ (f / 3);
    for (int k = 0; k < 40 + p[TP_E1] / 4; k++) {   /* a cloud of dots, denser with swarm */
        r = r * 1664525u + 1013904223u;
        {
            int32_t x = (int32_t)((r >> 8) % (uint32_t)w), y = (int32_t)((r >> 20) % (uint32_t)h);
            int32_t dx = x - w / 2, dy = (y - h / 2) * 2;
            if (dx * dx + dy * dy < (w / 2) * (w / 2))
                cv_rect(x0 + x, y0 + y, 2, 2, (r >> 4 & 3u) ? K_DIM : K_TEAL);
        }
    }
}
static void pic_drumline(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    int32_t pw = w / 6, ph = h / 2;
    (void)p; (void)f;
    for (int k = 0; k < DL_NSOUND; k++) {
        int32_t x = x0 + (k % 6) * pw + 2, y = y0 + (k / 6) * ph + 2, hit = eng_drum_hit[k];
        cv_round(x, y, pw - 4, ph - 4, 4, hit > 40 ? K_TEAL : k == ui.dsound ? K_PANEL2 : K_PANEL);
        if (k == ui.dsound)
            cv_frame(x, y, pw - 4, ph - 4, 4, K_ACCENT, hit > 40 ? K_TEAL : K_PANEL2);
        text_c(x + (pw - 4) / 2, y + (ph - 4) / 2 - 7, &FONT_XS, DL_SOUND_NAME[k], hit > 40 ? K_BG : K_TEXT);
    }
}
static void draw_engine(void)
{
    int trk = sel_trk(), eg = proj.trk[trk].par[TP_ENGINE] % NENGINE;
    const uint8_t *p = proj.trk[trk].par;
    uint32_t h = hash_u(2166136261u, 0xE0000u | (uint32_t)trk | (uint32_t)eg << 4 | (uint32_t)ui.dsound << 8 |
                                          (ui.frame / 2) << 12);
    char t[40];
    for (int i = 0; i < 4; i++)
        h = hash_u(h, p[TP_E1 + i]);
    h = hash_s(h, proj.trk[trk].name);
    if (!begin_main(h))
        return;
    cv_text(10, 2, &FONT_B, EG_NAME[eg], K_TEXT);
    {
        char nm[NAME_LEN + 1];
        for (int i = 0; i < NAME_LEN; i++)
            nm[i] = proj.trk[trk].name[i];
        nm[NAME_LEN] = 0;
        if (nm[0])
            text_r(230, 3, &FONT_S, nm, K_DIM);
    }
    (void)t;
    switch (eg) {
    case EG_SIMPLE: pic_simple(20, 24, 200, 88, p, ui.frame); break;
    case EG_PRISM: pic_prism(20, 24, 200, 88, p, ui.frame); break;
    case EG_WAVETABLE: pic_wave(20, 24, 200, 88, p, ui.frame); break;
    case EG_AXIS: pic_axis(20, 24, 200, 88, p, ui.frame); break;
    case EG_EPIANO: pic_epiano(20, 24, 200, 88, p, ui.frame); break;
    case EG_ORGAN: pic_organ(20, 24, 200, 88, p, ui.frame); break;
    case EG_HARDSYNC: pic_hardsync(20, 24, 200, 88, p, ui.frame); break;
    case EG_DISSOLVE: pic_dissolve(20, 24, 200, 88, p, ui.frame); break;
    default: pic_drumline(10, 26, 220, 90, p, ui.frame); break;
    }
    cv_blit(0, MAIN_Y);
}

/* the aux tracks' pages */
static void draw_brain(void)
{
    const uint8_t *p = proj.trk[AUX_BRAIN].par;
    int key = seq.brain_key % 12, sc = seq.brain_scale % NBS, deg = (int8_t)seq.brain_deg;
    uint32_t h = hash_u(2166136261u, 0xB0000u | (uint32_t)key | (uint32_t)sc << 4 | (uint32_t)(deg + 64) << 8 |
                                          (uint32_t)seq.brain_auto_ok << 16 | (uint32_t)p[TP_E1] << 17);
    char t[40], *e;
    for (int i = 0; i < NTRK; i++)
        h = hash_u(h, proj.trk[i].par[TP_BRAIN]);
    if (!begin_main(h))
        return;
    e = s_cat(s_cat(t, NOTE_NAME[key]), " ");
    s_cat(e, BS_NAME[sc]);
    cv_text(12, 4, &FONT_L, t, K_TEXT);
    if (p[TP_E1])
        text_r(230, 6, &FONT_XS, seq.brain_auto_ok ? "auto: sure" : "auto: guessing", K_DIM);
    e = s_cat(t, "degree ");
    if (deg > 0)
        e = s_cat(e, "+");
    num_i(deg, e);
    cv_text(12, 44, &FONT_B, t, deg ? K_TEAL : K_DIM);
    cv_text(12, 70, &FONT_XS, "tracks that follow", K_DIM);
    for (int i = 0; i < NTRK; i++) {
        int32_t x = 12 + i * 27;
        int m = proj.trk[i].par[TP_BRAIN] % 3;
        num_s((uint32_t)i + 1u, t);
        if (m == 1)
            cv_round(x, 86, 22, 22, 5, K_TEXT);
        else if (m == 2)
            cv_frame(x, 86, 22, 22, 5, K_TEXT, K_BG);
        else
            cv_frame(x, 86, 22, 22, 5, K_LINE, K_BG);
        text_c(x + 11, 90, &FONT_B, t, m == 1 ? K_BG : m == 2 ? K_TEXT : K_FAINT);
    }
    cv_blit(0, MAIN_Y);
}
static void draw_punch(void)
{
    uint32_t dm = eng_punch_drum, mm = eng_punch_mel;
    uint32_t h = hash_u(2166136261u, 0xD0000u ^ dm ^ (mm << 5) ^ (ui.keys << 3) ^ (ui.punch_latched << 7));
    char t[64], *e;
    int n = 0;
    if (!begin_main(h))
        return;
    e = t;
    *t = 0;
    for (int fx = 0; fx < NPUNCH_FX; fx++) {        /* the FX in effect, named */
        int on = fx < 16 ? (dm >> fx & 1u) : (mm >> (fx - 16) & 1u);
        if (!on || !PUNCH_NAME[fx][0])
            continue;
        if (n++)
            e = s_cat(e, ", ");
        if (s_len(t) < 40)
            e = s_cat(e, PUNCH_NAME[fx]);
    }
    if (!n)
        s_cat(t, "hold a key for an effect");
    text_fit(10, 4, n ? &FONT_B : &FONT_S, t, 220, n ? K_TEAL : K_DIM);
    cv_text(10, 24, &FONT_XS, "drums", K_DIM);
    text_r(230, 24, &FONT_XS, "melodic", K_DIM);
    for (int k = 0; k < 16; k++) {                   /* the keys: a small keyboard, the held ones teal */
        int32_t x = 8 + k * 14;
        int key = WHITE_K[k], fx = key < NPUNCH_D ? key : 16 + key - NPUNCH_D;
        int on = fx < 16 ? (dm >> fx & 1u) : (mm >> (fx - 16) & 1u);
        cv_round(x, 40, 13, 60, 2, on ? K_TEAL : K_BG);
        cv_frame(x, 40, 13, 60, 2, K_DIM, on ? K_TEAL : K_BG);
    }
    for (int k = 0; k < 11; k++) {
        int key = BLACK_K[k], fx = key < NPUNCH_D ? key : 16 + key - NPUNCH_D;
        int on = fx < 16 ? (dm >> fx & 1u) : (mm >> (fx - 16) & 1u);
        int32_t wi = 0;
        while (WHITE_K[wi] < key)
            wi++;
        cv_round(8 + wi * 14 - 5, 40, 10, 34, 2, on ? K_TEAL : K_TEXT);
    }
    cv_rect(8 + 7 * 14 - 1, 36, 2, 70, K_ACCENT_L);   /* the split: F3..E4 drums, F4..G5 melodic */
    cv_text(10, 104, &FONT_XS, proj.trk[AUX_PUNCH].par[TP_E1] ? "latch on: a key toggles" : "momentary: while held", K_FAINT);
    cv_blit(0, MAIN_Y);
}
static void draw_tape(void)
{
    uint32_t h = hash_u(2166136261u, 0x7A000u ^ ui.keys ^ (uint32_t)playing() << 30 ^ (ui.frame / 4) << 8);
    int32_t cx = 70, cy = 58, r = 44;
    uint32_t held = 0;
    char t[24];
    for (int k = 0; k < 16; k++)
        if (ui.keys >> WHITE_K[k] & 1u && ui.kmode[WHITE_K[k]] == KM_TAPE)
            held |= 1u << k;
    h = hash_u(h, held);
    if (!begin_main(h))
        return;
    for (int k = 0; k < 16; k++) {                   /* the ring: 16 segments, the held ones lit, the head turning */
        uint32_t a = (uint32_t)k * 2u;
        int32_t x = cx + cos100(a) * r / 100, y = cy + sin100(a) * r / 100;
        int on = held >> k & 1u;
        cv_round(x - 5, y - 5, 11, 11, 4, on ? K_TEAL : K_PANEL2);
        if (on)
            cv_frame(x - 6, y - 6, 13, 13, 5, K_TEAL, K_TEAL);
    }
    if (playing()) {
        uint32_t a = (ui.frame / 4) & 31u;
        cv_round(cx + cos100(a) * (r - 14) / 100 - 2, cy + sin100(a) * (r - 14) / 100 - 2, 5, 5, 2, K_ACCENT);
    }
    cv_text(130, 10, &FONT_B, "tape", K_TEXT);
    cv_text(130, 30, &FONT_XS, "the last 16 steps,", K_DIM);
    cv_text(130, 44, &FONT_XS, "a white key loops one", K_DIM);
    if (held) {
        char *e = s_cat(t, "segment ");
        for (int k = 0; k < 16; k++)
            if (held >> k & 1u) {
                e = num_s((uint32_t)k + 1u, e);
                e = s_cat(e, " ");
            }
        cv_text(130, 66, &FONT_S, t, K_TEAL);
    }
    cv_blit(0, MAIN_Y);
}
static void draw_fxtrack(void)
{
    int trk = sel_trk(), type = proj.trk[trk].par[TP_E1] % NFXT;
    uint32_t h = hash_u(2166136261u, 0xF0000u | (uint32_t)trk | (uint32_t)type << 4 | (ui.frame / 3) << 8);
    char t[24];
    if (!begin_main(h))
        return;
    cv_text(12, 4, &FONT_L, FXT_NAME[type], K_TEXT);
    s_cat(t, trk == AUX_FX1 ? "send effect I" : "send effect II");
    cv_text(12, 40, &FONT_S, t, K_DIM);
    {                                                /* a glyph per effect */
        int32_t x0 = 130, y0 = 60, w = 100;
        uint32_t f = ui.frame;
        switch (type) {
        case FXT_CHORUS:
            for (int32_t x = 0; x < w; x += 2) {
                cv_rect(x0 + x, y0 + 20 + sin100((uint32_t)(x / 6 + f / 3)) * 10 / 100, 2, 2, K_TEAL);
                cv_rect(x0 + x, y0 + 20 + sin100((uint32_t)(x / 6 + f / 3 + 5)) * 10 / 100, 2, 2, K_DIM);
            }
            break;
        case FXT_DELAY:
            for (int k = 0; k < 5; k++)
                cv_round(x0 + k * 20, y0 + 30 - (30 >> k), 10, 30 >> k, 2, k ? K_DIM : K_TEAL);
            break;
        case FXT_DIST:
            for (int32_t x = 0; x < w; x += 2) {
                int32_t s = sin100((uint32_t)(x / 3)) * 3;
                s = s > 100 ? 100 : s < -100 ? -100 : s;
                cv_rect(x0 + x, y0 + 20 - s * 16 / 100, 2, 2, K_TEAL);
            }
            break;
        case FXT_LOFI:
            for (int32_t x = 0; x < w; x += 8)
                cv_rect(x0 + x, y0 + 20 - sin100((uint32_t)(x / 8)) * 16 / 100, 8, 3, K_TEAL);
            break;
        case FXT_PHASER:
            for (int k = 0; k < 4; k++) {
                int32_t x = x0 + 10 + k * 24 + sin100(f / 3 + (uint32_t)k * 8) * 6 / 100;
                cv_round(x, y0, 6, 40, 3, k & 1 ? K_DIM : K_TEAL);
            }
            break;
        default:
            for (int k = 0; k < 6; k++) {
                int32_t rr = 8 + k * 7;
                for (uint32_t a = 0; a < 32; a += 2)
                    cv_rect(x0 + 50 + cos100(a) * rr / 100, y0 + 20 + sin100(a) * rr / 200, 1, 1, k & 1 ? K_DIM : K_TEAL);
            }
            break;
        }
    }
    cv_blit(0, MAIN_Y);
}

/* Play: voice mode, portamento, bend, volume: words */
static void draw_play(void)
{
    int trk = sel_trk();
    const uint8_t *p = proj.trk[trk].par;
    uint32_t h = hash_u(2166136261u, 0x9A000u | (uint32_t)trk | (uint32_t)p[TP_VMODE] << 4 | (uint32_t)p[TP_PORTA] << 8 |
                                          (uint32_t)p[TP_BEND] << 16 | (uint32_t)eng_voices << 24);
    char t[40];
    if (!begin_main(h))
        return;
    cv_text(12, 6, &FONT_B, VM_NAME[p[TP_VMODE] % 3], K_TEXT);
    cv_text(12, 26, &FONT_XS, p[TP_VMODE] % 3 == VM_POLY ? "up to 6 voices on the track"
                              : p[TP_VMODE] % 3 == VM_MONO ? "one voice, every key restarts it"
                                                            : "one voice, held keys glide", K_DIM);
    for (int v = 0; v < NVOICE; v++)                 /* the voices sounding, all tracks */
        cv_round(12 + v * 13, 50, 10, 10, 3, v < eng_voices ? K_TEAL : K_LINE);
    num_s((uint32_t)eng_voices, s_cat(t, "voices sounding "));
    cv_text(12, 66, &FONT_XS, t, K_DIM);
    cv_blit(0, MAIN_Y);
}

/* Envelope: the curve */
static void draw_env(int filter)
{
    int trk = sel_trk(), b = filter ? TP_FA : TP_A;
    const uint8_t *p = proj.trk[trk].par;
    uint32_t h = hash_u(2166136261u, 0xE7000u | (uint32_t)trk | (uint32_t)filter << 4 | (uint32_t)p[b] << 8 |
                                          (uint32_t)p[b + 1] << 16 | (uint32_t)p[b + 2] << 24);
    int32_t a, d, s, r, x0 = 16, y0 = 16, w = 208, hh = 84, x;
    h = hash_u(h, p[b + 3]);
    if (!begin_main(h))
        return;
    a = 4 + p[b] * 60 / 127;
    d = 4 + p[b + 1] * 60 / 127;
    r = 4 + p[b + 3] * 60 / 127;
    s = p[b + 2] * hh / 127;
    x = x0;
    cv_rect(x0, y0 + hh, w, 1, K_LINE);
    cv_line(x, y0 + hh, x + a, y0, K_TEAL);
    x += a;
    cv_line(x, y0, x + d, y0 + hh - s, K_TEAL);
    x += d;
    cv_rect(x, y0 + hh - s, w - a - d - r, 2, K_TEAL);
    x += w - a - d - r;
    cv_line(x, y0 + hh - s, x + r, y0 + hh, K_TEAL);
    cv_text(x0, y0 + hh + 6, &FONT_XS, filter ? "filter envelope" : "amp envelope", K_DIM);
    cv_blit(0, MAIN_Y);
}

/* Filter: the response curve */
static void draw_filter(void)
{
    int trk = sel_trk();
    const uint8_t *p = proj.trk[trk].par;
    int ft = p[TP_FTYPE] % NFT, cut = p[TP_CUT], res = p[TP_RES];
    uint32_t h = hash_u(2166136261u, 0xF1000u | (uint32_t)trk | (uint32_t)ft << 4 | (uint32_t)cut << 8 | (uint32_t)res << 16);
    int32_t x0 = 16, y0 = 12, w = 208, hh = 84, fc = x0 + 10 + cut * (w - 20) / 127, py = -1;
    if (!begin_main(h))
        return;
    cv_rect(x0, y0 + hh, w, 1, K_LINE);
    for (int32_t x = x0; x < x0 + w; x++) {
        int32_t dist = x - fc, y, g;
        g = dist < 0 ? 0 : dist * (ft == FT_LP24 ? 2 : 1);   /* the roll-off past the cutoff */
        if (ft == FT_HP)
            g = dist > 0 ? 0 : -dist;
        if (ft == FT_BP)
            g = dist < 0 ? -dist : dist;
        g = g > hh - 8 ? hh - 8 : g;
        {
            int32_t bump = dist > -8 && dist < 8 ? (8 - (dist < 0 ? -dist : dist)) * res / 30 : 0;
            y = y0 + 8 + g - bump;
            if (y < y0)
                y = y0;
        }
        if (py >= 0)
            cv_line(x - 1, py, x, y, K_TEAL);
        py = y;
    }
    cv_rect(fc, y0, 1, hh, K_ACCENT_L);
    cv_text(x0, y0 + hh + 6, &FONT_XS, FT_NAME[ft], K_DIM);
    text_r(x0 + w, y0 + hh + 6, &FONT_XS, p[TP_KTRK] ? "key tracking on" : "", K_FAINT);
    cv_blit(0, MAIN_Y);
}
static void draw_sends(void)
{
    int trk = sel_trk();
    const uint8_t *p = proj.trk[trk].par;
    uint32_t h = hash_u(2166136261u, 0x5E000u | (uint32_t)trk | (uint32_t)p[TP_SEND1] << 4 | (uint32_t)p[TP_SEND2] << 12 |
                                          (uint32_t)p[TP_SENDT] << 20 | (uint32_t)p[TP_FTYPE] << 28);
    static const char *const NM[3] = {"FX I", "FX II", "tape"};
    if (!begin_main(h))
        return;
    cv_text(12, 4, &FONT_B, FT_NAME[p[TP_FTYPE] % NFT], K_TEXT);
    cv_text(12, 24, &FONT_XS, "the track's sends", K_DIM);
    for (int i = 0; i < 3; i++) {
        int32_t y = 44 + i * 22, v = p[TP_SEND1 + i] * 160 / 127;
        cv_text(12, y - 2, &FONT_S, NM[i], K_TEXT);
        cv_round(64, y + 3, 160, 8, 4, K_LINE);
        if (v > 0)
            cv_round(64, y + 3, v < 8 ? 8 : v, 8, 4, K_TEAL);
    }
    cv_blit(0, MAIN_Y);
}

/* LFO: its shape scrolling */
static void draw_lfo(void)
{
    int trk = sel_trk();
    const uint8_t *p = proj.trk[trk].par;
    int lt = p[TP_LTYPE] % NLT;
    uint32_t h = hash_u(2166136261u, 0x1F000u | (uint32_t)trk | (uint32_t)lt << 4 | (uint32_t)p[TP_LAMT] << 8 |
                                          (lt ? ui.frame / 2 : 0u) << 16);
    int32_t x0 = 16, y0 = 12, w = 208, hh = 84, cy = y0 + hh / 2, amp = 6 + p[TP_LAMT] * (hh / 2 - 8) / 127;
    uint32_t r = 12345u;
    if (!begin_main(h))
        return;
    cv_rect(x0, cy, w, 1, K_LINE);
    if (lt == LT_OFF)
        text_c(120, 40, &FONT_S, "LFO off", K_DIM);
    for (int32_t x = 0; lt && x < w; x += 2) {
        int32_t y = cy;
        uint32_t ph = (uint32_t)(x / 4) + ui.frame / 2;
        if (lt == LT_VALUE || lt == LT_TREMOLO)
            y = cy - sin100(ph) * amp / 100;
        else if (lt == LT_RANDOM) {
            if (x % 24 == 0)
                r = r * 1664525u + 1013904223u + (ph / 6);
            y = cy - (int32_t)((r >> 16) % (uint32_t)(2 * amp + 1)) + amp;
        } else
            y = cy + amp - (x % 48 < 6 ? 2 * amp : 2 * amp * (48 - x % 48) / 48);
        cv_rect(x0 + x, y, 2, 2, K_TEAL);
    }
    {
        char t[40], *e = s_cat(t, "to ");
        s_cat(e, lt ? tp_info(p[TP_LDEST] % NTP, p[TP_ENGINE])->name : "-");
        cv_text(x0, y0 + hh + 6, &FONT_XS, t, K_DIM);
    }
    cv_blit(0, MAIN_Y);
}

/* Players: the notes held and what the player makes of them */
static void draw_players(void)
{
    int trk = sel_trk(), pl = proj.trk[trk].par[TP_PLAYER] % NPL;
    const player_t *pr = &seq.player[trk < NTRK ? trk : 0];
    uint32_t h = hash_u(2166136261u, 0x9C000u | (uint32_t)trk | (uint32_t)pl << 4 | (uint32_t)pr->nheld << 8 |
                                          (uint32_t)pr->nchord << 12 | (uint32_t)pr->arp_note << 16 | (uint32_t)ui.maestro_rec << 24);
    char t[48], *e;
    for (int i = 0; i < 16; i++)
        h = hash_u(h, pr->held[i]);
    for (int i = 0; i < SN * 2; i++)
        h = hash_u(h, pr->chord[i]);
    if (!begin_main(h))
        return;
    cv_text(12, 4, &FONT_B, PL_NAME[pl], K_TEXT);
    if (pl == PL_OFF)
        cv_text(12, 24, &FONT_XS, "the keys play as they are", K_DIM);
    else if (pl == PL_ARP)
        cv_text(12, 24, &FONT_XS, "held notes play one after another", K_DIM);
    else if (pl == PL_MAESTRO)
        cv_text(12, 24, &FONT_XS, ui.maestro_rec ? "hold the chord to record" : "one key plays the chord, moved", K_DIM);
    else
        cv_text(12, 24, &FONT_XS, "notes stay until the next", K_DIM);
    e = s_cat(t, pl == PL_MAESTRO ? "chord: " : "held: ");
    if (pl == PL_MAESTRO) {
        for (int i = 0; i < pr->nchord && i < SN * 2; i++) {
            if (i)
                e = s_cat(e, " ");
            e = note_name(pr->chord[i], e);
        }
        if (!pr->nchord)
            e = s_cat(e, "none yet, REC records one");
    } else {
        for (int i = 0; i < pr->nheld && i < 8; i++) {
            if (i)
                e = s_cat(e, " ");
            e = note_name(pr->held[i], e);
        }
        if (!pr->nheld)
            e = s_cat(e, "-");
    }
    text_fit(12, 48, &FONT_S, t, 216, K_TEXT);
    for (int k = 0; k < 24; k++) {                   /* two octaves of keys: the held ones teal, the arp's note coral */
        int32_t x = 12 + k * 9;
        int n = 48 + k, held = 0;
        for (int i = 0; i < pr->nheld && i < 16; i++)
            held |= pr->held[i] == n;
        cv_round(x, 76, 8, 30, 2, pr->arp_note == n ? K_ACCENT : held ? K_TEAL : (k % 12 == 1 || k % 12 == 3 ||
                                                           k % 12 == 6 || k % 12 == 8 || k % 12 == 10) ? K_DIM : K_PANEL2);
    }
    cv_blit(0, MAIN_Y);
}

/* Mix: thirteen meter strips, the selected one ringed, muted ones dim */
static void draw_mix(void)
{
    int sel = sel_trk();
    uint32_t h = hash_u(2166136261u, 0x3C000u | (uint32_t)sel | (uint32_t)seq.mute << 4);
    static const char *const SHORT_NM[NALLTRK] = {"1", "2", "3", "4", "5", "6", "7", "8", "BR", "PU", "TP", "F1", "F2"};
    for (int t = 0; t < NALLTRK; t++)
        h = hash_u(h, eng_peak[t] >> 9);
    if (!begin_main(h))
        return;
    for (int t = 0; t < NALLTRK; t++) {
        int32_t x = 4 + t * 18, lvl = (int32_t)eng_peak[t] * 80 / 32768;
        int muted = t < NSEQT && (seq.mute >> t & 1u);
        cv_round(x, 6, 14, 84, 3, K_PANEL);
        if (lvl > 0)
            cv_round(x, 90 - lvl, 14, lvl, 3, muted ? K_LINE : K_TEAL);
        if (t == sel) {                              /* the selected strip: a coral ring around it */
            cv_frame(x - 1, 5, 16, 86, 4, K_ACCENT, K_PANEL);
            if (lvl > 0)
                cv_round(x + 1, 90 - lvl, 12, lvl - 1, 2, muted ? K_LINE : K_TEAL);
        }
        text_c(x + 7, 94, &FONT_XS, SHORT_NM[t], t == sel ? K_ACCENT : muted ? K_FAINT : K_TEXT);
        if (muted)
            cv_rect(x + 2, 108, 10, 2, K_FAINT);
    }
    cv_blit(0, MAIN_Y);
}

/* Master: the EQ's curve, the saturator's curve, the groups' levels */
static void draw_master(void)
{
    int pg = ui.page - P_MASTER;
    const uint8_t *m = proj.master;
    uint32_t h = hash_u(2166136261u, 0x3A000u | (uint32_t)pg);
    int32_t x0 = 16, y0 = 12, w = 208, hh = 84, cy = y0 + hh / 2;
    for (int i = 0; i < NMS; i++)
        h = hash_u(h, m[i]);
    if (!begin_main(h))
        return;
    cv_rect(x0, cy, w, 1, K_LINE);
    if (pg == 0) {                                   /* lo, mid, hi: three humps, scaled by blend */
        for (int32_t x = 0; x < w; x += 2) {
            int32_t b = x < w / 3 ? m[MS_EQ_LO] : x < 2 * w / 3 ? m[MS_EQ_MID] : m[MS_EQ_HI];
            int32_t y = cy - (b - 64) * (hh / 2 - 6) * (int32_t)m[MS_EQ_BLEND] / (64 * 127);
            cv_rect(x0 + x, y, 2, 2, K_TEAL);
        }
        cv_text(x0, y0 + hh + 6, &FONT_XS, "low  mid  high, by the blend", K_DIM);
    } else if (pg == 1) {                            /* the saturator: a bent line */
        int32_t py = -1;
        for (int32_t x = 0; x < w; x++) {
            int32_t in = (x - w / 2) * 100 / (w / 2), g = 100 + (int32_t)m[MS_SAT_GAIN] * 3, clip = 30 + (127 - m[MS_SAT_CLIP]) * 70 / 127;
            int32_t out = in * g / 100;
            out = out > clip ? clip : out < -clip ? -clip : out;
            {
                int32_t y = cy - out * (hh / 2 - 4) / 100;
                if (py >= 0)
                    cv_line(x0 + x - 1, py, x0 + x, y, K_TEAL);
                py = y;
            }
        }
        cv_text(x0, y0 + hh + 6, &FONT_XS, "drive: gain, clip, tone, blend", K_DIM);
    } else {
        static const char *const NM[4] = {"drums", "melodic", "comp", "level"};
        for (int i = 0; i < 4; i++) {
            int32_t x = x0 + i * 52, v = m[MS_PERC + i] * (hh - 10) / 127;
            cv_round(x + 8, y0, 30, hh, 4, K_PANEL);
            cv_round(x + 8, y0 + hh - v, 30, v, 4, i == 2 ? K_ACCENT_L : K_TEAL);
            text_c(x + 23, y0 + hh + 6, &FONT_XS, NM[i], K_DIM);
        }
    }
    cv_blit(0, MAIN_Y);
}

static void setup_row(int32_t y, const char *name, const char *val)
{
    cv_text(12, y, &FONT_S, name, K_DIM);
    text_r(228, y, &FONT_S, val, K_TEXT);
}
static void draw_setup(void)
{
    char t[24], nm[NAME_LEN + 1];
    uint32_t h = hash_u(2166136261u, 0x5E700u | (uint32_t)proj_slot | (uint32_t)proj.usb_full << 4 | (uint32_t)seq.ext << 8);
    h = hash_s(h, proj.name);
    if (!begin_main(h))
        return;
    for (int i = 0; i < NAME_LEN; i++)
        nm[i] = proj.name[i];
    nm[NAME_LEN] = 0;
    num_s((uint32_t)proj_slot + 1u, s_cat(t, "slot "));
    setup_row(4, "project", t);
    setup_row(22, "name", nm[0] ? nm : "-");
    setup_row(40, "clock", seq.ext ? "MIDI in" : "internal");
    setup_row(58, "MIDI out", proj.send_clock || proj.send_notes ? (proj.send_clock && proj.send_notes ? "clock, notes"
                                                                  : proj.send_clock ? "clock" : "notes") : "off");
    setup_row(76, "USB audio", USBA_NAME[proj.usb_full & 1u]);
    setup_row(94, "version", XY_VERSION);
    cv_blit(0, MAIN_Y);
}
static void draw_status(void)
{
    char t[24], *e;
    uint32_t cpu = plat_cpu_pct(), peak = plat_cpu_peak_pct(), xr = plat_xruns();
    uint32_t h = hash_u(2166136261u, 0x57A00u | cpu | peak << 8 | (uint32_t)eng_voices << 16 | (uint32_t)eng_voices_max << 24);
    h = hash_u(h, xr ^ eng_guard_count << 8 ^ (uint32_t)engine_core2_state() << 28);
    if (!begin_main(h))
        return;
    e = num_s(cpu, t);
    s_cat(e, "%");
    setup_row(4, "audio load", t);
    e = num_s(peak, t);
    s_cat(e, "%");
    setup_row(22, "peak", t);
    e = num_s((uint32_t)eng_voices, t);
    e = s_cat(e, " of ");
    num_s((uint32_t)eng_voices_max, e);
    setup_row(40, "voices", t);
    setup_row(58, "second core", CORE2_NAME[engine_core2_state() & 3]);
    num_s(eng_guard_count, t);
    setup_row(76, "guard trips", t);
    num_s(xr, t);
    setup_row(94, "dropouts", t);
    cv_blit(0, MAIN_Y);
}

static void draw_page(void)
{
    int trk = sel_trk();
    if (ui.card) {
        draw_card();
        return;
    }
    if (lst.open) {
        draw_list();
        return;
    }
    switch (ui.page) {
    case P_ARRANGE: draw_arrange(); break;
    case P_SONG: draw_song(); break;
    case P_STEPS: draw_steps(); break;
    case P_BAR: draw_bar(); break;
    case P_ENGINE: case P_PLAY: case P_ENV: case P_ENV2: case P_FILTER: case P_SENDS: case P_LFO: case P_LFO2:
    case P_PLAYERS: case P_PLAYERS2:
        if (trk == AUX_BRAIN)
            draw_brain();
        else if (trk == AUX_PUNCH)
            draw_punch();
        else if (trk == AUX_TAPE)
            draw_tape();
        else if (is_aux(trk))
            draw_fxtrack();
        else if (ui.page == P_ENGINE)
            draw_engine();
        else if (ui.page == P_PLAY)
            draw_play();
        else if (ui.page == P_ENV || ui.page == P_ENV2)
            draw_env(ui.page == P_ENV2);
        else if (ui.page == P_FILTER)
            draw_filter();
        else if (ui.page == P_SENDS)
            draw_sends();
        else if (ui.page == P_LFO || ui.page == P_LFO2)
            draw_lfo();
        else
            draw_players();
        break;
    case P_MIX: draw_mix(); break;
    case P_MASTER: case P_MASTER2: case P_MASTER3: draw_master(); break;
    case P_SETUP: draw_setup(); break;
    default: draw_status(); break;
    }
}
