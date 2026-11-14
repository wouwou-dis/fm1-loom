/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal. Its text drawing (loom_glyph, loom_text_w, loom_text_box, loom_text's blending)
 * is fm1-base's device/gfx.c, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, under MPL-2.0
 * as there (felucca-bsp#4) */
/* LOOM-8's drawing over fm1-base's canvas (base/device/gfx.c: cv_px, cv_begin, cv_rect, cv_blit, the palette):
 * its own fonts in a compact format (each glyph its ink box only, the big faces run-length coded: tools/gen_font.py,
 * a third of fm1-base's), anti-aliased lines and dots, and on the host the layout audit (GFX_AUDIT). Included first by
 * app.c; from its end on, the app's FONT_*, cv_text, text_w, draw_text_box, cv_begin and cv_line are these (the
 * platform's own screens, compiled before the app, keep fm1-base's). */
typedef struct {               /* proportional, see tools/gen_font.py */
    uint8_t h;
    uint8_t pad;               /* bitmap starts this many pixels left of the pen */
    uint8_t first, last;
    uint8_t rle;               /* 1: the nibbles code runs of 0 and 15; 2: Huffman-coded alphas (gen_font.py) */
    const uint8_t *adv;        /* advance per glyph */
    const uint8_t (*box)[4];   /* ink box per glyph: x (from FONT_PAD left of the pen), y, w, h */
    const uint16_t *off;       /* byte offset of each glyph's nibbles */
    const uint8_t *data;
} loom_font_t;
#include "loom_font.h"

#if defined PLAT_HOST && !defined PLAT_WEB
#define GFX_AUDIT 1                                  /* the layout audit: LAYOUT lines on stdout (host.c counts them) */
#endif
#ifdef GFX_AUDIT
/* host only: the layout audit. Within one canvas, a label's ink must stay inside the canvas (and its band),
 * must not touch another label's ink, and must not cross the edge of a rounded box (wholly in or wholly out).
 * Findings print once each as "LAYOUT ..." with where the app is (gfx_audit_where, the host's). */
static const char *gfx_audit_where(void);
typedef struct { int32_t x0, y0, x1, y1; char s[24]; } ga_box_t;
static ga_box_t ga_txt[96], ga_rnd[160];
static uint32_t ga_ntxt, ga_nrnd, ga_seen[512], ga_nseen, ga_findings;
static int32_t ga_margin;                            /* LOOM_MARGIN=N (host.c): labels N px apart, and from boxes' edges */
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
        else if (ga_margin && x0 <= o->x1 + ga_margin && o->x0 <= x1 + ga_margin && y0 <= o->y1 + ga_margin &&
                 o->y0 <= y1 + ga_margin)
            ga_report("TOO CLOSE", s, o->s, x0, y0);   /* (LOOM_MARGIN=N: closer than N px to another label) */
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
        for (uint32_t i = 0; i < ga_ntxt; i++) {     /* a box drawn after a label: over it, or (LOOM_MARGIN) close */
            const ga_box_t *t = &ga_txt[i];
            int inside = t->x0 >= r->x0 && t->x1 <= r->x1 && t->y0 >= r->y0 && t->y1 <= r->y1;
            int apart = t->x1 < r->x0 || t->x0 > r->x1 || t->y1 < r->y0 || t->y0 > r->y1;
            int near = t->x1 + ga_margin < r->x0 || t->x0 - ga_margin > r->x1 || t->y1 + ga_margin < r->y0 ||
                       t->y0 - ga_margin > r->y1;
            if (!inside && !apart)
                ga_report("BOX OVER A LABEL", t->s, "", t->x0, t->y0);
            else if (apart && !near && ga_margin && (r->x1 - r->x0 > 3 && r->y1 - r->y0 > 3))
                ga_report("TOO CLOSE TO A BOX", t->s, "", t->x0, t->y0);
        }
    }
}
#endif

static void loom_begin(uint32_t w, uint32_t h, uint16_t bg)   /* fm1-base's cv_begin, and a new canvas to audit */
{
    cv_begin(w, h, bg);
#ifdef GFX_AUDIT
    ga_reset();
#endif
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
 * pixels across it by their distance from the line. The long axis is half-open: the line's own end (whichever way it
 * runs) is the next segment's first, so a polyline of short segments blends each pixel once. end_only: just that end
 * column (a polyline turning from one long axis to the other draws it: the next segment, walking the other axis, would
 * leave a notch there). Returns 1 if it walked y. */
