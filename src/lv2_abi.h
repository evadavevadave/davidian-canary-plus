/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The small part of the LV2 core C ABI this plugin uses.
 * Layouts match lv2/core/lv2.h from the LV2 specification, so no SDK is needed
 * to build. All plugin metadata lives in the .ttl files. */
#ifndef LV2_ABI_H
#define LV2_ABI_H

#include <stdint.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#    define LV2_EXPORT __declspec(dllexport)
#else
#    define LV2_EXPORT __attribute__((visibility("default")))
#endif

typedef void *LV2_Handle;

typedef struct LV2_Feature {
    const char *URI;
    void *data;
} LV2_Feature;

typedef struct LV2_Descriptor {
    const char *URI;
    LV2_Handle (*instantiate)(const struct LV2_Descriptor *descriptor,
                              double sample_rate,
                              const char *bundle_path,
                              const LV2_Feature *const *features);
    void (*connect_port)(LV2_Handle instance, uint32_t port, void *data);
    void (*activate)(LV2_Handle instance);
    void (*run)(LV2_Handle instance, uint32_t sample_count);
    void (*deactivate)(LV2_Handle instance);
    void (*cleanup)(LV2_Handle instance);
    const void *(*extension_data)(const char *uri);
} LV2_Descriptor;

#endif
