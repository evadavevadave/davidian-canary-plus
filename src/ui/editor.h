/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The panel as an embeddable editor, for the VST3 and Audio Unit versions.
 * Values are the plugin's own (port) values, not host-normalised ones. */
#ifndef UI_EDITOR_H
#define UI_EDITOR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Editor Editor;

typedef struct {
    void* ctx;
    /* The user changed a control. */
    void (*write)(void* ctx, int port, float value);
    /* Brackets each edit (begin = 1, end = 0). May be NULL. */
    void (*gesture)(void* ctx, int port, int begin);
    /* Called about 30 times a second: pass the host's current values to editor_set. */
    void (*poll)(void* ctx, Editor* ed);
} EditorHost;

#define EDITOR_W 720
#define EDITOR_H 450

/* Creates the editor's window (HWND, NSView* or X11 Window) inside parent, or on
 * its own when parent is NULL. Returns NULL on failure. */
Editor* editor_open(void* parent, const EditorHost* host);
void editor_close(Editor* ed);
void* editor_widget(Editor* ed);
void editor_set(Editor* ed, int port, float value);

#ifdef __cplusplus
}
#endif

#endif
