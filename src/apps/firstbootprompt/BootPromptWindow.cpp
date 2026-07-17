/*
 * Copyright 2010, Stephan Aßmus <superstippi@gmx.de>
 * Copyright 2010-2021, Adrien Destugues, pulkomandy@pulkomandy.tk.
 * Copyright 2011, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2020-2021, Panagiotis "Ivory" Vasilopoulos <git@n0toose.net>
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 *
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "BootPromptWindow.h"

#include <new>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <Alert.h>
#include <Bitmap.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <Directory.h>
#include <Entry.h>
#include <Font.h>
#include <FindDirectory.h>
#include <File.h>
#include <FormattingConventions.h>
#include <IconUtils.h>
#include <IconView.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Locale.h>
#include <Menu.h>
#include <MutableLocaleRoster.h>
#include <ObjectList.h>
#include <Path.h>
#include <Roster.h>
#include <Screen.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <StringView.h>
#include <TextView.h>
#include <UnicodeChar.h>

#include "BootPrompt.h"
#include "Keymap.h"
#include "KeymapNames.h"


using BPrivate::MutableLocaleRoster;


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BootPromptWindow"


namespace BPrivate {
	void ForceUnloadCatalog();
};


static const char* kLanguageKeymapMappings[] = {
	// While there is a "Dutch" keymap, it apparently has not been widely
	// adopted, and the US-International keymap is common
	"Dutch", "US-International",

	// Cyrillic keymaps are not usable alone, as latin alphabet is required to
	// use Terminal. So we stay in US international until the user has a chance
	// to set up KeymapSwitcher.
	"Belarusian", "US-International",
	"Russian", "US-International",
	"Ukrainian", "US-International",

	// Turkish has two layouts, we must pick one
	"Turkish", "Turkish (Type-Q)",
};
static const size_t kLanguageKeymapMappingsSize
	= sizeof(kLanguageKeymapMappings) / sizeof(kLanguageKeymapMappings[0]);


class LanguageItem : public BStringItem {
public:
	LanguageItem(const char* label, const char* language)
		:
		BStringItem(label),
		fLanguage(language)
	{
	}

	~LanguageItem()
	{
	}

	const char* Language() const
	{
		return fLanguage.String();
	}

	void DrawItem(BView* owner, BRect frame, bool complete)
	{
		BStringItem::DrawItem(owner, frame, true/*complete*/);
	}

private:
			BString				fLanguage;
};


static int
compare_void_list_items(const void* _a, const void* _b)
{
	static BCollator collator;

	LanguageItem* a = *(LanguageItem**)_a;
	LanguageItem* b = *(LanguageItem**)_b;

	return collator.Compare(a->Text(), b->Text());
}


static int
compare_void_menu_items(const void* _a, const void* _b)
{
	static BCollator collator;

	BMenuItem* a = *(BMenuItem**)_a;
	BMenuItem* b = *(BMenuItem**)_b;

	return collator.Compare(a->Label(), b->Label());
}


// #pragma mark -


