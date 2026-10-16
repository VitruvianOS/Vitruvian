/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef _PANEL_MENU_H
#define _PANEL_MENU_H


#include <OS.h>
#include <Rect.h>
#include <Window.h>

#include "PanelMenuModel.h"


class BMessenger;
class PanelMenuView;
struct PanelMenuTrackData;


/*!	Browser-style panel menu (Firefox's hamburger panel, Chrome's
	three-dot menu): one popup window anchored under the toolbar
	button whose content swaps in place when a sub-page row is
	chosen, with a back arrow plus the page title in the header to
	return. Built on a plain BWindow instead of BPopUpMenu: the menu
	machine only grows BMenuItems sideways, it cannot swap a whole
	page of rows in one rectangle.
*/
class PanelMenu : public BWindow {
public:
								PanelMenu(const PanelMenuModel& model,
									const BMessenger& target);
	virtual						~PanelMenu();

			// where: screen point the panel opens under, top-left
			// aligned and clamped the way BMenu::_CalcFrame does.
			void				Go(BPoint where);

			const PanelMenuModel&	Model() const;

	virtual	bool				QuitRequested();

private:
	friend class PanelMenuView;

			void				_UpdateLayout();
			void				_SelectRow(int32_t index);
			void				_Back();

	static	int32				_TrackEntry(void* data);

			PanelMenuModel		fModel;
			BMessenger*			fTarget;
			PanelMenuView*		fView;
			sem_id				fQuitSem;
			thread_id			fTrackThread;
			BPoint				fAnchor;
};


#endif // _PANEL_MENU_H
