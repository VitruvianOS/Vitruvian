/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WORK_STATUS_VIEW_H
#define WORK_STATUS_VIEW_H

#include <GroupView.h>


#include "TruncatingStringView.h"


class BarberPole;
class BCardLayout;
class BStatusBar;
class BView;


// BarberPole and BStatusBar share one card, swapped via SetVisibleItem()
// (not Hide()/Show()) and sized identically, so switching never resizes.
class WorkStatusView : public BGroupView {
	typedef BGroupView Inherited;
public:
								WorkStatusView();
	virtual						~WorkStatusView();

			void				SetIdle(const char* text);
			// percent < 0 means indeterminate (barber pole); package may
			// be NULL, and a dlstatus item number must never show as a name.
			void				SetProgress(int32 percent, const char* text,
									const char* package = NULL);

			void				SetPendingCount(int32 count);

private:
			BarberPole*			fBarberPole;
			BStatusBar*			fProgressBar;
			BCardLayout*		fProgressLayout;
			BView*				fProgressView;
			TruncatingStringView* fPackageText;
			TruncatingStringView* fStatusText;
			TruncatingStringView* fPendingText;
};


#endif // WORK_STATUS_VIEW_H