static int cv_line_ex(float x0, float y0, float x1, float y1, uint16_t c, float w, int end_only)
{
    float dx = x1 - x0, dy = y1 - y0, hw = w * 0.5f;
    int steep = f_abs(dy) > f_abs(dx), back = 0;
    float k, cs, reach;
    int32_t a, e, from, to;
    if (steep) {                                 /* walk y: swap the axes */
        float t;
        t = x0, x0 = y0, y0 = t;
        t = x1, x1 = y1, y1 = t;
        t = dx, dx = dy, dy = t;
    }
    if (dx < 0.0f) {                             /* walk it forwards (its end is then the first column) */
        float t;
        t = x0, x0 = x1, x1 = t;
        t = y0, y0 = y1, y1 = t;
        dx = -dx, dy = -dy;
        back = 1;
    }
    k = dx > 1e-4f ? dy / dx : 0.0f;
    cs = 1.0f / f_sqrt1(1.0f + k * k);           /* across the line, per pixel of the short axis */
    reach = (hw + 1.0f) / cs;
    a = f_floor(x0 + 0.5f);
    e = f_floor(x1 + 0.5f);
    if (e == a)
        e = a + 1;
    from = back ? a + 1 : a;                     /* [start, end): the end column left out */
    to = back ? e : e - 1;
    if (end_only)
        from = to = back ? a : e;
    for (int32_t i = from; i <= to; i++) {
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
    return steep;
}
static void cv_linef(float x0, float y0, float x1, float y1, uint16_t c, float w)
{
    cv_line_ex(x0, y0, x1, y1, c, w, 0);
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
static void loom_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    cv_linef((float)x0, (float)y0, (float)x1, (float)y1, c, 1.5f);
}

/* ---- the service manual's drawing (pages.c builds its figures from these): integer paths per pixel; a float per
 * shape or per row at most */
static uint32_t cv_isqrt(uint32_t v)                 /* floor(sqrt(v)) */
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
        b >>= 2;
    }
    return r;
}
static int32_t fx8(float v) { return (int32_t)(v * 256.0f + (v < 0.0f ? -0.5f : 0.5f)); }   /* to 1/256 px */
/* a filled circle (disc) or a ring w wide on a radius r, anti-aliased: each pixel by its centre's distance from the
 * centre, in 1/256 px (pixel (i, j)'s centre is the point (i, j), as cv_dotf). r up to ~120 px */
static void cv_circ(float cx, float cy, float r, float w, uint16_t c)
{
    int32_t x8 = fx8(cx), y8 = fx8(cy), r8 = fx8(r), h8 = fx8(w * 0.5f), out = r8 + (w > 0.0f ? h8 : 0) + 256;
    int32_t i0 = (x8 - out) >> 8, i1 = (x8 + out) >> 8, j0 = (y8 - out) >> 8, j1 = (y8 + out) >> 8;
    for (int32_t j = j0; j <= j1; j++) {
        int32_t dy = j * 256 - y8;
        for (int32_t i = i0; i <= i1; i++) {
            int32_t dx = i * 256 - x8, d, a;
            uint32_t d2 = (uint32_t)(dx * dx) + (uint32_t)(dy * dy);
            if (d2 > (uint32_t)out * (uint32_t)out)
                continue;
            d = (int32_t)cv_isqrt(d2);
            a = w > 0.0f ? h8 + 128 - (d > r8 ? d - r8 : r8 - d) : r8 + 128 - d;
            if (a > 0)
                cv_blend(i, j, c, a >= 256 ? 256u : (uint32_t)a);
        }
    }
}
static void cv_disc(float cx, float cy, float r, uint16_t c) { cv_circ(cx, cy, r, 0.0f, c); }
static void cv_ring(float cx, float cy, float r, float w, uint16_t c) { cv_circ(cx, cy, r, w, c); }
/* a filled polygon (n points x, y, x, y ...; any shape, even-odd): each pixel whose centre is inside, at a/256. hatch
 * > 0: only the pixels of diagonal lines ("/", hatch px apart along a row: the manual's section hatching), so the
 * same call hatches a wall or the area under a curve. A row's crossings in float, its pixels in integers */
