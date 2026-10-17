/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Small-canvas renderer (no full framebuffer). Draw text/lines into
 * an off-screen strip, then blit it in one DMA transfer. Pixels are stored
 * byte-swapped (the panel takes RGB565 big-endian). */
typedef struct {               /* proportional, see tools/gen_font.py */
    uint8_t h;
    uint8_t pad;               /* bitmap starts this many pixels left of the pen */
    uint8_t first, last;
    uint8_t rle;               /* 1: the nibbles code runs of 0 and 15 (gen_font.py) */
    const uint8_t *adv;        /* advance per glyph */
    const uint8_t (*box)[4];   /* ink box per glyph: x (from FONT_PAD left of the pen), y, w, h */
    const uint16_t *off;       /* byte offset of each glyph's nibbles */
    const uint8_t *data;
} felucca_font_t;
#include "felucca_font.h"

#define CV_MAX (240u * 120u)      /* LOOM-8: the tallest band, the main one, is 240 x 120 */
static uint16_t cv_px[CV_MAX] __attribute__((section(".pool")));
static uint32_t cv_w, cv_h;
static int32_t cv_oy;            /* y offset for graph drawing */
static int32_t cv_y0, cv_y1;     /* X0X: the rows drawing may touch (cv_band) */

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define C_BLACK 0x0000u
#define C_WHITE 0xFFFFu              /* accent only: what is being touched / where we are */
/* The screen's five steps, darkest to brightest, plus white; picked as THEME in GLOBAL. */
typedef struct {
    const char *name;
    uint16_t c[5];
} palette_t;
/* X0X: the first three steps are neutral greys in every theme (structure: rules, empty cells,
 * labels); only the two brightest carry the theme's hue (values, highlights). Colour on the
 * screen then means something: a part, a value, an alarm. */
#define NEUTRALS RGB(34, 34, 40), RGB(74, 74, 82), RGB(150, 150, 158)
static const palette_t PALETTES[] = {
    {"GREEN", {NEUTRALS, RGB(80, 220, 120), RGB(150, 255, 175)}},
    {"AMBER", {NEUTRALS, RGB(232, 136, 30), RGB(255, 184, 70)}},
    {"CYAN", {NEUTRALS, RGB(60, 180, 235), RGB(150, 225, 255)}},
    {"RED", {NEUTRALS, RGB(232, 76, 60), RGB(255, 132, 112)}},
    {"MONO", {NEUTRALS, RGB(196, 196, 202), RGB(236, 236, 240)}},
};
#define NPALETTES (sizeof(PALETTES) / sizeof(PALETTES[0]))
static uint16_t pal[5];
#define C_LINE pal[0]                /* 1 rules, separators */
#define C_DIM pal[1]                 /* 2 inactive, empty steps, units */
#define C_GRAY pal[2]                /* 3 labels */
#define C_AMB pal[3]                 /* 4 secondary text */
#define C_HI pal[4]                  /* 5 values, curves */

static void palette_set(uint32_t i)
{
    uint32_t k;
    for (k = 0; k < 5u; k++)
        pal[k] = PALETTES[i % NPALETTES].c[k];
}

static inline uint16_t swap16(uint32_t c) { return (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8)); }

#ifdef GFX_AUDIT
/* host only: the layout audit. Within one canvas, a label's ink must stay inside the canvas (and its band),
 * must not touch another label's ink, and must not cross the edge of a rounded box (wholly in or wholly out).
 * Findings print once each as "LAYOUT ..." with where the app is (gfx_audit_where, the host's). */
