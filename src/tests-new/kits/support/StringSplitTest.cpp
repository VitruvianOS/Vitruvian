// Unit tests for BString splitting

#include <String.h>
#include <StringList.h>

#include <catch2/catch_test_macros.hpp>

#include <StringMakers.h>


TEST_CASE("BString: Split", "[BString][support]")
{
	const BString str("test::string");

	SECTION("single char separator, no empty strings")
	{
		BStringList list;
		str.Split(":", true, list);
		CHECK(list.CountStrings() == 2);
	}

	SECTION("two char separator, no empty strings")
	{
		BStringList list;
		str.Split("::", true, list);
		CHECK(list.CountStrings() == 2);
	}

	SECTION("two char separator, with empty strings")
	{
		BStringList list;
		str.Split("::", false, list);
		CHECK(list.CountStrings() == 2);
	}

	SECTION("single char separator, with empty strings")
	{
		BStringList list;
		str.Split(":", false, list);
		CHECK(list.CountStrings() == 3);
	}
}
