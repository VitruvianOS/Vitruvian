// Unit tests for BString appending

#include <String.h>

#include <string.h>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <ScopedMemoryLimit.h>
#include <StringMakers.h>


TEST_CASE("BString: operator+=", "[BString][support]")
{
	SECTION("BString")
	{
		BString string("BASE");
		string += BString("APPENDED");
		CHECK(std::string(string.String()) == "BASEAPPENDED");
	}

	SECTION("const char*")
	{
		BString string("Base");
		string += "APPENDED";
		CHECK(std::string(string.String()) == "BaseAPPENDED");
	}

	SECTION("const char* to an empty string")
	{
		BString string;
		string += "APPENDEDTONOTHING";
		CHECK(std::string(string.String()) == "APPENDEDTONOTHING");
	}

	SECTION("NULL")
	{
		char* null = NULL;
		BString string("Base");
		string += null;
		CHECK(std::string(string.String()) == "Base");
	}

	SECTION("char")
	{
		BString string("Base");
		string += 'C';
		CHECK(std::string(string.String()) == "BaseC");
	}
}


TEST_CASE("BString: Append", "[BString][support]")
{
	const int32 kOutOfMemoryLength = 2 * 1000 * 1000 * 1000;
	char* null = NULL;

	SECTION("Append(BString)")
	{
		BString string("BASE");
		string.Append(BString("APPENDED"));
		CHECK(std::string(string.String()) == "BASEAPPENDED");
	}

	SECTION("Append(const char*)")
	{
		BString string("Base");
		string.Append("APPENDED");
		CHECK(std::string(string.String()) == "BaseAPPENDED");
	}

	SECTION("Append(const char*) to an empty string")
	{
		BString string;
		string.Append("APPENDEDTONOTHING");
		CHECK(std::string(string.String()) == "APPENDEDTONOTHING");
	}

	SECTION("Append(NULL)")
	{
		BString string("Base");
		string.Append(null);
		CHECK(std::string(string.String()) == "Base");
	}

	SECTION("Append(BString, length)")
	{
		BString string("BASE");
		string.Append(BString("APPENDED"), 2);
		CHECK(std::string(string.String()) == "BASEAP");
	}

	SECTION("Append(const char*, length) with length beyond the end")
	{
		BString string("Base");
		string.Append("APPENDED", 40);
		CHECK(std::string(string.String()) == "BaseAPPENDED");
		CHECK(string.Length() == (int32)strlen("BaseAPPENDED"));
	}

	SECTION("Append(NULL, length)")
	{
		BString string("BLABLA");
		string.Append(null, 2);
		CHECK(std::string(string.String()) == "BLABLA");
	}

	SECTION("Append(char, count)")
	{
		BString string("Base");
		string.Append('C', 5);
		CHECK(std::string(string.String()) == "BaseCCCCC");
	}

	SECTION("Append(char, count) with excessive count")
	{
		BString string("Base");
		{
			ScopedMemoryLimit limit;
			string.Append('C', kOutOfMemoryLength);
		}
		CHECK(std::string(string.String()) == "Base");
	}

	SECTION("Append(const char*, length) with excessive length")
	{
		BString string("Base");
		string.Append("some more text", kOutOfMemoryLength);
		CHECK(std::string(string.String()) == "Basesome more text");
	}
}
