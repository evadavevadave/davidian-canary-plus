/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The panel as an embeddable editor (see editor.h). Compiled as Objective-C on macOS. */
#include "editor.h"
#include "window.h"

#if PANEL_W != EDITOR_W || PANEL_H != EDITOR_H
#    error "EDITOR_W and EDITOR_H must match the panel size"
#endif

struct Editor {
    PanelWindow win;
    EditorHost host;
};

static void editor_write(void* ctx, int port, float value)
{
    Editor* ed = (Editor*)ctx;
    if (ed->host.write)
        ed->host.write(ed->host.ctx, port, value);
}

static void editor_gesture(void* ctx, int port, int begin)
{
    Editor* ed = (Editor*)ctx;
    if (ed->host.gesture)
        ed->host.gesture(ed->host.ctx, port, begin);
}

static void editor_tick(void* ctx)
{
    Editor* ed = (Editor*)ctx;
    if (ed->host.poll)
        ed->host.poll(ed->host.ctx, ed);
}

Editor* editor_open(void* parent, const EditorHost* host)
{
    Editor* ed = (Editor*)calloc(1, sizeof(Editor));
    if (!ed)
        return NULL;
    ed->host = *host;
    if (!panel_init(&ed->win.panel, editor_write, ed)) {
        free(ed);
        return NULL;
    }
    ed->win.panel.gesture = editor_gesture;
    /* Show the host's values from the start, not the panel's defaults. */
    editor_tick(ed);
    if (ed->win.panel.dirty)
        panel_draw(&ed->win.panel);
    ed->win.tick = editor_tick;
    ed->win.tick_ctx = ed;
    if (!window_open(&ed->win, parent)) {
        panel_free(&ed->win.panel);
        free(ed);
        return NULL;
    }
    return ed;
}

void editor_close(Editor* ed)
{
    if (!ed)
        return;
    window_close(&ed->win);
    panel_free(&ed->win.panel);
    free(ed);
}

void* editor_widget(Editor* ed) { return window_widget(&ed->win); }

void editor_set(Editor* ed, int port, float value)
{
    /* Ignore host echoes of a knob being dragged, so it doesn't jitter. */
    if (ed->win.panel.drag >= 0 && WIDGETS[ed->win.panel.drag].port == port)
        return;
    panel_set(&ed->win.panel, port, value);
}
