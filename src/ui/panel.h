/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The GUI panel: layout, drawing and mouse handling, independent of any windowing system.
 *
 * House style (style.h, font.h, canvas.h) is shared with the other plugin GUIs. The design
 * is original: it keeps the functional grouping of the circuit (gain, diode clipper, level,
 * footswitch) but uses its own name, lettering and colours, and none of the original
 * pedal's branding or panel artwork. */
#ifndef UI_PANEL_H
#define UI_PANEL_H

#include <stdio.h>
#include <stdlib.h>

#include "../ports.h"
#include "canvas.h"
#include "font.h"
#include "style.h"

#define PANEL_W 720
#define PANEL_H 450

#define N_DIODES 5

enum { W_KNOB, W_TRIM, W_FOOT, W_SELECT };

typedef struct {
    int kind, port;
    float x, y, r;
    float min, max, def;
    int log;
    const char* label; /* on the panel */
    const char* name;  /* in the display */
    const char* lo;    /* scale end labels */
    const char* hi;
} Widget;

static const Widget WIDGETS[] = {
    { W_KNOB, PORT_DISTORTION, 116, 178, 30, 0, 1, 0.5f, 0, "DISTORTION", "DISTORTION", "0", "10" },
    { W_SELECT, PORT_DIODE, 360, 112, 0, 0, N_DIODES - 1, 0, 0, "DIODES", "DIODES", 0, 0 },
    { W_KNOB, PORT_OUTPUT, 604, 178, 30, 0, 1, 0.8f, 0, "OUTPUT", "OUTPUT", "0", "10" },
    { W_TRIM, PORT_INPUT_VOLTS, 116, 372, 17, 0.1f, 10.0f, 1.0f, 1, "INPUT V", "VOLTS AT 0 DBFS", 0, 0 },
    { W_FOOT, PORT_ENABLED, 360, 386, 25, 0, 1, 1, 0, "ENGAGE", "ENGAGE", 0, 0 },
};
#define N_WIDGETS ((int)(sizeof WIDGETS / sizeof WIDGETS[0]))

/* Diode selector rows: label, character tag, and the name shown in the readout. */
static const struct { const char *label, *tag, *name; } DIODES[N_DIODES] = {
    { "1N270 GERMANIUM", "ORIGINAL", "1N270 GERMANIUM" },
    { "1N34 GERMANIUM", "FUZZY", "1N34 GERMANIUM" },
    { "1N4148 SILICON", "BUZZ", "1N4148 SILICON" },
    { "RED LED", "CRUNCH", "RED LED" },
    { "1N34 ARRAY", "ASYMMETRIC", "1N34 ARRAY, 2 + 1" },
};
#define SEL_STEP 34.0f /* row spacing */
#define SEL_X0 236.0f  /* row hit area */
#define SEL_X1 484.0f

/* Clip lamp: lit by the plugin's clip output port. */
#define LAMP_X 604.0f
#define LAMP_Y 368.0f

#define KNOB_SPAN (0.75f * CV_PI) /* +/-135 degrees */

typedef void (*PanelWrite)(void* host, int port, float value);

typedef struct {
    uint32_t* px; /* what the window shows */
    uint32_t* bg; /* static artwork */
    Canvas c;
    float val[PORT_COUNT];
    int drag;      /* widget being dragged, or -1 */
    float drag_x, drag_y, drag_norm;
    int shown;     /* widget in the display, or -1 */
    int dirty;
    PanelWrite write;
    void* host;
} Panel;

/* ---- Values ---- */

static float w_norm(const Widget* w, float v)
{
    float n = w->log ? logf(v / w->min) / logf(w->max / w->min) : (v - w->min) / (w->max - w->min);
    return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
}

static float w_value(const Widget* w, float n)
{
    n = n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    return w->log ? w->min * powf(w->max / w->min, n) : w->min + n * (w->max - w->min);
}

