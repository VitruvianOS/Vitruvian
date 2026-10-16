/*
 * Copyright 2026, Vitruvian Project.
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the MIT License.
 *
 * Fills a BKeymap from a live xkb_keymap. key_map/key_states are Haiku-
 * indexed; xkbcommon itself is queried with the xkb code (evdev + 8).
 */


#include <InterfaceDefs.h>
#include <Keymap.h>

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>

#include "LinuxKeycodeMap.h"


struct xkb_layout_entry {
	const char*	name;
	const char*	layout;
	const char*	variant;
};

static const xkb_layout_entry kXkbLayoutTable[] = {
	{ "Albanian",						"al",		"" },
	{ "Arabic (102)",					"ara",		"" },
	{ "Belarusian",						"by",		"" },
	{ "Belarusian (Latin)",				"by",		"latin" },
	{ "Belarusian (Mac)",				"by",		"" },
	{ "Belgian (comma)",				"be",		"" },
	{ "Belgian (point)",				"be",		"oss" },
	{ "Brazilian (ABNT2)",				"br",		"abnt2" },
	{ "Bulgarian (Cyrillic)",			"bg",		"" },
	{ "Bulgarian (Phonetic)",			"bg",		"phonetic" },
	{ "Canadian-French",				"ca",		"fr" },
	{ "Colemak",						"us",		"colemak" },
	{ "Czech",							"cz",		"" },
	{ "Czech (Mac)",					"cz",		"" },
	{ "Danish",							"dk",		"" },
	{ "Dutch",							"nl",		"" },
	{ "Dvorak",							"us",		"dvorak" },
	{ "Dvorak (programmer's)",			"us",		"dvp" },
	{ "Esperanto",						"epo",		"" },
	{ "Estonian",						"ee",		"" },
	{ "Faeroese",						"fo",		"" },
	{ "French",							"fr",		"" },
	{ "French (Bépo)",					"fr",		"bepo" },
	{ "French (Mac)",					"fr",		"" },
	{ "French (NF Z71-300)",			"fr",		"" },
	{ "Friulian",						"it",		"" },
	{ "German",							"de",		"" },
	{ "German (Mac)",					"de",		"" },
	{ "Greek",							"gr",		"" },
	{ "Hebrew",							"il",		"" },
	{ "Hungarian",						"hu",		"" },
	{ "Icelandic",						"is",		"" },
	{ "Irish",							"ie",		"" },
	{ "ISO-9995",						"us",		"" },
	{ "Italian",						"it",		"" },
	{ "Japanese",						"jp",		"" },
	{ "Kazakh",							"kz",		"" },
	{ "Latin-American",					"latam",	"" },
	{ "Lithuanian",						"lt",		"" },
	{ "Lithuanian (Standard)",			"lt",		"std" },
	{ "Macedonian",						"mk",		"" },
	{ "Norwegian",						"no",		"" },
	{ "Polish",							"pl",		"" },
	{ "Polish (Typewriter)",			"pl",		"qwertz" },
	{ "Portuguese",						"pt",		"" },
	{ "Romanian",						"ro",		"" },
	{ "Russian",						"ru",		"" },
	{ "Russian (Mac)",					"ru",		"" },
	{ "Russian (Typewriter)",			"ru",		"typewriter" },
	{ "Russian (Udmurt, Komi, Mari)",	"ru",		"" },
	{ "Serbian (Cyrillic)",				"rs",		"" },
	{ "Serbian (Latin)",				"rs",		"latin" },
	{ "Slovak",							"sk",		"" },
	{ "Slovene",						"si",		"" },
	{ "Spanish",						"es",		"" },
	{ "Spanish (Dvorak)",				"es",		"dvorak" },
	{ "Svorak",							"se",		"dvorak" },
	{ "Swedish",						"se",		"" },
	{ "Swiss-French",					"ch",		"fr" },
	{ "Swiss-German",					"ch",		"de" },
	{ "Thai (TIS-820.2538)",			"th",		"tis" },
	{ "Turkish (Type-F)",				"tr",		"f" },
	{ "Turkish (Type-Q)",				"tr",		"" },
	{ "Ukrainian",						"ua",		"" },
	{ "Ukrainian (Mac)",				"ua",		"" },
	{ "United-Kingdom",					"gb",		"" },
	{ "US",								"us",		"" },
	{ "US-International",				"us",		"intl" },
};


