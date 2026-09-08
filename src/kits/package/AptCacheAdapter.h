/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef APT_CACHE_ADAPTER_H
#define APT_CACHE_ADAPTER_H


#include <ObjectList.h>
#include <String.h>
#include <SupportDefs.h>

#include <package/PackageDefs.h>


// The adapter boundary: every libapt-pkg call is confined to this
// file; the rest of the kit only sees the plain fields below.
struct apt_raw_package {
			BString				name;
			BString				architecture;
			BString				installedVersion;
			BString				candidateVersion;
			BString				section;
			BString				summary;
			BString				description;
			BString				depends;
			BString				recommends;
			BString				breaks;
			BString				conflicts;
			BString				provides;
			off_t				installedSize;
			off_t				downloadSize;
			v_package_state		state;
			v_package_channel	channel;

								apt_raw_package();
};


class AptCacheAdapter {
public:
								AptCacheAdapter();
								~AptCacheAdapter();

			// Fills everything except the record-parsed fields; those are
			// too costly for all rows, fetch them via GetPackageDetails().
			status_t			GetPackageList(
									BObjectList<apt_raw_package, true>*
										packages);

			// Full record; dependency fields are Debian control-file text,
			// parsed by VDependencyExpression::ParseList(), not here.
			status_t			GetPackageDetails(const char* name,
									apt_raw_package* out);

			// File list via "dpkg -L"; empty, not an error, when uninstalled.
			status_t			GetPackageContents(const char* name,
									BObjectList<BString, true>* paths);

			status_t			GetAptLog(BString* text);

			// Tries the three /usr/share/doc/<name>/ changelog names; when
			// none exist, *out explains and the call still succeeds.
			status_t			GetChangelog(const char* name, BString* out);

			// strcmp-like, via apt's own comparator; cheap per comparison.
			int					CompareVersions(const char* a,
									const char* b);

			// Caller's thread must be the one that makes the next call.
			void				InvalidateCache();

private:
			class CacheHandle;

	static const int32			kMaxChangelogBytes	= 256 * 1024;

			bool				_EnsureCacheOpen();

	static	bool				_ReadFile(const char* path, BString* out);
	static	void				_AppendRecordsReversed(const BString& log,
									BString* out);

			status_t			_RunQuery(const char* const argv[],
									BObjectList<BString, true>* lines);

			bool				_ReadLocalChangelog(const char* name,
									BString* out);
			void				_TruncateChangelog(BString* text);

			// Kept for the adapter's lifetime; does not notice a
			// concurrent "apt update"/"dpkg -i".
			CacheHandle*		fCache;
};


#endif	// APT_CACHE_ADAPTER_H
