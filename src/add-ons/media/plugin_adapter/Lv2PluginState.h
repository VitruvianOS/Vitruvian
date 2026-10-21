/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#ifndef __LV2_PLUGIN_STATE_H__
#define __LV2_PLUGIN_STATE_H__


#include <map>
#include <string.h>
#include <string>
#include <vector>

#include <SupportDefs.h>

#include <lilv/lilv.h>

#include <lv2/atom/atom.h>
#include <lv2/core/lv2.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/data-access/data-access.h>
#include <lv2/instance-access/instance-access.h>
#include <lv2/options/options.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>


typedef struct LilvUIImpl LilvUI;


// One audio buffer per port covers a whole cycle; matches the LADSPA
// path's cycle cap.
static const uint32 kMaxBlockSize = 256;
static const size_t kMaxLv2AudioPorts = 16;


struct Lv2PluginState {
	LilvWorld*			world;
	const LilvPlugin*	plugin;
	LilvInstance*		instance;

	// Minimal urid:map/unmap; nearly every modern plugin needs it.
	std::map<std::string, LV2_URID>	uridTable;
	std::vector<char*>	uridStrings;
	LV2_URID_Map		map;
	LV2_URID_Unmap		unmap;

	// Port layout; per-index vectors parallel the lilv port indices.
	std::vector<uint8>	portKind;		// bit0 input, bit1 audio, bit2 control
	std::vector<float*>	audioBuffers;	// NULL unless the port is audio
	std::vector<uint32>	audioInPorts;
	std::vector<uint32>	audioOutPorts;
	std::vector<uint32>	controlPorts;	// input control ports, param order
	std::vector<float>	controlMin;
	std::vector<float>	controlMax;
	std::vector<float>	portValues;		// control cells by port index

	// Features passed to instantiate; storage outlives the instance.
	int32				minBlock;
	int32				maxBlock;
	LV2_Options_Option	options[3];
	LV2_Feature			featureMap;
	LV2_Feature			featureUnmap;
	LV2_Feature			featureOptions;
	LV2_Feature			featureBounded;
	const LV2_Feature*	featureList[5];

	// UI features; instance access and data access ride on the DSP.
	LV2_Feature			featureInstanceAccess;
	LV2_Feature			featureDataAccess;
	LV2_Feature			featureIdle;
	const LilvUI*		ui;
	const char*			uiTypeUri;
	bool				uiExternal;

	Lv2PluginState()
		:
		world(NULL),
		plugin(NULL),
		instance(NULL),
		minBlock(1),
		maxBlock(8192),
		ui(NULL),
		uiTypeUri(NULL),
		uiExternal(false)
	{
		memset(&map, 0, sizeof(map));
		memset(&unmap, 0, sizeof(unmap));
		memset(options, 0, sizeof(options));
		memset(&featureMap, 0, sizeof(featureMap));
		memset(&featureUnmap, 0, sizeof(featureUnmap));
		memset(&featureOptions, 0, sizeof(featureOptions));
		memset(&featureBounded, 0, sizeof(featureBounded));
		memset(&featureInstanceAccess, 0, sizeof(featureInstanceAccess));
		memset(&featureDataAccess, 0, sizeof(featureDataAccess));
		memset(&featureIdle, 0, sizeof(featureIdle));
		memset(featureList, 0, sizeof(featureList));
	}
};


#endif	// __LV2_PLUGIN_STATE_H__
