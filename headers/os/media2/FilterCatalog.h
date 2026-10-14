/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_FILTER_CATALOG_H
#define _MEDIA2_FILTER_CATALOG_H


#include <String.h>


class BAudioFilterUnit;
class BVideoFilterUnit;

template<class T, bool O> class BObjectList;


class BFilterCatalog {
public:
	struct Entry {
		BString		id;
		BString		name;
		BString		kind;		// "video" or "audio"
		BString		group;		// "Built-in", "frei0r", "LADSPA", "LV2", "Custom"
		BString		factory;	// GStreamer factory for plugin entries
	};

	// kindFilter: "video", "audio" or NULL for both.
	static	status_t			ListEntries(BObjectList<Entry, true>* outEntries,
									const char* kindFilter);

	static	BVideoFilterUnit*	CreateVideoUnit(const Entry& entry);
	static	BAudioFilterUnit*	CreateAudioUnit(const Entry& entry);

	// Add-on hook: user filters described by a GStreamer element chain.
	static	status_t			RegisterCustomFilter(const char* id,
									const char* name, const char* kind,
									const char* gstDescription);
};


#endif // _MEDIA2_FILTER_CATALOG_H
