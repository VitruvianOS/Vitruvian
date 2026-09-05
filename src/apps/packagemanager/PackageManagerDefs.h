/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef PACKAGE_MANAGER_DEFS_H
#define PACKAGE_MANAGER_DEFS_H

#include <SupportDefs.h>


#define kPackageManagerSignature "application/x-vnd.Vitruvian-PackageManager"


// Mirrors the bound vos-apt-helper itself enforces per verb group.
const int32 kMaxTransactionBatchSize = 256;


enum {
	kMsgRefreshList			= 'pmrl',
	kMsgLoadDetails			= 'pmld',
	kMsgLoadLog				= 'pmll',
	kMsgSimulate			= 'pmsm',
	kMsgApplyChanges		= 'pmac',
	kMsgAptUpdate			= 'pmau',
	kMsgCancel				= 'pmcn'
};

enum {
	kMsgListReady			= 'pmLR',
	kMsgDetailsReady		= 'pmDR',
	kMsgLogReady			= 'pmGR',
	kMsgChangelogReady		= 'pmCH',
	kMsgProgress			= 'pmPG',
	kMsgSimulateReady		= 'pmSR',
	kMsgTransactionDone		= 'pmTD',
	kMsgError				= 'pmER'
};

enum {
	kMsgFilterChanged		= 'pmfc',
	kMsgSectionSelected		= 'pmss',
	kMsgStatusSelected		= 'pmst',
	kMsgPackageSelected		= 'pmps',
	kMsgMarkInstall			= 'pmmi',
	kMsgMarkRemove			= 'pmmr',
	kMsgMarkPurge			= 'pmmp',
	kMsgUnmark				= 'pmum',
	kMsgApply				= 'pmap',
	kMsgMarkAllUpgrades		= 'pmmu',
	kMsgGlyphMarkChanged	= 'pmgm',
	kMsgReloadLog			= 'pmrg',
	kMsgManageRepositories	= 'pmmR',
	kMsgClearMarks			= 'pmcm'
};


enum package_state {
	kPackageAvailable		= 0,
	kPackageInstalled		= 1,
	kPackageUpgradable		= 2
};


enum package_mark {
	kMarkNone				= 0,
	kMarkInstall			= 1,
	kMarkRemove				= 2,
	kMarkPurge				= 3
};


enum clear_marks_scope {
	kClearAllMarks			= 0,
	kClearUpgradeMarks		= 1,
	kClearInstallMarks		= 2,
	kClearRemoveMarks		= 3,
	kClearPurgeMarks		= 4
};


// The single rule for what counts as an upgrade: there is no kMarkUpgrade,
// kMarkInstall on a kPackageUpgradable package IS the upgrade. Shared by
// the mark glyph and the Clear marks submenu so they cannot drift apart.
inline bool
is_upgrade_mark(package_mark mark, package_state state)
{
	return mark == kMarkInstall && state == kPackageUpgradable;
}


enum status_filter {
	kFilterAll				= 0,
	kFilterInstalled		= 1,
	kFilterUpgradable		= 2,
	kFilterMarked			= 3
};


enum package_channel {
	kChannelUnknown			= 0,
	kChannelStable			= 1,	// trixie
	kChannelTesting			= 2,	// trixie-testing
	kChannelNightly			= 3,	// trixie-nightly
	kChannelDebian			= 4
};


enum apt_error {
	kAptErrorNone			= 0,
	kAptErrorLockHeld		= 1,
	kAptErrorBrokenDeps		= 2,
	kAptErrorNetwork		= 3,
	kAptErrorSignature		= 4,
	kAptErrorDiskFull		= 5,
	kAptErrorNoPrivilege	= 6,
	kAptErrorInterrupted	= 7,
	kAptErrorBatchTooLarge	= 8,
	kAptErrorUnknown		= 99
};


#endif // PACKAGE_MANAGER_DEFS_H
