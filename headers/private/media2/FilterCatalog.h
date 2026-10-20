/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _MEDIA2_FILTER_CATALOG_H
#define _MEDIA2_FILTER_CATALOG_H


#include <String.h>


class BMediaFilter;

template<class T, bool O> class BObjectList;


class BFilterCatalog {
public:
	struct Entry {
		BString		id;
		BString		name;
		BString		kind;		// "video" or "audio"
		BString		group;		// "Built-in", "frei0r", "LADSPA", "LV2", "Custom"
		BString		factory;	// backend factory for plugin entries
	};

	// kindFilter: "video", "audio" or NULL for both.
	static	status_t			ListEntries(BObjectList<Entry, true>* outEntries,
									const char* kindFilter);

	static	BMediaFilter*		CreateVideoUnit(const Entry& entry);
	static	BMediaFilter*		CreateAudioUnit(const Entry& entry);
};


namespace BPrivate {
namespace media {


// Add-on hook: a user filter described by a backend element chain.
// kind is "video" or "audio".
status_t	RegisterCustomFilter(const char* id, const char* name,
				const char* kind, const char* description);


}	// namespace media
}	// namespace BPrivate


#endif // _MEDIA2_FILTER_CATALOG_H
