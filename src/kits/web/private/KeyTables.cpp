/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the LGPL License.
 */

#include "KeyTables.h"


uint32_t
web_decode_utf8(const char* bytes, int32 numBytes)
{
	if (bytes == NULL || numBytes < 1)
		return 0;

	const unsigned char* p = (const unsigned char*)bytes;
	uint32_t cp = p[0];
	int32 len = 1;
	if (cp < 0x80) {
		return cp;
	} else if ((cp & 0xe0) == 0xc0) {
		cp &= 0x1f;
		len = 2;
	} else if ((cp & 0xf0) == 0xe0) {
		cp &= 0x0f;
		len = 3;
	} else if ((cp & 0xf8) == 0xf0) {
		cp &= 0x07;
		len = 4;
	} else {
		return 0;
	}

	if (numBytes < len)
		return 0;

	for (int32 i = 1; i < len; i++) {
		if ((p[i] & 0xc0) != 0x80)
			return 0;
		cp = (cp << 6) | (p[i] & 0x3f);
	}
	return cp;
}


uint32_t
web_unfold_ctrl_letter(unsigned char b)
{
	if (b >= 0x01 && b <= 0x1a)
		return (uint32_t)('a' + b - 1);
	return 0;
}


uint32_t
web_special_key_to_keysym(int32 rawKey)
{
	switch (rawKey) {
		case kWebLegacyEscKey:			return kWebXK_Escape;
		case kWebLegacyPrintKey:			return kWebXK_Print;
		case kWebLegacyScrollLockKey:	return kWebXK_Scroll_Lock;
		case kWebLegacyPauseKey:			return kWebXK_Pause;
		case 0x02:						return kWebXK_F1;	// B_F1_KEY
		case 0x03:						return kWebXK_F1 + 1;
		case 0x04:						return kWebXK_F1 + 2;
		case 0x05:						return kWebXK_F1 + 3;
		case 0x06:						return kWebXK_F1 + 4;
		case 0x07:						return kWebXK_F1 + 5;
		case 0x08:						return kWebXK_F1 + 6;
		case 0x09:						return kWebXK_F1 + 7;
		case 0x0a:						return kWebXK_F1 + 8;
		case 0x0b:						return kWebXK_F1 + 9;
		case 0x0c:						return kWebXK_F1 + 10;
		case 0x0d:						return kWebXK_F1 + 11;	// B_F12_KEY
		case kWebLegacyBackspaceKey:		return kWebXK_BackSpace;
		case kWebLegacyInsertKey:		return kWebXK_Insert;
		case kWebLegacyHomeKey:			return kWebXK_Home;
		case kWebLegacyPageUpKey:		return kWebXK_Page_Up;
		case kWebLegacyNumLockKey:		return kWebXK_Num_Lock;
		case kWebLegacyTabKey:			return kWebXK_Tab;
		case kWebLegacyDeleteKey:		return kWebXK_Delete;
		case kWebLegacyEndKey:			return kWebXK_End;
		case kWebLegacyPageDownKey:		return kWebXK_Page_Down;
		case kWebLegacyCapsLockKey:		return kWebXK_Caps_Lock;
		case kWebLegacyEnterKey:			return kWebXK_Return;
		case kWebLegacyLeftShiftKey:		return kWebXK_Shift_L;
		case kWebLegacyRightShiftKey:	return kWebXK_Shift_R;
		case kWebLegacyUpKey:			return kWebXK_Up;
		case kWebLegacyKPEnterKey:		return kWebXK_KP_Enter;
		case kWebLegacyLeftCtrlKey:		return kWebXK_Control_L;
		case kWebLegacyLeftAltKey:		return kWebXK_Alt_L;
		case kWebLegacyRightAltKey:		return kWebXK_Alt_R;
		case kWebLegacyRightCtrlKey:		return kWebXK_Control_R;
		case kWebLegacyLeftKey:			return kWebXK_Left;
		case kWebLegacyDownKey:			return kWebXK_Down;
		case kWebLegacyRightKey:			return kWebXK_Right;
		case kWebLegacyLeftMetaKey:		return kWebXK_Super_L;
		case kWebLegacyRightMetaKey:		return kWebXK_Super_R;
		case kWebLegacyMenuKey:			return kWebXK_Menu;
		case kWebEvdevPlayPause:		return kWebXK_XF86AudioPlay;
		case kWebEvdevStop:				return kWebXK_XF86AudioStop;
		case kWebEvdevNextTrack:		return kWebXK_XF86AudioNext;
		case kWebEvdevPrevTrack:		return kWebXK_XF86AudioPrev;
		case kWebEvdevMute:				return kWebXK_XF86AudioMute;
		case kWebEvdevVolumeDown:		return kWebXK_XF86AudioLower;
		case kWebEvdevVolumeUp:			return kWebXK_XF86AudioRaise;
		default:
			break;
	}
	return 0;
}
