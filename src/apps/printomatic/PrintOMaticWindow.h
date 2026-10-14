/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PRINT_OMATIC_WINDOW_H
#define _PRINT_OMATIC_WINDOW_H


#include <Bitmap.h>
#include <List.h>
#include <Message.h>
#include <String.h>
#include <Window.h>

#include "libscan.h"


class BButton;
class BMenuField;
class BPopUpMenu;
class BStringView;
class BListView;
class BRadioButton;
class BTextControl;
class PageDrawView;
class ScanView;


enum {
	kMsgDeviceSelected	= 'PmDs',
	kMsgRefresh			= 'PmRf',
	kMsgPreview			= 'PmPv',
	kMsgScanPage		= 'PmSp',
	kMsgCancelScan		= 'PmCx',
	kMsgPrintCopies		= 'PmPr',
	kMsgSaveFile		= 'PmSv',
	kMsgDeletePage		= 'PmDp',
	kMsgMoveUp			= 'PmMu',
	kMsgMoveDown		= 'PmMd',
	kMsgOptionChanged	= 'PmOc',
	kMsgScanDone		= 'PmDn',
	kMsgScanFailed		= 'PmFl',
	kMsgScanProgress	= 'PmPg',
	kMsgDevicesLoaded	= 'PmDl',
	kMsgPrintersLoaded	= 'PmPl'
};


class PrintOMaticWindow : public BWindow {
public:
					PrintOMaticWindow(const BRect& frame,
						const char* scanner);

	virtual	void	MessageReceived(BMessage* message);
	virtual	bool	QuitRequested();

			void	SelectScanner(const char* name);

			// Worker thread entry; do not call from the window.
			void	_RunScan(bool preview);

private:
			void	_LoadDevices();
			void	_SelectDevice(int32 index);
			void	_LoadDeviceOptions();
			void	_UpdateOptionMenus();
			void	_UpdatePrinterMenu(const BMessage* message);
			void	_UpdateButtons();
			void	_SetStatus(const char* text);
			void	_AppendPage(BBitmap* bitmap);
			void	_RemoveSelectedPages();
			void	_MoveSelectedPage(bool up);

			void	_StartPreview();
			void	_StartScan();
			void	_CancelScan();
			void	_ScanDone(BBitmap* bitmap, bool preview);

			void	_DoPrint();
			void	_DoSave();
			void	_Notify(const char* title, const char* content);

			void	_SaveSettings();
			void	_LoadSettings();
			BMessage* _DeviceSettings(bool create);

			libscan_options _BuildOptions(bool preview);

			BPopUpMenu*		fDeviceMenu;
			BMenuField*		fDeviceField;
			BButton*		fRefreshButton;

			BPopUpMenu*		fSourceMenu;
			BMenuField*		fSourceField;
			BPopUpMenu*		fModeMenu;
			BMenuField*		fModeField;
			BPopUpMenu*		fResolutionMenu;
			BMenuField*		fResolutionField;

	BListView*		fPageList;
			BButton*		fDeletePageButton;
			BButton*		fMoveUpButton;
			BButton*		fMoveDownButton;

			BRadioButton*	fPrintRadio;
			BRadioButton*	fSaveRadio;
			BPopUpMenu*		fPrinterMenu;
			BMenuField*		fPrinterField;
			BTextControl*	fCopiesControl;
			BPopUpMenu*		fFormatMenu;
			BMenuField*		fFormatField;

			ScanView*		fPreviewView;
			PageDrawView*	fPageDrawView;

			BButton*		fPreviewButton;
			BButton*		fScanButton;
			BButton*		fCancelButton;
			BButton*		fPrintButton;
			BButton*		fSaveButton;
			BStringView*	fStatusView;

			libscan_device	fDevices[32];
			int32			fDeviceCount;
			char			fDeviceName[256];
			BMessage		fDeviceOptions;

			BList			fPages;
			bool			fBusy;
			bool			fBatchActive;
			thread_id		fScanThread;

			BMessage		fSettings;
			BMessage		fCurrentDeviceSettings;
};


#endif	// _PRINT_OMATIC_WINDOW_H
