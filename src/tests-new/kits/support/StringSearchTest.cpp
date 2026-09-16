// Unit tests for BString searching

#include <String.h>

#include <catch2/catch_test_macros.hpp>

#include <StringMakers.h>


TEST_CASE("BString: FindFirst", "[BString][support]")
{
	// FindFirst(BString&)
	CHECK(BString("last but not least").FindFirst(BString("st")) == 2);
	CHECK(BString().FindFirst(BString("some text")) == B_ERROR);

	// FindFirst(const char*)
	CHECK(BString("last but not least").FindFirst("st") == 2);
	CHECK(BString().FindFirst("some text") == B_ERROR);
	CHECK(BString("string").FindFirst((char*)NULL) == B_BAD_VALUE);

	// FindFirst(BString&, int32)
	CHECK(BString("abc abc abc").FindFirst(BString("abc"), 5) == 8);
	CHECK(BString("abc abc abc").FindFirst(BString("abc"), 200) == B_ERROR);
	CHECK(BString("abc abc abc").FindFirst(BString("abc"), -10) == B_ERROR);

	// FindFirst(const char*, int32)
	CHECK(BString("abc abc abc").FindFirst("abc", 2) == 4);
	CHECK(BString("abc abc abc").FindFirst("abc", 200) == B_ERROR);
	CHECK(BString("abc abc abc").FindFirst("abc", -10) == B_ERROR);
	CHECK(BString("abc abc abc").FindFirst((char*)NULL, 3) == B_BAD_VALUE);
	CHECK(BString("abc abc abc").FindFirst("b", 3) == 5);
	CHECK(BString("abc abc abc").FindFirst("a", 9) == B_ERROR);

	// FindFirst(char)
	CHECK(BString("abcd abcd").FindFirst('c') == 2);
	CHECK(BString("abcd abcd").FindFirst('e') == B_ERROR);

	// FindFirst(char, int32)
	CHECK(BString("abc abc abc").FindFirst('b', 3) == 5);
	CHECK(BString("abcd abcd").FindFirst('e', 3) == B_ERROR);
	CHECK(BString("abc abc abc").FindFirst('a', 9) == B_ERROR);
}


TEST_CASE("BString: StartsWith", "[BString][support]")
{
	const BString string("last but not least");

	CHECK(string.StartsWith(BString("last")));
	CHECK(string.StartsWith("last"));
	CHECK(string.StartsWith("last", 4));
}


TEST_CASE("BString: FindLast", "[BString][support]")
{
	// FindLast(BString&)
	CHECK(BString("last but not least").FindLast(BString("st")) == 16);
	CHECK(BString().FindLast(BString("some text")) == B_ERROR);

	// FindLast(const char*)
	CHECK(BString("last but not least").FindLast("st") == 16);
	CHECK(BString().FindLast("some text") == B_ERROR);
	CHECK(BString("string").FindLast((char*)NULL) == B_BAD_VALUE);

	// FindLast(BString&, int32)
	CHECK(BString("abcabcabc").FindLast(BString("abc"), 7) == 3);
	CHECK(BString("abc abc abc").FindLast(BString("abc"), -10) == B_ERROR);

	// FindLast(const char*, int32)
	CHECK(BString("abc abc abc").FindLast("abc", 9) == 4);
	CHECK(BString("abc abc abc").FindLast("abc", -10) == B_ERROR);
	CHECK(BString("abc abc abc").FindLast((char*)NULL, 3) == B_BAD_VALUE);
	CHECK(BString("abc abc abc").FindLast("b", 5) == 1);
	CHECK(BString("abc abc abc").FindLast("a", 0) == B_ERROR);

	// FindLast(char)
	CHECK(BString("abcd abcd").FindLast('c') == 7);
	CHECK(BString("abcd abcd").FindLast('e') == B_ERROR);

	// FindLast(char, int32): unlike the string versions, the character at
	// beforeOffset itself is included in the search
	CHECK(BString("abc abc abc").FindLast('b', 5) == 5);
	CHECK(BString("abcd abcd").FindLast('e', 3) == B_ERROR);
	CHECK(BString("abcd abcd").FindLast('b', 6) == 6);
	CHECK(BString("abcd abcd").FindLast('b', 5) == 1);
	CHECK(BString("abc abc abc").FindLast('a', 0) == 0);
}


