// Unit tests for BMallocIO

#include <DataIO.h>

#include <stdio.h>

#include <catch2/catch_test_macros.hpp>


TEST_CASE("BMallocIO: Seek", "[BMallocIO][support]")
{
	BMallocIO mem;

	// Each step builds on the position left by the previous one
	CHECK(mem.Seek(3, SEEK_SET) == 3);
	CHECK(mem.Seek(3, SEEK_CUR) == 6);
	CHECK(mem.Seek(0, SEEK_END) == 0);
	CHECK(mem.Seek(-5, SEEK_END) == -5);
	CHECK(mem.Seek(5, SEEK_END) == 5);
	CHECK(mem.Seek(-20, SEEK_SET) == -20);
}


TEST_CASE("BMallocIO: Write", "[BMallocIO][support]")
{
	const char* writeBuf = "ABCDEFG";
	BMallocIO mem;

	CHECK(mem.Write(writeBuf, 7) == 7);
	CHECK(mem.WriteAt(0, writeBuf, 4) == 4);
	CHECK(mem.WriteAt(34, writeBuf, 256) == 256);
}


TEST_CASE("BMallocIO: BufferLength", "[BMallocIO][support]")
{
	BMallocIO mem;
	char writeBuf[11] = "0123456789";

	CHECK(mem.BufferLength() == 0u);

	CHECK(mem.Write(writeBuf, 10) == 10);
	CHECK(mem.BufferLength() == 10u);

	CHECK(mem.SetSize(0) == B_OK);
	CHECK(mem.BufferLength() == 0u);

	// Regression check for the BResource crashing bug
	CHECK(mem.SetSize(200) == B_OK);
	CHECK(mem.BufferLength() == 200u);
	CHECK(mem.Seek(0, SEEK_END) == 200);

	// Shrinking the buffer does not move the position
	off_t offset = mem.Seek(0, SEEK_END);
	CHECK(mem.SetSize(100) == B_OK);
	CHECK(mem.BufferLength() == 100u);
	CHECK(mem.Position() == offset);
}
