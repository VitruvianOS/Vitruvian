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

// Optional, exported by the plugin adapter: hosts the plugin at path (a
// LADSPA library, or an LV2 bundle directory or URI). NULL on failure.
BMediaUnit*		media_plugin_load_file(const char* path);

}

#define MEDIA_UNIT_COUNT_SYMBOL		"media_unit_count"
#define MEDIA_UNIT_NAME_SYMBOL		"media_unit_name"
#define MEDIA_UNIT_INSTANTIATE_SYMBOL	"media_unit_instantiate"
#define MEDIA_PLUGIN_LOAD_FILE_SYMBOL	"media_plugin_load_file"


#endif	// _MEDIA2_MEDIA_UNIT_ADD_ON_H
