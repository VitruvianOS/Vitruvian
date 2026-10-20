/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */


#include "DownloadsPanel.h"
#include "NetPlusMessages.h"

#include <Button.h>
#include <Font.h>
#include <GroupView.h>
#include <GroupLayout.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <Messenger.h>
#include <Rect.h>
#include <Screen.h>
#include <Size.h>
#include <String.h>
#include <StringView.h>
#include <View.h>
#include <Window.h>


static BString
_FormatBytes(uint64 bytes)
{
	BString text;
	if (bytes >= 1073741824ULL)
		text << (double)bytes / 1073741824.0 << " GB";
	else if (bytes >= 1048576ULL)
		text << (double)bytes / 1048576.0 << " MB";
	else if (bytes >= 1024ULL)
		text << (double)bytes / 1024.0 << " KB";
	else
		text << bytes << " B";
	return text;
}


static BString
_StatusText(const DownloadRecord& item)
{
	switch (item.state) {
		case kDownloadStateFinished:
			return BString("Done");

		case kDownloadStateFailed:
			return BString("Failed");

		case kDownloadStateCancelled:
			return BString("Cancelled");

		default:
			break;
	}

	BString text;
	if (item.totalBytes > 0) {
		int percent = (int)((item.receivedBytes * 100) / item.totalBytes);
		text << percent << "% of " << _FormatBytes(item.totalBytes);
	} else {
		text = _FormatBytes(item.receivedBytes);
		text << " downloaded";
	}
	return text;
}


static BButton*
_ActionButton(const char* label, uint32 what, int64 id)
{
	BButton* button = new BButton(label, new BMessage(what));
	button->Message()->AddInt64("id", id);
	return button;
}


class DownloadsPanelView : public BView {
public:
								DownloadsPanelView(BRect frame,
									DownloadsPanel* panel);

	virtual	void				AttachedToWindow();
	virtual	BSize				PreferredSize();

			void				SetContent(const DownloadsModel& model);

private:
			BGroupView*			_BuildRow(const DownloadRecord& item);

			DownloadsPanel*		fPanel;
			DownloadsModel		fModel;
			BGroupView*			fRows;
			BStringView*		fEmpty;
};


DownloadsPanelView::DownloadsPanelView(BRect frame, DownloadsPanel* panel)
	:
	BView(frame, "downloads view", B_FOLLOW_ALL, B_WILL_DRAW),
	fPanel(panel),
	fRows(NULL),
	fEmpty(NULL)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fRows = new BGroupView("rows", B_VERTICAL, 2.0f);
	fRows->GroupLayout()->SetInsets(6, 2, 6, 2);
	fRows->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fEmpty = new BStringView("empty", "No downloads.");

	BButton* clear = _ActionButton("Clear finished", MSG_DOWNLOAD_CLEAR, 0);
	BFont boldFont;
	GetFont(&boldFont);
	boldFont.SetFace(B_BOLD_FACE);
	BStringView* title = new BStringView("title", "Downloads");
	title->SetFont(&boldFont);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 4)
		.SetInsets(8, 6, 8, 6)
		.AddGroup(B_HORIZONTAL, 8)
			.Add(title)
			.AddGlue()
			.Add(clear)
		.End()
		.Add(fRows, 1.0f)
		.Add(fEmpty);
}


void
DownloadsPanelView::AttachedToWindow()
{
	BView::AttachedToWindow();

	// Row buttons invoke this window; the panel forwards the row
	// messages to the owner, whose messenger only resolves once the
	// owner exists.
	for (int32 i = 0; i < fRows->CountChildren(); i++) {
		BView* row = fRows->ChildAt(i);
		for (int32 j = 0; j < row->CountChildren(); j++) {
			BView* child = row->ChildAt(j);
			BButton* button = dynamic_cast<BButton*>(child);
			if (button != NULL)
				button->SetTarget(Window());
		}
	}

	BButton* clear = dynamic_cast<BButton*>(FindView("Clear finished"));
	if (clear != NULL)
		clear->SetTarget(Window());
}


BSize
DownloadsPanelView::PreferredSize()
{
	BSize size = BView::PreferredSize();
	if (size.width < 420)
		size.width = 420;
	return size;
}


BGroupView*
DownloadsPanelView::_BuildRow(const DownloadRecord& item)
{
	BGroupView* row = new BGroupView(B_HORIZONTAL, 6.0f);
	row->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	row->GroupLayout()->SetInsets(2, 2, 2, 2);

	BStringView* name = new BStringView("name", item.filename.c_str());
	BFont nameFont;
	GetFont(&nameFont);
	nameFont.SetFace(B_BOLD_FACE);
	name->SetFont(&nameFont);

	BStringView* url = new BStringView("url", item.url.c_str());
	rgb_color gray = ui_color(B_PANEL_TEXT_COLOR);
	gray.red = (uint8)(gray.red * 0.7f);
	gray.green = (uint8)(gray.green * 0.7f);
	gray.blue = (uint8)(gray.blue * 0.7f);
	url->SetHighColor(gray);
	url->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	BView* labels = new BView("labels", B_FOLLOW_ALL, 0);
	BLayoutBuilder::Group<>(labels, B_VERTICAL, 0)
		.Add(name)
		.Add(url);

	BStringView* status = new BStringView("status",
		_StatusText(item).String());

	BLayoutBuilder::Group<>(row, B_HORIZONTAL, 6)
		.Add(labels, 1.0f)
		.Add(status);

	switch (item.state) {
		case kDownloadStateActive:
			row->GroupLayout()->AddView(
				_ActionButton("Cancel", MSG_DOWNLOAD_CANCEL,
					(int64)item.id));
			break;

		case kDownloadStateFinished:
			row->GroupLayout()->AddView(
				_ActionButton("Show", MSG_DOWNLOAD_SHOW,
					(int64)item.id));
			break;

		case kDownloadStateFailed:
		case kDownloadStateCancelled:
		default:
			row->GroupLayout()->AddView(
				_ActionButton("Retry", MSG_DOWNLOAD_RETRY,
					(int64)item.id));
			row->GroupLayout()->AddView(
				_ActionButton("Show", MSG_DOWNLOAD_SHOW,
					(int64)item.id));
			break;
	}

	return row;
}


