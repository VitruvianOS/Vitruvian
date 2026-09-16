// Unit tests for BString character access

#include <String.h>

#include <string>

#include <catch2/catch_test_macros.hpp>

#include <StringMakers.h>


TEST_CASE("BString: CharAccess", "[BString][support]")
{
	BString string("A simple string");

	// operator[]
	CHECK(string[0] == 'A');
	CHECK(string[1] == ' ');

	// SetByteAt() changes the string used by the checks below
	string.SetByteAt(0, 'a');
	CHECK(std::string(string.String()) == "a simple string");

	// ByteAt(int32)
	CHECK(string.ByteAt(-10) == 0);
	CHECK(string.ByteAt(200) == 0);
	CHECK(string.ByteAt(1) == ' ');
	CHECK(string.ByteAt(7) == 'e');
}
