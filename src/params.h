/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The plugin's parameters as the VST3 and Audio Unit versions show them to hosts.
 * Ranges, defaults and names match bundle/canary-plus.ttl. Each parameter drives
 * one LV2 port of the plugin in plugin.c, which all three formats share. */
#ifndef PARAMS_H
#define PARAMS_H

#include <math.h>
#include "ports.h"

typedef enum {
    PK_LINEAR, /* continuous, min to max */
    PK_LOG,    /* continuous on a logarithmic scale */
    PK_LIST,   /* whole numbers min to max, named by DIODE_NAMES */
    PK_TOGGLE, /* 0 or 1 */
    PK_METER   /* read-only output for the GUI */
} ParamKind;

typedef struct {
    int port;
    const char* name;
    const char* unit;
    float min, max, def;
    ParamKind kind;
} ParamSpec;

/* The array index is the parameter ID in both formats, so never reorder it:
 * saved sessions and automation refer to these numbers. */
enum { P_DISTORTION, P_OUTPUT, P_DIODE, P_INPUT_VOLTS, P_ENGAGE, P_CLIP, P_COUNT };

static const ParamSpec PARAMS[P_COUNT] = {
    { PORT_DISTORTION, "Distortion", "", 0.0f, 1.0f, 0.5f, PK_LINEAR },
    { PORT_OUTPUT, "Output", "", 0.0f, 1.0f, 0.8f, PK_LINEAR },
    { PORT_DIODE, "Diodes", "", 0.0f, 4.0f, 0.0f, PK_LIST },
    { PORT_INPUT_VOLTS, "Volts at 0 dBFS", "V", 0.1f, 10.0f, 1.0f, PK_LOG },
    { PORT_ENABLED, "Engage", "", 0.0f, 1.0f, 1.0f, PK_TOGGLE },
    { PORT_CLIP, "Clip", "", 0.0f, 1.0f, 0.0f, PK_METER },
};

#define DIODE_COUNT_NAMES 5
static const char* const DIODE_NAMES[DIODE_COUNT_NAMES] = {
    "1N270 germanium (original)",
    "1N34 germanium (fuzzy)",
    "1N4148 silicon (buzz)",
    "LED (crunch)",
    "1N34 array (asymmetric)",
};

/* Parameter for an LV2 port, or -1. */
static inline int param_for_port(int port)
{
    int i;
    for (i = 0; i < P_COUNT; ++i)
        if (PARAMS[i].port == port)
            return i;
    return -1;
}

static inline double param_clamp(int id, double v)
{
    const ParamSpec* s = &PARAMS[id];
    if (!(v == v))
        return s->def;
    if (s->kind == PK_LIST || s->kind == PK_TOGGLE)
        v = floor(v + 0.5);
    return v < s->min ? s->min : (v > s->max ? s->max : v);
}

/* Host-normalised 0-1 value to the plugin's value, and back. */
static inline double param_from_normal(int id, double n)
{
    const ParamSpec* s = &PARAMS[id];
    n = n < 0.0 ? 0.0 : (n > 1.0 ? 1.0 : n);
    if (s->kind == PK_LOG)
        return s->min * pow((double)s->max / s->min, n);
    return param_clamp(id, s->min + n * (s->max - s->min));
}

static inline double param_to_normal(int id, double v)
{
    const ParamSpec* s = &PARAMS[id];
    v = param_clamp(id, v);
    if (s->kind == PK_LOG)
        return log(v / s->min) / log((double)s->max / s->min);
    return (v - s->min) / (s->max - s->min);
}

#endif
