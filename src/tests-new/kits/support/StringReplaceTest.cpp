// Unit tests for BString replacement

#include <String.h>

#include <string.h>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <StringMakers.h>


TEST_CASE("BString: Replace char", "[BString][support]")
{
	// ReplaceFirst(char, char)
	{
		BString str("test string");
		str.ReplaceFirst('t', 'b');
		CHECK(std::string(str.String()) == "best string");
	}
	{
		BString str("test string");
		str.ReplaceFirst('x', 'b');
		CHECK(std::string(str.String()) == "test string");
	}

	// ReplaceLast(char, char)
	{
		BString str("test string");
		str.ReplaceLast('t', 'w');
		CHECK(std::string(str.String()) == "test swring");
	}
	{
		BString str("test string");
		str.ReplaceLast('x', 'b');
		CHECK(std::string(str.String()) == "test string");
	}

	// ReplaceAll(char, char, int32)
	{
		BString str("test string");
		str.ReplaceAll('t', 'i');
		CHECK(std::string(str.String()) == "iesi siring");
	}
	{
		BString str("test string");
		str.ReplaceAll('x', 'b');
		CHECK(std::string(str.String()) == "test string");
	}
	{
		BString str("test string");
		str.ReplaceAll('t', 't');
		CHECK(std::string(str.String()) == "test string");
	}
	{
		BString str("test string");
		str.ReplaceAll('t', 'i', 2);
		CHECK(std::string(str.String()) == "tesi siring");
	}

	// Replace(char, char, int32, int32)
	{
		BString str("she sells sea shells on the sea shore");
		str.Replace('s', 't', 4, 2);
		CHECK(std::string(str.String())
			== "she tellt tea thells on the sea shore");
	}
	{
		BString str("she sells sea shells on the sea shore");
		str.Replace('s', 's', 4, 2);
		CHECK(std::string(str.String())
			== "she sells sea shells on the sea shore");
	}
	{
		BString str;
		str.Replace('s', 'x', 12, 32);
		CHECK(std::string(str.String()) == "");
	}
}


TEST_CASE("BString: Replace string", "[BString][support]")
{
	// ReplaceFirst(const char*, const char*)
	{
		BString str("she sells sea shells on the seashore");
		str.ReplaceFirst("sea", "the");
		CHECK(std::string(str.String())
			== "she sells the shells on the seashore");
	}
	{
		BString str("she sells sea shells on the seashore");
		str.ReplaceFirst("tex", "the");
		CHECK(std::string(str.String())
			== "she sells sea shells on the seashore");
	}
	{
		BString str("Error moving \"%name\"");
		str.ReplaceFirst("%name", NULL);
		CHECK(std::string(str.String()) == "Error moving \"\"");
	}

	// ReplaceLast(const char*, const char*)
	{
		BString str("she sells sea shells on the seashore");
		str.ReplaceLast("sea", "the");
		CHECK(std::string(str.String())
			== "she sells sea shells on the theshore");
	}
	{
		BString str("she sells sea shells on the seashore");
		str.ReplaceLast("tex", "the");
		CHECK(std::string(str.String())
			== "she sells sea shells on the seashore");
	}
	{
		BString str("she sells sea shells on the seashore");
		str.ReplaceLast("sea", NULL);
		CHECK(std::string(str.String())
			== "she sells sea shells on the shore");
	}

	// ReplaceAll(const char*, const char*, int32)
	{
		BString str("abc abc abc");
		str.ReplaceAll("ab", "abc");
		CHECK(std::string(str.String()) == "abcc abcc abcc");
	}
	{
		BString str("abc abc abc");
		str.ReplaceAll("abc", "abc");
		CHECK(std::string(str.String()) == "abc abc abc");
	}
	{
		BString str("abc abc abc");
		str.ReplaceAll("abc", NULL);
		CHECK(std::string(str.String()) == "  ");
	}
	{
		BString str("she sells sea shells on the seashore");
		str.ReplaceAll("tex", "the");
		CHECK(std::string(str.String())
			== "she sells sea shells on the seashore");
	}
	{
		BString str("she sells sea shells on the seashore");
		str.ReplaceAll("sea", "the", 11);
		CHECK(std::string(str.String())
			== "she sells sea shells on the theshore");
	}
	{
		BString str("she sells sea shells on the seashore");
		str.ReplaceAll("sea", "sea", 11);
		CHECK(std::string(str.String())
			== "she sells sea shells on the seashore");
	}
}


