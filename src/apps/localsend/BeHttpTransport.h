/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef _BE_HTTP_TRANSPORT_H
#define _BE_HTTP_TRANSPORT_H

#include "LsHttpTransport.h"

namespace LocalSend {

// HTTP transport backed by BUrlRequest/BUrlProtocolRoster, as the
// app-level client path. The protocol-layer socket transport remains
// the host-test reference; this adapter gives the V\OS app the kit's
// HTTP stack.
class BeHttpTransport : public LsHttpTransport {
public:
	virtual	LsHttpResult	PostJson(const std::string& address,
									uint16_t port,
									const std::string& path,
									const std::string& jsonBody);
	virtual	LsHttpResult	PostFile(const std::string& address,
									uint16_t port,
									const std::string& path,
									const void* data, size_t length);
	virtual	LsHttpResult	PostFileFromPath(const std::string& address,
									uint16_t port,
									const std::string& path,
									const std::string& filePath,
									int64_t fileLength);

private:
			LsHttpResult	_RunRequest(const std::string& url,
									const char* method,
									const void* body, size_t bodyLength,
									const char* contentType);
};

} // namespace LocalSend

#endif // _BE_HTTP_TRANSPORT_H
