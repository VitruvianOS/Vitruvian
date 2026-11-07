/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "LsJson.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace LocalSend {

// Parser state, file-local.
struct JsonParseContext {
	const char* text;
	size_t length;
	size_t position;
};


static bool
_Peek(const JsonParseContext& context, char* c)
{
	if (context.position >= context.length)
		return false;
	*c = context.text[context.position];
	return true;
}


static void
_SkipWhitespace(JsonParseContext& context)
{
	char c;
	while (_Peek(context, &c)) {
		if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
			break;
		context.position++;
	}
}


static bool
_ParseValue(JsonParseContext& context, LsJson& result);


static bool
_ParseString(JsonParseContext& context, std::string& out)
{
	char c;
	if (!_Peek(context, &c) || c != '"')
		return false;
	context.position++;

	out.clear();
	while (_Peek(context, &c)) {
		context.position++;
		if (c == '"')
			return true;
		if (c == '\\') {
			char escape;
			if (!_Peek(context, &escape))
				return false;
			context.position++;
			switch (escape) {
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u':
				{
					// Keep it simple: decode the BMP code point only.
					unsigned int codePoint = 0;
					for (int i = 0; i < 4; i++) {
						char h;
						if (!_Peek(context, &h))
							return false;
						context.position++;
						codePoint <<= 4;
						if (h >= '0' && h <= '9')
							codePoint += h - '0';
						else if (h >= 'a' && h <= 'f')
							codePoint += h - 'a' + 10;
						else if (h >= 'A' && h <= 'F')
							codePoint += h - 'A' + 10;
						else
							return false;
					}
					if (codePoint < 0x80) {
						out += static_cast<char>(codePoint);
					} else if (codePoint < 0x800) {
						out += static_cast<char>(0xc0 | (codePoint >> 6));
						out += static_cast<char>(0x80 | (codePoint & 0x3f));
					} else {
						out += static_cast<char>(0xe0 | (codePoint >> 12));
						out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f));
						out += static_cast<char>(0x80 | (codePoint & 0x3f));
					}
					break;
				}
				default:
					return false;
			}
		} else {
			out += c;
		}
	}
	return false;
}


static bool
_ParseObject(JsonParseContext& context, LsJson& result)
{
	result = LsJson::Object();
	context.position++; // '{'

	_SkipWhitespace(context);
	char c;
	if (_Peek(context, &c) && c == '}') {
		context.position++;
		return true;
	}

	while (true) {
		_SkipWhitespace(context);
		std::string key;
		if (!_ParseString(context, key))
			return false;
		_SkipWhitespace(context);
		if (!_Peek(context, &c) || c != ':')
			return false;
		context.position++;

		LsJson value;
		if (!_ParseValue(context, value))
			return false;
		result.Set(key, value);

		_SkipWhitespace(context);
		if (!_Peek(context, &c))
			return false;
		context.position++;
		if (c == '}')
			return true;
		if (c != ',')
			return false;
	}
}


static bool
_ParseArray(JsonParseContext& context, LsJson& result)
{
	result = LsJson::Array();
	context.position++; // '['

	_SkipWhitespace(context);
	char c;
	if (_Peek(context, &c) && c == ']') {
		context.position++;
		return true;
	}

	while (true) {
		LsJson value;
		if (!_ParseValue(context, value))
			return false;
		result.Append(value);

		_SkipWhitespace(context);
		if (!_Peek(context, &c))
			return false;
		context.position++;
		if (c == ']')
			return true;
		if (c != ',')
			return false;
	}
}


static bool
_MatchVerbatim(JsonParseContext& context, const char* word)
{
	size_t length = strlen(word);
	if (context.position + length > context.length)
		return false;
	if (strncmp(context.text + context.position, word, length) != 0)
		return false;
	context.position += length;
	return true;
}


static bool
_ParseValue(JsonParseContext& context, LsJson& result)
{
	_SkipWhitespace(context);
	char c;
	if (!_Peek(context, &c))
		return false;

	switch (c) {
		case '{':
			return _ParseObject(context, result);
		case '[':
			return _ParseArray(context, result);
		case '"':
		{
			std::string text;
			if (!_ParseString(context, text))
				return false;
			result = LsJson(text);
			return true;
		}
		case 't':
			if (!_MatchVerbatim(context, "true"))
				return false;
			result = LsJson(true);
			return true;
		case 'f':
			if (!_MatchVerbatim(context, "false"))
				return false;
			result = LsJson(false);
			return true;
		case 'n':
			if (!_MatchVerbatim(context, "null"))
				return false;
			result = LsJson();
			return true;
		default:
		{
			// Scan the numeric extent manually; strtod stops at structurals.
			size_t start = context.position;
			const char* text = context.text;
			size_t length = context.length;
			size_t i = start;
			if (i < length && (text[i] == '-' || text[i] == '+'))
				i++;
			bool digits = false;
			while (i < length && text[i] >= '0' && text[i] <= '9') {
				i++;
				digits = true;
			}
			if (i < length && text[i] == '.') {
				i++;
				while (i < length && text[i] >= '0' && text[i] <= '9') {
					i++;
					digits = true;
				}
			}
			if (i < length && (text[i] == 'e' || text[i] == 'E')) {
				i++;
				if (i < length && (text[i] == '-' || text[i] == '+'))
					i++;
				while (i < length && text[i] >= '0' && text[i] <= '9')
					i++;
			}
			if (!digits)
				return false;
			result = LsJson(strtod(text + start, NULL));
			context.position = i;
			return true;
		}
	}
}


