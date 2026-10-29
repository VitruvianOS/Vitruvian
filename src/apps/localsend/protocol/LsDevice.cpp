/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "LsDevice.h"

namespace LocalSend {

LsDevice::LsDevice()
	:
	version("2.2"),
	deviceType(kDeviceTypeDesktop),
	port(53317),
	protocol(kProtocolHttp),
	download(false)
{
}


bool
LsDevice::operator==(const LsDevice& other) const
{
	return alias == other.alias
		&& version == other.version
		&& deviceModel == other.deviceModel
		&& deviceType == other.deviceType
		&& fingerprint == other.fingerprint
		&& port == other.port
		&& protocol == other.protocol
		&& download == other.download;
}


LsJson
LsDevice::ToJson() const
{
	LsJson json = LsJson::Object();
	json.Set("alias", LsJson(alias));
	json.Set("version", LsJson(version));
	if (!deviceModel.empty())
		json.Set("deviceModel", LsJson(deviceModel));
	json.Set("deviceType", LsJson(DeviceTypeName(deviceType)));
	json.Set("fingerprint", LsJson(fingerprint));
	json.Set("port", LsJson(static_cast<int>(port)));
	json.Set("protocol", LsJson(ProtocolTypeName(protocol)));
	json.Set("download", LsJson(download));
	return json;
}


LsJson
LsDevice::ToResponseJson() const
{
	LsJson json = LsJson::Object();
	json.Set("alias", LsJson(alias));
	json.Set("version", LsJson(version));
	if (!deviceModel.empty())
		json.Set("deviceModel", LsJson(deviceModel));
	json.Set("deviceType", LsJson(DeviceTypeName(deviceType)));
	json.Set("fingerprint", LsJson(fingerprint));
	json.Set("download", LsJson(download));
	return json;
}


/* static */ bool
LsDevice::FromJson(const LsJson& json, LsDevice& result)
{
	if (!json.IsObject())
		return false;
	if (!json.Has("alias") || !json.Has("version"))
		return false;

	result.alias = json["alias"].ToString();
	result.version = json["version"].ToString();
	if (result.alias.empty() || result.version.empty())
		return false;

	result.deviceModel = json["deviceModel"].ToString();
	result.deviceType = DeviceTypeFromName(
		json["deviceType"].ToString("desktop"));
	result.fingerprint = json["fingerprint"].ToString();
	result.port = static_cast<uint16_t>(json["port"].ToInt64(53317));
	result.protocol = ProtocolTypeFromName(
		json["protocol"].ToString("https"));
	result.download = json["download"].ToBool(false);
	return true;
}


/* static */ const char*
LsDevice::DeviceTypeName(LsDeviceType type)
{
	switch (type) {
		case kDeviceTypeMobile:
			return "mobile";
		case kDeviceTypeDesktop:
			return "desktop";
		case kDeviceTypeWeb:
			return "web";
		case kDeviceTypeHeadless:
			return "headless";
		case kDeviceTypeServer:
			return "server";
	}
	return "desktop";
}


/* static */ LsDeviceType
LsDevice::DeviceTypeFromName(const std::string& name)
{
	if (name == "mobile")
		return kDeviceTypeMobile;
	if (name == "web")
		return kDeviceTypeWeb;
	if (name == "headless")
		return kDeviceTypeHeadless;
	if (name == "server")
		return kDeviceTypeServer;
	// Unknown values fall back to desktop (protocol section 7.1).
	return kDeviceTypeDesktop;
}


/* static */ const char*
LsDevice::ProtocolTypeName(LsProtocolType type)
{
	return type == kProtocolHttps ? "https" : "http";
}


/* static */ LsProtocolType
LsDevice::ProtocolTypeFromName(const std::string& name)
{
	if (name == "http")
		return kProtocolHttp;
	// The reference defaults to https on anything unrecognized.
	return kProtocolHttps;
}


LsAnnounceMessage::LsAnnounceMessage()
	:
	announce(true)
{
}


LsJson
LsAnnounceMessage::ToJson() const
{
	LsJson json = device.ToJson();
	json.Set("announce", LsJson(announce));
	return json;
}


/* static */ bool
LsAnnounceMessage::FromJson(const LsJson& json, LsAnnounceMessage& result)
{
	if (!LsDevice::FromJson(json, result.device))
		return false;
	result.announce = json["announce"].ToBool(true);
	return true;
}


LsFileItem::LsFileItem()
	:
	size(0)
{
}


LsJson
LsFileItem::ToJson() const
{
	LsJson json = LsJson::Object();
	json.Set("id", LsJson(id));
	json.Set("fileName", LsJson(fileName));
	json.Set("size", LsJson(size));
	json.Set("fileType", LsJson(fileType));
	if (!sha256.empty())
		json.Set("sha256", LsJson(sha256));
	if (!preview.empty())
		json.Set("preview", LsJson(preview));
	if (!modified.empty() || !accessed.empty()) {
		LsJson metadata = LsJson::Object();
		if (!modified.empty())
			metadata.Set("modified", LsJson(modified));
		if (!accessed.empty())
			metadata.Set("accessed", LsJson(accessed));
		json.Set("metadata", metadata);
	}
	return json;
}


/* static */ bool
LsFileItem::FromJson(const LsJson& json, LsFileItem& result)
{
	if (!json.IsObject())
		return false;
	if (!json.Has("id") || !json.Has("fileName") || !json.Has("size"))
		return false;

	result.id = json["id"].ToString();
	result.fileName = json["fileName"].ToString();
	result.size = json["size"].ToInt64(0);
	if (result.id.empty() || result.fileName.empty() || result.size < 0)
		return false;

	result.fileType = json["fileType"].ToString("application/octet-stream");
	result.sha256 = json["sha256"].ToString();
	result.preview = json["preview"].ToString();

	const LsJson& metadata = json["metadata"];
	if (metadata.IsObject()) {
		result.modified = metadata["modified"].ToString();
		result.accessed = metadata["accessed"].ToString();
	}
	return true;
}


LsPrepareUploadRequest::LsPrepareUploadRequest()
{
}


LsJson
LsPrepareUploadRequest::ToJson() const
{
	LsJson json = LsJson::Object();
	json.Set("info", sender.ToJson());

	LsJson filesJson = LsJson::Object();
	for (std::map<std::string, LsFileItem>::const_iterator it = files.begin();
		it != files.end(); it++) {
		filesJson.Set(it->first, it->second.ToJson());
	}
	json.Set("files", filesJson);
	return json;
}


/* static */ bool
LsPrepareUploadRequest::Parse(const std::string& body,
	LsPrepareUploadRequest& result)
{
	LsJson json;
	if (!LsJson::Parse(body, json) || !json.IsObject())
		return false;
	if (!LsDevice::FromJson(json["info"], result.sender))
		return false;

	const LsJson& filesJson = json["files"];
	if (!filesJson.IsObject())
		return false;

	result.files.clear();
	const LsJsonObject& objects = filesJson.Objects();
	for (LsJsonObject::const_iterator it = objects.begin();
		it != objects.end(); it++) {
		LsFileItem file;
		if (!LsFileItem::FromJson(it->second, file))
			return false;
		// The map key and the "id" field must agree.
		if (file.id != it->first)
			return false;
		result.files[it->first] = file;
	}
	return true;
}


LsPrepareUploadResponse::LsPrepareUploadResponse()
{
}


LsJson
LsPrepareUploadResponse::ToJson() const
{
	LsJson json = LsJson::Object();
	json.Set("sessionId", LsJson(sessionId));

	LsJson filesJson = LsJson::Object();
	for (std::map<std::string, std::string>::const_iterator it = files.begin();
		it != files.end(); it++) {
		filesJson.Set(it->first, LsJson(it->second));
	}
	json.Set("files", filesJson);
	return json;
}


/* static */ bool
LsPrepareUploadResponse::Parse(const std::string& body,
	LsPrepareUploadResponse& result)
{
	LsJson json;
	if (!LsJson::Parse(body, json) || !json.IsObject())
		return false;

	result.sessionId = json["sessionId"].ToString();
	if (result.sessionId.empty())
		return false;

	const LsJson& filesJson = json["files"];
	if (!filesJson.IsObject())
		return false;

	result.files.clear();
	const LsJsonObject& objects = filesJson.Objects();
	for (LsJsonObject::const_iterator it = objects.begin();
		it != objects.end(); it++) {
		std::string token = it->second.ToString();
		if (token.empty())
			return false;
		result.files[it->first] = token;
	}
	return true;
}

} // namespace LocalSend
