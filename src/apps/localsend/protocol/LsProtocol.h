/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef _LS_PROTOCOL_H
#define _LS_PROTOCOL_H

// LocalSend protocol v2.2 constants (github.com/localsend/protocol).
// This is the real wire configuration, not a V\OS-only variant.

namespace LocalSend {

static const unsigned short kDefaultPort = 53317;
static const char* const kDefaultMulticastGroup = "224.0.0.167";
static const char* const kApiBase = "/api/localsend/v2";
static const char* const kProtocolVersion = "2.2";

// Endpoints under kApiBase.
static const char* const kEndpointRegister = "/register";
static const char* const kEndpointPrepareUpload = "/prepare-upload";
static const char* const kEndpointUpload = "/upload";
static const char* const kEndpointCancel = "/cancel";
static const char* const kEndpointInfo = "/info";

static const char* const kUserAgent = "LocalSend/1.0 (V\\OS)";

} // namespace LocalSend

#endif // _LS_PROTOCOL_H
