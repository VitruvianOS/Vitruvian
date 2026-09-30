/*
 * Copyright 2001-2006, Haiku.
 * Copyright 2026, Dario Casalinuovo.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Rafael Romo
 *		Stefano Ceccherini (burton666@libero.it)
 */
#ifndef CONSTANTS_H
#define CONSTANTS_H


#include <ScreenDefs.h>
#include <SupportDefs.h>


// Messages
static const uint32 WORKSPACE_CHECK_MSG = 'wchk';
static const uint32 BUTTON_LAUNCH_BACKGROUNDS_MSG = 'blbk';
static const uint32 BUTTON_DEFAULTS_MSG = 'bdef';
static const uint32 BUTTON_REVERT_MSG = 'brev';
static const uint32 BUTTON_APPLY_MSG = 'bapl';
static const uint32 BUTTON_DONE_MSG = 'bdon';
static const uint32 BUTTON_KEEP_MSG = 'bkep';
static const uint32 BUTTON_UNDO_MSG = 'bund';
static const uint32 POP_RESOLUTION_MSG = 'pres';
static const uint32 POP_COLORS_MSG = 'pclr';
static const uint32 POP_REFRESH_MSG = 'prfr';
static const uint32 POP_OTHER_REFRESH_MSG = 'porf';
static const uint32 POP_COMBINE_DISPLAYS_MSG = 'pcdi';
static const uint32 POP_SWAP_DISPLAYS_MSG = 'psdi';
static const uint32 POP_USE_LAPTOP_PANEL_MSG = 'pulp';
static const uint32 POP_TV_STANDARD_MSG = 'ptvs';
static const uint32 POP_ROTATION_MSG = 'prot';
static const uint32 POP_REFLECTION_MSG = 'prfl';
//static const uint32 UPDATE_DESKTOP_COLOR_MSG = 'udsc';
	// This is now defined in headers/private/preferences/ScreenDefs.h
static const uint32 UPDATE_DESKTOP_MSG = 'udsk';
static const uint32 SLIDER_MODIFICATION_MSG = 'sldm';
static const uint32 SLIDER_INVOKE_MSG = 'sldi';
static const uint32 SLIDER_BRIGHTNESS_MSG = 'brig';
static const uint32 SET_CUSTOM_REFRESH_MSG = 'scrf';
static const uint32 DIM_COUNT_MSG = 'scrf';
static const uint32 MAKE_INITIAL_MSG = 'mkin';
static const uint32 kMsgWorkspaceLayoutChanged = 'wslc';
static const uint32 kMsgWorkspaceColumnsChanged = 'wscc';
static const uint32 kMsgWorkspaceRowsChanged = 'wsrc';

// Output enable/disable per monitor
static const uint32 POP_OUTPUT_TOGGLE_MSG = 'potg';


// Profile save/delete
static const uint32 BUTTON_PROFILE_SAVE_MSG = 'bpsv';
static const uint32 BUTTON_PROFILE_DELETE_MSG = 'bpdl';
static const uint32 POP_PROFILE_SELECT_MSG = 'pps';
static const uint32 BUTTON_PROFILE_NAME_MSG = 'bpnm';

// Constants
extern const char* kBackgroundsSignature;

static const int32 gMinRefresh = 45;	// This is the minimum selectable refresh
static const int32 gMaxRefresh = 140;	// This is the maximum selectable refresh

#endif	/* CONSTANTS_H */
