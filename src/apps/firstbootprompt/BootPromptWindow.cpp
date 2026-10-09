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
#include <LocaleRoster.h>
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


// Read by the Installer too; keep the two in step.
static const char* kFirstBootHandoffPath = "/run/vos/firstboot.conf";


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
		fLanguage(language),
		fIcon(NULL)
	{
	}

	~LanguageItem()
	{
		delete fIcon;
	}

	const char* Language() const
	{
		return fLanguage.String();
	}

	void Update(BView* owner, const BFont* font)
	{
		BStringItem::Update(owner, font);

		// Reserve the icon column whether or not a flag is found, so the
		// labels line up with the rows that have one.
		float iconSize = Height();
		SetWidth(Width() + iconSize
			+ be_control_look->DefaultLabelSpacing());

		// Drop previous bitmap (Update() runs again on font/attach change, preventing leaks)
		delete fIcon;
		fIcon = new(std::nothrow) BBitmap(
			BRect(0, 0, iconSize - 1, iconSize - 1), B_RGBA32);
		if (fIcon != NULL
			&& BLocaleRoster::Default()->GetFlagIconForLanguage(fIcon,
				fLanguage.String()) != B_OK) {
			delete fIcon;
			fIcon = NULL;
		}
	}

	void DrawItem(BView* owner, BRect frame, bool complete)
	{
		if (Text() == NULL) {
			BStringItem::DrawItem(owner, frame, true/*complete*/);
			return;
		}

		float iconSize = fIcon != NULL && fIcon->IsValid()
			? fIcon->Bounds().Width() : Height();
		float spacing = be_control_look->DefaultLabelSpacing();
		rgb_color lowColor = owner->LowColor();

		owner->SetLowColor(IsSelected()
			? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
			: owner->ViewColor());
		owner->FillRect(frame, B_SOLID_LOW);

		BRect iconFrame(frame.left + spacing, frame.top,
			frame.left + spacing + iconSize - 1, frame.top + iconSize - 1);

		if (fIcon != NULL && fIcon->IsValid()) {
			owner->SetDrawingMode(B_OP_OVER);
			owner->DrawBitmap(fIcon, iconFrame);
			owner->SetDrawingMode(B_OP_COPY);
		} else {
			// No single country to show; draw muted outline as flag slot
			rgb_color border = ui_color(B_CONTROL_BORDER_COLOR);
			border.alpha = 90;
			rgb_color high = owner->HighColor();
			owner->SetDrawingMode(B_OP_ALPHA);
			owner->SetHighColor(border);
			owner->StrokeRoundRect(iconFrame.InsetByCopy(0, iconSize / 6), 2, 2);
			owner->SetHighColor(high);
			owner->SetDrawingMode(B_OP_COPY);
		}

		owner->MovePenTo(frame.left + spacing * 2 + iconSize,
			frame.top + BaselineOffset());
		owner->DrawString(Text());

		owner->SetLowColor(lowColor);
	}

private:
			BString				fLanguage;
			BBitmap*			fIcon;
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
			be_app->PostMessage(message);
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


void
BootPromptWindow::_ApplyLocaleToSession()
{
	// Not /run/vos/setup.conf: vos-install-helper truncates that file. The
	// keymap is already in the live session settings.
	BString language;
	if (LanguageItem* item = static_cast<LanguageItem*>(
			fLanguagesListView->ItemAt(
				fLanguagesListView->CurrentSelection(0)))) {
		language = item->Language();
	}

	if (language.IsEmpty())
		return;

	FILE* f = fopen(kFirstBootHandoffPath, "w");
	if (f == NULL)
		return;
	fprintf(f, "language=%s\n", language.String());
	fclose(f);
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

	// English has no catalog: take the selected language, or the ID stays
	// empty and the conventions fall back to the ICU root locale.
	BMessage settings;
	BString language;
	if (BLocaleRoster::Default()->GetCatalog()->GetLanguage(&language) != B_OK
		|| language.IsEmpty()) {
		BMessage preferred;
		BLocaleRoster::Default()->GetPreferredLanguages(&preferred);
		if (preferred.FindString("language", &language) != B_OK)
			language = "en";
	}
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

	// English is the source language, so it ships no catalog of its own and
	// the scan above never finds it. Offer it anyway; it is always built in.
	bool haveEnglish = false;
	const char* catalogID;
	for (int32 i = 0; installedCatalogs.FindString("language", i, &catalogID)
			== B_OK; i++) {
		if (strcmp(catalogID, "en") == 0) {
			haveEnglish = true;
			break;
		}
	}
	if (!haveEnglish)
		installedCatalogs.AddString("language", "en");

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
		BString name;
		message->FindString("name", &name);
		item->SetLabel(B_TRANSLATE_NOCOLLECT_ALL(name.String(),
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

	BString usInternational("US-International");

	BList itemsList;
	int32 count = xkb_layout_table_count();
	for (int32 i = 0; i < count; i++) {
		const char* name = xkb_layout_table_name_at(i);
		if (name == NULL)
			continue;

		BMessage* message = new BMessage(MSG_KEYMAP_SELECTED);
		message->AddString("name", name);
		BMenuItem* item =
			new BMenuItem(B_TRANSLATE_NOCOLLECT_ALL(name, "KeymapNames",
				NULL), message);
		itemsList.AddItem(item);
		if (currentName == name)
			item->SetMarked(true);

		if (usInternational == name)
			fDefaultKeymapItem = item;
	}
	itemsList.SortItems(compare_void_menu_items);
	fKeymapsMenuField->Menu()->AddList(&itemsList, 0);
}


void
BootPromptWindow::_ActivateKeymap(const BMessage* message) const
{
	BString name;
	if (message == NULL || message->FindString("name", &name) != B_OK)
		return;

	const char* layout;
	const char* variant;
	look_up_xkb_layout(name.String(), layout, variant);

	// Preserve the active Ctrl/Command mapping instead of reverting.
	BKeymap active;
	active.SetToCurrent();
	const char* options = look_up_xkb_modifier_options(active.Map());

	Keymap keymap;
	if (keymap.PopulateFromXkbNames("evdev", keymap.Model(), layout, variant,
			options) != B_OK) {
		fprintf(stderr, "Failed to derive keymap \"%s\".\n", name.String());
		return;
	}
	keymap.SetName(name.String());

	// A canonical "xkb:layout[:variant]" id, not the label above. Our menu
	// is built from the curated kXkbLayoutTable while the Keymap preflet
	// lists evdev.lst descriptions, and the two name spaces do not meet
	// ("US-International" appears nowhere in evdev.lst), so only the id
	// can identify this layout to the preflet afterwards.
	BString layoutId("xkb:");
	layoutId << layout;
	if (variant != NULL && variant[0] != '\0')
		layoutId << ':' << variant;

	keymap.SetLayoutName(layoutId.String());

	entry_ref currentRef;
	if (_GetCurrentKeymapRef(currentRef) != B_OK) {
		fprintf(stderr, "Failed to get ref to user keymap file.\n");
		return;
	}

	if (keymap.Save(currentRef) != B_OK) {
		fprintf(stderr, "Failed to save new keymap file (%s).\n",
			currentRef.name);
		return;
	}

	// Machine-wide (allowed to fail quietly; /etc/default is root-owned) and per-user
	// (janus ferries to real user at login). Written before activation triggers recompile.
	keymap.WriteSystemXkbLayout();
	keymap.ApplyXkbLayout();
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

		// The menu carries layout names now, not refs.
		BString itemName;
		if (message->FindString("name", &itemName) == B_OK
			&& name == itemName)
			return item;
	}

	return fDefaultKeymapItem;
}
