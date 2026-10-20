/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef VOS_DOWNLOAD_HANDLER_H
#define VOS_DOWNLOAD_HANDLER_H


#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <wpe/webkit.h>


struct DownloadEntry {
	uint64_t		id;
	std::string		filename;
	std::string		url;
	std::string		destination;
	uint64_t		totalBytes;
	uint64_t		receivedBytes;
	bool			finished;
	bool			failed;
	bool			cancelled;
};


// Per-session download fan-out: one WebKit "download-started" connection
// per session, N window listeners. All callbacks run on the GLib thread.
class DownloadHandler {
public:
						DownloadHandler();
						~DownloadHandler();

			void		Attach(WebKitNetworkSession* session);

			// One handler per network session: the "download-started"
			// signal would fire once per attached handler otherwise.
	static	DownloadHandler* ForSession(WebKitNetworkSession* session);

			void		SetDownloadDir(const std::string& dir)
							{ fDownloadDir = dir.empty()
								? fDefaultDownloadDir : dir; }
	const std::string&	DownloadDir() const { return fDownloadDir; }

			// When true each download asks the user where to save
			// through onAskDestination before it starts.
			void		SetAskLocation(bool ask) { fAskLocation = ask; }
			bool		AskLocation() const { return fAskLocation; }

			// Pins the next download of this URI to a path chosen by
			// the user (Save image as / Save link as).
			void		SetDestinationOverride(const std::string& uri,
							const std::string& destination);

			void		Cancel(uint64_t id);

			// Returns a snapshot copy — safe to call from any thread.
			std::vector<DownloadEntry> ActiveDownloads() const;

	using Listener = struct DownloadListener {
		std::function<void(const DownloadEntry&)> onStarted;
		std::function<void(const DownloadEntry&)> onProgress;
		std::function<void(const DownloadEntry&)> onFinished;
		std::function<void(const DownloadEntry&)> onFailed;
	};

			// The owner pointer keys the map so a listener can remove
			// itself by identity when its window closes.
			void		AddListener(const void* owner,
							const Listener& listener);
			void		RemoveListener(const void* owner);

			// Blocks the GLib thread in decide-destination until the
			// host returns a path ("" cancels); newest owner wins.
	using	AskHandler = std::function<std::string(const std::string&,
				const std::string&)>;
			void		SetAskHandler(const void* owner,
							const AskHandler& handler);

private:
			void		_Notify(std::function<void(const Listener&)> fn);

	static void		_OnDownloadStarted(WebKitNetworkSession*, WebKitDownload*,
							gpointer);
	static gboolean _OnDecideDestination(WebKitDownload*, const char*,
							gpointer);
	static void		_OnReceivedData(WebKitDownload*, guint64, gpointer);
	static void		_OnFinished(WebKitDownload*, gpointer);
	static void		_OnFailed(WebKitDownload*, GError*, gpointer);

	mutable std::mutex					fMutex;
			std::map<WebKitDownload*, DownloadEntry> fDownloads;
			std::map<std::string, std::string> fDestinationOverrides;
			std::map<const void*, Listener>	fListeners;
			AskHandler					fAskHandler;
			const void*					fAskOwner;
			uint64_t					fNextId;
			std::string					fDownloadDir;
			std::string					fDefaultDownloadDir;
			bool						fAskLocation;
};


#endif // VOS_DOWNLOAD_HANDLER_H
