// Unit tests for BString removal

#include <String.h>

#include <string.h>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <StringMakers.h>


TEST_CASE("BString: Truncate", "[BString][support]")
{
	SECTION("lazy")
	{
		BString string("This is a long string");
		string.Truncate(14, true);
		CHECK(std::string(string.String()) == "This is a long");
		CHECK(string.Length() == 14);
	}

	SECTION("not lazy")
	{
		BString string("This is a long string");
		string.Truncate(14, false);
		CHECK(std::string(string.String()) == "This is a long");
		CHECK(string.Length() == 14);
	}

	SECTION("negative length truncates to 0")
	{
		BString string("This is a long string");
		string.Truncate(-3);
		CHECK(std::string(string.String()) == "");
		CHECK(string.Length() == 0);
	}

	SECTION("length beyond the end does nothing")
	{
		BString string("This is a long string");
		string.Truncate(45);
		CHECK(std::string(string.String()) == "This is a long string");
		CHECK(string.Length() == 21);
	}

	SECTION("empty string")
	{
		BString string;
		string.Truncate(0);
		CHECK(std::string(string.String()) == "");
		CHECK(string.Length() == 0);
	}
}


TEST_CASE("BString: Remove", "[BString][support]")
{
	SECTION("middle of the string")
	{
		BString string("a String");
		string.Remove(2, 2);
		CHECK(std::string(string.String()) == "a ring");
	}

	SECTION("empty string")
	{
		BString string;
		string.Remove(2, 1);
		CHECK(std::string(string.String()) == "");
	}

	SECTION("from beyond the end")
	{
		BString string("a String");
		string.Remove(20, 2);
		CHECK(std::string(string.String()) == "a String");
	}

	SECTION("from + length beyond the end")
	{
		BString string("a String");
		string.Remove(4, 30);
		CHECK(std::string(string.String()) == "a St");
	}

	SECTION("negative from")
	{
		BString string("a String");
		string.Remove(-3, 5);
		CHECK(std::string(string.String()) == "ing");
	}
}


TEST_CASE("BString: RemoveFirst", "[BString][support]")
{
	SECTION("BString, found")
	{
		BString string("first second first");
		string.RemoveFirst(BString("first"));
		CHECK(std::string(string.String()) == " second first");
	}

	SECTION("BString, not found")
	{
		BString string("first second first");
		string.RemoveFirst(BString("noway"));
		CHECK(std::string(string.String()) == "first second first");
	}

	SECTION("const char*, found")
	{
		BString string("first second first");
		string.RemoveFirst("first");
		CHECK(std::string(string.String()) == " second first");
	}

	SECTION("const char*, not found")
	{
		BString string("first second first");
		string.RemoveFirst("noway");
		CHECK(std::string(string.String()) == "first second first");
	}

	SECTION("NULL")
	{
		BString string("first second first");
		string.RemoveFirst((char*)NULL);
		CHECK(std::string(string.String()) == "first second first");
	}
}


TEST_CASE("BString: RemoveLast", "[BString][support]")
{
	SECTION("BString, found")
	{
		BString string("first second first");
		string.RemoveLast(BString("first"));
		CHECK(std::string(string.String()) == "first second ");
	}

	SECTION("BString, not found")
	{
		BString string("first second first");
		string.RemoveLast(BString("noway"));
		CHECK(std::string(string.String()) == "first second first");
	}

	SECTION("const char*, found")
	{
		BString string("first second first");
		string.RemoveLast("first");
		CHECK(std::string(string.String()) == "first second ");
	}

	SECTION("const char*, not found")
	{
		BString string("first second first");
		string.RemoveLast("noway");
		CHECK(std::string(string.String()) == "first second first");
	}
}


TEST_CASE("BString: RemoveAll", "[BString][support]")
{
	SECTION("BString, found")
	{
		BString string("first second first");
		string.RemoveAll(BString("first"));
		CHECK(std::string(string.String()) == " second ");
	}

	SECTION("BString, not found")
	{
		BString string("first second first");
		string.RemoveAll(BString("noway"));
		CHECK(std::string(string.String()) == "first second first");
	}

	SECTION("const char*, found")
	{
		BString string("first second first");
		string.RemoveAll("first");
		CHECK(std::string(string.String()) == " second ");
	}

	SECTION("const char*, not found")
	{
		BString string("first second first");
		string.RemoveAll("noway");
		CHECK(std::string(string.String()) == "first second first");
	}
}


TEST_CASE("BString: RemoveSet", "[BString][support]")
{
	SECTION("characters found")
	{
		BString string("a sentence with (3) (642) numbers (2) in it");
		string.RemoveSet("()3624 ");
		CHECK(std::string(string.String()) == "asentencewithnumbersinit");
	}

	SECTION("characters not found")
	{
		BString string("a string");
		string.RemoveSet("1345");
		CHECK(std::string(string.String()) == "a string");
	}
}


TEST_CASE("BString: MoveInto", "[BString][support]")
{
	SECTION("BString, part of the string")
	{
		BString into("some text");
		BString string("string");
		string.MoveInto(into, 3, 2);
		CHECK(std::string(into.String()) == "in");
		CHECK(std::string(string.String()) == "strg");
	}

	SECTION("BString, length beyond the end")
	{
		BString into("some text");
		BString string("string");
		string.MoveInto(into, 0, 200);
		CHECK(std::string(into.String()) == "string");
		CHECK(std::string(string.String()) == "");
	}

	SECTION("char*, part of the string")
	{
		char dest[100];
		memset(dest, 0, 100);
		BString string("some text");
		string.MoveInto(dest, 3, 2);
		CHECK(std::string(dest) == "e ");
		CHECK(std::string(string.String()) == "somtext");
	}

	SECTION("char*, length beyond the end")
	{
		char dest[100];
		memset(dest, 0, 100);
		BString string("some text");
		string.MoveInto(dest, 0, 50);
		CHECK(std::string(dest) == "some text");
		CHECK(std::string(string.String()) == "");
	}
}
