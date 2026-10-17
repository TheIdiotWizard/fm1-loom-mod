/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8's pages: the state the input layer (ui.c) and the screen share, and the drawing. Part of the unity
 * build, after project.c and before ui.c; the host simulator includes it too. Polly's framework: a header
 * (the page, the tempo, REC, unsaved; messages and questions in an alert box), the main band (the page's
 * picture), the tag (where you are: track > pattern > scene, the page dots), the four knobs' strip (with
 * the track's ink under the values), the encoders' line and the footer (what the keys do here).
 * Each band is drawn only when what it shows changed (a hash of it). Over the main band: a help card (a
 * button held alone), a list (SEL). The look: below. */

/* The look: a theme (THEMES[]), and in every theme each instrument track its own ink, so the steps, the arrange
 * rows and the mix say whose they are at a glance. On a track's pages its ink is K_NOW (solid: sounding now, the
 * playhead, the picture's line, the background's colour as text on it) and K_TINT (its ink thinned over the
 * background: the steps with notes). Red (K_REC) only for recording, locks and questions. */
typedef struct {
    const char *name;
    uint16_t bg, panel, panel2, line, faint, dim, text, now_m, rec, aux;
    uint16_t ink[NTRK];
    uint8_t tint, mid;                                /* the ink's share over the background, of 16 */
} theme_t;
/* the themes (Setup: THEME; the project keeps it in rsv[1]); the first is the default */
static const theme_t THEMES[] = {
    {"tape deck", RGB(24, 18, 14), RGB(44, 34, 26), RGB(70, 56, 42), RGB(74, 60, 46), RGB(120, 100, 80),
     RGB(190, 170, 140), RGB(244, 230, 200), RGB(150, 130, 104), RGB(230, 70, 40), RGB(240, 220, 180),
     {RGB(230, 180, 50), RGB(214, 96, 48), RGB(160, 170, 70), RGB(70, 170, 160), RGB(110, 150, 200), RGB(180, 110, 150),
      RGB(210, 170, 120), RGB(150, 190, 150)}, 6, 9},
    {"blueprint", RGB(16, 48, 104), RGB(24, 62, 128), RGB(40, 84, 156), RGB(60, 100, 170), RGB(110, 140, 200),
     RGB(170, 195, 235), RGB(240, 246, 255), RGB(130, 160, 215), RGB(255, 110, 90), RGB(255, 255, 255),
     {RGB(255, 150, 190), RGB(120, 240, 230), RGB(255, 200, 80), RGB(180, 240, 120), RGB(255, 170, 110),
      RGB(210, 180, 255), RGB(240, 120, 120), RGB(200, 230, 200)}, 7, 11},
};
#define NTHEMES (sizeof THEMES / sizeof THEMES[0])
static const theme_t *theme = &THEMES[0];
#define THEME_OF_PROJ() (&THEMES[proj.rsv[1] % NTHEMES])
#define K_BG theme->bg
#define K_PANEL theme->panel
#define K_PANEL2 theme->panel2
#define K_LINE theme->line
#define K_DIM theme->dim
#define K_FAINT theme->faint
#define K_TEXT theme->text
#define K_NOW_M theme->now_m
#define K_REC theme->rec
#define K_SEL K_TEXT                                  /* the cursor, a held step, the knob last turned */
#define K_NOW k_now
#define K_TINT k_tint
#define K_NOW_T k_mid                                 /* the playhead on an empty step: the ink half thinned */
static uint16_t k_now, k_tint, k_mid;
static const theme_t *k_th;                           /* the theme k_now .. were made in */
static uint16_t trk_ink(int t) { return t >= 0 && t < NTRK ? theme->ink[t] : theme->aux; }
static uint16_t ink_mix(uint16_t ink, uint32_t a)    /* the ink at a/16 over the background */
{
    uint32_t pr = K_BG >> 11, pg = K_BG >> 5 & 63u, pb = K_BG & 31u;
    uint32_t r = ink >> 11, g = ink >> 5 & 63u, b = ink & 31u;
    return (uint16_t)(((r * a + pr * (16u - a)) / 16u) << 11 | ((g * a + pg * (16u - a)) / 16u) << 5 |
                      (b * a + pb * (16u - a)) / 16u);
}
static uint16_t trk_tint(int t) { return ink_mix(trk_ink(t), theme->tint); }

/* the pages (the spec's Buttons table): a button opens its first page; SELECT turns them. EDIT: the engine
 * (a drum track: the shown key's knobs) and Play (a drum track: the key's model, random, pan, volume); ENV: the
 * envelope, the filter, the filter envelope; FX: the selected track's sends and the two effects; ARP three
 * with the arp (its page 2, then hold) */
enum { P_ARRANGE, P_SONG, P_STEPS, P_BAR, P_ENGINE, P_PLAY, P_ENV, P_FILTER, P_ENV2, P_SENDS, P_FX1, P_FX2, P_LFO,
       P_LFO2, P_PLAYERS, P_PLAYERS2, P_PLAYERS3, P_MIX, P_MASTER, P_MASTER2, P_MASTER3, P_SETUP, P_STATUS, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"Arrange", "Song", "Steps", "Bar", "Engine", "Play", "Envelope",
                                              "Filter", "Filter env", "Sends", "FX I", "FX II", "LFO", "LFO 2",
                                              "Players", "Players 2", "Hold", "Mix", "Master EQ", "Master drive",
                                              "Master out", "Setup", "Status"};
static const struct { uint8_t btn, first, n; } PAGE_BTN[] = {
    {B_HOME, P_ARRANGE, 2}, {B_SEQ, P_STEPS, 2}, {B_EDIT, P_ENGINE, 2}, {B_ENV, P_ENV, 3}, {B_FX, P_SENDS, 3},
    {B_LFO, P_LFO, 2}, {B_ARP, P_PLAYERS, 3}, {B_GLO, P_MIX, 6}};
#define NPAGE_BTN (sizeof PAGE_BTN / sizeof PAGE_BTN[0])
static int page_group(int p)
{
    for (uint32_t i = 0; i < NPAGE_BTN; i++)
        if (p >= PAGE_BTN[i].first && p < PAGE_BTN[i].first + PAGE_BTN[i].n)
            return (int)i;
    return 0;
}
static int group_n(int g);                           /* the pages a button has now (the track decides) */
static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                          "PLAY", "REC", "OCT-", "OCT+"};

/* the tracks: 1-8, then the aux tracks (the tag's short names, the pages' long ones) */
static const char *const TRACK_NAME[NALLTRK] = {"T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8", "COND", "THROW",
                                                "TAPE", "FX I", "FX II"};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
/* the step components (SC_*: the OP-XY's 14, on the white keys with HOME and a step held), their letter on
 * the grid and their name */
static const char *const COMP_NAME[NSC] = {"none", "pulse", "hold", "multiply", "velocity", "ramp up", "ramp down",
                                           "random", "portamento", "bend", "tonality", "jump", "skip lock",
                                           "skip trick", "skip trig"};
static const char *const COMP_GLYPH[NSC] = {"", "P", "H", "M", "V", "^", "v", "?", "~", "B", "T", "J", "L", "C", "S"};
static const char *const VEL_VAL[10] = {"4", "8", "16", "32", "64", "100", "112", "127", "0", "random"};
static const char *const RAMP_VAL[10] = {"2 in 1", "3 in 1", "4 in 1", "5 in 1", "6 in 1", "2 in 3", "3 in 3", "4 in 3",
                                         "5 in 3", "random"};   /* steps over one / three octaves */
