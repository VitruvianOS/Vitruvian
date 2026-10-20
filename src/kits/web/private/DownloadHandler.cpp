/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "download_handler.h"

#include "DownloadSanitizer.h"
#include "logging.h"
#include "process_manager.h"

#include <cstdlib>
#include <filesystem>
#include <wpe/webkit.h>


DownloadHandler::DownloadHandler()
	:
	fAskOwner(NULL),
	fNextId(1),
	fAskLocation(false)
{
	const char* home = getenv("HOME");
	fDefaultDownloadDir = home ? std::string(home) + "/Downloads" : "/tmp";
	fDownloadDir = fDefaultDownloadDir;
	log_info("DownloadHandler: download dir = %s", fDownloadDir.c_str());
}


DownloadHandler::~DownloadHandler()
{
}


DownloadHandler*
DownloadHandler::ForSession(WebKitNetworkSession* session)
{
	// One handler per session: attaching twice would fire
	// "download-started" once per attachment and duplicate every entry.
	static std::map<WebKitNetworkSession*, DownloadHandler*> sHandlers;
	static std::mutex sMutex;

	std::lock_guard<std::mutex> lock(sMutex);
	auto it = sHandlers.find(session);
	if (it != sHandlers.end())
		return it->second;

	DownloadHandler* handler = new DownloadHandler();
	handler->Attach(session);
	sHandlers[session] = handler;
	return handler;
}


void
DownloadHandler::Attach(WebKitNetworkSession* session)
{
	g_signal_connect(session, "download-started",
		G_CALLBACK(_OnDownloadStarted), this);
	log_info("DownloadHandler: attached to network session");
}


void
DownloadHandler::AddListener(const void* owner, const Listener& listener)
{
	std::lock_guard<std::mutex> lock(fMutex);
	fListeners[owner] = listener;
}


void
DownloadHandler::RemoveListener(const void* owner)
{
	std::lock_guard<std::mutex> lock(fMutex);
	fListeners.erase(owner);
}


void
DownloadHandler::_Notify(std::function<void(const Listener&)> fn)
{
	// Snapshot under the lock, invoke outside: a listener posting a
	// BMessage is fine, one that takes our mutex again would not be.
	std::vector<Listener> listeners;
	{
		std::lock_guard<std::mutex> lock(fMutex);
		for (const auto& kv : fListeners)
			listeners.push_back(kv.second);
	}
	for (const auto& listener : listeners)
		fn(listener);
}


void
DownloadHandler::SetAskHandler(const void* owner, const AskHandler& handler)
{
	std::lock_guard<std::mutex> lock(fMutex);
	if (handler == nullptr) {
		// A closed window only clears its own slot.
		if (fAskOwner == owner) {
			fAskOwner = NULL;
			fAskHandler = nullptr;
		}
		return;
	}
	fAskOwner = owner;
	fAskHandler = handler;
}


void
DownloadHandler::SetDestinationOverride(const std::string& uri,
	const std::string& destination)
{
	std::lock_guard<std::mutex> lock(fMutex);
	fDestinationOverrides[uri] = destination;
}


void
DownloadHandler::Cancel(uint64_t id)
{
	WebKitDownload* found = NULL;
	{
		std::lock_guard<std::mutex> lock(fMutex);
		for (const auto& kv : fDownloads) {
			if (kv.second.id == id) {
				found = kv.first;
				break;
			}
		}
		if (found == NULL)
			return;
		fDownloads[found].cancelled = true;
	}
	// webkit_download_cancel must run on the GLib thread; hold a ref so
	// a finish racing this cancel cannot free the download first.
	g_object_ref(found);
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		WebKitDownload* dl = static_cast<WebKitDownload*>(d);
		webkit_download_cancel(dl);
		g_object_unref(dl);
		return G_SOURCE_REMOVE;
	}, found);
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

	WebKitURIResponse* response = webkit_download_get_response(dl);
	guint64 total = response != NULL
		? webkit_uri_response_get_content_length(response) : 0;

	DownloadEntry entry;
	{
		std::lock_guard<std::mutex> lock(self->fMutex);
		entry.id            = self->fNextId++;
		entry.url           = uri ? uri : "";
		entry.totalBytes    = total;
		entry.receivedBytes = 0;
		entry.finished      = false;
		entry.failed        = false;
		entry.cancelled     = false;
		self->fDownloads[dl] = entry;
	}
	log_info("DownloadHandler: download started (%s)", entry.url.c_str());
	self->_Notify([&entry](const Listener& listener) {
		if (listener.onStarted)
			listener.onStarted(entry);
	});
}


