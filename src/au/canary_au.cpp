// SPDX-License-Identifier: GPL-3.0-or-later
// Audio Unit (v2) version of the Davidian Canary Plus, built on Apple's AudioUnitSDK.
// The sound comes from the same code as the LV2 plugin: each channel gets its own
// instance of plugin.c (a "kernel"), and the AU parameters drive its ports.
// The Cocoa view is in au_view.m.
#include <AudioToolbox/AudioUnitUtilities.h>
#include <AudioUnitSDK/AUEffectBase.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <cstring>

#include "../lv2_abi.h"
#include "../params.h"
#include "au_ids.h"

// plugin.c is real-time safe (no locks, no allocation in run) but, being C, carries
// no nonblocking annotation for clang to check calls into it against.
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wfunction-effects"

namespace {

constexpr Float64 LATENCY_SAMPLES = 32.0; // OVERSAMPLE_LATENCY in oversampler.h; checked in Prepare()

class CanaryKernel : public ausdk::AUKernelBase {
public:
    explicit CanaryKernel(ausdk::AUEffectBase& au) : AUKernelBase(au), desc(lv2_descriptor(0)) {}
    ~CanaryKernel() override { Free(); }

    // Creates the circuit for the current sample rate. Called when the unit initialises,
    // never while rendering.
    bool Prepare(double sample_rate)
    {
        if (inst && sample_rate == rate)
            return true;
        Free();
        rate = sample_rate;
        inst = desc ? desc->instantiate(desc, rate, "", nullptr) : nullptr;
        if (!inst)
            return false;
        for (int p = 0; p < PORT_COUNT; ++p) {
            float* where = p == PORT_LATENCY ? &latency : (p == PORT_CLIP ? &clip : &port[p]);
            if (p != PORT_IN && p != PORT_OUT)
                desc->connect_port(inst, static_cast<uint32_t>(p), where);
        }
        ReadParameters();
        desc->connect_port(inst, PORT_IN, nullptr);
        desc->connect_port(inst, PORT_OUT, nullptr);
        desc->activate(inst);
        desc->run(inst, 0); // reports latency without touching audio
        return latency == static_cast<float>(LATENCY_SAMPLES);
    }

    void Reset() override
    {
        if (inst)
            desc->activate(inst);
        clip = 0.0f;
    }

    void Process(const Float32* in, Float32* out, UInt32 frames, bool& silence) AUSDK_RTSAFE override
    {
        if (!inst) {
            std::fill(out, out + frames, 0.0f);
            return;
        }
        ReadParameters();
        // plugin.c reads each input sample before writing that output sample, so
        // in-place buffers are fine.
        desc->connect_port(inst, PORT_IN, const_cast<Float32*>(in));
        desc->connect_port(inst, PORT_OUT, out);
        desc->run(inst, frames);
        silence = false;
    }

    float Clip() const { return clip; }

private:
    void ReadParameters()
    {
        for (int i = 0; i < P_COUNT; ++i)
            if (PARAMS[i].kind != PK_METER)
                port[PARAMS[i].port] = static_cast<float>(param_clamp(i, GetParameter(static_cast<AudioUnitParameterID>(i))));
    }

    void Free()
    {
        if (inst)
            desc->cleanup(inst);
        inst = nullptr;
    }

    const LV2_Descriptor* desc;
    LV2_Handle inst = nullptr;
    double rate = 0.0;
    float port[PORT_COUNT] = {};
    float latency = 0.0f;
    float clip = 0.0f;
};

CFStringRef cf_string(const char* s)
{
    return CFStringCreateWithCString(kCFAllocatorDefault, s, kCFStringEncodingUTF8);
}

class CanaryPlusAU : public ausdk::AUEffectBase {
public:
    explicit CanaryPlusAU(AudioComponentInstance ci) : AUEffectBase(ci, true)
    {
        for (int i = 0; i < P_COUNT; ++i)
            Globals()->SetParameter(static_cast<AudioUnitParameterID>(i), PARAMS[i].def);
    }

    std::unique_ptr<ausdk::AUKernelBase> NewKernel() override { return std::make_unique<CanaryKernel>(*this); }

    OSStatus Initialize() override
    {
        OSStatus r = AUEffectBase::Initialize();
        if (r != noErr)
            return r;
        for (auto& k : GetKernelList())
            if (k && !static_cast<CanaryKernel*>(k.get())->Prepare(GetSampleRate()))
                return kAudioUnitErr_FailedInitialization;
        return noErr;
    }

