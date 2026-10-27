/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <media2/FilterCatalog.h>

#include <new>
#include <vector>

#include <gst/gst.h>

#include <Autolock.h>
#include <Locker.h>
#include <ObjectList.h>

#include <media2/MediaFilter.h>
#include <media2/NativeAudioUnits.h>
#include <media2/NativeMidiUnits.h>


namespace {


struct CustomFilter {
	BString		id;
	BString		name;
	BString		kind;
	BString		description;
};


static BLocker sCustomLock("filter catalog customs");
static std::vector<CustomFilter> sCustomFilters;
static BLocker sScanLock("filter catalog scan");


static void
_AddEntry(BObjectList<BFilterCatalog::Entry, true>* out, const char* id,
	const char* name, const char* kind, const char* group,
	const char* factory)
{
	BFilterCatalog::Entry* entry = new(std::nothrow) BFilterCatalog::Entry();
	if (entry == NULL)
		return;
	entry->id = id;
	entry->name = name;
	entry->kind = kind;
	entry->group = group;
	entry->factory = factory != NULL ? factory : "";
	if (!out->AddItem(entry))
		delete entry;
}


static void
_ScanRegistry(BObjectList<BFilterCatalog::Entry, true>* out,
	const char* kindFilter)
{
	BAutolock _(sScanLock);
	if (!gst_is_initialized() && !gst_init_check(NULL, NULL, NULL))
		return;

	GstRegistry* registry = gst_registry_get();
	GList* features = gst_registry_get_feature_list(registry,
		GST_TYPE_ELEMENT_FACTORY);

	for (GList* item = features; item != NULL; item = item->next) {
		GstPluginFeature* feature = (GstPluginFeature*)item->data;
		const char* factoryName
			= gst_plugin_feature_get_name(feature);

		if (g_str_has_prefix(factoryName, "frei0r-filter-")) {
			if (kindFilter == NULL || strcmp(kindFilter, "video") == 0) {
				const char* longName = gst_element_factory_get_longname(
					(GstElementFactory*)feature);
				_AddEntry(out, factoryName,
					longName != NULL ? longName : factoryName,
					"video", "frei0r", factoryName);
			}
		} else if (g_str_has_prefix(factoryName, "ladspa-")) {
			if (kindFilter == NULL || strcmp(kindFilter, "audio") == 0) {
				_AddEntry(out, factoryName, factoryName, "audio", "LADSPA",
					factoryName);
			}
		}
	}

	if (features != NULL)
		g_list_free(features);
}


} // anonymous namespace


