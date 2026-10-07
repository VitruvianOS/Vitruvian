/*
 * Copyright 2004-2015 Haiku, Inc. All rights reserved.
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Alexandre Deckner, alex@zappotek.com
 *		Axel Dörfler, axeld@pinc-software.de
 *		Jérôme Duval
 *		John Scipione, jscipione@gmai.com
 *		Sandor Vroemisse
 *		Dario Casalinuovo
 */


#include "KeymapWindow.h"

#include <string.h>
#include <stdio.h>

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <List.h>
#include <ListView.h>
#include <Locale.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <ObjectList.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Screen.h>
#include <ScrollView.h>
#include <StringList.h>
#include <StringView.h>
#include <TextControl.h>

#include "KeyboardLayoutNames.h"
#include "KeyboardLayoutView.h"
#include "KeymapApplication.h"
#include "KeymapListItem.h"
#include "KeymapNames.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Keymap window"


static const uint32 kMsgMenuFileOpen = 'mMFO';
static const uint32 kMsgMenuFileSaveAs = 'mMFA';

static const uint32 kMsgSwitchShortcuts = 'swSc';

static const uint32 kMsgMenuFontChanged = 'mMFC';

static const uint32 kMsgSystemMapSelected = 'SmST';
static const uint32 kMsgUserMapSelected = 'UmST';
static const uint32 kMsgModelSelected = 'MdST';

static const uint32 kMsgDefaultKeymap = 'Dflt';
static const uint32 kMsgRevertKeymap = 'Rvrt';
static const uint32 kMsgKeymapUpdated = 'kMup';

static const uint32 kMsgDeadKeyAcuteChanged = 'dkAc';
static const uint32 kMsgDeadKeyCircumflexChanged = 'dkCc';
static const uint32 kMsgDeadKeyDiaeresisChanged = 'dkDc';
static const uint32 kMsgDeadKeyGraveChanged = 'dkGc';
static const uint32 kMsgDeadKeyTildeChanged = 'dkTc';

static const char* kDeadKeyTriggerNone = "<none>";

static const char* kCurrentKeymapName = "(Current)";
static const char* kDefaultKeymapName = "US-International";

static const float kDefaultHeight = 440;
static const float kDefaultWidth = 1000;

static int
compare_key_list_items(const void* a, const void* b)
{
	KeymapListItem* item1 = *(KeymapListItem**)a;
	KeymapListItem* item2 = *(KeymapListItem**)b;
	return BLocale::Default()->StringCompare(item1->Text(), item2->Text());
}


// A Generic row's XkbId() has no variant part, i.e. no second colon
// after the "xkb:" prefix. Checked structurally rather than against the
// translated label.
static bool
is_generic_xkb_id(const BString& id)
{
	int32 firstColon = id.FindFirst(':');
	if (firstColon < 0)
		return false;

	return id.FindFirst(':', firstColon + 1) < 0;
}


// BOutlineListView's sort hooks take BListItem* pairs, not void* pairs.
static int
compare_key_list_items_outline(const BListItem* a, const BListItem* b)
{
	const KeymapListItem* item1 = static_cast<const KeymapListItem*>(a);
	const KeymapListItem* item2 = static_cast<const KeymapListItem*>(b);

	// Sort the Generic row before its siblings rather than alphabetically.
	bool generic1 = is_generic_xkb_id(item1->XkbId());
	bool generic2 = is_generic_xkb_id(item2->XkbId());
	if (generic1 != generic2)
		return generic1 ? -1 : 1;

	return BLocale::Default()->StringCompare(item1->Text(), item2->Text());
}


// Mirrors KeyboardInputDevice::_RebuildXkb()'s reading of the same file, so
// the model menu opens already marking what is actually in effect.
static void
read_persisted_xkb_model(BString& _model)
{
	_model = "";

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;
	path.Append("input/xkb_layout");

	FILE* file = fopen(path.Path(), "r");
	if (file == NULL)
		return;

	static const char* kKey = "model=";
	size_t keyLen = strlen(kKey);

	char line[512];
	while (fgets(line, sizeof(line), file) != NULL) {
		char* newline = strchr(line, '\n');
		if (newline != NULL)
			*newline = '\0';

		if (strncmp(line, kKey, keyLen) == 0) {
			_model = line + keyLen;
			break;
		}
	}

	fclose(file);
}

