/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Checks the Audio Unit the way a host uses it, alongside Apple's auval: the sound in
 * mono and stereo, latency, the Engage switch, the clip meter, saved state, and the
 * Cocoa view (opened in a window, clicked and dragged, and updated by host-side
 * changes). Loads the component in-process, so it needn't be installed. Built without ARC.
 *
 *   au_check <path to canary-plus.component> */
#import <AudioUnit/AUCocoaUIView.h>
#import <Cocoa/Cocoa.h>
#include <AudioToolbox/AudioToolbox.h>
#include <AudioToolbox/AudioUnitUtilities.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Parameter IDs, from src/params.h. */
enum { P_DISTORTION, P_OUTPUT, P_DIODE, P_INPUT_VOLTS, P_ENGAGE, P_CLIP };

#define RATE 48000.0
#define LEN 4800  /* 220 Hz repeats exactly every 4800 samples at 48 kHz */
#define CHUNK 480
#define LATENCY 32

static int failures;

static void check(int ok, const char* what)
{
    printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok)
        ++failures;
}

/* ---- Audio ---- */

typedef struct {
    float left[LEN];
    long pos; /* samples supplied so far */
} Source;

static OSStatus supply_input(void* ref, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* ts, UInt32 bus,
                             UInt32 frames, AudioBufferList* io)
{
    Source* s = (Source*)ref;
    UInt32 b, i;
    (void)flags;
    (void)ts;
    (void)bus;
    for (b = 0; b < io->mNumberBuffers; ++b) {
        float* d = (float*)io->mBuffers[b].mData;
        for (i = 0; i < frames; ++i)
            d[i] = b == 0 ? s->left[(s->pos + i) % LEN] : 0.0f; /* the right channel stays silent */
    }
    s->pos += frames;
    return noErr;
}

static int setup(AudioUnit au, Source* src, UInt32 channels)
{
    AudioStreamBasicDescription f;
    AURenderCallbackStruct cb;
    UInt32 max_frames = 4096;
    memset(&f, 0, sizeof f);
    f.mSampleRate = RATE;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    f.mBytesPerPacket = 4;
    f.mFramesPerPacket = 1;
    f.mBytesPerFrame = 4;
    f.mChannelsPerFrame = channels;
    f.mBitsPerChannel = 32;
    cb.inputProc = supply_input;
    cb.inputProcRefCon = src;
    AudioUnitUninitialize(au);
    if (AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f)
        || AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f)
        || AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb)
        || AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &max_frames,
                                sizeof max_frames))
        return 0;
    return AudioUnitInitialize(au) == noErr;
}

/* Renders LEN samples per channel into out, in CHUNK-sized slices as a host would. */
static int render(AudioUnit au, UInt32 channels, float out[][LEN], double* sample_time)
{
    int at;
    for (at = 0; at < LEN; at += CHUNK) {
        struct {
            AudioBufferList list;
            AudioBuffer more;
        } abl;
        AudioTimeStamp ts;
        AudioUnitRenderActionFlags flags = 0;
        UInt32 c;
        abl.list.mNumberBuffers = channels;
        for (c = 0; c < channels; ++c) {
            abl.list.mBuffers[c].mNumberChannels = 1;
            abl.list.mBuffers[c].mDataByteSize = CHUNK * sizeof(float);
            abl.list.mBuffers[c].mData = out[c] + at;
        }
        memset(&ts, 0, sizeof ts);
        ts.mSampleTime = *sample_time;
        ts.mFlags = kAudioTimeStampSampleTimeValid;
        if (AudioUnitRender(au, &flags, &ts, 0, CHUNK, &abl.list) != noErr)
            return 0;
        /* A unit may answer with its own buffers; copy back if it did. */
        for (c = 0; c < channels; ++c)
            if (abl.list.mBuffers[c].mData != out[c] + at)
                memcpy(out[c] + at, abl.list.mBuffers[c].mData, CHUNK * sizeof(float));
        *sample_time += CHUNK;
    }
    return 1;
}

static double rms(const float* v, int n)
{
    double s = 0;
    int i;
    for (i = 0; i < n; ++i)
        s += (double)v[i] * v[i];
    return sqrt(s / n);
}

static float get(AudioUnit au, int id)
{
    AudioUnitParameterValue v = -1;
    AudioUnitGetParameter(au, (AudioUnitParameterID)id, kAudioUnitScope_Global, 0, &v);
    return v;
}