BootPromptWindow::BootPromptWindow()
	:
	BWindow(BRect(0, 0, 530, 400), "",
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_NOT_RESIZABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE,
		B_ALL_WORKSPACES),
	fDefaultKeymapItem(NULL)
{
	SetSizeLimits(450, 16384, 350, 16384);

	rgb_color textColor = ui_color(B_PANEL_TEXT_COLOR);
	fInfoTextView = new BTextView("");
	fInfoTextView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fInfoTextView->SetFontAndColor(be_plain_font, B_FONT_ALL, &textColor);
	fInfoTextView->MakeEditable(false);
	fInfoTextView->MakeSelectable(false);
	fInfoTextView->MakeResizable(false);

	BResources* res = BApplication::AppResources();
	size_t size = 0;
	const uint8_t* data;

	const BRect iconRect = BRect(BPoint(0, 0),
		be_control_look->ComposeIconSize(24));
	BBitmap desktopIcon(iconRect, B_RGBA32);
	data = (const uint8_t*)res->LoadResource('VICN', "Desktop", &size);
	BIconUtils::GetVectorIcon(data, size, &desktopIcon);

	BBitmap installerIcon(iconRect, B_RGBA32);
	data = (const uint8_t*)res->LoadResource('VICN', "Installer", &size);
	BIconUtils::GetVectorIcon(data, size, &installerIcon);

	fTryItButton = new BButton("desktop", "",
		new BMessage(MSG_BOOT_DESKTOP));
	fTryItButton->SetIcon(&desktopIcon);
	fTryItButton->MakeDefault(true);

	fInstallButton = new BButton("installer", "",
		new BMessage(MSG_RUN_INSTALLER));
	fInstallButton->SetIcon(&installerIcon);

	fEnableSshCheck = new BCheckBox("enableSsh",
		B_TRANSLATE("Enable debug SSH access"), NULL);
	if (!_DebugBuild())
		fEnableSshCheck->Hide();

	data = (const uint8_t*)res->LoadResource('VICN', "Language", &size);
	IconView* languageIcon = new IconView(B_LARGE_ICON);
	languageIcon->SetIcon(data, size, B_LARGE_ICON);

	data = (const uint8_t*)res->LoadResource('VICN', "Keymap", &size);
	IconView* keymapIcon = new IconView(B_LARGE_ICON);
	keymapIcon->SetIcon(data, size, B_LARGE_ICON);

	fLanguagesLabelView = new BStringView("languagesLabel", "");
	fLanguagesLabelView->SetFont(be_bold_font);
	fLanguagesLabelView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,
		B_SIZE_UNSET));

	fKeymapsMenuLabel = new BStringView("keymapsLabel", "");
	fKeymapsMenuLabel->SetFont(be_bold_font);
	fKeymapsMenuLabel->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,
		B_SIZE_UNSET));
	// Reserve room for verbose locales so width doesn't jump on language
	// changes.
	float labelWidth = fKeymapsMenuLabel->StringWidth("Disposition du clavier")
		+ 16;
	fKeymapsMenuLabel->SetExplicitMinSize(BSize(labelWidth, B_SIZE_UNSET));

	fLanguagesListView = new BListView();
	BScrollView* languagesScrollView = new BScrollView("languagesScroll",
		fLanguagesListView, B_WILL_DRAW, false, true);

	// Sized so the window fits within 640x480 at a 12pt font.
	float width = 640 * be_plain_font->Size() / 12 - (labelWidth + 64);
	float height = be_plain_font->Size() * 23;
	fInfoTextView->SetExplicitMinSize(BSize(width, height));
	fInfoTextView->SetExplicitMaxSize(BSize(width, B_SIZE_UNSET));

	fLanguagesListView->SetExplicitMinSize(
		BSize(fLanguagesListView->StringWidth("Português (Brasil)"),
		height));

	fKeymapsMenuField = new BMenuField("", "", new BMenu(""));
	fKeymapsMenuField->Menu()->SetLabelFromMarked(true);

	_InitCatalog(true);
	_PopulateLanguages();
	_PopulateKeymaps();

	BLayoutBuilder::Group<>(this, B_HORIZONTAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGroup(B_VERTICAL, 0)
			.SetInsets(0, 0, 0, B_USE_SMALL_SPACING)
			.AddGroup(B_HORIZONTAL)
				.Add(languageIcon)
				.Add(fLanguagesLabelView)
				.SetInsets(0, 0, 0, B_USE_SMALL_SPACING)
			.End()
			.Add(languagesScrollView)
			.AddGroup(B_HORIZONTAL)
				.Add(keymapIcon)
				.Add(fKeymapsMenuLabel)
				.SetInsets(0, B_USE_DEFAULT_SPACING, 0,
					B_USE_SMALL_SPACING)
			.End()
			.Add(fKeymapsMenuField)
		.End()
		.AddGroup(B_VERTICAL)
			.SetInsets(0)
			.Add(fInfoTextView)
			.Add(fEnableSshCheck)
			.AddGroup(B_HORIZONTAL)
				.SetInsets(0)
				.AddGlue()
				.Add(fInstallButton)
				.Add(fTryItButton)
			.End()
		.End();

	fLanguagesListView->MakeFocus();

	fInfoTextView->SetText("x\n\n\n\n\n\n\n\n\n\n\n\n\n\nx");
	ResizeToPreferred();

	_UpdateStrings();
	CenterOnScreen();
	Show();
}


void
BootPromptWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_LANGUAGE_SELECTED:
			if (LanguageItem* item = static_cast<LanguageItem*>(
					fLanguagesListView->ItemAt(
						fLanguagesListView->CurrentSelection(0)))) {
				BMessage preferredLanguages;
				preferredLanguages.AddString("language", item->Language());
				MutableLocaleRoster::Default()->SetPreferredLanguages(
					&preferredLanguages);
				_InitCatalog(true);
				_UpdateKeymapsMenu();

				BLanguage language(item->Language());
				BMenuItem* keymapItem = _KeymapItemForLanguage(language);
				if (keymapItem != NULL) {
					keymapItem->SetMarked(true);
					_ActivateKeymap(keymapItem->Message());
				}
			}
			// Also refreshes the visible strings; guarantees an item stays
			// selected even if the ListView tried to clear its selection.
			_UpdateStrings();
			break;

		case MSG_KEYMAP_SELECTED:
			_ActivateKeymap(message);
			break;

		case MSG_BOOT_DESKTOP:
		case MSG_RUN_INSTALLER:
			_ApplyLocaleToSession();
			if (message->what == MSG_BOOT_DESKTOP && _SshRequested())
				message->AddBool("enable_ssh", true);
			be_app->PostMessage(message);
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