static const char *gfx_audit_where(void);
typedef struct { int32_t x0, y0, x1, y1; char s[24]; } ga_box_t;
static ga_box_t ga_txt[96], ga_rnd[160];
static uint32_t ga_ntxt, ga_nrnd, ga_seen[512], ga_nseen, ga_findings;
static void ga_reset(void) { ga_ntxt = ga_nrnd = 0; }
static void ga_report(const char *what, const char *a, const char *b, int32_t x, int32_t y)
{
    uint32_t h = 2166136261u;
    const char *w = gfx_audit_where(), *q;
    for (q = what; *q; q++) h = (h ^ (uint8_t)*q) * 16777619u;
    for (q = a; *q; q++) h = (h ^ (uint8_t)*q) * 16777619u;
    for (q = b; *q; q++) h = (h ^ (uint8_t)*q) * 16777619u;
    for (q = w; *q; q++) h = (h ^ (uint8_t)*q) * 16777619u;
    for (uint32_t i = 0; i < ga_nseen; i++)
        if (ga_seen[i] == h)
            return;
    if (ga_nseen < 512)
        ga_seen[ga_nseen++] = h;
    ga_findings++;
    printf("LAYOUT %s: \"%s\"%s%s%s at %d,%d (%s)\n", what, a, *b ? " / \"" : "", b, *b ? "\"" : "", (int)x, (int)y, w);
}
static void ga_text(const char *s, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int clipped)
{
    ga_box_t *t;
    uint32_t i;
    if (x1 < x0)
        return;                                      /* no ink (spaces) */
    if (clipped)
        ga_report("CLIPPED", s, "", x0, y0);
    else if (cv_w == 240u && (x0 < 2 || x1 > 237))   /* a full-width band: ink right at the glass's edge */
        ga_report("AT THE SCREEN EDGE", s, "", x0, y0);
    for (i = 0; i < ga_ntxt; i++) {
        ga_box_t *o = &ga_txt[i];
        if (x0 <= o->x1 && o->x0 <= x1 && y0 <= o->y1 && o->y0 <= y1)
            ga_report("OVERLAP", s, o->s, x0, y0);
    }
    for (i = 0; i < ga_nrnd; i++) {
        ga_box_t *r = &ga_rnd[i];
        int inside = x0 >= r->x0 && x1 <= r->x1 && y0 >= r->y0 && y1 <= r->y1;
        int apart = x1 < r->x0 || x0 > r->x1 || y1 < r->y0 || y0 > r->y1;
        if (!inside && !apart)
            ga_report("CROSSES A BOX EDGE", s, "", x0, y0);
        else if (inside && (x0 < r->x0 + 1 || x1 > r->x1 - 1 || y0 < r->y0 + 1 || y1 > r->y1 - 1)
                 && (r->x1 - r->x0 > 16 || r->y1 - r->y0 > 16))   /* (a tiny chip may hug its label) */
        {
            char bx[40];
            snprintf(bx, sizeof bx, "ink %d..%d,%d..%d box %d..%d,%d..%d", (int)x0, (int)x1, (int)y0, (int)y1, (int)r->x0,
                     (int)r->x1, (int)r->y0, (int)r->y1);
            ga_report("TOUCHES A BOX EDGE", s, bx, x0, y0);
        }
    }
    if (ga_ntxt < 96) {
        t = &ga_txt[ga_ntxt++];
        t->x0 = x0, t->y0 = y0, t->x1 = x1, t->y1 = y1;
        for (i = 0; i < sizeof t->s - 1u && s[i]; i++)
            t->s[i] = s[i];
        t->s[i] = 0;
    }
}
static void gfx_audit_box(int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (ga_nrnd < 160 && w > 0 && h > 0) {
        ga_box_t *r = &ga_rnd[ga_nrnd++];
        r->x0 = x, r->y0 = y + cv_oy, r->x1 = x + w - 1, r->y1 = y + cv_oy + h - 1, r->s[0] = 0;
    }
}
#endif

static void cv_begin(uint32_t w, uint32_t h, uint16_t bg)
{
    uint32_t i, n;
    if (w * h > CV_MAX)
        h = CV_MAX / w;
    lcd_sync();                     /* the last blit may still read cv_px */
    cv_w = w;
    cv_h = h;
    cv_oy = cv_y0 = 0;
    cv_y1 = (int32_t)h;
#ifdef GFX_AUDIT
    ga_reset();
#endif
    n = w * h;
    for (i = 0; i < n; i++)
        cv_px[i] = swap16(bg);
}

/* X0X: draw the next things as if the canvas were rows y0 .. y0+h-1 alone (one band of a
 * canvas that holds several, sent in one transfer so the bands change on screen together) */
static void cv_band(int32_t y0, int32_t h)
{
    cv_oy = cv_y0 = y0;
    cv_y1 = y0 + h;
}

static void cv_blit(uint32_t x, uint32_t y) { lcd_blit(x, y, cv_w, cv_h, cv_px); }

/* canvas rows r0 .. cv_h-1 only, to screen row y + r0 */
static void cv_blit_from(uint32_t x, uint32_t y, uint32_t r0)
{
    if (r0 < cv_h)
        lcd_blit(x, y + r0, cv_w, cv_h - r0, cv_px + r0 * cv_w);
}

static inline void cv_pset(int32_t x, int32_t y, uint16_t c)
{
    y += cv_oy;
    if ((uint32_t)x < cv_w && y >= cv_y0 && y < cv_y1)
        cv_px[(uint32_t)y * cv_w + (uint32_t)x] = swap16(c);
}

