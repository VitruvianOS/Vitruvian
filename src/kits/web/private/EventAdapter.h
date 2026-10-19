/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the LGPL License.
 */
#ifndef VOS_EVENT_ADAPTER_H
#define VOS_EVENT_ADAPTER_H


#include <set>
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

			// Synthesizes key-up events for every key still held.
			// Games keep arrow keys pressed across a focus change and
			// never see the release otherwise.
			void		ReleaseAllKeys();

private:
	static uint32_t		_LegacyModsToWPE(uint32 legacyMods);
	static uint32_t		_LegacyPointerModsToWPE(uint32 legacyMods);
	static uint32_t		_LegacyButtonToWPE(int32 buttons);
	static uint32_t		_LegacyKeyToXKB(int32 rawKey, const char* bytes,
							int32 numBytes, uint32 modifiers);

			void		_DispatchKey(uint32_t keyCode, uint32_t modifiers,
							bool pressed);

	struct wpe_view_backend* fBackend;
	uint32_t				fPressedButton;
	std::set<uint32_t>		fHeldKeys;
};


#endif // VOS_EVENT_ADAPTER_H
