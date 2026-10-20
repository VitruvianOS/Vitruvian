/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _CORTEX_MONITOR_H
#define _CORTEX_MONITOR_H


#include <Bitmap.h>
#include <Locker.h>
#include <Messenger.h>
#include <View.h>
#include <Window.h>

#include <media2/MediaFormat.h>


class CortexMonitorView : public BView {
public:
								CortexMonitorView(const char* name);
	virtual						~CortexMonitorView();

	virtual	void				Draw(BRect updateRect);
	virtual	void				MessageReceived(BMessage* message);

			void				SetFrame(const uint8* bits, size_t size,
									uint32 width, uint32 height);

			// BMediaRecorder buffer hook; cookie is the view.
	static	void				FrameHook(void* cookie, bigtime_t timestamp,
									void* data, size_t size,
									const BMediaFormat& format);

private:
			BLocker				fLock;
			BBitmap*			fFrame;
			bool				fUpdatePending;
};


class CortexMonitorWindow : public BWindow {
public:
								CortexMonitorWindow(const char* title,
									const BMessenger& owner);

	virtual	bool				QuitRequested();

			CortexMonitorView*	View() const { return fView; }

private:
			CortexMonitorView*	fView;
			BMessenger			fOwner;
};


#endif
