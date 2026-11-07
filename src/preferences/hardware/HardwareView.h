/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _HARDWARE_VIEW_H
#define _HARDWARE_VIEW_H


#include <View.h>

#include <string>
#include <vector>

#include "HardwareModel.h"
#include "HwdataDatabase.h"


// Window content: machine summary header, the Plug and Play and
// Platform tabs, and the action footer (copy, export, refresh, show
// full details). Owns the udev scanner and the hwdata lookup.


class BButton;
class BFilePanel;
class BPath;
class BStringView;
class BTabView;
class DeviceTreeView;
class UdevScanner;


class HardwareView : public BView {
public:
							HardwareView();
	virtual					~HardwareView();

	virtual	void			AttachedToWindow();
	virtual	void			MessageReceived(BMessage* message);

private:
			void			_UpdateSummary();
			void			_ApplyRecords();
			void			_UpdateActions();
			void			_CopyInfo();
			void			_ShowExportPanel();
			void			_ExportReport(const BPath& path);
			void			_StartHelper();
			DeviceTreeView*
							_CurrentTab() const;

private:
			UdevScanner*	fScanner;
			Hardware::HwdataDatabase
							fNames;
			Hardware::MachineInfo
							fMachine;

			BStringView*	fSummaryView;
			BTabView*		fTabView;
			DeviceTreeView*	fPlayView;
			DeviceTreeView*	fPlatformView;

			BButton*		fCopyButton;
			BButton*		fExportButton;
			BButton*		fRefreshButton;
			BButton*		fDetailsButton;

			BFilePanel*		fFilePanel;
			bool			fHelperRunning;
			std::string		fPrivilegedText;
};


#endif	// _HARDWARE_VIEW_H
