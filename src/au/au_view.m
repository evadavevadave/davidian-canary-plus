/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Audio Unit's Cocoa view: the same panel as the LV2 GUI and the VST3 editor.
 * Hosts find the factory class through kAudioUnitProperty_CocoaUI (canary_au.cpp).
 * Built without ARC. */
#import <AudioUnit/AUCocoaUIView.h>
#import <Cocoa/Cocoa.h>
#include <AudioToolbox/AudioToolbox.h>
#include <AudioToolbox/AudioUnitUtilities.h>

#include "../params.h"
#include "../ui/editor.h"
#include "au_ids.h"

#if __has_feature(objc_arc)
#    error "Build without ARC"
#endif

static AudioUnitParameter au_param(AudioUnit au, int id)
{
    AudioUnitParameter p;
    p.mAudioUnit = au;
    p.mParameterID = (AudioUnitParameterID)id;
    p.mScope = kAudioUnitScope_Global;
    p.mElement = 0;
    return p;
}

/* The user moved a control: set it and tell the host, which records automation. */
static void au_write(void* ctx, int port, float value)
{
    const int id = param_for_port(port);
    AudioUnitParameter p;
    if (id < 0 || PARAMS[id].kind == PK_METER)
        return;
    p = au_param((AudioUnit)ctx, id);
    AUParameterSet(NULL, NULL, &p, value, 0);
}

static void au_gesture(void* ctx, int port, int begin)
{
    const int id = param_for_port(port);
    AudioUnitEvent e;
    if (id < 0 || PARAMS[id].kind == PK_METER)
        return;
    memset(&e, 0, sizeof e);
    e.mEventType = begin ? kAudioUnitEvent_BeginParameterChangeGesture : kAudioUnitEvent_EndParameterChangeGesture;
    e.mArgument.mParameter = au_param((AudioUnit)ctx, id);
    AUEventListenerNotify(NULL, NULL, &e);
}

/* Picks up automation, host edits and the clip meter, about 30 times a second. */
static void au_poll(void* ctx, Editor* ed)
{
    int i;
    for (i = 0; i < P_COUNT; ++i) {
        AudioUnitParameterValue v = 0;
        if (AudioUnitGetParameter((AudioUnit)ctx, (AudioUnitParameterID)i, kAudioUnitScope_Global, 0, &v) == noErr)
            editor_set(ed, PARAMS[i].port, v);
    }
}

/* Holds the panel. The panel runs only while the view is in a window, so nothing
 * polls the unit once the host has closed the view. */
@interface AU_VIEW_CLASS : NSView {
    AudioUnit unit;
    Editor* editor;
}
- (instancetype)initWithAudioUnit:(AudioUnit)au;
@end

@implementation AU_VIEW_CLASS
- (instancetype)initWithAudioUnit:(AudioUnit)au
{
    self = [super initWithFrame:NSMakeRect(0, 0, EDITOR_W, EDITOR_H)];
    if (!self)
        return nil;
    unit = au;
    if (![self openEditor]) {
        [self release];
        return nil;
    }
    return self;
}

- (BOOL)openEditor
{
    EditorHost host;
    if (editor)
        return YES;
    host.ctx = unit;
    host.write = au_write;
    host.gesture = au_gesture;
    host.poll = au_poll;
    editor = editor_open(self, &host);
    return editor != NULL;
}

- (void)closeEditor
{
    editor_close(editor);
    editor = NULL;
}

- (BOOL)isFlipped { return YES; }

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    if ([self window])
        [self openEditor];
    else
        [self closeEditor];
}

- (void)dealloc
{
    [self closeEditor];
    [super dealloc];
}
@end

@interface AU_VIEW_FACTORY_CLASS : NSObject <AUCocoaUIBase>
@end

@implementation AU_VIEW_FACTORY_CLASS
- (unsigned)interfaceVersion { return 0; }

- (NSString*)description { return @"Davidian Canary Plus"; }

- (NSView*)uiViewForAudioUnit:(AudioUnit)au withSize:(NSSize)size
{
    (void)size; /* the panel has one size */
    return [[[AU_VIEW_CLASS alloc] initWithAudioUnit:au] autorelease];
}
@end
