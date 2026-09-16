// Unit tests for BArchivable

#include <Archivable.h>
#include <Message.h>

#include <string>

#include <catch2/catch_test_macros.hpp>


TEST_CASE("BArchivable: Perform", "[BArchivable][support]")
{
	BArchivable archivable;

	CHECK(archivable.Perform(0, NULL) == B_NAME_NOT_FOUND);
}


TEST_CASE("BArchivable: Archive", "[BArchivable][support]")
{
	BArchivable archivable;

	SECTION("into a NULL message, shallow")
	{
		CHECK(archivable.Archive(NULL, false) == B_BAD_VALUE);
	}

	SECTION("into a NULL message, deep")
	{
		CHECK(archivable.Archive(NULL, true) == B_BAD_VALUE);
	}

	SECTION("shallow")
	{
		BMessage storage;
		REQUIRE(archivable.Archive(&storage, false) == B_OK);

		const char* name;
		REQUIRE(storage.FindString("class", &name) == B_OK);
		CHECK(std::string(name) == "BArchivable");
	}

	SECTION("deep")
	{
		BMessage storage;
		REQUIRE(archivable.Archive(&storage, true) == B_OK);

		const char* name;
		REQUIRE(storage.FindString("class", &name) == B_OK);
		CHECK(std::string(name) == "BArchivable");
	}
}
