/* SPDX-License-Identifier: GPL-3.0-or-later */
/* LV2 entry points: controls, oversampling, bypass and stage order. */
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "denormal.h"

#include "lv2_abi.h"
#include "ports.h"
#include "input_hp.h"
#include "gain_stage.h"
#include "clipper.h"
#include "oversampler.h"

#define SMOOTH_MS 20.0
#define CLIP_RELEASE_MS 150.0 /* how long the GUI's clip lamp takes to fade */
#define BYPASS_MS 10.0
#define DC_BLOCK_HZ 5.0      /* stands in for the next device's input coupling */
#define COEF_UPDATE_EVERY 32 /* oversampled samples between filter updates */
#define INPUT_LIMIT_VOLTS 100.0

typedef struct {
    const float *in;
    float *out;
    const float *distortion, *output, *diode, *input_volts, *enabled;
    float *latency, *clip_out;

    double rate;
    Oversampler os;
    OnePoleHP input_hp;
    GainStage gain;
    Clipper clip;
    OnePoleHP dc_block;

    float dry[OVERSAMPLE_LATENCY];
    int dry_pos;

    int fresh; /* jump straight to the port values on the first run */
    int coef_count;
    int diode_type;
    double input_scale;
    double smooth_base, smooth_os, bypass_step;
    double dist_s, dist_applied, vol_s, volts_s, mix;
    double clip_level;
} Plugin;

static float read_port(const float *port, float fallback, float lo, float hi)
{
    float v = port ? *port : fallback;
    if (!(v == v))
        return fallback;
    return v < lo ? lo : (v > hi ? hi : v);
}

static void reset_state(Plugin *p)
{
    double fos = p->rate * p->os.factor;
    oversampler_reset(&p->os);
    onepole_hp_init(&p->input_hp, input_corner_hz(), fos);
    gain_stage_init(&p->gain, fos);
    clipper_init(&p->clip, fos);
    onepole_hp_init(&p->dc_block, DC_BLOCK_HZ, p->rate);
    memset(p->dry, 0, sizeof(p->dry));
    p->dry_pos = 0;
    p->coef_count = 0;
    p->clip_level = 0.0;
    p->fresh = 1;
}

static LV2_Handle instantiate(const LV2_Descriptor *desc, double rate,
                              const char *bundle, const LV2_Feature *const *features)
{
    Plugin *p = (Plugin *)calloc(1, sizeof(Plugin));
    int factor = rate <= 50000.0 ? 8 : (rate <= 100000.0 ? 4 : 2);
    (void)desc;
    (void)bundle;
    (void)features;
    if (!p)
        return NULL;
    p->rate = rate;
    if (!oversampler_init(&p->os, factor)) {
        oversampler_free(&p->os);
        free(p);
        return NULL;
    }
    p->input_scale = input_divider();
    p->smooth_base = 1.0 - exp(-1.0 / (0.001 * SMOOTH_MS * rate));
    p->smooth_os = 1.0 - exp(-1.0 / (0.001 * SMOOTH_MS * rate * factor));
    p->bypass_step = 1.0 / (0.001 * BYPASS_MS * rate);
    reset_state(p);
    return p;
}

static void connect_port(LV2_Handle h, uint32_t port, void *data)
{
    Plugin *p = (Plugin *)h;
    switch (port) {
    case PORT_IN:          p->in = (const float *)data; break;
    case PORT_OUT:         p->out = (float *)data; break;
    case PORT_DISTORTION:  p->distortion = (const float *)data; break;
    case PORT_OUTPUT:      p->output = (const float *)data; break;
    case PORT_DIODE:       p->diode = (const float *)data; break;
    case PORT_INPUT_VOLTS: p->input_volts = (const float *)data; break;
    case PORT_ENABLED:     p->enabled = (const float *)data; break;
    case PORT_LATENCY:     p->latency = (float *)data; break;
    case PORT_CLIP:        p->clip_out = (float *)data; break;
    default: break;
    }
}

static void activate(LV2_Handle h)
{
    reset_state((Plugin *)h);
}