static int w_index(const Panel* p, const Widget* w)
{
    int i = (int)(p->val[w->port] + 0.5f);
    int n = w->kind == W_FOOT ? 2 : N_DIODES;
    return i < 0 ? 0 : (i >= n ? n - 1 : i);
}

static void w_format(const Panel* p, const Widget* w, char* buf, size_t n)
{
    const float v = p->val[w->port];
    switch (w->kind) {
    case W_SELECT:
        snprintf(buf, n, "%s", DIODES[w_index(p, w)].name);
        break;
    case W_FOOT:
        snprintf(buf, n, "%s", w_index(p, w) ? "ON" : "BYPASS");
        break;
    default:
        if (w->port == PORT_DISTORTION)
            snprintf(buf, n, "%.1f   GAIN %.0f DB", 10.0f * v, distortion_gain_db(v));
        else if (w->port == PORT_OUTPUT)
            if (v <= 0.0005f)
                snprintf(buf, n, "0.0   OFF");
            else
                snprintf(buf, n, "%.1f   %.1f DB", 10.0f * v, 20.0 * log10(audio_taper(v)));
        else
            snprintf(buf, n, "%.2f V", v);
    }
}

/* ---- Static artwork ---- */

static void draw_screw(Canvas* c, float x, float y)
{
    cv_circle(c, x + 1, y + 1.5f, 6, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, 6, 0xb8bbc2, 0x4a4d54, -0.4f, -0.5f, 1);
    cv_line(c, x - 4, y + 2, x + 4, y - 2, 1.6f, 0x2a2c30, 0.9f);
}

static void draw_section(Canvas* c, float x0, float y0, float x1, float y1, const char* title)
{
    const float tw = text_width(title, 9, 2.5f);
    const float cx = 0.5f * (x0 + x1);
    cv_rrect(c, x0, y0, x1, y1, 10, 0x000000, 0.18f);
    cv_rrect_line(c, x0, y0, x1, y1, 10, 1.4f, COL_LINE, 0.9f);
    /* Title sits in a gap in the top edge. */
    cv_rrect(c, cx - 0.5f * tw - 8, y0 - 6, cx + 0.5f * tw + 8, y0 + 6, 3,
             mix(COL_PANEL_TOP, COL_PANEL_BOT, y0 / PANEL_H), 1);
    text_center(c, cx, y0 - 4.5f, title, 9, 2.5f, 1.5f, COL_AMBER, 1);
}

static void draw_knob_scale(Canvas* c, const Widget* w)
{
    const int big = w->kind == W_KNOB;
    const int ticks = big ? 11 : 7;
    int i;
    for (i = 0; i < ticks; ++i) {
        const float a = -KNOB_SPAN + 2.0f * KNOB_SPAN * i / (ticks - 1);
        const float r0 = w->r + (big ? 9.0f : 6.0f), r1 = r0 + ((i % 5 == 0 || !big) ? 6.0f : 3.5f);
        cv_line(c, cv_ax(w->x, r0, a), cv_ay(w->y, r0, a), cv_ax(w->x, r1, a), cv_ay(w->y, r1, a),
                1.3f, COL_INK, 0.8f);
    }
    /* Dim track under the value arc. */
    cv_arc(c, w->x, w->y, w->r + 4.5f, 2.2f, -KNOB_SPAN, KNOB_SPAN, 0x000000, 0.55f);
    if (w->lo) {
        const float rr = w->r + 22.0f;
        text_center(c, cv_ax(w->x, rr, -KNOB_SPAN) - 2, cv_ay(w->y, rr, -KNOB_SPAN) - 3, w->lo, 7, 1, 1.1f, COL_DIM, 1);
        text_center(c, cv_ax(w->x, rr, KNOB_SPAN) + 2, cv_ay(w->y, rr, KNOB_SPAN) - 3, w->hi, 7, 1, 1.1f, COL_DIM, 1);
    }
    text_center(c, w->x, w->y + w->r + (big ? 20.0f : 14.0f), w->label, big ? 10.0f : 8.0f, 2.0f,
                big ? 1.7f : 1.4f, COL_INK, 1);
}