// Result is thread-local scratch; copy it out before calling again.
void
look_up_xkb_layout(const char* keymapName, const char*& layout,
	const char*& variant)
{
	layout = "us";
	variant = "";

	if (keymapName == NULL)
		return;

	static const size_t kPrefixLen = 4;	// strlen("xkb:")
	if (strncmp(keymapName, "xkb:", kPrefixLen) == 0) {
		static thread_local char sLayout[64];
		static thread_local char sVariant[64];

		const char* rest = keymapName + kPrefixLen;
		const char* colon = strchr(rest, ':');
		size_t layoutLen = colon != NULL
			? (size_t)(colon - rest) : strlen(rest);
		if (layoutLen >= sizeof(sLayout))
			layoutLen = sizeof(sLayout) - 1;
		memcpy(sLayout, rest, layoutLen);
		sLayout[layoutLen] = '\0';
		strlcpy(sVariant, colon != NULL ? colon + 1 : "", sizeof(sVariant));

		if (sLayout[0] != '\0') {
			layout = sLayout;
			variant = sVariant;
		}
		return;
	}

	for (size_t i = 0;
			i < sizeof(kXkbLayoutTable) / sizeof(kXkbLayoutTable[0]); i++) {
		if (strcmp(kXkbLayoutTable[i].name, keymapName) == 0) {
			layout = kXkbLayoutTable[i].layout;
			variant = kXkbLayoutTable[i].variant;
			break;
		}
	}
}


// Distinguishes a real entry from look_up_xkb_layout()'s silent "us" fallback.
bool
xkb_layout_name_is_known(const char* keymapName)
{
	if (keymapName == NULL)
		return false;

	for (size_t i = 0;
			i < sizeof(kXkbLayoutTable) / sizeof(kXkbLayoutTable[0]); i++) {
		if (strcmp(kXkbLayoutTable[i].name, keymapName) == 0)
			return true;
	}

	return false;
}


// The Haiku name for an xkb pair, or NULL. Multiple names can share a
// pair (e.g. "by"); the shortest wins, since first-match-in-table order
// would return "ISO-9995" for "us" instead of "US".
const char*
xkb_layout_name_for(const char* layout, const char* variant)
{
	if (layout == NULL)
		return NULL;
	if (variant == NULL)
		variant = "";

	const char* best = NULL;
	size_t bestLength = 0;

	for (size_t i = 0;
			i < sizeof(kXkbLayoutTable) / sizeof(kXkbLayoutTable[0]); i++) {
		if (strcmp(kXkbLayoutTable[i].layout, layout) != 0
			|| strcmp(kXkbLayoutTable[i].variant, variant) != 0) {
			continue;
		}

		size_t length = strlen(kXkbLayoutTable[i].name);
		if (best == NULL || length < bestLength) {
			best = kXkbLayoutTable[i].name;
			bestLength = length;
		}
	}

	return best;
}


int32
xkb_layout_table_count()
{
	return (int32)(sizeof(kXkbLayoutTable) / sizeof(kXkbLayoutTable[0]));
}


const char*
xkb_layout_table_name_at(int32 index)
{
	if (index < 0 || (size_t)index
			>= sizeof(kXkbLayoutTable) / sizeof(kXkbLayoutTable[0])) {
		return NULL;
	}

	return kXkbLayoutTable[index].name;
}


static bool
find_xkb_rules_file(BString& _path)
{
	const char* configRoot = getenv("XKB_CONFIG_ROOT");
	if (configRoot != NULL && configRoot[0] != '\0') {
		BString candidate(configRoot);
		candidate << "/rules/evdev.lst";
		if (access(candidate.String(), R_OK) == 0) {
			_path = candidate;
			return true;
		}
	}

	static const char* kFixedCandidates[] = {
		"/usr/share/X11/xkb/rules/evdev.lst",
		"/usr/local/share/X11/xkb/rules/evdev.lst",
	};
	for (size_t i = 0;
			i < sizeof(kFixedCandidates) / sizeof(kFixedCandidates[0]); i++) {
		if (access(kFixedCandidates[i], R_OK) == 0) {
			_path = kFixedCandidates[i];
			return true;
		}
	}

	return false;
}


