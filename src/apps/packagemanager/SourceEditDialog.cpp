/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SourceEditDialog.h"

#include <new>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Directory.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <ObjectList.h>
#include <Path.h>
#include <Size.h>
#include <TextControl.h>

#include "PackageManagerDefs.h"
#include "TruncatingStringView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SourceEditDialog"


// Local to the dialog: Save validates and posts kMsgSourceAdd, the
// menu items only carry their pick as a message field.
enum {
	kMsgDialogSave		= 'rsav',
	kMsgDialogPick		= 'rspk'
};


static bool
is_space(char c)
{
	return c == ' ' || c == '\t';
}


static int
compare_paths(const BString* a, const BString* b)
{
	return a->Compare(*b);
}


static bool
valid_slug(const BString& slug)
{
	if (slug.Length() == 0 || slug.Length() > 64)
		return false;

	const char first = slug.ByteAt(0);
	if (!((first >= 'a' && first <= 'z') || (first >= '0' && first <= '9')))
		return false;

	for (int32 i = 0; i < slug.Length(); i++) {
		const char c = slug.ByteAt(i);
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
				|| c == '-')) {
			return false;
		}
	}
	return true;
}


// Single-space separated list, no leading, trailing or doubled space.
// Mirrors the helper's suite and component rules.
static bool
valid_word_list(const BString& text, bool allowEmpty)
{
	if (text.IsEmpty())
		return allowEmpty;
	if (text.ByteAt(0) == ' ' || text.ByteAt(text.Length() - 1) == ' ')
		return false;
	if (text.FindFirst("  ") >= 0)
		return false;

	for (int32 i = 0; i < text.Length(); i++) {
		const char c = text.ByteAt(i);
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '.' || c == '_'
			|| c == '+' || c == '/' || c == '-' || c == ' ';
		if (!ok)
			return false;
	}
	return true;
}


static bool
valid_uri(const BString& uri)
{
	int32 schemeLength;
	if (uri.StartsWith("https://"))
		schemeLength = 8;
	else if (uri.StartsWith("http://"))
		schemeLength = 7;
	else
		return false;

	if (uri.Length() <= schemeLength)
		return false;

	BString rest;
	uri.CopyInto(rest, schemeLength, uri.Length() - schemeLength);
	if (rest.IsEmpty() || rest.ByteAt(0) == '/' || rest.StartsWith("//"))
		return false;

	for (int32 i = 0; i < rest.Length(); i++) {
		const char c = rest.ByteAt(i);
		if (c == '#' || c == '"' || c == '\'' || c == '\\')
			return false;
	}
	return true;
}


