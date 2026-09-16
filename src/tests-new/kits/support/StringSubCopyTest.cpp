// Unit tests for BString sub-string copying

#include <String.h>

#include <string.h>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <StringMakers.h>


TEST_CASE("BString: CopyInto", "[BString][support]")
{
	SECTION("CopyInto(BString&, int32, int32)")
	{
		BString destination;
		BString string("Something");
		string.CopyInto(destination, 4, 30);
		CHECK(std::string(destination.String()) == "thing");
	}

	SECTION("CopyInto(char*, int32, int32)")
	{
		char buffer[10];
		memset(buffer, 0, 10);
		BString string("ABC");
		string.CopyInto(buffer, 0, 4);
		CHECK(std::string(buffer) == "ABC");
		CHECK(std::string(string.String()) == "ABC");
	}
}