/* static */ status_t
BFilterCatalog::ListEntries(BObjectList<Entry, true>* outEntries,
	const char* kindFilter)
{
	if (outEntries == NULL)
		return B_BAD_VALUE;
	outEntries->MakeEmpty();

	if (kindFilter == NULL || strcmp(kindFilter, "video") == 0) {
		_AddEntry(outEntries, "balance", "Brightness/Contrast/Saturation",
			"video", "Built-in", NULL);
		_AddEntry(outEntries, "grayscale", "Grayscale", "video", "Built-in",
			NULL);
		_AddEntry(outEntries, "sepia", "Sepia", "video", "Built-in", NULL);
		_AddEntry(outEntries, "invert", "Invert", "video", "Built-in", NULL);
		_AddEntry(outEntries, "warm", "Warm", "video", "Built-in", NULL);
		_AddEntry(outEntries, "cool", "Cool", "video", "Built-in", NULL);
		_AddEntry(outEntries, "vintage", "Vintage", "video", "Built-in", NULL);
		_AddEntry(outEntries, "vignette", "Vignette", "video", "Built-in",
			NULL);
		_AddEntry(outEntries, "blur", "Blur", "video", "Built-in", NULL);
		_AddEntry(outEntries, "sharpen", "Sharpen", "video", "Built-in", NULL);
		_AddEntry(outEntries, "mirror", "Mirror", "video", "Built-in", NULL);
	}

	if (kindFilter == NULL || strcmp(kindFilter, "audio") == 0) {
		_AddEntry(outEntries, "equalizer", "Equalizer", "audio", "Built-in",
			NULL);
		_AddEntry(outEntries, "gain", "Gain", "audio", "Built-in", NULL);
		_AddEntry(outEntries, "compressor", "Compressor", "audio", "Built-in",
			NULL);
		_AddEntry(outEntries, "reverb", "Reverb", "audio", "Built-in", NULL);
		_AddEntry(outEntries, "native-gain", "Gain", "audio", "Native", NULL);
		_AddEntry(outEntries, "native-compressor", "Compressor", "audio",
			"Native", NULL);
		_AddEntry(outEntries, "native-reverb", "Reverb", "audio", "Native",
			NULL);
	}

	if (kindFilter == NULL || strcmp(kindFilter, "midi") == 0) {
		_AddEntry(outEntries, "midi-out", "MIDI Out", "midi", "MIDI", NULL);
		_AddEntry(outEntries, "midi-keyboard", "Virtual Keyboard", "midi",
			"MIDI", NULL);
		_AddEntry(outEntries, "midi-monitor", "MIDI Monitor", "midi", "MIDI",
			NULL);
		_AddEntry(outEntries, "midi-transpose", "Transpose", "midi", "MIDI",
			NULL);
		_AddEntry(outEntries, "midi-channel-filter", "Channel Filter", "midi",
			"MIDI", NULL);
		_AddEntry(outEntries, "midi-key-split", "Key Split", "midi", "MIDI",
			NULL);
		_AddEntry(outEntries, "midi-velocity-curve", "Velocity Curve", "midi",
			"MIDI", NULL);
		_AddEntry(outEntries, "midi-file-player", "MIDI File Player", "midi",
			"MIDI", NULL);
		_AddEntry(outEntries, "midi-clock", "Clock", "midi", "MIDI", NULL);
		_AddEntry(outEntries, "midi-arp", "Arpeggiator", "midi", "MIDI",
			NULL);
		_AddEntry(outEntries, "midi-chord", "Chord", "midi", "MIDI", NULL);
		_AddEntry(outEntries, "midi-cc-mapper", "CC Mapper", "midi", "MIDI",
			NULL);
	}

	_ScanRegistry(outEntries, kindFilter);

	{
		BAutolock _(sCustomLock);
		for (size_t i = 0; i < sCustomFilters.size(); i++) {
			const CustomFilter& custom = sCustomFilters[i];
			if (kindFilter != NULL && custom.kind != kindFilter)
				continue;
			_AddEntry(outEntries, custom.id.String(), custom.name.String(),
				custom.kind.String(), "Custom", custom.description.String());
		}
	}

	return B_OK;
}


/* static */ BMediaFilter*
BFilterCatalog::CreateVideoUnit(const Entry& entry)
{
	if (entry.kind != "video")
		return NULL;

	BMediaFilter* unit = NULL;
	if (entry.id == "balance")
		unit = new(std::nothrow) BVideoBalanceFilter();
	else if (entry.id == "grayscale")
		unit = new(std::nothrow) BGrayscaleFilter();
	else if (entry.id == "sepia")
		unit = new(std::nothrow) BSepiaFilter();
	else if (entry.id == "invert")
		unit = new(std::nothrow) BInvertFilter();
	else if (entry.id == "warm")
		unit = new(std::nothrow) BWarmFilter();
	else if (entry.id == "cool")
		unit = new(std::nothrow) BCoolFilter();
	else if (entry.id == "vintage")
		unit = new(std::nothrow) BVintageFilter();
	else if (entry.id == "vignette")
		unit = new(std::nothrow) BVignetteFilter();
	else if (entry.id == "blur")
		unit = new(std::nothrow) BBlurFilter();
	else if (entry.id == "sharpen")
		unit = new(std::nothrow) BSharpenFilter();
	else if (entry.id == "mirror")
		unit = new(std::nothrow) BVideoFlipFilter(true);
	else if (entry.factory.Length() > 0)
		unit = new(std::nothrow) BMediaFilter(entry.name.String(),
			entry.factory.String(), B_MEDIA_RAW_VIDEO);

	if (unit != NULL && unit->InitCheck() != B_OK) {
		delete unit;
		return NULL;
	}
	return unit;
}