void
BootPromptWindow::_ApplyLocaleToSession()
{
	// Installer picks these up from /run/vos/setup.conf as its defaults.
	BString language;
	if (LanguageItem* item = static_cast<LanguageItem*>(
			fLanguagesListView->ItemAt(
				fLanguagesListView->CurrentSelection(0)))) {
		language = item->Language();
	}

	BString keymap;
	if (BMenuItem* km = fKeymapsMenuField->Menu()->FindMarked())
		keymap = km->Label();

	FILE* f = fopen("/run/vos/setup.conf", "w");
	if (f != NULL) {
		if (language.Length() > 0)
			fprintf(f, "language=%s\n", language.String());
		if (keymap.Length() > 0)
			fprintf(f, "keymap=%s\n",   keymap.String());
		fclose(f);
	}
}


bool
BootPromptWindow::_DebugBuild() const
{
	return access("/etc/vos/debug", F_OK) == 0;
}


bool
BootPromptWindow::_SshRequested() const
{
	return _DebugBuild() && fEnableSshCheck != NULL
		&& fEnableSshCheck->Value() == B_CONTROL_ON;
}


bool
BootPromptWindow::QuitRequested()
{
	// Closing the window from the WM chrome exits FBP; the app's Quit path
	// decides whether to reboot (no Deskbar = we're the only thing on
	// screen, so a reboot is the safe action).
	BAlert* alert = new(std::nothrow) BAlert(
		B_TRANSLATE_SYSTEM_NAME("Quit"),
		B_TRANSLATE("Are you sure you want to close this window? This will "
			"restart your system!"),
		B_TRANSLATE("Cancel"), B_TRANSLATE("Restart system"), NULL,
		B_WIDTH_AS_USUAL, B_STOP_ALERT);

	if (alert != NULL) {
		alert->SetShortcut(0, B_ESCAPE);

		if (alert->Go() == 0)
			return false;
	}

	if (!be_roster->IsRunning(kDeskbarSignature))
		be_app->PostMessage(MSG_REBOOT_REQUESTED);

	return true;
}


void
BootPromptWindow::_InitCatalog(bool saveSettings)
{
	BPrivate::ForceUnloadCatalog();

	if (!saveSettings)
		return;

	BMessage settings;
	BString language;
	if (BLocaleRoster::Default()->GetCatalog()->GetLanguage(&language) == B_OK)
		settings.AddString("language", language.String());

	MutableLocaleRoster::Default()->SetPreferredLanguages(&settings);

	BFormattingConventions conventions(language.String());
	MutableLocaleRoster::Default()->SetDefaultFormattingConventions(
		conventions);
}


void
BootPromptWindow::_UpdateStrings()
{
	SetTitle(B_TRANSLATE("Welcome"));
	fInfoTextView->SetText(B_TRANSLATE_COMMENT(
			"Thank you for trying out Vitruvian! Please select your "
			"preferred language and keymap. Both settings can also be "
			"changed later.\n\n"
			"Do you wish to install the system now, or try it out first?",
			"FBP intro; both buttons follow this text."));
	fTryItButton->SetLabel(B_TRANSLATE("Try It Now"));
	fInstallButton->SetLabel(B_TRANSLATE("Install"));

	fLanguagesLabelView->SetText(B_TRANSLATE("Language"));
	fKeymapsMenuLabel->SetText(B_TRANSLATE("Keymap"));
	if (fKeymapsMenuField->Menu()->FindMarked() == NULL)
		fKeymapsMenuField->MenuItem()->SetLabel(B_TRANSLATE("Custom"));
}


void
BootPromptWindow::_PopulateLanguages()
{
	BMessage preferredLanguages;
	BLocaleRoster::Default()->GetPreferredLanguages(&preferredLanguages);
	const char* firstPreferredLanguage;
	if (preferredLanguages.FindString("language", &firstPreferredLanguage)
			!= B_OK) {
		firstPreferredLanguage = "en";
	}

	BMessage installedCatalogs;
	BLocaleRoster::Default()->GetAvailableCatalogs(&installedCatalogs,
		"x-vnd.Haiku-FirstBootPrompt");

	BFont font;
	fLanguagesListView->GetFont(&font);

	// The catalog list becomes the language list: this app only ships
	// languages it has been translated into.
	const char* languageID;
	LanguageItem* currentItem = NULL;
	for (int32 i = 0; installedCatalogs.FindString("language", i, &languageID)
			== B_OK; i++) {
		BLanguage* language;
		if (BLocaleRoster::Default()->GetLanguage(languageID, &language)
				== B_OK) {
			BString name;
			language->GetNativeName(name);

			// Drop any language whose native name can't be rendered by the
			// current font — fall back to the English name in that case.
			bool hasGlyphs[name.CountChars()];
			font.GetHasGlyphs(name.String(), name.CountChars(), hasGlyphs);
			for (int32 i = 0; i < name.CountChars(); ++i) {
				if (!hasGlyphs[i]) {
					language->GetName(name);
					break;
				}
			}

			LanguageItem* item = new LanguageItem(name.String(),
				languageID);
			fLanguagesListView->AddItem(item);
			if (strcmp(firstPreferredLanguage, languageID) == 0)
				currentItem = item;

			delete language;
		} else
			fprintf(stderr, "failed to get BLanguage for %s\n", languageID);
	}

	fLanguagesListView->SortItems(compare_void_list_items);
	if (currentItem != NULL)
		fLanguagesListView->Select(fLanguagesListView->IndexOf(currentItem));
	fLanguagesListView->ScrollToSelection();

	fLanguagesListView->SetSelectionMessage(
		new BMessage(MSG_LANGUAGE_SELECTED));
}


