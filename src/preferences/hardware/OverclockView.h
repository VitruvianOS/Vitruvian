/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _OVERCLOCK_VIEW_H
#define _OVERCLOCK_VIEW_H


#include <Messenger.h>
#include <String.h>
#include <StringList.h>
#include <View.h>

#include <TemperatureSensors.h>

#include <Overclock.h>


class BBox;
class BButton;
class BCheckBox;
class BMenuField;
class BMessageRunner;
class BStringView;
class BTextControl;
class OverclockView;


struct OverclockHelperJob {
	BMessenger		target;
	BStringList		arguments;
	bool			spdRequest;
};


class OverclockView : public BView {
public:
								OverclockView();
	virtual						~OverclockView();

	virtual	void				AttachedToWindow();
	virtual	void				Show();
	virtual	void				MessageReceived(BMessage* message);

			void				HelperDone(bool spdOnly);

private:
			void				_HideCpuControls();
			void				_LoadAll();
			void				_PopulateCpu();
			void				_PopulateMemory();
			void				_PopulateFreqMenu(BMenuField* field,
									int64 minKHz, int64 maxKHz,
									int64 selectKHz, bool isMinimum);
			void				_MarkItem(BMenuField* field,
									const BString& label);
			void				_Tick();
			void				_SetControlsEnabled(bool enabled);
			void				_StartHelper(const char* subcommand,
									bool spdOnly = false);
			void				_Apply(bool persist);
			status_t			_SpdDone(const BMessage& message);

	OverclockBackend*			fBackend;
	bool						fHaveCpuControls;
	TemperatureSensorReader		fReader;
	BMessageRunner*				fTimer;
	bool						fBusy;
	BButton*					fReadSpdButton;

	BStringView*				fDriverString;
	BMenuField*					fGovernorMenu;
	BStringView*				fCurrentFreqString;
	BMenuField*					fMinFreqMenu;
	BMenuField*					fMaxFreqMenu;
	BCheckBox*					fBoostCheckBox;
	BMenuField*					fEppMenu;
	BTextControl*				fPowerLongText;
	BTextControl*				fPowerShortText;
	BStringView*				fPowerMaxString;
	BStringView*				fCpuTempString;

	BMenuField*					fArmFreqMenu;
	BTextControl*				fOverVoltageText;
	BMenuField*					fSdramFreqMenu;
	BMenuField*					fGpuFreqMenu;
	BStringView*				fRpiNote;

	BStringView*				fDimmStrings[4];
	BStringView*				fMemoryNote;
	BStringView*				fRamTempString;

	BBox*						fGpuBox;

	BButton*					fApplyNowButton;
	BButton*					fApplyBootButton;
	BButton*					fRevertButton;

	BStringView*				fStatusString;
};


#endif	// _OVERCLOCK_VIEW_H
