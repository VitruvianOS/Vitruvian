/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEB_MAIL_STRINGS_H
#define WEB_MAIL_STRINGS_H


#include <String.h>

#include <cstdlib>


// The in-tree BString predates Mid()/Left()/Integer(); these helpers
// sit on CopyInto() so the app code stays readable.
inline BString
StringMid(const BString& string, int32 from, int32 length = -1)
{
	BString out;
	if (from < 0 || from >= string.Length())
		return out;
	if (length < 0 || from + length > string.Length())
		length = string.Length() - from;
	if (length <= 0)
		return out;
	string.CopyInto(out, from, length);
	return out;
}


inline BString
StringLeft(const BString& string, int32 length)
{
	return StringMid(string, 0, length);
}


inline int32
StringInteger(const BString& string)
{
	return (int32)strtol(string.String(), NULL, 10);
}


#endif // WEB_MAIL_STRINGS_H