static void set(AudioUnit au, int id, float v)
{
    AudioUnitSetParameter(au, (AudioUnitParameterID)id, kAudioUnitScope_Global, 0, v, 0);
}

/* ---- View ---- */

typedef struct {
    int begins[8], ends[8], changes[8];
} Events;

static void on_event(void* ref, void* object, const AudioUnitEvent* e, UInt64 host_time, AudioUnitParameterValue value)
{
    Events* ev = (Events*)ref;
    const UInt32 id = e->mArgument.mParameter.mParameterID;
    (void)object;
    (void)host_time;
    (void)value;
    if (id >= 8)
        return;
    if (e->mEventType == kAudioUnitEvent_BeginParameterChangeGesture)
        ev->begins[id]++;
    else if (e->mEventType == kAudioUnitEvent_EndParameterChangeGesture)
        ev->ends[id]++;
    else if (e->mEventType == kAudioUnitEvent_ParameterValueChange)
        ev->changes[id]++;
}

static void pump(double seconds)
{
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds, false);
}

static NSEvent* mouse(NSEventType type, NSView* panel, NSWindow* w, CGFloat x, CGFloat y)
{
    const NSPoint p = [panel convertPoint:NSMakePoint(x, y) toView:nil];
    return [NSEvent mouseEventWithType:type location:p modifierFlags:0 timestamp:0 windowNumber:[w windowNumber]
                               context:nil eventNumber:0 clickCount:1 pressure:1];
}

static NSBitmapImageRep* grab(NSView* v)
{
    NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect:[v bounds]];
    [v cacheDisplayInRect:[v bounds] toBitmapImageRep:rep];
    return rep;
}

static long differing(NSBitmapImageRep* a, NSBitmapImageRep* b)
{
    const long n = (long)[a bytesPerRow] * [a pixelsHigh];
    const unsigned char* pa = [a bitmapData];
    const unsigned char* pb = [b bitmapData];
    long i, d = 0;
    if ((long)[b bytesPerRow] * [b pixelsHigh] != n)
        return -1;
    for (i = 0; i < n; ++i)
        d += pa[i] != pb[i];
    return d;
}

