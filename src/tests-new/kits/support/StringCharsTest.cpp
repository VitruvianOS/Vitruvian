// Unit tests for BString's character based API
//
// These methods count in characters rather than bytes, so the interesting part
// is that they keep multi-byte UTF-8 characters intact. Every check states
// both lengths: the byte length and the character count.
//
// The operations run one after another on the same string, as in the old
// string_utf8_tests program.

#include <InterfaceDefs.h>
#include <String.h>

#include <string.h>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <StringMakers.h>


static void
CheckString(const BString& string, const char* expected, int32 bytes,
	int32 chars)
{
	INFO("expected \"" << expected << "\"");
	CHECK(std::string(string.String(), string.Length())
		== std::string(expected, bytes));
	CHECK(string.Length() == bytes);
	CHECK(string.CountChars() == chars);
}


TEST_CASE("BString: character based editing", "[BString][support]")
{
	BString string("ü-ä-ö");
	CheckString(string, "ü-ä-ö", 8, 5);

	// Replacing two of the characters with an ellipsis makes the string
	// longer in bytes but not in characters
	string.ReplaceCharsSet("üö", B_UTF8_ELLIPSIS);
	CheckString(string, B_UTF8_ELLIPSIS "-ä-" B_UTF8_ELLIPSIS, 10, 5);

	BString ellipsis;
	string.MoveCharsInto(ellipsis, 4, 1);
	CheckString(string, B_UTF8_ELLIPSIS "-ä-", 7, 4);
	CheckString(ellipsis, B_UTF8_ELLIPSIS, 3, 1);

	string.RemoveCharsSet("-" B_UTF8_ELLIPSIS);
	CheckString(string, "ä", 2, 1);

	// Only the first 5 characters of the 7 given
	string.SetToChars("öäü" B_UTF8_ELLIPSIS "öäü", 5);
	CheckString(string, "öäü" B_UTF8_ELLIPSIS "ö", 11, 5);

	string.TruncateChars(4);
	CheckString(string, "öäü" B_UTF8_ELLIPSIS, 9, 4);

	string.AppendChars("öäü", 2);
	CheckString(string, "öäü" B_UTF8_ELLIPSIS "öä", 13, 6);

	string.RemoveChars(1, 3);
	CheckString(string, "ööä", 6, 3);

	string.InsertChars("öäü" B_UTF8_ELLIPSIS B_UTF8_ELLIPSIS "ä", 3, 2, 1);
	CheckString(string, "ö" B_UTF8_ELLIPSIS B_UTF8_ELLIPSIS "öä", 12, 5);

	string.PrependChars("ää+üü", 3);
	CheckString(string, "ää+ö" B_UTF8_ELLIPSIS B_UTF8_ELLIPSIS "öä", 17, 8);
}


TEST_CASE("BString: character based comparison", "[BString][support]")
{
	const BString string("ää+ö" B_UTF8_ELLIPSIS B_UTF8_ELLIPSIS "öä");
	const char* other = "ää+ö" B_UTF8_ELLIPSIS "different";

	// The strings share their first five characters
	CHECK(string.CompareChars(other, 5) == 0);
	CHECK(string.CompareChars(other, 6) != 0);

	// Three characters from character 2 on, all of them two bytes
	CHECK(string.CountBytes(2, 3) == 6);
}
