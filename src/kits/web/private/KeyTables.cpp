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


// Inverse of kEvdevToHaiku in the input server's LinuxKeycodeMap.h,
// indexed by the legacy keycode (0x01..0x69). 0 means no equivalent.
static const uint8 kLegacyToEvdev[0x6a] = {
	/* 0x00 */  0,
	/* 0x01 Esc      */  1,
	/* 0x02-0x0d F1-F12 */  59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 87, 88,
	/* 0x0e PrtScr   */  99,
	/* 0x0f ScrLk    */  70,
	/* 0x10 Pause    */  119,
	/* 0x11 `        */  41,
	/* 0x12-0x1b 1-0 */  2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
	/* 0x1c -        */  12,
	/* 0x1d =        */  13,
	/* 0x1e Backspace */  14,
	/* 0x1f Insert   */  110,
	/* 0x20 Home     */  102,
	/* 0x21 PgUp     */  104,
	/* 0x22 NumLock  */  69,
	/* 0x23 KP/      */  98,
	/* 0x24 KP*      */  55,
	/* 0x25 KP-      */  74,
	/* 0x26 Tab      */  15,
	/* 0x27-0x30 Q-P */  16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
	/* 0x31 [        */  26,
	/* 0x32 ]        */  27,
	/* 0x33 \        */  43,
	/* 0x34 Delete   */  111,
	/* 0x35 End      */  107,
	/* 0x36 PgDn     */  109,
	/* 0x37-0x39 KP7-9 */  71, 72, 73,
	/* 0x3a KP+      */  78,
	/* 0x3b CapsLock */  58,
	/* 0x3c-0x46 A-' */  30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	/* 0x47 Enter    */  28,
	/* 0x48-0x4a KP4-6 */  75, 76, 77,
	/* 0x4b LShift   */  42,
	/* 0x4c-0x55 Z-/ */  44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
	/* 0x56 RShift   */  54,
	/* 0x57 Up       */  103,
	/* 0x58-0x5a KP1-3 */  79, 80, 81,
	/* 0x5b KPEnter  */  96,
	/* 0x5c LCtrl    */  29,
	/* 0x5d LAlt     */  56,
	/* 0x5e Space    */  57,
	/* 0x5f RAlt     */  100,
	/* 0x60 RCtrl    */  97,
	/* 0x61 Left     */  105,
	/* 0x62 Down     */  108,
	/* 0x63 Right    */  106,
	/* 0x64 KP0      */  82,
	/* 0x65 KP.      */  83,
	/* 0x66 LMeta    */  125,
	/* 0x67 RMeta    */  126,
	/* 0x68 Menu     */  127,
	/* 0x69 102nd    */  86
};


int32
web_legacy_key_to_evdev(int32 rawKey)
{
	// The input server passes codes above the legacy range through
	// untranslated: those already are evdev.
	if (rawKey >= 0x80)
		return rawKey;
	if (rawKey <= 0 || rawKey >= (int32)sizeof(kLegacyToEvdev))
		return 0;
	return kLegacyToEvdev[rawKey];
}
