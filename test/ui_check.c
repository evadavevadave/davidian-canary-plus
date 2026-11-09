/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Loads the built GUI library the way an LV2 host does, embeds it in a host window,
 * drives it with mouse events and checks the values it sends back. It also compares
 * what the window shows with a reference render, which catches colour-order and
 * upside-down mistakes in each platform's window layer.
 * Windows (Win32), macOS (Cocoa; built as Objective-C) and Linux (X11; needs a display,
 * for example xvfb-run).
 * Usage: ui_check <bundle folder> */
#if defined(_WIN32)
#    define _CRT_SECURE_NO_WARNINGS
#elif !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#endif
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/lv2_abi.h"
#include "../src/ui/panel.h" /* for the reference render */
#include "platform.h"

typedef void (*WriteFn)(void*, uint32_t, uint32_t, uint32_t, const void*);
typedef struct {
    const char* URI;
    void* (*instantiate)(const void*, const char*, const char*, WriteFn, void*, void**, const LV2_Feature* const*);
    void (*cleanup)(void*);
    void (*port_event)(void*, uint32_t, uint32_t, uint32_t, const void*);
    const void* (*extension_data)(const char*);
} UiDesc;
typedef struct { int (*idle)(void*); } IdleIface;

static int failures;
static int last_port = -1, writes;
static float last_value;
static void* g_ui;
static const IdleIface* g_idle;

static void check(const char* what, int ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}

static void on_write(void* controller, uint32_t port, uint32_t size, uint32_t format, const void* buf)
{
    (void)controller;
    if (size == sizeof(float) && format == 0) {
        last_port = (int)port;
        last_value = *(const float*)buf;
        ++writes;
    }
}

/* ---- Host side, per platform ----
 * host_open() makes the host's window and returns what goes in the ui:parent feature.
 * The ev_* functions deliver one mouse event to the GUI's widget at panel coordinates.
 * grab() captures the widget's pixels, then grabbed(x, y) returns one as 0x00RRGGBB. */

#if defined(_WIN32)
static HWND host_win;
static uint32_t* shot;

static void pump(void)
{
    MSG m;
    while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
}
static void* host_open(void)
{
    host_win = CreateWindowExA(0, "STATIC", "host", WS_OVERLAPPEDWINDOW, 0, 0, 760, 500, NULL, NULL, NULL, NULL);
    return host_win;
}
static void host_close(void) { DestroyWindow(host_win); free(shot); }
static int is_child(void* w) { return GetParent((HWND)w) == host_win; }
static void widget_size(void* w, int* width, int* height)
{
    RECT r;
    GetClientRect((HWND)w, &r);
    *width = r.right;
    *height = r.bottom;
}
static int widget_gone(void* w) { return !IsWindow((HWND)w); }
static void ev_down(void* w, int x, int y, int clicks)
{
    SendMessageA((HWND)w, clicks == 2 ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
}
static void ev_move(void* w, int x, int y) { SendMessageA((HWND)w, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x, y)); }
static void ev_up(void* w, int x, int y) { SendMessageA((HWND)w, WM_LBUTTONUP, 0, MAKELPARAM(x, y)); }
static void ev_wheel(void* w, int x, int y, int notches)
{
    POINT pt;
    pt.x = x;
    pt.y = y;
    ClientToScreen((HWND)w, &pt);
    SendMessageA((HWND)w, WM_MOUSEWHEEL, MAKEWPARAM(0, notches * WHEEL_DELTA), MAKELPARAM(pt.x, pt.y));
}
static int grab(void* w)
{
    HDC screen = GetDC(NULL), mem = CreateCompatibleDC(screen);
    BITMAPINFO bi;
    void* bits = NULL;
    HBITMAP bmp;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = PANEL_W;
    bi.bmiHeader.biHeight = -PANEL_H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    SelectObject(mem, bmp);
    SendMessageA((HWND)w, WM_PRINTCLIENT, (WPARAM)mem, PRF_CLIENT);
    GdiFlush();
    if (!shot)
        shot = (uint32_t*)malloc(4u * PANEL_W * PANEL_H);
    memcpy(shot, bits, 4u * PANEL_W * PANEL_H);
    DeleteDC(mem);
    DeleteObject(bmp);
    ReleaseDC(NULL, screen);
    return 1;
}
static uint32_t grabbed(int x, int y) { return shot[y * PANEL_W + x] & 0xffffff; }

