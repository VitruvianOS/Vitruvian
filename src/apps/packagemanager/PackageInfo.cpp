/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PackageInfo.h"

#include <Catalog.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PackageInfo"


PackageInfo::PackageInfo(const char* name)
	:
	fName(name),
	fInstalledSize(0),
	fDownloadSize(0),
	fState(kPackageAvailable),
	fChannel(kChannelUnknown),
	fMark(kMarkNone),
	fHasDetails(false)
{
}


PackageInfo::~PackageInfo()
{
}


void
PackageInfo::SetVersion(const char* version)
{
	fVersion = version;
}


void
PackageInfo::SetCandidateVersion(const char* version)
{
	fCandidateVersion = version;
}


void
PackageInfo::SetArchitecture(const char* arch)
{
	fArchitecture = arch;
}


void
PackageInfo::SetSection(const char* section)
{
	fSection = section;
}


void
PackageInfo::SetCategory(const char* category)
{
	fCategory = category;
}


void
PackageInfo::SetSummary(const char* summary)
{
	fSummary = summary;
}


void
PackageInfo::SetDescription(const char* description)
{
	fDescription = description;
}


void
PackageInfo::SetDepends(const char* depends)
{
	fDepends = depends;
}


void
PackageInfo::SetInstalledSize(off_t size)
{
	fInstalledSize = size;
}


void
PackageInfo::SetDownloadSize(off_t size)
{
	fDownloadSize = size;
}


void
PackageInfo::SetState(package_state state)
{
	fState = state;
}


void
PackageInfo::SetChannel(package_channel channel)
{
	fChannel = channel;
}


void
PackageInfo::SetMark(package_mark mark)
{
	fMark = mark;
}


void
PackageInfo::SetHasDetails(bool hasDetails)
{
	fHasDetails = hasDetails;
}


const char*
PackageInfo::ChannelLabel() const
{
	switch (fChannel) {
		case kChannelStable:
			return "Stable";
		case kChannelTesting:
			return "Testing";
		case kChannelNightly:
			return "Nightly (unvetted)";
		case kChannelDebian:
			return "Debian";
		case kChannelUnknown:
			return "Unknown";
	}
	return "Unknown";
}
