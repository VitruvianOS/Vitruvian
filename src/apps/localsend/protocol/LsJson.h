/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef _LS_JSON_H
#define _LS_JSON_H

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

namespace LocalSend {

// Minimal JSON DOM for the LocalSend wire format. Deliberately free of
// Be API so the protocol layer host-builds and unit-tests unmodified.

class LsJson;
typedef std::vector<LsJson> LsJsonArray;
typedef std::map<std::string, LsJson> LsJsonObject;

class LsJson {
public:
	enum Type {
		kNull,
		kBool,
		kNumber,
		kString,
		kArray,
		kObject
	};

							LsJson();
							LsJson(bool value);
							LsJson(int value);
							LsJson(int64_t value);
							LsJson(double value);
							LsJson(const char* value);
							LsJson(const std::string& value);

	static	LsJson			Array();
	static	LsJson			Object();

			Type			 GetType() const { return fType; }
			bool			IsNullOrUndefined() const
								{ return fType == kNull; }
			bool			IsBool() const { return fType == kBool; }
			bool			IsNumber() const { return fType == kNumber; }
			bool			IsString() const { return fType == kString; }
			bool			IsArray() const { return fType == kArray; }
			bool			IsObject() const { return fType == kObject; }

			bool			ToBool(bool defaultValue = false) const;
			int64_t			ToInt64(int64_t defaultValue = 0) const;
			double			ToDouble(double defaultValue = 0.0) const;
			const std::string&	ToString() const;
			std::string		ToString(const std::string& defaultValue) const;

			// Object access. Missing key returns a shared null value.
			const LsJson&	operator[](const std::string& key) const;
			bool			Has(const std::string& key) const;
			void			Set(const std::string& key, const LsJson& value);
			const LsJsonObject&	Objects() const { return fObject; }

			// Array access.
			size_t			Count() const;
			const LsJson&	At(size_t index) const;
			void			Append(const LsJson& value);
			const LsJsonArray&	Elements() const { return fArray; }

			std::string		Serialize() const;

	static	bool			Parse(const std::string& text, LsJson& result);

private:
			void			_SerializeTo(std::string& out) const;

			Type			fType;
			bool			fBool;
			double			fNumber;
			std::string		fString;
			LsJsonArray		fArray;
			LsJsonObject	fObject;

	static	const LsJson&	_Null();
};

} // namespace LocalSend

#endif // _LS_JSON_H
