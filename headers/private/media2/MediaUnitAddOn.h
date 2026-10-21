/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _MEDIA2_MEDIA_UNIT_ADD_ON_H
#define _MEDIA2_MEDIA_UNIT_ADD_ON_H


#include <SupportDefs.h>


class BMediaUnit;


// Media unit add-ons live under media/ in the add-ons directories and export
// these symbols. The caller owns the returned unit and must delete it before
// unloading the add-on image.
extern "C" {

int32			media_unit_count();
const char*		media_unit_name(int32 index);
BMediaUnit*		media_unit_instantiate(int32 index);

// Optional: the menu group unit index is listed under, or NULL for the
// top level. The string stays valid while the add-on is loaded.
const char*		media_unit_group(int32 index);

// Optional, exported by the plugin adapter: hosts the plugin at path (a
// LADSPA library, or an LV2 bundle directory or URI). NULL on failure.
BMediaUnit*		media_plugin_load_file(const char* path);

// Optional, exported by the plugin adapter: drives the hosted plugin's
// own UI (LV2 UI extension, hosted through suil). command is one of the
// kMediaPluginUI* constants; arg interpretation is per command. Returns
// B_NOT_SUPPORTED when unit is not a plugin this add-on hosts.
status_t		media_plugin_ui_command(BMediaUnit* unit, int32 command,
								void* arg);

}

#define MEDIA_UNIT_COUNT_SYMBOL		"media_unit_count"
#define MEDIA_UNIT_NAME_SYMBOL		"media_unit_name"
#define MEDIA_UNIT_INSTANTIATE_SYMBOL	"media_unit_instantiate"
#define MEDIA_UNIT_GROUP_SYMBOL		"media_unit_group"
#define MEDIA_PLUGIN_LOAD_FILE_SYMBOL	"media_plugin_load_file"
#define MEDIA_PLUGIN_UI_COMMAND_SYMBOL	"media_plugin_ui_command"


enum {
	// arg: uint32* out flags; B_OK means the unit is a hosted plugin.
	kMediaPluginUIQuery		= 0,
	// arg: (void*)(uintptr_t) parent XID; 0 lets the UI pick its own
	// window. Synchronous; B_OK means the UI was shown.
	kMediaPluginUIOpen		= 1,
	// arg: NULL; hides and destroys the UI if one is open.
	kMediaPluginUIClose		= 2
};

enum {
	kMediaPluginUIFlagHasUi	= 0x1	// a supported UI was found
};


#endif	// _MEDIA2_MEDIA_UNIT_ADD_ON_H
