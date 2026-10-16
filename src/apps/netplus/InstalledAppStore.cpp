/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */


#include "InstalledAppStore.h"
#include "WebAppManifest.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


namespace {


bool
MakeDirs(const std::string& path)
{
	std::string built;
	size_t start = 0;
	while (start < path.size()) {
		size_t slash = path.find('/', start);
		if (slash == std::string::npos)
			slash = path.size();
		built = path.substr(0, slash);
		if (!built.empty() && built != "/")
			mkdir(built.c_str(), 0755);
		start = slash + 1;
	}
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}


void
AppendJsonString(std::string* into, const std::string& value)
{
	into->push_back('"');
	for (size_t i = 0; i < value.size(); i++) {
		char c = value[i];
		switch (c) {
			case '"': into->append("\\\""); break;
			case '\\': into->append("\\\\"); break;
			case '\n': into->append("\\n"); break;
			case '\r': into->append("\\r"); break;
			case '\t': into->append("\\t"); break;
			default:
				if ((unsigned char)c < 0x20) {
					char buffer[8];
					snprintf(buffer, sizeof(buffer), "\\u%04x",
						(unsigned char)c);
					into->append(buffer);
				} else
					into->push_back(c);
				break;
		}
	}
	into->push_back('"');
}


bool
ReadWholeFile(const std::string& path, std::string* into)
{
	FILE* file = fopen(path.c_str(), "rb");
	if (file == NULL)
		return false;

	into->clear();
	char buffer[4096];
	size_t got = 0;
	while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0)
		into->append(buffer, got);
	fclose(file);
	return true;
}


} // namespace


InstalledAppStore::InstalledAppStore(const char* rootPath)
	:
	fRoot(rootPath != NULL ? rootPath : "")
{
	if (!fRoot.empty())
		_Load();
}


std::string
InstalledAppStore::IconsDirectory() const
{
	return fRoot + "/icons";
}


void
InstalledAppStore::_Load()
{
	fApps.clear();

	DIR* dir = opendir(fRoot.c_str());
	if (dir == NULL)
		return;

	struct dirent* entry = NULL;
	while ((entry = readdir(dir)) != NULL) {
		size_t nameLength = strlen(entry->d_name);
		if (nameLength <= 5
			|| strcmp(entry->d_name + nameLength - 5, ".json") != 0)
			continue;

		std::string text;
		if (!ReadWholeFile(fRoot + "/" + entry->d_name, &text))
			continue;

		InstalledApp app;
		if (!ExtractJsonString(text.c_str(), text.size(), "id", &app.id)
			|| !ExtractJsonString(text.c_str(), text.size(), "name",
				&app.name)
			|| !ExtractJsonString(text.c_str(), text.size(), "start_url",
				&app.startUrl))
			continue;
		ExtractJsonString(text.c_str(), text.size(), "display",
			&app.display);
		ExtractJsonString(text.c_str(), text.size(), "manifest_url",
			&app.manifestUrl);
		ExtractJsonString(text.c_str(), text.size(), "icon",
			&app.iconPath);
		fApps.push_back(app);
	}
	closedir(dir);
}


bool
InstalledAppStore::_WriteRecord(const InstalledApp& app) const
{
	if (fRoot.empty())
		return false;
	if (!MakeDirs(fRoot))
		return false;

	std::string text("{\n\t\"id\": ");
	AppendJsonString(&text, app.id);
	text.append(",\n\t\"name\": ");
	AppendJsonString(&text, app.name);
	text.append(",\n\t\"start_url\": ");
	AppendJsonString(&text, app.startUrl);
	text.append(",\n\t\"display\": ");
	AppendJsonString(&text, app.display);
	text.append(",\n\t\"manifest_url\": ");
	AppendJsonString(&text, app.manifestUrl);
	text.append(",\n\t\"icon\": ");
	AppendJsonString(&text, app.iconPath);
	text.append("\n}\n");

	std::string path = fRoot + "/" + app.id + ".json";
	FILE* file = fopen(path.c_str(), "wb");
	if (file == NULL)
		return false;
	size_t length = text.size();
	bool ok = fwrite(text.data(), 1, length, file) == length;
	fclose(file);
	return ok;
}


bool
InstalledAppStore::_RemoveRecord(const char* id) const
{
	if (id == NULL)
		return false;
	std::string path = fRoot + "/" + id + ".json";
	return unlink(path.c_str()) == 0;
}


bool
InstalledAppStore::Add(const InstalledApp& app)
{
	InstalledApp record = app;
	if (record.id.empty())
		record.id = IDFromName(record.name.c_str());
	if (record.id.empty())
		record.id = "app";
	if (record.name.empty() || record.startUrl.empty())
		return false;

	if (!_WriteRecord(record))
		return false;

	for (size_t i = 0; i < fApps.size(); i++) {
		if (fApps[i].id == record.id) {
			fApps[i] = record;
			return true;
		}
	}
	fApps.push_back(record);
	return true;
}


bool
InstalledAppStore::Remove(const char* id)
{
	int32_t index = IndexOf(id);
	if (index < 0)
		return false;
	if (!_RemoveRecord(fApps[index].id.c_str()))
		return false;
	fApps.erase(fApps.begin() + index);
	return true;
}


const InstalledApp&
InstalledAppStore::At(int32_t index) const
{
	static const InstalledApp sEmpty;
	if (index < 0 || index >= (int32_t)fApps.size())
		return sEmpty;
	return fApps[index];
}


int32_t
InstalledAppStore::IndexOf(const char* id) const
{
	if (id == NULL)
		return -1;
	for (size_t i = 0; i < fApps.size(); i++) {
		if (fApps[i].id == id)
			return (int32_t)i;
	}
	return -1;
}


int32_t
InstalledAppStore::IndexOfEntryName(const char* name) const
{
	if (name == NULL || name[0] == '\0')
		return -1;
	for (size_t i = 0; i < fApps.size(); i++) {
		if (fApps[i].name == name || fApps[i].id == name)
			return (int32_t)i;
	}

	std::string id = IDFromName(name);
	if (id.empty())
		return -1;
	return IndexOf(id.c_str());
}


std::string
InstalledAppStore::IDFromName(const char* name)
{
	std::string id;
	bool pendingDash = false;
	for (size_t i = 0; name != NULL && name[i] != '\0'; i++) {
		unsigned char c = (unsigned char)name[i];
		if (isalnum(c)) {
			if (pendingDash && !id.empty())
				id.push_back('-');
			pendingDash = false;
			id.push_back((char)tolower(c));
		} else
			pendingDash = true;
	}
	return id;
}