TEST_CASE("BString: IReplace char", "[BString][support]")
{
	// IReplaceFirst(char, char)
	{
		BString str("test string");
		str.IReplaceFirst('t', 'b');
		CHECK(std::string(str.String()) == "best string");
	}
	{
		BString str("test string");
		str.IReplaceFirst('x', 'b');
		CHECK(std::string(str.String()) == "test string");
	}

	// IReplaceLast(char, char)
	{
		BString str("test string");
		str.IReplaceLast('t', 'w');
		CHECK(std::string(str.String()) == "test swring");
	}
	{
		BString str("test string");
		str.IReplaceLast('x', 'b');
		CHECK(std::string(str.String()) == "test string");
	}

	// IReplaceAll(char, char, int32)
	{
		BString str("TEST string");
		str.IReplaceAll('t', 'i');
		CHECK(std::string(str.String()) == "iESi siring");
	}
	{
		BString str("TEST string");
		str.IReplaceAll('t', 'T');
		CHECK(std::string(str.String()) == "TEST sTring");
	}
	{
		BString str("test string");
		str.IReplaceAll('x', 'b');
		CHECK(std::string(str.String()) == "test string");
	}
	{
		BString str("TEST string");
		str.IReplaceAll('t', 'i', 2);
		CHECK(std::string(str.String()) == "TESi siring");
	}

	// IReplace(char, char, int32, int32)
	{
		BString str("She sells Sea shells on the sea shore");
		str.IReplace('s', 't', 4, 2);
		CHECK(std::string(str.String())
			== "She tellt tea thells on the sea shore");
	}
	{
		BString str("She sells Sea shells on the sea shore");
		str.IReplace('s', 's', 4, 2);
		CHECK(std::string(str.String())
			== "She sells sea shells on the sea shore");
	}
	{
		BString str;
		str.IReplace('s', 'x', 12, 32);
		CHECK(std::string(str.String()) == "");
	}
}


TEST_CASE("BString: IReplace string", "[BString][support]")
{
	// IReplaceFirst(const char*, const char*)
	{
		BString str("she sells SeA shells on the seashore");
		str.IReplaceFirst("sea", "the");
		CHECK(std::string(str.String())
			== "she sells the shells on the seashore");
	}
	{
		BString str("she sells sea shells on the seashore");
		str.IReplaceFirst("tex", "the");
		CHECK(std::string(str.String())
			== "she sells sea shells on the seashore");
	}
	{
		BString str("she sells SeA shells on the seashore");
		str.IReplaceFirst("sea ", NULL);
		CHECK(std::string(str.String())
			== "she sells shells on the seashore");
	}

	// IReplaceLast(const char*, const char*)
	{
		BString str("she sells sea shells on the SEashore");
		str.IReplaceLast("sea", "the");
		CHECK(std::string(str.String())
			== "she sells sea shells on the theshore");
	}
	{
		BString str("she sells sea shells on the seashore");
		str.IReplaceLast("tex", "the");
		CHECK(std::string(str.String())
			== "she sells sea shells on the seashore");
	}
	{
		BString str("she sells sea shells on the SEashore");
		str.IReplaceLast("sea", NULL);
		CHECK(std::string(str.String())
			== "she sells sea shells on the shore");
	}

	// IReplaceAll(const char*, const char*, int32)
	{
		BString str("abc ABc aBc");
		str.IReplaceAll("ab", "abc");
		CHECK(std::string(str.String()) == "abcc abcc abcc");
	}
	{
		BString str("she sells sea shells on the seashore");
		str.IReplaceAll("tex", "the");
		CHECK(std::string(str.String())
			== "she sells sea shells on the seashore");
	}
	{
		BString str("she sells SeA shells on the sEashore");
		str.IReplaceAll("sea", "the", 11);
		CHECK(std::string(str.String())
			== "she sells SeA shells on the theshore");
	}
	{
		BString str("abc ABc aBc");
		str.IReplaceAll("ab", NULL);
		CHECK(std::string(str.String()) == "c c c");
	}
}


TEST_CASE("BString: ReplaceSet", "[BString][support]")
{
	// ReplaceSet(const char*, char)
	{
		BString str("abc abc abc");
		str.ReplaceSet("ab", 'x');
		CHECK(std::string(str.String()) == "xxc xxc xxc");
	}
	{
		BString str("abcabcabcbababc");
		str.ReplaceSet("abc", 'c');
		CHECK(std::string(str.String()) == "ccccccccccccccc");
	}
	{
		BString str("abcabcabcbababc");
		str.ReplaceSet("c", 'c');
		CHECK(std::string(str.String()) == "abcabcabcbababc");
	}

	// ReplaceSet(const char*, const char*)
	{
		BString str("abcd abcd abcd");
		str.ReplaceSet("abcd ", "");
		CHECK(std::string(str.String()) == "");
	}
	{
		BString str("abcd abcd abcd");
		str.ReplaceSet("ad", "da");
		CHECK(std::string(str.String()) == "dabcda dabcda dabcda");
	}
	{
		BString str("abcd abcd abcd");
		str.ReplaceSet("ad", "");
		CHECK(std::string(str.String()) == "bc bc bc");
	}
}


TEST_CASE("BString: Replace large strings", "[BString][support]")
{
	// Repeats some of the tests above with a lot of data
	const int32 size = 1024 * 50;

	auto makeString = [size](BString& str) {
		char* buf = str.LockBuffer(size);
		memset(buf, 'x', size);
		str.UnlockBuffer(size);
	};

	// ReplaceSet(const char*, const char*)
	{
		BString str;
		makeString(str);
		str.ReplaceSet("x", "y");
		CHECK(str.Length() == size);
	}
	{
		BString str;
		makeString(str);
		str.ReplaceSet("x", "");
		CHECK(str.Length() == 0);
	}

	// ReplaceAll(const char*, const char*)
	{
		BString str;
		makeString(str);
		str.ReplaceAll("x", "y");
		CHECK(str.Length() == size);
	}
	{
		BString str;
		makeString(str);
		str.ReplaceAll("xx", "y");
		CHECK(str.Length() == size / 2);
	}
	{
		BString str;
		makeString(str);
		str.ReplaceAll("xx", "");
		CHECK(str.Length() == 0);
	}
}