// Returns a pointer into buffer, not a copy.
static char*
trim_line(char* buffer)
{
	size_t len = strlen(buffer);
	while (len > 0
			&& (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
		buffer[--len] = '\0';
	}

	char* start = buffer;
	while (*start == ' ' || *start == '\t')
		start++;
	return start;
}


// evdev.lst rows are "<code>  <description>" under "! <section>" headers.
static void
parse_xkb_rules_file(const char* path,
	BObjectList<xkb_catalog_entry, true>& _catalog)
{
	FILE* file = fopen(path, "r");
	if (file == NULL)
		return;

	enum { kSectionNone, kSectionLayout, kSectionVariant } section
		= kSectionNone;
	char line[512];

	while (fgets(line, sizeof(line), file) != NULL) {
		if (line[0] == '!') {
			if (strstr(line, "layout") != NULL)
				section = kSectionLayout;
			else if (strstr(line, "variant") != NULL)
				section = kSectionVariant;
			else
				section = kSectionNone;
			continue;
		}

		if (section == kSectionNone)
			continue;

		char* p = trim_line(line);
		if (*p == '\0')
			continue;

		char* code = p;
		while (*p != '\0' && *p != ' ' && *p != '\t')
			p++;
		if (*p == '\0')
			continue;	// no description column, malformed row
		*p++ = '\0';
		while (*p == ' ' || *p == '\t')
			p++;
		const char* description = p;
		if (*description == '\0')
			continue;

		// "custom" is a placeholder for a user ~/.xkb layout; with nothing
		// there, xkb refuses to compile it.
		if (section == kSectionLayout && strcmp(code, "custom") == 0)
			continue;

		xkb_catalog_entry* entry = new (std::nothrow) xkb_catalog_entry;
		if (entry == NULL)
			continue;

		if (section == kSectionLayout) {
			entry->id.SetToFormat("xkb:%s", code);
			entry->label = description;
		} else {
			const char* colon = strchr(description, ':');
			if (colon == NULL) {
				delete entry;
				continue;
			}
			BString ownerLayout(description, colon - description);
			const char* label = colon + 1;
			while (*label == ' ')
				label++;

			entry->id.SetToFormat("xkb:%s:%s", ownerLayout.String(), code);
			entry->label = label;
		}

		if (!_catalog.AddItem(entry))
			delete entry;
	}

	fclose(file);
}


status_t
get_xkb_layout_catalog(BObjectList<xkb_catalog_entry, true>& _catalog)
{
	_catalog.MakeEmpty();

	BString rulesPath;
	if (!find_xkb_rules_file(rulesPath))
		return B_ENTRY_NOT_FOUND;

	parse_xkb_rules_file(rulesPath.String(), _catalog);

	return _catalog.CountItems() > 0 ? B_OK : B_ERROR;
}


// evdev.lst's "! model" rows are "<code>  <description>", same shape as
// the layout section but with no owner-layout prefix to strip.
static void
parse_xkb_model_section(const char* path,
	BObjectList<xkb_model_entry, true>& _catalog)
{
	FILE* file = fopen(path, "r");
	if (file == NULL)
		return;

	bool inModelSection = false;
	char line[512];

	while (fgets(line, sizeof(line), file) != NULL) {
		if (line[0] == '!') {
			inModelSection = strstr(line, "model") != NULL;
			continue;
		}

		if (!inModelSection)
			continue;

		char* p = trim_line(line);
		if (*p == '\0')
			continue;

		char* code = p;
		while (*p != '\0' && *p != ' ' && *p != '\t')
			p++;
		if (*p == '\0')
			continue;	// no description column, malformed row
		*p++ = '\0';
		while (*p == ' ' || *p == '\t')
			p++;
		const char* description = p;
		if (*description == '\0')
			continue;

		xkb_model_entry* entry = new (std::nothrow) xkb_model_entry;
		if (entry == NULL)
			continue;

		entry->id = code;
		entry->label = description;

		if (!_catalog.AddItem(entry))
			delete entry;
	}

	fclose(file);
}


status_t
get_xkb_model_catalog(BObjectList<xkb_model_entry, true>& _catalog)
{
	_catalog.MakeEmpty();

	BString rulesPath;
	if (!find_xkb_rules_file(rulesPath))
		return B_ENTRY_NOT_FOUND;

	parse_xkb_model_section(rulesPath.String(), _catalog);

	return _catalog.CountItems() > 0 ? B_OK : B_ERROR;
}


// Derived from key_map rather than stored, so the two cannot drift.
const char*
look_up_xkb_modifier_options(const key_map& map)
{
	static const uint32 kLeftControl = 0x5c;	// KEY_LEFTCTRL
	static const uint32 kLeftAlt     = 0x5d;	// KEY_LEFTALT
	static const uint32 kLeftMeta    = 0x66;	// KEY_LEFTMETA

	if (map.left_command_key == kLeftControl
		&& map.left_control_key == kLeftAlt) {
		return "ctrl:swap_lalt_lctl";
	}

	if (map.left_command_key == kLeftMeta
		&& map.left_option_key == kLeftAlt) {
		return "altwin:swap_alt_win";
	}

	return "";
}


// Haiku byte for a keysym xkb_keysym_to_utf32() has no value for; 0 if none.
static uint32
haiku_byte_for_keysym(xkb_keysym_t sym)
{
	switch (sym) {
		case XKB_KEY_ISO_Left_Tab:	return B_TAB;
		case XKB_KEY_KP_Delete:		return B_DELETE;
		case XKB_KEY_Insert:
		case XKB_KEY_KP_Insert:		return B_INSERT;
		case XKB_KEY_Home:
		case XKB_KEY_KP_Home:		return B_HOME;
		case XKB_KEY_End:
		case XKB_KEY_KP_End:		return B_END;
		case XKB_KEY_Page_Up:
		case XKB_KEY_KP_Page_Up:	return B_PAGE_UP;
		case XKB_KEY_Page_Down:
		case XKB_KEY_KP_Page_Down:	return B_PAGE_DOWN;
		case XKB_KEY_Left:
		case XKB_KEY_KP_Left:		return B_LEFT_ARROW;
		case XKB_KEY_Right:
		case XKB_KEY_KP_Right:		return B_RIGHT_ARROW;
		case XKB_KEY_Up:
		case XKB_KEY_KP_Up:			return B_UP_ARROW;
		case XKB_KEY_Down:
		case XKB_KEY_KP_Down:		return B_DOWN_ARROW;

		// Dead keys get their spacing equivalents, so the key types something.
		case XKB_KEY_dead_grave:		return 0x60;	// GRAVE ACCENT
		case XKB_KEY_dead_acute:		return 0xb4;	// ACUTE ACCENT
		case XKB_KEY_dead_circumflex:	return 0x5e;	// CIRCUMFLEX ACCENT
		case XKB_KEY_dead_diaeresis:	return 0xa8;	// DIAERESIS
		case XKB_KEY_dead_tilde:		return 0x7e;	// TILDE
		default:
			break;
	}

	if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F35)
		return B_FUNCTION_KEY;
	if (sym == XKB_KEY_Print || sym == XKB_KEY_Sys_Req
		|| sym == XKB_KEY_Scroll_Lock || sym == XKB_KEY_Pause
		|| sym == XKB_KEY_Break) {
		return B_FUNCTION_KEY;
	}

	return 0;
}


