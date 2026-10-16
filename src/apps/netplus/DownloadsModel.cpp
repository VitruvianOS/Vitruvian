/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */


#include "DownloadsModel.h"


int32_t
DownloadsModel::AddOrUpdate(uint64_t id, const char* filename,
	const char* url, const char* destination, uint64_t receivedBytes,
	uint64_t totalBytes)
{
	int32_t index = IndexOf(id);
	if (index >= 0) {
		DownloadRecord& item = fItems[index];
		if (filename != NULL)
			item.filename = filename;
		if (url != NULL)
			item.url = url;
		if (destination != NULL)
			item.destination = destination;
		item.receivedBytes = receivedBytes;
		item.totalBytes = totalBytes;
		if (item.state != kDownloadStateActive)
			item.state = kDownloadStateActive;
		return index;
	}

	DownloadRecord item;
	item.id = id;
	item.filename = filename != NULL ? filename : "";
	item.url = url != NULL ? url : "";
	item.destination = destination != NULL ? destination : "";
	item.receivedBytes = receivedBytes;
	item.totalBytes = totalBytes;
	item.state = kDownloadStateActive;
	fItems.push_back(item);
	return (int32_t)fItems.size() - 1;
}


bool
DownloadsModel::SetProgress(uint64_t id, uint64_t receivedBytes,
	uint64_t totalBytes)
{
	int32_t index = IndexOf(id);
	if (index < 0)
		return false;

	fItems[index].receivedBytes = receivedBytes;
	if (totalBytes > 0)
		fItems[index].totalBytes = totalBytes;
	return true;
}


bool
DownloadsModel::MarkState(uint64_t id, DownloadState state)
{
	int32_t index = IndexOf(id);
	if (index < 0)
		return false;

	fItems[index].state = state;
	return true;
}


bool
DownloadsModel::Remove(uint64_t id)
{
	int32_t index = IndexOf(id);
	if (index < 0)
		return false;

	fItems.erase(fItems.begin() + index);
	return true;
}


void
DownloadsModel::ClearFinished()
{
	for (size_t i = fItems.size(); i > 0; i--) {
		DownloadState state = fItems[i - 1].state;
		if (state == kDownloadStateFinished
			|| state == kDownloadStateCancelled) {
			fItems.erase(fItems.begin() + (i - 1));
		}
	}
}


int32_t
DownloadsModel::Count() const
{
	return (int32_t)fItems.size();
}


int32_t
DownloadsModel::IndexOf(uint64_t id) const
{
	for (size_t i = 0; i < fItems.size(); i++) {
		if (fItems[i].id == id)
			return (int32_t)i;
	}
	return -1;
}


int32_t
DownloadsModel::ActiveCount() const
{
	int32_t active = 0;
	for (size_t i = 0; i < fItems.size(); i++) {
		if (fItems[i].state == kDownloadStateActive)
			active++;
	}
	return active;
}


const DownloadRecord&
DownloadsModel::ItemAt(int32_t index) const
{
	static const DownloadRecord kEmpty
		= { 0, std::string(), std::string(), std::string(), 0, 0,
			kDownloadStateActive };
	if (index < 0 || index >= (int32_t)fItems.size())
		return kEmpty;
	return fItems[index];
}
