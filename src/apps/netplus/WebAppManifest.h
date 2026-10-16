/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef _WEB_APP_MANIFEST_H
#define _WEB_APP_MANIFEST_H


#include <stdint.h>
#include <string>
#include <vector>


struct ManifestIcon {
	std::string			src;
	std::string			sizes;
	std::string			type;
	std::string			purpose;
};


/*!	Parsed web app manifest (W3C). Plain C++ so the host tests cover
	it without WebKit or BViews; NetPlus feeds it the JSON that the
	in-page fetch posted back.
*/
class WebAppManifest {
public:
			WebAppManifest();

			bool			Parse(const char* json, size_t length);

			bool			IsValid() const { return fValid; }

			std::string		name;
			std::string		shortName;
			std::string		startUrl;
			std::string		display;
			std::string		themeColor;
			std::string		backgroundColor;

			std::vector<ManifestIcon>	icons;

private:
			bool			fValid;
};


// Display mode mapped to what the window actually needs to keep.
enum ManifestDisplayMode {
	kManifestDisplayBrowser,
	kManifestDisplayStandalone,
	kManifestDisplayMinimalUi,
	kManifestDisplayFullscreen,
};


ManifestDisplayMode	ManifestDisplayModeFor(const std::string& display);

// Best icon for an installed-app record: largest area at or below
// maxArea, purpose "any" preferred over "maskable only". Returns NULL
// when the manifest carries no usable icon.
const ManifestIcon*	PickBestIcon(const WebAppManifest& manifest,
							int64_t maxArea);

// Pixel area of a "WxH" sizes entry; 0 when unparseable, and the
// sizes string "any" yields INT64_MAX (Chrome treats it as "take me").
int64_t				ManifestIconArea(const char* sizes);

// "#rgb", "#rrggbb" or "#rrggbbaa" into 0xRRGGBB; -1 on bad input.
int64_t				ParseManifestColor(const char* string);

// Pull one top-level string member out of a JSON object.
bool				ExtractJsonString(const char* json, size_t length,
							const char* key, std::string* into);

// Resolve ref against base per RFC 3986 (absolute, scheme-relative,
// root-relative and plain relative refs).
std::string			ResolveUrl(const std::string& base,
							const std::string& ref);

// Raw base64 (no newlines) into bytes; false on bad padding.
bool				Base64Decode(const char* string, size_t length,
							std::vector<uint8_t>* into);


#endif // _WEB_APP_MANIFEST_H
