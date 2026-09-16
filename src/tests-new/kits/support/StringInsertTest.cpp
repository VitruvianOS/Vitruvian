// Unit tests for BString insertion

#include <String.h>

#include <string>

#include <catch2/catch_test_macros.hpp>

#include <StringMakers.h>


TEST_CASE("BString: Insert(const char*)", "[BString][support]")
{
	SECTION("Insert(const char*, pos)")
	{
		BString string("String");
		string.Insert("INSERTED", 3);
		CHECK(std::string(string.String()) == "StrINSERTEDing");
	}

	SECTION("Insert(const char*, pos) with pos beyond the end")
	{
		BString string("String");
		string.Insert("INSERTED", 10);
		CHECK(std::string(string.String()) == "String");
	}

	SECTION("Insert(const char*, pos) with negative pos")
	{
		BString string;
		string.Insert("INSERTED", -1);
		CHECK(std::string(string.String()) == "NSERTED");
	}

	SECTION("Insert(const char*, pos) with pos below -length")
	{
		BString string;
		string.Insert("INSERTED", -142364253);
		CHECK(std::string(string.String()) == "");
	}

	SECTION("Insert(const char*, length, pos)")
	{
		BString string("string");
		string.Insert("INSERTED", 2, 2);
		CHECK(std::string(string.String()) == "stINring");
	}

	SECTION("Insert(const char*, length, pos) with pos beyond the end")
	{
		BString string("string");
		string.Insert("INSERTED", 2, 30);
		CHECK(std::string(string.String()) == "string");
	}

	SECTION("Insert(const char*, length, pos) with length beyond the end")
	{
		BString string("string");
		string.Insert("INSERTED", 10, 2);
		CHECK(std::string(string.String()) == "stINSERTEDring");
	}

	SECTION("Insert(const char*, fromOffset, length, pos)")
	{
		BString string("string");
		string.Insert("INSERTED", 4, 30, 2);
		CHECK(std::string(string.String()) == "stRTEDring");
	}
}


TEST_CASE("BString: Insert(char)", "[BString][support]")
{
	SECTION("Insert(char, count, pos)")
	{
		BString string("string");
		string.Insert('P', 5, 3);
		CHECK(std::string(string.String()) == "strPPPPPing");
	}

	SECTION("Insert(char, count, pos) with negative pos")
	{
		BString string("string");
		string.Insert('P', 5, -2);
		CHECK(std::string(string.String()) == "PPPstring");
	}
}


TEST_CASE("BString: Insert(BString)", "[BString][support]")
{
	SECTION("Insert(BString, pos)")
	{
		BString string("string");
		string.Insert(BString("INSERTED"), 0);
		CHECK(std::string(string.String()) == "INSERTEDstring");
	}

	SECTION("Insert(BString, pos) into itself")
	{
		BString string("string");
		string.Insert(string, 0);
		CHECK(std::string(string.String()) == "string");
	}

	SECTION("Insert(BString, pos) with negative pos")
	{
		BString string;
		string.Insert(BString("INSERTED"), -1);
		CHECK(std::string(string.String()) == "NSERTED");
	}

	SECTION("Insert(BString, length, pos)")
	{
		BString string("string");
		string.Insert(BString("INSERTED"), 2, 2);
		CHECK(std::string(string.String()) == "stINring");
	}

	SECTION("Insert(BString, fromOffset, length, pos)")
	{
		BString string("string");
		string.Insert(BString("INSERTED"), 4, 30, 2);
		CHECK(std::string(string.String()) == "stRTEDring");
	}
}