#elif defined(__APPLE__)
#    import <Cocoa/Cocoa.h>
static NSWindow* host_win;
static NSBitmapImageRep* shot;

static void pump(void)
{
    NSEvent* e;
    while ((e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:nil inMode:NSDefaultRunLoopMode dequeue:YES]))
        [NSApp sendEvent:e];
}
static void* host_open(void)
{
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    host_win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 760, 500) styleMask:NSWindowStyleMaskBorderless
                                             backing:NSBackingStoreBuffered defer:NO];
    [host_win setFrameOrigin:NSMakePoint(0, 0)];
    return [host_win contentView];
}
static void host_close(void) { [host_win close]; }
static int is_child(void* w) { return [(NSView*)w superview] == [host_win contentView]; }
static void widget_size(void* w, int* width, int* height)
{
    const NSRect r = [(NSView*)w frame];
    *width = (int)r.size.width;
    *height = (int)r.size.height;
}
static int widget_gone(void* w) { (void)w; return [[[host_win contentView] subviews] count] == 0; }
static NSEvent* mouse(void* w, NSEventType type, int x, int y, int clicks)
{
    const NSPoint p = [(NSView*)w convertPoint:NSMakePoint(x, y) toView:nil];
    return [NSEvent mouseEventWithType:type location:p modifierFlags:0 timestamp:0
                          windowNumber:[host_win windowNumber] context:nil eventNumber:0
                            clickCount:clicks pressure:1.0f];
}
static void ev_down(void* w, int x, int y, int clicks) { [(NSView*)w mouseDown:mouse(w, NSEventTypeLeftMouseDown, x, y, clicks)]; }
static void ev_move(void* w, int x, int y) { [(NSView*)w mouseDragged:mouse(w, NSEventTypeLeftMouseDragged, x, y, 1)]; }
static void ev_up(void* w, int x, int y) { [(NSView*)w mouseUp:mouse(w, NSEventTypeLeftMouseUp, x, y, 1)]; }
static void ev_wheel(void* w, int x, int y, int notches)
{
    /* The host window sits at the screen origin, so window and screen points match. */
    const NSPoint p = [host_win convertPointToScreen:[(NSView*)w convertPoint:NSMakePoint(x, y) toView:nil]];
    const CGFloat top = [[[NSScreen screens] objectAtIndex:0] frame].size.height;
    CGEventRef cg = CGEventCreateScrollWheelEvent(NULL, kCGScrollEventUnitLine, 1, notches);
    CGEventSetLocation(cg, CGPointMake(p.x, top - p.y));
    [(NSView*)w scrollWheel:[NSEvent eventWithCGEvent:cg]];
    CFRelease(cg);
}
static int grab(void* w)
{
    NSView* v = (NSView*)w;
    shot = [v bitmapImageRepForCachingDisplayInRect:[v bounds]];
    [v cacheDisplayInRect:[v bounds] toBitmapImageRep:shot];
    return shot != nil;
}
static uint32_t grabbed(int x, int y)
{
    /* Retina screens double the pixels; sample the middle of the point. */
    const double s = (double)[shot pixelsWide] / PANEL_W;
    NSColor* c = [[shot colorAtX:(NSInteger)((x + 0.5) * s) y:(NSInteger)((y + 0.5) * s)]
        colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]];
    return ((uint32_t)lround([c redComponent] * 255) << 16) | ((uint32_t)lround([c greenComponent] * 255) << 8)
         | (uint32_t)lround([c blueComponent] * 255);
}

