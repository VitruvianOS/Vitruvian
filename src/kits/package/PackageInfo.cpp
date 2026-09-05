/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <package/PackageInfo.h>


VPackageInfo::VPackageInfo(const char* name)
	:
	fName(name),
	fInstalledSize(0),
	fDownloadSize(0),
	fState(V_PACKAGE_STATE_UNKNOWN),
	fChannel(V_CHANNEL_UNKNOWN),
	fDepends(4),
	fRecommends(4),
	fBreaks(4),
	fConflicts(4),
	fProvides(4)
{
}


VPackageInfo::~VPackageInfo()
{
}


void
VPackageInfo::SetArchitecture(const char* arch)
{
	fArchitecture = arch;
}


void
VPackageInfo::SetInstalledVersion(const char* version)
{
	fInstalledVersion.SetTo(version);
}


void
VPackageInfo::SetCandidateVersion(const char* version)
{
	fCandidateVersion.SetTo(version);
}


void
VPackageInfo::SetSection(const char* section)
{
	fSection = section;
}


void
VPackageInfo::SetSummary(const char* summary)
{
	fSummary = summary;
}


void
VPackageInfo::SetDescription(const char* description)
{
	fDescription = description;
}


void
VPackageInfo::SetInstalledSize(off_t size)
{
	fInstalledSize = size;
}


void
VPackageInfo::SetDownloadSize(off_t size)
{
	fDownloadSize = size;
}


void
VPackageInfo::SetState(v_package_state state)
{
	fState = state;
}


void
VPackageInfo::SetChannel(v_package_channel channel)
{
	fChannel = channel;
}


void
VPackageInfo::SetDepends(const char* field)
{
	fDepends.MakeEmpty();
	VDependencyExpression::ParseList(field, &fDepends);
}


void
VPackageInfo::SetRecommends(const char* field)
{
	fRecommends.MakeEmpty();
	VDependencyExpression::ParseList(field, &fRecommends);
}


void
VPackageInfo::SetBreaks(const char* field)
{
	fBreaks.MakeEmpty();
	VDependencyExpression::ParseList(field, &fBreaks);
}


void
VPackageInfo::SetConflicts(const char* field)
{
	fConflicts.MakeEmpty();
	VDependencyExpression::ParseList(field, &fConflicts);
}


void
VPackageInfo::SetProvides(const char* field)
{
	fProvides.MakeEmpty();
	VDependencyExpression::ParseList(field, &fProvides);
}
