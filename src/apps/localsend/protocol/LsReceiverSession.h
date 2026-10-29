/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef _LS_RECEIVER_SESSION_H
#define _LS_RECEIVER_SESSION_H

#include <map>
#include <string>
#include <vector>

#include "LsDevice.h"
#include "LsHttpServer.h"

namespace LocalSend {

// Decision for an incoming prepare-upload.
struct LsReceiveDecision {
	bool			accepted;
	bool			autoAccepted;	// no prompt (always-allow device)

						LsReceiveDecision()
							:
							accepted(false),
							autoAccepted(false)
						{
						}
};


// Application hook: asked on a server worker thread when a peer wants
// to send files. |files| is the proposed set. Fill |acceptedFiles|
// with the subset of file ids to accept (empty plus accepted=true
// means all). The return value decides accept/reject.
class LsReceivePrompt {
public:
	virtual					~LsReceivePrompt() {}

	virtual	LsReceiveDecision	Prompt(const LsDevice& sender,
									const std::map<std::string, LsFileItem>&
										files,
									const std::string& pin,
									std::vector<std::string>&
										acceptedFiles) = 0;
};


// One accepted upload file: wire metadata plus where it lands and its
// running hash.
struct LsReceiveFile {
	LsFileItem		item;
	std::string		targetPath;
	void*			hashContext;	// LsSha256*, opaque here
	int64_t			received;
	bool			failed;

						LsReceiveFile()
							:
							hashContext(NULL),
							received(0),
							failed(false)
						{
						}
};


// The active receive session: one peer, one set of tokens.
struct LsReceiveSessionState {
	std::string				sessionId;
	LsDevice				sender;
	std::string				senderAddress;
	// file id -> upload token
	std::map<std::string, std::string>	tokens;
	// file id -> state
	std::map<std::string, LsReceiveFile>	files;
	bool					active;

							LsReceiveSessionState()
								:
								active(true)
							{
							}
};


// Receiver-side session logic implementing LsHttpServerHandler. The
// destination folder and the accept prompt are injected; file writes
// go through a plain stdio path so host tests run unmodified.
class LsReceiverSession : public LsHttpServerHandler {
public:
						LsReceiverSession();
	virtual				~LsReceiverSession();

			void			SetDestination(const std::string& folder)
								{ fDestination = folder; }
			void			SetPrompt(LsReceivePrompt* prompt)
								{ fPrompt = prompt; }
			void			SetRequiredPin(const std::string& pin)
								{ fRequiredPin = pin; }

			// Fingerprint list consulted for auto-accept.
			void			AddTrustedFingerprint(const std::string& fingerprint);
			bool			IsTrusted(const std::string& fingerprint) const;

			const LsReceiveSessionState*	Session() const
								{ return fSessionActive ? &fSession : NULL; }

			// LsHttpServerHandler
	virtual	void			RegisterReceived(const LsDevice& device,
									const std::string& address);
	virtual	int				PrepareUploadReceived(const LsDevice& sender,
									const std::map<std::string, LsFileItem>&
										files,
									const std::string& pin,
									const std::string& address,
									LsPrepareUploadResponse& response);
	virtual	LsUploadSink*	StartUpload(const std::string& sessionId,
									const std::string& fileId,
									const std::string& token,
									int64_t size);
	virtual	void			CancelReceived(const std::string& sessionId);

private:
			friend class LsReceiveUploadSink;

			std::string		_SafeFileName(const std::string& name) const;
			std::string		_ReserveTargetPath(const std::string& name) const;
			std::string		_TargetPathFor(const std::string& fileId) const;

			static	void	_MarkFailed(LsReceiverSession* session,
									const std::string& fileId);
			static	void	_MarkReceived(LsReceiverSession* session,
									const std::string& fileId,
									int64_t bytes);
			static	void	_MarkComplete(LsReceiverSession* session,
									const std::string& fileId);

			std::string		fDestination;
			std::string		fRequiredPin;
			LsReceivePrompt*	fPrompt;
			std::vector<std::string>	fTrusted;
			bool			fSessionActive;
			LsReceiveSessionState	fSession;
			// Peers seen via /register, kept for the device list.
			std::map<std::string, LsDevice>	fKnownPeers;
};

} // namespace LocalSend

#endif // _LS_RECEIVER_SESSION_H
