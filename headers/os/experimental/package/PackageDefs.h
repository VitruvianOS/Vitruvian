/*
 * Copyright 2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PACKAGE_DEFS_H
#define _PACKAGE_DEFS_H


#include <SupportDefs.h>


// Package kit signature for messages/logging.
#define V_PACKAGE_KIT_SIGNATURE "application/x-vnd.Vitruvian-PackageKit"


enum v_package_state {
	V_PACKAGE_STATE_UNKNOWN		= 0,
	V_PACKAGE_STATE_AVAILABLE		= 1,
	V_PACKAGE_STATE_INSTALLED		= 2,
	V_PACKAGE_STATE_UPGRADABLE		= 3
};


// Debian's comparison operators as they appear in a Depends field.
enum v_dependency_operator {
	V_DEPENDENCY_OP_NONE			= 0,
	V_DEPENDENCY_OP_LT				= 1,
	V_DEPENDENCY_OP_LE				= 2,
	V_DEPENDENCY_OP_EQ				= 3,
	V_DEPENDENCY_OP_GE				= 4,
	V_DEPENDENCY_OP_GT				= 5,
	V_DEPENDENCY_OP_NE				= 6
};


// Suite names are real repository codenames; mirrors
// PackageManagerDefs.h's package_channel.
enum v_package_channel {
	V_CHANNEL_UNKNOWN				= 0,
	V_CHANNEL_STABLE				= 1,	// trixie
	V_CHANNEL_TESTING				= 2,	// trixie-testing
	V_CHANNEL_NIGHTLY				= 3,	// trixie-nightly
	V_CHANNEL_DEBIAN				= 4
};


// Carried in error replies so callers never string-match apt/dpkg
// output; kept in step with PackageManagerDefs.h's apt_error.
enum v_package_error {
	V_PACKAGE_ERROR_NONE			= 0,
	V_PACKAGE_ERROR_LOCK_HELD		= 1,
	V_PACKAGE_ERROR_BROKEN_DEPS		= 2,
	V_PACKAGE_ERROR_NETWORK			= 3,
	V_PACKAGE_ERROR_SIGNATURE		= 4,
	V_PACKAGE_ERROR_DISK_FULL		= 5,
	V_PACKAGE_ERROR_NO_PRIVILEGE	= 6,
	V_PACKAGE_ERROR_INTERRUPTED		= 7,
	V_PACKAGE_ERROR_UNKNOWN			= 99
};


// Reply vocabulary for VPackageRoster's async queries.
enum {
	kVMsgPackageListReply		= 'vpLR',
	kVMsgPackageDetailsReply	= 'vpDR',
	kVMsgPackageContentsReply	= 'vpCR',
	kVMsgAptLogReply			= 'vpGR',
	kVMsgChangelogReply			= 'vpCH',
	kVMsgQueryError				= 'vpER'
};


#endif	// _PACKAGE_DEFS_H
