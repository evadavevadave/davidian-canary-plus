/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Linear-phase FIR interpolator and decimator sharing one Kaiser-windowed sinc.
 *
 * The prototype has 32*factor + 1 taps, so each filter delays by 16 base-rate
 * samples and the pair adds exactly OVERSAMPLE_LATENCY samples at any factor. */
#ifndef OVERSAMPLER_H
#define OVERSAMPLER_H

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "input_hp.h"

#define OVERSAMPLE_LATENCY 32
#define OVERSAMPLE_UP_LEN 33
#define OVERSAMPLE_KAISER_BETA 9.0

typedef struct {
    int factor;
    int n_taps;
    double *taps;  /* factor * OVERSAMPLE_UP_LEN entries, zero padded */
    double up[OVERSAMPLE_UP_LEN * 2];
    int up_pos;
    double *down;  /* n_taps * 2 entries, mirrored for contiguous reads */
    int down_pos;
} Oversampler;

static double bessel_i0(double x)
{
    double sum = 1.0, term = 1.0;
    int k;
    for (k = 1; k < 64; k++) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-14 * sum)
            break;
    }
    return sum;
}

static int oversampler_init(Oversampler *o, int factor)
{
    const int n_taps = 32 * factor + 1;
    const double centre = 0.5 * (n_taps - 1);
    const double fc = 0.5 / factor; /* base-rate Nyquist, cycles per sample */
    double sum = 0.0;
    int n;

    memset(o, 0, sizeof(*o));
    o->factor = factor;
    o->n_taps = n_taps;
    o->taps = (double *)calloc((size_t)factor * OVERSAMPLE_UP_LEN, sizeof(double));
    o->down = (double *)calloc((size_t)n_taps * 2, sizeof(double));
    if (!o->taps || !o->down)
        return 0;

    for (n = 0; n < n_taps; n++) {
        double m = n - centre;
        double r = m / centre;
        double sinc = (m == 0.0) ? 2.0 * fc : sin(2.0 * DP_PI * fc * m) / (DP_PI * m);
        double win = bessel_i0(OVERSAMPLE_KAISER_BETA * sqrt(1.0 - r * r)) /
                     bessel_i0(OVERSAMPLE_KAISER_BETA);
        o->taps[n] = sinc * win;
        sum += o->taps[n];
    }
    for (n = 0; n < n_taps; n++)
        o->taps[n] /= sum;
    return 1;
}

static void oversampler_free(Oversampler *o)
{
    free(o->taps);
    free(o->down);
    o->taps = o->down = NULL;
}

static void oversampler_reset(Oversampler *o)
{
    memset(o->up, 0, sizeof(o->up));
    memset(o->down, 0, (size_t)o->n_taps * 2 * sizeof(double));
    o->up_pos = 0;
    o->down_pos = 0;
}

static inline void oversampler_push_input(Oversampler *o, double x)
{
    o->up_pos = (o->up_pos == 0) ? OVERSAMPLE_UP_LEN - 1 : o->up_pos - 1;
    o->up[o->up_pos] = x;
    o->up[o->up_pos + OVERSAMPLE_UP_LEN] = x;
}

/* Interpolated sample for one phase of the newest input. */
static inline double oversampler_interpolate(const Oversampler *o, int phase)
{
    const double *x = o->up + o->up_pos;
    const double *h = o->taps + phase;
    const int step = o->factor;
    double y = 0.0;
    int k;
    for (k = 0; k < OVERSAMPLE_UP_LEN; k++)
        y += h[k * step] * x[k];
    return y * step;
}

static inline void oversampler_push_output(Oversampler *o, double x)
{
    o->down_pos = (o->down_pos == 0) ? o->n_taps - 1 : o->down_pos - 1;
    o->down[o->down_pos] = x;
    o->down[o->down_pos + o->n_taps] = x;
}

static inline double oversampler_decimate(const Oversampler *o)
{
    const double *x = o->down + o->down_pos;
    double y = 0.0;
    int k;
    for (k = 0; k < o->n_taps; k++)
        y += o->taps[k] * x[k];
    return y;
}

#endif