KeymapWindow::KeymapWindow()
	:
	BWindow(BRect(80, 50, kDefaultWidth, kDefaultHeight),
		B_TRANSLATE_SYSTEM_NAME("Keymap"), B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fUserPickedLayout(false)
{
	// If the window doesn't fit the screen, make it smaller but keep the
	// aspect ratio
	BScreen screen(this);
	display_mode mode;
	status_t status = screen.GetMode(&mode);
	if(status == B_OK && (mode.virtual_width <= kDefaultWidth
			|| mode.virtual_height <= kDefaultHeight)) {
		float width = mode.virtual_width - 64;
		ResizeTo(mode.virtual_width - 64,
			width * kDefaultHeight / kDefaultWidth);
	}

	// So the model menu opens already marking what is actually in effect.
	BString persistedModel;
	read_persisted_xkb_model(persistedModel);
	if (!persistedModel.IsEmpty())
		fCurrentMap.SetModel(persistedModel.String());

	fKeyboardLayoutView = new KeyboardLayoutView("layout");
	fKeyboardLayoutView->SetKeymap(&fCurrentMap);
	fKeyboardLayoutView->SetExplicitMinSize(BSize(B_SIZE_UNSET,
		192 * be_plain_font->Size() / 12.0f));

	fTextControl = new BTextControl(B_TRANSLATE("Sample and clipboard:"),
		"", NULL);

	fSwitchShortcutsButton = new BButton("switch", "",
		new BMessage(kMsgSwitchShortcuts));

	// controls pane
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(_CreateMenu())
		.AddGroup(B_HORIZONTAL)
			.SetInsets(B_USE_WINDOW_SPACING)
			.Add(_CreateMapLists(), 0.25)
			.AddGroup(B_VERTICAL)
				.Add(fKeyboardLayoutView)
				.AddGroup(B_HORIZONTAL)
					.Add(_CreateDeadKeyMenuField(), 0.0)
					.AddGlue()
					.Add(fSwitchShortcutsButton)
					.End()
				.Add(fTextControl)
				.AddGlue(0.0)
				.AddGroup(B_HORIZONTAL)
					.AddGlue()
					.Add(fDefaultsButton = new BButton("defaultsButton",
						B_TRANSLATE("Defaults"),
							new BMessage(kMsgDefaultKeymap)))
					.Add(fRevertButton = new BButton("revertButton",
						B_TRANSLATE("Revert"), new BMessage(kMsgRevertKeymap)))
					.End()
				.End()
			.End()
		.End();

	fKeyboardLayoutView->SetTarget(fTextControl->TextView());
	fTextControl->MakeFocus();

	// Make sure the user keymap directory exists
	BPath path;
	find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	path.Append("Keymap");

	entry_ref ref;
	BEntry entry(path.Path(), true); // follow symlink
	BDirectory userKeymapsDir(&entry);
	if (userKeymapsDir.InitCheck() != B_OK
		&& create_directory(path.Path(), S_IRWXU | S_IRWXG | S_IRWXO)
			== B_OK) {
		get_ref_for_path(path.Path(), &ref);
	} else if (entry.InitCheck() == B_OK)
		entry.GetRef(&ref);
	else
		get_ref_for_path(path.Path(), &ref);

#ifndef __VOS__
	BMessenger messenger(this);
	fOpenPanel = new BFilePanel(B_OPEN_PANEL, &messenger, &ref,
		B_FILE_NODE, false, NULL);
	fSavePanel = new BFilePanel(B_SAVE_PANEL, &messenger, &ref,
		B_FILE_NODE, false, NULL);
#endif

	BRect windowFrame;
	if (_LoadSettings(windowFrame) == B_OK) {
		ResizeTo(windowFrame.Width(), windowFrame.Height());
		MoveTo(windowFrame.LeftTop());
		MoveOnScreen();
	} else
		CenterOnScreen();

	// TODO: this might be a bug in the interface kit, but scrolling to
	// selection does not correctly work unless the window is shown.
	Show();
	Lock();

	// Try and find the current map name in the two list views (if the name
	// was read at all)
	_SelectCurrentMap();

	KeymapListItem* current
		= static_cast<KeymapListItem*>(fUserListView->FirstItem());

	fCurrentMap.Load(current->EntryRef());
	fPreviousMap = fCurrentMap;
	fAppliedMap = fCurrentMap;
	fCurrentMap.SetTarget(this, new BMessage(kMsgKeymapUpdated));

	_AutoPickKeyboardTemplate();

	_UpdateButtons();

	_UpdateDeadKeyMenu();
	_UpdateSwitchShortcutButton();

	Unlock();
}


KeymapWindow::~KeymapWindow()
{
#ifndef __VOS__
	delete fOpenPanel;
	delete fSavePanel;
#endif
}


bool
KeymapWindow::QuitRequested()
{
	_SaveSettings();

	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
KeymapWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_SIMPLE_DATA:
		case B_REFS_RECEIVED:
		{
			entry_ref ref;
			int32 i = 0;
			while (message->FindRef("refs", i++, &ref) == B_OK) {
				fCurrentMap.Load(ref);
				fAppliedMap = fCurrentMap;
			}
			fKeyboardLayoutView->SetKeymap(&fCurrentMap);
			fSystemListView->DeselectAll();
			fUserListView->DeselectAll();
			break;
		}

#ifndef __VOS__
		case B_SAVE_REQUESTED:
		{
			entry_ref ref;
			const char* name;
			if (message->FindRef("directory", &ref) == B_OK
				&& message->FindString("name", &name) == B_OK) {
				BDirectory directory(&ref);
				BEntry entry(&directory, name);
				entry.GetRef(&ref);
				fCurrentMap.SetName(name);
				fCurrentMap.Save(ref);
				fAppliedMap = fCurrentMap;
				_FillUserMaps();
				fCurrentMapName = name;
				_SelectCurrentMap();
			}
			break;
		}

		case kMsgMenuFileOpen:
			fOpenPanel->Show();
			break;
		case kMsgMenuFileSaveAs:
			fSavePanel->Show();
			break;
#endif
		case kMsgShowModifierKeysWindow:
			be_app->PostMessage(kMsgShowModifierKeysWindow);
			break;

		case kMsgSwitchShortcuts:
			_SwitchShortcutKeys();
			break;

		case kMsgModelSelected:
		{
			// A shape-only entry means exactly that: redraw, touch nothing
			// xkb knows about.
			BString shape;
			if (message->FindString("model:shape", &shape) == B_OK) {
				BPath shapePath;
				if (_FindKeyboardLayoutPath(shape.String(), shapePath)
						== B_OK) {
					fUserPickedLayout = true;
					_SetKeyboardLayout(shapePath.Path());
				}
				break;
			}

			BString id;
			if (message->FindString("model:id", &id) != B_OK)
				break;

			// Choosing a real model is a fresh answer to "what keyboard is
			// this", so it takes the drawing back from any hand-picked shape.
			fUserPickedLayout = false;

			const char* layout;
			const char* variant;
			look_up_xkb_layout(fCurrentMap.LayoutName(), layout, variant);
			const char* options
				= look_up_xkb_modifier_options(fCurrentMap.Map());

			// Compile before committing the model, or a pair xkb rejects
			// leaves the menu marking a model that is not in effect.
			BString previousModel(fCurrentMap.Model());
			fCurrentMap.SetModel(id.String());

			if (fCurrentMap.PopulateFromXkbNames("evdev", fCurrentMap.Model(),
					layout, variant, options) != B_OK) {
				fCurrentMap.SetModel(previousModel.String());
				_MarkCurrentModel();
				break;
			}

			fAppliedMap = fCurrentMap;
			fKeyboardLayoutView->SetKeymap(&fCurrentMap);
			// _UseKeymap() already writes the xkb layout and activates
			// once; see its body.
			_UseKeymap();
			_AutoPickKeyboardTemplate();
			_UpdateButtons();
			break;
		}

		case kMsgMenuFontChanged:
		{
			BMenuItem* item = fFontMenu->FindMarked();
			if (item != NULL) {
				BFont font;
				font.SetFamilyAndStyle(item->Label(), NULL);
				fKeyboardLayoutView->SetBaseFont(font);
				fTextControl->TextView()->SetFontAndColor(&font);
			}
			break;
		}

		case kMsgSystemMapSelected:
		{
			// Every row is xkb-derived now, so this always routes through
			// _XkbLayoutSelected() instead of duplicating its logic.
			int32 index = fSystemListView->CurrentSelection();
			if (index < 0)
				break;

			fUserListView->DeselectAll();

			KeymapListItem* item = static_cast<KeymapListItem*>(
				fSystemListView->ItemAt(index));
			if (item != NULL) {
				_XkbLayoutSelected(item->XkbId().String(),
					item->EntryRef().name);
			}

			break;
		}

		case kMsgUserMapSelected:
		{
			int32 index = fUserListView->CurrentSelection();
			if (index < 0)
				break;

			// Deselect item in the other list
			fSystemListView->DeselectAll();

			if (index == 0) {
				// we can safely ignore the "(Current)" item
				break;
			}

			KeymapListItem* item
				= static_cast<KeymapListItem*>(fUserListView->ItemAt(index));
			if (item != NULL) {
				status_t status = fCurrentMap.Load(item->EntryRef());
				if (status != B_OK) {
					fUserListView->RemoveItem(item);
					break;
				}

				fAppliedMap = fCurrentMap;
				fKeyboardLayoutView->SetKeymap(&fCurrentMap);
				// _UseKeymap() already writes the xkb layout and
				// activates once; see its body.
				_UseKeymap();
				_AutoPickKeyboardTemplate();
				_UpdateButtons();
			}
			break;
		}

		case kMsgDefaultKeymap:
			_DefaultKeymap();
			_UpdateButtons();
			break;

		case kMsgRevertKeymap:
			_RevertKeymap();
			_UpdateButtons();
			break;

		case kMsgUpdateNormalKeys:
		{
			uint32 keyCode;
			if (message->FindUInt32("keyCode", &keyCode) != B_OK)
				break;

			bool unset;
			if (message->FindBool("unset", &unset) == B_OK && unset) {
				fCurrentMap.SetKey(keyCode, modifiers(), 0, "", 0);
				_UpdateButtons();
				fKeyboardLayoutView->SetKeymap(&fCurrentMap);
			}
			break;
		}

		case kMsgUpdateModifierKeys:
		{
			uint32 keyCode;
			bool unset;
			if (message->FindBool("unset", &unset) != B_OK)
				unset = false;

			if (message->FindUInt32("left_shift_key", &keyCode) == B_OK) {
				fCurrentMap.SetModifier(unset ? 0x00 : keyCode,
					B_LEFT_SHIFT_KEY);
			}

			if (message->FindUInt32("right_shift_key", &keyCode) == B_OK) {
				fCurrentMap.SetModifier(unset ? 0x00 : keyCode,
					B_RIGHT_SHIFT_KEY);
			}

			if (message->FindUInt32("left_control_key", &keyCode) == B_OK) {
				fCurrentMap.SetModifier(unset ? 0x00 : keyCode,
					B_LEFT_CONTROL_KEY);
			}

			if (message->FindUInt32("right_control_key", &keyCode) == B_OK) {
				fCurrentMap.SetModifier(unset ? 0x00 : keyCode,
					B_RIGHT_CONTROL_KEY);
			}

			if (message->FindUInt32("left_option_key", &keyCode) == B_OK) {
				fCurrentMap.SetModifier(unset ? 0x00 : keyCode,
					B_LEFT_OPTION_KEY);
			}

			if (message->FindUInt32("right_option_key", &keyCode) == B_OK) {
				fCurrentMap.SetModifier(unset ? 0x00 : keyCode,
					B_RIGHT_OPTION_KEY);
			}

			if (message->FindUInt32("left_command_key", &keyCode) == B_OK) {
				fCurrentMap.SetModifier(unset ? 0x00 : keyCode,
					B_LEFT_COMMAND_KEY);
			}

			if (message->FindUInt32("right_command_key", &keyCode) == B_OK) {
				fCurrentMap.SetModifier(unset ? 0x00 : keyCode,
					B_RIGHT_COMMAND_KEY);
			}

			if (message->FindUInt32("menu_key", &keyCode) == B_OK)
				fCurrentMap.SetModifier(unset ? 0x00 : keyCode, B_MENU_KEY);

			if (message->FindUInt32("caps_key", &keyCode) == B_OK)
				fCurrentMap.SetModifier(unset ? 0x00 : keyCode, B_CAPS_LOCK);

			if (message->FindUInt32("num_key", &keyCode) == B_OK)
				fCurrentMap.SetModifier(unset ? 0x00 : keyCode, B_NUM_LOCK);

			if (message->FindUInt32("scroll_key", &keyCode) == B_OK)
				fCurrentMap.SetModifier(unset ? 0x00 : keyCode, B_SCROLL_LOCK);

			// Without this the new role reaches key_map but never
			// character generation. Write it before _UpdateButtons(),
			// whose _UseKeymap() is the one activation this path gets.
			fCurrentMap.WriteXkbLayout();
			_UpdateButtons();
			fKeyboardLayoutView->SetKeymap(&fCurrentMap);
			break;
		}

		case kMsgKeymapUpdated:
			_UpdateButtons();
			fSystemListView->DeselectAll();
			fUserListView->Select(0L);
			break;

		case kMsgDeadKeyAcuteChanged:
		{
			BMenuItem* item = fAcuteMenu->FindMarked();
			if (item != NULL) {
				const char* trigger = item->Label();
				if (strcmp(trigger, kDeadKeyTriggerNone) == 0)
					trigger = NULL;
				fCurrentMap.SetDeadKeyTrigger(kDeadKeyAcute, trigger);
				fKeyboardLayoutView->Invalidate();
			}
			break;
		}

		case kMsgDeadKeyCircumflexChanged:
		{
			BMenuItem* item = fCircumflexMenu->FindMarked();
			if (item != NULL) {
				const char* trigger = item->Label();
				if (strcmp(trigger, kDeadKeyTriggerNone) == 0)
					trigger = NULL;
				fCurrentMap.SetDeadKeyTrigger(kDeadKeyCircumflex, trigger);
				fKeyboardLayoutView->Invalidate();
			}
			break;
		}

		case kMsgDeadKeyDiaeresisChanged:
		{
			BMenuItem* item = fDiaeresisMenu->FindMarked();
			if (item != NULL) {
				const char* trigger = item->Label();
				if (strcmp(trigger, kDeadKeyTriggerNone) == 0)
					trigger = NULL;
				fCurrentMap.SetDeadKeyTrigger(kDeadKeyDiaeresis, trigger);
				fKeyboardLayoutView->Invalidate();
			}
			break;
		}

		case kMsgDeadKeyGraveChanged:
		{
			BMenuItem* item = fGraveMenu->FindMarked();
			if (item != NULL) {
				const char* trigger = item->Label();
				if (strcmp(trigger, kDeadKeyTriggerNone) == 0)
					trigger = NULL;
				fCurrentMap.SetDeadKeyTrigger(kDeadKeyGrave, trigger);
				fKeyboardLayoutView->Invalidate();
			}
			break;
		}

		case kMsgDeadKeyTildeChanged:
		{
			BMenuItem* item = fTildeMenu->FindMarked();
			if (item != NULL) {
				const char* trigger = item->Label();
				if (strcmp(trigger, kDeadKeyTriggerNone) == 0)
					trigger = NULL;
				fCurrentMap.SetDeadKeyTrigger(kDeadKeyTilde, trigger);
				fKeyboardLayoutView->Invalidate();
			}
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


BMenuBar*
KeymapWindow::_CreateMenu()
{
	BMenuBar* menuBar = new BMenuBar(Bounds(), "menubar");

	// Create the File menu
	BMenu* menu = new BMenu(B_TRANSLATE("File"));
#ifndef __VOS__
	menu->AddItem(new BMenuItem(B_TRANSLATE("Open" B_UTF8_ELLIPSIS),
		new BMessage(kMsgMenuFileOpen), 'O'));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Save as" B_UTF8_ELLIPSIS),
		new BMessage(kMsgMenuFileSaveAs)));
	menu->AddSeparatorItem();
#endif
	menu->AddItem(new BMenuItem(
		B_TRANSLATE("Set modifier keys" B_UTF8_ELLIPSIS),
		new BMessage(kMsgShowModifierKeysWindow)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(B_QUIT_REQUESTED), 'Q'));
	menuBar->AddItem(menu);

	// Create keyboard model menu
	fModelMenu = new BMenu(B_TRANSLATE("Keyboard model"));
	_AddModelMenu(fModelMenu);
	_MarkCurrentModel();
	menuBar->AddItem(fModelMenu);

	// Create the Font menu
	fFontMenu = new BMenu(B_TRANSLATE("Font"));
	fFontMenu->SetRadioMode(true);
	int32 numFamilies = count_font_families();
	font_family family, currentFamily;
	font_style currentStyle;
	uint32 flags;

	be_plain_font->GetFamilyAndStyle(&currentFamily, &currentStyle);

	for (int32 i = 0; i < numFamilies; i++) {
		if (get_font_family(i, &family, &flags) == B_OK) {
			BMenuItem* item
				= new BMenuItem(family, new BMessage(kMsgMenuFontChanged));
			fFontMenu->AddItem(item);

			if (!strcmp(family, currentFamily))
				item->SetMarked(true);
		}
	}
	menuBar->AddItem(fFontMenu);

	return menuBar;
}


BMenuField*
KeymapWindow::_CreateDeadKeyMenuField()
{
	BPopUpMenu* deadKeyMenu = new BPopUpMenu(B_TRANSLATE("Select dead keys"),
		false, false);

	fAcuteMenu = new BMenu(B_TRANSLATE("Acute trigger"));
	fAcuteMenu->SetRadioMode(true);
	fAcuteMenu->AddItem(new BMenuItem("\xC2\xB4",
		new BMessage(kMsgDeadKeyAcuteChanged)));
	fAcuteMenu->AddItem(new BMenuItem("'",
		new BMessage(kMsgDeadKeyAcuteChanged)));
	fAcuteMenu->AddItem(new BMenuItem(kDeadKeyTriggerNone,
		new BMessage(kMsgDeadKeyAcuteChanged)));
	deadKeyMenu->AddItem(fAcuteMenu);

	fCircumflexMenu = new BMenu(B_TRANSLATE("Circumflex trigger"));
	fCircumflexMenu->SetRadioMode(true);
	fCircumflexMenu->AddItem(new BMenuItem("^",
		new BMessage(kMsgDeadKeyCircumflexChanged)));
	fCircumflexMenu->AddItem(new BMenuItem(kDeadKeyTriggerNone,
		new BMessage(kMsgDeadKeyCircumflexChanged)));
	deadKeyMenu->AddItem(fCircumflexMenu);

	fDiaeresisMenu = new BMenu(B_TRANSLATE("Diaeresis trigger"));
	fDiaeresisMenu->SetRadioMode(true);
	fDiaeresisMenu->AddItem(new BMenuItem("\xC2\xA8",
		new BMessage(kMsgDeadKeyDiaeresisChanged)));
	fDiaeresisMenu->AddItem(new BMenuItem("\"",
		new BMessage(kMsgDeadKeyDiaeresisChanged)));
	fDiaeresisMenu->AddItem(new BMenuItem(kDeadKeyTriggerNone,
		new BMessage(kMsgDeadKeyDiaeresisChanged)));
	deadKeyMenu->AddItem(fDiaeresisMenu);

	fGraveMenu = new BMenu(B_TRANSLATE("Grave trigger"));
	fGraveMenu->SetRadioMode(true);
	fGraveMenu->AddItem(new BMenuItem("`",
		new BMessage(kMsgDeadKeyGraveChanged)));
	fGraveMenu->AddItem(new BMenuItem(kDeadKeyTriggerNone,
		new BMessage(kMsgDeadKeyGraveChanged)));
	deadKeyMenu->AddItem(fGraveMenu);

	fTildeMenu = new BMenu(B_TRANSLATE("Tilde trigger"));
	fTildeMenu->SetRadioMode(true);
	fTildeMenu->AddItem(new BMenuItem("~",
		new BMessage(kMsgDeadKeyTildeChanged)));
	fTildeMenu->AddItem(new BMenuItem(kDeadKeyTriggerNone,
		new BMessage(kMsgDeadKeyTildeChanged)));
	deadKeyMenu->AddItem(fTildeMenu);

	return new BMenuField(NULL, deadKeyMenu);
}


// BOutlineListView::Expand()/Collapse() are not virtual; override
// ExpandOrCollapse() to guard the collapse side against the base class
// reselecting a hidden variant's parent. Expanding never changes the
// selection.
class SystemListView : public BOutlineListView {
public:
								SystemListView();

protected:
	virtual	void				ExpandOrCollapse(BListItem* item,
									bool expand);
};


SystemListView::SystemListView()
	:
	BOutlineListView("systemList")
{
}


void
SystemListView::ExpandOrCollapse(BListItem* item, bool expand)
{
	KeymapListItem* base = static_cast<KeymapListItem*>(item);
	KeymapListItem* selected = static_cast<KeymapListItem*>(
		ItemAt(CurrentSelection()));

	if (expand) {
		BOutlineListView::ExpandOrCollapse(item, expand);
		return;
	}

	// Don't let the base class fall back to the parent on collapse (would
	// silently apply the plain layout). Deselect first to prevent
	// reselection; the Generic child is the exception, since it shares
	// its parent's xkb id.
	bool selectedIsUnder = selected != NULL && Superitem(selected) == item;
	bool selectedIsGeneric = selectedIsUnder
		&& selected->XkbId() == base->XkbId();

	if (selectedIsUnder && !selectedIsGeneric)
		selected->Deselect();

	BOutlineListView::ExpandOrCollapse(item, expand);

	if (selectedIsGeneric && CurrentSelection() != IndexOf(item))
		Select(IndexOf(item));
}


BView*
KeymapWindow::_CreateMapLists()
{
	// The System list: base layouts as parents, variants as children.
	fSystemListView = new SystemListView();
	fSystemListView->SetSelectionMessage(new BMessage(kMsgSystemMapSelected));

	BScrollView* systemScroller = new BScrollView("systemScrollList",
		fSystemListView, 0, false, true);

	// The User list
	fUserListView = new BListView("userList");
	fUserListView->SetSelectionMessage(new BMessage(kMsgUserMapSelected));
	BScrollView* userScroller = new BScrollView("userScrollList",
		fUserListView, 0, false, true);

	// Saved keymaps

	_FillSystemMaps();
	_FillUserMaps();

	_SetListViewSize(fSystemListView);
	_SetListViewSize(fUserListView);

	return BLayoutBuilder::Group<>(B_VERTICAL)
		.Add(new BStringView("system", B_TRANSLATE("System:")))
		.Add(systemScroller, 3)
		.Add(new BStringView("user", B_TRANSLATE("User:")))
		.Add(userScroller)
		.View();
}


// Geometry file for each xkb model we ship; others fall back to an
// ISO/ANSI guess.
static const struct {
	const char*	model;
	const char*	geometry;
} kModelGeometry[] = {
	{ "pc104",			"Generic 104-key" },
	{ "pc104alt",		"Generic 104-key" },
	{ "pc105",			"Generic 105-key International" },
	{ "thinkpad",		"ThinkPad" },
	{ "thinkpad60",		"ThinkPad" },
	{ "thinkpadz60",	"ThinkPad" },
	{ "applealu_ansi",	"Apple Aluminum" },
	{ "applealu_iso",	"Apple Aluminum" },
	{ "applealu_jis",	"Apple Aluminum" },
	{ "kinesis",		"Kinesis Advantage" },
	{ "tm2030PS2",		"TypeMatrix 2030" },
	{ "tm2030USB",		"TypeMatrix 2030" },
	{ "tm2030USB-102",	"TypeMatrix 2030" },
	{ "tm2030USB-106",	"TypeMatrix 2030" },
};

// Shapes we ship that xkb has no model for; picking one changes only
// the drawing, not the xkb model.
static const char* kShapeOnlyGeometries[] = {
	"Fizzbook NL2",
	"Kinesis Ergo Elan International",
	"X-Bows Nature",
};


static const char*
geometry_for_model(const char* model)
{
	if (model == NULL)
		return NULL;

	for (size_t i = 0;
			i < sizeof(kModelGeometry) / sizeof(kModelGeometry[0]); i++) {
		if (strcmp(kModelGeometry[i].model, model) == 0)
			return kModelGeometry[i].geometry;
	}

	return NULL;
}


/*!	Looks up a named keyboard layout file under the data directories, without
	going through a menu.
*/
status_t
KeymapWindow::_FindKeyboardLayoutPath(const char* name, BPath& _path)
{
	directory_which dataDirectories[] = {
		B_USER_NONPACKAGED_DATA_DIRECTORY,
		B_USER_DATA_DIRECTORY,
		B_SYSTEM_NONPACKAGED_DATA_DIRECTORY,
		B_SYSTEM_DATA_DIRECTORY,
	};

	for (uint32 i = 0;
			i < sizeof(dataDirectories) / sizeof(dataDirectories[0]); i++) {
		BPath path;
		if (find_directory(dataDirectories[i], &path) != B_OK)
			continue;
		if (path.Append("KeyboardLayouts") != B_OK)
			continue;
		if (path.Append(name) != B_OK)
			continue;

		BEntry entry(path.Path());
		if (entry.Exists()) {
			_path = path;
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


/*!	Picks the drawn geometry from the selected model when we ship its shape,
	otherwise from whether the current keymap has a 102nd key (ISO) or not
	(ANSI). A shape picked by hand wins over both for the rest of the session.
*/
void
KeymapWindow::_AutoPickKeyboardTemplate()
{
	if (fUserPickedLayout)
		return;

	const char* named = geometry_for_model(fCurrentMap.Model());
	if (named != NULL) {
		BPath namedPath;
		if (_FindKeyboardLayoutPath(named, namedPath) == B_OK) {
			_SetKeyboardLayout(namedPath.Path());
			return;
		}
	}

	char* chars = NULL;
	int32 numBytes = 0;
	fCurrentMap.GetChars(0x69, 0, 0, &chars, &numBytes);
	bool hasIsoKey = chars != NULL && numBytes > 0;
	delete[] chars;

	BPath path;
	const char* generic = hasIsoKey
		? "Generic 105-key International" : "Generic 104-key";
	// Unrepresentable keyboard: fall back to the generic shape, since the
	// previous one would carry the wrong label.
	if (_FindKeyboardLayoutPath(generic, path) != B_OK)
		_SetKeyboardLayout(NULL);
	else
		_SetKeyboardLayout(path.Path());
}


/*!	Marks the model actually in effect. Kept apart from _AddModelMenu()
	so re-marking never requires rebuilding the menu.
*/
void
KeymapWindow::_MarkCurrentModel()
{
	if (fModelMenu == NULL)
		return;

	BString currentModel(fCurrentMap.Model());
	for (int32 i = 0; i < fModelMenu->CountItems(); i++) {
		BMenuItem* item = fModelMenu->ItemAt(i);
		if (item == NULL || item->Message() == NULL)
			continue;

		BString id;
		if (item->Message()->FindString("model:id", &id) != B_OK)
			continue;

		if (id == currentModel) {
			item->SetMarked(true);
			return;
		}
	}
}


void
KeymapWindow::_AddModelMenu(BMenu* menu)
{
	BObjectList<xkb_model_entry, true> catalog;
	if (get_xkb_model_catalog(catalog) != B_OK)
		return;

	menu->SetRadioMode(true);

	for (int32 i = 0; i < catalog.CountItems(); i++) {
		xkb_model_entry* entry = catalog.ItemAt(i);

		BMessage* message = new BMessage(kMsgModelSelected);
		message->AddString("model:id", entry->id);

		menu->AddItem(new BMenuItem(entry->label.String(), message));
	}

	// Shapes with no xkb model carry "model:shape" instead of "model:id";
	// the handler for that case touches only the drawing.
	menu->AddSeparatorItem();
	for (size_t i = 0;
			i < sizeof(kShapeOnlyGeometries) / sizeof(kShapeOnlyGeometries[0]);
			i++) {
		BMessage* message = new BMessage(kMsgModelSelected);
		message->AddString("model:shape", kShapeOnlyGeometries[i]);
		menu->AddItem(new BMenuItem(kShapeOnlyGeometries[i], message));
	}
}


status_t
KeymapWindow::_SetKeyboardLayout(const char* path)
{
	status_t status = fKeyboardLayoutView->GetKeyboardLayout()->Load(path);

	if (path == NULL || path[0] == '\0' || status != B_OK)
		fKeyboardLayoutView->GetKeyboardLayout()->SetDefault();
	else
		fKeyboardLayoutPath = path;

	// Refresh currently set layout
	fKeyboardLayoutView->SetKeyboardLayout(
		fKeyboardLayoutView->GetKeyboardLayout());

	return status;
}

// The catalog has no keymap files to Load(), so every row funnels
// through here to compile live instead.
void
KeymapWindow::_XkbLayoutSelected(const char* id, const char* label)
{
	if (id == NULL || id[0] == '\0')
		return;

	// Base and Generic child share an id; skip the redundant rebuild.
	// (fCurrentMap.LayoutName() is empty on the first run.)
	if (strcmp(id, fCurrentMap.LayoutName()) == 0)
		return;

	const char* layout;
	const char* variant;
	look_up_xkb_layout(id, layout, variant);

	// Keep the current Ctrl/Command arrangement; the preview must match
	// _RebuildXkb()'s output.
	const char* options = look_up_xkb_modifier_options(fCurrentMap.Map());
	if (fCurrentMap.PopulateFromXkbNames("evdev", fCurrentMap.Model(), layout,
			variant, options) != B_OK) {
		return;
	}

	fCurrentMap.SetName(label != NULL && label[0] != '\0' ? label : id);
	// The id, not the label, is what look_up_xkb_layout() resolves.
	fCurrentMap.SetLayoutName(id);
	fAppliedMap = fCurrentMap;
	fKeyboardLayoutView->SetKeymap(&fCurrentMap);

	fUserListView->DeselectAll();

	// _UseKeymap() already writes the xkb layout and activates once;
	// see its body.
	_UseKeymap();
	_AutoPickKeyboardTemplate();
	_UpdateButtons();
}


/*!	Sets the label of the "Switch Shorcuts" button to make it more
	descriptive what will happen when you press that button.
*/
void
KeymapWindow::_UpdateSwitchShortcutButton()
{
	const char* label = B_TRANSLATE("Switch shortcut keys");
	if (fCurrentMap.KeyForModifier(B_LEFT_COMMAND_KEY) == 0x5d
		&& fCurrentMap.KeyForModifier(B_LEFT_CONTROL_KEY) == 0x5c) {
		label = B_TRANSLATE("Switch shortcut keys to Linux mode");
	} else if (fCurrentMap.KeyForModifier(B_LEFT_COMMAND_KEY) == 0x5c
		&& fCurrentMap.KeyForModifier(B_LEFT_CONTROL_KEY) == 0x5d) {
		label = B_TRANSLATE("Switch shortcut keys to Classic mode");
	}

	fSwitchShortcutsButton->SetLabel(label);
}


/*!	Marks the menu items corresponding to the dead key state of the current
	key map.
*/
void
KeymapWindow::_UpdateDeadKeyMenu()
{
	BString trigger;
	fCurrentMap.GetDeadKeyTrigger(kDeadKeyAcute, trigger);
	if (!trigger.Length())
		trigger = kDeadKeyTriggerNone;
	BMenuItem* menuItem = fAcuteMenu->FindItem(trigger.String());
	if (menuItem)
		menuItem->SetMarked(true);

	fCurrentMap.GetDeadKeyTrigger(kDeadKeyCircumflex, trigger);
	if (!trigger.Length())
		trigger = kDeadKeyTriggerNone;
	menuItem = fCircumflexMenu->FindItem(trigger.String());
	if (menuItem)
		menuItem->SetMarked(true);

	fCurrentMap.GetDeadKeyTrigger(kDeadKeyDiaeresis, trigger);
	if (!trigger.Length())
		trigger = kDeadKeyTriggerNone;
	menuItem = fDiaeresisMenu->FindItem(trigger.String());
	if (menuItem)
		menuItem->SetMarked(true);

	fCurrentMap.GetDeadKeyTrigger(kDeadKeyGrave, trigger);
	if (!trigger.Length())
		trigger = kDeadKeyTriggerNone;
	menuItem = fGraveMenu->FindItem(trigger.String());
	if (menuItem)
		menuItem->SetMarked(true);

	fCurrentMap.GetDeadKeyTrigger(kDeadKeyTilde, trigger);
	if (!trigger.Length())
		trigger = kDeadKeyTriggerNone;
	menuItem = fTildeMenu->FindItem(trigger.String());
	if (menuItem)
		menuItem->SetMarked(true);
}


void
KeymapWindow::_UpdateButtons()
{
	if (fCurrentMap != fAppliedMap) {
		fCurrentMap.SetName(kCurrentKeymapName);
		_UseKeymap();
	}

	fDefaultsButton->SetEnabled(
		fCurrentMapName.ICompare(kDefaultKeymapName) != 0);
	fRevertButton->SetEnabled(fCurrentMap != fPreviousMap);

	_UpdateDeadKeyMenu();
	_UpdateSwitchShortcutButton();
}


void
KeymapWindow::_SwitchShortcutKeys()
{
	uint32 leftCommand = fCurrentMap.Map().left_command_key;
	uint32 leftControl = fCurrentMap.Map().left_control_key;
	uint32 rightCommand = fCurrentMap.Map().right_command_key;
	uint32 rightControl = fCurrentMap.Map().right_control_key;

	// switch left side
	fCurrentMap.Map().left_command_key = leftControl;
	fCurrentMap.Map().left_control_key = leftCommand;

	// switch right side
	fCurrentMap.Map().right_command_key = rightControl;
	fCurrentMap.Map().right_control_key = rightCommand;

	fKeyboardLayoutView->SetKeymap(&fCurrentMap);
	// Same reason as kMsgUpdateModifierKeys: this is the PC-mode swap, and
	// it only takes effect for characters once xkb is told about it.
	fCurrentMap.WriteXkbLayout();
	_UpdateButtons();
}


//!	Restores the default keymap.
void
KeymapWindow::_DefaultKeymap()
{
	// A real decision (Defaults button); RestoreSystemDefault() writes the
	// xkb layout and activates once.
	fCurrentMap.RestoreSystemDefault();
	fAppliedMap = fCurrentMap;

	fKeyboardLayoutView->SetKeymap(&fCurrentMap);

	fCurrentMapName = _GetActiveKeymapName();
	_SelectCurrentMap();
}


//!	Saves previous map to the "Key_map" file.
void
KeymapWindow::_RevertKeymap()
{
	entry_ref ref;
	_GetCurrentKeymap(ref);

	status_t status = fPreviousMap.Save(ref);
	if (status != B_OK) {
		printf("error when saving keymap: %s", strerror(status));
		return;
	}

	// A real decision (Revert button). Write before activating, so this
	// costs input_server one xkb recompile rather than two.
	fPreviousMap.WriteXkbLayout();
	fPreviousMap.Use();
	fCurrentMap.Load(ref);
	fAppliedMap = fCurrentMap;

	fKeyboardLayoutView->SetKeymap(&fCurrentMap);

	fCurrentMapName = _GetActiveKeymapName();
	_SelectCurrentMap();
}


//!	Saves current map to the "Key_map" file.
void
KeymapWindow::_UseKeymap()
{
	entry_ref ref;
	_GetCurrentKeymap(ref);

	status_t status = fCurrentMap.Save(ref);
	if (status != B_OK) {
		printf("error when saving : %s", strerror(status));
		return;
	}

	// Write every destination before the one activation below, or
	// input_server recompiles the whole xkb keymap twice per click.
	fCurrentMap.WriteXkbLayout();
	fCurrentMap.Use();
	fAppliedMap.Load(ref);

	fCurrentMapName = _GetActiveKeymapName();
	_SelectCurrentMap();
}

void
KeymapWindow::_FillSystemMaps()
{
	BListItem* item;
	while ((item = fSystemListView->RemoveItem(static_cast<int32>(0))) != NULL)
		delete item;

	BObjectList<xkb_catalog_entry, true> catalog;
	if (get_xkb_layout_catalog(catalog) != B_OK)
		return;

	// evdev.lst lists bases before variants, never adjacent, so key
	// bases by layout code.
	BStringList baseCodes;
	BList baseItems;

	for (int32 i = 0; i < catalog.CountItems(); i++) {
		xkb_catalog_entry* entry = catalog.ItemAt(i);

		BString layoutCode(entry->id);
		layoutCode.Remove(0, 4);	// drop "xkb:"
		int32 colon = layoutCode.FindFirst(':');
		bool hasVariant = colon >= 0;
		BString variantCode;
		if (hasVariant) {
			variantCode.SetTo(layoutCode.String() + colon + 1);
			layoutCode.Truncate(colon);
		}

		// Prefer the Haiku name; most xkb pairs have none.
		const char* haikuName = xkb_layout_name_for(layoutCode.String(),
			variantCode.String());
		BString label = haikuName != NULL
			? BString(B_TRANSLATE_NOCOLLECT_ALL(haikuName, "KeymapNames", NULL))
			: entry->label;

		// expanded = false: the catalog is ~590 rows, so every base layout
		// starts collapsed. AddUnder() keeps a collapsed parent's children out
		// of the display list, and the sort below preserves that.
		KeymapListItem* newItem = new KeymapListItem(entry->id, label.String(),
			hasVariant ? 1 : 0, false);

		if (!hasVariant) {
			fSystemListView->AddItem(newItem);
			baseCodes.Add(layoutCode);
			baseItems.AddItem(newItem);
			continue;
		}

		int32 baseIndex = baseCodes.IndexOf(layoutCode);
		if (baseIndex >= 0) {
			fSystemListView->AddUnder(newItem,
				static_cast<BListItem*>(baseItems.ItemAt(baseIndex)));
		} else {
			// A variant whose base the catalog never listed: keep it
			// reachable at the top level rather than dropping it.
			fSystemListView->AddItem(newItem);
		}
	}

	// Second pass: give each base a "Generic" child for the plain layout.
	// (evdev lists bases before variants.)
	for (int32 i = 0; i < baseItems.CountItems(); i++) {
		KeymapListItem* base = static_cast<KeymapListItem*>(
			baseItems.ItemAt(i));
		if (fSystemListView->CountItemsUnder(base, true) == 0)
			continue;

		KeymapListItem* generic = new KeymapListItem(base->XkbId(),
			B_TRANSLATE("Generic"), 1, false);
		// Display text is "Generic" for every layout, but the saved keymap
		// name has to stay the base's label or every layout's Generic row
		// would save under the same useless name.
		generic->EntryRef().set_name(base->Text());
		fSystemListView->AddUnder(generic, base);
	}

	// FullListSortItems() sorts every level in place; plain SortItems() would
	// flatten the hierarchy.
	fSystemListView->FullListSortItems(&compare_key_list_items_outline);
}


void
KeymapWindow::_FillUserMaps()
{
	BListItem* item;
	while ((item = fUserListView->RemoveItem(static_cast<int32>(0))))
		delete item;

	entry_ref ref;
	_GetCurrentKeymap(ref);

	fUserListView->AddItem(new KeymapListItem(ref, B_TRANSLATE("(Current)")));

	fCurrentMapName = _GetActiveKeymapName();

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;

	path.Append("Keymap");

	BDirectory directory;
	if (directory.SetTo(path.Path()) == B_OK) {
		while (directory.GetNextRef(&ref) == B_OK) {
			fUserListView->AddItem(new KeymapListItem(ref));
		}
	}

	fUserListView->SortItems(&compare_key_list_items);
}


void
KeymapWindow::_SetListViewSize(BListView* listView)
{
	float minWidth = 0;
	for (int32 i = 0; i < listView->CountItems(); i++) {
		BStringItem* item = (BStringItem*)listView->ItemAt(i);
		float width = listView->StringWidth(item->Text());
		if (width > minWidth)
			minWidth = width;
	}

	const float scale = be_plain_font->Size() / 12.0f;
	listView->SetExplicitMinSize(BSize(minWidth + 8 * scale, 32 * scale));
}


status_t
KeymapWindow::_GetCurrentKeymap(entry_ref& ref)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return B_ERROR;

	path.Append("Key_map");

	return get_ref_for_path(path.Path(), &ref);
}


BString
KeymapWindow::_GetActiveKeymapName()
{
	BString mapName = kCurrentKeymapName;
		// safe default

	entry_ref ref;
	_GetCurrentKeymap(ref);

	BNode node(&ref);

	if (node.InitCheck() == B_OK)
		node.ReadAttrString("keymap:name", &mapName);

	return mapName;
}

//!	The xkb id of the active map, empty when the file predates it.
BString
KeymapWindow::_GetActiveLayoutId()
{
	BString layoutId;

	entry_ref ref;
	_GetCurrentKeymap(ref);

	BNode node(&ref);
	if (node.InitCheck() == B_OK)
		node.ReadAttrString("keymap:layout", &layoutId);

	return layoutId;
}


bool
KeymapWindow::_SelectCurrentSystemMap()
{
	if (fCurrentMapName.Length() <= 0)
		return false;

	// A Generic child carries its base row's name, so a name scan would
	// always land on the base, which comes first. Keep the row the user
	// actually picked whenever it is one of the matches.
	KeymapListItem* selected = static_cast<KeymapListItem*>(
		fSystemListView->ItemAt(fSystemListView->CurrentSelection()));
	if (selected != NULL && fCurrentMapName == selected->EntryRef().name)
		return true;

	// Prefer the xkb id. FirstBootPrompt labels its menu from the curated
	// kXkbLayoutTable while these rows carry evdev.lst descriptions, so a
	// map it activated has a name no row here uses and only the id
	// matches. Files written before the id was persisted fall through to
	// the name scan below.
	BString layoutId = _GetActiveLayoutId();
	if (layoutId.FindFirst("xkb:") == 0) {
		for (int32 i = 0; i < fSystemListView->FullListCountItems(); i++) {
			KeymapListItem* current = static_cast<KeymapListItem*>(
				fSystemListView->FullListItemAt(i));
			if (current == NULL || current->XkbId() != layoutId)
				continue;

			BListItem* super = fSystemListView->Superitem(current);
			if (super != NULL)
				fSystemListView->Expand(super);

			int32 displayIndex = fSystemListView->IndexOf(current);
			if (displayIndex >= 0
				&& displayIndex != fSystemListView->CurrentSelection()) {
				fSystemListView->Select(displayIndex);
				fSystemListView->ScrollToSelection();
			}

			return true;
		}
	}

	// FullList*: plain ItemAt() walks only expanded rows, so a collapsed
	// match would be invisible here.
	for (int32 i = 0; i < fSystemListView->FullListCountItems(); i++) {
		KeymapListItem* current = static_cast<KeymapListItem*>(
			fSystemListView->FullListItemAt(i));
		if (current == NULL || fCurrentMapName != current->EntryRef().name)
			continue;

		BListItem* super = fSystemListView->Superitem(current);
		if (super != NULL)
			fSystemListView->Expand(super);

		int32 displayIndex = fSystemListView->IndexOf(current);
		// Selecting an already-selected row reposts kMsgSystemMapSelected,
		// which would apply the same map again; skip it when redundant.
		if (displayIndex >= 0
			&& displayIndex != fSystemListView->CurrentSelection()) {
			fSystemListView->Select(displayIndex);
			fSystemListView->ScrollToSelection();
		}

		return true;
	}

	return false;
}

void
KeymapWindow::_SelectCurrentMap()
{
	if (_SelectCurrentSystemMap())
		return;

	if (fCurrentMapName.Length() > 0) {
		for (int32 i = 0; i < fUserListView->CountItems(); i++) {
			KeymapListItem* current = static_cast<KeymapListItem*>(
				fUserListView->ItemAt(i));
			if (current != NULL
				&& fCurrentMapName == current->EntryRef().name) {
				// Same repost hazard as the system list: only move the
				// selection when it would actually change.
				if (i != fUserListView->CurrentSelection()) {
					fUserListView->Select(i);
					fUserListView->ScrollToSelection();
				}
				return;
			}
		}
	}

	// The active map may have an unused name (e.g. "(Current)" after an
	// edit); don't fall back to the user list, or the selection is lost.
	if (fSystemListView->CurrentSelection() >= 0)
		return;

	// Select the "(Current)" entry if no name matches
	if (fUserListView->CurrentSelection() != 0)
		fUserListView->Select(0L);
}


status_t
KeymapWindow::_GetSettings(BFile& file, int mode) const
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path,
		(mode & O_ACCMODE) != O_RDONLY);
	if (status != B_OK)
		return status;

	path.Append("Keymap settings");

	return file.SetTo(path.Path(), mode);
}


status_t
KeymapWindow::_LoadSettings(BRect& windowFrame)
{
	BScreen screen(this);

	windowFrame.Set(-1, -1, 669, 357);
	// See if we can use a larger default size
	if (screen.Frame().Width() > 1200) {
		windowFrame.right = 899;
		windowFrame.bottom = 349;
	}
	float scaling = be_plain_font->Size() / 12.0f;
	windowFrame.right *= scaling;
	windowFrame.bottom *= scaling;

	BFile file;
	status_t status = _GetSettings(file, B_READ_ONLY);
	if (status == B_OK) {
		BMessage settings;
		status = settings.Unflatten(&file);
		if (status == B_OK) {
			BRect frame;
			status = settings.FindRect("window frame", &frame);
			if (status == B_OK)
				windowFrame = frame;

			const char* layoutPath;
			if (settings.FindString("keyboard layout", &layoutPath) == B_OK) {
				_SetKeyboardLayout(layoutPath);
				fUserPickedLayout
					= settings.GetBool("keyboard shape picked", false);
			}
		}
	}

	return status;
}


status_t
KeymapWindow::_SaveSettings()
{
	BFile file;
	status_t status
		= _GetSettings(file, B_WRITE_ONLY | B_ERASE_FILE | B_CREATE_FILE);
	if (status != B_OK)
		return status;

	BMessage settings('keym');
	settings.AddRect("window frame", Frame());

	// Restore a hand-picked shape only; otherwise the model drives it,
	// and re-deriving beats a stale path.
	if (fUserPickedLayout && fKeyboardLayoutPath.Length() > 0) {
		settings.AddString("keyboard layout", fKeyboardLayoutPath.String());
		settings.AddBool("keyboard shape picked", true);
	}

	return settings.Flatten(&file);
}
