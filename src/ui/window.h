/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Thin window layer that shows a Panel: Win32, Cocoa (macOS) and X11 (Linux and BSD).
 * Shared by the LV2 GUI, the VST3 editor and the Audio Unit view. On macOS, a file
 * that includes this must be compiled as Objective-C without ARC.
 *
 *   window_open(w, parent)  creates the window, as a child of parent if not NULL
 *   window_close(w)         destroys it
 *   window_widget(w)        HWND, NSView* or X11 Window id
 *   window_redraw(w)        asks for a repaint if the panel changed
 *   window_idle(w)          for hosts that pump the GUI (X11 needs this)
 *
 * The window calls w->tick(w->tick_ctx), if set, about 30 times a second, before
 * repainting. Hosts use it to pick up parameter changes and meter values. */
#ifndef UI_WINDOW_H
#define UI_WINDOW_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "panel.h"

#define DOUBLE_CLICK_MS 350

typedef struct PanelWindow PanelWindow;
typedef void (*WindowTick)(void* ctx);
static void window_redraw(PanelWindow* w);

static void window_tick(PanelWindow* w);

/* ---- Win32 ---- */
#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <windowsx.h>

struct PanelWindow {
    Panel panel;
    WindowTick tick;
    void* tick_ctx;
    HWND hwnd;
    BITMAPINFO bmi;
};

/* Window classes belong to the module that registers them, so each plugin
 * binary can use the same name. */
static const char* const CLASS_NAME = "CanaryPlusPanel";
static int class_users;

