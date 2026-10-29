/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "LsReceiverSession.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "LsFingerprint.h"
#include "LsProtocol.h"
#include "LsSha256.h"

namespace LocalSend {

// Upload sink: streams into the target file, hashing as it goes.
class LsReceiveUploadSink : public LsUploadSink {
public:
						LsReceiveUploadSink(LsReceiverSession* session,
							const std::string& fileId,
							const std::string& targetPath,
							const std::string& expectedSha,
							LsSha256* hash)
							:
							fSession(session),
							fFileId(fileId),
							fTargetPath(targetPath),
							fExpectedSha(expectedSha),
							fHash(hash),
							fFile(NULL),
							fOk(true)
						{
							fFile = fopen(targetPath.c_str(), "wb");
							if (fFile == NULL)
								fOk = false;
						}

	virtual				~LsReceiveUploadSink()
						{
							if (fFile != NULL)
								fclose(fFile);
							delete fHash;
						}

	virtual	bool		Write(const void* data, size_t length)
						{
							if (!fOk || fFile == NULL)
								return false;
							if (fwrite(data, 1, length, fFile) != length) {
								fOk = false;
								return false;
							}
							if (fHash != NULL)
								fHash->Update(data, length);
							LsReceiverSession::_MarkReceived(fSession,
								fFileId, static_cast<int64_t>(length));
							return true;
						}