#else
#    include <X11/Xlib.h>
#    include <X11/Xutil.h>
#    include <time.h>
static Display* xd;
static Window host_win;
static Time xtime = 1000;
static XImage* shot;
static int x_error;

static int on_x_error(Display* d, XErrorEvent* e) { (void)d; (void)e; x_error = 1; return 0; }
static void pump(void)
{
    int i;
    struct timespec ms = { 0, 2000000 };
    XSync(xd, False);
    for (i = 0; i < 10; ++i) {
        if (g_idle)
            g_idle->idle(g_ui);
        nanosleep(&ms, NULL);
    }
}
static void* host_open(void)
{
    xd = XOpenDisplay(NULL);
    if (!xd) {
        printf("no X display: run under xvfb-run or a desktop session\n");
        return NULL;
    }
    XSetErrorHandler(on_x_error);
    host_win = XCreateSimpleWindow(xd, DefaultRootWindow(xd), 0, 0, 760, 500, 0, 0, 0);
    XMapWindow(xd, host_win);
    XSync(xd, False);
    return (void*)(uintptr_t)host_win;
}
static void host_close(void)
{
    if (shot)
        XDestroyImage(shot);
    XDestroyWindow(xd, host_win);
    XCloseDisplay(xd);
}
static int is_child(void* w)
{
    Window root, parent, *kids = NULL;
    unsigned n;
    if (!XQueryTree(xd, (Window)(uintptr_t)w, &root, &parent, &kids, &n))
        return 0;
    if (kids)
        XFree(kids);
    return parent == host_win;
}
static void widget_size(void* w, int* width, int* height)
{
    XWindowAttributes a;
    XGetWindowAttributes(xd, (Window)(uintptr_t)w, &a);
    *width = a.width;
    *height = a.height;
}
static int widget_gone(void* w)
{
    XWindowAttributes a;
    x_error = 0;
    XGetWindowAttributes(xd, (Window)(uintptr_t)w, &a);
    XSync(xd, False);
    return x_error;
}
static void send_button(void* w, int type, int x, int y, unsigned button, unsigned state)
{
    XEvent e;
    memset(&e, 0, sizeof e);
    e.xbutton.type = type;
    e.xbutton.display = xd;
    e.xbutton.window = (Window)(uintptr_t)w;
    e.xbutton.root = DefaultRootWindow(xd);
    e.xbutton.time = xtime;
    e.xbutton.x = x;
    e.xbutton.y = y;
    e.xbutton.state = state;
    e.xbutton.button = button;
    e.xbutton.same_screen = True;
    XSendEvent(xd, e.xbutton.window, False, type == ButtonPress ? ButtonPressMask : ButtonReleaseMask, &e);
    pump();
}
static void ev_down(void* w, int x, int y, int clicks)
{
    if (clicks == 2) { /* a double-click is a quick press, release, press */
        xtime += 1000;
        send_button(w, ButtonPress, x, y, 1, 0);
        xtime += 10;
        send_button(w, ButtonRelease, x, y, 1, Button1Mask);
        xtime += 100;
    } else {
        xtime += 1000;
    }
    send_button(w, ButtonPress, x, y, 1, 0);
}
static void ev_move(void* w, int x, int y)
{
    XEvent e;
    memset(&e, 0, sizeof e);
    e.xmotion.type = MotionNotify;
    e.xmotion.display = xd;
    e.xmotion.window = (Window)(uintptr_t)w;
    e.xmotion.root = DefaultRootWindow(xd);
    e.xmotion.time = ++xtime;
    e.xmotion.x = x;
    e.xmotion.y = y;
    e.xmotion.state = Button1Mask;
    e.xmotion.same_screen = True;
    XSendEvent(xd, e.xmotion.window, False, PointerMotionMask, &e);
    pump();
}
static void ev_up(void* w, int x, int y) { send_button(w, ButtonRelease, x, y, 1, Button1Mask); }
static void ev_wheel(void* w, int x, int y, int notches)
{
    const unsigned b = notches > 0 ? 4 : 5;
    int i;
    for (i = 0; i < abs(notches); ++i) {
        xtime += 1000;
        send_button(w, ButtonPress, x, y, b, 0);
        send_button(w, ButtonRelease, x, y, b, 0);
    }
}
static int grab(void* w)
{
    if (shot)
        XDestroyImage(shot);
    shot = XGetImage(xd, (Window)(uintptr_t)w, 0, 0, PANEL_W, PANEL_H, AllPlanes, ZPixmap);
    return shot != NULL;
}
static uint32_t grabbed(int x, int y) { return (uint32_t)XGetPixel(shot, x, y) & 0xffffff; }
#endif

