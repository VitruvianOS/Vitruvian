// Unit tests for BDateTime

#include <DateTime.h>

#include <catch2/catch_test_macros.hpp>


TEST_CASE("BDateTime: SetTime_t before the epoch", "[BDateTime][support]")
{
	BDateTime dateTime;
	dateTime.SetTime_t(-1);

	CHECK(dateTime.IsValid());
	CHECK(dateTime.Time().Second() == 59);
	CHECK(dateTime.Time().Minute() == 59);
	CHECK(dateTime.Time().Hour() == 23);
	CHECK(dateTime.Date().Day() == 31);
	CHECK(dateTime.Date().Month() == 12);
	CHECK(dateTime.Date().Year() == 1969);
}