static void cv_poly(const float *xy, int n, uint16_t c, uint32_t a, int32_t hatch)
{
    float y0 = xy[1], y1 = xy[1];
    for (int k = 1; k < n; k++) {
        y0 = xy[2 * k + 1] < y0 ? xy[2 * k + 1] : y0;
        y1 = xy[2 * k + 1] > y1 ? xy[2 * k + 1] : y1;
    }
    for (int32_t j = f_floor(y0); j <= f_floor(y1) + 1; j++) {
        float xs[24], y = (float)j;
        int m = 0;
        for (int k = 0; k < n && m < 24; k++) {
            float ax = xy[2 * k], ay = xy[2 * k + 1], bx = xy[2 * ((k + 1) % n)], by = xy[2 * ((k + 1) % n) + 1];
            if ((ay <= y && by > y) || (by <= y && ay > y))
                xs[m++] = ax + (y - ay) * (bx - ax) / (by - ay);
        }
        for (int p = 1; p < m; p++)                  /* (a few: insertion) */
            for (int q = p; q > 0 && xs[q] < xs[q - 1]; q--) {
                float t = xs[q];
                xs[q] = xs[q - 1], xs[q - 1] = t;
            }
        for (int p = 0; p + 1 < m; p += 2) {
            int32_t i0 = f_floor(xs[p] + 0.5f), i1 = f_floor(xs[p + 1] + 0.5f);   /* [i0, i1): the centres inside */
            for (int32_t i = i0; i < i1; i++)
                if (hatch <= 0 || ((i + j) % hatch + hatch) % hatch == 0)
                    cv_blend(i, j, c, a);
        }
    }
}
/* a small filled triangle, anti-aliased (4 x 4 samples a pixel, in integers): pointers, arrowheads */
static void cv_tri(float ax, float ay, float bx, float by, float qx, float qy, uint16_t c)
{
    int32_t x[3] = {fx8(ax) >> 6, fx8(bx) >> 6, fx8(qx) >> 6}, y[3] = {fx8(ay) >> 6, fx8(by) >> 6, fx8(qy) >> 6};
    int32_t s = (x[1] - x[0]) * (y[2] - y[0]) - (y[1] - y[0]) * (x[2] - x[0]);   /* (quarter pixels; the winding) */
    int32_t i0 = x[0], i1 = x[0], j0 = y[0], j1 = y[0];
    for (int k = 1; k < 3; k++) {
        i0 = x[k] < i0 ? x[k] : i0, i1 = x[k] > i1 ? x[k] : i1;
        j0 = y[k] < j0 ? y[k] : j0, j1 = y[k] > j1 ? y[k] : j1;
    }
    if (!s)
        return;
    for (int32_t j = (j0 >> 2) - 1; j <= (j1 >> 2) + 1; j++)
        for (int32_t i = (i0 >> 2) - 1; i <= (i1 >> 2) + 1; i++) {
            uint32_t in = 0;
            for (int32_t sy = 0; sy < 4; sy++)
                for (int32_t sx = 0; sx < 4; sx++) {
                    int32_t px = i * 4 - 2 + sx, py = j * 4 - 2 + sy, ok = 1;   /* (the samples around the centre) */
                    for (int k = 0; k < 3 && ok; k++) {
                        int32_t e = (x[(k + 1) % 3] - x[k]) * (py - y[k]) - (y[(k + 1) % 3] - y[k]) * (px - x[k]);
                        ok = s > 0 ? e >= 0 : e <= 0;
                    }
                    in += (uint32_t)ok;
                }
            if (in)
                cv_blend(i, j, c, in * 16u);
        }
}
/* a dashed line: on px drawn, off px left, from the start */
static void cv_dash(float x0, float y0, float x1, float y1, float on, float off, float w, uint16_t c)
{
    float dx = x1 - x0, dy = y1 - y0, len = f_abs(dx) > f_abs(dy) ? f_abs(dx) : f_abs(dy), t;
    if (len < 1.0f)
        return;
    len = f_sqrt1(dx * dx + dy * dy);
    dx /= len, dy /= len;
    for (t = 0.0f; t < len; t += on + off) {
        float e = t + on < len ? t + on : len;
        cv_linef(x0 + dx * t, y0 + dy * t, x0 + dx * e, y0 + dy * e, c, w);
    }
}
/* a dotted leader along a row (the manual's "label ......... value"): a dot every 3 px */
static void cv_leader(int32_t x0, int32_t x1, int32_t y, uint16_t c)
{
    for (int32_t x = x0 + (3 - x0 % 3) % 3; x <= x1; x += 3)   /* (on a 3 px grid: leaders line up) */
        cv_blend(x, y, c, 210u);
}
/* graph paper: a dot every step px over the box, on the glass's grid (gy: the canvas's top on the glass) */
static void cv_grid(int32_t x0, int32_t y0, int32_t w, int32_t h, int32_t step, int32_t gy, uint16_t c)
{
    for (int32_t y = y0 + ((step - (y0 + gy) % step) % step); y < y0 + h; y += step)
        for (int32_t x = x0 + (step - x0 % step) % step; x < x0 + w; x += step)
            cv_blend(x, y, c, 220u);
}

/* glyph index of a character: lower case folds to upper case when the font
 * has none, anything missing (and C1 controls) draws as '?' */
static uint32_t loom_glyph(const loom_font_t *f, uint32_t ch)
{
    if (ch >= 'a' && ch <= 'z' && f->last < 'a')
        ch -= 32u;
    if (ch < f->first || ch > f->last || (ch >= 127u && ch < 160u))
        ch = '?';
    return ch - f->first;
}

