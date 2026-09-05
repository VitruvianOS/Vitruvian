# "package" is a CMake-reserved target name (CPack), so the library
# target is "vpackage" (-> libvpackage.so) even though the kit's
# headers live under package/ and its directory is src/kits/package/.
AddOn(
	vpackage
	SHARED
	SOURCES
	AptCacheAdapter.cpp
	DependencyExpression.cpp
	PackageInfo.cpp
	PackageRoster.cpp
	PackageVersion.cpp

	LIBS be root apt-pkg
)

# libapt-pkg-dev has no pkg-config file (verified: pkg-config --list-all
# has no apt entry); link the SONAME's library name directly. ABI is
# exact-version pinned (libapt-pkg-dev Depends: libapt-pkg (= 3.0.3));
# accepted per PACKAGE-KIT-STUDY.md D1, a rebase cost not a runtime one.
target_compile_options(vpackage PRIVATE -std=c++17)

