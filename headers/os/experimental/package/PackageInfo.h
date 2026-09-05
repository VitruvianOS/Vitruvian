/*
 * Copyright 2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _V_PACKAGE_INFO_H
#define _V_PACKAGE_INFO_H


#include <ObjectList.h>
#include <Referenceable.h>
#include <String.h>

#include <package/DependencyExpression.h>
#include <package/PackageDefs.h>
#include <package/PackageVersion.h>


class VPackageInfo;
typedef BReference<VPackageInfo> VPackageInfoRef;


// Read-only model; BReferenceable so a VPackageInfoRef can outlive
// the query that produced it.
class VPackageInfo : public BReferenceable {
public:
								VPackageInfo(const char* name);
	virtual						~VPackageInfo();

			const BString&		Name() const
									{ return fName; }
			const BString&		Architecture() const
									{ return fArchitecture; }
			const VPackageVersion& InstalledVersion() const
									{ return fInstalledVersion; }
			const VPackageVersion& CandidateVersion() const
									{ return fCandidateVersion; }
			const BString&		Section() const
									{ return fSection; }
			const BString&		Summary() const
									{ return fSummary; }
			const BString&		Description() const
									{ return fDescription; }
			off_t				InstalledSize() const
									{ return fInstalledSize; }
			off_t				DownloadSize() const
									{ return fDownloadSize; }
			v_package_state		State() const
									{ return fState; }
			v_package_channel	Channel() const
									{ return fChannel; }

			const BObjectList<VDependencyExpression, true>& Depends() const
									{ return fDepends; }
			const BObjectList<VDependencyExpression, true>& Recommends() const
									{ return fRecommends; }
			const BObjectList<VDependencyExpression, true>& Breaks() const
									{ return fBreaks; }
			const BObjectList<VDependencyExpression, true>& Conflicts() const
									{ return fConflicts; }
			const BObjectList<VDependencyExpression, true>& Provides() const
									{ return fProvides; }

			void				SetArchitecture(const char* arch);
			void				SetInstalledVersion(const char* version);
			void				SetCandidateVersion(const char* version);
			void				SetSection(const char* section);
			void				SetSummary(const char* summary);
			void				SetDescription(const char* description);
			void				SetInstalledSize(off_t size);
			void				SetDownloadSize(off_t size);
			void				SetState(v_package_state state);
			void				SetChannel(v_package_channel channel);

			// Each replaces the whole list, parsing raw field text.
			void				SetDepends(const char* field);
			void				SetRecommends(const char* field);
			void				SetBreaks(const char* field);
			void				SetConflicts(const char* field);
			void				SetProvides(const char* field);

private:
			BString				fName;
			BString				fArchitecture;
			VPackageVersion		fInstalledVersion;
			VPackageVersion		fCandidateVersion;
			BString				fSection;
			BString				fSummary;
			BString				fDescription;
			off_t				fInstalledSize;
			off_t				fDownloadSize;
			v_package_state		fState;
			v_package_channel	fChannel;
			BObjectList<VDependencyExpression, true> fDepends;
			BObjectList<VDependencyExpression, true> fRecommends;
			BObjectList<VDependencyExpression, true> fBreaks;
			BObjectList<VDependencyExpression, true> fConflicts;
			BObjectList<VDependencyExpression, true> fProvides;
};


#endif	// _V_PACKAGE_INFO_H
