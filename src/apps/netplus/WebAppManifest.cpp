/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */


#include "WebAppManifest.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>


// Minimal recursive-descent JSON reader: enough for web app
// manifests and the install envelope, nothing more.
namespace {


struct JsonValue {
	enum Type {
		kNull,
		kBool,
		kNumber,
		kString,
		kObject,
		kArray,
	};

			JsonValue()
				:
				type(kNull),
				boolValue(false),
				numberValue(0)
			{
			}

	Type					type;
	bool					boolValue;
	double					numberValue;
	std::string				stringValue;
	std::vector<std::pair<std::string, JsonValue> >	members;
	std::vector<JsonValue>	elements;

	const JsonValue*	Find(const char* key) const
	{
		for (size_t i = 0; i < members.size(); i++) {
			if (members[i].first == key)
				return &members[i].second;
		}
		return NULL;
	}
};


class Parser {
public:
			Parser(const char* text, size_t length)
				:
				fText(text),
				fLength(length),
				fPos(0)
			{
			}

	bool	Parse(JsonValue* into)
	{
		_SkipSpace();
		if (!_ParseValue(into))
			return false;
		_SkipSpace();
		return fPos == fLength;
	}

private:
	void	_SkipSpace()
	{
		while (fPos < fLength && isspace((unsigned char)fText[fPos]))
			fPos++;
	}

	bool	_Match(char c)
	{
		if (fPos < fLength && fText[fPos] == c) {
			fPos++;
			return true;
		}
		return false;
	}

	bool	_ParseValue(JsonValue* into)
	{
		if (fPos >= fLength)
			return false;

		char c = fText[fPos];
		if (c == '{')
			return _ParseObject(into);
		if (c == '[')
			return _ParseArray(into);
		if (c == '"') {
			into->type = JsonValue::kString;
			return _ParseString(&into->stringValue);
		}
		if (c == 't' || c == 'f')
			return _ParseBool(into);
		if (c == 'n')
			return _ParseNull(into);
		return _ParseNumber(into);
	}

	bool	_ParseObject(JsonValue* into)
	{
		if (!_Match('{'))
			return false;
		into->type = JsonValue::kObject;
		_SkipSpace();
		if (_Match('}'))
			return true;
		for (;;) {
			_SkipSpace();
			std::string key;
			if (!_ParseString(&key))
				return false;
			_SkipSpace();
			if (!_Match(':'))
				return false;
			_SkipSpace();
			JsonValue value;
			if (!_ParseValue(&value))
				return false;
			into->members.push_back(std::make_pair(key, value));
			_SkipSpace();
			if (_Match(','))
				continue;
			return _Match('}');
		}
	}

	bool	_ParseArray(JsonValue* into)
	{
		if (!_Match('['))
			return false;
		into->type = JsonValue::kArray;
		_SkipSpace();
		if (_Match(']'))
			return true;
		for (;;) {
			_SkipSpace();
			JsonValue value;
			if (!_ParseValue(&value))
				return false;
			into->elements.push_back(value);
			_SkipSpace();
			if (_Match(','))
				continue;
			return _Match(']');
		}
	}

	bool	_ParseString(std::string* into)
	{
		if (!_Match('"'))
			return false;
		while (fPos < fLength) {
			char c = fText[fPos++];
			if (c == '"')
				return true;
			if (c != '\\') {
				into->push_back(c);
				continue;
			}
			if (fPos >= fLength)
				return false;
			char esc = fText[fPos++];
			switch (esc) {
				case '"': into->push_back('"'); break;
				case '\\': into->push_back('\\'); break;
				case '/': into->push_back('/'); break;
				case 'b': into->push_back('\b'); break;
				case 'f': into->push_back('\f'); break;
				case 'n': into->push_back('\n'); break;
				case 'r': into->push_back('\r'); break;
				case 't': into->push_back('\t'); break;
				case 'u':
					if (!_ParseUnicode(into))
						return false;
					break;
				default:
					return false;
			}
		}
		return false;
	}