static void click(void* w, int x, int y)
{
    ev_down(w, x, y, 1);
    ev_up(w, x, y);
}

static void drag(void* w, int x, int y, int dy)
{
    int i;
    ev_down(w, x, y, 1);
    for (i = 1; i <= 6; ++i)
        ev_move(w, x, y + dy * i / 6);
    ev_up(w, x, y + dy);
}

/* Samples a grid over the window and counts pixels that differ from the reference. */
static void compare_with_reference(void* w, const float* vals)
{
    Panel ref;
    int x, y, n = 0, bad = 0, i;
    char label[160];
    if (!panel_init(&ref, NULL, NULL)) {
        check("reference panel", 0);
        return;
    }
    for (i = PORT_DISTORTION; i < PORT_COUNT; ++i)
        panel_set(&ref, i, vals[i]);
    panel_draw(&ref);
    pump();
    if (!grab(w)) {
        check("captures the window's pixels", 0);
        panel_free(&ref);
        return;
    }
    for (y = 3; y < PANEL_H; y += 7) {
        for (x = 5; x < PANEL_W; x += 11) {
            const uint32_t a = ref.px[y * PANEL_W + x] & 0xffffff, b = grabbed(x, y);
            const int dr = (int)(a >> 16) - (int)(b >> 16);
            const int dg = (int)((a >> 8) & 255) - (int)((b >> 8) & 255);
            const int db = (int)(a & 255) - (int)(b & 255);
            ++n;
            if (abs(dr) > 24 || abs(dg) > 24 || abs(db) > 24)
                ++bad;
        }
    }
    snprintf(label, sizeof label, "window shows the panel the right way up and in the right colours (%d of %d samples differ)", bad, n);
    check(label, bad * 50 <= n); /* allow 2% for edge smoothing when a Retina screen scales it */
    panel_free(&ref);
}

