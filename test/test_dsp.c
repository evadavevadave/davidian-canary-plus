/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Loads the built plugin library through its LV2 entry point and checks its behaviour.
 * Usage: test_dsp <bundle directory> */
#if defined(_WIN32)
#    define _CRT_SECURE_NO_WARNINGS
#elif !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/lv2_abi.h"
#include "platform.h"

#define PI 3.14159265358979323846
#define LATENCY 32

enum { P_IN, P_OUT, P_DIST, P_VOL, P_DIODE, P_VOLTS, P_ENABLED, P_LATENCY, P_CLIP };
enum { D_1N270, D_1N34, D_1N4148, D_LED, D_ARRAY };

static const LV2_Descriptor *g_desc;
static int g_fail;

typedef struct {
    LV2_Handle h;
    float dist, vol, diode, volts, enabled, latency, clip;
} Inst;

static void check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "pass" : "FAIL", what);
    if (!ok)
        g_fail++;
}

static void inst_open(Inst *s, double rate, float dist, float vol, float diode)
{
    s->h = g_desc->instantiate(g_desc, rate, ".", NULL);
    s->dist = dist;
    s->vol = vol;
    s->diode = diode;
    s->volts = 1.0f;
    s->enabled = 1.0f;
    s->latency = -1.0f;
    g_desc->connect_port(s->h, P_DIST, &s->dist);
    g_desc->connect_port(s->h, P_VOL, &s->vol);
    g_desc->connect_port(s->h, P_DIODE, &s->diode);
    g_desc->connect_port(s->h, P_VOLTS, &s->volts);
    g_desc->connect_port(s->h, P_ENABLED, &s->enabled);
    g_desc->connect_port(s->h, P_LATENCY, &s->latency);
    s->clip = -1.0f;
    g_desc->connect_port(s->h, P_CLIP, &s->clip);
    g_desc->activate(s->h);
}

static void inst_close(Inst *s)
{
    g_desc->deactivate(s->h);
    g_desc->cleanup(s->h);
}

static void inst_run(Inst *s, const float *in, float *out, size_t n, size_t block)
{
    size_t done = 0;
    while (done < n) {
        size_t now = n - done < block ? n - done : block;
        g_desc->connect_port(s->h, P_IN, (void *)(in + done));
        g_desc->connect_port(s->h, P_OUT, out + done);
        g_desc->run(s->h, (uint32_t)now);
        done += now;
    }
}

static float *make_sine(size_t n, double f, double rate, double amp)
{
    float *x = (float *)malloc(n * sizeof(float));
    size_t i;
    for (i = 0; i < n; i++)
        x[i] = (float)(amp * sin(2.0 * PI * f * (double)i / rate));
    return x;
}

/* Magnitude of the component at f, as a peak amplitude. */
static double tone_level(const float *x, size_t n, double f, double rate)
{
    double re = 0.0, im = 0.0;
    size_t i;
    for (i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * cos(2.0 * PI * (double)i / (double)n); /* Hann */
        double p = 2.0 * PI * f * (double)i / rate;
        re += w * x[i] * cos(p);
        im += w * x[i] * sin(p);
    }
    return 4.0 * sqrt(re * re + im * im) / (double)n;
}

static double db(double x)
{
    return 20.0 * log10(x > 1e-30 ? x : 1e-30);
}