	bool	_ParseUnicode(std::string* into)
	{
		if (fPos + 4 > fLength)
			return false;
		unsigned int code = 0;
		for (int i = 0; i < 4; i++) {
			char c = fText[fPos++];
			code <<= 4;
			if (c >= '0' && c <= '9')
				code |= (unsigned int)(c - '0');
			else if (c >= 'a' && c <= 'f')
				code |= (unsigned int)(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F')
				code |= (unsigned int)(c - 'A' + 10);
			else
				return false;
		}
		if (code < 0x80) {
			into->push_back((char)code);
		} else if (code < 0x800) {
			into->push_back((char)(0xC0 | (code >> 6)));
			into->push_back((char)(0x80 | (code & 0x3F)));
		} else {
			into->push_back((char)(0xE0 | (code >> 12)));
			into->push_back((char)(0x80 | ((code >> 6) & 0x3F)));
			into->push_back((char)(0x80 | (code & 0x3F)));
		}
		return true;
	}

	bool	_ParseBool(JsonValue* into)
	{
		if (fPos + 4 <= fLength && strncmp(fText + fPos, "true", 4) == 0) {
			fPos += 4;
			into->type = JsonValue::kBool;
			into->boolValue = true;
			return true;
		}
		if (fPos + 5 <= fLength && strncmp(fText + fPos, "false", 5) == 0) {
			fPos += 5;
			into->type = JsonValue::kBool;
			return true;
		}
		return false;
	}

	bool	_ParseNull(JsonValue* into)
	{
		if (fPos + 4 <= fLength && strncmp(fText + fPos, "null", 4) == 0) {
			fPos += 4;
			return true;
		}
		return false;
	}

	bool	_ParseNumber(JsonValue* into)
	{
		size_t start = fPos;
		if (fPos < fLength && (fText[fPos] == '-' || fText[fPos] == '+'))
			fPos++;
		bool digits = false;
		while (fPos < fLength && isdigit((unsigned char)fText[fPos])) {
			fPos++;
			digits = true;
		}
		if (fPos < fLength && fText[fPos] == '.') {
			fPos++;
			while (fPos < fLength && isdigit((unsigned char)fText[fPos])) {
				fPos++;
				digits = true;
			}
		}
		if (!digits)
			return false;
		if (fPos < fLength && (fText[fPos] == 'e' || fText[fPos] == 'E')) {
			fPos++;
			if (fPos < fLength
				&& (fText[fPos] == '-' || fText[fPos] == '+'))
				fPos++;
			while (fPos < fLength && isdigit((unsigned char)fText[fPos]))
				fPos++;
		}
		into->type = JsonValue::kNumber;
		std::string number(fText + start, fPos - start);
		into->numberValue = atof(number.c_str());
		return true;
	}

	const char*		fText;
	size_t			fLength;
	size_t			fPos;
};


void
FillManifest(const JsonValue& root, WebAppManifest* into)
{
	const JsonValue* value = root.Find("name");
	if (value != NULL && value->type == JsonValue::kString)
		into->name = value->stringValue;

	value = root.Find("short_name");
	if (value != NULL && value->type == JsonValue::kString)
		into->shortName = value->stringValue;

	value = root.Find("start_url");
	if (value != NULL && value->type == JsonValue::kString)
		into->startUrl = value->stringValue;

	value = root.Find("display");
	if (value != NULL && value->type == JsonValue::kString)
		into->display = value->stringValue;

	value = root.Find("theme_color");
	if (value != NULL && value->type == JsonValue::kString)
		into->themeColor = value->stringValue;

	value = root.Find("background_color");
	if (value != NULL && value->type == JsonValue::kString)
		into->backgroundColor = value->stringValue;

	value = root.Find("icons");
	if (value == NULL || value->type != JsonValue::kArray)
		return;
	for (size_t i = 0; i < value->elements.size(); i++) {
		const JsonValue& entry = value->elements[i];
		if (entry.type != JsonValue::kObject)
			continue;
		const JsonValue* src = entry.Find("src");
		if (src == NULL || src->type != JsonValue::kString
			|| src->stringValue.empty())
			continue;
		ManifestIcon icon;
		icon.src = src->stringValue;
		const JsonValue* sizes = entry.Find("sizes");
		if (sizes != NULL && sizes->type == JsonValue::kString)
			icon.sizes = sizes->stringValue;
		const JsonValue* type = entry.Find("type");
		if (type != NULL && type->type == JsonValue::kString)
			icon.type = type->stringValue;
		const JsonValue* purpose = entry.Find("purpose");
		if (purpose != NULL && purpose->type == JsonValue::kString)
			icon.purpose = purpose->stringValue;
		into->icons.push_back(icon);
	}
}


int
HexDigit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}


bool
PurposeIsAny(const std::string& purpose)
{
	// "any maskable" and friends: the "any" token is what the
	// install icon wants; maskable-only stays a fallback.
	return purpose.find("any") != std::string::npos
		|| purpose.empty();
}


} // namespace


WebAppManifest::WebAppManifest()
	:
	fValid(false)
{
}


bool
WebAppManifest::Parse(const char* json, size_t length)
{
	fValid = false;
	name.clear();
	shortName.clear();
	startUrl.clear();
	display.clear();
	themeColor.clear();
	backgroundColor.clear();
	icons.clear();

	if (json == NULL || length == 0)
		return false;

	Parser parser(json, length);
	JsonValue root;
	if (!parser.Parse(&root) || root.type != JsonValue::kObject)
		return false;

	FillManifest(root, this);
	fValid = true;
	return true;
}


ManifestDisplayMode
ManifestDisplayModeFor(const std::string& display)
{
	if (display == "fullscreen")
		return kManifestDisplayFullscreen;
	if (display == "minimal-ui")
		return kManifestDisplayMinimalUi;
	if (display == "browser")
		return kManifestDisplayBrowser;
	// The spec default, and the fallback for unknown values.
	return kManifestDisplayStandalone;
}


