/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef SERVICES_DEFS_H
#define SERVICES_DEFS_H

#include <SupportDefs.h>


#define kServicesSignature "application/x-vnd.vos-Services"


// Worker (BLooper) requests.
enum {
	kMsgSetManager		= 'svsm',	// "system" | "user"
	kMsgReload			= 'svrl',
	kMsgStartUnit		= 'svst',
	kMsgStopUnit		= 'svsp',
	kMsgRestartUnit		= 'svrs',
	kMsgEnableUnit		= 'sven',
	kMsgDisableUnit		= 'svds',
	kMsgLoadJournal		= 'svjl',
	kMsgShutdown		= 'svsd'
};

// Worker replies / bus signals delivered to the window.
enum {
	kMsgUnitsReady		= 'svLR',
	kMsgUnitsBatch		= 'svBT',
	kMsgUnitUpdated		= 'svUP',
	kMsgUnitRemoved		= 'svRM',
	kMsgJournalReady	= 'svJR',
	kMsgActionDone		= 'svAD',
	kMsgError			= 'svER'
};

// UI-side messages.
enum {
	kMsgFilterChanged	= 'svfc',
	kMsgAllUnitsToggled	= 'svat',
	kMsgManagerChanged	= 'svmc',
	kMsgUnitSelected	= 'svus',
	kMsgStart			= 'svgo',
	kMsgStop			= 'svno',
	kMsgRestart			= 'svrn',
	kMsgEnable			= 'svne',
	kMsgDisable			= 'svde'
};


enum unit_filter {
	kFilterServicesOnly	= 0,
	kFilterAllUnits		= 1
};


#endif // SERVICES_DEFS_H
