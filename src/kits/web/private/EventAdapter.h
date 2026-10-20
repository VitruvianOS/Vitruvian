/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the LGPL License.
 */
#ifndef VOS_EVENT_ADAPTER_H
#define VOS_EVENT_ADAPTER_H


#include <atomic>
#include <set>
#include <stdint.h>
#ifndef HAS_WPE_PLATFORM
#include <wpe/wpe.h>
#else
#include <wpe/wpe-platform.h>
#endif


class BMessage;


class EventAdapter {
public:
#ifdef HAS_WPE_PLATFORM
						EventAdapter(WPEView* view);
#else
						EventAdapter(struct wpe_view_backend* backend);
#endif
						~EventAdapter();

			// View size in pixels; pointer coordinates are clamped to it.
			void		SetViewSize(int width, int height);

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
	static int32_t		_Clamp(float v, int size);
	static uint32_t		_LegacyModsToWPE(uint32 legacyMods);
	static uint32_t		_LegacyPointerModsToWPE(uint32 legacyMods);
	static uint32_t		_LegacyButtonToWPE(int32 buttons);
	static uint32_t		_LegacyKeyToXKB(int32 rawKey, const char* bytes,
							int32 numBytes, uint32 modifiers);

			void		_DispatchKey(uint32_t keyCode,
#ifdef HAS_WPE_PLATFORM
							uint32_t hardwareCode,
#endif
							uint32_t modifiers, bool pressed);

#ifdef HAS_WPE_PLATFORM
	WPEView*			fView;
	// The keysym and hardware keycode of every key still down; the
	// release may not map (layout switch while held) and must free
	// the same pair the press registered.
	std::set<uint64_t>	fHeldKeys;
#else
	struct wpe_view_backend* fBackend;
	std::set<uint32_t>		fHeldKeys;
#endif
	uint32_t				fPressedButton;
	std::atomic<int>		fViewWidth;
	std::atomic<int>		fViewHeight;
};


#endif // VOS_EVENT_ADAPTER_H