static void check_view(AudioUnit au)
{
    UInt32 size = 0;
    Boolean writable = false;
    AudioUnitCocoaViewInfo* info;
    NSBundle* bundle;
    Class factory_class;
    id<AUCocoaUIBase> factory;
    NSView *view, *panel;
    NSWindow* win;
    AUEventListenerRef listener = NULL;
    Events ev;
    int id;

    printf("View:\n");
    check(AudioUnitGetPropertyInfo(au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &size, &writable) == noErr
              && size >= sizeof(AudioUnitCocoaViewInfo),
          "offers a Cocoa view");
    info = (AudioUnitCocoaViewInfo*)calloc(1, size ? size : sizeof *info);
    if (AudioUnitGetProperty(au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, info, &size) != noErr) {
        check(0, "describes the view");
        free(info);
        return;
    }
    bundle = [NSBundle bundleWithURL:(NSURL*)info->mCocoaAUViewBundleLocation];
    factory_class = [bundle classNamed:(NSString*)info->mCocoaAUViewClass[0]];
    printf("  view factory %s in %s\n", [(NSString*)info->mCocoaAUViewClass[0] UTF8String],
           [[(NSURL*)info->mCocoaAUViewBundleLocation path] UTF8String]);
    CFRelease(info->mCocoaAUViewBundleLocation);
    CFRelease(info->mCocoaAUViewClass[0]);
    free(info);
    check(factory_class && [factory_class conformsToProtocol:@protocol(AUCocoaUIBase)], "the factory class loads");
    if (!factory_class)
        return;
    factory = [[factory_class alloc] init];
    view = [[factory uiViewForAudioUnit:au withSize:NSZeroSize] retain];
    check(view && NSEqualSizes([view frame].size, NSMakeSize(720, 450)), "the view is 720 x 450");
    if (!view)
        return;

    win = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, 720, 450) styleMask:NSWindowStyleMaskTitled
                                        backing:NSBackingStoreBuffered defer:NO];
    [win setReleasedWhenClosed:NO];
    [[win contentView] addSubview:view];
    [win orderFront:nil];
    panel = [[view subviews] firstObject];
    check(panel != nil, "the panel is inside it");
    if (!panel)
        return;

    memset(&ev, 0, sizeof ev);
    AUEventListenerCreate(on_event, &ev, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode, 0.0, 0.0, &listener);
    for (id = 0; id < P_CLIP; ++id) {
        static const AudioUnitEventType types[] = { kAudioUnitEvent_BeginParameterChangeGesture,
                                                    kAudioUnitEvent_EndParameterChangeGesture,
                                                    kAudioUnitEvent_ParameterValueChange };
        int t;
        for (t = 0; t < 3; ++t) {
            AudioUnitEvent e;
            memset(&e, 0, sizeof e);
            e.mEventType = types[t];
            e.mArgument.mParameter.mAudioUnit = au;
            e.mArgument.mParameter.mParameterID = (AudioUnitParameterID)id;
            e.mArgument.mParameter.mScope = kAudioUnitScope_Global;
            AUEventListenerAddEventType(listener, NULL, &e);
        }
    }

    set(au, P_ENGAGE, 1);
    pump(0.15);
    memset(&ev, 0, sizeof ev);
    [panel mouseDown:mouse(NSEventTypeLeftMouseDown, panel, win, 360, 386)]; /* the Engage footswitch */
    [panel mouseUp:mouse(NSEventTypeLeftMouseUp, panel, win, 360, 386)];
    pump(0.2);
    printf("  footswitch: Engage %.0f; %d begin, %d change, %d end\n", get(au, P_ENGAGE), ev.begins[P_ENGAGE],
           ev.changes[P_ENGAGE], ev.ends[P_ENGAGE]);
    check(get(au, P_ENGAGE) == 0.0f, "clicking the footswitch turns Engage off");
    check(ev.begins[P_ENGAGE] == 1 && ev.ends[P_ENGAGE] == 1 && ev.changes[P_ENGAGE] >= 1,
          "the host hears one gesture with the change");

    {
        const float before = get(au, P_DISTORTION);
        int y;
        memset(&ev, 0, sizeof ev);
        [panel mouseDown:mouse(NSEventTypeLeftMouseDown, panel, win, 116, 178)]; /* the Distortion knob */
        for (y = 174; y >= 158; y -= 4)
            [panel mouseDragged:mouse(NSEventTypeLeftMouseDragged, panel, win, 116, y)];
        [panel mouseUp:mouse(NSEventTypeLeftMouseUp, panel, win, 116, 158)];
        pump(0.2);
        printf("  drag: distortion %.3f -> %.3f; %d begin, %d change, %d end\n", before, get(au, P_DISTORTION),
               ev.begins[P_DISTORTION], ev.changes[P_DISTORTION], ev.ends[P_DISTORTION]);
        check(get(au, P_DISTORTION) > before, "dragging the knob up turns it up");
        check(ev.begins[P_DISTORTION] == 1 && ev.ends[P_DISTORTION] == 1, "a knob drag is one gesture");
    }

    {
        NSBitmapImageRep *a, *b;
        long d;
        set(au, P_OUTPUT, 0.8f);
        pump(0.15);
        a = [grab(panel) retain];
        set(au, P_OUTPUT, 0.0f);
        pump(0.15);
        b = grab(panel);
        d = differing(a, b);
        printf("  host change redrew %ld bytes\n", d);
        check(d > 200, "the panel follows host-side changes");
        [a release];
    }

    [view removeFromSuperview];
    check([[view subviews] count] == 0, "closing the view stops the panel");
    AUListenerDispose(listener);
    [view release];
    [(NSObject*)factory release];
    [win close];
    [win release];
}

