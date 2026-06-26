/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "TrashInfo.h"

#include <Entry.h>
#include <File.h>
#include <Path.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>


namespace BPrivate {


static bool
is_unreserved(unsigned char c)
{
	// RFC 3986 unreserved: ALPHA / DIGIT / "-" / "." / "_" / "~"
	return isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/';
}


static void
url_encode(const char* in, BString& out)
{
	out.Truncate(0);
	for (const unsigned char* p = (const unsigned char*)in; *p; p++) {
		if (is_unreserved(*p)) {
			out.Append((char)*p, 1);
		} else {
			char buf[4];
			snprintf(buf, sizeof(buf), "%%%02X", *p);
			out.Append(buf);
		}
	}
}


static bool
url_decode(const char* in, BString& out)
{
	out.Truncate(0);
	for (const char* p = in; *p; p++) {
		if (*p == '%') {
			if (!isxdigit((unsigned char)p[1]) || !isxdigit((unsigned char)p[2]))
				return false;
			char hex[3] = { p[1], p[2], '\0' };
			out.Append((char)strtol(hex, NULL, 16), 1);
			p += 2;
		} else {
			out.Append(*p, 1);
		}
	}
	return true;
}


static status_t
info_dir_path(const BDirectory& trashFilesDir, BPath* infoDir)
{
	BEntry entry;
	status_t r = trashFilesDir.GetEntry(&entry);
	if (r != B_OK) return r;
	BPath filesPath;
	r = entry.GetPath(&filesPath);
	if (r != B_OK) return r;
	BPath trashRoot;
	r = filesPath.GetParent(&trashRoot);
	if (r != B_OK) return r;
	return infoDir->SetTo(trashRoot.Path(), "info");
}


static status_t
build_info_body(const char* originalAbsPath, BString& body)
{
	BString encoded;
	url_encode(originalAbsPath, encoded);

	time_t now = time(NULL);
	struct tm tmv;
	localtime_r(&now, &tmv);
	char date[32];
	strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &tmv);

	body = "[Trash Info]\n";
	body << "Path=" << encoded << "\n";
	body << "DeletionDate=" << date << "\n";
	return B_OK;
}


status_t
TrashInfo::Write(const BDirectory& trashFilesDir, const char* preferredName,
	const char* originalAbsPath, BString* chosenNameOut)
{
	if (preferredName == NULL || originalAbsPath == NULL
		|| chosenNameOut == NULL) {
		return B_BAD_VALUE;
	}

	BPath infoDir;
	status_t r = info_dir_path(trashFilesDir, &infoDir);
	if (r != B_OK) return r;

	if (mkdir(infoDir.Path(), 0700) != 0 && errno != EEXIST)
		return -errno;

	BString body;
	build_info_body(originalAbsPath, body);

	BString candidate(preferredName);
	for (int suffix = 0; suffix < 10000; suffix++) {
		if (suffix > 0) {
			candidate = preferredName;
			candidate << "." << suffix;
		}

		BString infoPath(infoDir.Path());
		infoPath << "/" << candidate << ".trashinfo";

		int fd = open(infoPath.String(),
			O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
		if (fd < 0) {
			if (errno == EEXIST)
				continue;
			return -errno;
		}

		ssize_t want = body.Length();
		ssize_t got = write(fd, body.String(), want);
		close(fd);
		if (got != want) {
			unlink(infoPath.String());
			return B_IO_ERROR;
		}

		*chosenNameOut = candidate;
		return B_OK;
	}
	return B_FILE_EXISTS;
}


status_t
TrashInfo::Read(const BDirectory& trashFilesDir, const char* nameInTrash,
	BString* originalAbsPathOut, time_t* deletionDateOut)
{
	if (nameInTrash == NULL || originalAbsPathOut == NULL)
		return B_BAD_VALUE;

	BPath infoDir;
	status_t r = info_dir_path(trashFilesDir, &infoDir);
	if (r != B_OK) return r;

	BString path(infoDir.Path());
	path << "/" << nameInTrash << ".trashinfo";

	FILE* f = fopen(path.String(), "r");
	if (f == NULL)
		return errno == ENOENT ? B_ENTRY_NOT_FOUND
			: -errno;

	bool foundPath = false;
	char line[4096];
	while (fgets(line, sizeof(line), f) != NULL) {
		size_t len = strlen(line);
		while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
			line[--len] = '\0';

		if (strncmp(line, "Path=", 5) == 0) {
			url_decode(line + 5, *originalAbsPathOut);
			foundPath = true;
		} else if (deletionDateOut != NULL
			&& strncmp(line, "DeletionDate=", 13) == 0) {
			struct tm tmv;
			memset(&tmv, 0, sizeof(tmv));
			if (strptime(line + 13, "%Y-%m-%dT%H:%M:%S", &tmv) != NULL)
				*deletionDateOut = mktime(&tmv);
		}
	}
	fclose(f);
	return foundPath ? B_OK : B_BAD_DATA;
}


status_t
TrashInfo::Remove(const BDirectory& trashFilesDir, const char* nameInTrash)
{
	if (nameInTrash == NULL)
		return B_BAD_VALUE;

	BPath infoDir;
	status_t r = info_dir_path(trashFilesDir, &infoDir);
	if (r != B_OK) return r;

	BString path(infoDir.Path());
	path << "/" << nameInTrash << ".trashinfo";

	if (unlink(path.String()) != 0 && errno != ENOENT)
		return -errno;
	return B_OK;
}


} // namespace BPrivate
