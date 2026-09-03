#pragma once

// Flush-to-zero for the duration of an audio callback.
//
// Every IIR state in this plugin decays towards zero without reaching it: the
// tone stack, the graphic EQ's gyrator branches, Spread's crossover and
// diffuser, the IR tails. When the player stops, those states walk down into
// the subnormal range and stay there, and subnormal arithmetic runs off the
// fast path on most FPUs. Measured on the real chain (tone stack -> 5-band EQ
// -> Spread) at 48 kHz on x86-64: 113 ns/sample on signal, 2834 ns/sample once
// 20 s of digital silence has driven the states subnormal -- a 25x cliff that
// arrives exactly when the CPU meter should be idling, with 909694 subnormal
// output samples over that stretch. On continuous signal the guard itself is
// free (110.8 -> 109.3 ns/sample, inside the noise).
//
// The JUCE build gets this from juce::ScopedNoDenormals; this is the same
// thing for the JUCE-free MOD LV2, which had no protection at all.
//
// The previous mode is restored on scope exit: the host's thread is not ours
// to leave reconfigured.

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
 #define NSDSP_DENORMAL_GUARD_X86 1
 #include <xmmintrin.h>
#elif defined(__aarch64__) || defined(_M_ARM64)
 #define NSDSP_DENORMAL_GUARD_AARCH64 1
#elif defined(__arm__) && (defined(__ARM_FP) || defined(__VFP_FP__))
 #define NSDSP_DENORMAL_GUARD_ARM32 1
#endif

namespace nsdsp
{

class DenormalGuard
{
public:
    DenormalGuard() noexcept
    {
       #if defined(NSDSP_DENORMAL_GUARD_X86)
        saved = _mm_getcsr();
        // FZ (bit 15) flushes subnormal results; DAZ (bit 6) treats subnormal
        // operands as zero. DAZ is architecturally optional on 32-bit x86 --
        // setting an unsupported MXCSR bit faults -- but universal on x86-64,
        // so only that half asks for both.
        #if defined(__x86_64__) || defined(_M_X64)
         _mm_setcsr (saved | 0x8040u);
        #else
         _mm_setcsr (saved | 0x8000u);
        #endif
       #elif defined(NSDSP_DENORMAL_GUARD_AARCH64)
        asm volatile ("mrs %0, fpcr" : "=r" (saved));
        asm volatile ("msr fpcr, %0" : : "r" (saved | (1ull << 24))); // FZ
       #elif defined(NSDSP_DENORMAL_GUARD_ARM32)
        asm volatile ("vmrs %0, fpscr" : "=r" (saved));
        asm volatile ("vmsr fpscr, %0" : : "r" (saved | (1u << 24)));  // FZ
       #endif
    }

    ~DenormalGuard() noexcept
    {
       #if defined(NSDSP_DENORMAL_GUARD_X86)
        _mm_setcsr (saved);
       #elif defined(NSDSP_DENORMAL_GUARD_AARCH64)
        asm volatile ("msr fpcr, %0" : : "r" (saved));
       #elif defined(NSDSP_DENORMAL_GUARD_ARM32)
        asm volatile ("vmsr fpscr, %0" : : "r" (saved));
       #endif
    }

    DenormalGuard (const DenormalGuard&) = delete;
    DenormalGuard& operator= (const DenormalGuard&) = delete;

    // True when this build actually has somewhere to set the bit, so a target
    // without one is a visible gap rather than a silent no-op.
    static constexpr bool isSupported()
    {
       #if defined(NSDSP_DENORMAL_GUARD_X86) || defined(NSDSP_DENORMAL_GUARD_AARCH64) \
        || defined(NSDSP_DENORMAL_GUARD_ARM32)
        return true;
       #else
        return false;
       #endif
    }

private:
   #if defined(NSDSP_DENORMAL_GUARD_X86)
    unsigned int saved = 0;
   #elif defined(NSDSP_DENORMAL_GUARD_AARCH64)
    unsigned long long saved = 0;
   #elif defined(NSDSP_DENORMAL_GUARD_ARM32)
    unsigned int saved = 0;
   #endif
};

} // namespace nsdsp