typedef struct { double re, im; } C;
static C c_mul(C a, C b) { C r = { a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re }; return r; }
static C c_div(C a, C b)
{
    double d = b.re * b.re + b.im * b.im;
    C r = { (a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d };
    return r;
}
static double c_abs(C a) { return sqrt(a.re * a.re + a.im * a.im); }

/* Expected small-signal gain from the schematic's values, worked out separately
 * from the plugin's discrete-time filters. rg is 4.7k + the pot's resistance. */
static double expected_gain(double f, double rg, double diode_g)
{
    double w = 2.0 * PI * f;
    C s = { 0.0, w };
    C one = { 1.0, 0.0 };
    C h1, hgain, hgbw, hnode, hdc, zf, zg, ynode, zseries, r;
    double fgbw, wdc = 2.0 * PI * 5.0, w1 = 1.0 / (1.01e6 * 10e-9);

    /* Input: 0.01uF into 10k + 1M, output taken across the 1M */
    h1.re = 0.0; h1.im = w;
    { C d = { w1, w }; h1 = c_div(h1, d); }
    h1.re *= 1e6 / 1.01e6; h1.im *= 1e6 / 1.01e6;

    /* Op-amp: 1 + Zf/Zg */
    { C d = { 1.0, w * 1e6 * 10e-12 }; zf = c_div((C){ 1e6, 0.0 }, d); }
    { C c = { 0.0, w * 47e-9 }; C i = c_div(one, c); zg.re = rg + i.re; zg.im = i.im; }
    hgain = c_div(zf, zg);
    hgain.re += 1.0;

    /* Gain-bandwidth pole */
    fgbw = 1e6 / (1.0 + 1e6 / rg);
    { C d = { 1.0, f / fgbw }; hgbw = c_div(one, d); }

    /* Node: series 1uF + 10k into 10k || 0.001uF || diode conductance */
    ynode.re = 1.0 / 10e3 + diode_g;
    ynode.im = w * 1e-9;
    { C c = { 0.0, w * 1e-6 }; C i = c_div(one, c); zseries.re = 10e3 + i.re; zseries.im = i.im; }
    { C d = c_mul(zseries, ynode); d.re += 1.0; hnode = c_div(one, d); }

    /* Output DC blocker */
    { C d = { wdc, w }; hdc = c_div(s, d); }

    r = c_mul(c_mul(c_mul(c_mul(h1, hgain), hgbw), hnode), hdc);
    return c_abs(r);
}

static void test_descriptor(void)
{
    Inst s;
    float in[8] = { 0 }, out[8];
    printf("Entry point\n");
    check(g_desc && strcmp(g_desc->URI, "urn:davidian:canary-plus") == 0, "descriptor URI");
    inst_open(&s, 48000.0, 0.5f, 1.0f, 0);
    inst_run(&s, in, out, 8, 8);
    check(s.latency == (float)LATENCY, "latency port reports 32 samples");
    inst_close(&s);
}

static void test_response(void)
{
    static const double freqs[] = { 40, 100, 300, 720, 1000, 3000, 5000, 10000 };
    static const struct { float knob; double rg; const char *name; } settings[] = {
        { 1.0f, 4.7e3, "Distortion max (Rg 4.7k)" },
        { 0.0f, 1.0047e6, "Distortion min (Rg 1.0047M)" },
    };
    const double rate = 48000.0;
    const size_t n = (size_t)(rate * 8.0), tail = (size_t)rate * 2;
    double worst = 0.0;
    int a, b;
    printf("Small-signal response against schematic values (1N4148, input 1 mV)\n");
    for (a = 0; a < 2; a++) {
        printf("  %s\n", settings[a].name);
        for (b = 0; b < (int)(sizeof(freqs) / sizeof(freqs[0])); b++) {
            Inst s;
            float *in = make_sine(n, freqs[b], rate, 1e-3);
            float *out = (float *)malloc(n * sizeof(float));
            double measured, expected, err;
            inst_open(&s, rate, settings[a].knob, 1.0f, D_1N4148);
            inst_run(&s, in, out, n, 256);
            measured = tone_level(out + n - tail, tail, freqs[b], rate) / 1e-3;
            expected = expected_gain(freqs[b], settings[a].rg, 0.0);
            err = db(measured) - db(expected);
            if (fabs(err) > worst)
                worst = fabs(err);
            printf("    %6.0f Hz  measured %7.2f dB  expected %7.2f dB  diff %+.3f\n",
                   freqs[b], db(measured), db(expected), err);
            inst_close(&s);
            free(in);
            free(out);
        }
    }
    check(worst < 0.1, "all points within 0.1 dB");
}

static void test_clipping(void)
{
    static const char *names[] = { "1N270", "1N34", "1N4148", "LED", "1N34 array" };
    const double rate = 48000.0;
    const size_t n = (size_t)rate * 3, tail = (size_t)rate;
    double pos[5], neg[5], mean[5];
    int d;
    printf("Clipping level (500 Hz at 1 V, Distortion and Output at max)\n");
    for (d = 0; d < 5; d++) {
        Inst s;
        float *in = make_sine(n, 500.0, rate, 1.0);
        float *out = (float *)malloc(n * sizeof(float));
        size_t i;
        pos[d] = neg[d] = mean[d] = 0.0;
        inst_open(&s, rate, 1.0f, 1.0f, (float)d);
        inst_run(&s, in, out, n, 256);
        for (i = n - tail; i < n; i++) {
            if (out[i] > pos[d]) pos[d] = out[i];
            if (out[i] < neg[d]) neg[d] = out[i];
            mean[d] += out[i];
        }
        mean[d] /= (double)tail;
        printf("    %-11s peaks %+.3f / %+.3f V   mean %+.5f V\n", names[d], pos[d], neg[d], mean[d]);
        inst_close(&s);
        free(in);
        free(out);
    }
    check(pos[D_1N34] < pos[D_1N270] && pos[D_1N270] < pos[D_1N4148] && pos[D_1N4148] < pos[D_LED],
          "clip level rises from 1N34 to 1N270 to 1N4148 to LED");
    check(pos[D_1N270] > 0.15 && pos[D_1N270] < 0.45, "1N270 clips within a germanium range");
    check(pos[D_1N4148] > 0.4 && pos[D_1N4148] < 0.8, "1N4148 clips within a silicon range");
    check(pos[D_LED] < 1.8, "LED peak stays near the op-amp swing limit through the divider");
    /* Hard drive squares the wave and the coupling caps recentre it, so peaks
     * don't show asymmetry. Even harmonics at moderate drive do. */
    {
        static const float types[2] = { D_1N34, D_ARRAY };
        double h2[2];
        int t;
        for (t = 0; t < 2; t++) {
            Inst s;
            float *in = make_sine(n, 200.0, rate, 0.05);
            float *out = (float *)malloc(n * sizeof(float));
            inst_open(&s, rate, 0.5f, 1.0f, types[t]);
            inst_run(&s, in, out, n, 256);
            h2[t] = tone_level(out + n - tail, tail, 400.0, rate) /
                    tone_level(out + n - tail, tail, 200.0, rate);
            inst_close(&s);
            free(in);
            free(out);
        }
        printf("    2nd harmonic at moderate drive: 1N34 %.1f dB, array %.1f dB\n", db(h2[0]), db(h2[1]));
        check(db(h2[1]) > db(h2[0]) + 20.0, "1N34 array adds even harmonics the symmetric pair lacks");
    }
    for (d = 0; d < 5; d++)
        if (fabs(mean[d]) > 1e-3) {
            check(0, "output mean near zero");
            return;
        }
    check(1, "output mean near zero for every diode type");
}

static void test_aliasing(void)
{
    static const struct { double rate; double f; double alias[2]; } cases[] = {
        { 48000.0, 9000.0, { 3000.0, 15000.0 } }, /* 45 kHz->3 kHz, 63 kHz->15 kHz */
    };
    int c;
    printf("Aliasing (1 V tone, Distortion max, 1N270)\n");
    for (c = 0; c < 1; c++) {
        const double rate = cases[c].rate;
        const size_t n = (size_t)rate * 2, tail = (size_t)rate;
        Inst s;
        float *in = make_sine(n, cases[c].f, rate, 1.0);
        float *out = (float *)malloc(n * sizeof(float));
        double fund, a0, a1;
        inst_open(&s, rate, 1.0f, 1.0f, D_1N270);
        inst_run(&s, in, out, n, 256);
        fund = tone_level(out + n - tail, tail, cases[c].f, rate);
        a0 = tone_level(out + n - tail, tail, cases[c].alias[0], rate);
        a1 = tone_level(out + n - tail, tail, cases[c].alias[1], rate);
        printf("    %.0f Hz at %.0f Hz rate: alias at %.0f Hz %.1f dB, at %.0f Hz %.1f dB (re fundamental)\n",
               cases[c].f, rate, cases[c].alias[0], db(a0 / fund), cases[c].alias[1], db(a1 / fund));
        check(db(a0 / fund) < -60.0 && db(a1 / fund) < -60.0, "aliases below -60 dB");
        inst_close(&s);
        free(in);
        free(out);
    }
}

static void test_bypass_and_latency(void)
{
    const double rate = 48000.0;
    const size_t n = 48000;
    Inst s;
    float *in = make_sine(n, 440.0, rate, 0.5);
    float *out = (float *)malloc(n * sizeof(float));
    size_t i, peak = 0;
    int match = 1;
    printf("Bypass and latency\n");

    inst_open(&s, rate, 0.5f, 0.5f, 0);
    s.enabled = 0.0f;
    inst_run(&s, in, out, n, 128);
    for (i = LATENCY; i < n; i++)
        if (out[i] != in[i - LATENCY])
            match = 0;
    check(match, "bypassed output equals input delayed by 32 samples");
    inst_close(&s);

    memset(in, 0, n * sizeof(float));
    in[1000] = 0.01f;
    inst_open(&s, rate, 0.0f, 1.0f, D_1N4148);
    inst_run(&s, in, out, n, 128);
    for (i = 0; i < n; i++)
        if (fabs(out[i]) > fabs(out[peak]))
            peak = i;
    printf("    impulse at sample 1000 peaks at %zu\n", peak);
    check(peak == 1000 + LATENCY, "processed impulse peak matches reported latency");
    inst_close(&s);

    /* The bypass ramp shouldn't jump: compare with a step-free reference tone. */
    {
        float *sine = make_sine(n, 100.0, rate, 0.1);
        double max_step = 0.0;
        inst_open(&s, rate, 0.0f, 1.0f, D_1N4148);
        inst_run(&s, sine, out, 24000, 128);
        s.enabled = 0.0f;
        inst_run(&s, sine + 24000, out + 24000, 24000, 128);
        for (i = 24001; i < 25000; i++) {
            double step = fabs(out[i] - out[i - 1]);
            if (step > max_step)
                max_step = step;
        }
        printf("    largest sample step during bypass ramp %.5f\n", max_step);
        check(max_step < 0.01, "bypass ramp has no large jump");
        inst_close(&s);
        free(sine);
    }
    free(in);
    free(out);
}

static void test_block_invariance(void)
{
    static const size_t blocks[] = { 1, 7, 64, 4096 };
    const double rate = 44100.0;
    const size_t n = 44100;
    float *in = make_sine(n, 220.0, rate, 0.7);
    float *ref = (float *)malloc(n * sizeof(float));
    float *out = (float *)malloc(n * sizeof(float));
    int b, same = 1;
    Inst s;
    printf("Host block size\n");
    inst_open(&s, rate, 0.7f, 0.8f, D_ARRAY);
    inst_run(&s, in, ref, n, 256);
    inst_close(&s);
    for (b = 0; b < 4; b++) {
        inst_open(&s, rate, 0.7f, 0.8f, D_ARRAY);
        inst_run(&s, in, out, n, blocks[b]);
        if (memcmp(ref, out, n * sizeof(float)) != 0)
            same = 0;
        inst_close(&s);
    }
    check(same, "output is identical for block sizes 1, 7, 64, 256 and 4096");
    free(in);
    free(ref);
    free(out);
}

static void test_robustness(void)
{
    static const double rates[] = { 22050, 44100, 48000, 88200, 96000, 176400, 192000, 384000 };
    int r, ok = 1;
    unsigned int inf_bits = 0x7f800000u, nan_bits = 0x7fc00000u;
    float inf_val, nan_val;
    memcpy(&inf_val, &inf_bits, 4);
    memcpy(&nan_val, &nan_bits, 4);
    printf("Robustness\n");
    for (r = 0; r < (int)(sizeof(rates) / sizeof(rates[0])); r++) {
        const size_t n = 48000;
        float *in = make_sine(n, 1000.0, rates[r], 1.0);
        float *out = (float *)malloc(n * sizeof(float));
        Inst s;
        size_t i;
        in[1000] = nan_val;
        in[2000] = inf_val;
        in[3000] = -inf_val;
        for (i = 4000; i < 4100; i++)
            in[i] = 1e30f;
        inst_open(&s, rates[r], 1.0f, 1.0f, 0);
        for (i = 0; i < n; i += 480) {
            /* Sweep controls and diode types, out of range values included. */
            s.dist = (float)(((i / 480) % 7) * 0.25 - 0.25);
            s.vol = (float)(((i / 480) % 5) * 0.3);
            s.diode = (float)((i / 480) % 7) - 1.0f;
            s.volts = (float)(((i / 480) % 4) * 4.0);
            s.enabled = (float)((i / 480) % 3 != 0);
            inst_run(&s, in + i, out + i, 480 < n - i ? 480 : n - i, 480);
        }
        for (i = 0; i < n; i++)
            if (!isfinite(out[i])) {
                printf("    %.0f Hz: sample %zu = %g\n", rates[r], i, out[i]);
                ok = 0;
                break;
            }
        inst_close(&s);
        free(in);
        free(out);
    }
    check(ok, "bad input and control sweeps stay finite at all sample rates");

    /* Bypass passes the input as it is, so bound the level with the effect on. */
    {
        const size_t n = 48000;
        float *in = make_sine(n, 1000.0, 48000.0, 1.0);
        float *out = (float *)malloc(n * sizeof(float));
        Inst s;
        size_t i;
        double peak = 0.0;
        for (i = 4000; i < 4100; i++)
            in[i] = 1e30f;
        s.volts = 0.0f;
        inst_open(&s, 48000.0, 1.0f, 1.0f, D_LED);
        s.volts = 0.1f;
        inst_run(&s, in, out, n, 256);
        for (i = 0; i < n; i++)
            if (fabs(out[i]) > peak)
                peak = fabs(out[i]);
        printf("    peak with the effect on and a huge input burst: %.2f\n", peak);
        check(isfinite(peak) && peak < 100.0, "processed output stays bounded");
        inst_close(&s);
        free(in);
        free(out);
    }

    /* Input removed: output must decay to silence. */
    {
        const double rate = 48000.0;
        const size_t n = 48000 * 3;
        float *in = make_sine(n, 100.0, rate, 1.0);
        float *out = (float *)malloc(n * sizeof(float));
        Inst s;
        size_t i;
        double tail = 0.0;
        memset(in + n / 3, 0, (n - n / 3) * sizeof(float));
        inst_open(&s, rate, 1.0f, 1.0f, D_ARRAY);
        inst_run(&s, in, out, n, 256);
        for (i = n - 4800; i < n; i++)
            if (fabs(out[i]) > tail)
                tail = fabs(out[i]);
        printf("    level 2 s after the input stops: %.1f dB\n", db(tail));
        check(db(tail) < -60.0, "output decays after input stops");
        inst_close(&s);
        free(in);
        free(out);
    }

    /* Instances must not share state. */
    {
        const size_t n = 4800;
        float *a = make_sine(n, 200.0, 48000.0, 0.5);
        float *o1 = (float *)malloc(n * sizeof(float));
        float *o2 = (float *)malloc(n * sizeof(float));
        Inst s1, s2;
        size_t i;
        inst_open(&s1, 48000.0, 0.6f, 0.8f, 0);
        inst_open(&s2, 48000.0, 0.6f, 0.8f, 0);
        for (i = 0; i < n; i += 64) {
            size_t now = n - i < 64 ? n - i : 64;
            inst_run(&s1, a + i, o1 + i, now, now);
            inst_run(&s2, a + i, o2 + i, now, now);
        }
        check(memcmp(o1, o2, n * sizeof(float)) == 0, "two interleaved instances stay independent");
        inst_close(&s1);
        inst_close(&s2);
        free(a);
        free(o1);
        free(o2);
    }
}

/* The clip output drives the GUI's lamp: dark when quiet or bypassed, lit when clipping. */
static double clip_after(double amp, float dist, float diode, float enabled)
{
    const double rate = 48000.0;
    const size_t n = 24000;
    Inst s;
    float *in = make_sine(n, 220.0, rate, amp);
    float *out = (float *)malloc(n * sizeof(float));
    double v;
    inst_open(&s, rate, dist, 0.8f, diode);
    s.enabled = enabled;
    inst_run(&s, in, out, n, 256);
    v = s.clip;
    inst_close(&s);
    free(in);
    free(out);
    return v;
}

static void test_clip_port(void)
{
    double silent, quiet_ge, hard_ge, hard_si, bypassed, led;
    printf("Clip lamp output\n");
    silent = clip_after(0.0, 1.0f, D_1N270, 1.0f);
    quiet_ge = clip_after(0.0005, 0.0f, D_1N270, 1.0f);
    hard_ge = clip_after(1.0, 1.0f, D_1N270, 1.0f);
    hard_si = clip_after(1.0, 1.0f, D_1N4148, 1.0f);
    led = clip_after(0.05, 0.5f, D_LED, 1.0f);
    bypassed = clip_after(1.0, 1.0f, D_1N270, 0.0f);
    printf("    silence %.3f, quiet germanium %.3f, hard germanium %.3f, hard silicon %.3f,\n"
           "    LED at moderate drive %.3f, bypassed %.3f\n", silent, quiet_ge, hard_ge, hard_si, led, bypassed);
    check(silent == 0.0, "dark with no input");
    check(quiet_ge < 0.05, "dark for a quiet clean signal through germanium diodes");
    check(hard_ge > 0.8 && hard_si > 0.8, "lit when clipping hard");
    check(led < 0.05, "LEDs barely conduct at moderate drive");
    check(bypassed == 0.0, "dark when bypassed");
}

static void test_cpu(void)
{
    static const struct { double rate; float diode; const char *name; } cases[] = {
        { 48000.0, D_1N270, "48 kHz, 8x, 1N270" },
        { 48000.0, D_ARRAY, "48 kHz, 8x, array" },
        { 96000.0, D_1N270, "96 kHz, 4x, 1N270" },
    };
    int c;
    printf("CPU, one core, signal clipping hard\n");
    for (c = 0; c < 3; c++) {
        const double rate = cases[c].rate;
        const size_t n = (size_t)rate * 5;
        float *in = make_sine(n, 220.0, rate, 1.0);
        float *out = (float *)malloc(n * sizeof(float));
        Inst s;
        double t0, seconds, percent;
        inst_open(&s, rate, 1.0f, 1.0f, cases[c].diode);
        t0 = now_seconds();
        inst_run(&s, in, out, n, 256);
        seconds = now_seconds() - t0;
        percent = 100.0 * seconds / 5.0;
        printf("    %-20s %.2f%%\n", cases[c].name, percent);
        check(percent < 15.0, "under 15% of one core");
        inst_close(&s);
        free(in);
        free(out);
    }
}

/* Not a Turtle parser: confirms the port table agrees with the code's numbering. */
static void test_ttl(const char *bundle)
{
    static const char *symbols[] = { "in", "out", "distortion", "output", "diode",
                                     "input_volts", "enabled", "latency", "clip" };
    char path[1024], *text, needle[128];
    FILE *fp;
    long size;
    int i, ok = 1, open_br = 0, close_br = 0;
    char *p;
    printf("TTL port table\n");
    snprintf(path, sizeof(path), "%s/canary-plus.ttl", bundle);
    fp = fopen(path, "rb");
    if (!fp) {
        check(0, "open canary-plus.ttl");
        return;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    text = (char *)calloc((size_t)size + 1, 1);
    fread(text, 1, (size_t)size, fp);
    fclose(fp);
    for (i = 0; i < 9; i++) {
        char *idx, *sym;
        snprintf(needle, sizeof(needle), "lv2:index %d ;", i);
        idx = strstr(text, needle);
        snprintf(needle, sizeof(needle), "lv2:symbol \"%s\" ;", symbols[i]);
        sym = strstr(text, needle);
        /* The symbol has to be the very next statement after its index. */
        if (!idx || !sym || sym < idx || sym - idx > 40)
            ok = 0;
    }
    check(ok, "indexes and symbols match the code");
    for (p = text; *p; p++) {
        if (*p == '[') open_br++;
        if (*p == ']') close_br++;
    }
    check(open_br == close_br, "brackets balance");
    free(text);
}

int main(int argc, char **argv)
{
    char dll[1024];
    Lib lib;
    const LV2_Descriptor *(*get)(uint32_t);
    if (argc < 2) {
        fprintf(stderr, "usage: test_dsp <bundle dir>\n");
        return 2;
    }
    snprintf(dll, sizeof(dll), "%s/canary-plus" LIB_SUFFIX, argv[1]);
    lib = lib_open(dll);
    if (!lib) {
        fprintf(stderr, "could not load %s (%s)\n", dll, lib_error());
        return 2;
    }
    *(void **)&get = lib_sym(lib, "lv2_descriptor");
    if (!get) {
        fprintf(stderr, "lv2_descriptor is not exported\n");
        return 2;
    }
    g_desc = get(0);

    test_descriptor();
    test_response();
    test_clipping();
    test_aliasing();
    test_bypass_and_latency();
    test_block_invariance();
    test_robustness();
    test_clip_port();
    test_cpu();
    test_ttl(argv[1]);

    printf("\n%s (%d failed)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
    lib_close(lib);
    return g_fail ? 1 : 0;
}
