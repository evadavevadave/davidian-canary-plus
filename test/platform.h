/* SPDX-License-Identifier: GPL-3.0-or-later */
/* What the tests need from the operating system: loading a plugin library, looking up a
 * symbol and reading a clock. Windows, macOS and Linux. */
#ifndef TEST_PLATFORM_H
#define TEST_PLATFORM_H

#include <stdio.h>

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    define LIB_SUFFIX ".dll"
typedef HMODULE Lib;
static Lib lib_open(const char* path) { return LoadLibraryA(path); }
static void* lib_sym(Lib lib, const char* name) { return (void*)GetProcAddress(lib, name); }
static void lib_close(Lib lib) { FreeLibrary(lib); }
static const char* lib_error(void)
{
    static char buf[64];
    snprintf(buf, sizeof buf, "error %lu", (unsigned long)GetLastError());
    return buf;
}
static double now_seconds(void)
{
    LARGE_INTEGER f, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)f.QuadPart;
}
#else
#    include <dlfcn.h>
#    include <time.h>
#    if defined(__APPLE__)
#        define LIB_SUFFIX ".dylib"
#    else
#        define LIB_SUFFIX ".so"
#    endif
typedef void* Lib;
static Lib lib_open(const char* path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
static void* lib_sym(Lib lib, const char* name) { return dlsym(lib, name); }
static void lib_close(Lib lib) { dlclose(lib); }
static const char* lib_error(void)
{
    const char* e = dlerror();
    return e ? e : "unknown error";
}
static double now_seconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec;
}
#endif

#endif
