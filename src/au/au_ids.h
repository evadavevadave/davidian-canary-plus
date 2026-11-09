/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Identity of the Audio Unit, shared by the code, its view and Info.plist.in. */
#ifndef AU_IDS_H
#define AU_IDS_H

#define AU_BUNDLE_ID "io.github.evadavevadave.davidian-canary-plus.component"

/* Objective-C class names are global in a process, so these carry the plugin's name. */
#define AU_VIEW_FACTORY_CLASS DavidianCanaryPlusAUViewFactory
#define AU_VIEW_FACTORY_CLASS_NAME "DavidianCanaryPlusAUViewFactory"
#define AU_VIEW_CLASS DavidianCanaryPlusAUView

#endif
