/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef PACKAGE_INFO_H
#define PACKAGE_INFO_H

#include <String.h>

#include "PackageManagerDefs.h"


class PackageInfo {
public:
								PackageInfo(const char* name);
								~PackageInfo();

			const BString&		Name() const
									{ return fName; }
			const BString&		Version() const
									{ return fVersion; }
			const BString&		CandidateVersion() const
									{ return fCandidateVersion; }
			const BString&		Architecture() const
									{ return fArchitecture; }
			const BString&		Section() const
									{ return fSection; }
			const BString&		Category() const
									{ return fCategory; }
			const BString&		Summary() const
									{ return fSummary; }
			const BString&		Description() const
									{ return fDescription; }
			const BString&		Depends() const
									{ return fDepends; }
			off_t				InstalledSize() const
									{ return fInstalledSize; }
			off_t				DownloadSize() const
									{ return fDownloadSize; }
			package_state		State() const
									{ return fState; }
			package_channel		Channel() const
									{ return fChannel; }
			package_mark		Mark() const
									{ return fMark; }
			bool				HasDetails() const
									{ return fHasDetails; }

			void				SetVersion(const char* version);
			void				SetCandidateVersion(const char* version);
			void				SetArchitecture(const char* arch);
			void				SetSection(const char* section);
			void				SetCategory(const char* category);
			void				SetSummary(const char* summary);
			void				SetDescription(const char* description);
			void				SetDepends(const char* depends);
			void				SetInstalledSize(off_t size);
			void				SetDownloadSize(off_t size);
			void				SetState(package_state state);
			void				SetChannel(package_channel channel);
			void				SetMark(package_mark mark);
			void				SetHasDetails(bool hasDetails);

			const char*			ChannelLabel() const;

private:
			BString				fName;
			BString				fVersion;
			BString				fCandidateVersion;
			BString				fArchitecture;
			BString				fSection;
			BString				fCategory;
			BString				fSummary;
			BString				fDescription;
			BString				fDepends;
			off_t				fInstalledSize;
			off_t				fDownloadSize;
			package_state		fState;
			package_channel		fChannel;
			package_mark		fMark;
			bool				fHasDetails;
};


#endif // PACKAGE_INFO_H