static HINSTANCE this_module(void)
{
    HMODULE m = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(void*)&this_module, &m);
    return (HINSTANCE)m;
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PanelWindow* w = (PanelWindow*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    if (!w)
        return DefWindowProcA(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (w->panel.dirty)
            panel_draw(&w->panel);
        SetDIBitsToDevice(dc, 0, 0, PANEL_W, PANEL_H, 0, 0, 0, PANEL_H, w->panel.px, &w->bmi, DIB_RGB_COLORS);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        if (w->panel.dirty)
            panel_draw(&w->panel);
        SetDIBitsToDevice((HDC)wp, 0, 0, PANEL_W, PANEL_H, 0, 0, 0, PANEL_H, w->panel.px, &w->bmi, DIB_RGB_COLORS);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        SetCapture(hwnd);
        panel_mouse_down(&w->panel, (float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), msg == WM_LBUTTONDBLCLK);
        window_redraw(w);
        return 0;
    case WM_MOUSEMOVE:
        panel_mouse_move(&w->panel, (float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), (wp & MK_SHIFT) != 0);
        window_redraw(w);
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (msg == WM_LBUTTONUP)
            ReleaseCapture();
        panel_mouse_up(&w->panel);
        window_redraw(w);
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt;
        pt.x = GET_X_LPARAM(lp);
        pt.y = GET_Y_LPARAM(lp);
        ScreenToClient(hwnd, &pt);
        panel_wheel(&w->panel, (float)pt.x, (float)pt.y, GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA,
                    (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0);
        window_redraw(w);
        return 0;
    }
    case WM_MOUSEACTIVATE:
        SetFocus(hwnd); /* so the wheel reaches this window */
        return MA_ACTIVATE;
    case WM_TIMER:
        window_tick(w);
        return 0;
    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static int window_open(PanelWindow* w, void* parent)
{
    WNDCLASSEXA wc;
    HINSTANCE inst = this_module();
    if (class_users++ == 0) {
        memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc;
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = wndproc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = CLASS_NAME;
        if (!RegisterClassExA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            --class_users;
            return 0;
        }
    }
    memset(&w->bmi, 0, sizeof w->bmi);
    w->bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    w->bmi.bmiHeader.biWidth = PANEL_W;
    w->bmi.bmiHeader.biHeight = -PANEL_H; /* top-down */
    w->bmi.bmiHeader.biPlanes = 1;
    w->bmi.bmiHeader.biBitCount = 32;
    w->bmi.bmiHeader.biCompression = BI_RGB;
    w->hwnd = CreateWindowExA(0, CLASS_NAME, "Canary Plus",
                              (parent ? WS_CHILD : WS_OVERLAPPEDWINDOW) | WS_VISIBLE | WS_CLIPSIBLINGS,
                              0, 0, PANEL_W, PANEL_H, (HWND)parent, NULL, inst, NULL);
    if (!w->hwnd) {
        if (--class_users == 0)
            UnregisterClassA(CLASS_NAME, inst);
        return 0;
    }
    SetWindowLongPtrA(w->hwnd, GWLP_USERDATA, (LONG_PTR)w);
    SetTimer(w->hwnd, 1, 33, NULL); /* picks up host updates such as the clip lamp */
    return 1;
}

static void window_close(PanelWindow* w)
{
    if (w->hwnd) {
        KillTimer(w->hwnd, 1);
        SetWindowLongPtrA(w->hwnd, GWLP_USERDATA, 0);
        DestroyWindow(w->hwnd);
        w->hwnd = NULL;
        if (--class_users == 0)
            UnregisterClassA(CLASS_NAME, this_module());
    }
}

static void* window_widget(PanelWindow* w) { return (void*)w->hwnd; }

static void window_redraw(PanelWindow* w)
{
    if (w->panel.dirty && w->hwnd)
        InvalidateRect(w->hwnd, NULL, FALSE);
}

/* The window's own timer drives redraws, so idle has nothing to do. */
static int window_idle(PanelWindow* w) { window_redraw(w); return 0; }

/* ---- Cocoa (macOS) ---- */
#elif defined(__APPLE__)
#    import <Cocoa/Cocoa.h>
#    if __has_feature(objc_arc)
#        error "Build without ARC: the window struct holds plain object pointers."
#    endif

/* Objective-C classes share one namespace per process, so each plugin binary
 * names its view class differently. */
#    ifndef PANEL_VIEW_CLASS
#        define PANEL_VIEW_CLASS DavidianCanaryPlusView
#    endif

@interface PANEL_VIEW_CLASS : NSView {
@public
    PanelWindow* ui;
    NSTimer* timer;
}
@end

struct PanelWindow {
    Panel panel;
    WindowTick tick;
    void* tick_ctx;
    PANEL_VIEW_CLASS* view;
    CGColorSpaceRef space;
};

static NSPoint panel_point(NSView* view, NSEvent* e)
{
    return [view convertPoint:[e locationInWindow] fromView:nil];
}

@implementation PANEL_VIEW_CLASS
- (BOOL)isFlipped { return YES; } /* top-left origin, like the panel */
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)e { (void)e; return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)drawRect:(NSRect)dirty_rect
{
    CGContextRef cg;
    CGDataProviderRef data;
    CGImageRef img;
    (void)dirty_rect;
    if (!ui)
        return;
    if (ui->panel.dirty)
        panel_draw(&ui->panel);
    cg = [[NSGraphicsContext currentContext] CGContext];
    /* Pixels are 0x00RRGGBB words, so they read as BGRX bytes in memory. */
    data = CGDataProviderCreateWithData(NULL, ui->panel.px, (size_t)PANEL_W * PANEL_H * 4, NULL);
    img = CGImageCreate(PANEL_W, PANEL_H, 8, 32, PANEL_W * 4, ui->space,
                        kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little, data, NULL, false,
                        kCGRenderingIntentDefault);
    CGContextSaveGState(cg);
    CGContextTranslateCTM(cg, 0, PANEL_H); /* undo the flip: images draw bottom-up */
    CGContextScaleCTM(cg, 1, -1);
    CGContextDrawImage(cg, CGRectMake(0, 0, PANEL_W, PANEL_H), img);
    CGContextRestoreGState(cg);
    CGImageRelease(img);
    CGDataProviderRelease(data);
}

- (void)mouseDown:(NSEvent*)e
{
    const NSPoint p = panel_point(self, e);
    if (!ui)
        return;
    panel_mouse_down(&ui->panel, (float)p.x, (float)p.y, [e clickCount] == 2);
    window_redraw(ui);
}

- (void)mouseDragged:(NSEvent*)e
{
    const NSPoint p = panel_point(self, e);
    if (!ui)
        return;
    panel_mouse_move(&ui->panel, (float)p.x, (float)p.y, ([e modifierFlags] & NSEventModifierFlagShift) != 0);
    window_redraw(ui);
}

- (void)mouseUp:(NSEvent*)e
{
    (void)e;
    if (!ui)
        return;
    panel_mouse_up(&ui->panel);
    window_redraw(ui);
}

- (void)scrollWheel:(NSEvent*)e
{
    const NSPoint p = panel_point(self, e);
    /* Trackpads report pixels, mouse wheels report lines; the panel wants notches. */
    const CGFloat dy = [e hasPreciseScrollingDeltas] ? [e scrollingDeltaY] / 10.0 : [e scrollingDeltaY];
    if (!ui)
        return;
    if (dy != 0)
        panel_wheel(&ui->panel, (float)p.x, (float)p.y, (float)dy, ([e modifierFlags] & NSEventModifierFlagShift) != 0);
    window_redraw(ui);
}

- (void)tick:(NSTimer*)t
{
    (void)t;
    if (ui)
        window_tick(ui);
}
@end

static int window_open(PanelWindow* w, void* parent)
{
    w->space = CGColorSpaceCreateDeviceRGB();
    w->view = [[PANEL_VIEW_CLASS alloc] initWithFrame:NSMakeRect(0, 0, PANEL_W, PANEL_H)];
    if (!w->view) {
        CGColorSpaceRelease(w->space);
        return 0;
    }
    w->view->ui = w;
    w->view->timer = [NSTimer timerWithTimeInterval:0.033 target:w->view selector:@selector(tick:)
                                           userInfo:nil repeats:YES];
    [[NSRunLoop currentRunLoop] addTimer:w->view->timer forMode:NSRunLoopCommonModes];
    if (parent)
        [(NSView*)parent addSubview:w->view];
    return 1;
}

static void window_close(PanelWindow* w)
{
    if (!w->view)
        return;
    [w->view->timer invalidate]; /* the timer holds a reference to the view */
    w->view->timer = nil;
    w->view->ui = NULL;
    [w->view removeFromSuperview];
    [w->view release];
    w->view = nil;
    CGColorSpaceRelease(w->space);
}

static void* window_widget(PanelWindow* w) { return (void*)w->view; }

static void window_redraw(PanelWindow* w)
{
    if (w && w->view && w->panel.dirty)
        [w->view setNeedsDisplay:YES];
}

/* The view's own timer drives redraws, so idle has nothing to do. */
static int window_idle(PanelWindow* w) { window_redraw(w); return 0; }

/* ---- X11 ---- */
#elif defined(__unix__)
#    include <X11/Xlib.h>
#    include <X11/Xutil.h>

struct PanelWindow {
    Panel panel;
    WindowTick tick;
    void* tick_ctx;
    Display* dpy;
    Window win;
    GC gc;
    XImage* img;
    Time last_click;
    int last_x, last_y;
};

static int window_open(PanelWindow* w, void* parent)
{
    XSetWindowAttributes a;
    Visual* vis;
    int screen, depth;
    Window root;
    w->dpy = XOpenDisplay(NULL);
    if (!w->dpy)
        return 0;
    screen = DefaultScreen(w->dpy);
    vis = DefaultVisual(w->dpy, screen);
    depth = DefaultDepth(w->dpy, screen);
    if (depth < 24 || vis->red_mask != 0xff0000 || vis->blue_mask != 0xff) {
        XCloseDisplay(w->dpy); /* this simple layer needs a standard 24/32-bit RGB visual */
        w->dpy = NULL;
        return 0;
    }
    root = parent ? (Window)(uintptr_t)parent : RootWindow(w->dpy, screen);
    memset(&a, 0, sizeof a);
    a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask;
    a.background_pixel = 0;
    w->win = XCreateWindow(w->dpy, root, 0, 0, PANEL_W, PANEL_H, 0, depth, InputOutput, vis,
                           CWEventMask | CWBackPixel, &a);
    w->gc = XCreateGC(w->dpy, w->win, 0, NULL);
    w->img = XCreateImage(w->dpy, vis, (unsigned)depth, ZPixmap, 0, (char*)w->panel.px,
                          PANEL_W, PANEL_H, 32, 0);
    if (!w->img) {
        XFreeGC(w->dpy, w->gc);
        XDestroyWindow(w->dpy, w->win);
        XCloseDisplay(w->dpy);
        w->dpy = NULL;
        return 0;
    }
    XMapRaised(w->dpy, w->win);
    XFlush(w->dpy);
    return 1;
}

static void window_close(PanelWindow* w)
{
    if (!w->dpy)
        return;
    w->img->data = NULL; /* the panel owns the pixels */
    XDestroyImage(w->img);
    XFreeGC(w->dpy, w->gc);
    XDestroyWindow(w->dpy, w->win);
    XCloseDisplay(w->dpy);
    w->dpy = NULL;
}

static void* window_widget(PanelWindow* w) { return (void*)(uintptr_t)w->win; }

static void blit(PanelWindow* w)
{
    XPutImage(w->dpy, w->win, w->gc, w->img, 0, 0, 0, 0, PANEL_W, PANEL_H);
    XFlush(w->dpy);
}

static void window_redraw(PanelWindow* w)
{
    if (w->panel.dirty && w->dpy) {
        panel_draw(&w->panel);
        blit(w);
    }
}

/* X11 has no window timer here: the host's idle calls drive events and ticks. */
static int window_idle(PanelWindow* w)
{
    if (!w->dpy)
        return 1;
    while (XPending(w->dpy)) {
        XEvent e;
        XNextEvent(w->dpy, &e);
        switch (e.type) {
        case Expose:
            if (e.xexpose.count == 0) {
                if (w->panel.dirty)
                    panel_draw(&w->panel);
                blit(w);
            }
            break;
        case ButtonPress:
            if (e.xbutton.button == 1) {
                const int dbl = e.xbutton.time - w->last_click < DOUBLE_CLICK_MS
                                && abs(e.xbutton.x - w->last_x) < 4 && abs(e.xbutton.y - w->last_y) < 4;
                panel_mouse_down(&w->panel, (float)e.xbutton.x, (float)e.xbutton.y, dbl);
                w->last_click = dbl ? 0 : e.xbutton.time;
                w->last_x = e.xbutton.x;
                w->last_y = e.xbutton.y;
            } else if (e.xbutton.button == 4 || e.xbutton.button == 5) {
                panel_wheel(&w->panel, (float)e.xbutton.x, (float)e.xbutton.y,
                            e.xbutton.button == 4 ? 1.0f : -1.0f, (e.xbutton.state & ShiftMask) != 0);
            }
            break;
        case MotionNotify:
            panel_mouse_move(&w->panel, (float)e.xmotion.x, (float)e.xmotion.y,
                             (e.xmotion.state & ShiftMask) != 0);
            break;
        case ButtonRelease:
            if (e.xbutton.button == 1)
                panel_mouse_up(&w->panel);
            break;
        default:
            break;
        }
    }
    window_tick(w);
    return 0;
}

#else
#    error "No window layer for this platform: Windows, macOS and X11 are supported."
#endif

static void window_tick(PanelWindow* w)
{
    if (w->tick)
        w->tick(w->tick_ctx);
    window_redraw(w);
}

#endif
