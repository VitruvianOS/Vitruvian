/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "LsHttpServer.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "LsProtocol.h"

namespace LocalSend {

static const size_t kMaxHeaderSize = 16384;
static const size_t kMaxJsonBody = 4 * 1024 * 1024;
static const int kConnectionBacklog = 16;


// Argument handed to a detached connection worker.
struct LsConnectionRequest {
	LsHttpServer*	server;
	int				clientSocket;

					LsConnectionRequest(LsHttpServer* _server, int socket)
						:
						server(_server),
						clientSocket(socket)
					{
					}
};


static std::string
_DecimalString(int64_t value)
{
	char buffer[24];
	snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
	return std::string(buffer);
}


static bool
_ParseQuery(const std::string& text,
	std::map<std::string, std::string>& result)
{
	size_t start = 0;
	while (start < text.size()) {
		size_t end = text.find('&', start);
		if (end == std::string::npos)
			end = text.size();
		size_t equals = text.find('=', start);
		if (equals != std::string::npos && equals < end) {
			std::string key = text.substr(start, equals - start);
			std::string value = text.substr(equals + 1, end - equals - 1);
			if (!key.empty())
				result[key] = value;
		}
		start = end + 1;
	}
	return true;
}


static bool
_ReadLine(int socket, std::string& line)
{
	line.clear();
	char c;
	while (true) {
		ssize_t n = recv(socket, &c, 1, 0);
		if (n <= 0)
			return false;
		if (c == '\n')
			return true;
		if (c != '\r')
			line += c;
		if (line.size() > kMaxHeaderSize)
			return false;
	}
}


static bool
_ReadFully(int socket, char* buffer, size_t length)
{
	size_t total = 0;
	while (total < length) {
		ssize_t n = recv(socket, buffer + total, length - total, 0);
		if (n <= 0) {
			if (n < 0 && errno == EINTR)
				continue;
			return false;
		}
		total += static_cast<size_t>(n);
	}
	return true;
}


LsHttpServer::LsHttpServer()
	:
	fListenSocket(-1),
	fPort(0),
	fAcceptThread(0),
	fRunning(false),
	fThreadStarted(false),
	fHandler(NULL)
{
	pthread_mutex_init(&fDeviceLock, NULL);
}


LsHttpServer::~LsHttpServer()
{
	Stop();
	if (fListenSocket >= 0)
		close(fListenSocket);
	pthread_mutex_destroy(&fDeviceLock);
}


int
LsHttpServer::Init(uint16_t port)
{
	fListenSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (fListenSocket < 0)
		return -1;

	int one = 1;
	setsockopt(fListenSocket, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

	struct sockaddr_in address;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons(port);

	if (bind(fListenSocket, reinterpret_cast<struct sockaddr*>(&address),
			sizeof(address)) != 0) {
		// Port busy: fall back to an ephemeral port so a second
		// instance still comes up (announced via Port()).
		address.sin_port = 0;
		if (bind(fListenSocket, reinterpret_cast<struct sockaddr*>(&address),
				sizeof(address)) != 0) {
			close(fListenSocket);
			fListenSocket = -1;
			return -1;
		}
	}

	socklen_t addressLength = sizeof(address);
	if (getsockname(fListenSocket, reinterpret_cast<struct sockaddr*>(&address),
			&addressLength) != 0) {
		close(fListenSocket);
		fListenSocket = -1;
		return -1;
	}
	fPort = ntohs(address.sin_port);

	if (listen(fListenSocket, kConnectionBacklog) != 0) {
		close(fListenSocket);
		fListenSocket = -1;
		return -1;
	}
	return 0;
}


int
LsHttpServer::Start(LsHttpServerHandler* handler, const LsDevice& device)
{
	if (fListenSocket < 0 || fRunning)
		return -1;
	fHandler = handler;
	SetDevice(device);

	fRunning = true;
	int status = pthread_create(&fAcceptThread, NULL,
		&LsHttpServer::_AcceptThreadEntry, this);
	if (status != 0) {
		fRunning = false;
		return -1;
	}
	fThreadStarted = true;
	return 0;
}


void
LsHttpServer::Stop()
{
	if (!fRunning && !fThreadStarted)
		return;

	fRunning = false;
	if (fListenSocket >= 0)
		shutdown(fListenSocket, SHUT_RDWR);

	if (fThreadStarted) {
		pthread_join(fAcceptThread, NULL);
		fThreadStarted = false;
	}
}


void
LsHttpServer::SetDevice(const LsDevice& device)
{
	pthread_mutex_lock(&fDeviceLock);
	fDevice = device;
	pthread_mutex_unlock(&fDeviceLock);
}


/* static */ void*
LsHttpServer::_AcceptThreadEntry(void* data)
{
	static_cast<LsHttpServer*>(data)->_AcceptLoop();
	return NULL;
}


void
LsHttpServer::_AcceptLoop()
{
	while (fRunning) {
		// shutdown() does not reliably wake accept(); Stop() joins us.
		struct pollfd descriptor;
		descriptor.fd = fListenSocket;
		descriptor.events = POLLIN;
		descriptor.revents = 0;
		int status = poll(&descriptor, 1, 500);
		if (status < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (status == 0 || (descriptor.revents & POLLIN) == 0)
			continue;

		int client = accept(fListenSocket, NULL, NULL);
		if (client < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		// Hand the connection to a detached worker. Bounded by the
		// listen backlog plus a small hard cap; LocalSend clients
		// open one connection per file at most.
		pthread_t thread;
		void* argument = new LsConnectionRequest(this, client);
		status = pthread_create(&thread, NULL,
			&LsHttpServer::_ConnectionThreadEntry, argument);
		if (status != 0) {
			delete static_cast<LsConnectionRequest*>(argument);
			close(client);
			continue;
		}
		pthread_detach(thread);
	}
}


/* static */ void*
LsHttpServer::_ConnectionThreadEntry(void* data)
{
	LsConnectionRequest* request = static_cast<LsConnectionRequest*>(data);
	LsHttpServer* server = request->server;
	int client = request->clientSocket;
	delete request;

	server->_HandleConnection(client);
	return NULL;
}


void
LsHttpServer::_HandleConnection(int clientSocket)
{
	std::string method;
	std::string pathAndQuery;
	std::map<std::string, std::string> headers;

	// Peek the route before reading any body: the upload route must
	// stream its body instead of buffering it.
	if (_ReadHeaders(clientSocket, method, pathAndQuery, headers)) {
		std::string path = pathAndQuery;
		std::map<std::string, std::string> query;
		size_t question = pathAndQuery.find('?');
		if (question != std::string::npos) {
			path = pathAndQuery.substr(0, question);
			_ParseQuery(pathAndQuery.substr(question + 1), query);
		}

		if (path == std::string(kApiBase) + kEndpointUpload
			&& method == "POST") {
			_HandleUpload(clientSocket, query, headers);
		} else {
			std::string body;
			if (!_ReadBody(clientSocket, headers, body)) {
				_SendError(clientSocket, 400, "Bad Request");
			} else {
				_HandleRoute(clientSocket, method, path, query, headers,
					body);
			}
		}
	} else {
		_SendError(clientSocket, 400, "Bad Request");
	}
	shutdown(clientSocket, SHUT_RDWR);
	close(clientSocket);
}


bool
LsHttpServer::_ReadHeaders(int socket, std::string& method,
	std::string& pathAndQuery, std::map<std::string, std::string>& headers)
{
	std::string line;
	if (!_ReadLine(socket, line) || line.empty())
		return false;

	size_t firstSpace = line.find(' ');
	size_t secondSpace = line.find(' ', firstSpace + 1);
	if (firstSpace == std::string::npos || secondSpace == std::string::npos)
		return false;
	method = line.substr(0, firstSpace);
	pathAndQuery = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
	if (method.empty() || pathAndQuery.empty())
		return false;

	headers.clear();
	while (true) {
		if (!_ReadLine(socket, line))
			return false;
		if (line.empty())
			break;
		size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;
		std::string key = line.substr(0, colon);
		std::string value = line.substr(colon + 1);
		// Trim leading whitespace on the value.
		size_t trim = value.find_first_not_of(" \t");
		if (trim != std::string::npos)
			value = value.substr(trim);
		for (size_t i = 0; i < key.size(); i++) {
			if (key[i] >= 'A' && key[i] <= 'Z')
				key[i] += 'a' - 'A';
		}
		headers[key] = value;
	}
	return true;
}


bool
LsHttpServer::_ReadBody(int socket,
	const std::map<std::string, std::string>& headers, std::string& body)
{
	int64_t contentLength = 0;
	std::map<std::string, std::string>::const_iterator lengthIt
		= headers.find("content-length");
	if (lengthIt != headers.end())
		contentLength = strtoll(lengthIt->second.c_str(), NULL, 10);
	if (contentLength < 0 || contentLength > static_cast<int64_t>(kMaxJsonBody))
		return false;

	if (contentLength > 0) {
		body.resize(static_cast<size_t>(contentLength));
		if (!_ReadFully(socket, &body[0], static_cast<size_t>(contentLength)))
			return false;
	}
	return true;
}


void
LsHttpServer::_HandleRoute(int socket, const std::string& method,
	const std::string& path, const std::map<std::string, std::string>& query,
	const std::map<std::string, std::string>& headers,
	const std::string& body)
{
	std::string basePath = kApiBase;
	if (path.compare(0, basePath.size(), basePath) != 0) {
		_SendError(socket, 404, "Not Found");
		return;
	}
	std::string route = path.substr(basePath.size());

	LsDevice device;
	pthread_mutex_lock(&fDeviceLock);
	device = fDevice;
	pthread_mutex_unlock(&fDeviceLock);

	if (route == kEndpointRegister && method == "POST") {
		LsJson json;
		if (!LsJson::Parse(body, json)) {
			_SendError(socket, 400, "Bad Request");
			return;
		}
		LsDevice sender;
		if (!LsDevice::FromJson(json, sender)) {
			_SendError(socket, 400, "Bad Request");
			return;
		}
		if (fHandler != NULL) {
			std::string address = "unknown";
			fHandler->RegisterReceived(sender, address);
		}
		_SendResponse(socket, 200, "OK", device.ToResponseJson().Serialize());
		return;
	}

	if (route == kEndpointInfo && method == "GET") {
		_SendResponse(socket, 200, "OK", device.ToResponseJson().Serialize());
		return;
	}

	if (route == kEndpointPrepareUpload && method == "POST") {
		LsPrepareUploadRequest request;
		if (!LsPrepareUploadRequest::Parse(body, request)) {
			_SendError(socket, 400, "Bad Request");
			return;
		}

		std::string pin;
		std::map<std::string, std::string>::const_iterator pinIt
			= query.find("pin");
		if (pinIt != query.end())
			pin = pinIt->second;

		LsPrepareUploadResponse response;
		int status = 500;
		if (fHandler != NULL) {
			status = fHandler->PrepareUploadReceived(request.sender,
				request.files, pin, "unknown", response);
		}
		if (status == 200) {
			_SendResponse(socket, 200, "OK", response.ToJson().Serialize());
		} else {
			const char* text = "Internal Server Error";
			if (status == 401)
				text = "Unauthorized";
			else if (status == 403)
				text = "Forbidden";
			else if (status == 409)
				text = "Conflict";
			else if (status == 429)
				text = "Too Many Requests";
			_SendError(socket, status, text);
		}
		return;
	}

	if (route == kEndpointCancel && method == "POST") {
		std::string sessionId;
		std::map<std::string, std::string>::const_iterator sessionIt
			= query.find("sessionId");
		if (sessionIt != query.end())
			sessionId = sessionIt->second;
		if (sessionId.empty()) {
			_SendError(socket, 400, "Bad Request");
			return;
		}
		if (fHandler != NULL)
			fHandler->CancelReceived(sessionId);
		_SendResponse(socket, 200, "OK", "");
		return;
	}

	_SendError(socket, 404, "Not Found");
}


void
LsHttpServer::_HandleUpload(int socket,
	const std::map<std::string, std::string>& query,
	const std::map<std::string, std::string>& headers)
{
	std::map<std::string, std::string>::const_iterator sessionIt
		= query.find("sessionId");
	std::map<std::string, std::string>::const_iterator fileIt
		= query.find("fileId");
	std::map<std::string, std::string>::const_iterator tokenIt
		= query.find("token");
	if (sessionIt == query.end() || fileIt == query.end()
		|| tokenIt == query.end()) {
		_SendError(socket, 400, "Bad Request");
		return;
	}

	int64_t contentLength = -1;
	std::map<std::string, std::string>::const_iterator lengthIt
		= headers.find("content-length");
	if (lengthIt != headers.end())
		contentLength = strtoll(lengthIt->second.c_str(), NULL, 10);
	if (contentLength < 0) {
		_SendError(socket, 400, "Bad Request");
		return;
	}

	LsUploadSink* sink = NULL;
	if (fHandler != NULL) {
		sink = fHandler->StartUpload(sessionIt->second, fileIt->second,
			tokenIt->second, contentLength);
	}
	if (sink == NULL) {
		// Drain the body so the client sees a clean error response.
		char drain[4096];
		int64_t remaining = contentLength;
		while (remaining > 0) {
			size_t take = sizeof(drain);
			if (static_cast<int64_t>(take) > remaining)
				take = static_cast<size_t>(remaining);
			ssize_t n = recv(socket, drain, take, 0);
			if (n <= 0)
				break;
			remaining -= n;
		}
		_SendError(socket, 403, "Forbidden");
		return;
	}

	// Stream the body into the sink.
	bool ok = true;
	int64_t remaining = contentLength;
	char buffer[65536];
	while (remaining > 0) {
		size_t take = sizeof(buffer);
		if (static_cast<int64_t>(take) > remaining)
			take = static_cast<size_t>(remaining);
		if (!_ReadFully(socket, buffer, take)) {
			ok = false;
			break;
		}
		if (!sink->Write(buffer, take)) {
			ok = false;
			break;
		}
		remaining -= static_cast<int64_t>(take);
	}
	int status = sink->Finish(ok);
	delete sink;

	if (status == 200) {
		_SendResponse(socket, 200, "OK", "");
		return;
	}
	if (status == 422) {
		_SendError(socket, 422, "Unprocessable Entity");
		return;
	}
	_SendError(socket, 500, "Internal Server Error");
}


void
LsHttpServer::_SendResponse(int socket, int status, const char* statusText,
	const std::string& body, const char* contentType)
{
	std::string response = "HTTP/1.1 ";
	response += _DecimalString(status);
	response += " ";
	response += statusText;
	response += "\r\nContent-Type: ";
	response += contentType;
	response += "\r\nContent-Length: ";
	response += _DecimalString(static_cast<int64_t>(body.size()));
	response += "\r\nConnection: close\r\n\r\n";
	response += body;

	size_t total = 0;
	while (total < response.size()) {
		ssize_t n = send(socket, response.c_str() + total,
			response.size() - total, MSG_NOSIGNAL);
		if (n <= 0) {
			if (n < 0 && errno == EINTR)
				continue;
			break;
		}
		total += static_cast<size_t>(n);
	}
}


void
LsHttpServer::_SendError(int socket, int status, const char* statusText)
{
	_SendResponse(socket, status, statusText, "");
}

} // namespace LocalSend