int64_t
ManifestIconArea(const char* sizes)
{
	if (sizes == NULL)
		return 0;
	if (strcmp(sizes, "any") == 0)
		return INT64_MAX;

	int width = 0;
	int height = 0;
	if (sscanf(sizes, "%dx%d", &width, &height) != 2)
		return 0;
	if (width <= 0 || height <= 0)
		return 0;
	return (int64_t)width * (int64_t)height;
}


const ManifestIcon*
PickBestIcon(const WebAppManifest& manifest, int64_t maxArea)
{
	const ManifestIcon* best = NULL;
	int64_t bestArea = -1;
	const ManifestIcon* anySize = NULL;
	const ManifestIcon* fallback = NULL;
	int64_t fallbackArea = INT64_MAX;

	for (size_t i = 0; i < manifest.icons.size(); i++) {
		const ManifestIcon& icon = manifest.icons[i];
		if (icon.src.empty())
			continue;
		int64_t area = ManifestIconArea(icon.sizes.c_str());
		if (area <= 0)
			continue;

		if (!PurposeIsAny(icon.purpose)) {
			// Maskable-only: only if nothing else exists.
			if (fallback == NULL || area < fallbackArea) {
				fallback = &icon;
				fallbackArea = area;
			}
			continue;
		}

		if (area == INT64_MAX) {
			// "any" is a last resort among preferred icons.
			if (anySize == NULL)
				anySize = &icon;
			continue;
		}
		if (area > maxArea)
			continue;
		if (area > bestArea) {
			best = &icon;
			bestArea = area;
		}
	}

	if (best != NULL)
		return best;
	if (anySize != NULL)
		return anySize;
	return fallback;
}


int64_t
ParseManifestColor(const char* string)
{
	if (string == NULL || string[0] != '#')
		return -1;

	size_t length = strlen(string + 1);
	unsigned int rgb = 0;
	if (length == 3) {
		for (int i = 0; i < 3; i++) {
			int digit = HexDigit(string[1 + i]);
			if (digit < 0)
				return -1;
			rgb = (rgb << 8) | (unsigned int)((digit << 4) | digit);
		}
	} else if (length == 6 || length == 8) {
		for (size_t i = 0; i < length; i++) {
			int digit = HexDigit(string[1 + i]);
			if (digit < 0)
				return -1;
			if (i < 6)
				rgb = (rgb << 4) | (unsigned int)digit;
			// Alpha is accepted and dropped.
		}
	} else
		return -1;

	return (int64_t)rgb;
}


bool
ExtractJsonString(const char* json, size_t length, const char* key,
	std::string* into)
{
	if (json == NULL || key == NULL || into == NULL)
		return false;

	Parser parser(json, length);
	JsonValue root;
	if (!parser.Parse(&root) || root.type != JsonValue::kObject)
		return false;

	const JsonValue* value = root.Find(key);
	if (value == NULL || value->type != JsonValue::kString)
		return false;
	*into = value->stringValue;
	return true;
}


std::string
ResolveUrl(const std::string& base, const std::string& ref)
{
	if (ref.empty())
		return std::string();

	// scheme: anywhere before a '/', '?' or '#' is absolute.
	size_t colon = ref.find(':');
	size_t cut = ref.find_first_of("/?#");
	if (colon != std::string::npos
		&& (cut == std::string::npos || colon < cut))
		return ref;

	size_t schemeEnd = base.find("://");
	if (schemeEnd == std::string::npos)
		return ref;

	if (ref.size() >= 2 && ref[0] == '/' && ref[1] == '/')
		return base.substr(0, schemeEnd + 1) + ref;

	size_t authorityEnd = base.find('/', schemeEnd + 3);
	std::string origin = authorityEnd == std::string::npos
		? base : base.substr(0, authorityEnd);

	if (!ref.empty() && ref[0] == '/')
		return origin + ref;

	// Plain relative: drop the last path segment of base.
	std::string path = authorityEnd == std::string::npos
		? std::string("/") : base.substr(authorityEnd);
	size_t query = path.find_first_of("?#");
	if (query != std::string::npos)
		path.erase(query);
	size_t slash = path.rfind('/');
	if (slash == std::string::npos)
		return origin + "/" + ref;
	return origin + path.substr(0, slash + 1) + ref;
}


bool
Base64Decode(const char* string, size_t length, std::vector<uint8_t>* into)
{
	if (string == NULL || into == NULL)
		return false;

	static const char kAlphabet[]
		= "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	int decode[256];
	for (int i = 0; i < 256; i++)
		decode[i] = -1;
	for (int i = 0; i < 64; i++)
		decode[(unsigned char)kAlphabet[i]] = i;

	into->clear();
	unsigned int buffer = 0;
	int bits = 0;
	for (size_t i = 0; i < length; i++) {
		unsigned char c = (unsigned char)string[i];
		if (c == '=')
			break;
		if (c == '\n' || c == '\r')
			continue;
		int value = decode[c];
		if (value < 0)
			return false;
		buffer = (buffer << 6) | (unsigned int)value;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			into->push_back((uint8_t)((buffer >> bits) & 0xFF));
		}
	}
	return true;
}
