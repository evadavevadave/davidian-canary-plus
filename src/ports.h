/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Port indices and knob curves, shared by the plugin and its GUI.
 * The indices must match bundle/canary-plus.ttl. */
#ifndef PORTS_H
#define PORTS_H

#include <math.h>

#define DP_URI    "urn:davidian:canary-plus"
#define DP_UI_URI "urn:davidian:canary-plus#ui"

enum {
    PORT_IN,
    PORT_OUT,
    PORT_DISTORTION,
    PORT_OUTPUT,
    PORT_DIODE,
    PORT_INPUT_VOLTS,
    PORT_ENABLED,
    PORT_LATENCY,
    PORT_CLIP, /* output: how hard the diodes are clipping, 0-1, for the GUI lamp */
    PORT_COUNT
};

/* The schematic doesn't mark pot tapers; this assumes audio taper, 10% at the
 * midpoint. Change TAPER_BASE to reshape both knobs. */
#define TAPER_BASE 81.0

static inline double audio_taper(double x)
{
    return (pow(TAPER_BASE, x) - 1.0) / (TAPER_BASE - 1.0);
}

/* Gain-stage resistance to ground: 4.7k plus the 1M Distortion pot, which loses
 * resistance as the knob turns up. Matches GAIN_R0 and GAIN_POT in gain_stage.h. */
static inline double distortion_to_rg(double knob)
{
    return 4.7e3 + 1e6 * audio_taper(1.0 - knob);
}

/* Mid-band gain of the 741 stage in dB, for the GUI readout. */
static inline double distortion_gain_db(double knob)
{
    return 20.0 * log10(1.0 + 1e6 / distortion_to_rg(knob));
}

#endif