int main(int argc, char** argv)
{
    char path[1024];
    Lib lib;
    const UiDesc* (*get)(uint32_t);
    const UiDesc* d;
    void* parent;
    void* child;
    LV2_Feature parent_feature;
    const LV2_Feature* features[2];
    void* widget = NULL;
    int i, width = 0, height = 0;
    char label[160];
    static const float vals[PORT_COUNT] = { 0, 0, 0.5f, 0.8f, 0, 1.0f, 1, 32, 0.4f };

    if (argc < 2) {
        printf("usage: ui_check <bundle folder>\n");
        return 2;
    }
    snprintf(path, sizeof path, "%s/canary-plus_ui" LIB_SUFFIX, argv[1]);
    lib = lib_open(path);
    if (!lib)
        printf("  %s\n", lib_error());
    check("GUI library loads", lib != NULL);
    if (!lib)
        return 1;
    *(void**)&get = lib_sym(lib, "lv2ui_descriptor");
    check("exports lv2ui_descriptor", get != NULL);
    if (!get)
        return 1;
    d = get(0);
    check("descriptor has the GUI's URI", d && strcmp(d->URI, DP_UI_URI) == 0);
    check("only one GUI in the library", get(1) == NULL);

    parent = host_open();
    check("host window opens", parent != NULL);
    if (!parent)
        return 1;
    parent_feature.URI = "http://lv2plug.in/ns/extensions/ui#parent";
    parent_feature.data = parent;
    features[0] = &parent_feature;
    features[1] = NULL;

    check("refuses another plugin's URI",
          d->instantiate(d, "urn:other", argv[1], on_write, NULL, &widget, features) == NULL);
    g_ui = d->instantiate(d, DP_URI, argv[1], on_write, NULL, &widget, features);
    check("instantiates inside the host's window", g_ui != NULL && widget != NULL);
    if (!g_ui)
        return 1;
    child = widget;
    g_idle = (const IdleIface*)d->extension_data("http://lv2plug.in/ns/extensions/ui#idleInterface");
    check("offers the idle interface", g_idle != NULL);
    check("the widget is a child of the host's window", is_child(child));
    widget_size(child, &width, &height);
    snprintf(label, sizeof label, "window is %dx%d", width, height);
    check(label, width == PANEL_W && height == PANEL_H);

    /* The host sends every control port's value first, outputs included. */
    for (i = PORT_DISTORTION; i < PORT_COUNT; ++i)
        d->port_event(g_ui, (uint32_t)i, sizeof(float), 0, &vals[i]);
    check("port updates from the host write nothing back", writes == 0);
    compare_with_reference(child, vals);

    drag(child, 116, 178, 36);
    snprintf(label, sizeof label, "dragging Distortion down 36 px sends port %d = %.3f", last_port, last_value);
    check(label, last_port == PORT_DISTORTION && fabsf(last_value - 0.3f) < 0.01f);

    drag(child, 604, 178, -400);
    snprintf(label, sizeof label, "dragging Output far up stops at %.3f", last_value);
    check(label, last_port == PORT_OUTPUT && last_value == 1.0f);

    ev_down(child, 116, 178, 2);
    ev_up(child, 116, 178);
    snprintf(label, sizeof label, "double-click resets Distortion to %.2f", last_value);
    check(label, last_port == PORT_DISTORTION && fabsf(last_value - 0.5f) < 1e-4f);

    /* Bottom row first: row 1 is already selected, and re-selecting it sends nothing. */
    for (i = 4; i >= 0; --i) {
        click(child, 330, 112 + 34 * i);
        snprintf(label, sizeof label, "clicking diode row %d selects %.0f", i + 1, last_value);
        check(label, last_port == PORT_DIODE && last_value == (float)i);
    }

    drag(child, 116, 372, -90);
    snprintf(label, sizeof label, "dragging Input V up half the range sends %.2f V", last_value);
    check(label, last_port == PORT_INPUT_VOLTS && fabsf(last_value - 10.0f) < 0.05f);

    click(child, 360, 386);
    check("footswitch bypasses", last_port == PORT_ENABLED && last_value == 0.0f);
    click(child, 360, 386);
    check("footswitch engages again", last_port == PORT_ENABLED && last_value == 1.0f);

    writes = 0;
    ev_wheel(child, 116, 178, 1);
    snprintf(label, sizeof label, "mouse wheel up on Distortion sends %.3f", last_value);
    check(label, writes == 1 && last_port == PORT_DISTORTION && fabsf(last_value - 0.525f) < 1e-4f);
    ev_wheel(child, 330, 180, -1);
    check("mouse wheel down on the diode list moves down one row", last_port == PORT_DIODE && last_value == 1.0f);

    writes = 0;
    click(child, 5, 5);
    click(child, 604, 368);
    check("clicking empty panel or the clip lamp sends nothing", writes == 0);
    {
        const float clip = 0.9f;
        d->port_event(g_ui, PORT_CLIP, sizeof(float), 0, &clip);
        pump();
        check("clip lamp updates write nothing back", writes == 0);
    }

    d->cleanup(g_ui);
    g_ui = NULL;
    g_idle = NULL;
    check("cleanup destroys the window", widget_gone(child));
    host_close();
    lib_close(lib);
    printf(failures ? "\n%d FAILED\n" : "\nAll passed\n", failures);
    return failures ? 1 : 0;
}
