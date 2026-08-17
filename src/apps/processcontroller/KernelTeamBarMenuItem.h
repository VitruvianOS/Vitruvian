/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_TEAM_BAR_MENU_ITEM_H_
#define _KERNEL_TEAM_BAR_MENU_ITEM_H_


#include "TeamBarMenuItem.h"


// Aggregate CPU bar for the "Kernel Team" row. Fed the per-pulse kernel
// usage sum computed by TeamBarMenu::Pulse() rather than scanning teams
// itself (see TeamBarMenu.h on the cost of a standalone sweep).
class KernelTeamBarMenuItem : public TeamBarMenuItem {
public:
					KernelTeamBarMenuItem(BMenu* menu, BBitmap* icon);

	void			BarUpdate(bigtime_t kernelUsageSum, bigtime_t now);

private:
	bigtime_t		fPrevUsage;
	bigtime_t		fLastTime;
	bool			fSeeded;
};

#endif // _KERNEL_TEAM_BAR_MENU_ITEM_H_
