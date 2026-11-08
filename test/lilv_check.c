/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Checks the installed bundle with the real LV2 parser (lilv) that ships with Ardour 9. */
#include <stdio.h>
#include <math.h>
#include <windows.h>

#define PLUGIN_ID "urn:davidian:canary-plus"

typedef void W; typedef void N; typedef void Pl; typedef void P; typedef void Po; typedef void SPs;
typedef struct { const void *d; void *h; void *pimpl; } LI;
typedef struct {
    const char *uri;
    void *(*instantiate)(const void *, double, const char *, const void *const *);
    void (*connect_port)(void *, unsigned, void *);
    void (*activate)(void *);
    void (*run)(void *, unsigned);
    void (*deactivate)(void *);
    void (*cleanup)(void *);
    const void *(*ext)(const char *);
} D;

#define SYM(ret, name, ...) ret (*name)(__VA_ARGS__) = (ret (*)(__VA_ARGS__))GetProcAddress(lib, #name); \
    if (!name) { printf("missing %s\n", #name); return 2; }

int main(void)
{
    HMODULE lib;
    SetDllDirectoryA("C:\\Program Files\\Ardour9\\bin");
    lib = LoadLibraryA("lilv-0.dll");
    if (!lib) { printf("cannot load lilv: %lu\n", GetLastError()); return 2; }
    {
    SYM(W *, lilv_world_new, void)
    SYM(void, lilv_world_load_all, W *)
    SYM(const Pl *, lilv_world_get_all_plugins, W *)
    SYM(unsigned, lilv_plugins_size, const Pl *)
    SYM(N *, lilv_new_uri, W *, const char *)
    SYM(const P *, lilv_plugins_get_by_uri, const Pl *, const N *)
    SYM(int, lilv_plugin_verify, const P *)
    SYM(N *, lilv_plugin_get_name, const P *)
    SYM(const char *, lilv_node_as_string, const N *)
    SYM(float, lilv_node_as_float, const N *)
    SYM(unsigned, lilv_plugin_get_num_ports, const P *)
    SYM(const Po *, lilv_plugin_get_port_by_index, const P *, unsigned)
    SYM(const N *, lilv_port_get_symbol, const P *, const Po *)
    SYM(N *, lilv_port_get_name, const P *, const Po *)
    SYM(void, lilv_port_get_range, const P *, const Po *, N **, N **, N **)
    SYM(SPs *, lilv_port_get_scale_points, const P *, const Po *)
    SYM(unsigned, lilv_scale_points_size, const SPs *)
    SYM(int, lilv_plugin_has_latency, const P *)
    SYM(unsigned, lilv_plugin_get_latency_port_index, const P *)
    SYM(const N *, lilv_plugin_get_library_uri, const P *)
    SYM(const void *, lilv_plugin_get_class, const P *)
    SYM(const N *, lilv_plugin_class_get_label, const void *)
    SYM(LI *, lilv_plugin_instantiate, const P *, double, const void *const *)
    SYM(void *, lilv_plugin_get_uis, const P *)
    SYM(unsigned, lilv_uis_size, const void *)

    W *w = lilv_world_new();
    const Pl *all;
    const P *p;
    N *u;
    unsigned i, n;
    lilv_world_load_all(w);
    all = lilv_world_get_all_plugins(w);
    printf("plugins found: %u\n", lilv_plugins_size(all));
    u = lilv_new_uri(w, PLUGIN_ID);
    p = lilv_plugins_get_by_uri(all, u);
    if (!p) { printf("FAIL: plugin not found\n"); return 1; }
    printf("verify: %s\n", lilv_plugin_verify(p) ? "ok" : "FAILED");
    printf("name: %s\n", lilv_node_as_string(lilv_plugin_get_name(p)));
    printf("class: %s\n", lilv_node_as_string(lilv_plugin_class_get_label(lilv_plugin_get_class(p))));
    printf("binary: %s\n", lilv_node_as_string(lilv_plugin_get_library_uri(p)));
    {
        void *uis = lilv_plugin_get_uis(p);
        printf("GUIs found: %u\n", uis ? lilv_uis_size(uis) : 0);
    }
    printf("latency port: %s %u\n", lilv_plugin_has_latency(p) ? "yes" : "no",
           lilv_plugin_has_latency(p) ? lilv_plugin_get_latency_port_index(p) : 0);
    n = lilv_plugin_get_num_ports(p);
    for (i = 0; i < n; i++) {
        const Po *po = lilv_plugin_get_port_by_index(p, i);
        N *d = 0, *lo = 0, *hi = 0;
        SPs *sp = lilv_port_get_scale_points(p, po);
        lilv_port_get_range(p, po, &d, &lo, &hi);
        printf("  %u %-12s %-16s def %5.2f min %5.2f max %5.2f points %u\n", i,
               lilv_node_as_string(lilv_port_get_symbol(p, po)),
               lilv_node_as_string(lilv_port_get_name(p, po)),
               d ? lilv_node_as_float(d) : -1, lo ? lilv_node_as_float(lo) : -1,
               hi ? lilv_node_as_float(hi) : -1, sp ? lilv_scale_points_size(sp) : 0);
    }
    {
        LI *inst = lilv_plugin_instantiate(p, 48000.0, NULL);
        float in[4800], out[4800], dist = 1, vol = 1, diode = 0, volts = 1, en = 1, lat = -1, peak = 0;
        const D *de;
        int k;
        if (!inst) { printf("FAIL: instantiate\n"); return 1; }
        de = (const D *)inst->d;
        for (k = 0; k < 4800; k++) in[k] = (float)sin(2 * 3.14159265 * 500 * k / 48000.0);
        de->connect_port(inst->h, 0, in); de->connect_port(inst->h, 1, out);
        de->connect_port(inst->h, 2, &dist); de->connect_port(inst->h, 3, &vol);
        de->connect_port(inst->h, 4, &diode); de->connect_port(inst->h, 5, &volts);
        de->connect_port(inst->h, 6, &en); de->connect_port(inst->h, 7, &lat);
        de->activate(inst->h);
        de->run(inst->h, 4800);
        for (k = 2400; k < 4800; k++) if (fabs(out[k]) > peak) peak = (float)fabs(out[k]);
        printf("instantiated through lilv: latency %.0f, peak %.3f\n", lat, peak);
    }
    }
    return 0;
}
