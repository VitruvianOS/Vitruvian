/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Rewrite legacy x-vnd.Haiku signatures in a user settings tree.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#if defined(__VOS__)

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Node.h>
#include <Path.h>
#include <String.h>

#include <fs_attr.h>

#else

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

#endif


static const char* kMarkerName = "vos-signature-migrated";
static const char* kOldUpper = "x-vnd.Haiku-";
static const char* kOldLower = "x-vnd.haiku-";
static const char* kNew = "x-vnd.vos-";

// Attribute names that hold app signatures in mime_db and special folders.
static const char* kAttrSysName = "SYS:NAME";
static const char* kAttrPrefApp = "META:PREF_APP";
static const char* kAttrSniffRule = "META:SNIFF_RULE";
static const char* kAttrFileType = "META:TYPE";
static const char* kAttrFileTypes = "META:FILE_TYPES";

// Host xattr type prefixes (little-endian fourcc), matching image postinst.
static const unsigned char kTypeString[4] = { 0x52, 0x54, 0x53, 0x43 };	// 'CSTR'
static const unsigned char kTypeMimeString[4] = { 0x53, 0x4d, 0x49, 0x4d };	// 'MIMS'
static const unsigned char kTypeSig[4] = { 0x47, 0x49, 0x53, 0x4d };	// 'MSIG'


struct RewriteStats {
	int filesScanned;
	int filesRenamed;
	int attrsRewritten;
	int textRewritten;
};


static bool
rewrite_signature(const char* in, char* out, size_t outSize)
{
	if (in == NULL || out == NULL || outSize == 0)
		return false;

	size_t inLen = strlen(in);
	const char* old = NULL;
	size_t oldLen = 0;
	if (strstr(in, kOldUpper) != NULL) {
		old = kOldUpper;
		oldLen = strlen(kOldUpper);
	} else if (strstr(in, kOldLower) != NULL) {
		old = kOldLower;
		oldLen = strlen(kOldLower);
	} else {
		snprintf(out, outSize, "%s", in);
		return false;
	}

	size_t newLen = strlen(kNew);
	size_t needed = 0;
	for (size_t i = 0; i < inLen;) {
		if (i + oldLen <= inLen && strncmp(in + i, old, oldLen) == 0) {
			needed += newLen;
			i += oldLen;
		} else {
			needed += 1;
			i += 1;
		}
	}
	needed += 1;
	if (needed > outSize)
		return false;

	size_t o = 0;
	for (size_t i = 0; i < inLen;) {
		if (i + oldLen <= inLen && strncmp(in + i, old, oldLen) == 0) {
			memcpy(out + o, kNew, newLen);
			o += newLen;
			i += oldLen;
		} else {
			out[o++] = in[i++];
		}
	}
	out[o] = '\0';
	return true;
}


static bool
rewrite_file_name(const char* name, char* out, size_t outSize)
{
	return rewrite_signature(name, out, outSize);
}


#if defined(__VOS__)

static void
migrate_node_attrs(BNode& node, RewriteStats& stats)
{
	const char* stringAttrs[] = {
		kAttrSysName, kAttrPrefApp, kAttrSniffRule, kAttrFileType, NULL
	};

	for (int32 i = 0; stringAttrs[i] != NULL; i++) {
		BString value;
		if (node.ReadAttrString(stringAttrs[i], &value) != B_OK)
			continue;
		char rewritten[B_MIME_TYPE_LENGTH * 2];
		if (!rewrite_signature(value.String(), rewritten, sizeof(rewritten)))
			continue;
		type_code type = (strcmp(stringAttrs[i], kAttrPrefApp) == 0)
			? (type_code)'MSIG' : (type_code)'CSTR';
		node.WriteAttr(stringAttrs[i], type, 0, rewritten,
			strlen(rewritten) + 1);
		stats.attrsRewritten++;
	}

	// Supporting-apps and sniffer payloads live in META:FILE_TYPES.
	attr_info info;
	if (node.GetAttrInfo(kAttrFileTypes, &info) == B_OK
			&& info.type == B_MESSAGE_TYPE) {
		void* buffer = malloc(info.size);
		if (buffer != NULL) {
			ssize_t read = node.ReadAttr(kAttrFileTypes,
				B_MESSAGE_TYPE, 0, buffer, info.size);
			if (read == (ssize_t)info.size) {
				BMessage message;
				if (message.Unflatten((const char*)buffer) == B_OK) {
					bool changed = false;
					const char* app = NULL;
					for (int32 index = 0;
							message.FindString("applications", index, &app)
								== B_OK; index++) {
						char rewritten[B_MIME_TYPE_LENGTH * 2];
						if (rewrite_signature(app, rewritten,
								sizeof(rewritten))) {
							message.ReplaceString("applications", index,
								rewritten);
							changed = true;
						}
					}
					if (changed) {
						BMallocIO flat;
						ssize_t bytes = 0;
						message.Flatten(&flat, &bytes);
						node.WriteAttr(kAttrFileTypes, B_MESSAGE_TYPE, 0,
							flat.Buffer(), flat.BufferLength());
						stats.attrsRewritten++;
					}
				}
			}
			free(buffer);
		}
	}
}