SourceEditDialog::SourceEditDialog(BWindow* parent, const BMessenger& target,
	bool edit, const BMessage* entry)
	:
	Inherited(BRect(0, 0, 460, 340),
		edit ? B_TRANSLATE("Edit source") : B_TRANSLATE("Add source"),
		B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fTarget(target),
	fEdit(edit),
	fSlugControl(NULL),
	fUrisControl(NULL),
	fSuitesControl(NULL),
	fComponentsControl(NULL),
	fTypesMenu(NULL),
	fSignedByMenu(NULL)
{
	if (parent != NULL) {
		SetFeel(B_MODAL_SUBSET_WINDOW_FEEL);
		AddToSubset(parent);
	}

	_BuildLayout(entry);

	if (parent != NULL)
		CenterIn(parent->Frame());
	else
		CenterOnScreen();
}


SourceEditDialog::~SourceEditDialog()
{
}


void
SourceEditDialog::_BuildLayout(const BMessage* entry)
{
	fSlugControl = new BTextControl("slug", B_TRANSLATE("Name:"), "", NULL);
	fSlugControl->SetEnabled(!fEdit);

	TruncatingStringView* hint = new TruncatingStringView("hint",
		B_TRANSLATE("Saved as vos-user-<name>.sources under "
			"/etc/apt/sources.list.d. The name may use lowercase "
			"letters, digits and '-'."));
	hint->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	hint->SetExplicitMinSize(BSize(0, B_SIZE_UNSET));

	fTypesMenu = new BMenu(B_TRANSLATE("Binary (deb)"));
	fTypesMenu->SetRadioMode(true);
	fTypesMenu->SetLabelFromMarked(true);
	BMessage* binaryMessage = new BMessage(kMsgDialogPick);
	binaryMessage->AddString("types", "deb");
	fTypesMenu->AddItem(new BMenuItem(B_TRANSLATE("Binary (deb)"),
		binaryMessage));
	BMessage* sourceMessage = new BMessage(kMsgDialogPick);
	sourceMessage->AddString("types", "deb-src");
	fTypesMenu->AddItem(new BMenuItem(B_TRANSLATE("Source (deb-src)"),
		sourceMessage));
	BMessage* bothMessage = new BMessage(kMsgDialogPick);
	bothMessage->AddString("types", "deb deb-src");
	fTypesMenu->AddItem(new BMenuItem(B_TRANSLATE("Binary and source"),
		bothMessage));
	fTypesMenu->ItemAt(0)->SetMarked(true);
	BMenuField* typesField = new BMenuField("types", B_TRANSLATE("Types:"),
		fTypesMenu);

	fUrisControl = new BTextControl("uris", B_TRANSLATE("URIs:"), "", NULL);
	fSuitesControl = new BTextControl("suites", B_TRANSLATE("Suites:"),
		"", NULL);
	fComponentsControl = new BTextControl("components",
		B_TRANSLATE("Components:"), "", NULL);

	fSignedByMenu = new BMenu(B_TRANSLATE("None"));
	fSignedByMenu->SetRadioMode(true);
	fSignedByMenu->SetLabelFromMarked(true);
	BMessage* noneMessage = new BMessage(kMsgDialogPick);
	noneMessage->AddString("path", "");
	fSignedByMenu->AddItem(new BMenuItem(B_TRANSLATE("None"), noneMessage));
	fSignedByMenu->ItemAt(0)->SetMarked(true);

	BObjectList<BString, true> names(8);
	BDirectory dir("/etc/apt/keyrings");
	if (dir.InitCheck() == B_OK) {
		BEntry entry;
		while (dir.GetNextEntry(&entry) == B_OK) {
			struct stat st;
			if (entry.GetStat(&st) != 0 || !S_ISREG(st.st_mode))
				continue;

			BPath path;
			if (entry.GetPath(&path) == B_OK)
				names.AddItem(new BString(path.Path()));
		}
	}
	names.SortItems(compare_paths);
	for (int32 i = 0; i < names.CountItems(); i++) {
		const BString& path = *names.ItemAt(i);
		BString label(path);
		const int32 slash = label.FindLast('/');
		if (slash >= 0)
			label.Remove(0, slash + 1);

		BMessage* message = new BMessage(kMsgDialogPick);
		message->AddString("path", path.String());
		fSignedByMenu->AddItem(new BMenuItem(label.String(), message));
	}
	BMenuField* signedByField = new BMenuField("signed_by",
		B_TRANSLATE("Signed-By:"), fSignedByMenu);

	BButton* cancelButton = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(B_QUIT_REQUESTED));
	BButton* saveButton = new BButton("save", B_TRANSLATE("Save"),
		new BMessage(kMsgDialogSave));
	saveButton->MakeDefault(true);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fSlugControl)
		.Add(hint)
		.AddStrut(B_USE_HALF_ITEM_SPACING)
		.Add(typesField)
		.Add(fUrisControl)
		.Add(fSuitesControl)
		.Add(fComponentsControl)
		.Add(signedByField)
		.AddGlue()
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGlue()
			.Add(cancelButton)
			.Add(saveButton)
		.End()
	.End();

	if (entry != NULL) {
		const char* text = "";
		if (entry->FindString("slug", &text) == B_OK) {
			fSlugControl->SetText(text);
			fSlugControl->SetEnabled(false);
		}
		if (entry->FindString("types", &text) == B_OK)
			_MarkTypes(text);
		if (entry->FindString("uris", &text) == B_OK)
			fUrisControl->SetText(text);
		if (entry->FindString("suites", &text) == B_OK)
			fSuitesControl->SetText(text);
		if (entry->FindString("components", &text) == B_OK)
			fComponentsControl->SetText(text);
		if (entry->FindString("signed_by", &text) == B_OK)
			_MarkSignedBy(text);
	}
}


void
SourceEditDialog::_MarkTypes(const char* types)
{
	if (types == NULL)
		return;

	for (int32 i = 0; i < fTypesMenu->CountItems(); i++) {
		BMenuItem* item = fTypesMenu->ItemAt(i);
		const char* value = "";
		if (item->Message() != NULL
				&& item->Message()->FindString("types", &value) == B_OK
				&& strcasecmp(value, types) == 0) {
			item->SetMarked(true);
			return;
		}
	}
}