static int
encode_utf8(uint32_t ucs4, char* buf)
{
	if (ucs4 < 0x80) {
		buf[0] = (char)ucs4;
		return 1;
	}
	if (ucs4 < 0x800) {
		buf[0] = (char)(0xC0 | (ucs4 >> 6));
		buf[1] = (char)(0x80 | (ucs4 & 0x3F));
		return 2;
	}
	if (ucs4 < 0x10000) {
		buf[0] = (char)(0xE0 | (ucs4 >> 12));
		buf[1] = (char)(0x80 | ((ucs4 >> 6) & 0x3F));
		buf[2] = (char)(0x80 | (ucs4 & 0x3F));
		return 3;
	}
	if (ucs4 < 0x110000) {
		buf[0] = (char)(0xF0 | (ucs4 >> 18));
		buf[1] = (char)(0x80 | ((ucs4 >> 12) & 0x3F));
		buf[2] = (char)(0x80 | ((ucs4 >> 6) & 0x3F));
		buf[3] = (char)(0x80 | (ucs4 & 0x3F));
		return 4;
	}
	return 0;
}


// Returns 0 if it does not fit. fChars[0] stays zeroed, so 0 means
// "not written".
static int32
append_chars(char* fChars, uint32& writePos, size_t kMaxChars,
	const char* bytes, int numBytes)
{
	if (numBytes <= 0 || writePos + 1 + (uint32_t)numBytes > kMaxChars)
		return 0;

	int32 offset = (int32)writePos;
	fChars[writePos] = (char)numBytes;
	memcpy(fChars + writePos + 1, bytes, numBytes);
	writePos += 1 + numBytes;
	return offset;
}