	virtual	int			Finish(bool success)
						{
							if (fFile != NULL) {
								fflush(fFile);
								fclose(fFile);
								fFile = NULL;
							}

							bool verified = true;
							if (success && fOk && fHash != NULL
								&& !fExpectedSha.empty()) {
								char hex[65];
								fHash->FinalHex(hex);
								verified = LsFingerprintsEqual(hex,
									fExpectedSha);
							}
							if (!success || !fOk) {
								// Leave no partial file behind.
								remove(fTargetPath.c_str());
								LsReceiverSession::_MarkFailed(fSession,
									fFileId);
								return 500;
							}
							if (!verified) {
								// Spec: 422 on checksum mismatch.
								remove(fTargetPath.c_str());
								LsReceiverSession::_MarkFailed(fSession,
									fFileId);
								return 422;
							}
							LsReceiverSession::_MarkComplete(fSession,
								fFileId);
							return 200;
						}

private:
			LsReceiverSession*	fSession;
			std::string		fFileId;
			std::string		fTargetPath;
			std::string		fExpectedSha;
			LsSha256*		fHash;
			FILE*			fFile;
			bool			fOk;
};


LsReceiverSession::LsReceiverSession()
	:
	fPrompt(NULL),
	fSessionActive(false)
{
}


LsReceiverSession::~LsReceiverSession()
{
}


void
LsReceiverSession::AddTrustedFingerprint(const std::string& fingerprint)
{
	if (!fingerprint.empty())
		fTrusted.push_back(fingerprint);
}


bool
LsReceiverSession::IsTrusted(const std::string& fingerprint) const
{
	for (size_t i = 0; i < fTrusted.size(); i++) {
		if (LsFingerprintsEqual(fTrusted[i], fingerprint))
			return true;
	}
	return false;
}


void
LsReceiverSession::RegisterReceived(const LsDevice& device,
	const std::string& address)
{
	if (device.fingerprint.empty())
		return;
	fKnownPeers[device.fingerprint] = device;
}


int
LsReceiverSession::PrepareUploadReceived(const LsDevice& sender,
	const std::map<std::string, LsFileItem>& files, const std::string& pin,
	const std::string& address, LsPrepareUploadResponse& response)
{
	if (files.empty())
		return 400;

	// One session at a time (protocol: 409 when busy).
	if (fSessionActive)
		return 409;

	if (!fRequiredPin.empty() && pin != fRequiredPin)
		return 401;

	LsReceiveDecision decision;
	bool autoAccepted = IsTrusted(sender.fingerprint);
	if (autoAccepted) {
		decision.accepted = true;
		decision.autoAccepted = true;
	} else if (fPrompt != NULL) {
		std::vector<std::string> acceptedFiles;
		decision = fPrompt->Prompt(sender, files, pin, acceptedFiles);
		if (decision.accepted && !acceptedFiles.empty()) {
			// Subset accepted: rebuild the file map.
			std::map<std::string, LsFileItem> subset;
			for (size_t i = 0; i < acceptedFiles.size(); i++) {
				std::map<std::string, LsFileItem>::const_iterator it
					= files.find(acceptedFiles[i]);
				if (it != files.end())
					subset[it->first] = it->second;
			}
			fSession.files.clear();
			for (std::map<std::string, LsFileItem>::const_iterator it
					= subset.begin(); it != subset.end(); it++) {
				fSession.files[it->first].item = it->second;
			}
		}
	} else {
		// No prompt installed: reject, callers must decide.
		return 403;
	}

	if (!decision.accepted)
		return 403;

	// When the prompt accepted everything (no subset given), take all.
	if (fSession.files.empty()) {
		for (std::map<std::string, LsFileItem>::const_iterator it
				= files.begin(); it != files.end(); it++) {
			fSession.files[it->first].item = it->second;
		}
	}

	// Build tokens and target paths.
	fSession.sessionId = LsGenerateFingerprint().substr(0, 16);
	fSession.sender = sender;
	fSession.senderAddress = address;
	fSession.tokens.clear();
	fSession.active = true;

	response.sessionId = fSession.sessionId;
	for (std::map<std::string, LsReceiveFile>::iterator it
			= fSession.files.begin(); it != fSession.files.end(); it++) {
		std::string token = LsGenerateFingerprint().substr(0, 16);
		fSession.tokens[it->first] = token;
		it->second.targetPath = _ReserveTargetPath(it->second.item.fileName);
		it->second.received = 0;
		it->second.failed = false;
		response.files[it->first] = token;
	}

	fSessionActive = true;
	return 200;
}


LsUploadSink*
LsReceiverSession::StartUpload(const std::string& sessionId,
	const std::string& fileId, const std::string& token, int64_t size)
{
	if (!fSessionActive || sessionId != fSession.sessionId)
		return NULL;

	std::map<std::string, std::string>::iterator tokenIt
		= fSession.tokens.find(fileId);
	if (tokenIt == fSession.tokens.end() || tokenIt->second != token)
		return NULL;

	std::map<std::string, LsReceiveFile>::iterator fileIt
		= fSession.files.find(fileId);
	if (fileIt == fSession.files.end())
		return NULL;
	if (size != fileIt->second.item.size)
		return NULL;

	LsSha256* hash = NULL;
	if (!fileIt->second.item.sha256.empty())
		hash = new LsSha256();

	return new LsReceiveUploadSink(this, fileId, fileIt->second.targetPath,
		fileIt->second.item.sha256, hash);
}


void
LsReceiverSession::CancelReceived(const std::string& sessionId)
{
	if (!fSessionActive || sessionId != fSession.sessionId)
		return;

	// Remove any files already written in this session.
	for (std::map<std::string, LsReceiveFile>::iterator it
			= fSession.files.begin(); it != fSession.files.end(); it++) {
		if (it->second.received > 0 && !it->second.failed)
			remove(it->second.targetPath.c_str());
	}
	fSession = LsReceiveSessionState();
	fSessionActive = false;
}


std::string
LsReceiverSession::_SafeFileName(const std::string& name) const
{
	// Strip any path components a hostile peer might send.
	size_t slash = name.rfind('/');
	std::string safe = slash == std::string::npos ? name
		: name.substr(slash + 1);
	slash = safe.rfind('\\');
	if (slash != std::string::npos)
		safe = safe.substr(slash + 1);
	if (safe.empty() || safe == "." || safe == "..")
		safe = "file";
	return safe;
}


std::string
LsReceiverSession::_ReserveTargetPath(const std::string& name) const
{
	std::string base = fDestination;
	if (base.empty())
		base = ".";
	std::string safe = _SafeFileName(name);

	std::string candidate = base + "/" + safe;
	struct stat info;
	if (stat(candidate.c_str(), &info) != 0)
		return candidate;

	// Append " (n)" before the extension, like Tracker does.
	size_t dot = safe.rfind('.');
	std::string stem = dot == std::string::npos ? safe : safe.substr(0, dot);
	std::string extension = dot == std::string::npos ? "" : safe.substr(dot);
	for (int i = 1; ; i++) {
		char suffix[16];
		snprintf(suffix, sizeof(suffix), " (%d)", i);
		candidate = base + "/" + stem + suffix + extension;
		if (stat(candidate.c_str(), &info) != 0)
			return candidate;
	}
}


// Called by the sink on failure; declared friend in the header.
void
LsReceiverSession::_MarkFailed(LsReceiverSession* session,
	const std::string& fileId)
{
	std::map<std::string, LsReceiveFile>::iterator it
		= session->fSession.files.find(fileId);
	if (it != session->fSession.files.end())
		it->second.failed = true;
}


void
LsReceiverSession::_MarkReceived(LsReceiverSession* session,
	const std::string& fileId, int64_t bytes)
{
	std::map<std::string, LsReceiveFile>::iterator it
		= session->fSession.files.find(fileId);
	if (it != session->fSession.files.end())
		it->second.received += bytes;
}


void
LsReceiverSession::_MarkComplete(LsReceiverSession* session,
	const std::string& fileId)
{
	std::map<std::string, LsReceiveFile>::iterator it
		= session->fSession.files.find(fileId);
	if (it == session->fSession.files.end())
		return;
	if (it->second.received < it->second.item.size) {
		it->second.failed = true;
		return;
	}

	// Session is done once every file has landed or failed; free it
	// so the next prepare-upload is not answered with 409.
	for (std::map<std::string, LsReceiveFile>::iterator check
			= session->fSession.files.begin();
			check != session->fSession.files.end(); check++) {
		if (!check->second.failed
			&& check->second.received < check->second.item.size)
			return;
	}
	session->fSession = LsReceiveSessionState();
	session->fSessionActive = false;
}


std::string
LsReceiverSession::_TargetPathFor(const std::string& fileId) const
{
	std::map<std::string, LsReceiveFile>::const_iterator it
		= fSession.files.find(fileId);
	if (it == fSession.files.end())
		return "";
	return it->second.targetPath;
}

} // namespace LocalSend