gboolean
DownloadHandler::_OnDecideDestination(WebKitDownload* dl,
	const char* suggestedFilename, gpointer data)
{
	auto* self = static_cast<DownloadHandler*>(data);
	const char* suggested = suggestedFilename ? suggestedFilename : "download";
	std::string filename = SanitizeDownloadFilename(suggested);

	WebKitURIRequest* req = webkit_download_get_request(dl);
	const char* uri = req != NULL ? webkit_uri_request_get_uri(req) : NULL;
	std::string url = uri != NULL ? uri : "";

	// A pinned destination (Save image as) wins over everything.
	std::string dest;
	{
		std::lock_guard<std::mutex> lock(self->fMutex);
		auto it = self->fDestinationOverrides.find(url);
		if (it != self->fDestinationOverrides.end()) {
			dest = it->second;
			self->fDestinationOverrides.erase(it);
		}
	}

	if (dest.empty() && self->fAskLocation) {
		AskHandler handler;
		{
			std::lock_guard<std::mutex> lock(self->fMutex);
			handler = self->fAskHandler;
		}
		if (handler != nullptr) {
			// Blocks the GLib thread until the host's save panel answered.
			dest = handler(filename, url);
			if (dest.empty()) {
				{
					std::lock_guard<std::mutex> lock(self->fMutex);
					self->fDownloads.erase(dl);
				}
				log_info("DownloadHandler: cancelled by user (%s)",
					url.c_str());
				return FALSE;
			}
		}
	}

	if (dest.empty()) {
		std::error_code error;
		std::filesystem::create_directories(self->fDownloadDir, error);
		dest = SafeDownloadDestination(self->fDownloadDir, suggested);
	}

	webkit_download_set_destination(dl, dest.c_str());

	DownloadEntry updated;
	{
		std::lock_guard<std::mutex> lock(self->fMutex);
		auto it = self->fDownloads.find(dl);
		if (it != self->fDownloads.end()) {
			size_t slash = dest.rfind('/');
			it->second.filename    = slash != std::string::npos
				? dest.substr(slash + 1) : dest;
			it->second.destination = dest;
			updated = it->second;
		}
	}
	if (!updated.filename.empty())
		self->_Notify([&updated](const Listener& listener) {
			if (listener.onStarted)
				listener.onStarted(updated);
		});
	return TRUE;
}


void
DownloadHandler::_OnReceivedData(WebKitDownload* dl, guint64 dataLength,
	gpointer data)
{
	auto* self = static_cast<DownloadHandler*>(data);
	DownloadEntry updated;
	{
		std::lock_guard<std::mutex> lock(self->fMutex);
		auto it = self->fDownloads.find(dl);
		if (it == self->fDownloads.end())
			return;
		it->second.receivedBytes += dataLength;
		WebKitURIResponse* response = webkit_download_get_response(dl);
		if (response != NULL) {
			guint64 total
				= webkit_uri_response_get_content_length(response);
			if (total > 0)
				it->second.totalBytes = total;
		}
		updated = it->second;
	}
	self->_Notify([&updated](const Listener& listener) {
		if (listener.onProgress)
			listener.onProgress(updated);
	});
}


void
DownloadHandler::_OnFinished(WebKitDownload* dl, gpointer data)
{
	auto* self = static_cast<DownloadHandler*>(data);
	DownloadEntry updated;
	{
		std::lock_guard<std::mutex> lock(self->fMutex);
		auto it = self->fDownloads.find(dl);
		if (it == self->fDownloads.end())
			return;
		it->second.finished = true;
		updated = it->second;
		self->fDownloads.erase(it);
	}
	log_info("DownloadHandler: finished '%s'", updated.filename.c_str());
	self->_Notify([&updated](const Listener& listener) {
		if (listener.onFinished)
			listener.onFinished(updated);
	});
}


void
DownloadHandler::_OnFailed(WebKitDownload* dl, GError* error, gpointer data)
{
	auto* self = static_cast<DownloadHandler*>(data);
	DownloadEntry updated;
	bool cancelled = error != NULL && g_error_matches(error,
		WEBKIT_DOWNLOAD_ERROR, WEBKIT_DOWNLOAD_ERROR_CANCELLED_BY_USER);
	{
		std::lock_guard<std::mutex> lock(self->fMutex);
		auto it = self->fDownloads.find(dl);
		if (it == self->fDownloads.end())
			return;
		it->second.cancelled = it->second.cancelled || cancelled;
		it->second.failed = !it->second.cancelled;
		updated = it->second;
		self->fDownloads.erase(it);
	}
	log_error("DownloadHandler: failed '%s': %s",
		updated.filename.c_str(), error ? error->message : "unknown");
	self->_Notify([&updated](const Listener& listener) {
		if (listener.onFailed)
			listener.onFailed(updated);
	});
}
