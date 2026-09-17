// Unit tests for instantiate_object()
//
// Some cases need a class that lives in another image. RemoteTestObject.cpp is
// built into an add-on for that, and REMOTE_ADDON_PATH is its path. The cases
// that name a signature need it resolved through BRoster, so the add-on's
// signature is registered in the MIME database for as long as the test needs
// it.

#include <Archivable.h>
#include <Entry.h>
#include <Message.h>
#include <MimeType.h>
#include <image.h>

#include <string.h>

#include <catch2/catch_test_macros.hpp>

#include "ArchivableTestObject.h"


static const char* kInvalidClassName = "TInvalidClassName";
static const char* kInvalidSignature = "application/x-vnd.InvalidSignature";
static const char* kLocalClassName = "TIOTest";
static const char* kRemoteClassName = "TRemoteTestObject";
static const char* kRemoteSignature = "application/x-vnd.RemoteObjectDef";


// instantiate_object() leaves an add-on it loaded itself in the team, so make
// sure a section that expects the class to be missing really starts without it.
static void
UnloadRemoteAddOn()
{
	image_info info;
	int32 cookie = 0;
	while (get_next_image_info(0, &cookie, &info) == B_OK) {
		if (strcmp(info.name, REMOTE_ADDON_PATH) == 0)
			unload_add_on(info.id);
	}
}


class ScopedAddOn {
public:
	ScopedAddOn()
		:
		fImage(load_add_on(REMOTE_ADDON_PATH))
	{
	}

	~ScopedAddOn()
	{
		if (fImage >= 0)
			unload_add_on(fImage);
	}

	image_id Image() const { return fImage; }

private:
	image_id	fImage;
};


// Makes BRoster::FindApp() able to resolve the add-on's signature, and leaves
// the MIME database as it was found.
class ScopedSignature {
public:
	ScopedSignature()
		:
		fType(kRemoteSignature),
		fInstalled(false)
	{
		if (!fType.IsInstalled()) {
			fType.Install();
			fInstalled = true;
		}

		BEntry entry(REMOTE_ADDON_PATH);
		entry_ref ref;
		if (entry.GetRef(&ref) == B_OK)
			fType.SetAppHint(&ref);
	}

	~ScopedSignature()
	{
		if (fInstalled)
			fType.Delete();
	}

private:
	BMimeType	fType;
	bool		fInstalled;
};


// The image_id argument doubles as a status output: on failure it receives an
// error code, on success the image the class was found in.
TEST_CASE("instantiate_object: without a usable archive",
	"[BArchivable][support]")
{
	UnloadRemoteAddOn();
	image_id id = B_OK;

	SECTION("NULL archive")
	{
		CHECK(instantiate_object(NULL, &id) == NULL);
		CHECK(id == B_BAD_VALUE);
	}

	SECTION("archive without a class name")
	{
		BMessage archive;
		CHECK(instantiate_object(&archive, &id) == NULL);
		CHECK(id == B_BAD_VALUE);
	}

	SECTION("invalid class name")
	{
		BMessage archive;
		archive.AddString("class", kInvalidClassName);
		CHECK(instantiate_object(&archive, &id) == NULL);
		CHECK(id == B_NAME_NOT_FOUND);
	}

	SECTION("class that is not loaded")
	{
		BMessage archive;
		archive.AddString("class", kRemoteClassName);
		CHECK(instantiate_object(&archive, &id) == NULL);
		CHECK(id == B_NAME_NOT_FOUND);
	}
}


TEST_CASE("instantiate_object: unknown signature",
	"[BArchivable][support][needs-registrar]")
{
	UnloadRemoteAddOn();
	image_id id = B_OK;
	BMessage archive;
	archive.AddString("add_on", kInvalidSignature);

	// A signature that resolves to nothing fails the same way whichever class
	// is named, even one that could have been found locally
	SECTION("invalid class name")
	{
		archive.AddString("class", kInvalidClassName);
		CHECK(instantiate_object(&archive, &id) == NULL);
		CHECK(id == B_LAUNCH_FAILED_APP_NOT_FOUND);
	}

	SECTION("local class")
	{
		archive.AddString("class", kLocalClassName);
		CHECK(instantiate_object(&archive, &id) == NULL);
		CHECK(id == B_LAUNCH_FAILED_APP_NOT_FOUND);
	}

	SECTION("class that is not loaded")
	{
		archive.AddString("class", kRemoteClassName);
		CHECK(instantiate_object(&archive, &id) == NULL);
		CHECK(id == B_LAUNCH_FAILED_APP_NOT_FOUND);
	}

	SECTION("class in an add-on that is loaded")
	{
		ScopedAddOn addOn;
		REQUIRE(addOn.Image() >= 0);

		archive.AddString("class", kRemoteClassName);
		CHECK(instantiate_object(&archive, &id) == NULL);
		CHECK(id == B_LAUNCH_FAILED_APP_NOT_FOUND);
	}
}


TEST_CASE("instantiate_object: class in a loaded add-on",
	"[BArchivable][support]")
{
	UnloadRemoteAddOn();
	ScopedAddOn addOn;
	REQUIRE(addOn.Image() >= 0);

	image_id id = B_OK;
	BMessage archive;
	archive.AddString("class", kRemoteClassName);

	BArchivable* object = instantiate_object(&archive, &id);
	CHECK(object != NULL);
	CHECK(id == addOn.Image());

	delete object;
}


TEST_CASE("instantiate_object: class in a loaded add-on, by signature",
	"[BArchivable][support][needs-registrar]")
{
	UnloadRemoteAddOn();
	ScopedAddOn addOn;
	REQUIRE(addOn.Image() >= 0);
	ScopedSignature signature;

	image_id id = B_OK;
	BMessage archive;
	archive.AddString("class", kRemoteClassName);
	archive.AddString("add_on", kRemoteSignature);

	BArchivable* object = instantiate_object(&archive, &id);
	CHECK(object != NULL);
	CHECK(id == addOn.Image());

	delete object;
}


TEST_CASE("instantiate_object: add-on loaded by signature",
	"[BArchivable][support][needs-registrar]")
{
	UnloadRemoteAddOn();
	ScopedSignature signature;

	image_id id = B_OK;
	BMessage archive;
	archive.AddString("add_on", kRemoteSignature);

	SECTION("class the add-on defines")
	{
		archive.AddString("class", kRemoteClassName);
		BArchivable* object = instantiate_object(&archive, &id);
		CHECK(object != NULL);
		CHECK(id > 0);
		delete object;
	}

	SECTION("class the add-on does not define")
	{
		archive.AddString("class", kInvalidClassName);
		CHECK(instantiate_object(&archive, &id) == NULL);
		CHECK(id == B_NAME_NOT_FOUND);
	}

	// The add-on stays loaded either way
	UnloadRemoteAddOn();
}


TEST_CASE("instantiate_object: local class", "[BArchivable][support]")
{
	image_id id = B_OK;
	BMessage archive;
	archive.AddString("class", kLocalClassName);

	BArchivable* object = instantiate_object(&archive, &id);
	CHECK(dynamic_cast<TIOTest*>(object) != NULL);

	// The class lives in this executable, so its image is reported
	CHECK(id > 0);

	delete object;
}
