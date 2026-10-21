/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#ifndef __LV2_UI_HOST_H__
#define __LV2_UI_HOST_H__


#include <atomic>

#include <Locker.h>
#include <Looper.h>
#include <MessageRunner.h>
#include <String.h>

typedef struct SuilHostImpl SuilHost;
typedef struct SuilInstanceImpl SuilInstance;
typedef struct LilvUIImpl LilvUI;

class BMediaPluginAdapter;


// Hosts an LV2 plugin's own UI through suil: an external UI (the plugin
// manages its window; we call show, hide and idle) is preferred, then an
// X11UI. All suil calls run on this looper's thread. Control port writes
// from the UI land in the adapter; parameter changes are pushed back as
// port events.
class Lv2UIHost : public BLooper {
public:
								Lv2UIHost(BMediaPluginAdapter* owner);
	virtual						~Lv2UIHost();

			void				RunUiThread();

			bool				HasSupportedUi() const;
			status_t			Open(uint32 parentWindow);
			void				Close();

			// Any thread; forwarded to the UI as a control port event.
			void				PortEvent(uint32 portIndex, float value);

	virtual	void				MessageReceived(BMessage* message);

private:
			enum {
				kMsgUiOpen		= 'UIop',
				kMsgUiClose		= 'UIcl',
				kMsgUiIdle		= 'UIid',
				kMsgUiPortEvent	= 'UIpe'
			};

			status_t			_HandleOpen(BMessage* message);
			void				_HandleClose();
			void				_HandleIdle();
			void				_HandlePortEvent(BMessage* message);

	static	void				_WriteThunk(void* controller,
									uint32 portIndex, uint32 bufferSize,
									uint32 protocol, const void* buffer);
	static	uint32				_IndexThunk(void* controller,
									const char* symbol);
			void				_UiPortWritten(uint32 portIndex,
									float value);

			BMediaPluginAdapter*	fOwner;
			SuilHost*			fSuilHost;
			SuilInstance*		fInstance;
			BMessageRunner*		fIdleRunner;
			uint32				fParentWindow;
			std::atomic<bool>	fOpen;
};


// Picks the UI to host for plugin: external (ui:showInterface) first,
// then an embeddable X11UI suil can wrap. NULL when none is supported.
// outTypeUri receives the UI's type URI as a world-owned string.
const LilvUI*	Lv2SelectUi(void* world, const void* plugin,
				bool* outExternal, const char** outTypeUri);


#endif	// __LV2_UI_HOST_H__