static const char *const PORTA_VAL[10] = {"10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "off", "random"};
static const char *const BEND_VAL[10] = {"down-up", "up-down", "bump dn", "bump up", "spring out", "spring in",
                                         "fade dn", "fade up", "random 1", "random 2"};
static const char *const TON_VAL[10] = {"ignore", "transpose", "oct up", "5th up", "3rd up", "chrom up", "chrom dn",
                                        "q 33%", "q 66%", "q 100%"};
static const char *const JUMP_VAL[10] = {"to 1", "to 5", "to 9", "to 13", "forward", "back", "fwd/back", "stay",
                                         "align", "random"};
static const char *const TS_NAME[NTS] = {"1/2", "1", "2", "3", "4", "5", "6", "8", "16"};
static const uint8_t BAR_SCALE_KEY[9] = {TS_1, TS_2, TS_3, TS_4, TS_5, TS_6, TS_8, TS_16, TS_HALF};   /* Bar's black keys */
static const char *const LIGHTS_NAME[3] = {"off", "on", "keys"};
static const char *const METRO_NAME[3] = {"off", "rec", "on"};
static const char *const ONOFF[2] = {"off", "on"};
static const char *const BRAIN_MODE[2] = {"manual", "auto"};
static const char *const LINK_NAME[9] = {"off", "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8"};
static const char *const FOLLOW_NAME[3] = {"no", "diatonic", "transpose"};
static const char *const CORE2_NAME[4] = {"one core", "two cores", "lost", "barred"};
static const char *const USBA_NAME[2] = {"follows knob", "full"};
static const char *const BRAIN_KNOB[4] = {"mode", "key", "scale", "link"};
static const char *const TAPE_KNOB[4] = {"pitch", "speed", "length", "mix"};
static const char *const TAPE_PITCH[5] = {"-2", "-1", "x1", "+1", "+2"};

/* the keys: 0 = F3; the white and the black ones in order */
static const uint8_t WHITE_K[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_K[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
#define KEY_BASE 53                                  /* F3 at octave 0 */
enum { KM_NONE, KM_PLAY, KM_STEP, KM_CSTEP, KM_COMP, KM_SCENE, KM_MUTE, KM_PUNCH, KM_TAPE, KM_BRAIN, KM_SOUND,
       KM_TRACK, KM_SAVE, KM_LENGTH, KM_SOLO, KM_SCALE, KM_ROUTE, KM_STEPSOUND, KM_IDLE };   /* KM_IDLE: does nothing */   /* what a held key does */

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
    uint8_t ask_arg;
    uint8_t dirty;                                   /* the project changed since it was saved */
    uint8_t bar;                                     /* Steps: the bar shown 0..3 */
    uint8_t dsound;                                  /* a drum track: the key shown 0..10 */
    int8_t oct;                                      /* the keys' octave -4..4 */
    uint8_t rem[SN], nrem;                           /* the remembered notes (the last keys held together) */
    uint8_t rem_fx, rem_seg;                         /* PUNCH: the remembered effect + 1; TAPE: the segment + 1 */
    uint8_t chord[8], nchord;                        /* the keys held together now (notes) */
    uint8_t chord_max[8], nchord_max;                /* the most held together since the last all-up (maestro) */
    uint16_t held;                                   /* Steps: the white keys down as steps (bit = cell) */
    uint16_t cstep;                                  /* Steps: HOME + white keys down: the steps for a component */
    uint8_t held_first;                              /* the first held step (the one edited) */
    uint8_t held_used;                               /* a held step was edited: its release toggles nothing */
    uint8_t ccomp;                                   /* HOME + step: the component the black keys value */
    uint8_t lockpg;                                  /* a step held + EDIT / ENV / FX / LFO: the lock editor's page + 1 */
    uint8_t cursor;                                  /* step record: the step the keys write */
    uint8_t rec_armed;                               /* REC tapped while stopped: step record */
    uint8_t srec_down;                               /* step record: the keys of the chord being written still down */
    uint8_t rec_held, rec_latched;                   /* REC down while playing (momentary); REC + PLAY latched it */
    uint8_t maestro_rec;                             /* REC on Players with maestro: the next chord is recorded */
    uint8_t kmode[NKEYS], knote[NKEYS];              /* each held key: KM_*, its note / scene / fx */
    uint32_t key_t[NKEYS];
    uint32_t play_t;
    uint32_t punch_latched;
    uint8_t last_inst;                               /* the instrument track selected last (the FX pages audition it) */
    uint8_t solo;                                    /* a black key held on Mix: track + 1 soloed */
    uint16_t solo_mute;                              /* the mutes before the solo */
    uint8_t preset_i;
    uint8_t pick_engine, pick_preset, pick_slot, pick_fx, pick_weave;
    uint8_t name_slot;
    uint8_t playing;
    uint8_t scene_seen;
    uint32_t sig[6];
} ui;

/* SEL: the screen as a list: the page's knobs, then its actions */
enum { LK_KNOB, LK_MORE, LK_ACTION, LK_PICK };
enum { A_CLEAR_NOTES, A_CLEAR_LOCKS, A_CLEAR_BOTH, A_COPY_PAT, A_PASTE_PAT, A_ROT_FWD, A_ROT_BACK, A_DUP_BAR,
       A_ENGINE, A_PRESET, A_SCRAMBLE, A_SAVE_PRESET, A_INIT, A_FOLLOW, A_INSERT_SLOT, A_DELETE_SLOT, A_CLEAR_SONG,
       A_PLAY_SONG, A_LOAD_PROJ, A_NEW_PROJ, A_USB_AUDIO, A_CLEAR_SCENE, A_PANIC, A_EFFECT, A_REMOVE_NOTE,
       A_COUNT_IN, A_PLAYMODE, A_FTYPE, A_FX_HP, A_FX_LP, A_FX_SEND, A_WEAVE, A_THEME, NACTIONS };
static const char *const ACTION_NAME[NACTIONS] = {
    "CLEAR NOTES", "CLEAR LOCKS", "CLEAR NOTES AND LOCKS", "COPY PATTERN", "PASTE PATTERN", "ROTATE FORWARD",
    "ROTATE BACK", "DUPLICATE BAR", "ENGINE", "PRESET", "SCRAMBLE", "SAVE AS PRESET", "RESET SOUND", "FOLLOWS CONDUCTOR",
    "INSERT SCENE", "DELETE SLOT", "CLEAR SONG", "PLAY SONG", "LOAD PROJECT", "NEW PROJECT", "USB AUDIO", "CLEAR SCENE",
    "ALL NOTES OFF", "EFFECT", "REMOVE", "COUNT-IN", "PLAY MODE", "FILTER TYPE", "HIGH-PASS", "LOW-PASS",
    "TO FX II", "WEAVE WITH COPIED", "THEME"};
/* the weave's ways (pattern.h WV_*): what this pattern keeps of itself */
static const char *const WEAVE_NAME[NWV] = {"bars", "steps", "rhythm", "notes"};
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
static tpat_t clip XY_POOL;
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
static const int8_t SIN32[32] = {0, 20, 38, 56, 71, 83, 92, 98, 100, 98, 92, 83, 71, 56, 38, 20,
                                 0, -20, -38, -56, -71, -83, -92, -98, -100, -98, -92, -83, -71, -56, -38, -20};
static int32_t sin100(uint32_t k) { return SIN32[k & 31u]; }
static int32_t cos100(uint32_t k) { return SIN32[(k + 8u) & 31u]; }

/* a rounded box; the corners anti-aliased (each corner pixel by its distance from the corner's centre) */
static void cv_round(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
#ifdef GFX_AUDIT
    gfx_audit_box(x, y, w, h);
#endif
    if (r > h / 2)
        r = h / 2;
    if (r > w / 2)
        r = w / 2;
    if (r < 1) {
        cv_rect(x, y, w, h, c);
        return;
    }
    cv_rect(x, y + r, w, h - 2 * r, c);
    cv_rect(x + r, y, w - 2 * r, r, c);
    cv_rect(x + r, y + h - r, w - 2 * r, r, c);
    for (int32_t j = 0; j < r; j++)
        for (int32_t i = 0; i < r; i++) {
            float dx = (float)r - ((float)i + 0.5f), dy = (float)r - ((float)j + 0.5f), d2 = dx * dx + dy * dy;
            float cov = (float)r + 0.5f - (d2 > 1.0f ? f_sqrt1(d2) : 1.0f);
            uint32_t a = cov >= 1.0f ? 256u : cov <= 0.0f ? 0u : (uint32_t)(cov * 256.0f);
            cv_blend(x + i, y + j, c, a);                     /* the four corners, mirrored */
            cv_blend(x + w - 1 - i, y + j, c, a);
            cv_blend(x + i, y + h - 1 - j, c, a);
            cv_blend(x + w - 1 - i, y + h - 1 - j, c, a);
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
static int32_t pill_o(int32_t x, int32_t y, int32_t w, const char *s, uint16_t c, uint16_t fg)
{
    if (!w)
        w = text_w(&FONT_S, s) + 16;
    cv_round(x, y, w, 20, 6, c);
    cv_round(x + 1, y + 1, w - 2, 18, 5, K_BG);
    text_c(x + w / 2, y + 2, &FONT_S, s, fg);
    return x + w;
}
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
#define HDR_H 24
#define MAIN_Y 24
#define MAIN_H 120
#define TAG_Y 144
#define TAG_H 14                                     /* the small font's descenders fit */
#define KNB_Y 158
#define KNB_H 50
#define ENC_Y 208
#define FOOT_Y 224
enum { AL_INFO, AL_DONE, AL_UNDO, AL_REDO, AL_ASK, AL_NO };

static int sel_trk(void) { return proj.trk_sel < NALLTRK ? proj.trk_sel : 0; }
static int is_aux(int t) { return t >= NTRK; }
static int is_seqt(int t) { return t < NSEQT; }
static int is_drum(int t) { return t < NTRK && proj.trk[t].par[TP_ENGINE] == EG_DRUMLINE; }
static int is_fx(int t) { return t >= AUX_FX1; }
static int cur_scene(void) { return seq.scene < NSCENE ? seq.scene : 0; }
static int cur_pat(int t) { return is_seqt(t) ? seq.ppat[t] % NPATT : 0; }
static tpat_t *tpat(int t) { return &pats.pat[is_seqt(t) ? t : 0][cur_pat(t)]; }
static int playing(void) { return seq.playing; }
static int recording(void) { return seq.rec || seq.rec_wait || ui.rec_armed || ui.maestro_rec; }
static int step_shown(int i) { return ui.bar * 16 + i; }
static int home_held(void) { return ui.btn >> B_HOME & 1u; }
static int pl_type(int t) { return is_aux(t) ? PL_OFF : proj.trk[t].par[TP_PLAYER] % NPL; }
static int inst_trk(void) { return is_aux(sel_trk()) ? ui.last_inst % NTRK : sel_trk(); }   /* the keys' instrument */
static int fx_page(int p) { return p == P_FX1 || p == P_FX2; }
/* the track a page's knobs edit: the FX pages their effect track, Sends the instrument */
static int page_trk(int p)
{
    return p == P_FX1 ? AUX_FX1 : p == P_FX2 ? AUX_FX2 : p == P_SENDS ? inst_trk() : sel_trk();
}
static int group_n(int g)
{
    int trk = sel_trk(), b = PAGE_BTN[g].btn;
    if (b == B_EDIT)
        return trk == AUX_PUNCH || trk == AUX_TAPE ? 1 : 2;
    if (b == B_ARP)
        return pl_type(trk) == PL_ARP ? 3 : 2;
    return PAGE_BTN[g].n;
}
/* the TP_* a page's knob k edits, -1 none */
static int tp_page_param(int p, int k, int trk)
{
    static const int8_t M[][4] = {
        {TP_E1, TP_E2, TP_E3, TP_E4}, {TP_VMODE, TP_PORTA, TP_BEND, TP_VOLUME}, {TP_A, TP_D, TP_S, TP_R},
        {TP_CUT, TP_RES, TP_FENV, TP_KTRK}, {TP_FA, TP_FD, TP_FS, TP_FR}, {TP_SEND1, TP_SEND2, TP_SENDT, TP_LEVEL},
        {TP_E1, TP_E2, TP_E3, TP_E4}, {TP_E1, TP_E2, TP_E3, TP_E4}, {TP_LTYPE, TP_LSPEED, TP_LAMT, TP_LDEST},
        {TP_LX, TP_LX2, -1, -1}, {TP_PLAYER, TP_P1, TP_P2, TP_P3}, {TP_AP1, TP_AP2, TP_AP3, TP_AP4},
        {TP_P4, -1, -1, -1}, {TP_SEND1, TP_SEND2, TP_PAN, TP_LEVEL}};
    if (p < P_ENGINE || p > P_MIX || k < 0 || k > 3)
        return -1;
    if (fx_page(p))                                  /* (the effect tracks' own knobs, whatever trk is) */
        return M[p - P_ENGINE][k];
    if (p == P_SENDS)
        return is_aux(trk) ? -1 : M[p - P_ENGINE][k];
    if (is_aux(trk)) {
        if (p == P_MIX)
            return k == 3 ? TP_LEVEL : -1;
        if (p == P_ENGINE)
            return trk == AUX_PUNCH && k ? -1 : TP_E1 + k;
        if (p == P_PLAY && is_fx(trk)) {             /* an FX track's page 2: high-pass, low-pass, the feed into FX II */
            static const int8_t F2[4] = {TP_FENV, TP_CUT, TP_SEND2, -1};
            return k == 2 && trk != AUX_FX1 ? -1 : F2[k];
        }
        return -1;
    }
    if (is_drum(trk)) {
        int key = ui.dsound % DL_NSOUND;
        if (p == P_ENGINE)
            return TP_DRUM(key, DP_K1 + k);
        if (p == P_PLAY) {
            static const uint8_t K2[4] = {DP_MODEL, DP_RANDOM, DP_PAN, DP_VOLUME};
            return TP_DRUM(key, K2[k]);
        }
    }
    if (p == P_PLAYERS2 && pl_type(trk) != PL_ARP)
        return k == 0 ? TP_P4 : -1;
    if (p == P_PLAYERS3 && pl_type(trk) != PL_ARP)
        return -1;
    return M[p - P_ENGINE][k];
}

/* a pen for smooth curves: pen_to draws an anti-aliased line from the last point (pen_up lifts it) */
typedef struct { float x, y; int down; } pen_t;
static void pen_up(pen_t *p) { p->down = 0; }
static void pen_to(pen_t *p, float x, float y, uint16_t c)
{
    if (p->down)
        cv_linef(p->x, p->y, x, y, c, 1.7f);
    p->x = x, p->y = y, p->down = 1;
}
static float ease_exp(float t)                       /* 1 -> 0 along an exponential, reaching 0 at t = 1 */
{
    static const float E4 = 0.0183156f;              /* exp(-4) */
    return (fm_expf(-4.0f * t) - E4) / (1.0f - E4);
}

/* ---------------------------------------------------------- alerts --- */
#define AL_X 28
static void alert_box(int kind)
{
    uint16_t fill = kind == AL_ASK ? K_REC : kind == AL_NO ? K_PANEL2 : K_PANEL;
    uint16_t edge = kind == AL_ASK ? K_TEXT : kind == AL_NO ? K_TEXT : K_DIM;
    uint16_t badge = kind == AL_ASK ? K_TEXT : kind == AL_NO ? K_TEXT : K_DIM;
    uint16_t mark = kind == AL_ASK ? K_REC : K_BG;
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
static void tempo_text(char *t)                      /* "120.0" */
{
    char *e = num_s(proj.tempo_x10 / 10u, t);
    *e++ = '.';
    num_s(proj.tempo_x10 % 10u, e);
}
static void draw_header(void)
{
    char t[40];
    int msg = plat_ms() < ui.msg_until, rec = recording(), ext = seq.ext, ask = ui.ask, cnt = seq.count_in;
    uint32_t h = hash_u(2166136261u, ui.page | (uint32_t)msg << 8 | (uint32_t)ui.dirty << 9 | (uint32_t)rec << 11 |
                                         (uint32_t)ext << 12 | (uint32_t)ask << 14 | (uint32_t)proj.tempo_x10 << 16);
    h = hash_u(h, (uint32_t)cnt | (uint32_t)playing() << 8 | (uint32_t)is_drum(sel_trk()) << 9 |
                      (uint32_t)page_trk(ui.page) << 10 | (uint32_t)trk_ink(page_trk(ui.page)) << 16);
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
            cv_text(AL_X, 4, &FONT_B, ui.ask_q, K_TEXT);
        else if (text_w(&FONT_S, ui.ask_q) <= 208)
            cv_text(AL_X, 5, &FONT_S, ui.ask_q, K_TEXT);
        else
            text_fit(AL_X, 5, &FONT_XS, ui.ask_q, 232 - AL_X, K_TEXT);
    } else if (cnt && playing()) {
        int32_t x = cv_text(8, 4, &FONT_B, "Count-in", K_TEXT);
        num_s((uint32_t)cnt, t);
        cv_text(x + 8, 4, &FONT_B, t, K_REC);
        cv_round(222, 8, 9, 9, 4, K_REC);
    } else if (msg) {
        int no = ui.msg_no;
        int32_t x;
        alert_box(no ? AL_NO : ui.msg_kind);
        x = cv_text(AL_X, 4, &FONT_B, ui.msg[0], K_TEXT);
        text_fit(x + 8, 4, &FONT_B, ui.msg[1], 232 - (x + 8), K_DIM);
    } else {
        int32_t r = 232, x = 8;
        const char *bn = BTN_LABEL[PAGE_BTN[page_group(ui.page)].btn];
        const char *pn = PAGE_NAME[ui.page];
        int32_t w = text_w(&FONT_XS, bn) + 6;
        if (is_drum(sel_trk()) && ui.page == P_PLAY)
            pn = "Key";
        if (ui.page >= P_STEPS && ui.page <= P_PLAYERS3) {   /* a track's page: the track first, in its colour */
            int pt = page_trk(ui.page);
            const char *tn = TRACK_NAME[pt];
            int32_t tw = text_w(&FONT_XS, tn) + 10;
            cv_round(x, 4, tw, 17, 8, trk_ink(pt));
            cv_text(x + 5, cap_y(4, 17, &FONT_XS) - 1, &FONT_XS, tn, K_BG);
            x += tw + 5;
        }
        cv_round(x, 4, w, 17, 3, K_DIM);
        cv_round(x + 1, 5, w - 2, 15, 2, K_BG);
        cv_text(x + 3, cap_y(4, 17, &FONT_XS) - 1, &FONT_XS, bn, K_DIM);
        x += w + 6;
        cv_text(x, 4, &FONT_B, pn, K_TEXT);
        if (ext)
            s_cat(t, "ext");
        else
            tempo_text(t);
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
    int32_t num, max;
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
static const char *short_name(const char *n)        /* a knob name short enough for the strip */
{
    return s_eq(n, "frequency") ? "freq" : s_eq(n, "modulation") ? "mod" : s_eq(n, "feedback") ? "fdbk"
           : s_eq(n, "envelope") ? "env" : s_eq(n, "keytrack") ? "keytrk" : n;
}
/* a track parameter's knob: its name and value as synth.c prints them (the value v, or the track's) */
static void ktp_val(knob_t *k, int trk, int p, int v)
{
    const uint8_t *par = proj.trk[trk].par;
    const tp_info_t *in = tp_info_par(p, par);
    char num[12], unit[6], val[24], *e;
    if (!in->name || !in->name[0] || s_eq(in->name, "-"))
        return;
    tp_text_par(p, v, par, num, unit);
    e = s_cat(val, num);
    if (unit[0]) {
        if (unit[0] != '%')
            e = s_cat(e, " ");
        s_cat(e, unit);
    }
    kv(k, short_name(in->name), val, v - in->lo, in->kind == PK_NAMES || in->kind == PK_SWITCH ? 0 : in->hi - in->lo);
}
static void ktp(knob_t *k, int trk, int p)
{
    if (p >= 0 && p < NTP)
        ktp_val(k, trk, p, proj.trk[trk].par[p]);
}
/* a length in steps, from quarter steps: "1/2", "1", "1 1/2", "8"; 0: the pattern's note length, "1/2 auto" */
static void quarters_text(int q, char *t)
{
    static const char *const Q[4] = {"", "1/4", "1/2", "3/4"};
    char *e = t;
    *t = 0;
    if (q >= 4) {
        e = num_s((uint32_t)q / 4u, t);
        if (q & 3)
            e = s_cat(e, " ");
    }
    if (q & 3 || !q)
        s_cat(e, q ? Q[q & 3] : "0");
}
static void step_len_text(int len, char *t, int nlen)
{
    if (len) {
        quarters_text(len, t);
        return;
    }
    if (nlen == 25 || nlen == 50 || nlen == 75 || nlen == 100)
        quarters_text(nlen / 25, t);
    else
        s_cat(num_s((uint32_t)nlen, t), "%");
    s_cat(t + s_len(t), " auto");
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
    switch (comp) {
    case SC_VELOCITY: return VEL_VAL[clampi(v, 0, 9)];
    case SC_RAMP_UP: case SC_RAMP_DOWN: case SC_RANDOM: return RAMP_VAL[clampi(v, 0, 9)];
    case SC_PORTAMENTO: return PORTA_VAL[clampi(v, 0, 9)];
    case SC_BEND: return BEND_VAL[clampi(v, 0, 9)];
    case SC_TONALITY: return TON_VAL[clampi(v, 0, 9)];
    case SC_JUMP: return JUMP_VAL[clampi(v, 0, 9)];
    case SC_SKIP_LOCK: case SC_SKIP_COMP: case SC_SKIP: {   /* "on 4", "not 4", "1st/4" of the cycle */
        int n = (int)SK_N((uint32_t)v), m = (int)SK_MODE((uint32_t)v);
        char *e;
        if (n >= SC_RANDOM_VAL)
            return "random";
        e = s_cat(t, m == 0 ? "on " : m == 1 ? "not " : "1st of ");
        num_s((uint32_t)n + 1u, e);
        return t;
    }
    default:
        if (v >= SC_RANDOM_VAL)
            return "random";
        num_s((uint32_t)v + 1u, t);
        return t;
    }
}
static void step_knobs(knob_t *k)
{
    const tpat_t *tp = tpat(sel_trk());
    const step_t *st = &tp->st[step_shown(ui.held_first)];
    char t[16];
    kn(&k[0], "VEL", st->vel, 127);
    t[0] = 0;
    step_len_text(st->len, t, tp->nlen);
    kv(&k[1], "LENGTH", t, st->len, 64);
    t[0] = 0;
    cond_text(st->cond, t);
    kv(&k[2], "CHANCE", t, 0, 0);
    {
        char *e = t;
        if (st->nudge > 0)
            e = s_cat(e, "+");
        num_i(st->nudge, e);
        kv(&k[3], "NUDGE", t, st->nudge + 12, 24);
    }
}
/* HOME + a step held: the strip shows the step's components and their values */
static void comp_knobs(knob_t *k)
{
    const step_t *st = &tpat(sel_trk())->st[step_shown(ui.held_first)];
    char t[16];
    kv(&k[0], "TRICK 1", st->comp ? COMP_NAME[st->comp % NSC] : "-", 0, 0);
    kv(&k[1], "VALUE", st->comp ? comp_val_text(st->comp, st->cval, t) : "-", 0, 0);
    kv(&k[2], "TRICK 2", st->comp2 ? COMP_NAME[st->comp2 % NSC] : "-", 0, 0);
    kv(&k[3], "VALUE", st->comp2 ? comp_val_text(st->comp2, st->cval2, t) : "-", 0, 0);
}
/* the lock editor: a step held + EDIT / ENV / FX / LFO: that page's four, with the lock's value ("-": none) */
static void lock_knobs(knob_t *k)
{
    int trk = sel_trk(), pg = ui.lockpg - 1, step = step_shown(ui.held_first);   /* (lock pages are the track's own) */
    for (int i = 0; i < 4; i++) {
        int p = tp_page_param(pg, i, trk), ln;
        if (p < 0)
            continue;
        ktp(&k[i], trk, p);
        if (!k[i].name)
            continue;
        ln = lane_find(pats.lane, trk, cur_pat(trk), p);
        if (ln < 0 || pats.lane[ln].val[step] == LOCK_NONE)
            kv(&k[i], k[i].name, "-", 0, 0);
        else
            ktp_val(&k[i], trk, p, pats.lane[ln].val[step]);
    }
}
static void tape_knob(knob_t *k, int i, int v)
{
    char t[12], *e;
    if (i == 0)
        kv(k, TAPE_KNOB[0], TAPE_PITCH[v * 5 / 128], v, 127);
    else if (i == 1) {
        e = num_s(50u + (uint32_t)v * 150u / 127u, t);
        s_cat(e, "%");
        kv(k, TAPE_KNOB[1], t, v, 127);
    } else if (i == 2)
        kn(k, TAPE_KNOB[2], 1 + v * 15 / 127, 16);
    else {
        e = num_s((uint32_t)v * 100u / 127u, t);
        s_cat(e, "%");
        kv(k, TAPE_KNOB[3], t, v, 127);
    }
}
static void aux_engine_knobs(knob_t *k, int trk)
{
    const uint8_t *par = proj.trk[trk].par;
    if (trk == AUX_BRAIN) {
        kname(&k[0], BRAIN_KNOB[0], BRAIN_MODE, par[TP_E1], 2);
        kname(&k[1], BRAIN_KNOB[1], NOTE_NAME, par[TP_E2], 12);
        kname(&k[2], BRAIN_KNOB[2], BS_NAME, par[TP_E3], NBS);
        kname(&k[3], BRAIN_KNOB[3], LINK_NAME, par[TP_E4], 9);
    } else if (trk == AUX_PUNCH) {
        kname(&k[0], "latch", ONOFF, par[TP_E1], 2);
    } else if (trk == AUX_TAPE) {
        for (int i = 0; i < 4; i++)
            tape_knob(&k[i], i, par[TP_E1 + i]);
    } else {
        int type = par[TP_ENGINE] % NFXT;
        for (int i = 0; i < 4; i++)
            kn(&k[i], short_name(FXT_KNOB[type][i]), par[TP_E1 + i] * 100 / 128, 99);
    }
}
static void page_knobs(knob_t *k)
{
    int trk = sel_trk(), p = ui.page;
    tpat_t *tp = tpat(trk);
    char t[16], *e;
    memset(k, 0, 4 * sizeof k[0]);
    if ((ui.btn >> B_REC & 1u) && !home_held()) {   /* REC held: the record settings */
        static const char *const CIN[3] = {"off", "1 bar", "2 bars"};
        static const char *const MODE[2] = {"overdub", "replace"};
        kname(&k[0], "CLICK", METRO_NAME, proj.metronome, 3);
        kname(&k[1], "COUNT", CIN, proj.rsv[0] % 3u, 3);
        if (is_seqt(trk))
            kn(&k[2], "QUANT", tp->quant, 100);
        kname(&k[3], "MODE", MODE, proj.rsv[2] & 1u, 2);
        return;
    }
    if (ui.lockpg) {
        lock_knobs(k);
        return;
    }
    if (p == P_STEPS && ui.cstep && is_seqt(trk)) {
        comp_knobs(k);
        return;
    }
    if (p == P_STEPS && ui.held && is_seqt(trk)) {
        step_knobs(k);
        return;
    }
    switch (p) {
    case P_ARRANGE:
        tempo_text(t);
        kv(&k[0], "TEMPO", t, (int)proj.tempo_x10 / 10 - 40, 180);
        kname(&k[1], "GROOVE", GR_NAME, proj.groove, NGROOVE);
        if (is_seqt(trk))
            kn(&k[2], "AMOUNT", tp->groove, 0);
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
        kn(&k[3], "GROOVE", tp->groove, 0);
        break;
    case P_BAR:
        if (!is_seqt(trk))
            break;
        kn(&k[0], "QUANT", tp->quant, 100);
        e = num_s(tp->nlen, t);
        s_cat(e, "%");
        kv(&k[1], "NOTE", t, tp->nlen, 100);
        kn(&k[2], "GROOVE", tp->groove, 0);
        kn(&k[3], "SHAPE", tp->shape, 100);
        break;
    case P_ENGINE:
        if (is_aux(trk))
            aux_engine_knobs(k, trk);
        else
            for (int i = 0; i < 4; i++)
                ktp(&k[i], trk, tp_page_param(p, i, trk));
        break;
    case P_FX1: case P_FX2: {                        /* an effect: its four knobs (the type: PRESETS, SEL) */
        int ft = P_FX1 == p ? AUX_FX1 : AUX_FX2, type = proj.trk[ft].par[TP_ENGINE] % NFXT;
        for (int i = 0; i < 4; i++)
            kn(&k[i], short_name(FXT_KNOB[type][i]), proj.trk[ft].par[TP_E1 + i] * 100 / 128, 99);
        break;
    }
    case P_SENDS: {                                  /* the instrument's sends and level */
        int it = inst_trk();
        ktp(&k[0], it, TP_SEND1);
        ktp(&k[1], it, TP_SEND2);
        ktp(&k[2], it, TP_SENDT);
        ktp(&k[3], it, TP_LEVEL);
        k[0].name = "fx I";
        k[1].name = "fx II";
        k[2].name = "tape";
        break;
    }
    case P_PLAY:
        if (is_fx(trk)) {
            const uint8_t *par = proj.trk[trk].par;
            kn(&k[0], "HP", par[TP_FENV], 127);
            kn(&k[1], "LP", par[TP_CUT], 127);
            if (trk == AUX_FX1)
                kn(&k[2], "TO FX II", par[TP_SEND2], 127);
        } else if (!is_aux(trk))
            for (int i = 0; i < 4; i++)
                ktp(&k[i], trk, tp_page_param(p, i, trk));
        break;
    case P_MIX:
        if (is_aux(trk))
            ktp(&k[3], trk, TP_LEVEL);
        else {
            ktp(&k[0], trk, TP_SEND1);
            ktp(&k[1], trk, TP_SEND2);
            ktp(&k[2], trk, TP_PAN);
            ktp(&k[3], trk, TP_LEVEL);
            k[0].name = "FX I";
            k[1].name = "FX II";
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
        kname(&k[1], "CLOCK", ONOFF, proj.send_clock != 0, 2);
        kname(&k[2], "NOTES", ONOFF, proj.send_notes != 0, 2);
        kv(&k[3], "CORES", proj.one_core ? "one" : "two", 0, 0);
        break;
    case P_STATUS: case P_ARRANGE + 100:
        break;
    default:                                         /* the sound pages: every knob through tp_info_par */
        if (!is_aux(trk))
            for (int i = 0; i < 4; i++)
                ktp(&k[i], trk, tp_page_param(p, i, trk));
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
    page_knobs(k);
    for (i = 0; i < 4; i++) {
        h = hash_s(h, k[i].val);
        h = hash_s(h, k[i].name);
        h = hash_u(h, (uint32_t)k[i].num << 8 | (uint32_t)(k[i].name != 0));
    }
    h = hash_u(h, (uint32_t)now_touch | (uint32_t)ui.lockpg << 1);
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
        int32_t x = (int32_t)i * 60, cx = x + 30, nw;
        int hot = now_touch && ui.touched == (int)i, lock = ui.lockpg && !s_eq(k[i].val, "-");
        uint16_t vc = hot ? K_SEL : lock ? K_REC : K_TEXT;
        if (i)
            cv_rect(x, 10, 1, KNB_H - 22, K_LINE);
        if (!k[i].name)
            continue;
        nw = text_w(&FONT_XS, k[i].name);
        if (nw > 46)
            nw = 46;
        text_fit(cx - nw / 2, 0, &FONT_XS, k[i].name, 46, hot ? K_TEXT : K_DIM);
        if (upper_only(k[i].val) && text_w(&FONT_M, k[i].val) <= 48)   /* (48 px: clear of the panel's sides) */
            text_c(cx, cap_y(12, 28, &FONT_M), &FONT_M, k[i].val, vc);
        else if (text_w(&FONT_B, k[i].val) <= 48)
            text_c(cx, cap_y(12, 28, &FONT_B), &FONT_B, k[i].val, vc);
        else if (text_w(&FONT_XS, k[i].val) <= 48)
            text_c(cx, cap_y(12, 28, &FONT_XS), &FONT_XS, k[i].val, vc);
        else
            text_fit(cx - 24, cap_y(12, 28, &FONT_XS), &FONT_XS, k[i].val, 48, vc);
        if (k[i].max > 0) {
            int32_t bw = 44 * k[i].num / k[i].max;
            cv_round(cx - 22, 41, 44, 6, 3, K_LINE);
            if (bw > 0)
                cv_round(cx - 22, 41, bw < 6 ? 6 : bw, 6, 3, hot ? K_SEL : K_NOW);
        }
    }
    cv_blit(0, KNB_Y);
}

/* ------------------------------------------------- tag, footer, cards --- */
static void track_crumb(char *t)
{
    int trk = sel_trk();
    char *e = s_cat(t, TRACK_NAME[trk]);
    if (!is_aux(trk)) {
        e = s_cat(e, " ");
        e = s_cat(e, EG_NAME[proj.trk[trk].par[TP_ENGINE] % NENGINE]);
    }
    if (is_seqt(trk))
        e = num_s((uint32_t)cur_pat(trk) + 1u, s_cat(e, ">P"));
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
        if (ui.lockpg) {
            e = num_s((uint32_t)step_shown(ui.held_first) + 1u, s_cat(t + s_len(t), ">step "));
            s_cat(s_cat(e, ">"), PAGE_NAME[ui.lockpg - 1]);
        } else if (ui.held || ui.cstep)
            num_s((uint32_t)step_shown(ui.held_first) + 1u, s_cat(t + s_len(t), ">step "));
        break;
    case P_ENGINE: case P_PLAY: case P_ENV: case P_FILTER: case P_ENV2: case P_LFO: case P_LFO2:
        track_crumb(t);
        if (is_drum(sel_trk()))
            s_cat(s_cat(t + s_len(t), ">"), DL_SOUND_NAME[ui.dsound % DL_NSOUND]);
        break;
    case P_SENDS:
        s_cat(s_cat(t, TRACK_NAME[inst_trk()]), ">sends");
        break;
    case P_FX1: case P_FX2:
        s_cat(s_cat(s_cat(t, TRACK_NAME[ui.page == P_FX1 ? AUX_FX1 : AUX_FX2]), ">"),
              FXT_NAME[proj.trk[ui.page == P_FX1 ? AUX_FX1 : AUX_FX2].par[TP_ENGINE] % NFXT]);
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
    int g = page_group(ui.page), n = group_n(g), d = ui.page - PAGE_BTN[g].first;
    int32_t r = 232, x, w;
    uint32_t h;
    page_tag(t);
    h = hash_s(hash_u(2166136261u, ui.page | (uint32_t)n << 8), t);
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
    if ((ui.btn >> B_REC & 1u) && !home_held()) {   /* REC held: its knobs, and step record */
        s_cat(t, playing() ? "KNOBS record settings|KEYS record" : "KNOBS record settings|PLAY record");
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
    if (home_held()) {
        if ((ui.page == P_STEPS || ui.page == P_BAR) && ui.cstep)
            s_cat(t, "WHITE trick|BLACK value");
        else if (ui.page == P_STEPS || ui.page == P_BAR)
            s_cat(t, "HOME+WHITE step: trick|REC undo|PLAY redo");
        else if (ui.page == P_ARRANGE)
            s_cat(t, "HOME+WHITE cue scene|REC undo|PLAY redo");
        else if (ui.page == P_MIX)
            s_cat(t, "HOME+BLACK mute|REC undo|PLAY redo");
        else
            s_cat(t, "HOME+KEYS throws|REC undo|PLAY redo");
        return;
    }
    switch (ui.page) {
    case P_ARRANGE:
        s_cat(t, "WHITE scene|BLACK mute|HOME+WHITE at the bar");
        break;
    case P_SONG:
        s_cat(t, "WHITE add scene|PLAY song");
        break;
    case P_BAR:
        s_cat(t, is_seqt(trk) ? "BLACK scale" : "ALGO track: no steps here");
        break;
    case P_STEPS:
        if (!is_seqt(trk))
            s_cat(t, "ALGO track: no steps here");
        else if (ui.lockpg)
            s_cat(t, "KNOBS lock|SELECT page|SAVE clear");
        else if (ui.held && is_drum(trk))
            s_cat(t, "BLACK sounds|OCT nudge|EDIT ENV FX LFO lock");
        else if (ui.held)
            s_cat(t, "WHITE length|OCT nudge|EDIT ENV FX LFO lock");
        else if (ui.rec_armed)
            s_cat(t, "KEYS write|OCT+ rest|OCT- back|REC done");
        else if (seq.rec || seq.rec_wait)
            s_cat(t, "KEYS record|REC off");
        else if (ui.btn >> B_SEQ & 1u)
            s_cat(t, is_drum(trk) ? "KEYS pick a sound" : "KEYS the notes a step writes");
        else if (is_drum(trk))
            s_cat(t, "WHITE steps|BLACK sound|SEQ held: pick a sound");
        else
            s_cat(t, "WHITE steps|SEQ held: play notes");
        break;
    case P_MIX:
        s_cat(t, "BLACK track|hold: solo");
        break;
    case P_MASTER: case P_MASTER2: case P_MASTER3: case P_SETUP: case P_STATUS:
        break;
    case P_SENDS:
        s_cat(t, "KEYS play");
        break;
    case P_FX1: case P_FX2:
        s_cat(t, "PRESET effect|SEL filter|KEYS play");
        break;
    default:
        if (ui.rec_armed)
            s_cat(t, "KEYS write|OCT+ rest|OCT- back|REC done");
        else if (trk == AUX_BRAIN)
            s_cat(t, ui.page == P_PLAY ? "BLACK route track" : "KEYS chord");
        else if (trk == AUX_PUNCH)
            s_cat(t, "KEYS fx|REC record");
        else if (trk == AUX_TAPE)
            s_cat(t, "WHITE segment|REC record");
        else if (is_aux(trk))
            s_cat(s_cat(t, "KEYS play "), TRACK_NAME[ui.last_inst % NTRK]);
        else if (ui.maestro_rec)
            s_cat(t, "KEYS the chord|REC cancel");
        else if (ui.page >= P_PLAYERS && ui.page <= P_PLAYERS3 && pl_type(trk) == PL_STRUM)
            s_cat(t, "REC record the chord|KEYS play");
        else if (ui.page >= P_PLAYERS && ui.page <= P_PLAYERS3 && pl_type(trk) == PL_ARP)
            s_cat(t, "KEYS hold notes: they arpeggiate");
        else if (is_drum(trk) && ui.page <= P_LFO2)
            s_cat(t, "BLACK pick a key|KEYS play");
        else if (ui.page == P_ENGINE)
            s_cat(t, "PRESET next sound|SEL engine");
        else if (ui.page == P_FILTER)
            s_cat(t, "PRESET filter type|KEYS play");
        else if (ui.page == P_ENV)
            s_cat(t, "KEYS play|REC held: record");
        else if (ui.page == P_LFO)
            s_cat(t, "KEYS play|REC held: record");
        else
            s_cat(t, "KEYS play|REC held: record");
        break;
    }
}
static void enc_line(char *t)
{
    int g = page_group(ui.page), n = group_n(g), p = ui.page, trk = sel_trk();
    char *e = t;
    *t = 0;
    if (ui.ask || (lst.open && lst.n) || ui.card)
        return;
    if (home_held()) {
        s_cat(t, "SELECT tempo|PRESET scene|KNOBS fine");
        return;
    }
    if (n > 1)                        /* SELECT names what it turns to where that is the point of the page */
        e = s_cat(t, p == P_SENDS ? "SELECT effects" : p == P_ENV ? "SELECT filter" : p == P_LFO ? "SELECT more"
                                                                                              : "SELECT page");
    if (p != P_ARRANGE && p != P_SONG && p < P_MASTER)
        e = s_cat(e, *t ? "|ALGO track" : "ALGO track");
    {
        const char *a = p == P_ARRANGE ? "scene" : p == P_SONG ? "slot"
                        : (p == P_STEPS || p == P_BAR) && is_drum(trk) ? "sound"
                        : (p == P_STEPS || p == P_BAR) && is_seqt(trk) ? "pattern"
                        : (p == P_ENGINE || p == P_PLAY) && is_drum(trk) ? "key"
                        : p == P_ENGINE && is_fx(trk) ? "effect"
                        : p == P_MIX ? "track" : 0;   /* (Engine, Filter, the FX pages: the footer says it) */
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
    if (s[0] == 'O' && s[1] == 'C' && s[2] == 'T' && (s[3] == '+' || s[3] == '-') && (s[4] == ' ' || !s[4]))
        return 4;                                    /* "OCT+ rest": one control, its sign part of the name */
    for (;; n++)
        if (s[n] == '+' || s[n] == ' ' || !s[n]) {
            if (n == a || !ctl_is(s + a, n - a))
                return 0;
            if (s[n] != '+')
                return n;
            a = n + 1;
        }
}
static int s_has_q(const char *s)                   /* a label with a Q: its tail needs the pixel below */
{
    for (; *s; s++)
        if (*s == 'Q')
            return 1;
    return 0;
}
static int32_t bot_part(int32_t x, int32_t y, const char *s, uint32_t n, const felucca_font_t *job, int draw)
{
    char b[48];
    uint32_t c = ctl_len(s), a = 0, i;
    int32_t x0 = x;
    if (c > n)
        c = 0;
    for (i = 0; c && i <= c; i++)
        if (i == c || (s[i] == '+' && !(i == 3 && s[0] == 'O'))) {     /* ("OCT+": its sign is part of the name) */
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
                cv_text(x + pad, cap_y(y, BOT_PH, &FONT_XS) - (s_has_q(b) ? 1 : 0), &FONT_XS, b, K_TEXT);   /* (Q's tail) */
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
#define PG_SOUND (PG(P_ENGINE) | PG(P_PLAY) | PG(P_ENV) | PG(P_ENV2) | PG(P_FILTER) | PG(P_LFO) | PG(P_LFO2))
static const struct { uint8_t btn; uint32_t pages; const char *title, *l1, *l2; } CARD[] = {
    {B_SEQ, PG_ANY, "Steps", "The bar's 16 steps on the white keys. Hold SEQ and play the notes,",
     "then press steps to write them. HOME + a step: tricks."},
    {B_EDIT, PG(P_STEPS), "Lock", "With a step held, EDIT, ENV, FX or LFO show that module's knobs.",
     "Turn one to lock it on the step; SELECT turns pages. SAVE clears."},
    {B_EDIT, PG_ANY, "Engine", "The track's engine and its four knobs. PRESETS: its next sound.",
     "SEL changes the engine. Page 2: Play. Drums: black keys pick a key."},
    {B_ENV, PG_ANY, "Envelope", "The amp's envelope, then the filter, then the filter's envelope.",
     "On the Filter page, PRESETS turns the filter type."},
    {B_FX, PG_ANY, "Effects", "The track's sends to FX I, FX II and tape, then the two effects.",
     "On FX I and FX II, PRESETS picks the effect; SEL has its filter."},
    {B_LFO, PG_ANY, "LFO", "Type, then its first three knobs.", "Page 2 has the type's fourth and fifth."},
    {B_ARP, PG_ANY, "Players", "Arp, strum or hold on the track's keys.",
     "With strum, REC records the chord you hold next. A key plays it."},
    {B_GLO, PG_ANY, "Mix", "Sends, pan and level of the track. Black keys pick a track, held: solo.",
     "SELECT: Master, Setup and Status. SEL has the projects."},
    {B_HOME, PG_ANY, "Arrange", "Each track's pattern in the scene. White keys: scenes, black: mutes.",
     "Hold HOME + a key: a throw. HOME + REC undoes, HOME + PLAY redoes."},
    {B_SEL, PG_ANY, "Lists", "The page's knobs, then its actions. SEL runs the one picked.",
     "SELECT moves, ALGORITHM changes, HOME goes back."},
    {B_SAVE, PG(P_STEPS), "Save", "Tap to save the project. With a step held, clears its locks.",
     "Hold SAVE and press white key 1 to 4 to save to that slot."},
    {B_SAVE, PG_SOUND, "Save", "Tap to save the project.",
     "Hold SAVE and press a white key: the sound as that user preset."},
    {B_SAVE, PG_ANY, "Save", "Tap to save the project.", "Hold SAVE and press white key 1 to 4 to save to that slot."},
    {B_REC, PG(P_PLAYERS) | PG(P_PLAYERS2) | PG(P_PLAYERS3), "Recording",
     "With strum, the chord you hold next becomes its chord.", "Else: hold it and play; stopped, a tap arms step record."},
    {B_REC, PG_ANY, "Recording", "Stopped: a tap, then each key writes a step. + PLAY: live.",
     "Playing: hold it and play. Held, the knobs set click, count-in, mode."},
    {B_PLAY, PG(P_SONG), "Play", "Plays the song from slot 1. PLAY again stops.", "HOME + PLAY redoes."},
    {B_PLAY, PG_ANY, "Play", "Starts and stops. Twice quickly: from the top.", "HOME + PLAY redoes."},
    {B_OCTDN, PG_STEPS, "Bar", "Shows the bar before. A step held: nudges it earlier.",
     "Step record: the cursor back. Hold both for 5 seconds for update mode."},
    {B_OCTUP, PG_STEPS, "Bar", "Shows the next bar. A step held: nudges it later.",
     "Step record: the cursor on. Hold both for 5 seconds for update mode."},
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
    uint32_t h = hash_u(hash_u(2166136261u, 0xCAu), ui.card + ((uint32_t)ui.page << 8));
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
        cv_rect(18, y - 6, 204, 1, K_DIM);
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
static void proj_slot_text(int slot, char *t)
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
    int keep = ui.page, g = page_group(keep), f = PAGE_BTN[g].first, n = group_n(g), trk = sel_trk();
    knob_t k[4];
    static char slot_t[24], fol_t[24], nm[NTRK][16], rm[SN][12];
    lst.n = 0;
    if (keep == P_STEPS && ui.held && is_seqt(trk)) {   /* a step held + SEL: its notes, to remove */
        const step_t *st = &tpat(trk)->st[step_shown(ui.held_first)];
        for (int i = 0; i < SN && st->note[i]; i++) {
            char *e = s_cat(rm[i], "REMOVE ");
            if (is_drum(trk))
                s_cat(e, DL_SOUND_NAME[(st->note[i] - 1) % DL_NSOUND]);
            else if (trk == AUX_PUNCH)
                s_cat(e, PX_VALID(st->note[i] - 1) ? PUNCH_NAME[(st->note[i] - 1) & 15] : "fx");
            else if (trk == AUX_TAPE)
                num_s((uint32_t)st->note[i], s_cat(e, "segment "));
            else
                note_name(st->note[i], e);
            list_add(rm[i], 0, LK_ACTION, A_REMOVE_NOTE);
        }
        return;
    }
    if (keep >= P_MIX || PAGE_BTN[g].btn == B_FX)
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
            if (clip_ok)
                list_add(ACTION_NAME[A_WEAVE], WEAVE_NAME[ui.pick_weave % NWV], LK_PICK, A_WEAVE);
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
    } else if (fx_page(keep)) {                     /* an effect: the type, its filter, the feed into FX II */
        int ft = keep == P_FX1 ? AUX_FX1 : AUX_FX2;
        static char hp[8], lp[8], fd[8];
        num_s(proj.trk[ft].par[TP_FENV], hp);
        num_s(proj.trk[ft].par[TP_CUT], lp);
        list_add(ACTION_NAME[A_EFFECT], FXT_NAME[ui.pick_fx % NFXT], LK_PICK, A_EFFECT);
        list_add(ACTION_NAME[A_FX_HP], hp, LK_PICK, A_FX_HP);
        list_add(ACTION_NAME[A_FX_LP], lp, LK_PICK, A_FX_LP);
        if (ft == AUX_FX1) {
            num_s(proj.trk[ft].par[TP_SEND2], fd);
            list_add(ACTION_NAME[A_FX_SEND], fd, LK_PICK, A_FX_SEND);
        }
    } else if (keep == P_SENDS) {
        list_add(ACTION_NAME[A_PANIC], 0, LK_ACTION, A_PANIC);
    } else if ((keep == P_ENV || keep == P_FILTER || keep == P_ENV2) && !is_aux(trk)) {
        list_add(ACTION_NAME[A_FTYPE], tp_info_par(TP_FTYPE, proj.trk[trk].par)->names[proj.trk[trk].par[TP_FTYPE] % NFT],
                 LK_PICK, A_FTYPE);
    } else if ((keep == P_ENGINE || keep == P_PLAY) && is_drum(trk)) {   /* the key's play mode, then the sound's */
        list_add(ACTION_NAME[A_PLAYMODE], PM_NAME[proj.trk[trk].par[TP_DRUM(ui.dsound % DL_NSOUND, DP_MODE)] % NPM],
                 LK_PICK, A_PLAYMODE);
        list_add(ACTION_NAME[A_SCRAMBLE], 0, LK_ACTION, A_SCRAMBLE);
        num_s((uint32_t)ui.pick_slot % NUSER_PRESETS + 1u, s_cat(slot_t, "U"));
        list_add(ACTION_NAME[A_SAVE_PRESET], slot_t, LK_PICK, A_SAVE_PRESET);
        list_add(ACTION_NAME[A_ENGINE], EG_NAME[ui.pick_engine % NENGINE], LK_PICK, A_ENGINE);
    } else if (keep >= P_ENGINE && keep <= P_PLAYERS3) {
        if (trk == AUX_BRAIN) {
            for (int t = 0; t < NTRK; t++) {
                s_cat(num_s((uint32_t)t + 1u, s_cat(nm[t], "T")), " FOLLOWS");
                list_add(nm[t], FOLLOW_NAME[proj.trk[t].par[TP_BRAIN] % 3], LK_PICK, A_FOLLOW);
            }
        } else if (is_fx(trk)) {
            list_add(ACTION_NAME[A_EFFECT], FXT_NAME[ui.pick_fx % NFXT], LK_PICK, A_EFFECT);
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
        {
            static const char *const CIN[3] = {"off", "1 bar", "2 bars"};
            list_add(ACTION_NAME[A_COUNT_IN], CIN[proj.rsv[0] % 3u], LK_PICK, A_COUNT_IN);   /* (rsv[0]: the count-in) */
        }
        list_add(ACTION_NAME[A_THEME], THEME_OF_PROJ()->name, LK_PICK, A_THEME);   /* (rsv[1]: the theme) */
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
        int32_t y = 1 + (int32_t)(i - lst.top) * LIST_RH - oy, ty = cap_y(y, LIST_RH - 1, &FONT_S) - 1;   /* (descenders clear the foot) */
        int on = i == lst.sel, k = lst.row[i].kind;
        uint16_t fg = on ? K_BG : K_TEXT, dim = on ? K_PANEL2 : K_DIM;
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
    uint32_t h = hash_u(hash_u(2166136261u, 0x4Cu), lst.sel | (uint32_t)lst.top << 8), i;
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
static int begin_main(uint32_t h)
{
    if (h == ui.sig[1])
        return 0;
    ui.sig[1] = h;
    ui.sig[5] = 0;                                   /* (the step record strip over it: drawn again) */
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
    uint32_t h = hash_u(hash_u(2166136261u, 0xA0u), (uint32_t)sc | (uint32_t)cue << 8 | (uint32_t)playing() << 16 |
                                          (uint32_t)sel_trk() << 20);
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
        uint16_t ink = muted ? K_FAINT : K_DIM;
        char nm[8];
        if (tr < NTRK)
            num_s((uint32_t)tr + 1u, nm);
        else
            s_cat(nm, tr == AUX_BRAIN ? "CO" : tr == AUX_PUNCH ? "TH" : "TP");
        text_r(22, y - 3, &FONT_XS, nm, tr == sel_trk() ? K_TEXT : ink);
        num_s((uint32_t)pt + 1u, s_cat(t, "P"));
        text_c(37, y - 3, &FONT_XS, t, muted ? K_FAINT : trk_ink(tr));
        cv_rect(52, y + 3, 96, 2, K_PANEL2);
        if (len > 0 && len <= NSTEP)
            cv_rect(52, y + 3, 96 * len / NSTEP, 2, muted ? K_LINE : trk_tint(tr));
        if (playing() && !muted && len > 0)
            cv_round(52 + 94 * (pos % len) / (len > 1 ? len - 1 : 1), y + 1, 3, 6, 1, trk_ink(tr));
        if (muted)
            cv_text(152, y - 3, &FONT_XS, "mute", K_DIM);
    }
    cv_rect(168, 4, 1, 112, K_LINE);
    text_c(204, 8, &FONT_XS, cue != 0xFF && cue != sc ? "scene, next" : "scene", K_DIM);
    num_s((uint32_t)sc + 1u, t);
    text_c(204, 24, &FONT_L, t, playing() ? K_NOW : K_TEXT);
    if (cue != 0xFF && cue != sc) {
        num_s((uint32_t)cue + 1u, t);
        text_c(204, 62, &FONT_B, t, K_TEXT);
    }
    for (int i = 0; i < NSCENE; i++) {
        int32_t x = 176 + (i % 8) * 7, y = 92 + (i / 8) * 9;
        cv_round(x, y, 5, 6, 1, pats.scene[i].used ? (i == sc ? K_NOW : K_DIM) : K_LINE);
        if (i == cue && cue != sc)
            cv_frame(x, y, 5, 6, 1, K_TEXT, K_BG);
    }
    cv_blit(0, MAIN_Y);
}

static void draw_song(void)
{
    const song_t *sg = &pats.song;
    int cur = ui.name_slot <= sg->len ? ui.name_slot : sg->len, top = (cur / 16) * 16, pp = seq.song_pos;
    uint32_t h = hash_u(hash_u(2166136261u, 0x50u), (uint32_t)cur | (uint32_t)sg->len << 8 | (uint32_t)sg->loop << 16 |
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
        if (i == sg->len) {
            cv_frame(x, y, 52, 22, 5, i == cur ? K_SEL : K_LINE, K_BG);
            text_c(x + 26, y + 3, &FONT_XS, sg->loop ? "loop" : "end", K_DIM);
            continue;
        }
        cv_round(x, y, 52, 22, 5, on ? K_NOW : K_PANEL);
        if (i == cur) {
            cv_round(x, y, 52, 22, 5, K_SEL);
            cv_round(x + 2, y + 2, 48, 18, 4, on ? K_NOW : K_PANEL);
        }
        num_s((uint32_t)sg->slot[i].scene + 1u, s_cat(t, "S"));
        cv_text(x + 6, y + 3, &FONT_B, t, on ? K_BG : K_TEXT);
        num_s((uint32_t)sg->slot[i].rep, s_cat(t, "x"));
        text_r(x + 47, y + 4, &FONT_XS, t, on ? K_BG : K_DIM);
    }
    if (sg->len >= 16) {
        num_s((uint32_t)sg->len, s_cat(num_s((uint32_t)top + 1u, s_cat(t, "slots ")), " to "));
        text_r(232, 112, &FONT_XS, t, K_DIM);
    }
    cv_blit(0, MAIN_Y);
}

/* Steps: what a press writes and the bars' squares, then the bar's 16 cells */
#define ST_X 4
#define ST_CW 29
#define ST_Y 22
#define ST_CH 46
/* a step's first line of text: the notes (two a line), a drum's sounds (the shown one first), a punch effect, a
 * tape segment, a brain chord */
static int step_on(const step_t *st, int trk)       /* the cell is "on" for the sound shown (drums) or any note */
{
    if (is_drum(trk)) {
        for (int k = 0; k < SN; k++)
            if (st->note[k] == ui.dsound + 1)
                return 1;
        return 0;
    }
    return !step_empty(st);
}
/* a chord's name from its notes (3 or 4): "C", "Am", "G7", "Fmaj7", "Dm7", "Bdim", "Caug", "Dsus2", "Gsus4";
 * the root is a note whose intervals to the others match a shape (the lowest tried first); 0 = none */
static int chord_name(const uint8_t *notes, int n, char *t)
{
    static const struct { uint16_t mask; const char *suf; } SHAPE[] = {
        {0x091, ""}, {0x089, "m"}, {0x491, "7"}, {0x891, "maj7"}, {0x489, "m7"}, {0x049, "dim"}, {0x111, "aug"},
        {0x085, "sus2"}, {0x0A1, "sus4"}};
    uint16_t pcs = 0;
    int lo = 0;
    if (n < 3)
        return 0;
    for (int i = 0; i < n; i++) {
        pcs |= (uint16_t)(1u << (notes[i] % 12));
        if (notes[i] < notes[lo])
            lo = i;
    }
    for (int pass = 0; pass < 2; pass++)              /* the bass note as the root first, then the others */
        for (int i = 0; i < n; i++) {
            int r = notes[pass ? i : lo] % 12;
            uint16_t rot = (uint16_t)(((pcs >> r) | (pcs << (12 - r))) & 0xFFFu);
            for (uint32_t k = 0; k < sizeof SHAPE / sizeof SHAPE[0]; k++)
                if (rot == SHAPE[k].mask) {
                    s_cat(s_cat(t, NOTE_NAME[r]), SHAPE[k].suf);
                    return 1;
                }
            if (!pass)
                break;
        }
    return 0;
}
/* how many steps a step's note covers (its length, rounded up; the pattern's note length is under a step) */
static int step_cover(const step_t *st) { return st->len ? (st->len + 3) / 4 : 1; }
static void draw_steps(void)
{
    int trk = sel_trk(), drum = is_drum(trk), pt = cur_pat(trk), ph = eng_step[trk];
    const tpat_t *tp = tpat(trk);
    uint32_t h = hash_u(hash_u(2166136261u, 0x57u), (uint32_t)trk | (uint32_t)pt << 4 | (uint32_t)ui.bar << 8 |
                                          (uint32_t)ui.held << 10 | (uint32_t)ui.dsound << 26);
    char t[40];
    h = hash_u(h, (uint32_t)ph | (uint32_t)playing() << 8 | (uint32_t)ui.rec_armed << 9 | (uint32_t)ui.cursor << 16 |
                      (uint32_t)ui.lockpg << 24 | (uint32_t)(seq.rec_wait != 0) << 30);
    h = hash_u(h, ui.cstep | (uint32_t)ui.rem_fx << 16 | (uint32_t)ui.rem_seg << 24 | (uint32_t)(ui.frame / 8 & 1u) << 31);
    h = hash_pat(h, tp);
    h = hash_s(hash_u(h, proj.trk[trk].par[TP_ENGINE]), proj.trk[trk].name);   /* the sound's name, shown */
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
    if (!is_aux(trk)) {                              /* an instrument: its sound first, then what a step takes */
        char nm[NAME_LEN + 1];
        int32_t x;
        for (int i = 0; i < NAME_LEN; i++)
            nm[i] = proj.trk[trk].name[i];
        nm[NAME_LEN] = 0;
        x = text_fit(ST_X + 2, 0, &FONT_XS, nm[0] ? nm : EG_NAME[proj.trk[trk].par[TP_ENGINE] % NENGINE], 96, K_TEXT);
        if (drum)
            s_cat(t, DL_SOUND_NAME[ui.dsound % DL_NSOUND]);
        else {
            char *e = t;
            *e = 0;
            for (int i = 0; i < ui.nrem && i < SN; i++) {
                if (i)
                    e = s_cat(e, " ");
                e = note_name(ui.rem[i], e);
            }
        }
        text_fit(x + 8, 0, &FONT_XS, t, 168 - (x + 8), K_DIM);
        t[0] = 0;
    } else if (trk == AUX_PUNCH) {
        char *e = s_cat(t, "fx: ");
        if (ui.rem_fx && PX_VALID(ui.rem_fx - 1))
            s_cat(s_cat(e, PUNCH_NAME[(ui.rem_fx - 1) & 15]), ui.rem_fx - 1 >= PX_MEL ? " (melodic)" : " (drums)");
        else
            s_cat(e, "play a key on EDIT first");
    } else if (trk == AUX_TAPE) {
        if (ui.rem_seg)
            num_s((uint32_t)ui.rem_seg, s_cat(t, "segment: "));
        else
            s_cat(t, "segment: play a key on EDIT first");
    } else {
        char *e = s_cat(t, "chord: ");
        for (int i = 0; i < ui.nrem && i < SN; i++) {
            if (i)
                e = s_cat(e, " ");
            e = note_name(ui.rem[i], e);
        }
    }
    if (t[0])
        cv_text(ST_X + 2, 0, &FONT_XS, t, K_DIM);
    {
        int nb = (tp->len + 15) / 16, pb = ph / 16;
        for (int b = 0; b < NBAR; b++) {
            int32_t x = 176 + b * 14;
            uint16_t c = b < nb ? K_DIM : K_LINE;
            cv_round(x - 1, 0, 12, 15, 4, b == pb && playing() ? K_NOW : c);
            if (b == ui.bar)
                cv_frame(x - 1, 0, 12, 15, 4, K_SEL, b == pb && playing() ? K_NOW : c);
            num_s((uint32_t)b + 1u, t);
            text_c(x + 5, cap_y(0, 15, &FONT_XS), &FONT_XS, t, b == ui.bar || (b == pb && playing()) ? K_BG : K_TEXT);
        }
    }
    for (int i = 0; i < 16; i++) {
        int s = ui.bar * 16 + i, in = s < tp->len, held = (ui.held | ui.cstep) >> i & 1u, on;
        const step_t *st = &tp->st[s];
        int32_t x = ST_X + (i % 8) * ST_CW, y = ST_Y + (i / 8) * (ST_CH + 4);
        uint16_t bg, fg;
        on = step_on(st, trk);
        bg = !in ? K_BG : on ? K_TINT : K_PANEL;
        if (playing() && s == ph && in)
            bg = on ? K_NOW : K_NOW_T;
        fg = bg == K_NOW ? K_BG : K_TEXT;
        cv_round(x, y, ST_CW - 2, ST_CH, 4, bg);
        if (!in)
            cv_frame(x, y, ST_CW - 2, ST_CH, 4, K_LINE, K_BG);
        if (held)
            cv_frame(x, y, ST_CW - 2, ST_CH, 4, K_SEL, bg), cv_frame(x + 1, y + 1, ST_CW - 4, ST_CH - 2, 3, K_SEL, bg);
        if ((ui.rec_armed && s == ui.cursor) || (seq.rec_wait && s == 0 && (ui.frame / 8 & 1u)))
            cv_frame(x, y, ST_CW - 2, ST_CH, 4, K_REC, bg);
        num_s((uint32_t)s + 1u, t);
        cv_text(x + 4, y + 1, &FONT_XS, t, bg == K_NOW ? K_BG : K_DIM);
        if (drum && !step_empty(st)) {               /* every sound on the step: a tick each, by key, the shown one bright */
            uint16_t have = 0;
            int nt = 0;
            for (int k = 0; k < SN; k++)
                if (st->note[k] >= 1 && st->note[k] <= DL_NSOUND)
                    have |= (uint16_t)(1u << (st->note[k] - 1));
            for (int b = 0; b < DL_NSOUND; b++)
                nt += have >> b & 1u;
            for (int b = 0, j = 0; b < DL_NSOUND; b++)
                if (have >> b & 1u) {
                    int32_t tx = x + (ST_CW - 2) / 2 - (nt * 5 - 1) / 2 + j++ * 5;
                    uint16_t tc = b == ui.dsound ? (bg == K_NOW ? K_BG : K_TEXT) : (bg == K_NOW ? K_PANEL2 : K_NOW_M);
                    cv_round(tx, y + 30, 4, 7, 1, tc);
                }
            if (on) {
                s_cat(t, DL_SOUND_NAME[ui.dsound % DL_NSOUND]);
                t[3] = 0;
                text_c(x + (ST_CW - 2) / 2, y + 12, &FONT_XS, t, fg);
            }
        } else if (on) {
            if (trk == AUX_PUNCH) {
                int fx = st->note[0] - 1;
                text_fit(x + 2, y + 11, &FONT_XS, PX_VALID(fx) ? PUNCH_NAME[fx & 15] : "?", ST_CW - 5, fg);
                text_fit(x + 2, y + 23, &FONT_XS, PX_VALID(fx) && fx >= PX_MEL ? "mel" : "drum", ST_CW - 5, bg == K_NOW ? K_BG : K_DIM);
            } else if (trk == AUX_TAPE) {
                num_s((uint32_t)st->note[0], s_cat(t, "seg "));
                text_fit(x + 2, y + 15, &FONT_XS, t, ST_CW - 5, fg);
            } else {
                char l1[12], *e = l1, cn[12];
                int n = 0, line = 0, nn = 0;
                for (int k = 0; k < SN && st->note[k]; k++)
                    nn++;
                cn[0] = 0;
                if (chord_name(st->note, nn, cn)) {  /* a chord: its name, the bass note under it */
                    int lo = 0;
                    for (int k = 1; k < nn; k++)
                        if (st->note[k] < st->note[lo])
                            lo = k;
                    text_c(x + (ST_CW - 2) / 2, y + 11, text_w(&FONT_B, cn) <= ST_CW - 4 ? &FONT_B : &FONT_XS, cn, fg);
                    note_name(st->note[lo], l1);
                    text_c(x + (ST_CW - 2) / 2, y + 26, &FONT_XS, l1, bg == K_NOW ? K_BG : K_DIM);
                    nn = 0;
                }
                {                                    /* two notes a line when they fit the cell, else one; two lines */
                    char two[12], *t2;
                    int per = 2;
                    t2 = note_name(st->note[0], two);
                    if (nn > 1) {
                        note_name(st->note[1], s_cat(t2, " "));
                        if (text_w(&FONT_XS, two) > ST_CW - 8)
                            per = 1;
                    }
                    for (int k = 0; k < nn && line < 2; k++) {
                        e = note_name(st->note[k], e);
                        n++;
                        if (n == per || k == nn - 1) {
                            if (line == 1 && k < nn - 1)
                                s_cat(e, "+");       /* more notes than show */
                            text_c(x + (ST_CW - 2) / 2, y + (line ? 25 : 13), &FONT_XS, l1, fg);
                            e = l1;
                            n = 0;
                            line++;
                        } else
                            e = s_cat(e, " ");
                    }
                }
            }
        }
        if (st->comp || st->comp2) {                 /* the components' letters, bottom right */
            char g[4] = {0};
            char *e = g;
            if (st->comp)
                e = s_cat(e, COMP_GLYPH[st->comp % NSC]);
            if (st->comp2)
                s_cat(e, COMP_GLYPH[st->comp2 % NSC]);
            text_r(x + ST_CW - 6, y + ST_CH - 17, &FONT_XS, g, fg);
        }
        if (st->cond)
            cv_text(x + 4, y + ST_CH - 17, &FONT_XS, "%", bg == K_NOW ? K_BG : K_DIM);
        if (step_has_lock(pats.lane, trk, pt, s))
            cv_round(x + ST_CW - 9, y + 3, 4, 4, 2, K_REC);
    }
    for (int i = 0; i < 16; i++) {                   /* lengths: a bar from a step across the steps its note covers */
        const step_t *st = &tp->st[ui.bar * 16 + i];
        int c = step_cover(st), last;
        if (step_empty(st) || c < 2 || ui.bar * 16 + i >= tp->len)
            continue;
        last = i + c - 1;
        for (int j = i; j <= last && j < 16; j++) {
            int32_t x = ST_X + (j % 8) * ST_CW, y = ST_Y + (j / 8) * (ST_CH + 4) + ST_CH - 4;
            int32_t x0 = j == i ? x + 3 : x - 2, x1 = j == last || j == 15 || j % 8 == 7 ? x + ST_CW - 5 : x + ST_CW;
            cv_rect(x0, y - 1, x1 - x0, 3, K_DIM);
        }
        if (last > 15) {                             /* it goes on past the bar: an arrow at its end */
            int32_t x = ST_X + 7 * ST_CW + ST_CW - 5, y = ST_Y + (ST_CH + 4) + ST_CH - 6;
            for (int k = 0; k < 3; k++)
                cv_rect(x + k, y + k, 1, 6 - 2 * k, K_TEXT);
        }
    }
    cv_blit(0, MAIN_Y);
}

static void draw_bar(void)
{
    int trk = sel_trk();
    const tpat_t *tp = tpat(trk);
    uint32_t h = hash_u(hash_u(2166136261u, 0xBAu), (uint32_t)trk | (uint32_t)cur_pat(trk) << 4 | (uint32_t)tp->len << 8 |
                                          (uint32_t)tp->scale << 16 | (uint32_t)eng_step[trk] << 20 | (uint32_t)playing() << 28);
    char t[48], *e;
    h = hash_u(h, (uint32_t)(tp->groove + 100) | (uint32_t)proj.groove << 8 | (uint32_t)tp->quant << 16);
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
                cv_round(x + 2 + 46 * (ph % 16) / 16, 36, 4, 36, 2, K_NOW);
            num_s((uint32_t)b + 1u, t);
            text_c(x + w / 2, 46, &FONT_B, t, b < nb ? K_TEXT : K_FAINT);
        }
    }
    e = s_cat(s_cat(t, "Groove "), GR_NAME[proj.groove % NGROOVE]);
    e = s_cat(e, " ");
    if (tp->groove > 0)
        e = s_cat(e, "+");
    e = num_i(tp->groove, e);
    e = num_s((uint32_t)tp->quant, s_cat(e, ", quantise "));
    s_cat(e, "%");
    cv_text(12, 86, &FONT_XS, t, K_DIM);
    cv_text(12, 100, &FONT_XS, "black keys: scale 1 2 3 4 5 6 8 16 1/2", K_DIM);
    cv_blit(0, MAIN_Y);
}

/* the engines' pictures */
/* the engines' pictures: each follows its four knobs (p[TP_E1..E4], 0..127), redrawn as they turn */
static float weft_pic_v(float ph, float d, float kd, float kd2, float mo, float r, int reso)   /* synth.c weft_wave */
{
    float qs = ph < d ? ph * kd : 0.5f + (ph - d) * kd2;
    float qp = ph < 0.5f ? fm_minf(ph * kd, 0.5f) : 0.5f + fm_minf((ph - 0.5f) * kd, 0.5f);
    if (!reso)
        return fm_sin_turns(qs + (qp - qs) * mo + 0.25f);
    {
        float pl = fm_sin_turns(qp + 0.25f), rs = (1.0f - ph) * fm_sin_turns(r * qs);
        return pl + (rs - pl) * mo;
    }
}
static void pic_weft(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    /* two cycles of the wave over its bent phase (the faint ramp): bend moves the bend point, wave turns
     * saw into pulse into the resonant sine under its falling window, sweep packs that sine's cycles in,
     * spread draws the detuned copy drifting beside it */
    float b = (float)p[TP_E1] * (1.0f / 127.0f), wv = (float)p[TP_E2] * (1.0f / 127.0f);
    float r = 1.0f + 15.0f * (float)p[TP_E3] * (1.0f / 127.0f), d = 0.5f * fm_exp2f(-4.64f * b);
    float kd = 0.5f / d, kd2 = 0.5f / (1.0f - d), mo = wv < 0.5f ? wv * 2.0f : wv * 2.0f - 1.0f;
    int reso = wv >= 0.5f, sp = p[TP_E4];
    int32_t per = w / 2, cy = y0 + h / 2, amp = h / 2 - 8, py = -1;
    for (int32_t x = 0; x < w; x++) {                /* the bent phase, a faint ramp each cycle */
        float ph = (float)(x % per) / (float)per;
        float qs = ph < d ? ph * kd : 0.5f + (ph - d) * kd2;
        float qp = ph < 0.5f ? fm_minf(ph * kd, 0.5f) : 0.5f + fm_minf((ph - 0.5f) * kd, 0.5f);
        float q = reso ? qp + (qs - qp) * mo : qs + (qp - qs) * mo;
        int32_t y = y0 + h - 4 - (int32_t)(q * (float)(h - 8));
        if (py >= 0 && x % per)
            cv_line(x0 + x - 1, py, x0 + x, y, K_LINE);
        py = y;
    }
    for (int line = sp > 8 ? 1 : 0; line >= 0; line--) {   /* the copy first, the wave on top */
        int32_t off = line ? (int32_t)((f * (1u + (uint32_t)sp / 24u)) % (uint32_t)per) : 0;
        py = -1;
        for (int32_t x = 0; x < w; x++) {
            float ph = (float)((x + off) % per) / (float)per;
            int32_t y = cy - (int32_t)(weft_pic_v(ph, d, kd, kd2, mo, r, reso) * (float)amp);
            if (py >= 0)
                cv_line(x0 + x - 1, py, x0 + x, y, line ? K_DIM : K_NOW);
            py = y;
        }
    }
}
static void pic_stack(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    /* two oscillators stacked: wave bends the first from sine to saw; ratio: the second's cycles over it
     * (1..8); detune: the second slides against the first; spread: the two drawn apart */
    float gap = 6.0f + p[TP_E4] * 18.0f / 127.0f, amp = ((float)h - gap) / 4.0f - 2.0f;
    float ratio = (float)(p[TP_E2] < 1 ? 1 : p[TP_E2] > 8 ? 8 : p[TP_E2]);
    float cy0 = (float)y0 + (float)h / 2.0f - gap / 2.0f - amp, cy1 = (float)y0 + (float)h / 2.0f + gap / 2.0f + amp;
    float slide = p[TP_E3] ? (float)f * (1.0f + (float)(p[TP_E3] / 16)) / 64.0f : 0.0f;
    cv_rect(x0, y0 + h / 2, w, 1, K_LINE);
    for (int osc = 0; osc < 2; osc++) {
        pen_t pn = {0, 0, 0};
        for (int32_t x = 0; x <= w; x++) {
            float ph = (float)x / (float)w * 2.0f * (osc ? ratio : 1.0f) + (osc ? slide : 0.0f), v;
            float fr = ph - fm_floorf(ph), sn = fm_sin_turns(ph), saw = 1.0f - 2.0f * fr;
            v = osc ? sn : (sn * (127 - p[TP_E1]) + saw * p[TP_E1]) / 127.0f;
            pen_to(&pn, (float)(x0 + x), (osc ? cy1 : cy0) - v * amp, osc ? K_DIM : K_NOW);
        }
    }
}
static void pic_wave(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    /* table: the harmonics; scan: their mix; bend: the phase bent; ripple: a second cycle sliding */
    float cy = (float)(y0 + h / 2), amp = (float)(h / 2 - 8), harm = (float)(1 + p[TP_E1] * 7 / 127);
    pen_t pn = {0, 0, 0};
    (void)f;
    cv_rect(x0, y0 + h / 2, w, 1, K_LINE);
    for (int32_t x = 0; x <= w; x++) {
        float ph = (float)x / (float)w * 2.0f, bend = ph + fm_sin_turns(ph) * (float)p[TP_E3] / 127.0f / 6.0f;
        float a = fm_sin_turns(bend), b = fm_sin_turns(bend * harm + (float)p[TP_E4] / 256.0f);
        pen_to(&pn, (float)(x0 + x), cy - (a * (127 - p[TP_E2]) + b * p[TP_E2]) / 127.0f * amp, K_NOW);
    }
}
static void pic_orbit(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    /* bright: the ring's size; ratio: the modulator's orbit; edge: its dots; wobble: its speed */
    float cx = (float)(x0 + w / 2), cy = (float)(y0 + h / 2), r1 = 14.0f + p[TP_E1] * (float)(h / 2 - 20) / 127.0f;
    float r2 = 4.0f + p[TP_E2] * 16.0f / 127.0f, k = (float)f * (1.0f + (float)(p[TP_E4] / 32)) / 64.0f;
    uint32_t dots = 4u + p[TP_E3] / 10u;
    pen_t ring = {0, 0, 0};
    for (int i = 0; i <= 64; i++) {
        float t = (float)i / 64.0f;
        pen_to(&ring, cx + fm_sin_turns(t + 0.25f) * r1, cy + fm_sin_turns(t) * r1, K_DIM);
    }
    {
        float mx = cx + fm_sin_turns(k + 0.25f) * r1, my = cy + fm_sin_turns(k) * r1;
        cv_linef(cx, cy, mx, my, K_DIM, 1.2f);
        for (uint32_t j = 0; j < dots; j++) {
            float t = (float)j / (float)dots;
            cv_dotf(mx + fm_sin_turns(t + 0.25f) * r2, my + fm_sin_turns(t) * r2, 1.3f, K_NOW);
        }
    }
}
static void pic_tines(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    /* bright: the bar's brightness; grit: specks on it; bite: how far the keys sink; bell: the tine's place */
    int32_t kw = w / 8, sink = p[TP_E3] * 10 / 127;
    uint32_t r = 5u;
    (void)f;
    cv_round(x0 + 4, y0 + 6, w - 8, 10, 3, p[TP_E1] > 64 ? K_NOW_M : K_DIM);
    for (int i = 0; i < p[TP_E2] / 4; i++) {
        r = r * 1664525u + 1013904223u;
        cv_rect(x0 + 6 + (int32_t)((r >> 8) % (uint32_t)(w - 12)), y0 + 7 + (int32_t)((r >> 20) % 8u), 1, 1, K_BG);
    }
    cv_round(x0 + 4 + p[TP_E4] * (w - 20) / 127, y0 + 4, 12, 14, 4, K_NOW);
    for (int k = 0; k < 8; k++) {
        int black = k % 7 == 1 || k % 7 == 3 || k % 7 == 6;
        cv_round(x0 + k * kw + 1, y0 + 24 + (k == 3 ? sink : 0), kw - 2, h - 28 - (k == 3 ? sink : 0), 2, black ? K_TEXT : K_PANEL2);
    }
}
static void pic_organ(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    (void)f;
    for (int k = 0; k < 4; k++) {                    /* four drawbars: stops, pedal, rotor, rate */
        int32_t x = x0 + 20 + k * (w - 40) / 3 - 6, v = p[TP_E1 + k] * (h - 20) / 127;
        cv_round(x, y0 + 4, 12, h - 8, 3, K_PANEL2);
        cv_round(x, y0 + h - 4 - v - 8, 12, v + 8, 3, k ? K_DIM : K_NOW);
    }
}
static void pic_sync(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    /* reach: the reset's period; sub: a square under it; hiss: grain; thin: the line lifted */
    int32_t base = y0 + h - 6 - p[TP_E4] * 12 / 127, period = 10 + (127 - p[TP_E1]) * 50 / 127, py = base, top = h - 24;
    uint32_t r = 9u;
    (void)f;
    for (int32_t x = 0; x < w; x++) {
        int32_t y = base - (x % period) * top / period;
        r = r * 1664525u + 1013904223u;
        y += (int32_t)((r >> 24) % 9u) * p[TP_E3] / 127 - 4 * p[TP_E3] / 127;
        cv_line(x0 + x, py, x0 + x, y, x % period == 0 ? K_DIM : K_NOW);
        py = y;
        if (p[TP_E2] > 8)
            cv_rect(x0 + x, (x / 40) & 1 ? base - p[TP_E2] * 12 / 127 : base, 1, 1, K_DIM);
    }
}
static void pic_swarm(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    /* swarm: how many; ring: squashed tall; tone: smeared wide; spread: two clouds apart */
    uint32_t r = 0x9E3779B9u ^ (f / 3);
    int32_t sep = p[TP_E4] * 40 / 127, rx = w / 4 + p[TP_E3] * (w / 4) / 127, ry = h / 2 - p[TP_E2] * (h / 4) / 127;
    for (int k = 0; k < 30 + p[TP_E1] / 2; k++) {
        int32_t cx = x0 + w / 2 + (k & 1 ? sep : -sep) / 2, dx, dy;
        r = r * 1664525u + 1013904223u;
        dx = (int32_t)((r >> 8) % (uint32_t)(2 * rx)) - rx;
        dy = (int32_t)((r >> 20) % (uint32_t)(2 * ry)) - ry;
        if (dx * dx * ry * ry + dy * dy * rx * rx < rx * rx * ry * ry)
            cv_rect(cx + dx, y0 + h / 2 + dy, 2, 2, (r >> 4 & 3u) ? K_DIM : K_NOW);
    }
}
/* the drumline: eleven pads, each key's model, the shown one ringed, hits flashing */
static void pic_drumline(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    int32_t pw = w / 6, ph = h / 2;
    (void)f;
    for (int k = 0; k < DL_NSOUND; k++) {
        int32_t x = x0 + (k % 6) * pw + 2, y = y0 + (k / 6) * ph + 2, hit = eng_drum_hit[k];
        static const char *const PAD[NDM] = {"kick", "snare", "hat", "clap", "shakr", "tamb", "strng", "conga", "tom", "fm"};
        const char *nm = PAD[p[TP_DRUM(k, DP_MODEL)] % NDM];
        char t[4];
        cv_round(x, y, pw - 4, ph - 4, 4, hit > 40 ? K_NOW : k == ui.dsound ? K_PANEL2 : K_PANEL);
        if (k == ui.dsound)
            cv_frame(x, y, pw - 4, ph - 4, 4, K_SEL, hit > 40 ? K_NOW : K_PANEL2);
        {
            int32_t tw = text_w(&FONT_XS, nm);
            text_fit(tw <= pw - 9 ? x + (pw - 4 - tw) / 2 : x + 3, y + (ph - 4) / 2 - 6, &FONT_XS, nm, pw - 9,
                     hit > 40 ? K_BG : K_TEXT);
        }
        num_s((uint32_t)k + 1u, t);
        cv_text(x + 3, y, &FONT_XS, t, hit > 40 ? K_BG : K_FAINT);
    }
}
static void draw_engine(void)
{
    int trk = sel_trk(), eg = proj.trk[trk].par[TP_ENGINE] % NENGINE;
    const uint8_t *p = proj.trk[trk].par;
    uint32_t h = hash_u(hash_u(2166136261u, 0xE0u), (uint32_t)trk | (uint32_t)eg << 4 | (uint32_t)ui.dsound << 8 |
                                          (ui.frame / 2) << 12);
    for (int i = 0; i < 4; i++)
        h = hash_u(h, p[TP_E1 + i]);
    if (eg == EG_DRUMLINE)
        for (int k = 0; k < DL_NSOUND; k++)
            h = hash_u(h, p[TP_DRUM(k, DP_MODEL)] | (uint32_t)eng_drum_hit[k] << 8);
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
    switch (eg) {
    case EG_WEFT: pic_weft(20, 24, 200, 88, p, ui.frame); break;
    case EG_STACK: pic_stack(20, 24, 200, 88, p, ui.frame); break;
    case EG_WAVETABLE: pic_wave(20, 24, 200, 88, p, ui.frame); break;
    case EG_ORBIT: pic_orbit(20, 24, 200, 88, p, ui.frame); break;
    case EG_TINES: pic_tines(20, 24, 200, 88, p, ui.frame); break;
    case EG_ORGAN: pic_organ(20, 24, 200, 88, p, ui.frame); break;
    case EG_SYNC: pic_sync(20, 24, 200, 88, p, ui.frame); break;
    case EG_SWARM: pic_swarm(20, 24, 200, 88, p, ui.frame); break;
    default: pic_drumline(10, 26, 220, 90, p, ui.frame); break;
    }
    cv_blit(0, MAIN_Y);
}

/* the aux tracks' pages */
static void draw_brain(void)
{
    const uint8_t *p = proj.trk[AUX_BRAIN].par;
    int key = seq.brain_key % 12, sc = seq.brain_scale % NBS, deg = (int8_t)seq.brain_deg, routing = ui.page != P_ENGINE;
    uint32_t h = hash_u(hash_u(2166136261u, 0xB0u), (uint32_t)key | (uint32_t)sc << 4 | (uint32_t)(deg + 64) << 8 |
                                          (uint32_t)seq.brain_auto_ok << 16 | (uint32_t)p[TP_E1] << 17 | (uint32_t)routing << 18 |
                                          (uint32_t)p[TP_E4] << 20);
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
    if (routing) {
        cv_text(12, 44, &FONT_XS, "routing: black keys 1 to 8 toggle a track", K_DIM);
    } else {
        e = s_cat(t, "degree ");
        if (deg > 0)
            e = s_cat(e, "+");
        num_i(deg, e);
        cv_text(12, 44, &FONT_B, t, deg ? K_NOW : K_DIM);
        if (p[TP_E4])
            text_r(230, 48, &FONT_XS, LINK_NAME[p[TP_E4] % 9], K_DIM), text_r(230 - text_w(&FONT_XS, LINK_NAME[p[TP_E4] % 9]) - 4, 48, &FONT_XS, "link", K_FAINT);
    }
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
    uint32_t h = hash_u(hash_u(2166136261u, 0xD0u), dm ^ (mm << 12) ^ (ui.keys << 3) ^ (ui.punch_latched << 7));
    char t[64], *e;
    int n = 0;
    if (!begin_main(h))
        return;
    e = t;
    *t = 0;
    for (int fx = 0; fx < NPUNCH_FX; fx++) {
        int on = fx < PX_MEL ? (dm >> fx & 1u) : (mm >> (fx - PX_MEL) & 1u);
        if (!on || !PX_VALID(fx))
            continue;
        if (n++)
            e = s_cat(e, ", ");
        if (s_len(t) < 40)
            e = s_cat(e, PUNCH_NAME[fx & 15]);
    }
    if (!n)
        s_cat(t, "hold a key: mute, stutter, echo, build...");
    text_fit(10, 4, n ? &FONT_B : &FONT_S, t, 220, n ? K_NOW : K_DIM);
    cv_text(10, 24, &FONT_XS, "drums", K_DIM);
    text_r(230, 24, &FONT_XS, "melodic", K_DIM);
    for (int k = 0; k < 16; k++) {                   /* the keys: two octaves, the ones in effect lit */
        int32_t x = 8 + k * 14;
        int fx = punch_of_key(WHITE_K[k]);
        int on = fx >= 0 && (fx < PX_MEL ? (dm >> fx & 1u) : (mm >> (fx - PX_MEL) & 1u));
        cv_round(x, 40, 13, 60, 2, on ? K_NOW : fx < 0 ? K_PANEL : K_PANEL2);
        if (fx >= 0 && k < 7) {                      /* the drum octave's initials: m s r e b f j */
            char ini[2] = {PUNCH_NAME[fx & 15][0], 0};
            text_c(x + 6, 102, &FONT_XS, ini, K_DIM);
        }
    }
    for (int k = 0; k < 11; k++) {
        int key = BLACK_K[k], fx = punch_of_key(key);
        int on = fx >= 0 && (fx < PX_MEL ? (dm >> fx & 1u) : (mm >> (fx - PX_MEL) & 1u));
        int32_t wi = 0;
        while (WHITE_K[wi] < key)
            wi++;
        cv_frame(8 + wi * 14 - 5, 40, 10, 34, 2, fx < 0 ? K_LINE : K_DIM, on ? K_NOW : K_BG);
    }
    cv_rect(8 + 7 * 14 - 1, 36, 2, 70, K_DIM);
    cv_text(120, 104, &FONT_XS, proj.trk[AUX_PUNCH].par[TP_E1] ? "latch on" : "while held", K_DIM);
    cv_blit(0, MAIN_Y);
}
static void draw_tape(void)
{
    uint32_t h = hash_u(hash_u(2166136261u, 0x7Au), ui.keys ^ (uint32_t)playing() << 30 ^ (ui.frame / 4) << 8);
    int32_t cx = 70, cy = 58, r = 44;
    uint32_t held = 0;
    char t[24];
    for (int k = 0; k < 16; k++)
        if (ui.keys >> WHITE_K[k] & 1u && ui.kmode[WHITE_K[k]] == KM_TAPE)
            held |= 1u << k;
    h = hash_u(h, held);
    if (!begin_main(h))
        return;
    for (int k = 0; k < 16; k++) {
        uint32_t a = (uint32_t)k * 2u;
        int32_t x = cx + cos100(a) * r / 100, y = cy + sin100(a) * r / 100;
        int on = held >> k & 1u;
        cv_round(x - 5, y - 5, 11, 11, 4, on ? K_NOW : K_PANEL2);
    }
    if (playing()) {
        uint32_t a = (ui.frame / 4) & 31u;
        cv_round(cx + cos100(a) * (r - 14) / 100 - 2, cy + sin100(a) * (r - 14) / 100 - 2, 5, 5, 2, K_TEXT);
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
        cv_text(130, 66, &FONT_S, t, K_NOW);
    }
    cv_blit(0, MAIN_Y);
}
static void draw_fxtrack(int trk)                    /* AUX_FX1 / AUX_FX2: the effect and a picture of it */
{
    int type = proj.trk[trk].par[TP_ENGINE] % NFXT;
    uint32_t h = hash_u(hash_u(2166136261u, 0xF0u), (uint32_t)trk | (uint32_t)type << 4 | (ui.frame / 3) << 8 |
                                          (uint32_t)ui.last_inst << 24);
    char t[32];
    if (!begin_main(h))
        return;
    cv_text(12, 4, &FONT_L, FXT_NAME[type], K_TEXT);
    s_cat(s_cat(t, trk == AUX_FX1 ? "send effect I, keys play " : "send effect II, keys play "), TRACK_NAME[inst_trk()]);
    cv_text(12, 40, &FONT_XS, t, K_DIM);
    {
        int32_t x0 = 130, y0 = 60, w = 100;
        uint32_t f = ui.frame;
        switch (type) {
        case FXT_CHORUS: {
            pen_t p0 = {0, 0, 0}, p1 = {0, 0, 0};
            for (int32_t x = 0; x <= w; x++) {
                float t = (float)x / 192.0f + (float)f / 96.0f;
                pen_to(&p1, (float)(x0 + x), (float)(y0 + 20) + 10.0f * fm_sin_turns(t + 0.16f), K_DIM);
                pen_to(&p0, (float)(x0 + x), (float)(y0 + 20) + 10.0f * fm_sin_turns(t), K_NOW);
            }
            break;
        }
        case FXT_DELAY:
            for (int k = 0; k < 5; k++)
                cv_round(x0 + k * 20, y0 + 30 - (30 >> k), 10, 30 >> k, 2, k ? K_DIM : K_NOW);
            break;
        case FXT_DIST: {
            pen_t pn = {0, 0, 0};
            for (int32_t x = 0; x <= w; x++)             /* a sine driven into a soft clip */
                pen_to(&pn, (float)(x0 + x), (float)(y0 + 20) - 16.0f * fm_tanhf(3.0f * fm_sin_turns((float)x / 96.0f)),
                       K_NOW);
            break;
        }
        case FXT_LOFI:
            for (int32_t x = 0; x < w; x += 8)
                cv_rect(x0 + x, y0 + 20 - sin100((uint32_t)(x / 8)) * 16 / 100, 8, 3, K_NOW);
            break;
        case FXT_PHASER:
            for (int k = 0; k < 4; k++) {
                int32_t x = x0 + 10 + k * 24 + sin100(f / 3 + (uint32_t)k * 8) * 6 / 100;
                cv_round(x, y0, 6, 40, 3, k & 1 ? K_DIM : K_NOW);
            }
            break;
        default:
            for (int k = 0; k < 6; k++) {                /* rings spreading out, fainter as they go */
                float rr = 8.0f + (float)k * 7.0f;
                pen_t pn = {0, 0, 0};
                for (int i = 0; i <= 48; i++) {
                    float t = (float)i / 48.0f;
                    pen_to(&pn, (float)(x0 + 50) + rr * fm_sin_turns(t + 0.25f), (float)(y0 + 20) + rr * 0.5f * fm_sin_turns(t),
                           k & 1 ? K_DIM : K_NOW);
                }
            }
            break;
        }
    }
    cv_blit(0, MAIN_Y);
}

static void draw_play(void)
{
    int trk = sel_trk();
    const uint8_t *p = proj.trk[trk].par;
    uint32_t h = hash_u(hash_u(2166136261u, 0x9Au), (uint32_t)trk | (uint32_t)p[TP_VMODE] << 4 | (uint32_t)eng_voices << 24);
    char t[40];
    if (!begin_main(h))
        return;
    cv_text(12, 6, &FONT_B, tp_info_par(TP_VMODE, p)->names[p[TP_VMODE] % 3], K_TEXT);
    cv_text(12, 26, &FONT_XS, p[TP_VMODE] % 3 == VM_POLY ? "up to 6 voices on the track"
                              : p[TP_VMODE] % 3 == VM_MONO ? "one voice, every key restarts it"
                                                            : "one voice, held keys glide", K_DIM);
    for (int v = 0; v < NVOICE; v++)
        cv_round(12 + v * 13, 50, 10, 10, 3, v < eng_voices ? K_NOW : K_LINE);
    num_s((uint32_t)eng_voices, s_cat(t, "voices sounding "));
    cv_text(12, 66, &FONT_XS, t, K_DIM);
    cv_blit(0, MAIN_Y);
}
static void draw_env(int filter)
{
    int trk = sel_trk(), b = filter ? TP_FA : TP_A, drum = is_drum(trk) && !filter;
    const uint8_t *p = proj.trk[trk].par;
    uint32_t h = hash_u(hash_u(2166136261u, 0xE7u), (uint32_t)trk | (uint32_t)filter << 4 | (uint32_t)p[b] << 8 |
                                          (uint32_t)p[b + 1] << 16 | (uint32_t)p[b + 2] << 24);
    float x0 = 16.0f, y0 = 16.0f, w = 208.0f, hh = 84.0f, base = y0 + hh, x = x0, a, d, r, s;
    float px[64], py[64];
    int n = 0;
    pen_t pn = {0, 0, 0};
    h = hash_u(h, p[b + 3] | (uint32_t)drum << 8);
    if (!begin_main(h))
        return;
    cv_rect(16, 16 + 84, 208, 1, K_LINE);
    /* the shape as points: each stage a curve (attack easing in, decay and release falling exponentially) */
    if (drum) {                                      /* lag (a delay), attack, then the hit's length */
        a = 2.0f + p[TP_A] * 40.0f / 127.0f;
        d = 4.0f + p[TP_D] * 40.0f / 127.0f;
        r = 20.0f + p[TP_R] * 100.0f / 127.0f;
        px[n] = x, py[n++] = base;
        x += a;
        px[n] = x, py[n++] = base;
        for (int i = 1; i <= 12; i++) {
            float t = i / 12.0f;
            px[n] = x + d * t, py[n++] = base - hh * (1.0f - (1.0f - t) * (1.0f - t));
        }
        x += d;
        for (int i = 1; i <= 24; i++) {
            float t = i / 24.0f;
            px[n] = x + r * t, py[n++] = base - hh * ease_exp(t);
        }
    } else {
        a = 4.0f + p[b] * 60.0f / 127.0f;
        d = 4.0f + p[b + 1] * 60.0f / 127.0f;
        r = 4.0f + p[b + 3] * 60.0f / 127.0f;
        s = p[b + 2] / 127.0f;
        px[n] = x, py[n++] = base;
        for (int i = 1; i <= 12; i++) {
            float t = i / 12.0f;
            px[n] = x + a * t, py[n++] = base - hh * (1.0f - (1.0f - t) * (1.0f - t));
        }
        x += a;
        for (int i = 1; i <= 16; i++) {
            float t = i / 16.0f;
            px[n] = x + d * t, py[n++] = base - hh * (s + (1.0f - s) * ease_exp(t));
        }
        x += d;
        x = x0 + w - r;                              /* the sustain, then the release */
        px[n] = x, py[n++] = base - hh * s;
        for (int i = 1; i <= 16; i++) {
            float t = i / 16.0f;
            px[n] = x + r * t, py[n++] = base - hh * s * ease_exp(t);
        }
    }
    for (int i = 1; i < n; i++)                      /* under the curve, the track's colour thinned */
        for (int32_t xi = f_floor(px[i - 1] + 0.5f); xi < f_floor(px[i] + 0.5f); xi++) {
            float t = px[i] > px[i - 1] ? (xi - px[i - 1]) / (px[i] - px[i - 1]) : 0.0f;
            int32_t yt = f_floor(py[i - 1] + (py[i] - py[i - 1]) * t + 1.5f);
            if (yt < (int32_t)base)
                cv_rect(xi, yt, 1, (int32_t)base - yt, K_TINT);
        }
    for (int i = 0; i < n; i++)
        pen_to(&pn, px[i], py[i], K_NOW);
    cv_text(16, 16 + 84 + 3, &FONT_XS, drum ? "drum envelope: lag, attack, random, length"
                                              : filter ? "filter envelope" : "amp envelope", K_DIM);
    cv_blit(0, MAIN_Y);
}
static void draw_filter(void)
{
    int trk = sel_trk();
    const uint8_t *p = proj.trk[trk].par;
    int ft = p[TP_FTYPE] % NFT, cut = p[TP_CUT], res = p[TP_RES], dyn = ft == FT_DYNAMIC;
    uint32_t h = hash_u(hash_u(2166136261u, 0xF1u), (uint32_t)trk | (uint32_t)ft << 4 | (uint32_t)cut << 8 | (uint32_t)res << 16 |
                                          (uint32_t)p[TP_KTRK] << 24);
    int32_t x0 = 16, y0 = 12, w = 208, hh = 84, fc = x0 + 10 + cut * (w - 20) / 127, py = -1;
    if (!begin_main(h))
        return;
    cv_rect(x0, y0 + hh, w, 1, K_LINE);
    if (dyn) {                                       /* low cut .. hi cut: the band that passes */
        int32_t lo = x0 + p[TP_CUT] * (w - 20) / 127, hi = x0 + 20 + p[TP_KTRK] * (w - 20) / 127;
        cv_round(lo, y0 + 10, hi > lo ? hi - lo : 2, hh - 10, 4, K_NOW_T);
        cv_rect(lo, y0 + 10, 2, hh - 10, K_NOW);
        cv_rect(hi, y0 + 10, 2, hh - 10, K_NOW);
        cv_text(x0, y0 + hh + 3, &FONT_XS, "dynamic: low cut, compression, drive, hi cut", K_DIM);
        cv_blit(0, MAIN_Y);
        return;
    }
    {
        /* the response: a resonant two-pole section (squared for the 24 dB types) on a log frequency axis, an
         * octave every 22 px, in dB from +12 at the top to -48 at the bottom */
        float q = 0.7f + (float)res * 6.0f / 127.0f, ys[209];
        int steep = ft == FT_LP24 || ft == FT_LADDER, hp = ft == FT_HP12, bp = ft == FT_BP12;
        pen_t pn = {0, 0, 0};
        (void)py;
        for (int32_t x = 0; x <= w; x++) {
            float wn = fm_exp2f((float)(x0 + x - fc) / 22.0f), m2, db, y;
            if (hp)
                wn = 1.0f / wn;
            m2 = (1.0f - wn * wn) * (1.0f - wn * wn) + wn * wn / (q * q);   /* 1 / |H|^2 */
            if (bp)                                  /* the band-pass: (w/q)^2 over it, unity at its peak */
                m2 = m2 * q * q / (wn * wn > 1e-6f ? wn * wn : 1e-6f);
            db = -3.0103f * fm_log2f(m2 > 1e-6f ? m2 : 1e-6f) * (steep ? 2.0f : 1.0f);
            y = (float)y0 + 14.0f - db * (float)(hh - 14) / 48.0f;
            ys[x] = y < (float)y0 ? (float)y0 : y > (float)(y0 + hh) ? (float)(y0 + hh) : y;
        }
        for (int32_t x = 0; x < w; x++) {            /* under the curve, the track's colour thinned */
            int32_t yt = f_floor(ys[x] + 1.5f);
            if (yt < y0 + hh)
                cv_rect(x0 + x, yt, 1, y0 + hh - yt, K_TINT);
        }
        for (int32_t x = 0; x <= w; x++)
            pen_to(&pn, (float)(x0 + x), ys[x], K_NOW);
    }
    cv_rect(fc, y0, 1, hh, K_FAINT);
    cv_text(x0, y0 + hh + 3, &FONT_XS, tp_info_par(TP_FTYPE, p)->names[ft], K_DIM);
    text_r(x0 + w, y0 + hh + 3, &FONT_XS, p[TP_KTRK] ? "key tracking on" : "", K_DIM);
    cv_blit(0, MAIN_Y);
}
static void draw_sends(void)                         /* the instrument's three sends, named with the effects on them */
{
    int trk = inst_trk();
    const uint8_t *p = proj.trk[trk].par;
    uint32_t h = hash_u(hash_u(2166136261u, 0x5Eu), (uint32_t)trk | (uint32_t)p[TP_SEND1] << 4 | (uint32_t)p[TP_SEND2] << 12 |
                                          (uint32_t)p[TP_SENDT] << 20 | (uint32_t)p[TP_LEVEL] << 28);
    char t[32];
    h = hash_u(h, proj.trk[AUX_FX1].par[TP_ENGINE] | (uint32_t)proj.trk[AUX_FX2].par[TP_ENGINE] << 8);
    if (!begin_main(h))
        return;
    s_cat(s_cat(t, TRACK_NAME[trk]), " sends to");
    cv_text(12, 4, &FONT_B, t, K_TEXT);
    for (int i = 0; i < 3; i++) {
        int32_t y = 34 + i * 26, v = p[TP_SEND1 + i] * 132 / 127;
        cv_text(12, y - 2, &FONT_S, i == 0 ? "FX I" : i == 1 ? "FX II" : "tape", K_TEXT);
        if (i < 2)
            cv_text(12, y + 11, &FONT_XS, FXT_NAME[proj.trk[AUX_FX1 + i].par[TP_ENGINE] % NFXT], K_DIM);
        cv_round(92, y + 3, 132, 8, 4, K_LINE);
        if (v > 0)
            cv_round(92, y + 3, v < 8 ? 8 : v, 8, 4, K_NOW);
    }
    cv_blit(0, MAIN_Y);
}
static void draw_lfo(void)
{
    int trk = sel_trk();
    const uint8_t *p = proj.trk[trk].par;
    int lt = p[TP_LTYPE] % NLT;
    uint32_t h = hash_u(hash_u(2166136261u, 0x1Fu), (uint32_t)trk | (uint32_t)lt << 4 | (uint32_t)p[TP_LAMT] << 8 |
                                          (lt ? ui.frame / 2 : 0u) << 16);
    int32_t x0 = 16, y0 = 12, w = 208, hh = 84, cy = y0 + hh / 2, amp = 6 + p[TP_LAMT] * (hh / 2 - 8) / 127;
    uint32_t r = 12345u;
    if (!begin_main(h))
        return;
    cv_rect(x0, cy, w, 1, K_LINE);
    if (lt == LT_OFF)
        text_c(120, 40, &FONT_S, "LFO off", K_DIM);
    {
        pen_t pn = {0, 0, 0};
        float scroll = (float)(ui.frame / 2u) * 2.0f;     /* the curve slides left as it runs */
        (void)r;
        for (int32_t x = 0; lt && x <= w; x++) {
            float u = (float)x + scroll, v;              /* v: -1..1 */
            if (lt == LT_WAVE || lt == LT_WOBBLE)
                v = fm_sin_turns(u / 128.0f);
            else if (lt == LT_DRIFT) {                   /* a held random value every 24 px, slewed between */
                uint32_t k = (uint32_t)(u / 24.0f), h0 = (k * 2654435761u) >> 8, h1 = ((k + 1u) * 2654435761u) >> 8;
                float t = (u - (float)k * 24.0f) / 24.0f, s0 = (float)(h0 & 1023u) / 511.5f - 1.0f,
                      s1 = (float)(h1 & 1023u) / 511.5f - 1.0f;
                t = t * t * (3.0f - 2.0f * t);
                v = s0 + (s1 - s0) * t;
            } else {                                     /* pump: down at once, back up exponentially */
                float t = (u - fm_floorf(u / 48.0f) * 48.0f) / 48.0f;
                v = 1.0f - 2.0f * ease_exp(t);
            }
            pen_to(&pn, (float)(x0 + x), (float)cy - v * (float)amp, K_NOW);
        }
    }
    if (lt == LT_WAVE || lt == LT_DRIFT) {
        char t[40], num[12], unit[6], *e = s_cat(t, "to ");
        tp_text_par(TP_LDEST, p[TP_LDEST], p, num, unit);
        e = s_cat(s_cat(e, num), " ");
        tp_text_par(TP_LX, p[TP_LX], p, num, unit);
        s_cat(e, num);
        cv_text(x0, y0 + hh + 3, &FONT_XS, t, K_DIM);
    } else if (lt)
        cv_text(x0, y0 + hh + 3, &FONT_XS, tp_info_par(TP_LTYPE, p)->names[lt], K_DIM);
    cv_blit(0, MAIN_Y);
}
static void draw_players(void)
{
    int trk = sel_trk(), pl = pl_type(trk);
    const player_t *pr = &seq.player[trk < NTRK ? trk : 0];
    uint32_t h = hash_u(hash_u(2166136261u, 0x9Cu), (uint32_t)trk | (uint32_t)pl << 4 | (uint32_t)pr->nheld << 8 |
                                          (uint32_t)pr->nchord << 12 | (uint32_t)pr->arp_note << 16 | (uint32_t)ui.maestro_rec << 24);
    char t[48], *e;
    for (int i = 0; i < 16; i++)
        h = hash_u(h, pr->held[i]);
    for (int i = 0; i < SN * 2; i++)
        h = hash_u(h, pr->chord[i]);
    if (!begin_main(h))
        return;
    cv_text(12, 4, &FONT_B, tp_info_par(TP_PLAYER, proj.trk[trk].par)->names[pl], K_TEXT);
    if (pl == PL_OFF)
        cv_text(12, 24, &FONT_XS, "the keys play as they are", K_DIM);
    else if (pl == PL_ARP)
        cv_text(12, 24, &FONT_XS, "held notes play one after another", K_DIM);
    else if (pl == PL_STRUM)
        cv_text(12, 24, &FONT_XS, ui.maestro_rec ? "hold the chord to record (up to 8)" : "one key plays the chord, moved", K_DIM);
    else
        cv_text(12, 24, &FONT_XS, "notes stay until the next", K_DIM);
    e = s_cat(t, pl == PL_STRUM ? "chord: " : "held: ");
    if (pl == PL_STRUM) {
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
    for (int k = 0; k < 24; k++) {
        int32_t x = 12 + k * 9;
        int n = 48 + k, held = 0;
        for (int i = 0; i < pr->nheld && i < 16; i++)
            held |= pr->held[i] == n;
        cv_round(x, 76, 8, 30, 2, pr->arp_note == n ? K_TEXT : held ? K_NOW_M : (k % 12 == 1 || k % 12 == 3 ||
                                                           k % 12 == 6 || k % 12 == 8 || k % 12 == 10) ? K_DIM : K_PANEL2);
    }
    cv_blit(0, MAIN_Y);
}
static void draw_mix(void)
{
    int sel = sel_trk();
    uint32_t h = hash_u(hash_u(2166136261u, 0x3Cu), (uint32_t)sel | (uint32_t)seq.mute << 4 | (uint32_t)ui.solo << 20);
    static const char *const SHORT_NM[NALLTRK] = {"1", "2", "3", "4", "5", "6", "7", "8", "CO", "TH", "TP", "F1", "F2"};
    for (int t = 0; t < NALLTRK; t++)
        h = hash_u(h, eng_peak[t] >> 9);
    if (!begin_main(h))
        return;
    for (int t = 0; t < NALLTRK; t++) {
        int32_t x = 4 + t * 18, lvl = (int32_t)eng_peak[t] * 80 / 32768;
        int muted = t < NSEQT && (seq.mute >> t & 1u);
        cv_round(x, 6, 14, 84, 3, K_PANEL);
        if (lvl > 0)
            cv_round(x, 90 - lvl, 14, lvl, 3, muted ? K_LINE : trk_ink(t));
        if (t == sel) {
            cv_frame(x - 1, 5, 16, 86, 4, K_SEL, K_PANEL);
            if (lvl > 0)
                cv_round(x + 1, 90 - lvl, 12, lvl - 1, 2, muted ? K_LINE : trk_ink(t));
        }
        text_c(x + 7, 94, &FONT_XS, SHORT_NM[t], t == sel ? K_TEXT : muted ? K_FAINT : K_DIM);
        if (muted)
            cv_rect(x + 2, 108, 10, 2, K_FAINT);
    }
    if (ui.solo)
        text_r(236, 108, &FONT_XS, "solo", K_TEXT);
    cv_blit(0, MAIN_Y);
}
static void draw_master(void)
{
    int pg = ui.page - P_MASTER;
    const uint8_t *m = proj.master;
    uint32_t h = hash_u(hash_u(2166136261u, 0x3Au), (uint32_t)pg);
    int32_t x0 = 16, y0 = 12, w = 208, hh = 84, cy = y0 + hh / 2;
    for (int i = 0; i < NMS; i++)
        h = hash_u(h, m[i]);
    if (!begin_main(h))
        return;
    cv_rect(x0, cy, w, 1, K_LINE);
    if (pg == 0) {
        pen_t pn = {0, 0, 0};
        for (int32_t x = 0; x <= w; x++) {           /* the three bands, eased into each other */
            float u = (float)x / (float)w * 3.0f - 0.5f, g0, g1, t, b;
            int i = u < 0.0f ? 0 : u >= 2.0f ? 2 : (int)u;
            static const uint8_t BAND[3] = {MS_EQ_LO, MS_EQ_MID, MS_EQ_HI};
            t = u - (float)i;
            t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
            t = t * t * (3.0f - 2.0f * t);
            g0 = (float)m[BAND[i]];
            g1 = (float)m[BAND[i < 2 ? i + 1 : 2]];
            b = g0 + (g1 - g0) * t;
            pen_to(&pn, (float)(x0 + x), (float)cy - (b - 64.0f) * (float)(hh / 2 - 6) * (float)m[MS_EQ_BLEND] / (64.0f * 127.0f),
                   K_NOW);
        }
        cv_text(x0, y0 + hh + 3, &FONT_XS, "low  mid  high, by the blend", K_DIM);
    } else if (pg == 1) {
        int32_t py = -1;
        for (int32_t x = 0; x < w; x++) {
            int32_t in = (x - w / 2) * 100 / (w / 2), g = 100 + (int32_t)m[MS_SAT_GAIN] * 3, clip = 30 + (127 - m[MS_SAT_CLIP]) * 70 / 127;
            int32_t out = in * g / 100;
            out = out > clip ? clip : out < -clip ? -clip : out;
            {
                int32_t y = cy - out * (hh / 2 - 4) / 100;
                if (py >= 0)
                    cv_line(x0 + x - 1, py, x0 + x, y, K_NOW);
                py = y;
            }
        }
        cv_text(x0, y0 + hh + 3, &FONT_XS, "drive: gain, clip, tone, blend", K_DIM);
    } else {
        static const char *const NM[4] = {"drums", "melodic", "comp", "level"};
        for (int i = 0; i < 4; i++) {
            int32_t x = x0 + i * 52, v = m[MS_PERC + i] * (hh - 10) / 127;
            cv_round(x + 8, y0, 30, hh, 4, K_PANEL);
            cv_round(x + 8, y0 + hh - v, 30, v, 4, i == 2 ? K_NOW_M : K_NOW);
            text_c(x + 23, y0 + hh + 3, &FONT_XS, NM[i], K_DIM);
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
    uint32_t h = hash_u(2166136261u, 0x5E700u | (uint32_t)proj_slot | (uint32_t)proj.usb_full << 4 | (uint32_t)seq.ext << 8 |
                                          (uint32_t)proj.rsv[0] << 12 | (uint32_t)proj.send_clock << 16 | (uint32_t)proj.send_notes << 17 |
                                          (uint32_t)proj.rsv[1] << 20);
    h = hash_s(h, proj.name);
    if (!begin_main(h))
        return;
    for (int i = 0; i < NAME_LEN; i++)
        nm[i] = proj.name[i];
    nm[NAME_LEN] = 0;
    num_s((uint32_t)proj_slot + 1u, s_cat(t, "slot "));
    setup_row(2, "project", t);
    setup_row(18, "name", nm[0] ? nm : "-");
    setup_row(34, "clock", seq.ext ? "MIDI in" : "internal");
    setup_row(50, "MIDI out", proj.send_clock || proj.send_notes ? (proj.send_clock && proj.send_notes ? "clock, notes"
                                                                  : proj.send_clock ? "clock" : "notes") : "off");
    setup_row(66, "count-in", proj.rsv[0] % 3u == 2u ? "2 bars" : proj.rsv[0] % 3u ? "1 bar" : "off");
    setup_row(82, "theme", THEME_OF_PROJ()->name);
    setup_row(98, "version", XY_VERSION);
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

/* step record on a play page: the cursor's bar as a strip of 16 cells along the main band's foot, so the
 * keys' writes are not blind */
#define RS_H 16
static void draw_rec_strip(void)
{
    int trk = sel_trk(), b = ui.cursor / 16;
    const tpat_t *tp;
    uint32_t h;
    if (!ui.rec_armed || playing() || !is_seqt(trk) || ui.card || lst.open || ui.page < P_ENGINE || ui.page > P_PLAYERS3)
        return;
    tp = tpat(trk);
    h = hash_u(hash_u(2166136261u, 0x55u), (uint32_t)ui.cursor | (uint32_t)trk << 8);
    for (int i = 0; i < 16; i++)
        h = hash_u(h, !step_empty(&tp->st[b * 16 + i]));
    if (h == ui.sig[5])
        return;
    ui.sig[5] = h;
    cv_begin(240, RS_H, K_BG);
    for (int i = 0; i < 16; i++) {
        int st = b * 16 + i;
        int32_t x = 8 + i * 14;
        uint16_t c = st >= tp->len ? K_BG : !step_empty(&tp->st[st]) ? K_PANEL2 : K_PANEL;
        cv_round(x, 3, 12, 11, 2, c);
        if (st == ui.cursor)
            cv_frame(x - 1, 2, 14, 13, 3, K_REC, c);
    }
    cv_blit(0, MAIN_Y + MAIN_H - RS_H);
}
static void ink_follow(void)                         /* another track: its ink, every band drawn again */
{
    int trk = sel_trk();
    theme = THEME_OF_PROJ();
    if (k_th == theme && k_now == trk_ink(trk))
        return;
    if (k_th != theme)                               /* another theme: the whole screen, the gaps between the bands too */
        lcd_fill(0, 0, 240, 240, K_BG);
    k_th = theme;
    k_now = trk_ink(trk);
    k_tint = trk_tint(trk);
    k_mid = ink_mix(k_now, theme->mid);
    for (uint32_t i = 0; i < sizeof ui.sig / sizeof ui.sig[0]; i++)
        ui.sig[i] ^= 0x5A5A5A5Au;
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
    case P_SENDS: draw_sends(); break;
    case P_FX1: draw_fxtrack(AUX_FX1); break;
    case P_FX2: draw_fxtrack(AUX_FX2); break;
    case P_ENGINE: case P_PLAY: case P_ENV: case P_ENV2: case P_FILTER: case P_LFO:
    case P_LFO2: case P_PLAYERS: case P_PLAYERS2: case P_PLAYERS3:
        if (trk == AUX_BRAIN)
            draw_brain();
        else if (trk == AUX_PUNCH)
            draw_punch();
        else if (trk == AUX_TAPE)
            draw_tape();
        else if (is_aux(trk))
            draw_fxtrack(trk);
        else if (ui.page == P_ENGINE || (is_drum(trk) && ui.page == P_PLAY))
            draw_engine();
        else if (ui.page == P_PLAY)
            draw_play();
        else if (ui.page == P_ENV || ui.page == P_ENV2)
            draw_env(ui.page == P_ENV2);
        else if (ui.page == P_FILTER)
            draw_filter();
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