/*!	Convenience wrapper: encode_utf8() + append_chars() in one call, for the
	dead-key pass where callers only have a codepoint, not pre-encoded bytes.
*/
static int32
append_codepoint(char* fChars, uint32& writePos, size_t kMaxChars,
	uint32_t ucs4)
{
	char buf[4];
	int numBytes = encode_utf8(ucs4, buf);
	return append_chars(fChars, writePos, kMaxChars, buf, numBytes);
}


// Sets are per accent (not global vowel list): tilde is A O N
static int
candidate_priority_class(xkb_keysym_t deadKeysym, uint32_t ucs4)
{
	uint32_t upper = (ucs4 >= 'a' && ucs4 <= 'z') ? ucs4 - 0x20 : ucs4;

	switch (deadKeysym) {
		case XKB_KEY_dead_acute:
			switch (upper) {
				case 'A': case 'E': case 'I': case 'O': case 'U': case 'Y':
					return 1;
				case 'C': case 'N': case 'S': case 'Z': case 'G': case 'L':
				case 'R': case 'W':
					return 2;
				default:
					return 3;
			}

		case XKB_KEY_dead_grave:
			switch (upper) {
				case 'A': case 'E': case 'I': case 'O': case 'U':
					return 1;
				case 'N': case 'W': case 'Y':
					return 2;
				default:
					return 3;
			}

		case XKB_KEY_dead_circumflex:
			switch (upper) {
				case 'A': case 'E': case 'I': case 'O': case 'U':
					return 1;
				case 'C': case 'G': case 'H': case 'J': case 'S': case 'W':
				case 'Y':
					return 2;
				default:
					return 3;
			}

		case XKB_KEY_dead_diaeresis:
			switch (upper) {
				case 'A': case 'E': case 'I': case 'O': case 'U': case 'Y':
					return 1;
				case 'H': case 'T': case 'W': case 'X':
					return 2;
				default:
					return 3;
			}

		case XKB_KEY_dead_tilde:
			switch (upper) {
				case 'A': case 'O': case 'N':
					return 1;
				case 'E': case 'I': case 'U': case 'V': case 'Y':
					return 2;
				default:
					return 3;
			}

		default:
			return 3;
	}
}


