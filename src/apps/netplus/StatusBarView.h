/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */
#ifndef STATUS_BAR_VIEW_H
#define STATUS_BAR_VIEW_H


#include <StatusBar.h>
#include <StringView.h>
#include <View.h>


class StatusBarView : public BView {
public:
								StatusBarView(BRect frame);
	virtual						~StatusBarView();

	virtual void				AttachedToWindow();

			void				SetProgress(double progress);
			void				SetStatusText(const char* text);
			void				ClearProgress();

private:
			BStatusBar*			fProgressBar;
			BStringView*		fStatusText;
};


#endif // STATUS_BAR_VIEW_H
