/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "KernelTeamBarMenuItem.h"


KernelTeamBarMenuItem::KernelTeamBarMenuItem(BMenu* menu, BBitmap* icon)
	:
	TeamBarMenuItem(menu, NULL, -1, icon, false)
{
	fPrevUsage = 0;
	fLastTime = system_time();
	fSeeded = false;
}


void
KernelTeamBarMenuItem::BarUpdate(bigtime_t kernelUsageSum, bigtime_t now)
{
	// The first call has no previous sample to subtract; using 0 would
	// charge the whole since-boot total to one interval.
	if (!fSeeded || now <= fLastTime)
		fKernel = 0;
	else
		fKernel = double(kernelUsageSum - fPrevUsage) / double(now - fLastTime);

	if (fKernel < 0)
		fKernel = 0;

	fSeeded = true;

	fUser = 0;
	fPrevUsage = kernelUsageSum;
	fLastTime = now;
	DrawBar(false);
}
