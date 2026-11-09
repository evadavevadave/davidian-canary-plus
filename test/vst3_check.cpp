// SPDX-License-Identifier: GPL-3.0-or-later
// Checks the VST3 version the way a host uses it, beyond Steinberg's validator: the
// sound in mono and stereo, latency, bypass, saved state, and the editor (opened in a
// window, clicked and dragged, and updated by host-side changes). Windows only.
//
//   vst3_check <path to canary-plus.vst3>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

// Parameter IDs, from src/params.h.
enum { P_DISTORTION, P_OUTPUT, P_DIODE, P_INPUT_VOLTS, P_BYPASS, P_CLIP };

static int failures;

static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok)
        ++failures;
}

// Records the editor's edits, as a host would for automation.
struct Handler : IComponentHandler {
    int begins = 0, performs = 0, ends = 0;
    ParamID last_id = 0xffffffff;
    ParamValue last_value = -1;

    tresult PLUGIN_API beginEdit(ParamID id) override { ++begins; last_id = id; return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue v) override
    {
        ++performs;
        last_id = id;
        last_value = v;
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID id) override { ++ends; last_id = id; return kResultOk; }
    tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IComponentHandler::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = this;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    void reset() { begins = performs = ends = 0; }
};

struct Rig {
    IComponent* component = nullptr;
    IAudioProcessor* proc = nullptr;
    IEditController* ctrl = nullptr;
    int channels = 1;

    // Runs n samples through the plugin, with optional parameter changes.
    void run(std::vector<float>* in, std::vector<float>* out, int n, ParameterChanges* changes = nullptr)
    {
        float* in_ptrs[2] = { in[0].data(), channels > 1 ? in[1].data() : nullptr };
        float* out_ptrs[2] = { out[0].data(), channels > 1 ? out[1].data() : nullptr };
        AudioBusBuffers ib {}, ob {};
        ib.numChannels = ob.numChannels = channels;
        ib.channelBuffers32 = in_ptrs;
        ob.channelBuffers32 = out_ptrs;
        ParameterChanges outs(8);
        ProcessData d {};
        d.processMode = kRealtime;
        d.symbolicSampleSize = kSample32;
        d.numSamples = n;
        d.numInputs = d.numOutputs = 1;
        d.inputs = &ib;
        d.outputs = &ob;
        d.inputParameterChanges = changes;
        d.outputParameterChanges = &outs;
        proc->process(d);
    }

    bool start(int ch, double rate)
    {
        channels = ch;
        SpeakerArrangement arr = ch == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
        if (proc->setBusArrangements(&arr, 1, &arr, 1) != kResultOk)
            return false;
        ProcessSetup setup { kRealtime, kSample32, 4096, rate };
        if (proc->setupProcessing(setup) != kResultOk)
            return false;
        component->setActive(true);
        proc->setProcessing(true);
        return true;
    }

    void stop()
    {
        proc->setProcessing(false);
        component->setActive(false);
    }
};

static double rms(const std::vector<float>& v, size_t from)
{
    double s = 0;
    for (size_t i = from; i < v.size(); ++i)
        s += (double)v[i] * v[i];
    return std::sqrt(s / (double)(v.size() - from));
}

static void sine(std::vector<float>& v, double freq, double rate, double amp)
{
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = (float)(amp * std::sin(2.0 * 3.14159265358979 * freq * (double)i / rate));
}

static void pump(int ms)
{
    const DWORD until = GetTickCount() + (DWORD)ms;
    while ((int)(until - GetTickCount()) > 0) {
        MSG m;
        while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
        Sleep(5);
    }
}

// What the editor window shows, via WM_PRINTCLIENT.
static std::vector<uint32_t> grab(HWND w, int width, int height)
{
    std::vector<uint32_t> px((size_t)width * height);
    BITMAPINFO bi {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, bmp);
    SendMessageA(w, WM_PRINTCLIENT, (WPARAM)dc, PRF_CLIENT);
    GdiFlush();
    std::memcpy(px.data(), bits, px.size() * 4);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return px;
}

static size_t differing(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b)
{
    size_t n = 0;
    for (size_t i = 0; i < a.size(); ++i)
        n += (a[i] & 0xffffff) != (b[i] & 0xffffff);
    return n;
}

