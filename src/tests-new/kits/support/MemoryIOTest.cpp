// Unit tests for BMemoryIO

#include <DataIO.h>

#include <stdio.h>
#include <string.h>
#include <string>

#include <catch2/catch_test_macros.hpp>


TEST_CASE("BMemoryIO: const buffer", "[BMemoryIO][support]")
{
	const char buf[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	BMemoryIO mem(buf, 10);
	char readBuf[10] = "";

	SECTION("SetSize() smaller is not allowed")
	{
		CHECK(mem.SetSize(4) == B_NOT_ALLOWED);
	}

	SECTION("SetSize() larger is not allowed")
	{
		CHECK(mem.SetSize(20) == B_NOT_ALLOWED);
	}

	SECTION("Write() is not allowed")
	{
		CHECK(mem.Write(readBuf, 3) == B_NOT_ALLOWED);
		CHECK(std::string(readBuf) == "");
	}

	SECTION("WriteAt() is not allowed")
	{
		CHECK(mem.WriteAt(2, readBuf, 1) == B_NOT_ALLOWED);
		CHECK(std::string(readBuf) == "");
	}
}


TEST_CASE("BMemoryIO: Seek", "[BMemoryIO][support]")
{
	char buf[10];
	BMemoryIO mem(buf, 10);

	// Each step builds on the position left by the previous one
	CHECK(mem.Seek(3, SEEK_SET) == 3);
	CHECK(mem.Seek(3, SEEK_CUR) == 6);
	CHECK(mem.Seek(0, SEEK_END) == 10);
	CHECK(mem.Seek(-5, SEEK_END) == 5);
	CHECK(mem.Seek(5, SEEK_END) == 15);
}


TEST_CASE("BMemoryIO: Write", "[BMemoryIO][support]")
{
	char buf[10];
	const char* writeBuf = "ABCDEFG";
	BMemoryIO mem(buf, 10);
	ssize_t written;
	off_t pos;

	// Write() advances the position; the WriteAt() checks below are relative
	// to wherever that leaves it.
	memset(buf, 0, 10);
	pos = mem.Position();
	written = mem.Write(writeBuf, 7);
	CHECK(written == 7);
	CHECK(std::string(buf) == writeBuf);
	CHECK(mem.Position() == pos + written);

	// WriteAt() in the middle does not move the position
	memset(buf, 0, 10);
	pos = mem.Position();
	written = mem.WriteAt(3, writeBuf, 2);
	CHECK(written == 2);
	CHECK(std::string(buf + 3, 2) == std::string(writeBuf, 2));
	CHECK(mem.Position() == pos);

	// WriteAt() past the end is truncated to the buffer
	memset(buf, 0, 10);
	pos = mem.Position();
	written = mem.WriteAt(9, writeBuf, 5);
	CHECK(written == 1);
	CHECK(buf[9] == writeBuf[0]);
	CHECK(mem.Position() == pos);

	// WriteAt() a negative offset fails
	memset(buf, 0, 10);
	written = mem.WriteAt(-10, writeBuf, 5);
	CHECK(written == B_BAD_VALUE);
}


TEST_CASE("BMemoryIO: Read", "[BMemoryIO][support]")
{
	char buf[20] = "0123456789ABCDEFGHI";
	char readBuf[10];
	memset(readBuf, 0, 10);
	BMemoryIO mem(buf, 20);
	ssize_t bytesRead;
	off_t pos;

	SECTION("Read() from the start")
	{
		pos = mem.Position();
		bytesRead = mem.Read(readBuf, 10);
		CHECK(bytesRead == 10);
		CHECK(std::string(readBuf, 10) == std::string(buf, 10));
		CHECK(mem.Position() == pos + bytesRead);
	}

	SECTION("ReadAt() past the end reads nothing")
	{
		pos = mem.Position();
		bytesRead = mem.ReadAt(30, readBuf, 10);
		CHECK(bytesRead == 0);
		CHECK(mem.Position() == pos);
	}

	SECTION("Read() at the end reads nothing")
	{
		pos = mem.Seek(0, SEEK_END);
		bytesRead = mem.Read(readBuf, 10);
		CHECK(bytesRead == 0);
		CHECK(mem.Position() == pos);
	}
}


TEST_CASE("BMemoryIO: SetSize", "[BMemoryIO][support]")
{
	char buf[20] = "0123456789ABCDEFGHI";
	char readBuf[10];
	memset(readBuf, 0, 10);
	BMemoryIO mem(buf, 10);

	// Shrink, then grow back to the original length
	CHECK(mem.SetSize(5) == B_OK);
	CHECK(mem.Seek(0, SEEK_END) == 5);
	CHECK(mem.WriteAt(10, readBuf, 3) == 0);

	CHECK(mem.SetSize(10) == B_OK);
	CHECK(mem.Seek(0, SEEK_END) == 10);
	CHECK(mem.WriteAt(5, readBuf, 6) == 5);

	// Cannot grow beyond the buffer passed to the constructor
	CHECK(mem.SetSize(20) == B_ERROR);
}