static void cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t x1 = x + w, y1 = y + h + cv_oy, i;
    uint16_t sc = swap16(c);
    y += cv_oy;                             /* clipped once, then filled row by row */
    if (x < 0)
        x = 0;
    if (y < cv_y0)
        y = cv_y0;
    if (x1 > (int32_t)cv_w)
        x1 = (int32_t)cv_w;
    if (y1 > cv_y1)
        y1 = cv_y1;
    for (; y < y1; y++)
        for (i = x; i < x1; i++)
            cv_px[(uint32_t)y * cv_w + (uint32_t)i] = sc;
}

/* colour c over the pixel at a/256 (anti-aliased drawing) */
static void cv_blend(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    uint32_t bg, r, g, b;
    y += cv_oy;
    if ((uint32_t)x >= cv_w || y < cv_y0 || y >= cv_y1 || !a)
        return;
    if (a >= 256u) {
        cv_px[(uint32_t)y * cv_w + (uint32_t)x] = swap16(c);
        return;
    }
    bg = swap16(cv_px[(uint32_t)y * cv_w + (uint32_t)x]);
    r = ((c >> 11) * a + (bg >> 11) * (256u - a)) >> 8;
    g = ((c >> 5 & 63u) * a + (bg >> 5 & 63u) * (256u - a)) >> 8;
    b = ((c & 31u) * a + (bg & 31u) * (256u - a)) >> 8;
    cv_px[(uint32_t)y * cv_w + (uint32_t)x] = swap16((uint16_t)(r << 11 | g << 5 | b));
}

static int32_t f_floor(float v) { int32_t i = (int32_t)v; return v < (float)i ? i - 1 : i; }
static float f_abs(float v) { return v < 0.0f ? -v : v; }
static float f_sqrt1(float v)                   /* sqrt for v >= 1 (Newton, a few steps: line slopes) */
{
    float r = v < 2.0f ? 1.2f : v * 0.5f;
    for (int i = 0; i < 6; i++)
        r = 0.5f * (r + v / r);
    return r;
}

/* an anti-aliased line w pixels wide between sub-pixel points: along its long axis, each step lights the few
 * pixels across it by their distance from the line. The long axis is half-open (the end column is the next
 * segment's first), so a polyline of short segments blends each pixel once. */
static void cv_linef(float x0, float y0, float x1, float y1, uint16_t c, float w)
{
    float dx = x1 - x0, dy = y1 - y0, hw = w * 0.5f;
    int steep = f_abs(dy) > f_abs(dx);
    float k, cs, reach;
    int32_t a, e;
    if (steep) {                                 /* walk y: swap the axes */
        float t;
        t = x0, x0 = y0, y0 = t;
        t = x1, x1 = y1, y1 = t;
        t = dx, dx = dy, dy = t;
    }
    if (dx < 0.0f) {
        float t;
        t = x0, x0 = x1, x1 = t;
        t = y0, y0 = y1, y1 = t;
        dx = -dx, dy = -dy;
    }
    k = dx > 1e-4f ? dy / dx : 0.0f;
    cs = 1.0f / f_sqrt1(1.0f + k * k);           /* across the line, per pixel of the short axis */
    reach = (hw + 1.0f) / cs;
    a = f_floor(x0 + 0.5f);
    e = f_floor(x1 + 0.5f);
    if (e == a)
        e = a + 1;
    for (int32_t i = a; i < e; i++) {
        float yc = y0 + ((float)i - x0) * k;
        int32_t j0 = f_floor(yc - reach), j1 = f_floor(yc + reach) + 1;
        for (int32_t j = j0; j <= j1; j++) {
            float cov = hw + 0.5f - f_abs((float)j - yc) * cs;
            if (cov <= 0.0f)
                continue;
            if (cov > 1.0f)
                cov = 1.0f;
            if (steep)
                cv_blend(j, i, c, (uint32_t)(cov * 256.0f));
            else
                cv_blend(i, j, c, (uint32_t)(cov * 256.0f));
        }
    }
}

/* a soft round dot of radius r at a sub-pixel point */
static void cv_dotf(float x, float y, float r, uint16_t c)
{
    int32_t x0 = f_floor(x - r - 1.0f), x1 = f_floor(x + r + 1.0f), y0 = f_floor(y - r - 1.0f), y1 = f_floor(y + r + 1.0f);
    for (int32_t j = y0; j <= y1; j++)
        for (int32_t i = x0; i <= x1; i++) {
            float ddx = (float)i - x, ddy = (float)j - y, d2 = ddx * ddx + ddy * ddy, cov;
            if (d2 > (r + 1.0f) * (r + 1.0f))
                continue;
            cov = r + 0.5f - (d2 > 1.0f ? f_sqrt1(d2) : d2);
            if (cov > 0.0f)
                cv_blend(i, j, c, cov >= 1.0f ? 256u : (uint32_t)(cov * 256.0f));
        }
}

