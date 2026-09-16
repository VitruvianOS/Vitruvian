// Unit tests for BString assignment

#include <String.h>

#include <string>

#include <catch2/catch_test_macros.hpp>

#include <ScopedMemoryLimit.h>
#include <StringMakers.h>


static const int32 kOutOfMemoryLength = 2 * 1000 * 1000 * 1000;


TEST_CASE("BString: operator=", "[BString][support]")
{
	SECTION("BString")
	{
		BString string;
		BString string2("Something");
		string = string2;
		CHECK(std::string(string.String()) == string2.String());
		CHECK(std::string(string.String()) == "Something");
	}

	SECTION("const char*")
	{
		BString string;
		string = "Something Else";
		CHECK(std::string(string.String()) == "Something Else");
	}

	SECTION("NULL")
	{
		char* nullString = NULL;
		BString string;
		string = nullString;
		CHECK(std::string(string.String()) == "");
	}
}


TEST_CASE("BString: SetTo", "[BString][support]")
{
	SECTION("SetTo(const char*) with NULL")
	{
		char* nullString = NULL;
		BString string;
		string.SetTo(nullString);
		CHECK(std::string(string.String()) == "");
	}

	SECTION("SetTo(const char*)")
	{
		BString string;
		string.SetTo("BLA");
		CHECK(std::string(string.String()) == "BLA");
	}

	SECTION("SetTo(const BString&)")
	{
		BString source("Something");
		BString string;
		string.SetTo(source);
		CHECK(std::string(string.String()) == source.String());
	}

	SECTION("SetTo(char, int32)")
	{
		BString string;
		string.SetTo('C', 10);
		CHECK(std::string(string.String()) == "CCCCCCCCCC");
	}

	SECTION("SetTo(char, int32) with zero count")
	{
		BString string("ASDSGAFA");
		string.SetTo('C', 0);
		CHECK(std::string(string.String()) == "");
	}

	SECTION("SetTo(const char*, int32) with length past the end")
	{
		BString string;
		string.SetTo("ABC", 10);
		CHECK(std::string(string.String()) == "ABC");
	}

	SECTION("SetTo(char, int32) with excessive length leaves the string unchanged")
	{
		BString string("dummy");
		{
			ScopedMemoryLimit limit;
			string.SetTo('C', kOutOfMemoryLength);
		}
		CHECK(std::string(string.String()) == "dummy");
	}

	SECTION("SetTo(const char*, int32) with excessive length")
	{
		BString string("dummy");
		string.SetTo("some more text", kOutOfMemoryLength);
		CHECK(std::string(string.String()) == "some more text");
	}
}


TEST_CASE("BString: Adopt", "[BString][support]")
{
	SECTION("Adopt(BString&)")
	{
		BString source("Something");
		std::string oldSource(source.String());
		BString string;
		string.Adopt(source);
		CHECK(std::string(string.String()) == oldSource);
		CHECK(std::string(source.String()) == "");
	}

	SECTION("Adopt(BString&, int32)")
	{
		BString source("SomethingElseAgain");
		BString string;
		string.Adopt(source, 2);
		CHECK(std::string(string.String(), 2) == "So");
		CHECK(string.Length() == 2);
		CHECK(std::string(source.String()) == "");
	}
}