static void
_EscapeString(const std::string& text, std::string& out)
{
	out += '"';
	for (size_t i = 0; i < text.size(); i++) {
		char c = text[i];
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (static_cast<unsigned char>(c) < 0x20) {
					char buffer[8];
					snprintf(buffer, sizeof(buffer), "\\u%04x",
						static_cast<unsigned char>(c));
					out += buffer;
				} else {
					out += c;
				}
				break;
		}
	}
	out += '"';
}


LsJson::LsJson()
	:
	fType(kNull),
	fBool(false),
	fNumber(0.0)
{
}


LsJson::LsJson(bool value)
	:
	fType(kBool),
	fBool(value),
	fNumber(0.0)
{
}


LsJson::LsJson(int value)
	:
	fType(kNumber),
	fBool(false),
	fNumber(value)
{
}


LsJson::LsJson(int64_t value)
	:
	fType(kNumber),
	fBool(false),
	fNumber(static_cast<double>(value))
{
}


LsJson::LsJson(double value)
	:
	fType(kNumber),
	fBool(false),
	fNumber(value)
{
}


LsJson::LsJson(const char* value)
	:
	fType(value == NULL ? kNull : kString),
	fBool(false),
	fNumber(0.0),
	fString(value == NULL ? "" : value)
{
}


LsJson::LsJson(const std::string& value)
	:
	fType(kString),
	fBool(false),
	fNumber(0.0),
	fString(value)
{
}


/* static */ LsJson
LsJson::Array()
{
	LsJson json;
	json.fType = kArray;
	return json;
}


/* static */ LsJson
LsJson::Object()
{
	LsJson json;
	json.fType = kObject;
	return json;
}


bool
LsJson::ToBool(bool defaultValue) const
{
	if (fType == kBool)
		return fBool;
	return defaultValue;
}


int64_t
LsJson::ToInt64(int64_t defaultValue) const
{
	if (fType == kNumber)
		return static_cast<int64_t>(fNumber);
	return defaultValue;
}


double
LsJson::ToDouble(double defaultValue) const
{
	if (fType == kNumber)
		return fNumber;
	return defaultValue;
}


const std::string&
LsJson::ToString() const
{
	static const std::string empty;
	if (fType == kString)
		return fString;
	return empty;
}


std::string
LsJson::ToString(const std::string& defaultValue) const
{
	if (fType == kString)
		return fString;
	return defaultValue;
}


const LsJson&
LsJson::operator[](const std::string& key) const
{
	if (fType == kObject) {
		LsJsonObject::const_iterator it = fObject.find(key);
		if (it != fObject.end())
			return it->second;
	}
	return _Null();
}


bool
LsJson::Has(const std::string& key) const
{
	return fType == kObject && fObject.find(key) != fObject.end();
}


void
LsJson::Set(const std::string& key, const LsJson& value)
{
	fType = kObject;
	fObject[key] = value;
}


size_t
LsJson::Count() const
{
	if (fType == kArray)
		return fArray.size();
	if (fType == kObject)
		return fObject.size();
	return 0;
}


const LsJson&
LsJson::At(size_t index) const
{
	if (fType == kArray && index < fArray.size())
		return fArray[index];
	return _Null();
}


void
LsJson::Append(const LsJson& value)
{
	fType = kArray;
	fArray.push_back(value);
}


std::string
LsJson::Serialize() const
{
	std::string out;
	_SerializeTo(out);
	return out;
}


/* static */ bool
LsJson::Parse(const std::string& text, LsJson& result)
{
	JsonParseContext context;
	context.text = text.c_str();
	context.length = text.size();
	context.position = 0;

	if (!_ParseValue(context, result))
		return false;
	_SkipWhitespace(context);
	return context.position == context.length;
}


/* static */ const LsJson&
LsJson::_Null()
{
	static const LsJson null;
	return null;
}


void
LsJson::_SerializeTo(std::string& out) const
{
	switch (fType) {
		case kNull:
			out += "null";
			break;
		case kBool:
			out += fBool ? "true" : "false";
			break;
		case kNumber:
		{
			// Integers stay integral on the wire.
			double integer;
			if (modf(fNumber, &integer) == 0.0
				&& fNumber >= -9.0e15 && fNumber <= 9.0e15) {
				char buffer[32];
				snprintf(buffer, sizeof(buffer), "%lld",
					static_cast<long long>(fNumber));
				out += buffer;
			} else {
				char buffer[40];
				snprintf(buffer, sizeof(buffer), "%.17g", fNumber);
				out += buffer;
			}
			break;
		}
		case kString:
			_EscapeString(fString, out);
			break;
		case kArray:
		{
			out += '[';
			for (size_t i = 0; i < fArray.size(); i++) {
				if (i > 0)
					out += ',';
				fArray[i]._SerializeTo(out);
			}
			out += ']';
			break;
		}
		case kObject:
		{
			out += '{';
			bool first = true;
			for (LsJsonObject::const_iterator it = fObject.begin();
				it != fObject.end(); it++) {
				if (!first)
					out += ',';
				first = false;
				_EscapeString(it->first, out);
				out += ':';
				it->second._SerializeTo(out);
			}
			out += '}';
			break;
		}
	}
}

} // namespace LocalSend