/* static */ BMediaClient*
BFilterCatalog::CreateAudioUnit(const Entry& entry)
{
	if (entry.kind != "audio")
		return NULL;

	BNativeAudioUnit* native = NULL;
	if (entry.id == "native-gain")
		native = new(std::nothrow) BGainUnit();
	else if (entry.id == "native-compressor")
		native = new(std::nothrow) BCompressorUnit();
	else if (entry.id == "native-reverb")
		native = new(std::nothrow) BReverbUnit();
	if (native != NULL) {
		if (native->InitCheck() != B_OK) {
			delete native;
			return NULL;
		}
		return native;
	}

	BMediaFilter* unit = NULL;
	if (entry.id == "equalizer")
		unit = new(std::nothrow) BEqualizerFilter();
	else if (entry.id == "gain")
		unit = new(std::nothrow) BGainFilter();
	else if (entry.id == "compressor")
		unit = new(std::nothrow) BCompressorFilter();
	else if (entry.id == "reverb")
		unit = new(std::nothrow) BReverbFilter();
	else if (entry.factory.Length() > 0)
		unit = new(std::nothrow) BMediaFilter(entry.name.String(),
			entry.factory.String(), B_MEDIA_RAW_AUDIO);

	if (unit != NULL && unit->InitCheck() != B_OK) {
		delete unit;
		return NULL;
	}
	return unit;
}


/* static */ BMediaClient*
BFilterCatalog::CreateMidiUnit(const Entry& entry)
{
	if (entry.kind != "midi")
		return NULL;

	BMidiNativeUnit* unit = NULL;
	if (entry.id == "midi-out")
		unit = new(std::nothrow) BMidiOutUnit();
	else if (entry.id == "midi-keyboard")
		unit = new(std::nothrow) BMidiKeyboardUnit();
	else if (entry.id == "midi-monitor")
		unit = new(std::nothrow) BMidiMonitorUnit();
	else if (entry.id == "midi-transpose")
		unit = new(std::nothrow) BTransposeUnit();
	else if (entry.id == "midi-channel-filter")
		unit = new(std::nothrow) BChannelFilterUnit();
	else if (entry.id == "midi-key-split")
		unit = new(std::nothrow) BKeySplitUnit();
	else if (entry.id == "midi-velocity-curve")
		unit = new(std::nothrow) BVelocityCurveUnit();
	else if (entry.id == "midi-file-player")
		unit = new(std::nothrow) BMidiFilePlayerUnit();
	else if (entry.id == "midi-clock")
		unit = new(std::nothrow) BClockUnit();
	else if (entry.id == "midi-arp")
		unit = new(std::nothrow) BArpeggiatorUnit();
	else if (entry.id == "midi-chord")
		unit = new(std::nothrow) BChordUnit();
	else if (entry.id == "midi-cc-mapper")
		unit = new(std::nothrow) BCCMapperUnit();

	if (unit != NULL && unit->InitCheck() != B_OK) {
		delete unit;
		return NULL;
	}
	return unit;
}


status_t
BPrivate::media::RegisterCustomFilter(const char* id, const char* name,
	const char* kind, const char* description)
{
	if (id == NULL || kind == NULL || description == NULL)
		return B_BAD_VALUE;
	if (strcmp(kind, "video") != 0 && strcmp(kind, "audio") != 0)
		return B_BAD_VALUE;

	BAutolock _(sCustomLock);
	CustomFilter custom;
	custom.id = id;
	custom.name = name != NULL ? name : id;
	custom.kind = kind;
	custom.description = description;
	sCustomFilters.push_back(custom);
	return B_OK;
}
