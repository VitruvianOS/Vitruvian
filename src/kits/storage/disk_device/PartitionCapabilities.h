/*
 * Copyright 2026, the Vitruvian project.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PARTITION_CAPABILITIES_H
#define _PARTITION_CAPABILITIES_H

#include <String.h>
#include <StringList.h>
#include <SupportDefs.h>


namespace BPrivate {


struct PartitionCapabilities {
	BString		filesystem;

	BString		create;
	BString		grow;
	BString		shrink;
	BString		move;
	BString		check;
	BString		readLabel;
	BString		writeLabel;
	BString		readUuid;
	BString		writeUuid;

	bool		onlineGrow;
	bool		onlineShrink;

	BStringList	toolsPresent;
	BStringList	toolsMissing;

	off_t		minSizeMiB;
	off_t		maxSizeMiB;

	static	status_t	Get(const char* filesystem,
							PartitionCapabilities& out);

	// Size-limits reads raw device state (root:disk 0660): pkexec, unlike Get().
	static	status_t	GetSizeLimits(const char* target,
							const char* filesystem, off_t& minSizeMiB,
							off_t& maxSizeMiB, off_t* usedMiB = NULL);
};


}

using BPrivate::PartitionCapabilities;

#endif
