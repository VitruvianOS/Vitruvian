/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _RENDERING_VIEW_H
#define _RENDERING_VIEW_H


#include <StringList.h>
#include <View.h>


class BButton;
class BListView;
class BMenuField;
class BStringView;
class BTextView;


// Hardware Acceleration preferences: GL renderer priority list, the GPU
// policy selector (Automatic, Power saving, High performance) and, where the
// hardware offers a choice, the display driver.
class RenderingView : public BView {
public:
								RenderingView();
	virtual						~RenderingView();

	static	RenderingView*			Create();

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_LoadList();
			void				_PopulateList();
			void				_SaveList();
			void				_UpdateButtons();
			status_t			_Move(int32 offset);

			void				_LoadPolicy();
			void				_SavePolicy();
			void				_RefreshStatus();

			void				_LoadDriver();
			void				_ApplyDriver(bool enable, bool acceptRealtime);
			void				_DriverResult(BMessage* message);

			BStringList			fPriority;

			BListView*			fListView;
			BButton*			fUpButton;
			BButton*			fDownButton;
			BButton*			fResetButton;
			BMenuField*			fPolicyField;
			BMenuField*			fDriverField;
			BStringView*		fStatusLabel;
			BTextView*			fStatusView;
};


#endif	// _RENDERING_VIEW_H