static void draw_foot_static(Canvas* c, const Widget* w)
{
    cv_circle(c, w->x + 2, w->y + 4, w->r + 9, 0x000000, 0.5f);
    cv_shaded_circle(c, w->x, w->y, w->r + 8, 0xa9adb5, 0x3c3f45, -0.5f, -0.7f, 1);
    cv_ring(c, w->x, w->y, w->r + 8, 1.2f, 0x202226, 0.8f);
    text_center(c, w->x, w->y + w->r + 18, w->label, 10, 2.5f, 1.7f, COL_INK, 1);
    /* LED bezel */
    cv_circle(c, w->x, w->y - w->r - 26, 7.5f, 0x0e0e10, 1);
    cv_ring(c, w->x, w->y - w->r - 26, 7.5f, 1.5f, 0x8a8e96, 0.8f);
}

static void draw_select_static(Canvas* c, const Widget* w)
{
    int i;
    for (i = 0; i < N_DIODES; ++i) {
        const float y = w->y + SEL_STEP * i;
        if (i > 0)
            cv_line(c, SEL_X0 + 8, y - 0.5f * SEL_STEP, SEL_X1 - 8, y - 0.5f * SEL_STEP, 1, COL_LINE, 0.35f);
        /* Indicator bezel */
        cv_circle(c, SEL_X0 + 16, y, 6.5f, 0x0e0e10, 1);
        cv_ring(c, SEL_X0 + 16, y, 6.5f, 1.3f, 0x8a8e96, 0.7f);
        text_draw(c, SEL_X1 - 6 - text_width(DIODES[i].tag, 7, 1.4f), y - 3.5f, DIODES[i].tag, 7, 1.4f,
                  1.1f, COL_DIM, 1);
    }
}

static void draw_lamp_static(Canvas* c, float x, float y)
{
    cv_circle(c, x + 1, y + 2, 13, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, 13, 0xb9bcc3, 0x45484e, -0.4f, -0.6f, 1);
    cv_circle(c, x, y, 9, 0x140c04, 1);
    text_center(c, x, y + 22, "CLIP", 8, 2.0f, 1.4f, COL_INK, 1);
}

static void draw_background(Panel* p)
{
    Canvas c;
    int i, y;
    cv_init(&c, p->bg, PANEL_W, PANEL_H);
    for (y = 0; y < PANEL_H; ++y) {
        uint32_t col = mix(COL_PANEL_TOP, COL_PANEL_BOT, (float)y / PANEL_H);
        int x;
        for (x = 0; x < PANEL_W; ++x)
            p->bg[y * PANEL_W + x] = col;
    }
    cv_texture(&c, PANEL_TEXTURE);
    /* Bevelled edge */
    cv_rrect_line(&c, 1.5f, 1.5f, PANEL_W - 1.5f, PANEL_H - 1.5f, 6, 1.5f, 0x55595f, 0.8f);
    cv_rrect_line(&c, 3.5f, 3.5f, PANEL_W - 3.5f, PANEL_H - 3.5f, 5, 1.5f, 0x000000, 0.4f);

    /* Header */
    text_draw(&c, 30, 18, "DAVIDIAN CANARY PLUS", 20, 3.2f, 2.8f, COL_INK, 1);
    text_draw(&c, 32, 46, "OP-AMP GAIN STAGE INTO A DIODE CLIPPER", 7, 2.4f, 1.1f, COL_DIM, 1);
    cv_line(&c, 30, 60, 396, 60, 1.2f, COL_AMBER, 0.55f);
    /* Readout window */
    cv_rrect(&c, 412, 14, 704, 56, 6, 0x000000, 0.6f);
    cv_rrect(&c, 414, 16, 702, 54, 5, 0x0d0a06, 1);
    cv_rrect_line(&c, 412, 14, 704, 56, 6, 1.2f, 0x6d7179, 0.8f);

    draw_section(&c, 16, 76, 216, 300, "GAIN");
    draw_section(&c, 226, 76, 494, 300, "CLIPPER DIODES");
    draw_section(&c, 504, 76, 704, 300, "LEVEL");
    draw_section(&c, 16, 322, 216, 434, "CALIBRATE");
    draw_section(&c, 504, 322, 704, 434, "MONITOR");

    for (i = 0; i < N_WIDGETS; ++i) {
        const Widget* w = &WIDGETS[i];
        switch (w->kind) {
        case W_KNOB:
        case W_TRIM: draw_knob_scale(&c, w); break;
        case W_FOOT: draw_foot_static(&c, w); break;
        case W_SELECT: draw_select_static(&c, w); break;
        }
    }
    draw_lamp_static(&c, LAMP_X, LAMP_Y);

    draw_screw(&c, 14, 14);
    draw_screw(&c, PANEL_W - 14, 14);
    draw_screw(&c, 14, PANEL_H - 14);
    draw_screw(&c, PANEL_W - 14, PANEL_H - 14);
}

