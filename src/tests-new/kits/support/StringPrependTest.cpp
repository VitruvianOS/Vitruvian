// Unit tests for BString prepending

#include <String.h>

#include <string>

#include <catch2/catch_test_macros.hpp>

#include <StringMakers.h>


TEST_CASE("BString: Prepend", "[BString][support]")
{
	SECTION("Prepend(const BString&)")
	{
		BString string("a String");
		BString prefix("PREPENDED");
		string.Prepend(prefix);
		CHECK(std::string(string.String()) == "PREPENDEDa String");
	}

	SECTION("Prepend(const char*)")
	{
		BString string("String");
		string.Prepend("PREPEND");
		CHECK(std::string(string.String()) == "PREPENDString");
	}

	SECTION("Prepend(const char*) with NULL")
	{
		BString string("String");
		string.Prepend((char*)NULL);
		CHECK(std::string(string.String()) == "String");
	}

	SECTION("Prepend(const char*, int32)")
	{
		BString string("String");
		string.Prepend("PREPENDED", 3);
		CHECK(std::string(string.String()) == "PREString");
	}

	SECTION("Prepend(const BString&) of a length-limited string")
	{
		BString string("String");
		BString prefix("PREPEND", 4);
		string.Prepend(prefix);
		CHECK(std::string(string.String()) == "PREPString");
	}

	SECTION("Prepend(char, int32)")
	{
		BString string("aString");
		string.Prepend('c', 4);
		CHECK(std::string(string.String()) == "ccccaString");
	}

	SECTION("Prepend(const char*) to an empty string")
	{
		BString string;
		string.Prepend("PREPENDED");
		CHECK(std::string(string.String()) == "PREPENDED");
	}
}
