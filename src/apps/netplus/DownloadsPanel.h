/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef _DOWNLOADS_PANEL_H
#define _DOWNLOADS_PANEL_H


#include <Window.h>

#include "DownloadsModel.h"


class BMessenger;
class DownloadsPanelView;


enum {
	kDownloadsSetModel = 'dlsm'
		// "id" int64 array plus parallel string/int64/int32 arrays
};


/*!	Downloads panel: one row per download with filename, size or
	progress, and the actions that apply to its state. Rows rebuild
	from the model on every change.
*/
class DownloadsPanel : public BWindow {
public:
								DownloadsPanel(const DownloadsModel& model,
									const BMessenger& target);
	virtual						~DownloadsPanel();

			void				SetModel(const DownloadsModel& model);

			// where: screen point the panel opens under, top-left
			// aligned and clamped the way BMenu::_CalcFrame does.
			void				Go(BPoint where);

	virtual	bool				QuitRequested();
	virtual void				MessageReceived(BMessage* message);

private:
	friend class DownloadsPanelView;

			DownloadsPanelView*	fView;
			BMessenger*			fTarget;
};


#endif // _DOWNLOADS_PANEL_H
