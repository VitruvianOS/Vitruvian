/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 *
 * XDG Trash spec .trashinfo helper.
 * https://specifications.freedesktop.org/trash-spec/trashspec-1.0.html
 */

#ifndef _TRACKER_TRASH_INFO_H
#define _TRACKER_TRASH_INFO_H

#include <Directory.h>
#include <String.h>
#include <SupportDefs.h>

#include <time.h>


namespace BPrivate {


class TrashInfo {
public:
	// Writes <trashFilesDir>/../info/<chosenName>.trashinfo describing a
	// file about to be moved into <trashFilesDir>/<chosenName>.
	//
	// preferredName is the original basename; on collision the writer
	// appends a numeric suffix and retries via O_EXCL until it wins.
	// The chosen on-disk name is returned in chosenNameOut so the caller
	// can rename the source into trashFilesDir under the same name.
	//
	// originalAbsPath must be an absolute filesystem path.
	static status_t	Write(const BDirectory& trashFilesDir,
						const char* preferredName,
						const char* originalAbsPath,
						BString* chosenNameOut);

	// Reads the matching .trashinfo for <name> in <trashFilesDir>.
	// Returns B_ENTRY_NOT_FOUND if no info file is present.
	static status_t	Read(const BDirectory& trashFilesDir,
						const char* nameInTrash,
						BString* originalAbsPathOut,
						time_t* deletionDateOut /* may be NULL */);

	// Deletes <trashFilesDir>/../info/<name>.trashinfo. Returns B_OK
	// even if the file did not exist.
	static status_t	Remove(const BDirectory& trashFilesDir,
						const char* nameInTrash);
};


} // namespace BPrivate

#endif // _TRACKER_TRASH_INFO_H
