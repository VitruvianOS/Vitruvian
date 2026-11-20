/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _STORE_CATALOG_H
#define _STORE_CATALOG_H

#include <ObjectList.h>
#include <String.h>


// One catalog entry. The catalog (catalog.json, the same file the
// store site serves) is the explicit allow list: only entries that
// appear here are ever shown, so libraries and -dev packages cannot
// leak in by name guessing. The three kinds are all a user ever sees;
// package internals belong to PackageManager.
struct StoreEntry {
	BString		fId;
	BString		fKind;		// "app", "component" or "data"
	BString		fName;
	BString		fSummary;
	BString		fCategory;	// Internet, Graphics, Media, ...
	BString		fIcon;		// site path or absolute URL, may be empty
			// Ids of components installed before this entry.
			BObjectList<BString, true>	fRequires;
			// Debian package names; one item may map to several.
			BObjectList<BString, true>	fPackages;

								StoreEntry();
								~StoreEntry();

			bool				IsValid() const;
			// Components first (installing an app may pull one),
			// then apps, then data, each by name.
	static	int32				Compare(const StoreEntry* a,
									const StoreEntry* b);
};


class StoreCatalog {
public:
								StoreCatalog();
								~StoreCatalog();

			// Replaces the entries with those of a catalog.json.
			status_t			LoadFrom(const char* path);
			status_t			LoadFromData(const char* json);

			int32				CountEntries() const
									{ return fEntries.CountItems(); }
			StoreEntry*			EntryAt(int32 index) const
									{ return fEntries.ItemAt(index); }
			StoreEntry*			FindById(const char* id) const;

private:
			BObjectList<StoreEntry, true>	fEntries;
};


#endif // _STORE_CATALOG_H
