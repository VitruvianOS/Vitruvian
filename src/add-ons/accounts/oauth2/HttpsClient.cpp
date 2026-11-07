/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "HttpsClient.h"

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <openssl/ssl.h>


static status_t
_ParseUrl(const char* url, BString& host, int& port, BString& path)
{
	const char* prefix = "https://";
	if (strncmp(url, prefix, strlen(prefix)) != 0)
		return B_BAD_VALUE;

	const char* cursor = url + strlen(prefix);
	const char* end = cursor;
	while (*end != '\0' && *end != '/' && *end != ':')
		end++;

	if (end == cursor)
		return B_BAD_VALUE;

	host.SetTo(cursor, end - cursor);

	port = 443;
	if (*end == ':') {
		port = atoi(end + 1);
		end++;
		while (*end >= '0' && *end <= '9')
			end++;
	}

	path = *end == '/' ? BString(end) : BString("/");
	return B_OK;
}


static int
_Connect(const char* host, int port)
{
	char service[16];
	snprintf(service, sizeof(service), "%d", port);

	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	struct addrinfo* addresses = NULL;
	if (getaddrinfo(host, service, &hints, &addresses) != 0)
		return -1;

	int socket = -1;
	for (struct addrinfo* address = addresses; address != NULL;
		address = address->ai_next) {
		socket = ::socket(address->ai_family, address->ai_socktype,
			address->ai_protocol);
		if (socket < 0)
			continue;

		struct timeval timeout;
		timeout.tv_sec = 30;
		timeout.tv_usec = 0;
		setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout,
			sizeof(timeout));
		setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout,
			sizeof(timeout));

		if (connect(socket, address->ai_addr, address->ai_addrlen) == 0)
			break;

		close(socket);
		socket = -1;
	}

	freeaddrinfo(addresses);
	return socket;
}


status_t
HttpsPost(const char* url, const BString& body, BString& response)
{
	if (url == NULL)
		return B_BAD_VALUE;

	BString host;
	BString path;
	int port = 443;
	status_t status = _ParseUrl(url, host, port, path);
	if (status != B_OK)
		return status;

	int socket = _Connect(host.String(), port);
	if (socket < 0)
		return B_ERROR;

	SSL_CTX* context = SSL_CTX_new(TLS_client_method());
	if (context == NULL) {
		close(socket);
		return B_NO_MEMORY;
	}

	SSL* ssl = SSL_new(context);
	if (ssl == NULL) {
		SSL_CTX_free(context);
		close(socket);
		return B_NO_MEMORY;
	}

	SSL_set_fd(ssl, socket);
	SSL_set_tlsext_host_name(ssl, host.String());
	SSL_set1_host(ssl, host.String());

	if (SSL_connect(ssl) != 1) {
		SSL_free(ssl);
		SSL_CTX_free(context);
		close(socket);
		return B_ERROR;
	}

	BString request("POST ");
	request << path << " HTTP/1.1\r\n";
	request << "Host: " << host << "\r\n";
	request << "User-Agent: VOS-Accounts/1.0\r\n";
	request << "Content-Type: application/x-www-form-urlencoded\r\n";
	request << "Content-Length: " << (int32)body.Length() << "\r\n";
	request << "Connection: close\r\n\r\n";
	request << body;

	if (SSL_write(ssl, request.String(), request.Length())
			!= (int)request.Length()) {
		SSL_free(ssl);
		SSL_CTX_free(context);
		close(socket);
		return B_ERROR;
	}

	char buffer[4096];
	ssize_t bytes = 0;
	while ((bytes = SSL_read(ssl, buffer, sizeof(buffer) - 1)) > 0) {
		buffer[bytes] = '\0';
		response.Append(buffer, bytes);
	}

	SSL_free(ssl);
	SSL_CTX_free(context);
	close(socket);

	int32 statuscode = 0;
	if (sscanf(response.String(), "HTTP/%*d.%*d %d", &statuscode) != 1)
		return B_ERROR;

	if (statuscode < 200 || statuscode > 299)
		return B_ERROR;

	return B_OK;
}


status_t
UrlEncode(const BString& value, BString& encoded)
{
	static const char* hex = "0123456789ABCDEF";

	encoded = "";
	const char* bytes = value.String();
	for (int32 i = 0; i < value.Length(); i++) {
		char c = bytes[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '-' || c == '_'
			|| c == '.' || c == '~') {
			encoded << c;
		} else {
			encoded << "%" << hex[(c >> 4) & 0x0f] << hex[c & 0x0f];
		}
	}

	return B_OK;
}
