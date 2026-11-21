/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "PackageErrors.h"

#include <Catalog.h>

#include "PackageManagerDefs.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PackageErrors"


BString
PackageErrorText(int32 error)
{
	switch (error) {
		case kAptErrorLockHeld:
			return B_TRANSLATE("Another program is using the package "
				"system. Close it and try again.");
		case kAptErrorNoPrivilege:
			return B_TRANSLATE("Authentication failed or was cancelled. "
				"Nothing was changed.");
		case kAptErrorBrokenDeps:
			return B_TRANSLATE("These changes cannot be applied without "
				"breaking other packages.");
		case kAptErrorNetwork:
			return B_TRANSLATE("Could not reach the software repositories.");
		case kAptErrorSignature:
			return B_TRANSLATE("A repository failed its signature check. "
				"Nothing was installed.");
		case kAptErrorDiskFull:
			return B_TRANSLATE("There is not enough free disk space.");
		case kAptErrorInterrupted:
			return B_TRANSLATE("The operation was interrupted.");
		case kAptErrorBatchTooLarge:
			return B_TRANSLATE("Too many packages are marked for a single "
				"action. Apply changes in smaller batches.");
		default:
			return B_TRANSLATE("The operation failed.");
	}
}
