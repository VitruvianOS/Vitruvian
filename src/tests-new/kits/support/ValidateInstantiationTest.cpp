// Unit tests for validate_instantiation()

#include <Archivable.h>
#include <Message.h>

#include <errno.h>

#include <catch2/catch_test_macros.hpp>


static const char* kClassName = "FooBar";
static const char* kBogusClassName = "BarFoo";


TEST_CASE("validate_instantiation: invalid arguments",
	"[BArchivable][support]")
{
	SECTION("both NULL")
	{
		errno = B_OK;
		CHECK_FALSE(validate_instantiation(NULL, NULL));
		CHECK(errno == B_BAD_VALUE);
	}

	SECTION("NULL class name")
	{
		errno = B_OK;
		BMessage archive;
		CHECK_FALSE(validate_instantiation(&archive, NULL));
		CHECK(errno == B_MISMATCHED_VALUES);
	}

	SECTION("NULL archive")
	{
		errno = B_OK;
		CHECK_FALSE(validate_instantiation(NULL, kClassName));
		CHECK(errno == B_BAD_VALUE);
	}
}


TEST_CASE("validate_instantiation: class field", "[BArchivable][support]")
{
	SECTION("missing from the archive")
	{
		errno = B_OK;
		BMessage archive;
		CHECK_FALSE(validate_instantiation(&archive, kClassName));
		CHECK(errno == B_MISMATCHED_VALUES);
	}

	SECTION("does not match the class name")
	{
		errno = B_OK;
		BMessage archive;
		archive.AddString("class", kClassName);
		CHECK_FALSE(validate_instantiation(&archive, kBogusClassName));
		CHECK(errno == B_MISMATCHED_VALUES);
	}

	SECTION("matches the class name")
	{
		errno = B_OK;
		BMessage archive;
		archive.AddString("class", kClassName);
		CHECK(validate_instantiation(&archive, kClassName));
		CHECK(errno == B_OK);
	}
}
