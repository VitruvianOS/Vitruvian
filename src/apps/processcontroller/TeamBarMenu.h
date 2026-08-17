/*
 * Copyright 2000, Georges-Edouard Berenger. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _TEAM_BAR_MENU_H_
#define _TEAM_BAR_MENU_H_


#include "Utilities.h"

#include <Menu.h>


class TeamBarMenuItem;

typedef struct {
	TeamBarMenuItem*	item;
	int					index;
} TRecycleItem;


class TeamBarMenu : public BMenu {
	public:
						TeamBarMenu(const char* title, info_pack* infos, int32 teamCount,
							bool kernelThreads = false);
		virtual			~TeamBarMenu();

		virtual	void	Draw(BRect updateRect);
		virtual	void	Pulse();

		team_id*		fTeamList;
		int				fTeamCount;
		TRecycleItem*	fRecycleList;
		int				fRecycleCount;
		bigtime_t		fLastTotalTime;
		bool			fFirstShow;
		bool			fKernelThreads;

		// The "Kernel Team" submenu only pulses while it's visible, so the
		// top-level menu tracks kernel usage itself rather than reading it
		// back from those items.
		bigtime_t		fKernelPrevUsage;
		bigtime_t		fKernelLastTime;

		bigtime_t		SumKernelUsage();
};

#endif // _TEAM_BAR_MENU_H_