/* the next alpha of a Huffman-coded glyph (rle 2, gen_font.py huff_face): t the code for the alpha before (the counts of
 * each length 1..12, then the alphas in code order), d the face's data, *bit the next bit's index */
static uint32_t loom_alpha(const uint8_t *t, const uint8_t *d, uint32_t *bit)
{
    uint32_t len, code = 0, first = 0, at = 0;
    for (len = 0; len < 12u; len++) {
        uint32_t n = t[len];
        code |= (uint32_t)(d[*bit >> 3] >> (7u - (*bit & 7u))) & 1u;
        ++*bit;
        if (code - first < n)
            return t[12u + at + code - first];
        at += n;
        first = (first + n) << 1;
        code <<= 1;
    }
    return 0;
}

/* text in colour c, each pixel's coverage blended over what is already on the canvas (an
 * anti-aliased face on a coloured box gets no dark fringe); returns the end x */
static int32_t loom_track;                           /* letters this much closer (a label a few pixels too wide: 1) */
static int32_t loom_text(int32_t x, int32_t y, const loom_font_t *f, const char *s, uint16_t c)
{
    uint32_t r = c >> 11, g = (c >> 5) & 63u, b = c & 31u;
#ifdef GFX_AUDIT
    const char *s0 = s;
    int32_t ix0 = 1 << 20, iy0 = 1 << 20, ix1 = -(1 << 20), iy1 = -(1 << 20), clip = 0, shown = 0;
#endif
    for (; *s; s++) {
        uint32_t gi = loom_glyph(f, (uint8_t)*s), gx, gy, w, bh, nib = 0, run = 0, a = 0;
        const uint8_t *gd = f->data + f->off[gi], *bx = f->box[gi];
        int32_t x0 = x - f->pad + bx[0], y0 = y + bx[1] + cv_oy;
        w = bx[2];
        bh = bx[3];
        for (gy = 0; gy < bh; gy++)
            for (gx = 0; gx < w; gx++) {
                int32_t px = x0 + (int32_t)gx, py = y0 + (int32_t)gy;
                if (f->rle == 2u) {                  /* (Huffman: the code for the alpha before; bits from the glyph's) */
                    if (!gx && !gy)
                        nib = (uint32_t)f->off[gi] * 8u, a = 0;
                    a = loom_alpha(f->data + 28u * (a == 0 ? 0u : a == 15u ? 1u : 2u), f->data, &nib);
                    run = 1;
                } else if (!run) {                   /* the next nibble: a pixel, or a run of them */
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
        x += f->adv[gi] - (s[1] ? loom_track : 0);
    }
#ifdef GFX_AUDIT
    if (shown)                                       /* (wholly outside: a list's hidden rows, not drawn) */
        ga_text(s0, ix0, iy0, ix1, iy1, clip);
#endif
    return x;
}

static int32_t loom_text_w(const loom_font_t *f, const char *s)
{
    int32_t w = 0;
    for (; *s; s++)
        w += f->adv[loom_glyph(f, (uint8_t)*s)] - (s[1] ? loom_track : 0);
    return w;
}

/* one-shot: text in a box, cleared to black, blitted */
static void loom_text_box(uint32_t x, uint32_t y, uint32_t w, const loom_font_t *f, const char *s, uint16_t c, int align)
{
    int32_t tw = loom_text_w(f, s), tx = 0;
    loom_begin(w, f->h, C_BLACK);
    if (align == 1)
        tx = ((int32_t)w - tw) / 2;
    else if (align == 2)
        tx = (int32_t)w - tw;
    loom_text(tx, 0, f, s, c);
    cv_blit(x, y);
    lcd_sync();
}

/* fm1-base's own screens in LOOM-8's faces (base/device/sys_text.c, APP_SYS_TEXT): the body, a heading, the big
 * one (the panel's screens: the heading's face, as before) */
static const loom_font_t *const SYS_FACE[3] = {&LFONT_S, &LFONT_B, &LFONT_B};
static int32_t app_sys_h(int face) { return SYS_FACE[face]->h; }
static int32_t app_sys_w(int face, const char *s) { return loom_text_w(SYS_FACE[face], s); }
static void app_sys_text(int32_t x, int32_t y, int face, const char *s, uint16_t c) { loom_text(x, y, SYS_FACE[face], s, c); }

/* from here on, the app draws with these */
#define felucca_font_t loom_font_t
#define FONT_XS LFONT_XS
#define FONT_S LFONT_S
#define FONT_B LFONT_B
#define FONT_M LFONT_M
#define FONT_L LFONT_L
#define cv_begin loom_begin
#define cv_line loom_line
#define cv_text loom_text
#define text_w loom_text_w
#define draw_text_box loom_text_box