/* ---- Moving parts ---- */

static void draw_knob(Canvas* c, const Widget* w, float n, int active)
{
    const float r = w->r, x = w->x, y = w->y;
    const float ang = -KNOB_SPAN + 2.0f * KNOB_SPAN * n;
    const int knurls = w->kind == W_KNOB ? 36 : 20;
    int i;
    cv_arc(c, x, y, r + 4.5f, 2.2f, -KNOB_SPAN, ang, active ? 0xffd27f : COL_AMBER, 0.95f);
    cv_circle(c, x + 2, y + 4, r + 1, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, r, COL_KNOB_LIGHT, COL_KNOB_DARK, -0.45f, -0.65f, 1);
    for (i = 0; i < knurls; ++i) {
        const float a = ang + 2.0f * CV_PI * i / knurls;
        cv_line(c, cv_ax(x, r * 0.84f, a), cv_ay(y, r * 0.84f, a), cv_ax(x, r * 0.98f, a),
                cv_ay(y, r * 0.98f, a), w->kind == W_KNOB ? 1.6f : 1.2f, 0x000000, 0.55f);
    }
    cv_shaded_circle(c, x, y, r * 0.74f, 0x3c3e44, 0x18191c, -0.35f, -0.55f, 1);
    cv_ring(c, x, y, r * 0.74f, 1.0f, 0x6a6d74, 0.5f);
    cv_line(c, cv_ax(x, r * 0.2f, ang), cv_ay(y, r * 0.2f, ang), cv_ax(x, r * 0.93f, ang),
            cv_ay(y, r * 0.93f, ang), w->kind == W_KNOB ? 3.0f : 2.2f, COL_INK, 1);
}

static void draw_foot(Canvas* c, const Widget* w, int on)
{
    const float lx = w->x, ly = w->y - w->r - 26;
    cv_shaded_circle(c, w->x, w->y, w->r, 0xe3e5e9, 0x6b6e75, -0.4f, -0.55f, 1);
    cv_ring(c, w->x, w->y, w->r * 0.62f, 1.2f, 0x8e9198, 0.6f);
    cv_ring(c, w->x, w->y, w->r, 1.0f, 0x34363b, 0.8f);
    if (on) {
        cv_glow(c, lx, ly, 5, 16, COL_RED, 0.35f);
        cv_shaded_circle(c, lx, ly, 5.5f, 0xffd0c8, COL_RED, -0.3f, -0.4f, 1);
    } else {
        cv_shaded_circle(c, lx, ly, 5.5f, 0x5a1f1b, 0x240a08, -0.3f, -0.4f, 1);
    }
}