TEST_CASE("BString: IFindFirst", "[BString][support]")
{
	// IFindFirst(BString&)
	CHECK(BString("last but not least").IFindFirst(BString("st")) == 2);
	CHECK(BString("last but not least").IFindFirst(BString("ST")) == 2);
	CHECK(BString().IFindFirst(BString("some text")) == B_ERROR);
	CHECK(BString("string").IFindFirst(BString()) == 0);

	// IFindFirst(const char*)
	CHECK(BString("last but not least").IFindFirst("st") == 2);
	CHECK(BString("LAST BUT NOT least").IFindFirst("st") == 2);
	CHECK(BString().IFindFirst("some text") == B_ERROR);
	CHECK(BString("string").IFindFirst((char*)NULL) == B_BAD_VALUE);

	// IFindFirst(BString&, int32)
	CHECK(BString("abc abc abc").IFindFirst(BString("abc"), 5) == 8);
	CHECK(BString("abc abc abc").IFindFirst(BString("AbC"), 5) == 8);
	CHECK(BString("abc abc abc").IFindFirst(BString("abc"), 200) == B_ERROR);
	CHECK(BString("abc abc abc").IFindFirst(BString("abc"), -10) == B_ERROR);

	// IFindFirst(const char*, int32)
	CHECK(BString("abc abc abc").IFindFirst("abc", 2) == 4);
	CHECK(BString("AbC ABC abC").IFindFirst("abc", 2) == 4);
	CHECK(BString("abc abc abc").IFindFirst("abc", 200) == B_ERROR);
	CHECK(BString("abc abc abc").IFindFirst("abc", -10) == B_ERROR);
}


TEST_CASE("BString: IStartsWith", "[BString][support]")
{
	const BString string("last but not least");

	CHECK(string.IStartsWith(BString("lAsT")));
	CHECK(string.IStartsWith("lAsT"));
	CHECK(string.IStartsWith("lAsT", 4));
}


TEST_CASE("BString: IFindLast", "[BString][support]")
{
	// IFindLast(BString&)
	CHECK(BString("last but not least").IFindLast(BString("st")) == 16);
	CHECK(BString("laSt but NOT leaSt").IFindLast(BString("sT")) == 16);
	CHECK(BString().IFindLast(BString("some text")) == B_ERROR);

	// IFindLast(const char*)
	CHECK(BString("last but not least").IFindLast("st") == 16);
	CHECK(BString("laSt but NOT leaSt").IFindLast("ST") == 16);
	CHECK(BString().IFindLast("some text") == B_ERROR);
	CHECK(BString("string").IFindLast((char*)NULL) == B_BAD_VALUE);

	// IFindLast(BString&, int32)
	CHECK(BString("abcabcabc").IFindLast(BString("abc"), 7) == 3);
	CHECK(BString("abcabcabc").IFindLast(BString("AbC"), 7) == 3);
	CHECK(BString("abc abc abc").IFindLast(BString("abc"), -10) == B_ERROR);

	// IFindLast(const char*, int32)
	CHECK(BString("abc abc abc").IFindLast("abc", 9) == 4);
	CHECK(BString("ABc abC aBC").IFindLast("aBc", 9) == 4);
	CHECK(BString("abc abc abc").IFindLast("abc", -10) == B_ERROR);
	CHECK(BString("abc def ghi").IFindLast("abc", 4) == 0);
}


TEST_CASE("BString: EndsWith", "[BString][support]")
{
	const BString lower("last but not least");
	const BString mixed("laSt but NOT leaSt");

	// EndsWith(BString&)
	CHECK(lower.EndsWith(BString("st")));
	CHECK_FALSE(mixed.EndsWith(BString("sT")));

	// EndsWith(const char*)
	CHECK(lower.EndsWith("least"));
	CHECK_FALSE(mixed.EndsWith("least"));

	// EndsWith(const char*, int32)
	CHECK(lower.EndsWith("st", 2));
	CHECK_FALSE(mixed.EndsWith("sT", 2));
}


TEST_CASE("BString: IEndsWith", "[BString][support]")
{
	const BString lower("last but not least");
	const BString mixed("laSt but NOT leaSt");

	// IEndsWith(BString&)
	CHECK(lower.IEndsWith(BString("st")));
	CHECK(mixed.IEndsWith(BString("sT")));

	// IEndsWith(const char*)
	CHECK(lower.IEndsWith("st"));
	CHECK(mixed.IEndsWith("sT"));

	// IEndsWith(const char*, int32)
	CHECK(lower.IEndsWith("st", 2));
	CHECK(mixed.IEndsWith("sT", 2));
}
