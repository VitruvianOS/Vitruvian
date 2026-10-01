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


class DownloadHandler {
public:
						DownloadHandler();
						~DownloadHandler();

			void		Attach(WebKitNetworkSession* session);

			void		SetDownloadDir(const std::string& dir)
							{ fDownloadDir = dir; }
	const std::string&	DownloadDir() const { return fDownloadDir; }

			// Returns a snapshot copy — safe to call from any thread.
			std::vector<DownloadEntry> ActiveDownloads() const;

			std::function<void(const DownloadEntry&)> onDownloadStarted;
			std::function<void(const DownloadEntry&)> onDownloadProgress;
			std::function<void(const DownloadEntry&)> onDownloadFinished;
			std::function<void(const DownloadEntry&)> onDownloadFailed;

private:
	static void		_OnDownloadStarted(WebKitNetworkSession*, WebKitDownload*,
							gpointer);
	static gboolean _OnDecideDestination(WebKitDownload*, const char*,
							gpointer);
	static void		_OnReceivedData(WebKitDownload*, guint64, gpointer);
	static void		_OnFinished(WebKitDownload*, gpointer);
	static void		_OnFailed(WebKitDownload*, GError*, gpointer);

	static std::string	_SanitizeFilename(const std::string& name);
	static std::string	_UniqueDestination(const std::string& dir,
							const std::string& filename);

	mutable std::mutex					fMutex;
			std::map<WebKitDownload*, DownloadEntry> fDownloads;
			uint64_t					fNextId;
			std::string					fDownloadDir;
};


#endif // VOS_DOWNLOAD_HANDLER_H