static void
migrate_directory_tree(BDirectory& dir, RewriteStats& stats)
{
	BEntry entry;
	for (int32 i = 0; dir.GetNextEntry(&entry, true) == B_OK; i++) {
		char name[B_FILE_NAME_LENGTH];
		if (entry.GetName(name) != B_OK)
			continue;

		char newName[B_FILE_NAME_LENGTH];
		bool renameEntry = rewrite_file_name(name, newName, sizeof(newName));

		if (entry.IsDirectory()) {
			BDirectory sub(&entry);
			if (sub.InitCheck() == B_OK) {
				BNode node(&entry);
				if (node.InitCheck() == B_OK)
					migrate_node_attrs(node, stats);
				migrate_directory_tree(sub, stats);
			}
		} else if (entry.IsFile()) {
			BNode node(&entry);
			if (node.InitCheck() == B_OK)
				migrate_node_attrs(node, stats);
		}

		if (renameEntry) {
			entry.Rename(newName);
			stats.filesRenamed++;
		}
		stats.filesScanned++;
	}
}


static void
rewrite_text_file(const char* path, RewriteStats& stats)
{
	BFile file(path, B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	off_t size = 0;
	if (file.GetSize(&size) != B_OK || size <= 0 || size > 4 * 1024 * 1024)
		return;

	char* data = (char*)malloc(size + 1);
	if (data == NULL)
		return;
	ssize_t read = file.Read(data, size);
	if (read != size) {
		free(data);
		return;
	}
	data[size] = '\0';

	char* rewritten = (char*)malloc(size + 1);
	if (rewritten == NULL) {
		free(data);
		return;
	}

	bool changed = false;
	const char* src = data;
	char* dst = rewritten;
	char* end = rewritten + size;
	while (*src != '\0' && dst < end) {
		const char* hit = NULL;
		size_t oldLen = 0;
		if (strstr(src, kOldUpper) != NULL) {
			hit = strstr(src, kOldUpper);
			oldLen = strlen(kOldUpper);
		} else if (strstr(src, kOldLower) != NULL) {
			hit = strstr(src, kOldLower);
			oldLen = strlen(kOldLower);
		}
		if (hit == NULL) {
			size_t left = strlen(src);
			if (dst + left >= end)
				break;
			memcpy(dst, src, left);
			dst += left;
			break;
		}
		size_t prefix = hit - src;
		if (dst + prefix + strlen(kNew) >= end)
			break;
		memcpy(dst, src, prefix);
		dst += prefix;
		memcpy(dst, kNew, strlen(kNew));
		dst += strlen(kNew);
		src = hit + oldLen;
		changed = true;
	}
	*dst = '\0';

	if (changed) {
		BFile out(path, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
		if (out.InitCheck() == B_OK) {
			out.Write(rewritten, strlen(rewritten));
			stats.textRewritten++;
		}
	}

	free(rewritten);
	free(data);
}


static void
migrate_special_folders(BPath& home, RewriteStats& stats)
{
	const char* rels[] = {
		"Desktop", "config", "mail", "mail/draft", "mail/in", "mail/out",
		"mail/queries", "mail/sent", "mail/spam",
		"config/settings/Tracker/Tracker New Templates",
		"config/settings/deskbar/menu/Applications",
		"config/settings/deskbar/menu/Demos",
		"config/settings/deskbar/menu/Preferences",
		"config/settings/deskbar/menu/Desktop applets",
		NULL
	};

	for (int32 i = 0; rels[i] != NULL; i++) {
		BPath path(home.Path(), rels[i]);
		BEntry entry(path.Path());
		if (!entry.Exists())
			continue;
		BNode node(&entry);
		if (node.InitCheck() != B_OK)
			continue;
		migrate_node_attrs(node, stats);
	}
}


static void
migrate_settings_text(BPath& settings, RewriteStats& stats)
{
	const char* files[] = {
		"RosterSettings",
		"Tracker/TrackerSettings",
		"Tracker/Tracker Settings",
		"Tracker/Backgrounds_settings",
		"Tracker/Shortcuts_window_settings",
		NULL
	};

	for (int32 i = 0; files[i] != NULL; i++) {
		BPath path(settings.Path(), files[i]);
		rewrite_text_file(path.Path(), stats);
	}
}


static status_t
run_migration(const char* rootOverride)
{
	BPath settings;
	if (rootOverride != NULL) {
		if (settings.SetTo(rootOverride) != B_OK)
			return B_BAD_VALUE;
	} else if (find_directory(B_USER_SETTINGS_DIRECTORY, &settings, true)
			!= B_OK) {
		return B_ENTRY_NOT_FOUND;
	}

	RewriteStats stats;
	memset(&stats, 0, sizeof(stats));

	BDirectory settingsDir(settings.Path());
	if (settingsDir.InitCheck() == B_OK)
		migrate_directory_tree(settingsDir, stats);

	BPath home;
	if (rootOverride != NULL) {
		// Fixture layout: settings is <home>/config/settings; special
		// folders live under <home>.
		BPath parent(settings.Path());
		parent.GetParent(&parent);
		parent.GetParent(&parent);
		home = parent;
	} else {
		find_directory(B_USER_DIRECTORY, &home);
	}
	if (home.InitCheck() == B_OK)
		migrate_special_folders(home, stats);

	migrate_settings_text(settings, stats);

	BString marker(settings.Path());
	marker << '/' << kMarkerName;
	if (rootOverride != NULL) {
		// Keep the marker outside the settings dir in fixture mode.
		BPath parent(settings.Path());
		parent.GetParent(&parent);
		marker.SetTo(parent.Path());
		marker << '/' << kMarkerName;
	}
	BFile markerFile(marker.String(),
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (markerFile.InitCheck() == B_OK)
		markerFile.Write("1\n", 2);

	fprintf(stderr,
		"vos-signature-migrate: scanned %d, renamed %d, attrs %d, text %d\n",
		stats.filesScanned, stats.filesRenamed, stats.attrsRewritten,
		stats.textRewritten);
	return B_OK;
}


int
main(int argc, char** argv)
{
	const char* root = NULL;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--root") == 0 && i + 1 < argc)
			root = argv[++i];
		else if (strcmp(argv[i], "--help") == 0) {
			printf("usage: vos-signature-migrate [--root DIR]\n");
			return 0;
		}
	}
	return run_migration(root) == B_OK ? 0 : 1;
}

#else	// host test build

static void
host_xattr_name(const char* attr, char* out, size_t outSize)
{
	snprintf(out, outSize, "user.beos.%s", attr);
}


static bool
host_read_xattr(const char* path, const char* attr, char* value,
	size_t valueSize, const unsigned char** typeOut)
{
	char name[256];
	host_xattr_name(attr, name, sizeof(name));
	unsigned char buf[4096];
	ssize_t n = getxattr(path, name, buf, sizeof(buf));
	if (n < 4)
		return false;
	if (typeOut != NULL)
		*typeOut = buf;
	size_t len = (size_t)n - 4;
	if (len >= valueSize)
		len = valueSize - 1;
	memcpy(value, buf + 4, len);
	value[len] = '\0';
	return true;
}


static void
host_write_xattr(const char* path, const char* attr, const unsigned char* type,
	const char* value)
{
	char name[256];
	host_xattr_name(attr, name, sizeof(name));
	size_t vlen = strlen(value);
	unsigned char* buf = (unsigned char*)malloc(4 + vlen + 1);
	if (buf == NULL)
		return;
	memcpy(buf, type, 4);
	memcpy(buf + 4, value, vlen + 1);
	setxattr(path, name, buf, 4 + vlen + 1, 0);
	free(buf);
}


static void
host_migrate_file(const char* path, RewriteStats& stats)
{
	stats.filesScanned++;

	struct stat st;
	if (stat(path, &st) != 0)
		return;

	if (S_ISDIR(st.st_mode)) {
		DIR* dir = opendir(path);
		if (dir == NULL)
			return;
		struct dirent* de;
		while ((de = readdir(dir)) != NULL) {
			if (strcmp(de->d_name, ".") == 0
					|| strcmp(de->d_name, "..") == 0)
				continue;
			char child[4096];
			snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
			host_migrate_file(child, stats);
		}
		closedir(dir);
	}

	const char* stringAttrs[] = {
		kAttrSysName, kAttrPrefApp, kAttrSniffRule, kAttrFileType, NULL
	};
	for (int i = 0; stringAttrs[i] != NULL; i++) {
		char value[2048];
		const unsigned char* type = NULL;
		if (!host_read_xattr(path, stringAttrs[i], value, sizeof(value),
				&type))
			continue;
		char rewritten[4096];
		if (!rewrite_signature(value, rewritten, sizeof(rewritten)))
			continue;
		const unsigned char* useType = kTypeString;
		if (strcmp(stringAttrs[i], kAttrPrefApp) == 0)
			useType = kTypeSig;
		else if (strcmp(stringAttrs[i], kAttrFileType) == 0)
			useType = kTypeMimeString;
		host_write_xattr(path, stringAttrs[i], useType, rewritten);
		stats.attrsRewritten++;
	}

	if (S_ISREG(st.st_mode)) {
		char workPath[4096];
		snprintf(workPath, sizeof(workPath), "%s", path);
		char rewrittenName[4096];
		const char* base = strrchr(workPath, '/');
		base = base != NULL ? base + 1 : workPath;
		if (rewrite_file_name(base, rewrittenName, sizeof(rewrittenName))
				&& strcmp(base, rewrittenName) != 0) {
			char newPath[4096];
			if (base == workPath)
				snprintf(newPath, sizeof(newPath), "%s", rewrittenName);
			else {
				char dirPart[4096];
				size_t dlen = (size_t)(base - workPath);
				memcpy(dirPart, workPath, dlen);
				dirPart[dlen] = '\0';
				snprintf(newPath, sizeof(newPath), "%s%s", dirPart,
					rewrittenName);
			}
			if (rename(workPath, newPath) == 0) {
				stats.filesRenamed++;
				snprintf(workPath, sizeof(workPath), "%s", newPath);
			}
		}

		int fd = open(workPath, O_RDONLY);
		if (fd < 0)
			return;
		off_t fsize = st.st_size;
		char* data = (char*)malloc(fsize + 1);
		if (data == NULL) {
			close(fd);
			return;
		}
		ssize_t n = read(fd, data, fsize);
		close(fd);
		if (n != fsize) {
			free(data);
			return;
		}
		data[n] = '\0';

		bool changed = false;
		char* out = (char*)malloc(n + 1);
		if (out == NULL) {
			free(data);
			return;
		}
		char* dst = out;
		char* src = data;
		while (*src != '\0') {
			char* hit = strstr(src, kOldUpper);
			size_t oldLen = strlen(kOldUpper);
			if (hit == NULL) {
				hit = strstr(src, kOldLower);
				oldLen = strlen(kOldLower);
			}
			if (hit == NULL) {
				size_t left = strlen(src);
				memcpy(dst, src, left + 1);
				dst += left;
				break;
			}
			size_t prefix = (size_t)(hit - src);
			memcpy(dst, src, prefix);
			dst += prefix;
			memcpy(dst, kNew, strlen(kNew));
			dst += strlen(kNew);
			src = hit + oldLen;
			changed = true;
		}
		*dst = '\0';
		if (changed) {
			fd = open(workPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
			if (fd >= 0) {
				write(fd, out, strlen(out));
				close(fd);
				stats.textRewritten++;
			}
		}
		free(out);
		free(data);
	}
}


int
main(int argc, char** argv)
{
	const char* root = NULL;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--root") == 0 && i + 1 < argc)
			root = argv[++i];
	}
	if (root == NULL) {
		fprintf(stderr, "usage: vos-signature-migrate --root DIR\n");
		return 2;
	}

	struct stat st;
	if (stat(root, &st) != 0 || !S_ISDIR(st.st_mode)) {
		fprintf(stderr, "vos-signature-migrate: bad root %s\n", root);
		return 2;
	}

	RewriteStats stats;
	memset(&stats, 0, sizeof(stats));
	host_migrate_file(root, stats);

	// Special folders sit beside settings in the fixture layout.
	char special[4096];
	const char* rels[] = {
		"Desktop", "config", "mail", NULL
	};
	char parent[4096];
	snprintf(parent, sizeof(parent), "%s", root);
	// settings is <home>/config/settings; special folders live under <home>.
	char* slash = strrchr(parent, '/');
	if (slash != NULL)
		*slash = '\0';
	slash = strrchr(parent, '/');
	if (slash != NULL)
		*slash = '\0';
	for (int i = 0; rels[i] != NULL; i++) {
		snprintf(special, sizeof(special), "%s/%s", parent, rels[i]);
		struct stat sst;
		if (stat(special, &sst) == 0)
			host_migrate_file(special, stats);
	}

	// Marker lives in the settings dir, matching the janus_session check.
	char marker[4096];
	snprintf(marker, sizeof(marker), "%s/%s", root, kMarkerName);
	int fd = open(marker, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0) {
		write(fd, "1\n", 2);
		close(fd);
	}

	fprintf(stderr,
		"vos-signature-migrate: scanned %d, renamed %d, attrs %d, text %d\n",
		stats.filesScanned, stats.filesRenamed, stats.attrsRewritten,
		stats.textRewritten);
	return 0;
}

#endif
