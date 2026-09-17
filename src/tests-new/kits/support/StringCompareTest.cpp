// Unit tests for BString comparison

#include <String.h>

#include <catch2/catch_test_macros.hpp>

#include <StringMakers.h>


TEST_CASE("BString: compare with BString", "[BString][support]")
{
	SECTION("operator<")
	{
		CHECK(BString("11111_a") < BString("22222_b"));
	}

	SECTION("operator<=")
	{
		CHECK(BString("11111_a") <= BString("22222_b"));
		CHECK(BString("11111") <= BString("11111"));
	}

	SECTION("operator==")
	{
		CHECK(BString("string") == BString("string"));
		CHECK_FALSE(BString("text") == BString("string"));
	}

	SECTION("operator>=")
	{
		CHECK(BString("BBBBB") >= BString("AAAAA"));
		CHECK(BString("11111") >= BString("11111"));
	}

	SECTION("operator>")
	{
		CHECK(BString("BBBBB") > BString("AAAAA"));
	}

	SECTION("operator!=")
	{
		CHECK_FALSE(BString("string") != BString("string"));
		CHECK(BString("text") != BString("string"));
	}
}


TEST_CASE("BString: compare with const char*", "[BString][support]")
{
	SECTION("operator<")
	{
		BString string("AAAAA");
		CHECK(string < "BBBBB");
	}

	SECTION("operator<=")
	{
		BString string("AAAAA");
		CHECK(string <= "BBBBB");
		CHECK(string <= "AAAAA");
	}

	SECTION("operator==")
	{
		BString string("AAAAA");
		CHECK(string == "AAAAA");
		CHECK_FALSE(string == "BBBB");
	}

	SECTION("operator>=")
	{
		BString string("BBBBB");
		CHECK(string >= "AAAAA");
		CHECK(string >= "BBBBB");
	}

	SECTION("operator>")
	{
		BString string("BBBBB");
		CHECK(string > "AAAAA");
	}

	SECTION("operator!=")
	{
		BString string("AAAAA");
		CHECK_FALSE(string != "AAAAA");
		CHECK(string != "BBBB");
	}
}