    OSStatus Render(AudioUnitRenderActionFlags& flags, const AudioTimeStamp& ts, UInt32 frames) AUSDK_RTSAFE override
    {
        const OSStatus r = AUEffectBase::Render(flags, ts, frames);
        float level = 0.0f;
        if (!ShouldBypassEffect())
            for (auto& k : GetKernelList())
                if (k)
                    level = std::max(level, static_cast<CanaryKernel*>(k.get())->Clip());
        SetParameter(P_CLIP, level);
        return r;
    }

    bool SupportsTail() AUSDK_RTSAFE override { return true; }
    Float64 GetTailTime() AUSDK_RTSAFE override { return 0.1; } // the filters ring briefly after the input stops
    Float64 GetLatency() AUSDK_RTSAFE override { return LATENCY_SAMPLES / GetSampleRate(); }

    OSStatus GetParameterInfo(AudioUnitScope scope, AudioUnitParameterID id, AudioUnitParameterInfo& info) override
    {
        if (scope != kAudioUnitScope_Global || id >= P_COUNT)
            return kAudioUnitErr_InvalidParameter;
        const ParamSpec& s = PARAMS[id];
        info.flags = kAudioUnitParameterFlag_IsReadable;
        info.flags |= s.kind == PK_METER ? kAudioUnitParameterFlag_MeterReadOnly : kAudioUnitParameterFlag_IsWritable;
        FillInParameterName(info, cf_string(s.name), true);
        info.minValue = s.min;
        info.maxValue = s.max;
        info.defaultValue = s.def;
        switch (s.kind) {
        case PK_LINEAR: info.unit = kAudioUnitParameterUnit_Generic; break;
        case PK_LOG:
            info.unit = kAudioUnitParameterUnit_CustomUnit;
            info.unitName = CFSTR("V");
            info.flags |= kAudioUnitParameterFlag_DisplayLogarithmic;
            break;
        case PK_LIST: info.unit = kAudioUnitParameterUnit_Indexed; break;
        case PK_TOGGLE: info.unit = kAudioUnitParameterUnit_Boolean; break;
        case PK_METER: info.unit = kAudioUnitParameterUnit_Generic; break;
        }
        return noErr;
    }

    OSStatus GetParameterValueStrings(AudioUnitScope scope, AudioUnitParameterID id, CFArrayRef* strings) override
    {
        if (scope != kAudioUnitScope_Global || id != P_DIODE)
            return kAudioUnitErr_InvalidProperty;
        if (!strings)
            return noErr; // the host is only asking whether there are names
        CFStringRef names[DIODE_COUNT_NAMES];
        for (int i = 0; i < DIODE_COUNT_NAMES; ++i)
            names[i] = cf_string(DIODE_NAMES[i]);
        *strings = CFArrayCreate(kCFAllocatorDefault, reinterpret_cast<const void**>(names), DIODE_COUNT_NAMES,
                                 &kCFTypeArrayCallBacks);
        for (CFStringRef n : names)
            CFRelease(n);
        return noErr;
    }

    OSStatus GetPropertyInfo(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement el, UInt32& size,
                             bool& writable) override
    {
        if (id == kAudioUnitProperty_CocoaUI && scope == kAudioUnitScope_Global) {
            size = sizeof(AudioUnitCocoaViewInfo);
            writable = false;
            return noErr;
        }
        return AUEffectBase::GetPropertyInfo(id, scope, el, size, writable);
    }

    OSStatus GetProperty(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement el, void* data) override
    {
        if (id == kAudioUnitProperty_CocoaUI && scope == kAudioUnitScope_Global) {
            // The view's code is in this same bundle; the host loads its factory class.
            CFBundleRef bundle = CFBundleGetBundleWithIdentifier(CFSTR(AU_BUNDLE_ID));
            if (!bundle)
                return kAudioUnitErr_InvalidProperty;
            AudioUnitCocoaViewInfo info;
            info.mCocoaAUViewBundleLocation = CFBundleCopyBundleURL(bundle);
            info.mCocoaAUViewClass[0] = CFSTR(AU_VIEW_FACTORY_CLASS_NAME);
            CFRetain(info.mCocoaAUViewClass[0]); // the host releases both
            std::memcpy(data, &info, sizeof info);
            return noErr;
        }
        return AUEffectBase::GetProperty(id, scope, el, data);
    }
};

} // namespace

AUSDK_COMPONENT_ENTRY(ausdk::AUBaseFactory, CanaryPlusAU)
