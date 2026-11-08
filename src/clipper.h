/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Stages 3 and 4: output coupling and diode clipper.
 *
 *   Vin --[1uF]--[10k]--+--> V (top of the 10k volume pot)
 *                       |
 *            D1/D2 || 0.001uF || 10k pot, all to ground
 *
 * Both capacitors use trapezoidal companion models and the node equation
 *
 *   A*V + Id(V) = B
 *
 * is solved each sample with Newton's method, kept inside a bracket so it
 * can't diverge. Keeping the 1uF cap in this solve, rather than as a separate
 * high-pass, means asymmetric diodes shift its charge as they do in the circuit.
 */
#ifndef CLIPPER_H
#define CLIPPER_H

#include <math.h>

#define CLIP_R1 10e3
#define CLIP_C1 1e-6
#define CLIP_C2 1e-9
#define CLIP_RP 10e3

/* Is and n are starting values from typical parts, to be tuned by ear or
 * against a real pedal. The schematic doesn't specify them. */
typedef struct {
    double is;
    double nvt;  /* emission coefficient * thermal voltage */
    int n_fwd;   /* diodes in series, positive direction */
    int n_rev;   /* diodes in series, negative direction */
} DiodeModel;

#define DIODE_VT 0.02585

enum { DIODE_1N270, DIODE_1N34, DIODE_1N4148, DIODE_LED, DIODE_1N34_ARRAY, DIODE_COUNT };

static const DiodeModel DIODE_MODELS[DIODE_COUNT] = {
    { 2.0e-7,  1.30 * DIODE_VT, 1, 1 },
    { 2.6e-6,  1.60 * DIODE_VT, 1, 1 },
    { 2.52e-9, 1.752 * DIODE_VT, 1, 1 },
    { 1.0e-18, 1.90 * DIODE_VT, 1, 1 },
    { 2.6e-6,  1.60 * DIODE_VT, 2, 1 },
};

typedef struct {
    double g1c, gc2, gs, a;
    double h1, h2, v;
    double id;         /* diode current at the last solve */
    double share_idle; /* diode share of the node current for a tiny signal */
    const DiodeModel *diode;
    double vt_fwd, vt_rev;
} Clipper;

static void clipper_set_diode(Clipper *c, int type)
{
    if (type < 0)
        type = 0;
    if (type >= DIODE_COUNT)
        type = DIODE_COUNT - 1;
    c->diode = &DIODE_MODELS[type];
    c->vt_fwd = c->diode->nvt * c->diode->n_fwd;
    c->vt_rev = c->diode->nvt * c->diode->n_rev;
    {
        const double g0 = c->diode->is / c->vt_fwd + c->diode->is / c->vt_rev;
        c->share_idle = g0 / (g0 + 1.0 / CLIP_RP);
    }
}

/* 0 while the diodes behave like their small-signal resistance, rising to 1 as they
 * take nearly all the current, so a germanium pair doesn't read as clipping at idle. */
static inline double clipper_clip_amount(const Clipper *c)
{
    const double rest = fabs(c->v) / CLIP_RP;
    const double d = fabs(c->id);
    double share, amount;
    if (d + rest < 1e-12)
        return 0.0;
    share = d / (d + rest);
    amount = (share - c->share_idle) / (1.0 - c->share_idle);
    return amount < 0.0 ? 0.0 : amount;
}

static void clipper_init(Clipper *c, double fs)
{
    c->g1c = 2.0 * CLIP_C1 * fs;
    c->gc2 = 2.0 * CLIP_C2 * fs;
    c->gs = 1.0 / (CLIP_R1 + 1.0 / c->g1c);
    c->a = c->gs + c->gc2 + 1.0 / CLIP_RP;
    c->h1 = c->h2 = c->v = c->id = 0.0;
    clipper_set_diode(c, DIODE_1N270);
}

static inline double clamped_exp(double x)
{
    if (x > 300.0)
        x = 300.0;
    return exp(x);
}

/* Diode current and its slope at voltage v. */
static inline void clipper_diode(const Clipper *c, double v, double *id, double *gd)
{
    const double is = c->diode->is;
    double ef = clamped_exp(v / c->vt_fwd);
    /* A symmetric pair only needs one exp(). */
    double er = (c->diode->n_fwd == c->diode->n_rev) ? 1.0 / ef : clamped_exp(-v / c->vt_rev);
    *id = is * (ef - er);
    *gd = is * (ef / c->vt_fwd + er / c->vt_rev);
}

static inline double clipper_process(Clipper *c, double vin)
{
    const double bias = c->h1 / c->g1c;
    const double b = c->gs * (vin - bias) + c->h2;
    /* Id has the same sign as V, so the solution lies between 0 and b/a. */
    const double linear = b / c->a;
    double lo = linear < 0.0 ? linear : 0.0;
    double hi = linear < 0.0 ? 0.0 : linear;
    double v = c->v;
    double i;
    int iter;

    if (v < lo)
        v = lo;
    if (v > hi)
        v = hi;

    for (iter = 0; iter < 24; iter++) {
        double id, gd, f, next;
        clipper_diode(c, v, &id, &gd);
        c->id = id;
        f = c->a * v + id - b;
        if (f > 0.0)
            hi = v;
        else
            lo = v;
        next = v - f / (c->a + gd);
        if (next <= lo || next >= hi)
            next = 0.5 * (lo + hi);
        if (fabs(next - v) < 1e-8) {
            v = next;
            break;
        }
        v = next;
    }

    i = c->gs * (vin - v - bias);
    c->h1 += 2.0 * i;
    c->h2 = 2.0 * c->gc2 * v - c->h2;
    c->v = v;
    return v;
}

#endif
