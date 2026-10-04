/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _HOSTSHIMS_STRING_H
#define _HOSTSHIMS_STRING_H

#include <SupportDefs.h>
#include <string>

class BString {
public:
	BString() {}
	BString(const char* string) : fString(string != NULL ? string : "") {}
	BString(const BString& other) : fString(other.fString) {}

	BString& operator=(const BString& other)
	{
		fString = other.fString;
		return *this;
	}

	BString& operator=(const char* string)
	{
		fString = string != NULL ? string : "";
		return *this;
	}

	const char* String() const { return fString.c_str(); }
	operator const char*() const { return fString.c_str(); }
	bool IsEmpty() const { return fString.empty(); }
	int32 Length() const { return (int32)fString.size(); }

	uint32 HashValue() const
	{
		uint32 hash = 5381;
		for (size_t i = 0; i < fString.size(); i++)
			hash = hash * 33 + (uint8)fString[i];
		return hash;
	}

	bool operator==(const BString& other) const
	{
		return fString == other.fString;
	}

	bool operator==(const char* string) const
	{
		return fString == (string != NULL ? string : "");
	}

	bool operator<(const BString& other) const
	{
		return fString < other.fString;
	}

private:
	std::string fString;
};

#endif
