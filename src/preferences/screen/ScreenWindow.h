/*
 * Copyright 2001-2014 Haiku, Inc. All rights reserved.
 * Copyright 2026, Dario Casalinuovo.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stefano Ceccherini, burton666@libero.it
 *		Axel Dörfler, axeld@pinc-software.de
 *		Thomas Kurschel
 *		Rafael Romo
 *		John Scipione, jscipione@gmail.com
 */
#ifndef SCREEN_WINDOW_H
#define SCREEN_WINDOW_H


#include <Window.h>
#include <CheckBox.h>
#include <String.h>

#include "ScreenMode.h"


class BBox;
class BButton;
class BMessageRunner;
class BPopUpMenu;
class BMenuField;
class BSlider;
class BSpinner;
class BStringView;

class AlertWindow;
class ArrangementView;
class RefreshWindow;
class MonitorView;
class ScreenSettings;


class ScreenWindow : public BWindow {
public:
							ScreenWindow(ScreenSettings *settings);
	virtual					~ScreenWindow();

	virtual	bool			QuitRequested();
	virtual	void			MessageReceived(BMessage *message);
	virtual	void			WorkspaceActivated(int32 ws, bool state);
	virtual	void			ScreenChanged(BRect frame, color_space mode);

private:
			void			_BuildSupportedColorSpaces();

			void			_CheckApplyEnabled();
			void			_CheckResolutionMenu();
			void			_CheckColorMenu();
			void			_CheckRefreshMenu();

			void			_MarkReflectionItem(int32 reflection);
			void			_UpdateActiveMode();
			void			_UpdateActiveMode(int32 workspace);
			void			_UpdateWorkspaceButtons();
			void			_UpdateRefreshControl();
			void			_UpdateMonitorView();
			void			_UpdateControls();
			void			_UpdateTemperatureControls();
			void			_MarkTemperaturePreset(float kelvin);
			void			_UpdateOriginal();
			void			_UpdateMonitor();
			void			_UpdateColorLabel();
			screen_id		_ScreenID() const;
			int32			_CountResolutions(uint16& maxWidth,
								uint16& maxHeight);
			void			_FillModeMenus();
			void			_UpdateBrightnessControl();
			void			_ReloadScreen(int32 id);
			void			_RefreshOutputs(bool force);
			void			_SelectOutput(const char* name);
			void			_SetModeControlsEnabled(bool enabled);
			void			_UpdateOutputControls();
			bool			_BeginDisplayChange();
			void			_EndDisplayChange(bool wasPending,
								status_t status);
			status_t		_SetMirrored(bool mirrored);
			void			_Identify();
			void			_UpdateProfileMenu();

			void			_Apply();

			void			_BeginScreenChange();
			void			_ShowChangeAlert();
			void			_DropScreenChange();
			void			_RevertScreenChange();
			void			_KeepScreenChange();

			status_t		_WriteVesaModeFile(const screen_mode& mode) const;
			bool			_IsVesa() const { return fIsVesa; }

			status_t		_SaveProfile(const char* name);
			status_t		_DeleteProfile(const char* name);
			status_t		_ApplyProfile(const char* name);

private:
			ScreenSettings*	fSettings;
			bool			fIsVesa;
			bool			fBootWorkspaceApplied;

			BBox*			fScreenBox;
			BStringView*	fDeviceInfo;
			MonitorView*	fMonitorView;
			BMenuItem*		fAllWorkspacesItem;

			BSpinner*		fColumnsControl;
			BSpinner*		fRowsControl;

			uint32			fSupportedColorSpaces;
			BMenuItem*		fUserSelectedColorSpace;

			bool			fResolutionMatrix;
			BPopUpMenu*		fResolutionMenu;
			BMenuField*		fResolutionField;
			BPopUpMenu*		fColorsMenu;
			BMenuField*		fColorsField;
			BPopUpMenu*		fRefreshMenu;
			BMenuField*		fRefreshField;
			BMenuItem*		fOtherRefresh;

			BPopUpMenu*		fRotationMenu;
			BMenuField*		fRotationField;
			BPopUpMenu*		fReflectionMenu;
			BMenuField*		fReflectionField;

			BSlider*		fBrightnessSlider;

			BMenuField*		fTemperatureField;
			BPopUpMenu*		fTemperatureMenu;
			float			fTemperature;
			bool			fTemperatureSupported;
			float			fOriginalTemperature;

			BBox*			fDisplaysBox;
			ArrangementView*	fArrangement;
			BCheckBox*		fUseOutputBox;
			BCheckBox*		fPrimaryBox;
			BCheckBox*		fMirrorBox;
			BButton*		fIdentifyButton;

			BPopUpMenu*		fProfileMenu;
			BMenuField*		fProfileField;
			BButton*		fSaveProfileButton;
			BButton*		fDeleteProfileButton;

			BButton*		fDefaultsButton;
			BButton*		fApplyButton;
			BButton*		fRevertButton;

			ScreenMode		fScreenMode;
			ScreenMode		fUndoScreenMode;
				// screen modes for all workspaces

			screen_mode		fActive, fSelected, fOriginal;
				// screen modes for the current workspace

			uint32			fOriginalWorkspacesColumns;
			uint32			fOriginalWorkspacesRows;
			float			fOriginalBrightness;
			int32			fOriginalRotation;
			int32			fOriginalReflection;
			bool			fModified;

			AlertWindow*	fAlertWindow;
				// countdown for the pending change; NULL when none

			// The output the controls act on, by connector name, and the
			// screen it is on (a client screen id).
			BString			fSelectedOutput;
			int32			fScreenID;
			BString			fOutputsSignature;
			bool			fBrightnessShown;
			bool			fDisplaysShown;
			BMessageRunner*	fPollRunner;

	static	const int32		kMaxOutputs = 8;
};

#endif	/* SCREEN_WINDOW_H */