void
DownloadsPanelView::SetContent(const DownloadsModel& model)
{
	fModel = model;

	for (int32 i = fRows->CountChildren() - 1; i >= 0; i--) {
		BView* child = fRows->ChildAt(i);
		fRows->RemoveChild(child);
		delete child;
	}

	for (int32 i = 0; i < model.Count(); i++)
		fRows->GroupLayout()->AddView(_BuildRow(model.ItemAt(i)));

	if (model.Count() == 0) {
		fEmpty->Show();
		fRows->Hide();
	} else {
		fEmpty->Hide();
		fRows->Show();
	}
	Invalidate();
}


//#pragma mark -


DownloadsPanel::DownloadsPanel(const DownloadsModel& model,
	const BMessenger& target)
	:
	BWindow(BRect(0, 0, 0, 0), "Downloads", B_TITLED_WINDOW_LOOK,
		B_FLOATING_APP_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fView(NULL),
	fTarget(new BMessenger(target))
{
	fView = new DownloadsPanelView(BRect(0, 0, 0, 0), this);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0)
		.Add(fView);

	fView->SetContent(model);
}


DownloadsPanel::~DownloadsPanel()
{
	delete fTarget;
}


void
DownloadsPanel::SetModel(const DownloadsModel& model)
{
	// The caller runs on the owner window's thread; hand the content
	// over through the panel's own looper.
	BMessage message(kDownloadsSetModel);
	for (int32 i = 0; i < model.Count(); i++) {
		const DownloadRecord& item = model.ItemAt(i);
		message.AddInt64("id", (int64)item.id);
		message.AddString("filename", item.filename.c_str());
		message.AddString("url", item.url.c_str());
		message.AddString("destination", item.destination.c_str());
		message.AddInt64("received", (int64)item.receivedBytes);
		message.AddInt64("total", (int64)item.totalBytes);
		message.AddInt32("state", (int32)item.state);
	}
	PostMessage(&message);
}


void
DownloadsPanel::Go(BPoint where)
{
	if (!Lock())
		return;

	BSize size = fView->PreferredSize();
	ResizeTo(size.width, size.height);

	BRect frame(0, 0, size.width - 1, size.height - 1);
	frame.OffsetTo(where);
	BScreen screen(B_MAIN_SCREEN_ID);
	BRect screenFrame = screen.Frame();
	if (frame.bottom > screenFrame.bottom)
		frame.OffsetBy(0, screenFrame.bottom - frame.bottom);
	else if (frame.top < screenFrame.top)
		frame.OffsetBy(0, screenFrame.top - frame.top);
	if (frame.right > screenFrame.right)
		frame.OffsetBy(screenFrame.right - frame.right, 0);
	else if (frame.left < screenFrame.left)
		frame.OffsetBy(screenFrame.left - frame.left, 0);
	MoveTo(frame.LeftTop());

	fView->Invalidate();
	Unlock();

	Show();
	Activate();
}


bool
DownloadsPanel::QuitRequested()
{
	if (fTarget->IsValid()) {
		BMessage message(MSG_DOWNLOADS_CLOSED);
		fTarget->SendMessage(&message);
	}
	return true;
}


void
DownloadsPanel::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kDownloadsSetModel:
		{
			DownloadsModel model;
			const char* filename = NULL;
			const char* url = NULL;
			const char* destination = NULL;
			for (int32 i = 0;
				message->FindString("filename", i, &filename) == B_OK; i++) {
				int64 id = 0;
				int64 received = 0;
				int64 total = 0;
				int32 state = 0;
				message->FindInt64("id", i, &id);
				message->FindString("url", i, &url);
				message->FindString("destination", i, &destination);
				message->FindInt64("received", i, &received);
				message->FindInt64("total", i, &total);
				message->FindInt32("state", i, &state);
				model.AddOrUpdate((uint64)id, filename, url, destination,
					(uint64)received, (uint64)total);
				model.MarkState((uint64)id, (DownloadState)state);
			}

			fView->SetContent(model);
			BSize size = fView->PreferredSize();
			ResizeTo(size.width, size.height);
			break;
		}

		case MSG_DOWNLOAD_CANCEL:
		case MSG_DOWNLOAD_RETRY:
		case MSG_DOWNLOAD_SHOW:
		case MSG_DOWNLOAD_CLEAR:
			// Row buttons land here; hand them to the owner window.
			if (fTarget->IsValid())
				fTarget->SendMessage(message);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}
