// Unit tests for BString case conversion

#include <String.h>

#include <string>

#include <catch2/catch_test_macros.hpp>

#include <StringMakers.h>


TEST_CASE("BString: Capitalize", "[BString][support]")
{
	SECTION("sentence")
	{
		BString string("this is a sentence");
		string.Capitalize();
		CHECK(std::string(string.String()) == "This is a sentence");
	}

	SECTION("leading digits")
	{
		BString string("134this is a sentence");
		string.Capitalize();
		CHECK(std::string(string.String()) == "134this is a sentence");
	}

	SECTION("empty string")
	{
		BString string;
		string.Capitalize();
		CHECK(std::string(string.String()) == "");
	}
}


TEST_CASE("BString: ToLower", "[BString][support]")
{
	SECTION("mixed case")
	{
		BString string("1a2B3c4d5e6f7G");
		string.ToLower();
		CHECK(std::string(string.String()) == "1a2b3c4d5e6f7g");
	}

	SECTION("empty string")
	{
		BString string;
		string.ToLower();
		CHECK(std::string(string.String()) == "");
	}
}


TEST_CASE("BString: ToUpper", "[BString][support]")
{
	SECTION("mixed case")
	{
		BString string("1a2b3c4d5E6f7g");
		string.ToUpper();
		CHECK(std::string(string.String()) == "1A2B3C4D5E6F7G");
	}

	SECTION("empty string")
	{
		BString string;
		string.ToUpper();
		CHECK(std::string(string.String()) == "");
	}
}


TEST_CASE("BString: CapitalizeEachWord", "[BString][support]")
{
	SECTION("words with digits and punctuation")
	{
		BString string("each wOrd 3will_be >capiTalized");
		string.CapitalizeEachWord();
		CHECK(std::string(string.String()) == "Each Word 3Will_Be >Capitalized");
	}

	SECTION("empty string")
	{
		BString string;
		string.CapitalizeEachWord();
		CHECK(std::string(string.String()) == "");
	}
}