static void run(LV2_Handle h, uint32_t n_samples)
{
    Plugin *p = (Plugin *)h;
    const float *in = p->in;
    float *out = p->out;
    const int factor = p->os.factor;
    /* Flush denormals to zero while processing, then restore the host's mode. */
    const FpState saved_fp = denormals_off();
    double dist_target, vol_target, volts_target, mix_target;
    double clip_peak = 0.0;
    int diode_type;
    uint32_t i;

    if (p->latency)
        *p->latency = (float)OVERSAMPLE_LATENCY;

    dist_target = read_port(p->distortion, 0.5f, 0.0f, 1.0f);
    vol_target = audio_taper(read_port(p->output, 0.8f, 0.0f, 1.0f));
    volts_target = read_port(p->input_volts, 1.0f, 0.1f, 10.0f);
    mix_target = read_port(p->enabled, 1.0f, 0.0f, 1.0f) > 0.5f ? 1.0 : 0.0;
    diode_type = (int)(read_port(p->diode, 0.0f, 0.0f, DIODE_COUNT - 1.0f) + 0.5f);

    if (p->fresh) {
        p->dist_s = dist_target;
        p->dist_applied = dist_target;
        p->vol_s = vol_target;
        p->volts_s = volts_target;
        p->mix = mix_target;
        p->diode_type = -1;
        gain_stage_set_rg(&p->gain, distortion_to_rg(dist_target));
        p->fresh = 0;
    }
    if (diode_type != p->diode_type) {
        clipper_set_diode(&p->clip, diode_type);
        p->diode_type = diode_type;
    }

    if (!in || !out) {
        if (p->clip_out)
            *p->clip_out = 0.0f;
        denormals_restore(saved_fp);
        return;
    }

    for (i = 0; i < n_samples; i++) {
        float raw = in[i];
        float dry_out;
        double volts, wet, result;
        int phase;

        if (!isfinite(raw))
            raw = 0.0f;

        dry_out = p->dry[p->dry_pos];
        p->dry[p->dry_pos] = raw;
        p->dry_pos = (p->dry_pos + 1) % OVERSAMPLE_LATENCY;

        p->vol_s += (vol_target - p->vol_s) * p->smooth_base;
        p->volts_s += (volts_target - p->volts_s) * p->smooth_base;

        volts = (double)raw * p->volts_s;
        if (volts > INPUT_LIMIT_VOLTS)
            volts = INPUT_LIMIT_VOLTS;
        if (volts < -INPUT_LIMIT_VOLTS)
            volts = -INPUT_LIMIT_VOLTS;

        oversampler_push_input(&p->os, volts);
        wet = 0.0;
        for (phase = 0; phase < factor; phase++) {
            double x;

            p->dist_s += (dist_target - p->dist_s) * p->smooth_os;
            if (++p->coef_count >= COEF_UPDATE_EVERY) {
                p->coef_count = 0;
                if (fabs(p->dist_s - p->dist_applied) > 1e-5) {
                    p->dist_applied = p->dist_s;
                    gain_stage_set_rg(&p->gain, distortion_to_rg(p->dist_s));
                }
            }

            x = oversampler_interpolate(&p->os, phase);
            x = onepole_hp_process(&p->input_hp, x) * p->input_scale;
            x = gain_stage_process(&p->gain, x);
            x = clipper_process(&p->clip, x);
            {
                const double amount = clipper_clip_amount(&p->clip) * p->mix;
                if (amount > clip_peak)
                    clip_peak = amount;
            }
            oversampler_push_output(&p->os, x);
            /* Taking the output at phase 0 keeps the total delay a whole
             * number of base-rate samples. */
            if (phase == 0)
                wet = oversampler_decimate(&p->os);
        }

        wet = onepole_hp_process(&p->dc_block, wet);
        wet = wet * p->vol_s / p->volts_s;

        if (!isfinite(wet)) {
            double mix = p->mix;
            double dist = p->dist_s;
            reset_state(p);
            p->fresh = 0;
            p->mix = mix;
            p->dist_s = p->dist_applied = dist;
            gain_stage_set_rg(&p->gain, distortion_to_rg(dist));
            clipper_set_diode(&p->clip, p->diode_type);
            wet = 0.0;
        }

        if (p->mix < mix_target) {
            p->mix += p->bypass_step;
            if (p->mix > mix_target)
                p->mix = mix_target;
        } else if (p->mix > mix_target) {
            p->mix -= p->bypass_step;
            if (p->mix < mix_target)
                p->mix = mix_target;
        }

        result = (p->mix >= 1.0) ? wet
               : (p->mix <= 0.0) ? (double)dry_out
                                 : (double)dry_out + (wet - (double)dry_out) * p->mix;
        out[i] = (float)result;
    }

    /* Peak hold with a release, so short bursts still show on the lamp. */
    p->clip_level *= exp(-(double)n_samples / (0.001 * CLIP_RELEASE_MS * p->rate));
    if (clip_peak > p->clip_level)
        p->clip_level = clip_peak;
    if (p->clip_out)
        *p->clip_out = (float)(p->clip_level > 1.0 ? 1.0 : p->clip_level);

    denormals_restore(saved_fp);
}

static void deactivate(LV2_Handle h)
{
    (void)h;
}

static void cleanup(LV2_Handle h)
{
    Plugin *p = (Plugin *)h;
    oversampler_free(&p->os);
    free(p);
}

static const void *extension_data(const char *uri)
{
    (void)uri;
    return NULL;
}

static const LV2_Descriptor descriptor = {
    DP_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

LV2_EXPORT const LV2_Descriptor *lv2_descriptor(uint32_t index)
{
    return index == 0 ? &descriptor : NULL;
}