static void draw_select(Canvas* c, const Widget* w, int idx)
{
    int i;
    for (i = 0; i < N_DIODES; ++i) {
        const float y = w->y + SEL_STEP * i;
        const int on = i == idx;
        if (on) {
            cv_glow(c, SEL_X0 + 16, y, 3, 12, COL_AMBER, 0.35f);
            cv_shaded_circle(c, SEL_X0 + 16, y, 4.5f, 0xfff0d0, COL_AMBER, -0.3f, -0.4f, 1);
        } else {
            cv_circle(c, SEL_X0 + 16, y, 4.5f, 0x2a1a08, 1);
        }
        text_draw(c, SEL_X0 + 32, y - 4, DIODES[i].label, 8, 1.8f, 1.35f, on ? COL_AMBER : COL_INK, on ? 1.0f : 0.75f);
    }
}

static void draw_lamp(Canvas* c, float x, float y, float b)
{
    b = b < 0.0f ? 0.0f : (b > 1.0f ? 1.0f : b);
    cv_circle(c, x, y, 9, mix(0x1e1206, COL_AMBER, b), 1);
    if (b > 0.02f) {
        cv_glow(c, x, y, 5, 22, COL_AMBER, 0.45f * b);
        cv_circle(c, x - 2.5f, y - 2.5f, 2.6f, 0xfff2d6, 0.8f * b);
    }
}

static void draw_display(Panel* p)
{
    char val[64];
    Canvas* c = &p->c;
    if (p->shown < 0) {
        text_draw(c, 428, 22, "CANARY PLUS", 8, 2.2f, 1.3f, COL_AMBER_DK, 1);
        text_draw(c, 428, 36, "DRAG KNOBS - DOUBLE-CLICK RESETS", 7, 1.6f, 1.2f, 0x9c6a24, 1);
        return;
    }
    w_format(p, &WIDGETS[p->shown], val, sizeof val);
    text_draw(c, 428, 22, WIDGETS[p->shown].name, 8, 2.2f, 1.3f, 0xc58b33, 1);
    text_draw(c, 428, 35, val, 13, 2.0f, 2.0f, COL_AMBER, 1);
}

static void panel_draw(Panel* p)
{
    int i;
    memcpy(p->px, p->bg, sizeof(uint32_t) * PANEL_W * PANEL_H);
    for (i = 0; i < N_WIDGETS; ++i) {
        const Widget* w = &WIDGETS[i];
        switch (w->kind) {
        case W_KNOB:
        case W_TRIM: draw_knob(&p->c, w, w_norm(w, p->val[w->port]), p->drag == i); break;
        case W_FOOT: draw_foot(&p->c, w, w_index(p, w)); break;
        case W_SELECT: draw_select(&p->c, w, w_index(p, w)); break;
        }
    }
    draw_lamp(&p->c, LAMP_X, LAMP_Y, p->val[PORT_CLIP]);
    draw_display(p);
    p->dirty = 0;
}

/* ---- Setup and host values ---- */

static int panel_init(Panel* p, PanelWrite write, void* host)
{
    int i;
    memset(p, 0, sizeof *p);
    p->px = (uint32_t*)malloc(sizeof(uint32_t) * PANEL_W * PANEL_H);
    p->bg = (uint32_t*)malloc(sizeof(uint32_t) * PANEL_W * PANEL_H);
    if (!p->px || !p->bg) {
        free(p->px);
        free(p->bg);
        return 0;
    }
    cv_init(&p->c, p->px, PANEL_W, PANEL_H);
    for (i = 0; i < N_WIDGETS; ++i)
        p->val[WIDGETS[i].port] = WIDGETS[i].def;
    p->drag = -1;
    p->shown = -1;
    p->write = write;
    p->host = host;
    draw_background(p);
    panel_draw(p);
    return 1;
}

static void panel_free(Panel* p)
{
    free(p->px);
    free(p->bg);
    p->px = p->bg = NULL;
}