/* the integer line: anti-aliased, a little over a pixel wide */
static void cv_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    cv_linef((float)x0, (float)y0, (float)x1, (float)y1, c, 1.5f);
}

/* glyph index of a character: lower case folds to upper case when the font
 * has none, anything missing (and C1 controls) draws as '?' */
static uint32_t glyph(const felucca_font_t *f, uint32_t ch)
{
    if (ch >= 'a' && ch <= 'z' && f->last < 'a')
        ch -= 32u;
    if (ch < f->first || ch > f->last || (ch >= 127u && ch < 160u))
        ch = '?';
    return ch - f->first;
}

/* text in colour c, each pixel's coverage blended over what is already on the canvas (an
 * anti-aliased face on a coloured box gets no dark fringe); returns the end x */
static int32_t cv_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    uint32_t r = c >> 11, g = (c >> 5) & 63u, b = c & 31u;
#ifdef GFX_AUDIT
    const char *s0 = s;
    int32_t ix0 = 1 << 20, iy0 = 1 << 20, ix1 = -(1 << 20), iy1 = -(1 << 20), clip = 0, shown = 0;
#endif
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), gx, gy, w, bh, nib = 0, run = 0, a = 0;
        const uint8_t *gd = f->data + f->off[gi], *bx = f->box[gi];
        int32_t x0 = x - f->pad + bx[0], y0 = y + bx[1] + cv_oy;
        w = bx[2];
        bh = bx[3];
        for (gy = 0; gy < bh; gy++)
            for (gx = 0; gx < w; gx++) {
                int32_t px = x0 + (int32_t)gx, py = y0 + (int32_t)gy;
                if (!run) {                          /* the next nibble: a pixel, or a run of them */
                    a = (nib & 1u) ? gd[nib >> 1] & 15u : gd[nib >> 1] >> 4;
                    nib++;
                    run = 1;
                    if (f->rle && (a == 0 || a == 15u)) {
                        run += (nib & 1u) ? gd[nib >> 1] & 15u : gd[nib >> 1] >> 4;
                        nib++;
                    }
                }
                run--;
#ifdef GFX_AUDIT
                if (a >= 4u) {                       /* ink that shows (the faintest edge pixels aside) */
                    if (px < ix0) ix0 = px;
                    if (px > ix1) ix1 = px;
                    if (py < iy0) iy0 = py;
                    if (py > iy1) iy1 = py;
                    if ((uint32_t)px >= cv_w || py < cv_y0 || py >= cv_y1)
                        clip = 1;
                    else
                        shown = 1;
                }
#endif
                if (!a || (uint32_t)px >= cv_w || py < cv_y0 || py >= cv_y1)
                    continue;
                if (a == 15u) {
                    cv_px[(uint32_t)py * cv_w + (uint32_t)px] = swap16(c);
                } else {
                    uint32_t bg = swap16(cv_px[(uint32_t)py * cv_w + (uint32_t)px]);
                    uint32_t br = bg >> 11, bgg = (bg >> 5) & 63u, bb = bg & 31u;
                    uint32_t nr = (r * a + br * (15u - a)) / 15u, ng = (g * a + bgg * (15u - a)) / 15u,
                             nb = (b * a + bb * (15u - a)) / 15u;
                    cv_px[(uint32_t)py * cv_w + (uint32_t)px] = swap16((nr << 11) | (ng << 5) | nb);
                }
            }
        x += f->adv[gi];
    }
#ifdef GFX_AUDIT
    if (shown)                                       /* (wholly outside: a list's hidden rows, not drawn) */
        ga_text(s0, ix0, iy0, ix1, iy1, clip);
#endif
    return x;
}

static int32_t text_w(const felucca_font_t *f, const char *s)
{
    int32_t w = 0;
    for (; *s; s++)
        w += f->adv[glyph(f, (uint8_t)*s)];
    return w;
}

/* one-shot: text in a box, cleared to black, blitted */
static void draw_text_box(uint32_t x, uint32_t y, uint32_t w, const felucca_font_t *f, const char *s,
                          uint16_t c, int align)
{
    int32_t tw = text_w(f, s), tx = 0;
    cv_begin(w, f->h, C_BLACK);
    if (align == 1)
        tx = ((int32_t)w - tw) / 2;
    else if (align == 2)
        tx = (int32_t)w - tw;
    cv_text(tx, 0, f, s, c);
    cv_blit(x, y);
    lcd_sync();                     /* one-shots (boot, crash, UBOOT, update) finish here */
}
