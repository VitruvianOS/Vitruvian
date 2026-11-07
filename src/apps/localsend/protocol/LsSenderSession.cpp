/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "LsSenderSession.h"

#include <stdio.h>
#include <sys/stat.h>

#include "LsProtocol.h"

namespace LocalSend {

LsSenderSession::LsSenderSession()
	:
	fPreparedOk(false)
{
}


int
LsSenderSession::AddFile(const std::string& path)
{
	struct stat info;
	if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
		return -1;

	size_t slash = path.rfind('/');
	std::string name = slash == std::string::npos ? path
		: path.substr(slash + 1);
	if (name.empty())
		return -1;

	char id[32];
	snprintf(id, sizeof(id), "file%zu", fFiles.size());

	LsSendFile file;
	file.path = path;
	file.item.id = id;
	file.item.fileName = name;
	file.item.size = static_cast<int64_t>(info.st_size);
	file.item.fileType = "application/octet-stream";
	fFiles.push_back(file);
	return static_cast<int>(fFiles.size() - 1);
}


bool
LsSenderSession::Prepare(LsHttpTransport* transport, const std::string& address,
	const LsDevice& target)
{
	if (transport == NULL || fFiles.empty())
		return false;

	LsPrepareUploadRequest request;
	request.sender = fSender;
	for (size_t i = 0; i < fFiles.size(); i++)
		request.files[fFiles[i].item.id] = fFiles[i].item;

	std::string path = kApiBase;
	path += kEndpointPrepareUpload;
	if (!fPin.empty()) {
		path += "?pin=";
		path += fPin;
	}

	uint16_t port = target.port != 0 ? target.port : kDefaultPort;
	LsHttpResult result = transport->PostJson(address, port, path,
		request.ToJson().Serialize());
	if (result.status != 200)
		return false;

	fPreparedOk = LsPrepareUploadResponse::Parse(result.body, fPrepared);
	return fPreparedOk;
}


bool
LsSenderSession::UploadFile(LsHttpTransport* transport, const std::string& address,
	const LsDevice& target, size_t index)
{
	if (transport == NULL || index >= fFiles.size() || !fPreparedOk)
		return false;

	std::map<std::string, std::string>::iterator tokenIt
		= fPrepared.files.find(fFiles[index].item.id);
	if (tokenIt == fPrepared.files.end())
		return false;

	if (fProgress)
		fProgress(index, 0, fFiles[index].item.size);

	std::string path = kApiBase;
	path += kEndpointUpload;
	path += "?sessionId=";
	path += fPrepared.sessionId;
	path += "&fileId=";
	path += fFiles[index].item.id;
	path += "&token=";
	path += tokenIt->second;

	uint16_t port = target.port != 0 ? target.port : kDefaultPort;
	LsHttpResult result = transport->PostFileFromPath(address, port, path,
		fFiles[index].path, fFiles[index].item.size);
	if (result.status != 200)
		return false;

	if (fProgress)
		fProgress(index, fFiles[index].item.size, fFiles[index].item.size);
	return true;
}


bool
LsSenderSession::UploadAll(LsHttpTransport* transport, const std::string& address,
	const LsDevice& target)
{
	if (!fPreparedOk)
		return false;

	bool allOk = true;
	for (size_t i = 0; i < fFiles.size(); i++) {
		if (!UploadFile(transport, address, target, i))
			allOk = false;
	}
	return allOk;
}


void
LsSenderSession::Cancel(LsHttpTransport* transport, const std::string& address,
	const LsDevice& target)
{
	if (transport == NULL || !fPreparedOk)
		return;

	std::string path = kApiBase;
	path += kEndpointCancel;
	path += "?sessionId=";
	path += fPrepared.sessionId;
	uint16_t port = target.port != 0 ? target.port : kDefaultPort;
	transport->PostJson(address, port, path, "{}");
}

} // namespace LocalSend
