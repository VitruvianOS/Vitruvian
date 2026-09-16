// Unit tests for the byte order functions in <ByteOrder.h>

#include <ByteOrder.h>
#include <TypeConstants.h>

#include <cmath>
#include <string.h>

#include <catch2/catch_test_macros.hpp>


TEST_CASE("ByteOrder: B_SWAP_INT16", "[ByteOrder][support]")
{
	CHECK(B_SWAP_INT16(0) == 0);
	CHECK(B_SWAP_INT16(0x1234) == 0x3412);
	CHECK(B_SWAP_INT16((int16)0xfedc) == (uint16)0xdcfe);
	CHECK(B_SWAP_INT16((uint16)0xfefd) == (uint16)0xfdfe);
}


TEST_CASE("ByteOrder: B_SWAP_INT32", "[ByteOrder][support]")
{
	CHECK(B_SWAP_INT32(0) == 0u);
	CHECK(B_SWAP_INT32(0x12345678) == 0x78563412u);
	CHECK(B_SWAP_INT32((int32)0xfedcba98) == 0x98badcfeu);
	CHECK(B_SWAP_INT32((uint32)0xfefdfcfb) == 0xfbfcfdfeu);
}


TEST_CASE("ByteOrder: B_SWAP_INT64", "[ByteOrder][support]")
{
	CHECK(B_SWAP_INT64(0) == 0ULL);
	CHECK(B_SWAP_INT64(0x1234567890LL) == 0x9078563412000000ULL);
	CHECK(B_SWAP_INT64((int64)0xfedcba9876543210LL)
		== 0x1032547698badcfeULL);
	CHECK(B_SWAP_INT64((uint64)0xfefdLL) == 0xfdfe000000000000ULL);
}


TEST_CASE("ByteOrder: B_SWAP_FLOAT", "[ByteOrder][support]")
{
	const float kNumber = 1.125f;

	CHECK(B_SWAP_FLOAT(B_SWAP_FLOAT(kNumber)) == kNumber);
	CHECK(std::isnan(B_SWAP_FLOAT(B_SWAP_FLOAT(NAN))));
	CHECK(B_SWAP_FLOAT(B_SWAP_FLOAT(HUGE_VALF)) == HUGE_VALF);
}


TEST_CASE("ByteOrder: B_SWAP_DOUBLE", "[ByteOrder][support]")
{
	const double kNumber = 1.125;

	CHECK(B_SWAP_DOUBLE(B_SWAP_DOUBLE(kNumber)) == kNumber);
	CHECK(std::isnan(B_SWAP_DOUBLE(B_SWAP_DOUBLE((double)NAN))));
	CHECK(B_SWAP_DOUBLE(B_SWAP_DOUBLE(HUGE_VAL)) == HUGE_VAL);
}


// Swaps an array of four values in every direction and checks that it only
// changes when the byte order actually differs, and that swapping back
// restores the original.
template<typename T>
static void
CheckSwapData(type_code type, const T source[4])
{
	const size_t size = 4 * sizeof(T);
	T target[4];
	memcpy(target, source, size);

	const swap_action toHost = B_HOST_IS_LENDIAN
		? B_SWAP_LENDIAN_TO_HOST : B_SWAP_BENDIAN_TO_HOST;
	const swap_action fromHost = B_HOST_IS_LENDIAN
		? B_SWAP_HOST_TO_LENDIAN : B_SWAP_HOST_TO_BENDIAN;
	const swap_action toOther = B_HOST_IS_LENDIAN
		? B_SWAP_HOST_TO_BENDIAN : B_SWAP_HOST_TO_LENDIAN;
	const swap_action fromOther = B_HOST_IS_LENDIAN
		? B_SWAP_BENDIAN_TO_HOST : B_SWAP_LENDIAN_TO_HOST;

	// Host byte order: nothing changes
	swap_data(type, target, size, fromHost);
	CHECK(memcmp(target, source, size) == 0);
	swap_data(type, target, size, toHost);
	CHECK(memcmp(target, source, size) == 0);

	// Other byte order: swapped, and swapped back
	swap_data(type, target, size, toOther);
	CHECK(memcmp(target, source, size) != 0);
	swap_data(type, target, size, fromOther);
	CHECK(memcmp(target, source, size) == 0);

	swap_data(type, target, size, B_SWAP_ALWAYS);
	CHECK(memcmp(target, source, size) != 0);
	swap_data(type, target, size, B_SWAP_ALWAYS);
	CHECK(memcmp(target, source, size) == 0);
}


