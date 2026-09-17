// Unit tests for BString escaping

#include <String.h>

#include <string>

#include <catch2/catch_test_macros.hpp>

#include <StringMakers.h>


TEST_CASE("BString: CharacterEscape", "[BString][support]")
{
	SECTION("CharacterEscape(const char*, char)")
	{
		BString string("abcdefghi");
		string.CharacterEscape("acf", '/');
		CHECK(std::string(string.String()) == "/ab/cde/fghi");
	}

	SECTION("CharacterEscape(const char*, char) on an empty string")
	{
		BString string;
		string.CharacterEscape("abc", '/');
		CHECK(std::string(string.String()) == "");
	}

	SECTION("CharacterEscape(const char*, char) with no matching characters")
	{
		BString string("abcdefghi");
		string.CharacterEscape("z34", 'z');
		CHECK(std::string(string.String()) == "abcdefghi");
	}

	SECTION("CharacterEscape(const char*, const char*, char)")
	{
		BString string("something");
		string.CharacterEscape("newstring", "esi", '0');
		CHECK(std::string(string.String()) == "n0ew0str0ing");
	}

	SECTION("CharacterEscape(const char*, const char*, char) with NULL source")
	{
		BString string("something");
		string.CharacterEscape((char*)NULL, "ei", '-');
		CHECK(std::string(string.String()) == "");
	}

	SECTION("CharacterEscape(const char*, const char*, char) on an empty string")
	{
		BString string;
		string.CharacterEscape("newstring", "esi", '0');
		CHECK(std::string(string.String()) == "n0ew0str0ing");
	}
}


TEST_CASE("BString: CharacterDeescape", "[BString][support]")
{
	SECTION("CharacterDeescape(char)")
	{
		BString string("/a/nh/g/bhhgy/fgtuhjkb/");
		string.CharacterDeescape('/');
		CHECK(std::string(string.String()) == "anhgbhhgyfgtuhjkb");
	}

	SECTION("CharacterDeescape(char) on an empty string")
	{
		BString string;
		string.CharacterDeescape('/');
		CHECK(std::string(string.String()) == "");
	}

	SECTION("CharacterDeescape(char) with no matching characters")
	{
		BString string("/a/nh/g/bhhgy/fgtuhjkb/");
		string.CharacterDeescape('-');
		CHECK(std::string(string.String()) == "/a/nh/g/bhhgy/fgtuhjkb/");
	}

	SECTION("CharacterDeescape(const char*, char)")
	{
		BString string("oldString");
		string.CharacterDeescape("-ne-ws-tri-ng-", '-');
		CHECK(std::string(string.String()) == "newstring");
	}

	SECTION("CharacterDeescape(const char*, char) on an empty string")
	{
		BString string;
		string.CharacterDeescape("new/str/ing", '/');
		CHECK(std::string(string.String()) == "newstring");
	}

	SECTION("CharacterDeescape(const char*, char) with NULL source")
	{
		BString string("pippo");
		string.CharacterDeescape((char*)NULL, '/');
		CHECK(std::string(string.String()) == "");
	}

	SECTION("CharacterDeescape(const char*, char) with no matching characters")
	{
		BString string("Old");
		string.CharacterDeescape("/a/nh/g/bhhgy/fgtuhjkb/", '-');
		CHECK(std::string(string.String()) == "/a/nh/g/bhhgy/fgtuhjkb/");
	}
}
