/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_TEAM_MEMORY_BAR_MENU_ITEM_H_
#define _KERNEL_TEAM_MEMORY_BAR_MENU_ITEM_H_


#include "IconMenuItem.h"


// The kernel's own footprint, not tied to a team_id: /proc/meminfo only
// gives a system-wide figure.
class KernelTeamMemoryBarMenuItem : public IconMenuItem {
	public:
		KernelTeamMemoryBarMenuItem(system_info& systemInfo, BBitmap* icon);
		virtual	void	DrawContent();
		virtual	void	GetContentSize(float* _width, float* _height);

		void			DrawBar(bool force);
		void			UpdateSituation();

	private:
		int64	fPhysicalMemory;
		int64	fFootprint;
		double	fLastFootprint;
		double	fGrenze1;
};

#endif // _KERNEL_TEAM_MEMORY_BAR_MENU_ITEM_H_
