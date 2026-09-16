// Unit tests for BString format appending (operator<<)

#include <String.h>

#include <string>

#include <catch2/catch_test_macros.hpp>

#include <StringMakers.h>


TEST_CASE("BString: operator<< strings and chars", "[BString][support]")
{
	SECTION("const char*")
	{
		BString string("some");
		string << " ";
		string << "text";
		CHECK(std::string(string.String()) == "some text");
	}

	SECTION("BString")
	{
		BString string("some ");
		BString string2("text");
		string << string2;
		CHECK(std::string(string.String()) == "some text");
	}

	SECTION("char")
	{
		BString string("str");
		string << 'i' << 'n' << 'g';
		CHECK(std::string(string.String()) == "string");
	}

	SECTION("mixed")
	{
		BString string;
		string << "This" << ' ' << "is" << ' ' << 'a' << ' ' << "test"
			<< ' ' << "sentence";
		CHECK(std::string(string.String()) == "This is a test sentence");
	}
}


TEST_CASE("BString: operator<< numbers", "[BString][support]")
{
	SECTION("int")
	{
		BString string("level ");
		string << (int)42;
		CHECK(std::string(string.String()) == "level 42");
	}

	SECTION("negative int")
	{
		BString string("error ");
		string << (int)-1;
		CHECK(std::string(string.String()) == "error -1");
	}

	SECTION("unsigned int")
	{
		BString string("number ");
		string << (unsigned int)296;
		CHECK(std::string(string.String()) == "number 296");
	}

	SECTION("uint32")
	{
		BString string;
		string << (uint32)102456;
		CHECK(std::string(string.String()) == "102456");
	}

	SECTION("int32")
	{
		BString string;
		string << (int32)112456;
		CHECK(std::string(string.String()) == "112456");
	}

	SECTION("negative int32")
	{
		BString string;
		string << (int32)-112475;
		CHECK(std::string(string.String()) == "-112475");
	}

	SECTION("uint64")
	{
		BString string;
		string << (uint64)1145267987;
		CHECK(std::string(string.String()) == "1145267987");
	}

	SECTION("int64")
	{
		BString string;
		string << (int64)112456;
		CHECK(std::string(string.String()) == "112456");
	}

	SECTION("negative int64")
	{
		BString string;
		string << (int64)-112475;
		CHECK(std::string(string.String()) == "-112475");
	}

	SECTION("float")
	{
		BString string;
		string << (float)34.542;
		CHECK(std::string(string.String()) == "34.54");
	}
}
