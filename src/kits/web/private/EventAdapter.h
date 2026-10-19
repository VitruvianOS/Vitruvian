/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the LGPL License.
 */
#ifndef VOS_EVENT_ADAPTER_H
#define VOS_EVENT_ADAPTER_H


#include <stdint.h>
#include <wpe/wpe.h>


class BMessage;


class EventAdapter {
public:
						EventAdapter(struct wpe_view_backend* backend);
						~EventAdapter();

			void		TranslateKeyDown(const BMessage* msg);
			void		TranslateKeyUp(const BMessage* msg);
			void		TranslateMouseDown(const BMessage* msg);
			void		TranslateMouseUp(const BMessage* msg);
			void		TranslateMouseMoved(const BMessage* msg);
			void		TranslateMouseWheel(const BMessage* msg);

private:
	static uint32_t		_HaikuModsToWPE(uint32 haikuMods);
	static uint32_t		_HaikuButtonToWPE(int32 buttons);
	static uint32_t		_HaikuKeyToXKB(uint8 rawKey, const char* bytes,
							int32 numBytes);

	struct wpe_view_backend* fBackend;
	uint32_t				fPressedButton;
};


#endif // VOS_EVENT_ADAPTER_H
