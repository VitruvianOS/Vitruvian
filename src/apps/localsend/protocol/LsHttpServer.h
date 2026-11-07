/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef _LS_HTTP_SERVER_H
#define _LS_HTTP_SERVER_H

#include <pthread.h>
#include <stdint.h>

#include <map>
#include <string>

#include "LsDevice.h"

namespace LocalSend {

// Sink for one upload's binary body. The server feeds chunks as they
// arrive; Finish gets the outcome (true = body fully received) and
// returns the HTTP status to answer with (200 ok, 422 checksum
// mismatch, 500 write failure).
class LsUploadSink {
public:
	virtual					~LsUploadSink() {}

	virtual	bool			Write(const void* data, size_t length) = 0;
	virtual	int				Finish(bool success) = 0;
};


// Application callbacks for the embedded server. All are invoked on a
// server worker thread; the implementation marshals to its own loop.
class LsHttpServerHandler {
public:
	virtual					~LsHttpServerHandler() {}

	// POST /register: a peer introduced itself. No response body
	// needed beyond our own info (filled by the server).
	virtual	void			RegisterReceived(const LsDevice& device,
									const std::string& address) = 0;

	// POST /prepare-upload: return 200 and fill |response| to accept,
	// or an error status (401 pin, 403 rejected, 409 busy, ...).
	// |pin| is the query parameter, empty when absent.
	virtual	int				PrepareUploadReceived(
									const LsDevice& sender,
									const std::map<std::string, LsFileItem>&
										files,
									const std::string& pin,
									const std::string& address,
									LsPrepareUploadResponse& response) = 0;

	// POST /upload: called once per file. Return NULL to reject the
	// token (403); otherwise the server streams the body into the
	// sink and the sink's outcome decides 200 vs 422/500.
	virtual	LsUploadSink*	StartUpload(const std::string& sessionId,
									const std::string& fileId,
									const std::string& token,
									int64_t size) = 0;

	// POST /cancel
	virtual	void			CancelReceived(const std::string& sessionId) = 0;
};


// Minimal HTTP/1.1 server exposing the LocalSend v2 REST endpoints.
// Plain POSIX sockets and threads so it also runs in host loopback
// tests. One upload session at a time is enforced by the handler, not
// the server.
class LsHttpServer {
public:
							LsHttpServer();
							~LsHttpServer();

			int				Init(uint16_t port);
			int				Start(LsHttpServerHandler* handler,
									const LsDevice& device);
			void			Stop();

			uint16_t		Port() const { return fPort; }
			bool			IsRunning() const { return fRunning; }

			// Our device info; updated when settings change.
			void			SetDevice(const LsDevice& device);

private:
			static	void*	_AcceptThreadEntry(void* data);
			static	void*	_ConnectionThreadEntry(void* data);
			void			_AcceptLoop();
			void			_HandleConnection(int clientSocket);
			bool			_ReadHeaders(int socket, std::string& method,
									std::string& pathAndQuery,
									std::map<std::string, std::string>& headers);
			bool			_ReadBody(int socket,
									const std::map<std::string, std::string>&
										headers,
									std::string& body);
			void			_HandleRoute(int socket,
									const std::string& method,
									const std::string& path,
									const std::map<std::string, std::string>&
										query,
									const std::map<std::string, std::string>&
										headers,
									const std::string& body,
									const std::string& address);
			void			_HandleUpload(int socket,
									const std::map<std::string, std::string>&
										query,
									const std::map<std::string, std::string>&
										headers,
									const std::string& address);
			void			_SendResponse(int socket, int status,
									const char* statusText,
									const std::string& body,
									const char* contentType
										= "application/json");
			void			_SendError(int socket, int status,
									const char* statusText);

			int				fListenSocket;
			uint16_t		fPort;
			pthread_t		fAcceptThread;
			volatile bool	fRunning;
			bool			fThreadStarted;
			LsHttpServerHandler* fHandler;
			pthread_mutex_t	fDeviceLock;
			LsDevice		fDevice;
};

} // namespace LocalSend

#endif // _LS_HTTP_SERVER_H
