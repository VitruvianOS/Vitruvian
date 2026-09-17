// Unit tests for BString construction

#include <String.h>

#include <string.h>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <StringMakers.h>


TEST_CASE("BString: construction", "[BString][support]")
{
	const char* str = "Something";

	SECTION("BString()")
	{
		BString string;
		CHECK(std::string(string.String()) == "");
		CHECK(string.Length() == 0);
	}

	SECTION("BString(const char*)")
	{
		BString string(str);
		CHECK(std::string(string.String()) == str);
		CHECK((size_t)string.Length() == strlen(str));
	}

	SECTION("BString(NULL)")
	{
		BString string((const char*)NULL);
		CHECK(std::string(string.String()) == "");
		CHECK(string.Length() == 0);
	}

	SECTION("BString(const BString&)")
	{
		BString anotherString("Something Else");
		BString string(anotherString);
		CHECK(std::string(string.String()) == anotherString.String());
		CHECK(string.Length() == anotherString.Length());
	}

	SECTION("BString(const char*, int32)")
	{
		BString string(str, 5);
		CHECK(std::string(string.String()) != str);
		CHECK(std::string(string.String()) == std::string(str, 5));
		CHECK(string.Length() == 5);
	}

	SECTION("BString(const char*, int32) with length past the end")
	{
		BString string(str, 255);
		CHECK(std::string(string.String()) == str);
		CHECK((size_t)string.Length() == strlen(str));
	}
}
