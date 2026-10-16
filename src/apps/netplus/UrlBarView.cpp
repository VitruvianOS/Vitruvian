/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */


#include "HistoryStore.h"
#include "NetPlusMessages.h"
#include "SuggestPopup.h"
#include "ToolbarIcons.h"
#include "UrlBarView.h"
#include "UrlInput.h"

#include <Bitmap.h>
#include <ControlLook.h>
#include <IconUtils.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <PopUpMenu.h>
#include <Size.h>
#include <String.h>
#include <TextView.h>
#include <Window.h>

#include <math.h>
#include <vector>


static const float kIconInset = 20.0f;


static BBitmap*
_LoadIcon(const uint8* hvif, size_t size)
{
	BSize iconSize = be_control_look->ComposeIconSize(16);
	BBitmap* bitmap = new BBitmap(BRect(BPoint(0, 0), iconSize),
		B_BITMAP_NO_SERVER_LINK, B_RGBA32);
	if (bitmap == NULL)
		return NULL;
	if (bitmap->InitCheck() != B_OK
		|| BIconUtils::GetVectorIcon(hvif, size, bitmap) != B_OK) {
		delete bitmap;
		return NULL;
	}
	return bitmap;
}


/*!	Single-line field: swallow Enter and the arrows the popup
	consumes, report every text mutation to the owner so the
	suggestion list tracks the typing.
*/
class UrlTextView : public BTextView {
public:
								UrlTextView(BRect frame, UrlBarView* owner);

	virtual	void				KeyDown(const char* bytes, int32 numBytes);

protected:
	virtual	void				InsertText(const char* text, int32 length,
									int32 offset, const text_run_array* runs);
	virtual	void				DeleteText(int32 fromOffset, int32 toOffset);

private:
			UrlBarView*			fOwner;
};


UrlTextView::UrlTextView(BRect frame, UrlBarView* owner)
	:
	BTextView(frame, "url text", B_FOLLOW_ALL, B_WILL_DRAW | B_NAVIGABLE),
	fOwner(owner)
{
	SetWordWrap(false);
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
}


void
UrlTextView::KeyDown(const char* bytes, int32 numBytes)
{
	if (numBytes == 1) {
		switch (bytes[0]) {
			case B_UP_ARROW:
				fOwner->_MoveSelection(-1);
				return;

			case B_DOWN_ARROW:
				fOwner->_MoveSelection(1);
				return;

			case B_ENTER:
				if (fOwner->fSelection >= 0)
					fOwner->_AcceptSuggestion(fOwner->fSelection);
				else
					fOwner->_Submit();
				return;

			case B_ESCAPE:
				fOwner->_HideSuggestions();
				return;
		}
	}
	BTextView::KeyDown(bytes, numBytes);
}


void
UrlTextView::InsertText(const char* text, int32 length, int32 offset,
	const text_run_array* runs)
{
	BTextView::InsertText(text, length, offset, runs);
	fOwner->_TextChanged();
}


void
UrlTextView::DeleteText(int32 fromOffset, int32 toOffset)
{
	BTextView::DeleteText(fromOffset, toOffset);
	fOwner->_TextChanged();
}


//#pragma mark -


UrlBarView::UrlBarView(BRect frame)
	:
	BView(frame, "url bar", B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP,
		B_WILL_DRAW),
	fTextView(NULL),
	fPopup(NULL),
	fHistory(NULL),
	fSecureIcon(NULL),
	fInsecureIcon(NULL),
	fSelection(-1),
	fSearchEngine(0),
	fSecurity(0),
	fBlockNotify(false)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	fSecureIcon = _LoadIcon(kNetPlusSecureIcon, kNetPlusSecureIconSize);
	fInsecureIcon = _LoadIcon(kNetPlusInsecureIcon,
		kNetPlusInsecureIconSize);

	fTextView = new UrlTextView(BRect(0, 0, 0, 0), this);

	// The left inset reserves the strip the security icon draws in.
	BLayoutBuilder::Group<>(this, B_HORIZONTAL, 0)
		.SetInsets(kIconInset, 1, 2, 1)
		.Add(fTextView, 1.0f);
}


UrlBarView::~UrlBarView()
{
	if (fPopup != NULL) {
		fPopup->Quit();
		fPopup = NULL;
	}

	delete fSecureIcon;
	delete fInsecureIcon;
}


void
UrlBarView::DetachedFromWindow()
{
	if (fPopup != NULL) {
		fPopup->Quit();
		fPopup = NULL;
	}
	BView::DetachedFromWindow();
}


void
UrlBarView::MouseDown(BPoint where)
{
	if (_IconRect().Contains(where)) {
		_ShowInfo();
		return;
	}
	BView::MouseDown(where);
}


