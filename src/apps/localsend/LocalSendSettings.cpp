/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "LocalSendSettings.h"

#include <File.h>
#include <Path.h>
#include <String.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <FindDirectory.h>


static const char* kSettingsFileName = "LocalSend/settings";

static const uint32 kMsgVersion = 1;
static const char* kKeyVersion = "version";
static const char* kKeyAlias = "alias";
static const char* kKeyDestination = "destination";
static const char* kKeyRequirePin = "requirePin";
static const char* kKeyPin = "pin";
static const char* kKeyFingerprint = "fingerprint";
static const char* kKeyTrusted = "trusted";
static const char* kKeyHistory = "history";
static const char* kKeyHistoryPeer = "peer";
static const char* kKeyHistoryFiles = "files";
static const char* kKeyHistoryBytes = "bytes";
static const char* kKeyHistoryOutgoing = "outgoing";
static const char* kKeyHistorySuccess = "success";
static const char* kKeyHistoryWhen = "when";


LocalSendSettings::LocalSendSettings()
	:
	fRequirePin(false)
{
	char hostname[256];
	if (gethostname(hostname, sizeof(hostname)) == 0)
		fAlias = hostname;
	else
		fAlias = "VOS Device";

	// Default destination: ~/Downloads/LocalSend
	BPath path;
	if (find_directory(B_USER_DIRECTORY, &path) == B_OK) {
		path.Append("Downloads");
		path.Append("LocalSend");
		fDestination = path.Path();
	} else
		fDestination = "/boot/home/Downloads/LocalSend";
}


bool
LocalSendSettings::Load()
{
	BPath settingsPath;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) != B_OK)
		return false;
	settingsPath.Append(kSettingsFileName);

	BFile file(settingsPath.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return false;

	BMessage archive;
	if (archive.Unflatten(&file) != B_OK)
		return false;

	uint32 version = 0;
	if (archive.FindUInt32(kKeyVersion, &version) != B_OK
		|| version != kMsgVersion)
		return false;

	const char* alias;
	if (archive.FindString(kKeyAlias, &alias) == B_OK)
		fAlias = alias;
	const char* destination;
	if (archive.FindString(kKeyDestination, &destination) == B_OK)
		fDestination = destination;
	bool requirePin;
	if (archive.FindBool(kKeyRequirePin, &requirePin) == B_OK)
		fRequirePin = requirePin;
	const char* pin;
	if (archive.FindString(kKeyPin, &pin) == B_OK)
		fPin = pin;
	const char* fingerprint;
	if (archive.FindString(kKeyFingerprint, &fingerprint) == B_OK)
		fFingerprint = fingerprint;

	fTrusted.clear();
	const char* trusted;
	for (int32 i = 0;
		archive.FindString(kKeyTrusted, i, &trusted) == B_OK; i++)
		fTrusted.push_back(BString(trusted));

	fHistory.clear();
	BMessage history;
	if (archive.FindMessage(kKeyHistory, &history) == B_OK) {
		int32 count = 0;
		history.FindInt32("count", &count);
		for (int32 i = 0; i < count; i++) {
			LsHistoryEntry entry;
			const char* peer;
			if (history.FindString(kKeyHistoryPeer, i, &peer) == B_OK)
				entry.peerAlias = peer;
			const char* files;
			if (history.FindString(kKeyHistoryFiles, i, &files) == B_OK)
				entry.fileNames = files;
			history.FindInt64(kKeyHistoryBytes, i, &entry.bytes);
			history.FindBool(kKeyHistoryOutgoing, i, &entry.outgoing);
			history.FindBool(kKeyHistorySuccess, i, &entry.success);
			history.FindInt64(kKeyHistoryWhen, i, &entry.when);
			fHistory.push_back(entry);
		}
	}
	return true;
}


bool
LocalSendSettings::Save() const
{
	BPath settingsPath;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) != B_OK)
		return false;
	settingsPath.Append(kSettingsFileName);

	BMessage archive(kMsgVersion);
	archive.AddUInt32(kKeyVersion, kMsgVersion);
	archive.AddString(kKeyAlias, fAlias);
	archive.AddString(kKeyDestination, fDestination);
	archive.AddBool(kKeyRequirePin, fRequirePin);
	archive.AddString(kKeyPin, fPin);
	if (!fFingerprint.IsEmpty())
		archive.AddString(kKeyFingerprint, fFingerprint);

	for (size_t i = 0; i < fTrusted.size(); i++)
		archive.AddString(kKeyTrusted, fTrusted[i]);

	BMessage history;
	history.AddInt32("count", static_cast<int32>(fHistory.size()));
	for (size_t i = 0; i < fHistory.size(); i++) {
		const LsHistoryEntry& entry = fHistory[i];
		history.AddString(kKeyHistoryPeer, entry.peerAlias);
		history.AddString(kKeyHistoryFiles, entry.fileNames);
		history.AddInt64(kKeyHistoryBytes, entry.bytes);
		history.AddBool(kKeyHistoryOutgoing, entry.outgoing);
		history.AddBool(kKeyHistorySuccess, entry.success);
		history.AddInt64(kKeyHistoryWhen, entry.when);
	}
	archive.AddMessage(kKeyHistory, &history);

	BFile file(settingsPath.Path(), B_WRITE_ONLY | B_CREATE_FILE
		| B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return false;
	return archive.Flatten(&file) == B_OK;
}


bool
LocalSendSettings::IsTrusted(const BString& fingerprint) const
{
	for (size_t i = 0; i < fTrusted.size(); i++) {
		if (fTrusted[i].ICompare(fingerprint) == 0)
			return true;
	}
	return false;
}


void
LocalSendSettings::AddTrusted(const BString& fingerprint)
{
	if (fingerprint.IsEmpty() || IsTrusted(fingerprint))
		return;
	fTrusted.push_back(fingerprint);
}


void
LocalSendSettings::AddHistory(const LsHistoryEntry& entry)
{
	fHistory.push_back(entry);
	// Keep the list bounded.
	if (fHistory.size() > 200)
		fHistory.erase(fHistory.begin(),
			fHistory.begin() + (fHistory.size() - 200));
}
