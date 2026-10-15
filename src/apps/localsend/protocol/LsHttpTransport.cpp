/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "LsHttpTransport.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "LsProtocol.h"

namespace LocalSend {

static const int kConnectTimeoutMs = 5000;


LsHttpResult
LsHttpTransport::PostFileFromPath(const std::string& address, uint16_t port,
	const std::string& path, const std::string& filePath, int64_t fileLength)
{
	FILE* file = fopen(filePath.c_str(), "rb");
	if (file == NULL)
		return LsHttpResult();

	std::string data;
	char buffer[65536];
	while (true) {
		size_t n = fread(buffer, 1, sizeof(buffer), file);
		if (n == 0)
			break;
		data.append(buffer, n);
	}
	fclose(file);

	if (fileLength > 0 && static_cast<int64_t>(data.size()) != fileLength)
		return LsHttpResult();

	return PostFile(address, port, path, data.data(), data.size());
}


LsHttpResult
LsSocketTransport::PostJson(const std::string& address, uint16_t port,
	const std::string& path, const std::string& jsonBody)
{
	return _Post(address, port, path, "application/json", jsonBody.c_str(),
		jsonBody.size());
}
LsHttpResult
LsSocketTransport::PostFile(const std::string& address, uint16_t port,
	const std::string& path, const void* data, size_t length)
{
	return _Post(address, port, path, "application/octet-stream", data,
		length);
}


LsHttpResult
LsSocketTransport::_Post(const std::string& address, uint16_t port,
	const std::string& path, const char* contentType, const void* body,
	size_t bodyLength)
{
	LsHttpResult result;

	int socket = ::socket(AF_INET, SOCK_STREAM, 0);
	if (socket < 0)
		return result;

	struct sockaddr_in target;
	memset(&target, 0, sizeof(target));
	target.sin_family = AF_INET;
	target.sin_port = htons(port);
	if (inet_pton(AF_INET, address.c_str(), &target.sin_addr) != 1) {
		// Not a literal address; resolve it.
		struct addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		struct addrinfo* resolved = NULL;
		char portText[8];
		snprintf(portText, sizeof(portText), "%u", port);
		if (getaddrinfo(address.c_str(), portText, &hints, &resolved) != 0
			|| resolved == NULL) {
			close(socket);
			return result;
		}
		memcpy(&target, resolved->ai_addr, sizeof(target));
		freeaddrinfo(resolved);
	}

	// A short connect timeout keeps dead peers from stalling sends.
	struct timeval timeout;
	timeout.tv_sec = kConnectTimeoutMs / 1000;
	timeout.tv_usec = (kConnectTimeoutMs % 1000) * 1000;
	setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
	setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

	if (connect(socket, reinterpret_cast<struct sockaddr*>(&target),
			sizeof(target)) != 0) {
		close(socket);
		return result;
	}

	std::string request = "POST ";
	request += path;
	request += " HTTP/1.1\r\nHost: ";
	request += address;
	request += "\r\nUser-Agent: ";
	request += kUserAgent;
	request += "\r\nContent-Type: ";
	request += contentType;
	request += "\r\nContent-Length: ";
	char lengthText[24];
	snprintf(lengthText, sizeof(lengthText), "%zu", bodyLength);
	request += lengthText;
	request += "\r\nConnection: close\r\n\r\n";

	size_t total = 0;
	while (total < request.size()) {
		ssize_t n = send(socket, request.c_str() + total,
			request.size() - total, MSG_NOSIGNAL);
		if (n <= 0) {
			if (n < 0 && errno == EINTR)
				continue;
			close(socket);
			return result;
		}
		total += static_cast<size_t>(n);
	}

	total = 0;
	while (total < bodyLength) {
		ssize_t n = send(socket, static_cast<const char*>(body) + total,
			bodyLength - total, MSG_NOSIGNAL);
		if (n <= 0) {
			if (n < 0 && errno == EINTR)
				continue;
			close(socket);
			return result;
		}
		total += static_cast<size_t>(n);
	}

	// Read the full response; the server closes after replying.
	std::string response;
	char buffer[8192];
	while (true) {
		ssize_t n = recv(socket, buffer, sizeof(buffer), 0);
		if (n <= 0) {
			if (n < 0 && errno == EINTR)
				continue;
			break;
		}
		response.append(buffer, static_cast<size_t>(n));
	}
	close(socket);

	size_t headerEnd = response.find("\r\n\r\n");
	if (headerEnd == std::string::npos)
		return result;

	int status = 0;
	if (sscanf(response.c_str(), "HTTP/%*d.%*d %d", &status) != 1)
		return result;
	result.status = status;
	result.body = response.substr(headerEnd + 4);
	return result;
}

} // namespace LocalSend
