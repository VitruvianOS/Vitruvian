/*
 * Copyright 2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PACKAGE_VERSION_H
#define _PACKAGE_VERSION_H


#include <String.h>


// Raw Debian string on purpose: field-splitting cannot represent
// epochs or non-numeric revisions; compare via the real dpkg code.
class VPackageVersion {
public:
								VPackageVersion();
								VPackageVersion(const char* version);
								VPackageVersion(const VPackageVersion& other);

			VPackageVersion&	operator=(const VPackageVersion& other);

			void				SetTo(const char* version);
			bool				IsEmpty() const
									{ return fVersion.Length() == 0; }

			const BString&		AsString() const
									{ return fVersion; }
			const char*			String() const
									{ return fVersion.String(); }

			// strcmp-like; delegates to the real dpkg comparator.
			int					Compare(const VPackageVersion& other) const;

			bool				operator==(const VPackageVersion& other) const;
			bool				operator!=(const VPackageVersion& other) const;
			bool				operator<(const VPackageVersion& other) const;
			bool				operator<=(const VPackageVersion& other) const;
			bool				operator>(const VPackageVersion& other) const;
			bool				operator>=(const VPackageVersion& other) const;

private:
			BString				fVersion;
};


#endif	// _PACKAGE_VERSION_H
