// SPDX-License-Identifier: GPL-3.0-or-later
// VST3 version of the Davidian Canary Plus. The sound comes from the same code as the
// LV2 plugin: this wrapper runs one instance of plugin.c per channel (mono or dual
// mono) and maps VST3 parameters onto its ports. The editor is the same panel.
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/common/pluginview.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

#include "../lv2_abi.h"
#include "../params.h"
#include "../ui/editor.h"
#include "version.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr int MAX_CHANNELS = 2;
constexpr uint32 LATENCY = 32; // OVERSAMPLE_LATENCY in oversampler.h; checked in initialize()
constexpr int32 STATE_VERSION = 1;

// VST3 has a standard bypass parameter, which hosts link to their own bypass
// button. Here it is the inverse of the panel's Engage footswitch.
inline bool is_bypass(int id) { return id == P_ENGAGE; }

inline double port_value_from_normal(int id, double n)
{
    return is_bypass(id) ? (n >= 0.5 ? 0.0 : 1.0) : param_from_normal(id, n);
}

inline double normal_from_port_value(int id, double v)
{
    return is_bypass(id) ? (v >= 0.5 ? 0.0 : 1.0) : param_to_normal(id, v);
}

void to_string128(const char* text, String128 out)
{
    UString(out, str16BufferSize(String128)).fromAscii(text);
}

class CanaryParameter : public Parameter
{
public:
    explicit CanaryParameter(int id) : id(id)
    {
        const ParamSpec& s = PARAMS[id];
        info.id = static_cast<ParamID>(id);
        to_string128(is_bypass(id) ? "Bypass" : s.name, info.title);
        to_string128(is_bypass(id) ? "Bypass" : s.name, info.shortTitle);
        to_string128(s.unit, info.units);
        info.unitId = kRootUnitId;
        info.stepCount = s.kind == PK_LIST ? static_cast<int32>(s.max - s.min) : (s.kind == PK_TOGGLE ? 1 : 0);
        info.defaultNormalizedValue = normal_from_port_value(id, s.def);
        switch (s.kind) {
        case PK_METER: info.flags = ParameterInfo::kIsReadOnly; break;
        case PK_LIST: info.flags = ParameterInfo::kCanAutomate | ParameterInfo::kIsList; break;
        case PK_TOGGLE: info.flags = ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass; break;
        default: info.flags = ParameterInfo::kCanAutomate; break;
        }
        setNormalized(info.defaultNormalizedValue);
    }

    void toString(ParamValue n, String128 string) const SMTG_OVERRIDE
    {
        const double v = toPlain(n);
        char buf[64];
        switch (PARAMS[id].kind) {
        case PK_LINEAR: std::snprintf(buf, sizeof buf, "%.1f", v * 10.0); break; // the knob's 0-10 scale
        case PK_LOG: std::snprintf(buf, sizeof buf, "%.2f", v); break;
        case PK_LIST: std::snprintf(buf, sizeof buf, "%s", DIODE_NAMES[static_cast<int>(v)]); break;
        case PK_TOGGLE: std::snprintf(buf, sizeof buf, "%s", n >= 0.5 ? "On" : "Off"); break;
        case PK_METER: std::snprintf(buf, sizeof buf, "%.0f%%", v * 100.0); break;
        }
        to_string128(buf, string);
    }

    bool fromString(const TChar* string, ParamValue& n) const SMTG_OVERRIDE
    {
        char buf[128];
        // The size counts the terminator, or the last character is lost.
        UString(const_cast<TChar*>(string), tstrlen(string) + 1).toAscii(buf, sizeof buf);
        const ParamSpec& s = PARAMS[id];
        if (s.kind == PK_LIST) {
            for (int i = 0; i < DIODE_COUNT_NAMES; ++i) {
                if (std::strncmp(buf, DIODE_NAMES[i], std::strlen(buf)) == 0 && buf[0]) {
                    n = toNormalized(i);
                    return true;
                }
            }
        }
        if (s.kind == PK_TOGGLE) {
            if (!std::strcmp(buf, "On") || !std::strcmp(buf, "on")) { n = 1.0; return true; }
            if (!std::strcmp(buf, "Off") || !std::strcmp(buf, "off")) { n = 0.0; return true; }
        }
        char* end = nullptr;
        double v = std::strtod(buf, &end);
        if (end == buf)
            return false;
        if (s.kind == PK_LINEAR)
            v /= 10.0;
        else if (s.kind == PK_METER)
            v /= 100.0;
        n = toNormalized(v);
        return true;
    }

