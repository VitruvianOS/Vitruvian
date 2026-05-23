/*
 * Copyright 2026, Vitruvian Project.
 * Distributed under the terms of the MIT License.
 *
 * Fills a BKeymap from a live xkb_keymap, using evdev keycodes as indices
 * (same as VOS keyboard add-on: key field = raw evdev code, xkb code = evdev + 8).
 */


#include <Keymap.h>

#include <new>
#include <string.h>

#include <xkbcommon/xkbcommon.h>


status_t
BKeymap::PopulateFromXkb(struct xkb_keymap* xkbKeymap)
{
	if (xkbKeymap == NULL)
		return B_BAD_VALUE;

	Unset();

	static const size_t kMaxChars = 65536;
	fChars = new (std::nothrow) char[kMaxChars];
	if (fChars == NULL)
		return B_NO_MEMORY;
	fCharsSize = kMaxChars;
	memset(fChars, 0, kMaxChars);

	uint32 writePos = 1;

	memset(&fKeys, 0, sizeof(fKeys));
	fKeys.version = 3;

	/* VOS modifier key fields — evdev codes (VTkeymap.h / InterfaceDefs.h use same space) */
	fKeys.caps_key         = 58;	/* KEY_CAPSLOCK */
	fKeys.scroll_key       = 70;	/* KEY_SCROLLLOCK */
	fKeys.num_key          = 69;	/* KEY_NUMLOCK */
	fKeys.left_shift_key   = 42;	/* KEY_LEFTSHIFT */
	fKeys.right_shift_key  = 54;	/* KEY_RIGHTSHIFT */
	fKeys.left_command_key = 56;	/* KEY_LEFTALT  (Alt = Command in VOS) */
	fKeys.right_command_key = 0;
	fKeys.left_control_key  = 29;	/* KEY_LEFTCTRL */
	fKeys.right_control_key = 97;	/* KEY_RIGHTCTRL */
	fKeys.left_option_key   = 125;	/* KEY_LEFTMETA  (Super = Option) */
	fKeys.right_option_key  = 100;	/* KEY_RIGHTALT  (AltGr = right Option) */
	fKeys.menu_key          = 139;	/* KEY_MENU */

	static const int32 kUseShift = 0x01;
	static const int32 kUseCaps  = 0x02;
	static const int32 kUseCtrl  = 0x04;
	static const int32 kUseAlt   = 0x08;

	struct ModCombo {
		int32 flags;
		int32* table;
	};

	ModCombo combos[] = {
		{ 0,                          fKeys.normal_map },
		{ kUseShift,                  fKeys.shift_map },
		{ kUseCaps,                   fKeys.caps_map },
		{ kUseShift | kUseCaps,       fKeys.caps_shift_map },
		{ kUseCtrl,                   fKeys.control_map },
		{ kUseAlt,                    fKeys.option_map },
		{ kUseAlt | kUseShift,        fKeys.option_shift_map },
		{ kUseAlt | kUseCaps,         fKeys.option_caps_map },
		{ kUseAlt | kUseShift | kUseCaps,
		                              fKeys.option_caps_shift_map },
	};
	static const int kNumCombos = (int)(sizeof(combos) / sizeof(combos[0]));

	xkb_mod_index_t shiftIdx = xkb_keymap_mod_get_index(xkbKeymap,
		XKB_MOD_NAME_SHIFT);
	xkb_mod_index_t capsIdx = xkb_keymap_mod_get_index(xkbKeymap,
		XKB_MOD_NAME_CAPS);
	xkb_mod_index_t ctrlIdx = xkb_keymap_mod_get_index(xkbKeymap,
		XKB_MOD_NAME_CTRL);
	xkb_mod_index_t altIdx = xkb_keymap_mod_get_index(xkbKeymap,
		XKB_MOD_NAME_ALT);

	bool hasShift = (shiftIdx != XKB_MOD_INVALID);
	bool hasCaps  = (capsIdx  != XKB_MOD_INVALID);
	bool hasCtrl  = (ctrlIdx  != XKB_MOD_INVALID);
	bool hasAlt   = (altIdx   != XKB_MOD_INVALID);

	struct xkb_state* state = xkb_state_new(xkbKeymap);
	if (state == NULL) {
		delete[] fChars;
		fChars = NULL;
		fCharsSize = 0;
		return B_NO_MEMORY;
	}

	/* Iterate evdev codes 0..127 (covers all standard keyboard keys).
	 * xkb keycode = evdev code + 8  (standard XKB/evdev offset). */
	for (uint32 kc = 0; kc < 128; kc++) {
		xkb_keycode_t xkbCode = kc + 8;
		if (!xkb_keymap_key_get_syms_by_level(xkbKeymap, xkbCode, 0, 0, NULL))
			continue;

		for (int ci = 0; ci < kNumCombos; ci++) {
			if ((combos[ci].flags & kUseShift) && !hasShift)
				{ combos[ci].table[kc] = 0; continue; }
			if ((combos[ci].flags & kUseCaps) && !hasCaps)
				{ combos[ci].table[kc] = 0; continue; }
			if ((combos[ci].flags & kUseCtrl) && !hasCtrl)
				{ combos[ci].table[kc] = 0; continue; }
			if ((combos[ci].flags & kUseAlt) && !hasAlt)
				{ combos[ci].table[kc] = 0; continue; }

			xkb_mod_mask_t wantedMask = 0;
			if (combos[ci].flags & kUseShift) wantedMask |= (1u << shiftIdx);
			if (combos[ci].flags & kUseCaps)  wantedMask |= (1u << capsIdx);
			if (combos[ci].flags & kUseCtrl)  wantedMask |= (1u << ctrlIdx);
			if (combos[ci].flags & kUseAlt)   wantedMask |= (1u << altIdx);

			xkb_state_update_mask(state, wantedMask, 0, 0, 0, 0, 0);
			xkb_layout_index_t layout = xkb_state_key_get_layout(state, xkbCode);
			xkb_level_index_t level = xkb_state_key_get_level(state,
				xkbCode, layout);

			const xkb_keysym_t* syms;
			int nsyms = xkb_keymap_key_get_syms_by_level(xkbKeymap,
				xkbCode, layout, level, &syms);

			if (nsyms <= 0 || syms[0] == XKB_KEY_NoSymbol
				|| syms[0] == XKB_KEY_VoidSymbol) {
				combos[ci].table[kc] = 0;
				continue;
			}

			uint32_t ucs4 = xkb_keysym_to_utf32(syms[0]);
			if (ucs4 == 0) {
				combos[ci].table[kc] = 0;
				continue;
			}

			char buf[5];
			int numBytes = 0;
			if (ucs4 < 0x80) {
				buf[0] = (char)ucs4;
				numBytes = 1;
			} else if (ucs4 < 0x800) {
				buf[0] = (char)(0xC0 | (ucs4 >> 6));
				buf[1] = (char)(0x80 | (ucs4 & 0x3F));
				numBytes = 2;
			} else if (ucs4 < 0x10000) {
				buf[0] = (char)(0xE0 | (ucs4 >> 12));
				buf[1] = (char)(0x80 | ((ucs4 >> 6) & 0x3F));
				buf[2] = (char)(0x80 | (ucs4 & 0x3F));
				numBytes = 3;
			} else if (ucs4 < 0x110000) {
				buf[0] = (char)(0xF0 | (ucs4 >> 18));
				buf[1] = (char)(0x80 | ((ucs4 >> 12) & 0x3F));
				buf[2] = (char)(0x80 | ((ucs4 >> 6) & 0x3F));
				buf[3] = (char)(0x80 | (ucs4 & 0x3F));
				numBytes = 4;
			}

			if (numBytes == 0) {
				combos[ci].table[kc] = 0;
				continue;
			}

			if (writePos + 1 + (uint32)numBytes > kMaxChars) {
				combos[ci].table[kc] = 0;
				continue;
			}

			fChars[writePos] = (char)numBytes;
			memcpy(fChars + writePos + 1, buf, numBytes);
			combos[ci].table[kc] = (int32)writePos;
			writePos += 1 + numBytes;
		}
	}

	fCharsSize = writePos;

	xkb_state_unref(state);

	return B_OK;
}
