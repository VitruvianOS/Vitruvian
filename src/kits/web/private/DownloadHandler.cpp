/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "download_handler.h"

#include "DownloadSanitizer.h"
#include "logging.h"

#include <cstdlib>
#include <filesystem>
#include <wpe/webkit.h>


DownloadHandler::DownloadHandler()
	:
	fNextId(1)
{
	const char* home = getenv("HOME");
	fDownloadDir = home ? std::string(home) + "/Downloads" : "/tmp";
	log_info("DownloadHandler: download dir = %s", fDownloadDir.c_str());
}


DownloadHandler::~DownloadHandler()
{
}


void
DownloadHandler::Attach(WebKitNetworkSession* session)
{
	g_signal_connect(session, "download-started",
		G_CALLBACK(_OnDownloadStarted), this);
	log_info("DownloadHandler: attached to network session");
}


std::vector<DownloadEntry>
DownloadHandler::ActiveDownloads() const
{
	std::lock_guard<std::mutex> lock(fMutex);
	std::vector<DownloadEntry> result;
	result.reserve(fDownloads.size());
	for (const auto& kv : fDownloads)
		result.push_back(kv.second);
	return result;
}


void
DownloadHandler::_OnDownloadStarted(WebKitNetworkSession*, WebKitDownload* dl,
	gpointer data)
{
	auto* self = static_cast<DownloadHandler*>(data);
	g_signal_connect(dl, "decide-destination",
		G_CALLBACK(_OnDecideDestination), self);
	g_signal_connect(dl, "received-data",
		G_CALLBACK(_OnReceivedData), self);
	g_signal_connect(dl, "finished",
		G_CALLBACK(_OnFinished), self);
	g_signal_connect(dl, "failed",
		G_CALLBACK(_OnFailed), self);

	WebKitURIRequest* req = webkit_download_get_request(dl);
	const char* uri = webkit_uri_request_get_uri(req);

	std::lock_guard<std::mutex> lock(self->fMutex);
	DownloadEntry entry;
	entry.id            = self->fNextId++;
	entry.url           = uri ? uri : "";
	entry.totalBytes    = 0;
	entry.receivedBytes = 0;
	entry.finished      = false;
	entry.failed        = false;
	entry.cancelled     = false;
	self->fDownloads[dl] = entry;
	log_info("DownloadHandler: download started (%s)", entry.url.c_str());
}


gboolean
DownloadHandler::_OnDecideDestination(WebKitDownload* dl,
	const char* suggestedFilename, gpointer data)
{
	auto* self = static_cast<DownloadHandler*>(data);
	std::string filename = SanitizeDownloadFilename(
		suggestedFilename ? suggestedFilename : "download");
	std::string dest = SafeDownloadDestination(self->fDownloadDir,
		suggestedFilename ? suggestedFilename : "download");

	if (dest != UniqueDownloadDestination(self->fDownloadDir, filename)) {
		log_error("DownloadHandler: destination escaped dir, using fallback");
		filename = "download";
	}

	webkit_download_set_destination(dl, dest.c_str());

	std::lock_guard<std::mutex> lock(self->fMutex);
	auto it = self->fDownloads.find(dl);
	if (it != self->fDownloads.end()) {
		it->second.filename    = filename;
		it->second.destination = dest;
		if (self->onDownloadStarted)
			self->onDownloadStarted(it->second);
	}
	return TRUE;
}


void
DownloadHandler::_OnReceivedData(WebKitDownload* dl, guint64 dataLength,
	gpointer data)
{
	auto* self = static_cast<DownloadHandler*>(data);
	std::lock_guard<std::mutex> lock(self->fMutex);
	auto it = self->fDownloads.find(dl);
	if (it == self->fDownloads.end()) return;
	it->second.receivedBytes += dataLength;
	if (self->onDownloadProgress)
		self->onDownloadProgress(it->second);
}


void
DownloadHandler::_OnFinished(WebKitDownload* dl, gpointer data)
{
	auto* self = static_cast<DownloadHandler*>(data);
	std::lock_guard<std::mutex> lock(self->fMutex);
	auto it = self->fDownloads.find(dl);
	if (it == self->fDownloads.end()) return;
	it->second.finished = true;
	log_info("DownloadHandler: finished '%s'", it->second.filename.c_str());
	if (self->onDownloadFinished)
		self->onDownloadFinished(it->second);
	self->fDownloads.erase(it);
}


void
DownloadHandler::_OnFailed(WebKitDownload* dl, GError* error, gpointer data)
{
	auto* self = static_cast<DownloadHandler*>(data);
	std::lock_guard<std::mutex> lock(self->fMutex);
	auto it = self->fDownloads.find(dl);
	if (it == self->fDownloads.end()) return;
	it->second.failed = true;
	log_error("DownloadHandler: failed '%s': %s",
		it->second.filename.c_str(), error ? error->message : "unknown");
	if (self->onDownloadFailed)
		self->onDownloadFailed(it->second);
	self->fDownloads.erase(it);
}