int main(int argc, char** argv)
{
    @autoreleasepool {
        AudioComponentDescription desc = { kAudioUnitType_Effect, 'Cnry', 'Dvdn', 0, 0 };
        CFURLRef url;
        CFBundleRef bundle;
        AudioComponentFactoryFunction factory;
        AudioComponent comp;
        AudioUnit au = NULL;
        static Source src;
        static float out[2][LEN];
        double t = 0;
        int i;

        if (argc < 2) {
            fprintf(stderr, "usage: au_check <canary-plus.component>\n");
            return 2;
        }
        [NSApplication sharedApplication];
        url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8*)argv[1], (CFIndex)strlen(argv[1]), true);
        bundle = CFBundleCreate(NULL, url);
        CFRelease(url);
        if (!bundle || !CFBundleLoadExecutable(bundle)) {
            fprintf(stderr, "cannot load %s\n", argv[1]);
            return 1;
        }
        factory = (AudioComponentFactoryFunction)CFBundleGetFunctionPointerForName(bundle, CFSTR("CanaryPlusAUFactory"));
        check(factory != NULL, "exports CanaryPlusAUFactory");
        if (!factory)
            return 1;
        comp = AudioComponentRegister(&desc, CFSTR("Davidian: Canary Plus"), 0x10000, factory);
        check(comp && AudioComponentInstanceNew(comp, &au) == noErr && au, "makes an instance");
        if (!au)
            return 1;

        for (i = 0; i < LEN; ++i)
            src.left[i] = (float)(0.5 * sin(2.0 * M_PI * 220.0 * i / RATE));

        printf("Parameters:\n");
        {
            UInt32 size = 0;
            Boolean w;
            CFArrayRef names = NULL;
            AudioUnitGetPropertyInfo(au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &size, &w);
            check(size / sizeof(AudioUnitParameterID) == 6, "6 parameters");
            size = sizeof names;
            AudioUnitGetProperty(au, kAudioUnitProperty_ParameterValueStrings, kAudioUnitScope_Global, P_DIODE, &names,
                                 &size);
            check(names && CFArrayGetCount(names) == 5
                      && CFStringCompare((CFStringRef)CFArrayGetValueAtIndex(names, 3), CFSTR("LED (crunch)"), 0)
                             == kCFCompareEqualTo,
                  "the diode menu has 5 named entries");
            if (names)
                CFRelease(names);
        }

        printf("Mono at 48 kHz:\n");
        check(setup(au, &src, 1), "accepts mono");
        {
            Float64 latency = 0;
            UInt32 size = sizeof latency;
            AudioUnitGetProperty(au, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &latency, &size);
            printf("  latency %.0f samples\n", latency * RATE);
            check(fabs(latency * RATE - LATENCY) < 1e-6, "reports 32 samples of latency");
        }
        check(render(au, 1, out, &t), "renders");
        for (i = 0; i < 3; ++i)
            render(au, 1, out, &t);
        printf("  output RMS %.3f\n", rms(out[0], LEN));
        check(isfinite(rms(out[0], LEN)) && rms(out[0], LEN) > 0.02, "a sine comes out distorted, not silent");

        set(au, P_DISTORTION, 1.0f);
        render(au, 1, out, &t);
        render(au, 1, out, &t);
        printf("  clip meter at full distortion %.2f\n", get(au, P_CLIP));
        check(get(au, P_CLIP) > 0.1f, "the clip meter reads");

        set(au, P_ENGAGE, 0.0f);
        render(au, 1, out, &t);
        render(au, 1, out, &t);
        {
            /* Disengaged, after the 10 ms crossfade, the output is the input delayed by the latency. */
            const long start = src.pos - LEN;
            double worst = 0;
            for (i = LATENCY; i < LEN; ++i)
                worst = fmax(worst, fabs(out[0][i] - src.left[(start + i - LATENCY) % LEN]));
            printf("  disengaged: largest difference from the delayed input %.2e\n", worst);
            check(worst < 1e-6, "Engage off passes the input through, delayed by the latency");
        }

        printf("Stereo:\n");
        set(au, P_ENGAGE, 1.0f);
        check(setup(au, &src, 2), "accepts stereo");
        for (i = 0; i < 4; ++i)
            render(au, 2, out, &t);
        printf("  left RMS %.3f, right RMS %.2e\n", rms(out[0], LEN), rms(out[1], LEN));
        check(rms(out[0], LEN) > 0.02 && rms(out[1], LEN) < 1e-4, "left is processed and silence stays on the right");

        printf("State:\n");
        {
            CFPropertyListRef state = NULL;
            UInt32 size = sizeof state;
            set(au, P_DISTORTION, 0.25f);
            set(au, P_DIODE, 2.0f);
            check(AudioUnitGetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, &size)
                          == noErr
                      && state,
                  "saves");
            set(au, P_DISTORTION, 0.9f);
            set(au, P_DIODE, 0.0f);
            check(state
                      && AudioUnitSetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state,
                                              sizeof state)
                             == noErr,
                  "loads");
            check(fabsf(get(au, P_DISTORTION) - 0.25f) < 1e-6f && get(au, P_DIODE) == 2.0f, "loading restores the controls");
            if (state)
                CFRelease(state);
        }

        check_view(au);

        AudioUnitUninitialize(au);
        AudioComponentInstanceDispose(au);
        printf(failures ? "%d check(s) FAILED\n" : "All checks passed\n", failures);
        return failures ? 1 : 0;
    }
}