void
BootPromptWindow::_UpdateKeymapsMenu()
{
	BMenu *menu = fKeymapsMenuField->Menu();
	BMenuItem* item;
	BList itemsList;

	// BMenu can't sort itself; drain and re-add sorted.
	while ((item = menu->ItemAt(0)) != NULL) {
		BMessage* message = item->Message();
		entry_ref ref;
		message->FindRef("ref", &ref);
		item-> SetLabel(B_TRANSLATE_NOCOLLECT_ALL((ref.name),
		"KeymapNames", NULL));
		itemsList.AddItem(item);
		menu->RemoveItem((int32)0);
	}
	itemsList.SortItems(compare_void_menu_items);
	fKeymapsMenuField->Menu()->AddList(&itemsList, 0);
}


void
BootPromptWindow::_PopulateKeymaps()
{
	BString currentName;
	entry_ref currentRef;
	if (_GetCurrentKeymapRef(currentRef) == B_OK) {
		BNode node(&currentRef);
		node.ReadAttrString("keymap:name", &currentName);
	}

	BPath path;
	if (find_directory(B_SYSTEM_DATA_DIRECTORY, &path) != B_OK
		|| path.Append("Keymaps") != B_OK) {
		return;
	}

	BString usInternational("US-International");

	BDirectory directory;
	if (directory.SetTo(path.Path()) == B_OK) {
		entry_ref ref;
		BList itemsList;
		while (directory.GetNextRef(&ref) == B_OK) {
			BMessage* message = new BMessage(MSG_KEYMAP_SELECTED);
			message->AddRef("ref", &ref);
			BMenuItem* item =
				new BMenuItem(B_TRANSLATE_NOCOLLECT_ALL((ref.name),
				"KeymapNames", NULL), message);
			itemsList.AddItem(item);
			if (currentName == ref.name)
				item->SetMarked(true);

			if (usInternational == ref.name)
				fDefaultKeymapItem = item;
		}
		itemsList.SortItems(compare_void_menu_items);
		fKeymapsMenuField->Menu()->AddList(&itemsList, 0);
	}
}


void
BootPromptWindow::_ActivateKeymap(const BMessage* message) const
{
	entry_ref ref;
	if (message == NULL || message->FindRef("ref", &ref) != B_OK)
		return;

	Keymap keymap;
	if (keymap.Load(ref) != B_OK) {
		fprintf(stderr, "Failed to load new keymap file (%s).\n", ref.name);
		return;
	}

	entry_ref currentRef;
	if (_GetCurrentKeymapRef(currentRef) != B_OK) {
		fprintf(stderr, "Failed to get ref to user keymap file.\n");
		return;
	}

	if (keymap.Save(currentRef) != B_OK) {
		fprintf(stderr, "Failed to save new keymap file (%s).\n", ref.name);
		return;
	}

	keymap.Use();
}


status_t
BootPromptWindow::_GetCurrentKeymapRef(entry_ref& ref) const
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK
		|| path.Append("Key_map") != B_OK) {
		return B_ERROR;
	}

	return get_ref_for_path(path.Path(), &ref);
}


BMenuItem*
BootPromptWindow::_KeymapItemForLanguage(BLanguage& language) const
{
	BLanguage english("en");
	BString name;
	if (language.GetName(name, &english) != B_OK)
		return fDefaultKeymapItem;

	for (size_t i = 0; i < kLanguageKeymapMappingsSize; i += 2) {
		if (!strcmp(name, kLanguageKeymapMappings[i])) {
			name = kLanguageKeymapMappings[i + 1];
			break;
		}
	}

	BMenu* menu = fKeymapsMenuField->Menu();
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		BMessage* message = item->Message();

		entry_ref ref;
		if (message->FindRef("ref", &ref) == B_OK
			&& name == ref.name)
			return item;
	}

	return fDefaultKeymapItem;
}