void
UrlBarView::MakeFocus(bool focused)
{
	BView::MakeFocus(focused);
	if (!focused)
		_HideSuggestions();
}


void
UrlBarView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_SUGGEST_ACCEPTED:
		{
			const char* url = NULL;
			if (message->FindString("url", &url) == B_OK && url != NULL) {
				SetText(url);
				BMessage open(MSG_OPEN_URL);
				open.AddString("url", url);
				Window()->PostMessage(&open);
			}
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
UrlBarView::SetHistorySource(HistoryStore* store)
{
	fHistory = store;
}


void
UrlBarView::SetSearchEngine(int32_t engine)
{
	fSearchEngine = engine;
}


const char*
UrlBarView::Text() const
{
	return fTextView->Text();
}


void
UrlBarView::SetText(const char* text)
{
	fBlockNotify = true;
	fTextView->SetText(text == NULL ? "" : text);
	fBlockNotify = false;
	fSelection = -1;
	_HideSuggestions();
}


void
UrlBarView::SelectAll()
{
	fTextView->Select(0, fTextView->TextLength());
}


void
UrlBarView::SetSecurityState(uint8 state)
{
	if (fSecurity == state)
		return;
	fSecurity = state;
	Invalidate(_IconRect());
}


void
UrlBarView::_TextChanged()
{
	fSelection = -1;
	if (fBlockNotify)
		return;

	std::vector<std::string> history;
	if (fHistory != NULL)
		history = fHistory->Entries();

	if (fModel.Update(fTextView->Text(), history) == 0) {
		_HideSuggestions();
		return;
	}

	if (fPopup == NULL)
		fPopup = new SuggestPopup(BMessenger(this));

	BPoint anchor = Bounds().LeftBottom() + BPoint(0, 2);
	ConvertToScreen(&anchor);
	fPopup->Go(anchor);

	std::vector<std::string> urls;
	for (int32 i = 0; i < fModel.Count(); i++)
		urls.push_back(fModel.MatchAt(i).text);
	fPopup->Update(urls, fSelection);
}


void
UrlBarView::_MoveSelection(int32_t delta)
{
	int32 count = fModel.Count();
	if (count == 0 || fPopup == NULL || fPopup->IsHidden())
		return;

	int32 next = fSelection + delta;
	if (next < -1)
		next = count - 1;
	if (next >= count)
		next = -1;
	fSelection = next;

	std::vector<std::string> urls;
	for (int32 i = 0; i < count; i++)
		urls.push_back(fModel.MatchAt(i).text);
	fPopup->Update(urls, fSelection);
}


void
UrlBarView::_AcceptSuggestion(int32_t index)
{
	const SuggestMatch& match = fModel.MatchAt(index);
	if (match.text.empty())
		return;

	SetText(match.text.c_str());
	BMessage open(MSG_OPEN_URL);
	open.AddString("url", match.text.c_str());
	Window()->PostMessage(&open);
}


void
UrlBarView::_Submit()
{
	_HideSuggestions();

	if (fSelection >= 0) {
		_AcceptSuggestion(fSelection);
		return;
	}

	std::string url;
	UrlInputKind kind = ClassifyUrlInput(fTextView->Text(), &url);
	if (kind == kUrlInputNone)
		return;
	if (kind == kUrlInputSearch)
		url = BuildSearchURL(fTextView->Text(), fSearchEngine);

	SetText(url.c_str());
	BMessage open(MSG_OPEN_URL);
	open.AddString("url", url.c_str());
	Window()->PostMessage(&open);
}


void
UrlBarView::_HideSuggestions()
{
	if (fPopup != NULL)
		fPopup->Hide();
}


void
UrlBarView::_ShowInfo()
{
	BPopUpMenu menu("connection info");

	const char* line = "Internal or local page.";
	if (fSecurity == 2)
		line = "Connection is encrypted (HTTPS).";
	else if (fSecurity == 1)
		line = "Connection is not encrypted (HTTP).";

	BMenuItem* status = new BMenuItem(line, NULL);
	status->SetEnabled(false);
	menu.AddItem(status);

	BMenuItem* cert = new BMenuItem(
		"Certificate details are not exposed by the web bridge yet.",
		NULL);
	cert->SetEnabled(false);
	menu.AddItem(cert);

	BPoint where = _IconRect().LeftBottom() + BPoint(0, 2);
	ConvertToScreen(&where);
	menu.Go(where);
}


BRect
UrlBarView::_IconRect() const
{
	BRect bounds = Bounds();
	float top = floorf((bounds.Height() - 16) / 2);
	if (top < 0)
		top = 0;
	return BRect(bounds.left + 3, bounds.top + top,
		bounds.left + 3 + 15, bounds.top + top + 15);
}