/* A value from the host. */
static void panel_set(Panel* p, int port, float v)
{
    if (port < 0 || port >= PORT_COUNT || !isfinite(v))
        return;
    if (p->val[port] != v) {
        p->val[port] = v;
        p->dirty = 1;
    }
}

/* ---- Mouse ---- */

static void set_value(Panel* p, int wi, float v)
{
    const Widget* w = &WIDGETS[wi];
    if (w->kind == W_SELECT || w->kind == W_FOOT)
        v = floorf(v + 0.5f);
    if (v < w->min && !w->log) v = w->min;
    if (v > w->max) v = w->max;
    if (w->log && v < w->min) v = w->min;
    p->shown = wi;
    p->dirty = 1;
    if (p->val[w->port] != v) {
        p->val[w->port] = v;
        if (p->write)
            p->write(p->host, w->port, v);
    }
}

static int select_row_at(const Widget* w, float y)
{
    const int row = (int)floorf((y - w->y) / SEL_STEP + 0.5f);
    return row < 0 ? 0 : (row >= N_DIODES ? N_DIODES - 1 : row);
}

static int hit(float x, float y)
{
    int i;
    for (i = 0; i < N_WIDGETS; ++i) {
        const Widget* w = &WIDGETS[i];
        const float dx = x - w->x, dy = y - w->y;
        switch (w->kind) {
        case W_KNOB:
        case W_TRIM:
        case W_FOOT:
            if (dx * dx + dy * dy <= (w->r + 8) * (w->r + 8))
                return i;
            break;
        case W_SELECT:
            if (x >= SEL_X0 && x <= SEL_X1 && dy >= -0.5f * SEL_STEP && dy <= (N_DIODES - 0.5f) * SEL_STEP)
                return i;
            break;
        }
    }
    return -1;
}

static void panel_mouse_down(Panel* p, float x, float y, int dbl)
{
    const int i = hit(x, y);
    const Widget* w;
    if (i < 0)
        return;
    w = &WIDGETS[i];
    switch (w->kind) {
    case W_KNOB:
    case W_TRIM:
        if (dbl) {
            set_value(p, i, w->def);
            return;
        }
        p->drag = i;
        p->drag_y = y;
        p->drag_norm = w_norm(w, p->val[w->port]);
        p->shown = i;
        p->dirty = 1;
        break;
    case W_SELECT:
        set_value(p, i, (float)select_row_at(w, y));
        break;
    case W_FOOT:
        set_value(p, i, w_index(p, w) ? 0.0f : 1.0f);
        break;
    }
}

static void panel_mouse_move(Panel* p, float x, float y, int fine)
{
    const Widget* w;
    (void)x;
    if (p->drag < 0)
        return;
    w = &WIDGETS[p->drag];
    {
        const float n = p->drag_norm + (p->drag_y - y) / (fine ? 900.0f : 180.0f);
        /* Fine mode needs a fresh reference so switching modes mid-drag doesn't jump. */
        set_value(p, p->drag, w_value(w, n));
        p->drag_norm = w_norm(w, p->val[w->port]);
        p->drag_y = y;
    }
}

static void panel_mouse_up(Panel* p)
{
    if (p->drag >= 0)
        p->dirty = 1;
    p->drag = -1;
}

static void panel_wheel(Panel* p, float x, float y, float steps, int fine)
{
    const int i = hit(x, y);
    const Widget* w;
    if (i < 0)
        return;
    w = &WIDGETS[i];
    if (w->kind == W_SELECT) {
        /* Wheel up moves up the list. */
        int n = w_index(p, w) - (steps > 0 ? 1 : -1);
        set_value(p, i, (float)(n < 0 ? 0 : (n >= N_DIODES ? N_DIODES - 1 : n)));
    } else if (w->kind != W_FOOT) {
        set_value(p, i, w_value(w, w_norm(w, p->val[w->port]) + steps * (fine ? 0.004f : 0.025f)));
    }
}

#endif