TEST_CASE("ByteOrder: swap_data", "[ByteOrder][support]")
{
	SECTION("invalid arguments")
	{
		char string[4];
		int32 number = 0;
		const swap_action fromHost = B_HOST_IS_LENDIAN
			? B_SWAP_HOST_TO_LENDIAN : B_SWAP_HOST_TO_BENDIAN;

		CHECK(swap_data(B_STRING_TYPE, string, 4, B_SWAP_ALWAYS)
			== B_BAD_VALUE);
		CHECK(swap_data(B_INT32_TYPE, NULL, 4, B_SWAP_ALWAYS) == B_BAD_VALUE);

		// Nothing to swap succeeds before the data is looked at
		CHECK(swap_data(B_INT32_TYPE, &number, 0, B_SWAP_ALWAYS) == B_OK);
		CHECK(swap_data(B_INT32_TYPE, NULL, 4, fromHost) == B_OK);
	}

	SECTION("uint64")
	{
		const uint64 kValues[] = { 0x0123456789abcdefULL, 0x1234,
			0x5678000000000000ULL, 0x0 };
		CheckSwapData(B_UINT64_TYPE, kValues);
	}

	SECTION("uint32")
	{
		const uint32 kValues[] = { 0x12345678, 0x1234, 0x56780000, 0x0 };
		CheckSwapData(B_UINT32_TYPE, kValues);
	}

	SECTION("uint16")
	{
		const uint16 kValues[] = { 0x1234, 0x12, 0x3400, 0x0 };
		CheckSwapData(B_UINT16_TYPE, kValues);
	}

	SECTION("float")
	{
		const float kValues[] = { 3.4f, 0.0f, NAN, HUGE_VALF };
		CheckSwapData(B_FLOAT_TYPE, kValues);
	}

	SECTION("double")
	{
		const double kValues[] = { 3.42, 0.0, NAN, HUGE_VAL };
		CheckSwapData(B_DOUBLE_TYPE, kValues);
	}
}


TEST_CASE("ByteOrder: is_type_swapped", "[ByteOrder][support]")
{
	const type_code kSwappedTypes[] = {
		B_BOOL_TYPE,
		B_CHAR_TYPE,
		B_COLOR_8_BIT_TYPE,
		B_DOUBLE_TYPE,
		B_FLOAT_TYPE,
		B_GRAYSCALE_8_BIT_TYPE,
		B_INT64_TYPE,
		B_INT32_TYPE,
		B_INT16_TYPE,
		B_INT8_TYPE,
		B_MESSAGE_TYPE,
		B_MESSENGER_TYPE,
		B_MIME_TYPE,
		B_MONOCHROME_1_BIT_TYPE,
		B_OFF_T_TYPE,
		B_PATTERN_TYPE,
		B_POINTER_TYPE,
		B_POINT_TYPE,
		B_RECT_TYPE,
		B_REF_TYPE,
		B_NODE_REF_TYPE,
		B_RGB_32_BIT_TYPE,
		B_RGB_COLOR_TYPE,
		B_SIZE_T_TYPE,
		B_SSIZE_T_TYPE,
		B_STRING_TYPE,
		B_TIME_TYPE,
		B_UINT64_TYPE,
		B_UINT32_TYPE,
		B_UINT16_TYPE,
		B_UINT8_TYPE,
	};

	const type_code kNotSwappedTypes[] = {
		B_ANY_TYPE,
		B_ASCII_TYPE,
		B_MEDIA_PARAMETER_TYPE,
		B_MEDIA_PARAMETER_WEB_TYPE,
		B_MEDIA_PARAMETER_GROUP_TYPE,
		B_OBJECT_TYPE,
		B_RAW_TYPE,
		'    ',
		'0000',
		'1111',
		'aaaa',
	};

	for (type_code type : kSwappedTypes) {
		CAPTURE(type);
		CHECK(is_type_swapped(type));
	}

	for (type_code type : kNotSwappedTypes) {
		CAPTURE(type);
		CHECK_FALSE(is_type_swapped(type));
	}
}