    // Plain values are the plugin's own: 0-1 knobs, 0.1-10 V, diode 0-4. Bypass stays 0-1.
    ParamValue toPlain(ParamValue n) const SMTG_OVERRIDE
    {
        return is_bypass(id) ? (n >= 0.5 ? 1.0 : 0.0) : param_from_normal(id, n);
    }

    ParamValue toNormalized(ParamValue v) const SMTG_OVERRIDE
    {
        return is_bypass(id) ? (v >= 0.5 ? 1.0 : 0.0) : param_to_normal(id, v);
    }

private:
    int id;
};

class CanaryPlus;

class CanaryView : public CPluginView
{
public:
    explicit CanaryView(CanaryPlus* fx);
    ~CanaryView() SMTG_OVERRIDE;

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) SMTG_OVERRIDE;
    tresult PLUGIN_API attached(void* parent, FIDString type) SMTG_OVERRIDE;
    tresult PLUGIN_API removed() SMTG_OVERRIDE;

private:
    static void write(void* ctx, int port, float value);
    static void gesture(void* ctx, int port, int begin);
    static void poll(void* ctx, Editor* ed);

    CanaryPlus* fx;
    Editor* editor = nullptr;
};

class CanaryPlus : public SingleComponentEffect
{
public:
    static FUnknown* createInstance(void*) { return static_cast<IAudioProcessor*>(new CanaryPlus); }

    CanaryPlus()
    {
        for (int i = 0; i < P_COUNT; ++i)
            target[i].store(PARAMS[i].def);
    }

    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE
    {
        tresult r = SingleComponentEffect::initialize(context);
        if (r != kResultOk)
            return r;
        desc = lv2_descriptor(0);
        if (!desc)
            return kResultFalse;
        addAudioInput(STR16("Input"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Output"), SpeakerArr::kStereo);
        for (int i = 0; i < P_COUNT; ++i)
            parameters.addParameter(new CanaryParameter(i));
        return create_instances(48000.0) ? kResultOk : kResultFalse;
    }

    tresult PLUGIN_API terminate() SMTG_OVERRIDE
    {
        free_instances();
        return SingleComponentEffect::terminate();
    }

    // Mono or stereo, the same on both sides. Stereo runs two independent circuits.
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                          SpeakerArrangement* outputs, int32 numOuts) SMTG_OVERRIDE
    {
        if (numIns != 1 || numOuts != 1 || inputs[0] != outputs[0])
            return kResultFalse;
        if (inputs[0] != SpeakerArr::kMono && inputs[0] != SpeakerArr::kStereo)
            return kResultFalse;
        return SingleComponentEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }

    tresult PLUGIN_API canProcessSampleSize(int32 size) SMTG_OVERRIDE
    {
        return size == kSample32 ? kResultTrue : kResultFalse;
    }

    uint32 PLUGIN_API getLatencySamples() SMTG_OVERRIDE { return LATENCY; }

    // Filters ring briefly after the input stops; 100 ms covers it.
    uint32 PLUGIN_API getTailSamples() SMTG_OVERRIDE
    {
        return static_cast<uint32>(0.1 * rate) + LATENCY;
    }

    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) SMTG_OVERRIDE
    {
        if (setup.sampleRate != rate && !create_instances(setup.sampleRate))
            return kResultFalse;
        return SingleComponentEffect::setupProcessing(setup);
    }

    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE
    {
        if (state) {
            for (int c = 0; c < MAX_CHANNELS; ++c)
                if (inst[c] && desc->activate)
                    desc->activate(inst[c]);
        }
        clip.store(0.0f);
        return SingleComponentEffect::setActive(state);
    }

