// Unit tests for BString access

#include <String.h>
#include <UTF8.h>

#include <string.h>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <StringMakers.h>


TEST_CASE("BString: CountChars and Length", "[BString][support]")
{
	SECTION("UTF-8 string")
	{
		BString string("Something" B_UTF8_ELLIPSIS);
		CHECK(string.CountChars() == 10);
		CHECK((size_t)string.Length() == strlen(string.String()));
	}

	SECTION("ASCII string")
	{
		BString string("ABCD");
		CHECK(string.CountChars() == 4);
		CHECK((size_t)string.Length() == strlen(string.String()));
	}

	SECTION("only multi-byte characters")
	{
		char s[64];
		strcpy(s, B_UTF8_ELLIPSIS);
		strcat(s, B_UTF8_SMILING_FACE);
		BString string(s);
		CHECK(string.CountChars() == 2);
		CHECK((size_t)string.Length() == strlen(string.String()));
	}

	SECTION("empty string")
	{
		BString empty;
		CHECK(std::string(empty.String()) == "");
		CHECK(empty.Length() == 0);
		CHECK(empty.CountChars() == 0);
	}

	SECTION("truncated in the middle of a UTF-8 character")
	{
		BString invalid("some text with utf8 characters" B_UTF8_ELLIPSIS);
		invalid.Truncate(invalid.Length() - 1);
		CHECK(invalid.CountChars() == 31);
	}
}


TEST_CASE("BString: LockBuffer and UnlockBuffer", "[BString][support]")
{
	SECTION("grow the buffer and append")
	{
		BString locked("a string");
		char* ptr = locked.LockBuffer(20);
		CHECK(std::string(ptr) == "a string");
		strcat(ptr, " to be locked");
		locked.UnlockBuffer();
		CHECK(std::string(locked.String()) == "a string to be locked");
	}

	SECTION("unlock with a shorter length")
	{
		BString locked("some text");
		char* ptr = locked.LockBuffer(3);
		CHECK(std::string(ptr) == "some text");
		locked.UnlockBuffer(4);
		CHECK(std::string(locked.String()) == "some");
		CHECK(locked.Length() == 4);
	}

	SECTION("empty string")
	{
		BString locked;
		char* ptr = locked.LockBuffer(10);
		CHECK(std::string(ptr) == "");
		strcat(ptr, "pippo");
		locked.UnlockBuffer();
		CHECK(std::string(locked.String()) == "pippo");
	}

	SECTION("LockBuffer(0) and UnlockBuffer(-1) on an empty string")
	{
		BString locked;
		locked.LockBuffer(0);
		locked.UnlockBuffer(-1);
		CHECK(std::string(locked.String()) == "");
	}
}
