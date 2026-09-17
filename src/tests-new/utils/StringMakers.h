// Catch2 printers for Vitruvian API types, so that failed checks show the
// values involved instead of "{?}".
//
// Include this after <catch2/catch_test_macros.hpp> in any test that compares
// these types directly, e.g. CHECK(string == BString("abc")).

#ifndef _STRING_MAKERS_H
#define _STRING_MAKERS_H

#include <String.h>

#include <string>

#include <catch2/catch_tostring.hpp>


namespace Catch {

template<>
struct StringMaker<BString> {
	static std::string convert(const BString& value)
	{
		return Detail::stringify(std::string(value.String(), value.Length()));
	}
};

}	// namespace Catch


#endif	// _STRING_MAKERS_H
