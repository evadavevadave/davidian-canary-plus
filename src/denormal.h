/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Flush-to-zero for the processing loop on any CPU. Denormal numbers (tiny values left
 * in filter states as a signal decays) are very slow on most CPUs, so run() turns on
 * flush-to-zero while it works and restores the host's setting afterwards. */
#ifndef DENORMAL_H
#define DENORMAL_H

#include <stdint.h>

typedef uint64_t FpState;

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#    include <xmmintrin.h>
/* MXCSR bits 15 (flush to zero) and 6 (denormals are zero). */
static FpState denormals_off(void)
{
    const unsigned int saved = _mm_getcsr();
    _mm_setcsr(saved | 0x8040);
    return saved;
}
static void denormals_restore(FpState saved) { _mm_setcsr((unsigned int)saved); }

#elif defined(__aarch64__) || defined(_M_ARM64)
#    if defined(_MSC_VER) && !defined(__clang__)
#        include <intrin.h>
#        define FPCR_READ() ((uint64_t)_ReadStatusReg(ARM64_FPCR))
#        define FPCR_WRITE(v) _WriteStatusReg(ARM64_FPCR, (__int64)(v))
#    else
static uint64_t fpcr_read(void) { uint64_t v; __asm__ __volatile__("mrs %0, fpcr" : "=r"(v)); return v; }
static void fpcr_write(uint64_t v) { __asm__ __volatile__("msr fpcr, %0" : : "r"(v)); }
#        define FPCR_READ() fpcr_read()
#        define FPCR_WRITE(v) fpcr_write(v)
#    endif
/* FPCR bit 24 (FZ) flushes denormals to zero. */
static FpState denormals_off(void)
{
    const uint64_t saved = FPCR_READ();
    FPCR_WRITE(saved | (1ull << 24));
    return saved;
}
static void denormals_restore(FpState saved) { FPCR_WRITE(saved); }

#else
/* Other CPUs: no flush-to-zero. The filters still work, only slower on decaying tails. */
static FpState denormals_off(void) { return 0; }
static void denormals_restore(FpState saved) { (void)saved; }
#endif

#endif
