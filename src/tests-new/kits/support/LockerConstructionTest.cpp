// Unit tests for BLocker construction
//
// Every constructor is checked through Sem(): the semaphore carries the name
// the locker was given, and its count says whether the locker is a benaphore
// (0) or a plain semaphore (1).

#include <Locker.h>
#include <OS.h>

#include <string.h>
#include <string>

#include <catch2/catch_test_macros.hpp>


static std::string
SemaphoreName(const BLocker& locker)
{
	sem_info info;
	if (get_sem_info(locker.Sem(), &info) != B_OK)
		return "<no semaphore>";

	return info.name;
}


static int32
SemaphoreCount(const BLocker& locker)
{
	int32 count = -1;
	if (get_sem_count(locker.Sem(), &count) != B_OK)
		return -1;

	return count;
}


static const int32 kBenaphore = 0;
static const int32 kSemaphore = 1;


TEST_CASE("BLocker: construction", "[BLocker][support]")
{
	SECTION("no arguments")
	{
		BLocker locker;
		CHECK(SemaphoreName(locker) == "some BLocker");
		CHECK(SemaphoreCount(locker) == kBenaphore);
	}

	SECTION("name only")
	{
		BLocker locker("test string");
		CHECK(SemaphoreName(locker) == "test string");
		CHECK(SemaphoreCount(locker) == kBenaphore);
	}

	SECTION("semaphore style")
	{
		BLocker locker(false);
		CHECK(SemaphoreName(locker) == "some BLocker");
		CHECK(SemaphoreCount(locker) == kSemaphore);
	}

	SECTION("benaphore style")
	{
		BLocker locker(true);
		CHECK(SemaphoreName(locker) == "some BLocker");
		CHECK(SemaphoreCount(locker) == kBenaphore);
	}

	SECTION("name, semaphore style")
	{
		BLocker locker("test string", false);
		CHECK(SemaphoreName(locker) == "test string");
		CHECK(SemaphoreCount(locker) == kSemaphore);
	}

	SECTION("name, benaphore style")
	{
		BLocker locker("test string", true);
		CHECK(SemaphoreName(locker) == "test string");
		CHECK(SemaphoreCount(locker) == kBenaphore);
	}
}
