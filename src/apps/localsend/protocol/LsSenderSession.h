/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef _LS_SENDER_SESSION_H
#define _LS_SENDER_SESSION_H

#include <stdint.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "LsDevice.h"
#include "LsHttpTransport.h"

namespace LocalSend {

// One file to send: local path plus its wire metadata.
struct LsSendFile {
	std::string		path;
	LsFileItem		item;		// id assigned by the session
};


// Sender-side session (protocol section 4). Drives prepare-upload,
// then uploads every accepted file, then reports completion. The
// transport is injected so host tests and the V\OS app share the
// state machine.
class LsSenderSession {
public:
	// Progress: file index, bytes sent for that file, file size.
	typedef std::function<void(size_t, int64_t, int64_t)> ProgressCallback;

						LsSenderSession();

			void			SetSender(const LsDevice& sender) { fSender = sender; }
			void			SetPin(const std::string& pin) { fPin = pin; }
			void			SetProgressCallback(ProgressCallback callback)
								{ fProgress = callback; }

			// Adds a file by path; id is derived from the index.
			// Returns the file index, or -1 when the file cannot
			// be read.
			int				AddFile(const std::string& path);

			// Runs prepare-upload against |address:port| of |target|.
			// On success the per-file tokens are stored and true is
			// returned.
			bool			Prepare(LsHttpTransport* transport,
									const std::string& address,
									const LsDevice& target);

			// Uploads one prepared file (by index). Files whose
			// token was rejected by the receiver are skipped.
			bool			UploadFile(LsHttpTransport* transport,
									const std::string& address,
									const LsDevice& target, size_t index);

			// Uploads all prepared files in order.
			bool			UploadAll(LsHttpTransport* transport,
									const std::string& address,
									const LsDevice& target);

			// Tells the receiver to drop the session.
			void			Cancel(LsHttpTransport* transport,
									const std::string& address,
									const LsDevice& target);

			size_t			CountFiles() const { return fFiles.size(); }
			const LsSendFile&	FileAt(size_t index) const
								{ return fFiles[index]; }
			// Mutable access for callers that fill in checksums or
			// MIME types after adding the file.
			LsSendFile&		FileAt(size_t index) { return fFiles[index]; }
			const LsPrepareUploadResponse&	Prepared() const
								{ return fPrepared; }
			bool			IsPrepared() const { return fPreparedOk; }

private:
			std::string		_BasePath(const LsDevice& target) const;

			LsDevice		fSender;
			std::string		fPin;
			std::vector<LsSendFile>	fFiles;
			LsPrepareUploadResponse	fPrepared;
			bool			fPreparedOk;
			ProgressCallback	fProgress;
};

} // namespace LocalSend

#endif // _LS_SENDER_SESSION_H
