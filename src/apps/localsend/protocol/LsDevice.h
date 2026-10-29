/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef _LS_DEVICE_H
#define _LS_DEVICE_H

#include <stdint.h>

#include <map>
#include <string>

#include "LsJson.h"

namespace LocalSend {

// Device type values from protocol section 7.1. Unknown values fall back
// to kDesktop, matching the reference implementation.
enum LsDeviceType {
	kDeviceTypeMobile,
	kDeviceTypeDesktop,
	kDeviceTypeWeb,
	kDeviceTypeHeadless,
	kDeviceTypeServer
};


enum LsProtocolType {
	kProtocolHttp,
	kProtocolHttps
};


// A device as carried by announce, register and prepare-upload messages.
struct LsDevice {
	std::string		alias;
	std::string		version;
	std::string		deviceModel;	// optional
	LsDeviceType	deviceType;
	std::string		fingerprint;
	uint16_t		port;
	LsProtocolType	protocol;
	bool			download;		// download API active

						LsDevice();

	bool				operator==(const LsDevice& other) const;
	bool				operator!=(const LsDevice& other) const
								{ return !(*this == other); }

	// Full device JSON (announce, register, prepare-upload "info").
	LsJson				ToJson() const;

	// Register response / info: no port, no protocol.
	LsJson				ToResponseJson() const;

	// Parses the common device fields. Returns false when required
	// fields (alias, version) are missing or malformed.
	static bool			FromJson(const LsJson& json, LsDevice& result);

	static const char*	DeviceTypeName(LsDeviceType type);
	static LsDeviceType	DeviceTypeFromName(const std::string& name);
	static const char*	ProtocolTypeName(LsProtocolType type);
	static LsProtocolType	ProtocolTypeFromName(const std::string& name);
};


// Announce message: device fields plus the "announce" flag.
struct LsAnnounceMessage {
	LsDevice		device;
	bool			announce;

						LsAnnounceMessage();

	LsJson				ToJson() const;
	static bool			FromJson(const LsJson& json, LsAnnounceMessage& result);
};


// Per-file metadata in a prepare-upload request.
struct LsFileItem {
	std::string		id;
	std::string		fileName;
	int64_t			size;
	std::string		fileType;		// MIME type
	std::string		sha256;			// optional, lowercase hex
	std::string		preview;		// optional, base64
	std::string		modified;		// optional, RFC 3339
	std::string		accessed;		// optional, RFC 3339

						LsFileItem();

	LsJson				ToJson() const;
	static bool			FromJson(const LsJson& json, LsFileItem& result);
};


struct LsPrepareUploadRequest {
	LsDevice						sender;
	// keyed by file id
	std::map<std::string, LsFileItem>	files;

						LsPrepareUploadRequest();

	LsJson				ToJson() const;
	static bool			Parse(const std::string& body,
								LsPrepareUploadRequest& result);
};


struct LsPrepareUploadResponse {
	std::string								sessionId;
	// file id -> upload token
	std::map<std::string, std::string>		files;

						LsPrepareUploadResponse();

	LsJson				ToJson() const;
	static bool			Parse(const std::string& body,
								LsPrepareUploadResponse& result);
};

} // namespace LocalSend

#endif // _LS_DEVICE_H