static void click(HWND w, int x, int y)
{
    SendMessageA(w, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
    SendMessageA(w, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: vst3_check <canary-plus.vst3>\n");
        return 2;
    }
    std::string error;
    auto module = VST3::Hosting::Module::create(argv[1], error);
    if (!module) {
        std::fprintf(stderr, "cannot load %s: %s\n", argv[1], error.c_str());
        return 1;
    }
    auto factory = module->getFactory();
    HostApplication host;
    Handler handler;
    Rig rig;
    for (auto& info : factory.classInfos()) {
        if (info.category() == kVstAudioEffectClass) {
            std::printf("Class: %s by %s, %s\n", info.name().c_str(), info.vendor().c_str(), info.subCategoriesString().c_str());
            rig.component = factory.createInstance<IComponent>(info.ID()).take();
            break;
        }
    }
    check(rig.component != nullptr, "the factory makes the effect");
    if (!rig.component)
        return 1;
    check(rig.component->initialize(&host) == kResultOk, "it initialises");
    rig.component->queryInterface(IAudioProcessor::iid, (void**)&rig.proc);
    rig.component->queryInterface(IEditController::iid, (void**)&rig.ctrl);
    check(rig.proc && rig.ctrl, "it is both processor and controller");
    if (!rig.proc || !rig.ctrl)
        return 1;
    rig.ctrl->setComponentHandler(&handler);
    check(rig.ctrl->getParameterCount() == 6, "6 parameters");
    check(rig.proc->getLatencySamples() == 32, "reports 32 samples of latency");

    const double rate = 48000.0;
    const int n = 4800;

    // ---- Mono ----
    std::printf("Mono at 48 kHz:\n");
    {
        std::vector<float> in[1] = { std::vector<float>(n) }, out[1] = { std::vector<float>(n) };
        check(rig.start(1, rate), "accepts mono");
        sine(in[0], 220.0, rate, 0.5);
        for (int i = 0; i < 4; ++i)
            rig.run(in, out, n);
        bool finite = true;
        float peak = 0;
        for (float v : out[0]) {
            finite = finite && std::isfinite(v);
            peak = std::fmax(peak, std::fabs(v));
        }
        std::printf("  output RMS %.3f, peak %.3f\n", rms(out[0], 0), peak);
        check(finite && rms(out[0], 0) > 0.02, "a sine comes out distorted, not silent");

        // Bypass: after the 10 ms crossfade, the output is the input delayed by the latency.
        ParameterChanges changes(4);
        int32 idx = 0;
        changes.addParameterData(P_BYPASS, idx)->addPoint(0, 1.0, idx);
        rig.run(in, out, n, &changes);
        rig.run(in, out, n);
        double worst = 0;
        for (int i = 32; i < n; ++i)
            worst = std::fmax(worst, std::fabs(out[0][i] - in[0][i - 32]));
        std::printf("  bypassed: largest difference from the delayed input %.2e\n", worst);
        check(worst < 1e-6, "bypass passes the input through, delayed by the latency");
        rig.stop();
    }

    // ---- Stereo: two independent circuits ----
    std::printf("Stereo at 44.1 kHz:\n");
    {
        std::vector<float> in[2] = { std::vector<float>(n), std::vector<float>(n) };
        std::vector<float> out[2] = { std::vector<float>(n), std::vector<float>(n) };
        ParameterChanges changes(4);
        int32 idx = 0;
        changes.addParameterData(P_BYPASS, idx)->addPoint(0, 0.0, idx);
        check(rig.start(2, 44100.0), "accepts stereo");
        sine(in[0], 220.0, 44100.0, 0.5);
        rig.run(in, out, n, &changes);
        for (int i = 0; i < 3; ++i)
            rig.run(in, out, n);
        std::printf("  left RMS %.3f, right RMS %.2e\n", rms(out[0], 0), rms(out[1], 0));
        check(rms(out[0], 0) > 0.02 && rms(out[1], 0) < 1e-4, "left is processed and silence stays on the right");
        rig.stop();
    }

    // ---- Saved state ----
    std::printf("State:\n");
    {
        rig.ctrl->setParamNormalized(P_DISTORTION, 0.9);
        rig.ctrl->setParamNormalized(P_DIODE, 0.75); // diode 3
        MemoryStream saved;
        check(rig.component->getState(&saved) == kResultOk, "saves");
        rig.ctrl->setParamNormalized(P_DISTORTION, 0.1);
        rig.ctrl->setParamNormalized(P_DIODE, 0.0);
        int64 pos = 0;
        saved.seek(0, IBStream::kIBSeekSet, &pos);
        check(rig.component->setState(&saved) == kResultOk, "loads");
        check(std::fabs(rig.ctrl->getParamNormalized(P_DISTORTION) - 0.9) < 1e-6
                  && std::fabs(rig.ctrl->getParamNormalized(P_DIODE) - 0.75) < 1e-6,
              "loading restores the controls");
        String128 text {};
        rig.ctrl->getParamStringByValue(P_DIODE, 0.75, text);
        char ascii[128] {};
        for (int i = 0; i < 127 && text[i]; ++i)
            ascii[i] = (char)text[i];
        std::printf("  diode 3 reads \"%s\"\n", ascii);
        check(std::strcmp(ascii, "LED (crunch)") == 0, "the diode menu shows names");
    }

    // ---- Editor ----
    std::printf("Editor:\n");
    {
        IPlugView* view = rig.ctrl->createView(ViewType::kEditor);
        check(view != nullptr, "creates an editor");
        if (!view)
            return 1;
        ViewRect size {};
        view->getSize(&size);
        check(size.getWidth() == 720 && size.getHeight() == 450, "editor is 720 x 450");
        check(view->isPlatformTypeSupported(kPlatformTypeHWND) == kResultTrue, "supports HWND");

        HWND frame = CreateWindowExA(0, "STATIC", "vst3_check", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40,
                                     760, 520, nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
        check(view->attached(frame, kPlatformTypeHWND) == kResultOk, "attaches to a host window");
        HWND child = GetWindow(frame, GW_CHILD);
        RECT rc {};
        if (child)
            GetClientRect(child, &rc);
        check(child && rc.right == 720 && rc.bottom == 450, "puts a 720 x 450 child window in it");
        pump(100);

        rig.ctrl->setParamNormalized(P_BYPASS, 0.0);
        pump(100);
        handler.reset();
        click(child, 360, 386); // the Engage footswitch
        check(handler.begins == 1 && handler.performs == 1 && handler.ends == 1 && handler.last_id == P_BYPASS
                  && handler.last_value == 1.0,
              "clicking the footswitch bypasses, as one begin-perform-end edit");
        check(rig.ctrl->getParamNormalized(P_BYPASS) == 1.0, "the bypass parameter follows the footswitch");

        handler.reset();
        const double before = rig.ctrl->getParamNormalized(P_DISTORTION);
        SendMessageA(child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(116, 178)); // the Distortion knob
        for (int y = 178; y >= 158; y -= 4)
            SendMessageA(child, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(116, y));
        SendMessageA(child, WM_LBUTTONUP, 0, MAKELPARAM(116, 158));
        std::printf("  drag: %d begin, %d perform, %d end; distortion %.3f -> %.3f\n", handler.begins,
                    handler.performs, handler.ends, before, rig.ctrl->getParamNormalized(P_DISTORTION));
        check(handler.begins == 1 && handler.ends == 1 && handler.performs >= 2, "a knob drag is one gesture");
        check(rig.ctrl->getParamNormalized(P_DISTORTION) < before + 0.2
                  && rig.ctrl->getParamNormalized(P_DISTORTION) != before,
              "dragging the knob changes it");

        // A change from the host (automation) shows up on the panel.
        pump(100);
        const std::vector<uint32_t> a = grab(child, 720, 450);
        rig.ctrl->setParamNormalized(P_OUTPUT, 0.0);
        pump(150);
        const std::vector<uint32_t> b = grab(child, 720, 450);
        std::printf("  host change redrew %zu pixels\n", differing(a, b));
        check(differing(a, b) > 50, "the panel follows host-side changes");

        check(view->removed() == kResultOk, "detaches");
        check(GetWindow(frame, GW_CHILD) == nullptr, "and removes its window");
        view->release();
        DestroyWindow(frame);
    }

    rig.ctrl->setComponentHandler(nullptr);
    rig.proc->release();
    rig.ctrl->release();
    rig.component->terminate();
    rig.component->release();

    std::printf(failures ? "%d check(s) FAILED\n" : "All checks passed\n", failures);
    return failures ? 1 : 0;
}