void
SourceEditDialog::_MarkSignedBy(const char* path)
{
	if (path == NULL || path[0] == '\0')
		return;

	for (int32 i = 0; i < fSignedByMenu->CountItems(); i++) {
		BMenuItem* item = fSignedByMenu->ItemAt(i);
		const char* value = "";
		if (item->Message() != NULL
				&& item->Message()->FindString("path", &value) == B_OK
				&& strcmp(value, path) == 0) {
			item->SetMarked(true);
			return;
		}
	}

	// The keyring vanished since the source was written; keep the path
	// visible instead of silently dropping it on save.
	BString label(path);
	const int32 slash = label.FindLast('/');
	if (slash >= 0)
		label.Remove(0, slash + 1);
	label << " (missing)";

	BMessage* message = new BMessage(kMsgDialogPick);
	message->AddString("path", path);
	BMenuItem* item = new BMenuItem(label.String(), message);
	fSignedByMenu->AddItem(item);
	item->SetMarked(true);
}


bool
SourceEditDialog::_Validate(BString* error)
{
	BString slug(fSlugControl->Text());
	slug.Trim();
	if (!fEdit && !valid_slug(slug)) {
		*error = B_TRANSLATE("The name must be 1 to 64 characters, start "
			"with a lowercase letter or digit, and may only contain "
			"lowercase letters, digits and '-'.");
		return false;
	}

	BString uris(fUrisControl->Text());
	uris.Trim();
	if (uris.IsEmpty()) {
		*error = B_TRANSLATE("Enter at least one URI.");
		return false;
	}
	int32 at = 0;
	while (at < uris.Length()) {
		while (at < uris.Length() && is_space(uris.ByteAt(at)))
			at++;
		int32 end = at;
		while (end < uris.Length() && !is_space(uris.ByteAt(end)))
			end++;
		if (end > at) {
			BString uri;
			uris.CopyInto(uri, at, end - at);
			if (!valid_uri(uri)) {
				BString text(B_TRANSLATE(
					"Not a valid URI (http and https only): %uri%"));
				text.ReplaceFirst("%uri%", uri);
				*error = text;
				return false;
			}
		}
		at = end;
	}

	BString suites(fSuitesControl->Text());
	suites.Trim();
	if (!valid_word_list(suites, false)) {
		*error = B_TRANSLATE("Suites must be a space-separated list of "
			"suite names.");
		return false;
	}

	BString components(fComponentsControl->Text());
	components.Trim();
	if (!valid_word_list(components, true)) {
		*error = B_TRANSLATE("Components must be a space-separated list "
			"of component names.");
		return false;
	}

	BMenuItem* marked = fSignedByMenu->FindMarked();
	if (marked != NULL && marked->Message() != NULL) {
		const char* path = "";
		marked->Message()->FindString("path", &path);
		BString signedBy(path);
		if (signedBy.Length() > 0
				&& !signedBy.StartsWith("/etc/apt/keyrings/")) {
			*error = B_TRANSLATE("The signing key must live under "
				"/etc/apt/keyrings/.");
			return false;
		}
	}

	return true;
}


void
SourceEditDialog::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgDialogSave:
		{
			BString error;
			if (!_Validate(&error)) {
				BAlert* alert = new BAlert(B_TRANSLATE("Repository"),
					error.String(), B_TRANSLATE("OK"), NULL, NULL,
					B_WIDTH_AS_USUAL, B_STOP_ALERT);
				alert->Go();
				break;
			}

			BString slug(fSlugControl->Text());
			slug.Trim();
			BString uris(fUrisControl->Text());
			uris.Trim();
			BString suites(fSuitesControl->Text());
			suites.Trim();
			BString components(fComponentsControl->Text());
			components.Trim();

			const char* types = "deb";
			BMenuItem* marked = fTypesMenu->FindMarked();
			if (marked != NULL && marked->Message() != NULL)
				marked->Message()->FindString("types", &types);

			BString signedBy;
			marked = fSignedByMenu->FindMarked();
			if (marked != NULL && marked->Message() != NULL) {
				const char* path = "";
				marked->Message()->FindString("path", &path);
				signedBy = path;
			}

			BMessage request(kMsgSourceAdd);
			request.AddString("slug", slug);
			request.AddString("types", types);
			request.AddString("uris", uris);
			request.AddString("suites", suites);
			request.AddString("components", components);
			request.AddString("signed_by", signedBy);
			fTarget.SendMessage(&request);
			PostMessage(B_QUIT_REQUESTED);
			break;
		}

		default:
			Inherited::MessageReceived(message);
			break;
	}
}
