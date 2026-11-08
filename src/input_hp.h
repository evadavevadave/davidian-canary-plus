/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Stage 1: input coupling. 0.01uF into 10k + 1M bias.
 * High-pass at about 15.8 Hz with a small 1M / 1.01M divider loss.
 * The 0.001uF shunt cap is omitted: it only matters with a real pickup's
 * source impedance, which a plugin doesn't see. */
#ifndef INPUT_HP_H
#define INPUT_HP_H

#include <math.h>

#define DP_PI 3.14159265358979323846

typedef struct {
    double g; /* TPT integrator coefficient */
    double z;
} OnePoleHP;

static void onepole_hp_init(OnePoleHP *f, double fc, double fs)
{
    double w = tan(DP_PI * fc / fs);
    f->g = w / (1.0 + w);
    f->z = 0.0;
}

static inline double onepole_hp_process(OnePoleHP *f, double x)
{
    double v = (x - f->z) * f->g;
    double lp = v + f->z;
    f->z = lp + v;
    return x - lp;
}

#define INPUT_R_SERIES 10e3
#define INPUT_R_BIAS   1e6
#define INPUT_C        10e-9

static inline double input_corner_hz(void)
{
    return 1.0 / (2.0 * DP_PI * (INPUT_R_SERIES + INPUT_R_BIAS) * INPUT_C);
}

static inline double input_divider(void)
{
    return INPUT_R_BIAS / (INPUT_R_SERIES + INPUT_R_BIAS);
}

#endif