    tresult PLUGIN_API process(ProcessData& data) SMTG_OVERRIDE
    {
        read_parameter_changes(data.inputParameterChanges);
        for (int i = 0; i < P_COUNT; ++i)
            if (PARAMS[i].kind != PK_METER)
                port[PARAMS[i].port] = target[i].load(std::memory_order_relaxed);

        if (data.numSamples <= 0 || data.numInputs < 1 || data.numOutputs < 1)
            return kResultOk;

        AudioBusBuffers& in = data.inputs[0];
        AudioBusBuffers& out = data.outputs[0];
        const int channels = std::min({ in.numChannels, out.numChannels, static_cast<int32>(MAX_CHANNELS) });
        float level = 0.0f;
        for (int c = 0; c < channels; ++c) {
            desc->connect_port(inst[c], PORT_IN, in.channelBuffers32[c]);
            desc->connect_port(inst[c], PORT_OUT, out.channelBuffers32[c]);
            desc->run(inst[c], static_cast<uint32_t>(data.numSamples));
            level = std::max(level, clip_out[c]);
        }
        for (int c = channels; c < out.numChannels; ++c)
            std::memset(out.channelBuffers32[c], 0, sizeof(float) * static_cast<size_t>(data.numSamples));
        out.silenceFlags = 0;

        clip.store(level, std::memory_order_relaxed);
        if (data.outputParameterChanges) {
            int32 index = 0;
            if (IParamValueQueue* q = data.outputParameterChanges->addParameterData(P_CLIP, index))
                q->addPoint(0, level, index);
        }
        return kResultOk;
    }

    tresult PLUGIN_API setState(IBStream* state) SMTG_OVERRIDE
    {
        IBStreamer s(state, kLittleEndian);
        int32 version = 0;
        if (!s.readInt32(version) || version < 1)
            return kResultFalse;
        for (int i = 0; i < P_COUNT; ++i) {
            if (PARAMS[i].kind == PK_METER)
                continue;
            float v = 0.0f;
            if (!s.readFloat(v))
                return kResultFalse;
            v = static_cast<float>(param_clamp(i, v));
            target[i].store(v);
            setParamNormalized(static_cast<ParamID>(i), normal_from_port_value(i, v));
        }
        return kResultOk;
    }

    tresult PLUGIN_API getState(IBStream* state) SMTG_OVERRIDE
    {
        IBStreamer s(state, kLittleEndian);
        if (!s.writeInt32(STATE_VERSION))
            return kResultFalse;
        for (int i = 0; i < P_COUNT; ++i) {
            if (PARAMS[i].kind == PK_METER)
                continue;
            // The controller's values, which are what the user last saw.
            const float v = static_cast<float>(port_value_from_normal(i, getParamNormalized(static_cast<ParamID>(i))));
            if (!s.writeFloat(v))
                return kResultFalse;
        }
        return kResultOk;
    }

    IPlugView* PLUGIN_API createView(FIDString name) SMTG_OVERRIDE
    {
        if (name && std::strcmp(name, ViewType::kEditor) == 0)
            return new CanaryView(this);
        return nullptr;
    }

    // ---- For the editor (GUI thread) ----

    void edit(int port, float value)
    {
        const int id = param_for_port(port);
        if (id < 0 || PARAMS[id].kind == PK_METER)
            return;
        const ParamValue n = normal_from_port_value(id, value);
        setParamNormalized(static_cast<ParamID>(id), n);
        performEdit(static_cast<ParamID>(id), n);
    }

    void gesture(int port, bool begin)
    {
        const int id = param_for_port(port);
        if (id < 0 || PARAMS[id].kind == PK_METER)
            return;
        if (begin)
            beginEdit(static_cast<ParamID>(id));
        else
            endEdit(static_cast<ParamID>(id));
    }

    float port_value(int id) { return static_cast<float>(port_value_from_normal(id, getParamNormalized(static_cast<ParamID>(id)))); }
    float clip_level() const { return clip.load(std::memory_order_relaxed); }

private:
    ~CanaryPlus() SMTG_OVERRIDE { free_instances(); }

    void read_parameter_changes(IParameterChanges* changes)
    {
        if (!changes)
            return;
        const int32 n = changes->getParameterCount();
        for (int32 i = 0; i < n; ++i) {
            IParamValueQueue* q = changes->getParameterData(i);
            if (!q)
                continue;
            const ParamID id = q->getParameterId();
            const int32 points = q->getPointCount();
            int32 offset = 0;
            ParamValue value = 0.0;
            // The plugin smooths its controls, so the block's last value is enough.
            if (id < P_COUNT && PARAMS[id].kind != PK_METER && points > 0
                && q->getPoint(points - 1, offset, value) == kResultOk)
                target[id].store(static_cast<float>(port_value_from_normal(static_cast<int>(id), value)));
        }
    }

