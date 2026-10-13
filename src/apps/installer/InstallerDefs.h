/*
 * Copyright 2013, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#ifndef INSTALLER_DEFS_H
#define INSTALLER_DEFS_H


#include <SupportDefs.h>


static const uint32 MSG_STATUS_MESSAGE = 'iSTM';
static const uint32 MSG_INSTALL_FINISHED = 'iIFN';
static const uint32 MSG_RESET = 'iRSI';
static const uint32 MSG_START_INSTALLING = 'eSRT';
static const uint32 MSG_INSTALL_BOOTLOADER = 'iBLR';
static const uint32 MSG_BOOTLOADER_SETUP = 'iBLS';
static const uint32 MSG_BOOTLOADER_INSTALL_DONE = 'iBLD';
static const uint32 MSG_LAUNCH_DRIVE_SETUP = 'iLDS';
static const uint32 MSG_TARGET_SETUP = 'iTSP';
static const uint32 MSG_PLAN_READY = 'iPLR';

// Sentinel target ids: real partition ids are >= 0, the in-place item -2.
static const int32 kPlannedPartitionId = -3;


#endif	// INSTALLER_DEFS_H
