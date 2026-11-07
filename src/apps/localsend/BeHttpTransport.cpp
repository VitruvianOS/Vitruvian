/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "BeHttpTransport.h"

#include <DataIO.h>
#include <File.h>
#include <HttpRequest.h>
#include <HttpResult.h>
#include <String.h>
#include <Url.h>
#include <UrlProtocolRoster.h>

#include <stdio.h>

#include "LsProtocol.h"

namespace LocalSend {

using BPrivate::Network::BUrlProtocolRoster;
using BPrivate::Network::BHttpRequest;
using BPrivate::Network::BHttpResult;
using BPrivate::Network::BUrlRequest;
using BPrivate::Network::BHttpHeaders;
using BPrivate::Network::B_HTTP_POST;


LsHttpResult
BeHttpTransport::PostJson(const std::string& address, uint16_t port,
	const std::string& path, const std::string& jsonBody)
{
	BString url = "http://";
	url << address.c_str() << ":" << port << path.c_str();
	return _RunRequest(url.String(), B_HTTP_POST, jsonBody.data(),
		jsonBody.size(), "application/json");
}


LsHttpResult
BeHttpTransport::PostFile(const std::string& address, uint16_t port,
	const std::string& path, const void* data, size_t length)
{
	BString url = "http://";
	url << address.c_str() << ":" << port << path.c_str();
	return _RunRequest(url.String(), B_HTTP_POST, data, length,
		"application/octet-stream");
}


LsHttpResult
BeHttpTransport::PostFileFromPath(const std::string& address, uint16_t port,
	const std::string& path, const std::string& filePath, int64_t fileLength)
{
	// Stream the file through BFile so large sends do not buffer.
	// AdoptInputData takes ownership, hence the heap allocation.
	BFile* file = new BFile(filePath.c_str(), B_READ_ONLY);
	if (file->InitCheck() != B_OK) {
		delete file;
		return LsHttpResult();
	}

	BString url = "http://";
	url << address.c_str() << ":" << port << path.c_str();

	LsHttpResult result;

	BUrl requestUrl(url.String(), true);
	BHttpRequest* request = dynamic_cast<BHttpRequest*>(
		BUrlProtocolRoster::MakeRequest(requestUrl, NULL));
	if (request == NULL) {
		delete file;
		return result;
	}

	request->SetMethod(B_HTTP_POST);
	request->AdoptInputData(file, fileLength);

	status_t status = request->Run();
	if (status != B_OK) {
		delete request;
		return result;
	}

	while (request->IsRunning())
		snooze(10000);

	const BHttpResult& httpResult
		= static_cast<const BHttpResult&>(request->Result());
	result.status = httpResult.StatusCode();

	delete request;
	return result;
}


LsHttpResult
BeHttpTransport::_RunRequest(const std::string& url, const char* method,
	const void* body, size_t bodyLength, const char* contentType)
{
	LsHttpResult result;

	BUrl requestUrl(url.c_str(), true);
	BDataIO* output = new BMallocIO();

	BHttpRequest* request = dynamic_cast<BHttpRequest*>(
		BUrlProtocolRoster::MakeRequest(requestUrl, output));
	if (request == NULL) {
		delete output;
		return result;
	}

	request->SetMethod(method);
	// AdoptHeaders takes ownership.
	BHttpHeaders* headers = new BHttpHeaders();
	headers->AddHeader("Content-Type", contentType);
	request->AdoptHeaders(headers);

	if (body != NULL && bodyLength > 0) {
		BMemoryIO* input = new BMemoryIO(body, bodyLength);
		request->AdoptInputData(input, static_cast<ssize_t>(bodyLength));
	}

	status_t status = request->Run();
	if (status != B_OK) {
		delete request;
		delete output;
		return result;
	}

	while (request->IsRunning())
		snooze(10000);

	const BHttpResult& httpResult
		= static_cast<const BHttpResult&>(request->Result());
	result.status = httpResult.StatusCode();

	BMallocIO* buffer = dynamic_cast<BMallocIO*>(output);
	if (buffer != NULL && buffer->Buffer() != NULL) {
		result.body.assign(static_cast<const char*>(buffer->Buffer()),
			static_cast<size_t>(buffer->BufferLength()));
	}

	delete request;
	delete output;
	return result;
}

} // namespace LocalSend
