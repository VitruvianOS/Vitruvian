// Copyright 2026, Dario Casalinuovo. All rights reserved.

// Host copy of the FSUtils.cpp system-folder predicates; V\OS binaries
// need /dev/nexus, so they cannot run here. From the tree root:
//   g++ -std=c++11 -Wall -o system_folder_policy_test src/tests/host/system_folder_policy_test.cpp
//   ./system_folder_policy_test

#include <stdio.h>
#include <string.h>

#include <string>

// find_directory() results, see libroot2/fs/find_directory.cpp
static const char* kSystemDir = "/system";
static const char* kSystemDataDir = "/system/data";
static const char* kUserDir = "/home/vos";
static const char* kSystemTempDir = "/system/var/tmp";
static const char* kDesktopDir = "/home/vos/Desktop";


// BDirectory::Contains: path prefix on a component boundary
static bool
DirectoryMatchesOrContains(const char* entryPath, const char* dirPath)
{
	size_t dirLen = strlen(dirPath);
	if (strncmp(dirPath, entryPath, dirLen) != 0)
		return false;
	return entryPath[dirLen] == '\0' || entryPath[dirLen] == '/';
}


static bool
DirectoryMatches(const char* entryPath, const char* dirPath)
{
	return strcmp(entryPath, dirPath) == 0;
}


// /system without its data subtree
static bool
IsProtectedSystemEntry(const char* entryPath)
{
	return DirectoryMatchesOrContains(entryPath, kSystemDir)
		&& !DirectoryMatchesOrContains(entryPath, kSystemDataDir);
}


// warning ConfirmChangeIfWellKnownDirectory shows, "" if none; useFix=false
// is the old rule
static std::string
ConfirmWarning(const char* entryPath, bool useFix)
{
	bool systemEntry = useFix
		? IsProtectedSystemEntry(entryPath)
		: DirectoryMatchesOrContains(entryPath, kSystemDir);

	if ((!systemEntry
			&& !DirectoryMatchesOrContains(entryPath, kUserDir))
		|| DirectoryMatchesOrContains(entryPath, kSystemTempDir))
		return "";

	if (systemEntry)
		return "system";
	if (DirectoryMatches(entryPath, kUserDir))
		return "home";
	return "";
}


// true when the drop is forced to a copy
static bool
IsSystemDragOutOfSystem(const char* srcPath, const char* destPath,
	bool useFix)
{
	if (DirectoryMatchesOrContains(destPath, kSystemDir))
		return false;

	if (useFix)
		return IsProtectedSystemEntry(srcPath);
	return DirectoryMatchesOrContains(srcPath, kSystemDir);
}


struct TestCase {
	const char* name;
	const char* entry;
};


static const TestCase kCases[] = {
	{ "Samples file",	"/system/data/samples/loop.mp4" },
	{ "Samples folder",	"/system/data/samples" },
	{ "system binary",	"/system/bin/vos-shell" },
	{ "system library",	"/system/lib/libc.so" },
	{ "home document",	"/home/vos/Documents/notes.txt" },
	{ "home folder",		"/home/vos" },
	{ "Desktop",		"/home/vos/Desktop" }
};


static int gFailures = 0;


static void
Expect(const char* what, bool got, bool want)
{
	if (got == want)
		return;
	printf("FAIL: %s: got %s, want %s\n", what,
		got ? "true" : "false", want ? "true" : "false");
	gFailures++;
}


static void
ExpectString(const char* what, const std::string& got, bool wantWarning)
{
	bool isWarning = !got.empty();
	if (isWarning == wantWarning)
		return;
	printf("FAIL: %s: got \"%s\"\n", what, got.c_str());
	gFailures++;
}


int
main()
{
	printf("%-16s %-30s %-14s %-14s %s\n", "case", "entry",
		"warn before", "warn after", "drag out before/after");

	for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
		const TestCase& c = kCases[i];
		std::string before = ConfirmWarning(c.entry, false);
		std::string after = ConfirmWarning(c.entry, true);
		bool dragBefore = IsSystemDragOutOfSystem(c.entry, kDesktopDir,
			false);
		bool dragAfter = IsSystemDragOutOfSystem(c.entry, kDesktopDir,
			true);

		printf("%-16s %-30s %-14s %-14s %s/%s\n", c.name, c.entry,
			before.empty() ? "no" : before.c_str(),
			after.empty() ? "no" : after.c_str(),
			dragBefore ? "copy" : "move",
			dragAfter ? "copy" : "move");
		printf("    inputs: find_directory(B_SYSTEM_DIRECTORY)=%s "
			"find_directory(B_SYSTEM_DATA_DIRECTORY)=%s "
			"find_directory(B_USER_DIRECTORY)=%s\n",
			kSystemDir, kSystemDataDir, kUserDir);
		printf("    contains(system)=%d contains(system/data)=%d "
			"contains(user)=%d\n",
			DirectoryMatchesOrContains(c.entry, kSystemDir),
			DirectoryMatchesOrContains(c.entry, kSystemDataDir),
			DirectoryMatchesOrContains(c.entry, kUserDir));
	}

	// old rule warned on Samples; /system must still warn
	ExpectString("samples warn before",
		ConfirmWarning(kCases[0].entry, false), true);
	ExpectString("samples warn after",
		ConfirmWarning(kCases[0].entry, true), false);
	ExpectString("samples folder warn after",
		ConfirmWarning(kCases[1].entry, true), false);
	ExpectString("system binary warn after",
		ConfirmWarning(kCases[2].entry, true), true);
	ExpectString("system library warn after",
		ConfirmWarning(kCases[3].entry, true), true);
	ExpectString("home document warn after",
		ConfirmWarning(kCases[4].entry, true), false);
	ExpectString("home folder warn after",
		ConfirmWarning(kCases[5].entry, true), true);

	Expect("samples drag before",
		IsSystemDragOutOfSystem(kCases[0].entry, kDesktopDir, false),
		true);
	Expect("samples drag after",
		IsSystemDragOutOfSystem(kCases[0].entry, kDesktopDir, true),
		false);
	Expect("system binary drag after",
		IsSystemDragOutOfSystem(kCases[2].entry, kDesktopDir, true),
		true);
	Expect("home document drag after",
		IsSystemDragOutOfSystem(kCases[4].entry, kDesktopDir, true),
		false);
	Expect("into system is not an out-of-system drag",
		IsSystemDragOutOfSystem(kCases[4].entry, kCases[2].entry, true),
		false);

	if (gFailures == 0)
		printf("\nPASS: 0 failures\n");
	else
		printf("\nFAIL: %d failures\n", gFailures);
	return gFailures == 0 ? 0 : 1;
}
