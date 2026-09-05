/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <package/PackageVersion.h>

#include "AptCacheAdapter.h"


VPackageVersion::VPackageVersion()
{
}


VPackageVersion::VPackageVersion(const char* version)
	:
	fVersion(version)
{
}


VPackageVersion::VPackageVersion(const VPackageVersion& other)
	:
	fVersion(other.fVersion)
{
}


VPackageVersion&
VPackageVersion::operator=(const VPackageVersion& other)
{
	fVersion = other.fVersion;
	return *this;
}


void
VPackageVersion::SetTo(const char* version)
{
	fVersion = version;
}


int
VPackageVersion::Compare(const VPackageVersion& other) const
{
	if (fVersion == other.fVersion)
		return 0;

	// A per-call adapter is wasteful but harmless; batch before bulk sorts.
	AptCacheAdapter adapter;
	return adapter.CompareVersions(fVersion.String(),
		other.fVersion.String());
}


bool
VPackageVersion::operator==(const VPackageVersion& other) const
{
	return Compare(other) == 0;
}


bool
VPackageVersion::operator!=(const VPackageVersion& other) const
{
	return Compare(other) != 0;
}


bool
VPackageVersion::operator<(const VPackageVersion& other) const
{
	return Compare(other) < 0;
}


bool
VPackageVersion::operator<=(const VPackageVersion& other) const
{
	return Compare(other) <= 0;
}


bool
VPackageVersion::operator>(const VPackageVersion& other) const
{
	return Compare(other) > 0;
}


bool
VPackageVersion::operator>=(const VPackageVersion& other) const
{
	return Compare(other) >= 0;
}
