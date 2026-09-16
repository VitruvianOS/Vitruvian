// Unit tests for find_instantiation_func()
//
// The class name and signature arguments are covered in this matrix:
//
//		              name        signature
//		              ----------  ---------
//		              NULL        NULL
//		              invalid     NULL
//		              NULL        invalid
//		              invalid     invalid
//		              local       NULL
//		              not loaded  NULL
//		              local       invalid
//		              not loaded  invalid
//		              not loaded  valid
//
// Each case is checked twice: once through the (name, signature) overload and
// once through the BMessage overload. A "local" class is implemented in the
// test executable, a "not loaded" one in an add-on that is not loaded here --
// find_instantiation_func() never loads anything, so it must not find it.
//
// The case of a local class with its own valid signature is left out: it needs
// a BApplication to know the signature of the running executable.

#include <Archivable.h>
#include <Message.h>

#include <catch2/catch_test_macros.hpp>

#include "ArchivableTestObject.h"


static const char* kInvalidClassName = "TInvalidClassName";
static const char* kInvalidSignature = "application/x-vnd.InvalidSignature";
static const char* kLocalClassName = "TIOTest";
static const char* kNotLoadedClassName = "TRemoteTestObject";
static const char* kNotLoadedSignature = "application/x-vnd.RemoteObjectDef";


TEST_CASE("find_instantiation_func: by class name", "[BArchivable][support]")
{
	SECTION("both arguments NULL")
	{
		CHECK(find_instantiation_func(NULL, NULL) == NULL);
	}

	SECTION("invalid class name, no signature")
	{
		CHECK(find_instantiation_func(kInvalidClassName, NULL) == NULL);
	}

	SECTION("no class name, invalid signature")
	{
		CHECK(find_instantiation_func(NULL, kInvalidSignature) == NULL);
	}

	SECTION("invalid class name and signature")
	{
		CHECK(find_instantiation_func(kInvalidClassName, kInvalidSignature)
			== NULL);
	}

	SECTION("local class, no signature")
	{
		instantiation_func function
			= find_instantiation_func(kLocalClassName, NULL);
		REQUIRE(function != NULL);

		BMessage archive;
		archive.AddString("class", kLocalClassName);
		CHECK(dynamic_cast<TIOTest*>(function(&archive)) != NULL);
	}

	SECTION("class that is not loaded, no signature")
	{
		CHECK(find_instantiation_func(kNotLoadedClassName, NULL) == NULL);
	}

	SECTION("local class, invalid signature")
	{
		CHECK(find_instantiation_func(kLocalClassName, kInvalidSignature)
			== NULL);
	}

	SECTION("class that is not loaded, invalid signature")
	{
		CHECK(find_instantiation_func(kNotLoadedClassName, kInvalidSignature)
			== NULL);
	}

	SECTION("class that is not loaded, its own signature")
	{
		// Nothing is loaded to satisfy the lookup
		CHECK(find_instantiation_func(kNotLoadedClassName, kNotLoadedSignature)
			== NULL);
	}
}


TEST_CASE("find_instantiation_func: by archive", "[BArchivable][support]")
{
	SECTION("NULL archive")
	{
		CHECK(find_instantiation_func((BMessage*)NULL) == NULL);
	}

	SECTION("invalid class name, no signature")
	{
		BMessage archive;
		archive.AddString("class", kInvalidClassName);
		CHECK(find_instantiation_func(&archive) == NULL);
	}

	SECTION("no class name, invalid signature")
	{
		BMessage archive;
		archive.AddString("add_on", kInvalidSignature);
		CHECK(find_instantiation_func(&archive) == NULL);
	}

	SECTION("invalid class name and signature")
	{
		BMessage archive;
		archive.AddString("class", kInvalidClassName);
		archive.AddString("add_on", kInvalidSignature);
		CHECK(find_instantiation_func(&archive) == NULL);
	}

	SECTION("local class, no signature")
	{
		BMessage archive;
		archive.AddString("class", kLocalClassName);

		instantiation_func function = find_instantiation_func(&archive);
		REQUIRE(function != NULL);
		CHECK(dynamic_cast<TIOTest*>(function(&archive)) != NULL);
	}

	SECTION("class that is not loaded, no signature")
	{
		BMessage archive;
		archive.AddString("class", kNotLoadedClassName);
		CHECK(find_instantiation_func(&archive) == NULL);
	}

	SECTION("local class, invalid signature")
	{
		BMessage archive;
		archive.AddString("class", kLocalClassName);
		archive.AddString("add_on", kInvalidSignature);
		CHECK(find_instantiation_func(&archive) == NULL);
	}

	SECTION("class that is not loaded, invalid signature")
	{
		BMessage archive;
		archive.AddString("class", kNotLoadedClassName);
		archive.AddString("add_on", kInvalidSignature);
		CHECK(find_instantiation_func(&archive) == NULL);
	}

	SECTION("class that is not loaded, its own signature")
	{
		BMessage archive;
		archive.AddString("class", kNotLoadedClassName);
		archive.AddString("add_on", kNotLoadedSignature);
		CHECK(find_instantiation_func(&archive) == NULL);
	}
}
