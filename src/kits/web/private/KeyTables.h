/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the LGPL License.
 */
#ifndef VOS_WEB_KEY_TABLES_H
#define VOS_WEB_KEY_TABLES_H


#include <stdint.h>


// Pure mapping tables between the V\OS hybrid keycode space and the X11
// keysym values libwpe expects in wpe_input_keyboard_event.key_code.
// No libwpe, no BeAPI: unit-tested in src/tests/kits/web.

// V\OS hybrid keycode space: <=0x7f is the legacy keycode, not an
// evdev code and not the ASCII byte constants in InterfaceDefs.h.
// Values match the input_server mapping (see LinuxKeycodeMap.h).
enum {
	kWebLegacyEscKey		= 0x01,
	kWebLegacyPrintKey	= 0x0e,
	kWebLegacyScrollLockKey	= 0x0f,
	kWebLegacyPauseKey	= 0x10,
	kWebLegacyBacktickKey	= 0x11,
	kWebLegacyBackspaceKey	= 0x1e,
	kWebLegacyInsertKey	= 0x1f,
	kWebLegacyHomeKey	= 0x20,
	kWebLegacyPageUpKey	= 0x21,
	kWebLegacyNumLockKey	= 0x22,
	kWebLegacyTabKey		= 0x26,
	kWebLegacyDeleteKey	= 0x34,
	kWebLegacyEndKey		= 0x35,
	kWebLegacyPageDownKey	= 0x36,
	kWebLegacyCapsLockKey	= 0x3b,
	kWebLegacyEnterKey	= 0x47,
	kWebLegacyLeftShiftKey	= 0x4b,
	kWebLegacyRightShiftKey	= 0x56,
	kWebLegacyUpKey		= 0x57,
	kWebLegacyKPEnterKey	= 0x5b,
	kWebLegacyLeftCtrlKey	= 0x5c,
	kWebLegacyLeftAltKey	= 0x5d,
	kWebLegacyRightAltKey	= 0x5f,
	kWebLegacyRightCtrlKey	= 0x60,
	kWebLegacyLeftKey	= 0x61,
	kWebLegacyDownKey	= 0x62,
	kWebLegacyRightKey	= 0x63,
	kWebLegacyLeftMetaKey	= 0x66,
	kWebLegacyRightMetaKey	= 0x67,
	kWebLegacyMenuKey	= 0x68
};


// X11 keysyms (libwpe key_code space).
enum {
	kWebXK_F1			= 0xffbe,
	kWebXK_F12			= 0xffc9,
	kWebXK_Return		= 0xff0d,
	kWebXK_KP_Enter		= 0xff8d,
	kWebXK_Escape		= 0xff1b,
	kWebXK_Home			= 0xff50,
	kWebXK_Left			= 0xff51,
	kWebXK_Up			= 0xff52,
	kWebXK_Right		= 0xff53,
	kWebXK_Down			= 0xff54,
	kWebXK_Page_Up		= 0xff55,
	kWebXK_Page_Down	= 0xff56,
	kWebXK_End			= 0xff57,
	kWebXK_Insert		= 0xff63,
	kWebXK_Delete		= 0xffff,
	kWebXK_BackSpace	= 0xff08,
	kWebXK_Tab			= 0xff09,
	kWebXK_Print		= 0xff61,
	kWebXK_Scroll_Lock	= 0xff14,
	kWebXK_Pause		= 0xff13,
	kWebXK_Num_Lock		= 0xff7f,
	kWebXK_Caps_Lock	= 0xffe5,
	kWebXK_Shift_L		= 0xffe1,
	kWebXK_Shift_R		= 0xffe2,
	kWebXK_Control_L	= 0xffe3,
	kWebXK_Control_R	= 0xffe4,
	kWebXK_Alt_L		= 0xffe9,
	kWebXK_Alt_R		= 0xffea,
	kWebXK_Super_L		= 0xffeb,
	kWebXK_Super_R		= 0xffec,
	kWebXK_Menu			= 0xff67,
	kWebXK_XF86AudioPlay	= 0x1008ff14,
	kWebXK_XF86AudioStop	= 0x1008ff15,
	kWebXK_XF86AudioNext	= 0x1008ff17,
	kWebXK_XF86AudioPrev	= 0x1008ff16,
	kWebXK_XF86AudioMute	= 0x1008ff12,
	kWebXK_XF86AudioLower	= 0x1008ff11,
	kWebXK_XF86AudioRaise	= 0x1008ff13
};


// Raw evdev codes the input server passes through untranslated plus
// media keys that have no legacy keycode.
enum {
	kWebEvdevPlayPause	= 164,
	kWebEvdevNextTrack	= 163,
	kWebEvdevPrevTrack	= 165,
	kWebEvdevStop		= 166,
	kWebEvdevMute		= 113,
	kWebEvdevVolumeDown	= 114,
	kWebEvdevVolumeUp	= 115
};


// Returns the keysym for a non-printable key, or 0 when rawKey is not a
// special key this table covers (printable text goes through
// wpe_unicode_to_key_code instead).
uint32_t
web_special_key_to_keysym(int32 rawKey);

// Decodes the first UTF-8 codepoint in bytes; returns 0 on error or
// empty input.
uint32_t
web_decode_utf8(const char* bytes, int32 numBytes);

// Expands a Ctrl-folded control byte (0x01-0x1a, xkb folds Ctrl+letter)
// to the letter it came from; returns 0 when b is not a fold.
uint32_t
web_unfold_ctrl_letter(unsigned char b);


#endif // VOS_WEB_KEY_TABLES_H
