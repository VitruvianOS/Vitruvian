/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef _DOWNLOADS_MODEL_H
#define _DOWNLOADS_MODEL_H


#include <stdint.h>
#include <string>
#include <vector>


enum DownloadState {
	kDownloadStateActive	= 0,
	kDownloadStateFinished	= 1,
	kDownloadStateFailed	= 2,
	kDownloadStateCancelled	= 3,
};


struct DownloadRecord {
	uint64_t		id;
	std::string		filename;
	std::string		url;
	std::string		destination;
	uint64_t		receivedBytes;
	uint64_t		totalBytes;
	DownloadState	state;
};


/*!	Download list behind the downloads panel: engine entries are
	added or updated by id, terminal states stick around as history
	until cleared. Plain C++ so a host test covers add/update/remove
	and the finished state without BViews.
*/
class DownloadsModel {
public:
			int32_t			AddOrUpdate(uint64_t id, const char* filename,
								const char* url, const char* destination,
								uint64_t receivedBytes, uint64_t totalBytes);
			bool			SetProgress(uint64_t id, uint64_t receivedBytes,
								uint64_t totalBytes);
			bool			MarkState(uint64_t id, DownloadState state);
			bool			Remove(uint64_t id);

			void			ClearFinished();

			int32_t			Count() const;
			int32_t			IndexOf(uint64_t id) const;
			int32_t			ActiveCount() const;

			const DownloadRecord&	ItemAt(int32_t index) const;

private:
			std::vector<DownloadRecord>	fItems;
};


#endif // _DOWNLOADS_MODEL_H