    bool create_instances(double new_rate)
    {
        free_instances();
        rate = new_rate;
        for (int c = 0; c < MAX_CHANNELS; ++c) {
            inst[c] = desc->instantiate(desc, rate, "", nullptr);
            if (!inst[c]) {
                free_instances();
                return false;
            }
            for (int p = 0; p < PORT_COUNT; ++p) {
                if (p == PORT_IN || p == PORT_OUT)
                    continue;
                float* where = p == PORT_LATENCY ? &latency_out[c] : (p == PORT_CLIP ? &clip_out[c] : &port[p]);
                desc->connect_port(inst[c], static_cast<uint32_t>(p), where);
            }
            desc->connect_port(inst[c], PORT_IN, nullptr);
            desc->connect_port(inst[c], PORT_OUT, nullptr);
            desc->activate(inst[c]);
            desc->run(inst[c], 0); // reports latency without touching audio
        }
        return static_cast<uint32>(latency_out[0]) == LATENCY;
    }

    void free_instances()
    {
        for (int c = 0; c < MAX_CHANNELS; ++c) {
            if (inst[c])
                desc->cleanup(inst[c]);
            inst[c] = nullptr;
        }
    }

    const LV2_Descriptor* desc = nullptr;
    LV2_Handle inst[MAX_CHANNELS] = {};
    double rate = 0.0;
    float port[PORT_COUNT] = {};              // control inputs, shared by the channels
    float latency_out[MAX_CHANNELS] = {};
    float clip_out[MAX_CHANNELS] = {};
    std::atomic<float> target[P_COUNT];       // latest values from the host
    std::atomic<float> clip { 0.0f };
};

// ---- Editor ----

CanaryView::CanaryView(CanaryPlus* fx) : fx(fx)
{
    ViewRect r(0, 0, EDITOR_W, EDITOR_H);
    setRect(r);
    fx->addRef();
}

CanaryView::~CanaryView()
{
    editor_close(editor);
    fx->release();
}

tresult PLUGIN_API CanaryView::isPlatformTypeSupported(FIDString type)
{
#if SMTG_OS_WINDOWS
    return std::strcmp(type, kPlatformTypeHWND) == 0 ? kResultTrue : kResultFalse;
#elif SMTG_OS_MACOS
    return std::strcmp(type, kPlatformTypeNSView) == 0 ? kResultTrue : kResultFalse;
#else
    (void)type;
    return kResultFalse;
#endif
}

tresult PLUGIN_API CanaryView::attached(void* parent, FIDString type)
{
    if (isPlatformTypeSupported(type) != kResultTrue || editor)
        return kResultFalse;
    EditorHost host = { this, write, gesture, poll };
    editor = editor_open(parent, &host);
    if (!editor)
        return kResultFalse;
    return CPluginView::attached(parent, type);
}

tresult PLUGIN_API CanaryView::removed()
{
    editor_close(editor);
    editor = nullptr;
    return CPluginView::removed();
}

void CanaryView::write(void* ctx, int port, float value) { static_cast<CanaryView*>(ctx)->fx->edit(port, value); }

void CanaryView::gesture(void* ctx, int port, int begin) { static_cast<CanaryView*>(ctx)->fx->gesture(port, begin != 0); }

void CanaryView::poll(void* ctx, Editor* ed)
{
    CanaryPlus* fx = static_cast<CanaryView*>(ctx)->fx;
    for (int i = 0; i < P_COUNT; ++i)
        if (PARAMS[i].kind != PK_METER)
            editor_set(ed, PARAMS[i].port, fx->port_value(i));
    editor_set(ed, PORT_CLIP, fx->clip_level());
}

} // namespace

// ---- Factory ----

BEGIN_FACTORY_DEF(CP_VENDOR, CP_URL, CP_EMAIL)

DEF_CLASS2(INLINE_UID(0x5E996C6D, 0x36D342EC, 0x8E41D552, 0x1E4C2AE6),
           PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           CP_NAME,
           0, // processor and editor are one object, so they can't be separated
           "Fx|Distortion",
           CP_VERSION_STR,
           kVstVersionString,
           CanaryPlus::createInstance)

END_FACTORY
