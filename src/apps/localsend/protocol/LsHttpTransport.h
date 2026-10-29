/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef _LS_HTTP_TRANSPORT_H
#define _LS_HTTP_TRANSPORT_H

#include <stdint.h>

#include <string>

namespace LocalSend {

// Outcome of one HTTP exchange.
struct LsHttpResult {
	int			status;			// HTTP status, or -1 on transport error
	std::string	body;			// response body (JSON routes)

					LsHttpResult() : status(-1) {}
};


// Blocking HTTP/1.1 client over POSIX sockets. This is the transport
// the session logic and the host loopback tests use; the V\OS app
// additionally drives sends through BUrlRequest, but the session
// state machine itself is transport-independent.
class LsHttpTransport {
public:
	virtual					~LsHttpTransport() {}

	// POST |path| (may include ?query) with a JSON body.
	virtual	LsHttpResult	PostJson(const std::string& address,
									uint16_t port,
									const std::string& path,
									const std::string& jsonBody) = 0;

	// POST |path| with a raw binary body (file upload).
	virtual	LsHttpResult	PostFile(const std::string& address,
									uint16_t port,
									const std::string& path,
									const void* data, size_t length) = 0;
};


// Plain-socket implementation used by host tests and as the generic
// fallback when BUrlRequest is not available.
class LsSocketTransport : public LsHttpTransport {
public:
	virtual	LsHttpResult	PostJson(const std::string& address,
									uint16_t port,
									const std::string& path,
									const std::string& jsonBody);
	virtual	LsHttpResult	PostFile(const std::string& address,
									uint16_t port,
									const std::string& path,
									const void* data, size_t length);

private:
			LsHttpResult	_Post(const std::string& address,
									uint16_t port,
									const std::string& path,
									const char* contentType,
									const void* body, size_t bodyLength);
};

} // namespace LocalSend

#endif // _LS_HTTP_TRANSPORT_H
