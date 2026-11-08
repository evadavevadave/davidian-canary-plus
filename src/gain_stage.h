/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Stage 2: 741 non-inverting gain stage.
 *
 *   H(s) = 1 + Zf/Zg
 *   Zf = 1M || 10pF      Zg = Rg + 1/(s 0.047uF)      Rg = 4.7k + pot
 *
 * followed by a low-pass for the 741's finite gain-bandwidth, its slew-rate
 * limit, then a soft limit for the output swing. */
#ifndef GAIN_STAGE_H
#define GAIN_STAGE_H

#include <math.h>
#include "input_hp.h"

#define GAIN_RF  1e6
#define GAIN_CF  10e-12
#define GAIN_CG  47e-9
#define GAIN_R0  4.7e3
#define GAIN_POT 1e6

/* Not on the schematic: typical 741 figures on a 9V single supply. */
#define OPAMP_GBW_HZ 1e6
#define OPAMP_SLEW_V_PER_S 0.5e6
#define OPAMP_VSAT   3.0
#define OPAMP_VKNEE  2.5

typedef struct {
    double b0, b1, b2, a1, a2, z1, z2;
    double lp_g, lp_z;
    double slew_step;
    double fs;
} GainStage;

static inline double gain_gbw_corner_hz(double rg)
{
    return OPAMP_GBW_HZ / (1.0 + GAIN_RF / rg);
}

static void gain_stage_set_rg(GainStage *s, double rg)
{
    const double a = GAIN_RF * GAIN_CF;
    const double b = rg * GAIN_CG;
    const double k = 2.0 * s->fs;
    const double k2 = k * k;
    const double d1 = a + b;
    const double d2 = a * b;
    const double n1 = a + b + GAIN_RF * GAIN_CG;
    const double n2 = a * b;
    const double a0 = 1.0 + d1 * k + d2 * k2;
    double fc, w;

    s->b0 = (1.0 + n1 * k + n2 * k2) / a0;
    s->b1 = (2.0 - 2.0 * n2 * k2) / a0;
    s->b2 = (1.0 - n1 * k + n2 * k2) / a0;
    s->a1 = (2.0 - 2.0 * d2 * k2) / a0;
    s->a2 = (1.0 - d1 * k + d2 * k2) / a0;

    fc = gain_gbw_corner_hz(rg);
    if (fc > 0.4 * s->fs)
        fc = 0.4 * s->fs;
    w = tan(DP_PI * fc / s->fs);
    s->lp_g = w / (1.0 + w);
}

static void gain_stage_init(GainStage *s, double fs)
{
    s->fs = fs;
    s->z1 = s->z2 = s->lp_z = 0.0;
    s->slew_step = OPAMP_SLEW_V_PER_S / fs;
    gain_stage_set_rg(s, GAIN_R0 + GAIN_POT);
}

/* Linear up to the knee, then eases into VSAT with a continuous slope. */
static inline double opamp_limit(double x)
{
    const double span = OPAMP_VSAT - OPAMP_VKNEE;
    double m = fabs(x);
    double y;
    if (m <= OPAMP_VKNEE)
        return x;
    y = OPAMP_VKNEE + span * tanh((m - OPAMP_VKNEE) / span);
    return x < 0.0 ? -y : y;
}

static inline double gain_stage_process(GainStage *s, double x)
{
    double y = s->b0 * x + s->z1;
    double v;
    s->z1 = s->b1 * x - s->a1 * y + s->z2;
    s->z2 = s->b2 * x - s->a2 * y;

    /* One integrator models both limits, as in the real part: small signals see
     * the gain-bandwidth low-pass, and the tanh caps how fast it can move. A
     * hard slew clamp here aliases badly. The integrator advances by about 2v
     * per sample, hence the half step. */
    v = (y - s->lp_z) * s->lp_g;
    {
        const double cap = 0.5 * s->slew_step;
        if (fabs(v) > 0.05 * cap)
            v = cap * tanh(v / cap);
    }
    y = v + s->lp_z;
    s->lp_z = y + v;

    return opamp_limit(y);
}

#endif