status_t
BKeymap::PopulateFromXkb(struct xkb_keymap* xkbKeymap)
{
	if (xkbKeymap == NULL)
		return B_BAD_VALUE;

	// Preserve a caller-supplied assignment: this generates characters,
	// not roles.
	bool hadModifiers = fChars != NULL;
	key_map savedModifiers = key_map();
	if (hadModifiers)
		savedModifiers = fKeys;

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

	// Haiku legacy keycodes; menu_key has no legacy slot so it stays raw evdev.
	fKeys.caps_key         = linux_to_haiku_keycode(58);	/* KEY_CAPSLOCK */
	fKeys.scroll_key       = linux_to_haiku_keycode(70);	/* KEY_SCROLLLOCK */
	fKeys.num_key          = linux_to_haiku_keycode(69);	/* KEY_NUMLOCK */
	fKeys.left_shift_key   = linux_to_haiku_keycode(42);	/* KEY_LEFTSHIFT */
	fKeys.right_shift_key  = linux_to_haiku_keycode(54);	/* KEY_RIGHTSHIFT */
	// ctrl mode, the shipped default. Must match SystemKeymap.h byte for byte.
	fKeys.left_command_key = linux_to_haiku_keycode(29);	/* KEY_LEFTCTRL */
	fKeys.right_command_key = linux_to_haiku_keycode(97);	/* KEY_RIGHTCTRL */
	fKeys.left_control_key  = linux_to_haiku_keycode(56);	/* KEY_LEFTALT */
	fKeys.right_control_key = 0;
	/* Super becomes Option, AltGr the right Option. */
	fKeys.left_option_key   = linux_to_haiku_keycode(125);
	fKeys.right_option_key  = linux_to_haiku_keycode(100);
	fKeys.menu_key          = 139;

	if (hadModifiers) {
		fKeys.caps_key = savedModifiers.caps_key;
		fKeys.scroll_key = savedModifiers.scroll_key;
		fKeys.num_key = savedModifiers.num_key;
		fKeys.left_shift_key = savedModifiers.left_shift_key;
		fKeys.right_shift_key = savedModifiers.right_shift_key;
		fKeys.left_command_key = savedModifiers.left_command_key;
		fKeys.right_command_key = savedModifiers.right_command_key;
		fKeys.left_control_key = savedModifiers.left_control_key;
		fKeys.right_control_key = savedModifiers.right_control_key;
		fKeys.left_option_key = savedModifiers.left_option_key;
		fKeys.right_option_key = savedModifiers.right_option_key;
		fKeys.menu_key = savedModifiers.menu_key;
		fKeys.lock_settings = savedModifiers.lock_settings;
	}

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

	static const uint32 kTableBits[] = {
		B_NORMAL_TABLE, B_SHIFT_TABLE, B_CAPS_TABLE, B_CAPS_SHIFT_TABLE,
		B_CONTROL_TABLE, B_OPTION_TABLE, B_OPTION_SHIFT_TABLE,
		B_OPTION_CAPS_TABLE, B_OPTION_CAPS_SHIFT_TABLE
	};

	// A compose pair is only useful if its second keysym is typeable here.
	struct ReachableChar { uint32_t ucs4; uint32_t tableBits; };
	ReachableChar reachable[kNumCombos * 128];
	int reachableCount = 0;

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

	for (uint32 evdevCode = 0; evdevCode < 256; evdevCode++) {
		uint32 kc = linux_to_haiku_keycode(evdevCode);
		if (kc == 0 || kc >= 128)
			continue;

		xkb_keycode_t xkbCode = evdevCode + 8;
		// Some xkbcommon builds write through syms_out without null-checking.
		const xkb_keysym_t* probeSyms = NULL;
		if (!xkb_keymap_key_get_syms_by_level(xkbKeymap, xkbCode, 0, 0,
				&probeSyms))
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
			xkb_layout_index_t layout
				= xkb_state_key_get_layout(state, xkbCode);
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

			switch (syms[0]) {
				case XKB_KEY_dead_acute:
					fKeys.acute_tables |= kTableBits[ci];
					break;
				case XKB_KEY_dead_grave:
					fKeys.grave_tables |= kTableBits[ci];
					break;
				case XKB_KEY_dead_circumflex:
					fKeys.circumflex_tables |= kTableBits[ci];
					break;
				case XKB_KEY_dead_diaeresis:
					fKeys.dieresis_tables |= kTableBits[ci];
					break;
				case XKB_KEY_dead_tilde:
					fKeys.tilde_tables |= kTableBits[ci];
					break;
				default:
					break;
			}

			uint32_t ucs4 = xkb_keysym_to_utf32(syms[0]);
			if (ucs4 == 0)
				ucs4 = haiku_byte_for_keysym(syms[0]);
			// Haiku's Return is LF; xkb reports CR. Must agree with
			// KeyboardInputDevice.cpp.
			if (ucs4 == 0x0d)
				ucs4 = B_RETURN;
			if (ucs4 == 0) {
				combos[ci].table[kc] = 0;
				continue;
			}

			char buf[4];
			int numBytes = encode_utf8(ucs4, buf);
			int32 offset = append_chars(fChars, writePos, kMaxChars, buf,
				numBytes);
			combos[ci].table[kc] = offset;	// 0 on overflow, same as before

			if (offset == 0)
				continue;

			bool alreadyKnown = false;
			for (int ri = 0; ri < reachableCount; ri++) {
				if (reachable[ri].ucs4 == ucs4) {
					reachable[ri].tableBits |= kTableBits[ci];
					alreadyKnown = true;
					break;
				}
			}
			if (!alreadyKnown
				&& reachableCount
					< (int)(sizeof(reachable) / sizeof(reachable[0]))) {
				reachable[reachableCount].ucs4 = ucs4;
				reachable[reachableCount].tableBits = kTableBits[ci];
				reachableCount++;
			}
		}
	}

	// Derive compose tables from xkb (key_map must match what typing honors)
	struct DeadKeyTable {
		xkb_keysym_t	keysym;
		int32*			pairs;			// fKeys.*_dead_key[32]
		uint32*			tableFlags;		// fKeys.*_tables
	};
	DeadKeyTable deadKeyTables[] = {
		{ XKB_KEY_dead_acute,
			fKeys.acute_dead_key, &fKeys.acute_tables },
		{ XKB_KEY_dead_grave,
			fKeys.grave_dead_key, &fKeys.grave_tables },
		{ XKB_KEY_dead_circumflex,
			fKeys.circumflex_dead_key, &fKeys.circumflex_tables },
		{ XKB_KEY_dead_diaeresis,
			fKeys.dieresis_dead_key, &fKeys.dieresis_tables },
		{ XKB_KEY_dead_tilde,
			fKeys.tilde_dead_key, &fKeys.tilde_tables },
	};
	static const int kNumDeadKeys
		= (int)(sizeof(deadKeyTables) / sizeof(deadKeyTables[0]));

	bool anyDeadKeyReachable = false;
	for (int i = 0; i < kNumDeadKeys; i++) {
		if (*deadKeyTables[i].tableFlags != 0)
			anyDeadKeyReachable = true;
	}

	if (anyDeadKeyReachable) {
		struct xkb_context* composeContext
			= xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		struct xkb_compose_table* composeTable = NULL;
		if (composeContext != NULL) {
			const char* locale = getenv("LANG");
			if (locale == NULL || locale[0] == '\0')
				locale = "C";
			composeTable = xkb_compose_table_new_from_locale(composeContext,
				locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
			xkb_context_unref(composeContext);
		}

		if (composeTable != NULL) {
			struct Candidate {
				uint32_t	baseUcs4;
				const char*	resultUtf8;
				int			resultLen;
			};
			static const int kMaxCandidates = 64;

			for (int i = 0; i < kNumDeadKeys; i++) {
				if (*deadKeyTables[i].tableFlags == 0)
					continue;	// not reachable here; don't invent pairs

				Candidate candidates[kMaxCandidates];
				int candidateCount = 0;
				Candidate spacePair = { 0, NULL, 0 };
				bool haveSpacePair = false;

				struct xkb_compose_table_iterator* iter
					= xkb_compose_table_iterator_new(composeTable);
				struct xkb_compose_table_entry* entry;
				while (iter != NULL && (entry
						= xkb_compose_table_iterator_next(iter)) != NULL) {
					size_t seqLen;
					const xkb_keysym_t* seq
						= xkb_compose_table_entry_sequence(entry, &seqLen);
					if (seqLen != 2 || seq[0] != deadKeyTables[i].keysym)
						continue;

					uint32_t baseUcs4 = xkb_keysym_to_utf32(seq[1]);
					if (baseUcs4 == 0)
						baseUcs4 = haiku_byte_for_keysym(seq[1]);
					if (baseUcs4 == 0)
						continue;	// not representable as a key_map byte

					uint32_t tableBits = 0;
					for (int ri = 0; ri < reachableCount; ri++) {
						if (reachable[ri].ucs4 == baseUcs4) {
							tableBits = reachable[ri].tableBits;
							break;
						}
					}
					if (tableBits == 0)
						continue;

					const char* resultUtf8
						= xkb_compose_table_entry_utf8(entry);
					int resultLen = resultUtf8 != NULL
						? (int)strlen(resultUtf8) : 0;
					if (resultLen <= 0 || resultLen > 4)
						continue;

					if (seq[1] == XKB_KEY_space) {
						// DeadKeyIndex() hardcodes *_dead_key[1]
						// as the alone-press character.
						spacePair.baseUcs4 = baseUcs4;
						spacePair.resultUtf8 = resultUtf8;
						spacePair.resultLen = resultLen;
						haveSpacePair = true;
						continue;
					}

					if (candidateCount < kMaxCandidates) {
						candidates[candidateCount].baseUcs4 = baseUcs4;
						candidates[candidateCount].resultUtf8 = resultUtf8;
						candidates[candidateCount].resultLen = resultLen;
						candidateCount++;
					}
				}
				if (iter != NULL)
					xkb_compose_table_iterator_free(iter);

				// Priority order (foreign compose entries evict common letters)
				int poolSlots = haveSpacePair ? 15 : 16;
				if (candidateCount > poolSlots) {
					Candidate ordered[kMaxCandidates];
					int orderedCount = 0;
					for (int cls = 1; cls <= 3; cls++) {
						for (int c = 0; c < candidateCount; c++) {
							if (candidate_priority_class(
									deadKeyTables[i].keysym,
									candidates[c].baseUcs4) == cls)
								ordered[orderedCount++] = candidates[c];
						}
					}
					memcpy(candidates, ordered,
						sizeof(Candidate) * orderedCount);
					candidateCount = orderedCount;
				}

				int32* pairs = deadKeyTables[i].pairs;
				memset(pairs, 0, sizeof(int32) * 32);
				int slot = 0;

				if (haveSpacePair) {
					int32 triggerOffset = append_codepoint(fChars, writePos,
						kMaxChars, spacePair.baseUcs4);
					int32 resultOffset = append_chars(fChars, writePos,
						kMaxChars, spacePair.resultUtf8,
						spacePair.resultLen);
					if (triggerOffset != 0 && resultOffset != 0) {
						pairs[0] = triggerOffset;
						pairs[1] = resultOffset;
						slot = 2;
					}
				}

				if (slot == 0) {
					// Slot 0 must hold <dead_X> <space>: IsDeadKey() reads
					// pairs[1] to identify the dead key.
					uint32 spacing
						= haiku_byte_for_keysym(deadKeyTables[i].keysym);
					int32 triggerOffset = append_codepoint(fChars, writePos,
						kMaxChars, 0x20);
					int32 resultOffset = append_codepoint(fChars, writePos,
						kMaxChars, spacing);
					if (spacing != 0 && triggerOffset != 0
						&& resultOffset != 0) {
						pairs[0] = triggerOffset;
						pairs[1] = resultOffset;
						slot = 2;
					}
				}

				for (int c = 0; c < candidateCount && slot + 1 < 32; c++) {
					int32 triggerOffset = append_codepoint(fChars, writePos,
						kMaxChars, candidates[c].baseUcs4);
					int32 resultOffset = append_chars(fChars, writePos,
						kMaxChars, candidates[c].resultUtf8,
						candidates[c].resultLen);
					if (triggerOffset == 0 || resultOffset == 0)
						continue;	// fChars overflow; drop this pair
					pairs[slot++] = triggerOffset;
					pairs[slot++] = resultOffset;
				}
			}

			xkb_compose_table_unref(composeTable);
		}
	}

	fCharsSize = writePos;

	xkb_state_unref(state);

	return B_OK;
}


status_t
BKeymap::PopulateFromXkbNames(const char* rules, const char* model,
	const char* layout, const char* variant, const char* options)
{
	struct xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (context == NULL)
		return B_NO_MEMORY;

	struct xkb_rule_names names = { rules, model, layout, variant, options };
	struct xkb_keymap* keymap = xkb_keymap_new_from_names(context, &names,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (keymap == NULL) {
		struct xkb_rule_names fallback = { "evdev", "pc105", "us", "", "" };
		keymap = xkb_keymap_new_from_names(context, &fallback,
			XKB_KEYMAP_COMPILE_NO_FLAGS);
	}
	if (keymap == NULL) {
		xkb_context_unref(context);
		return B_ERROR;
	}

	status_t status = PopulateFromXkb(keymap);

	xkb_keymap_unref(keymap);
	xkb_context_unref(context);

	return status;
}
