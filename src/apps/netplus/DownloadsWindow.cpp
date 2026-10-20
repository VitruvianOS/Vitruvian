/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */


#include "DownloadsWindow.h"

#include <Alert.h>
#include <Button.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <Messenger.h>
#include <Notification.h>
#include <Path.h>
#include <Roster.h>
#include <ScrollView.h>
#include <String.h>

#include <web/WebDefs.h>
#include <web/WebView.h>

#include <cstdio>


static const uint32 kMsgCancelDownload = 'dlca';
static const uint32 kMsgOpenDownload = 'dlop';
static const uint32 kMsgShowInTracker = 'dlsh';
static const uint32 kMsgListSelection = 'dlsl';


class DownloadItem : public BStringItem {
public:
								DownloadItem(uint64 id)
									:
									BStringItem(""),
									fId(id),
									fReceived(0),
									fTotal(0),
									fFinished(false),
									fFailed(false)
								{
								}

	// The list does not erase behind items (see below); paint the row
	// whole so progress updates do not flash.
	virtual	void				DrawItem(BView* owner, BRect frame,
									bool complete)
								{
									BStringItem::DrawItem(owner, frame, true);
								}

			uint64				fId;
			BString				fFilename;
			BString				fDestination;
			uint64				fReceived;
			uint64				fTotal;
			bool				fFinished;
			bool				fFailed;
};


// Items paint their whole row; the list only fills the space below them.
class DownloadsListView : public BListView {
public:
								DownloadsListView(const char* name)
									:
									BListView(name, B_SINGLE_SELECTION_LIST)
								{
									SetViewColor(B_TRANSPARENT_COLOR);
								}

	virtual	void				Draw(BRect updateRect)
								{
									BRect empty = Bounds();
									const int32 count = CountItems();
									if (count > 0)
										empty.top = ItemFrame(count - 1).bottom + 1;
									empty = empty & updateRect;
									if (empty.IsValid()) {
										SetLowColor(ui_color(B_LIST_BACKGROUND_COLOR));
										FillRect(empty, B_SOLID_LOW);
									}
									BListView::Draw(updateRect);
								}
};


DownloadsWindow* DownloadsWindow::sInstance = NULL;


static BString
formatBytes(uint64 bytes)
{
	BString text;
	if (bytes >= 1024ull * 1024ull * 1024ull)
		text << (double)bytes / (1024.0 * 1024.0 * 1024.0) << " GB";
	else if (bytes >= 1024ull * 1024ull)
		text << (double)bytes / (1024.0 * 1024.0) << " MB";
	else if (bytes >= 1024ull)
		text << (double)bytes / 1024.0 << " KB";
	else
		text << bytes << " B";
	return text;
}


//	#pragma mark - Constructor/Destructor


DownloadsWindow::DownloadsWindow(BRect frame, BWebView* webView)
	:
	// No B_QUIT_ON_WINDOW_CLOSE: closing the downloads window must
	// not take the browser with it.
	BWindow(frame, "Downloads", B_TITLED_WINDOW, 0),
	fList(NULL),
	fCancelButton(NULL),
	fOpenButton(NULL),
	fShowButton(NULL),
	fWebView(webView)
{
	fList = new DownloadsListView("downloads");
	fList->SetSelectionMessage(new BMessage(kMsgListSelection));
	BScrollView* scroller = new BScrollView("scroll", fList, 0, true, true);

	fCancelButton = new BButton("Cancel", new BMessage(kMsgCancelDownload));
	fOpenButton = new BButton("Open", new BMessage(kMsgOpenDownload));
	fShowButton = new BButton("Show in Tracker",
		new BMessage(kMsgShowInTracker));
	fCancelButton->SetEnabled(false);
	fOpenButton->SetEnabled(false);
	fShowButton->SetEnabled(false);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_SMALL_SPACING)
		.Add(scroller, 1.0f)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fCancelButton)
			.Add(fOpenButton)
			.Add(fShowButton)
		.End();
}


DownloadsWindow::~DownloadsWindow()
{
	sInstance = NULL;
}


//	#pragma mark - Public static


void
DownloadsWindow::ShowWindow(BWebView* webView)
{
	if (sInstance == NULL) {
		BRect frame(200, 200, 700, 480);
		sInstance = new DownloadsWindow(frame, webView);
		sInstance->Show();
	} else {
		if (webView != NULL)
			sInstance->fWebView = webView;
		if (sInstance->IsHidden())
			sInstance->Show();
		else
			sInstance->Activate();
	}
}


void
DownloadsWindow::DetachWebView(BWebView* webView)
{
	if (sInstance != NULL && sInstance->fWebView == webView)
		sInstance->fWebView = NULL;
}


void
DownloadsWindow::HandleDownloadMessage(BMessage* message, BWebView* webView)
{
	// The window appears when a download starts and never steals focus
	// again; progress ticks only update an open window.
	if (sInstance == NULL) {
		if (message->what != B_WEB_DOWNLOAD_STARTED)
			return;
		ShowWindow(webView);
	}
	if (sInstance == NULL)
		return;
	if (webView != NULL)
		sInstance->fWebView = webView;
	BMessenger target(sInstance);
	target.SendMessage(message);
}


//	#pragma mark - Private


DownloadItem*
DownloadsWindow::_Selected() const
{
	int32 index = fList->CurrentSelection(0);
	if (index < 0)
		return NULL;
	return static_cast<DownloadItem*>(fList->ItemAt(index));
}


void
DownloadsWindow::_UpdateButtons()
{
	DownloadItem* selected = _Selected();
	fCancelButton->SetEnabled(selected != NULL
		&& !selected->fFinished && !selected->fFailed);
	fOpenButton->SetEnabled(selected != NULL
		&& selected->fFinished && !selected->fFailed);
	fShowButton->SetEnabled(selected != NULL
		&& !selected->fDestination.IsEmpty());
}


DownloadItem*
DownloadsWindow::_ItemForID(uint64 id) const
{
	for (int32 i = 0; i < fList->CountItems(); i++) {
		DownloadItem* item = static_cast<DownloadItem*>(fList->ItemAt(i));
		if (item->fId == id)
			return item;
	}
	return NULL;
}


void
DownloadsWindow::_UpdateLabel(DownloadItem* item)
{
	BString label;
	label << item->fFilename;
	if (item->fFailed) {
		label << " (failed)";
	} else if (item->fFinished) {
		label << " (done)";
	} else if (item->fTotal > 0) {
		int percent = (int)((double)item->fReceived * 100.0
			/ (double)item->fTotal);
		label << " (" << percent << "%, " << formatBytes(item->fReceived)
			<< " of " << formatBytes(item->fTotal) << ")";
	} else {
		label << " (" << formatBytes(item->fReceived) << ")";
	}
	// Progress ticks often leave the text unchanged; skip the redraw.
	if (label == item->Text())
		return;
	item->SetText(label.String());
	fList->InvalidateItem(fList->IndexOf(item));
}


void
DownloadsWindow::_UpsertRow(uint64 id, const char* filename,
	const char* destination, uint64 received, uint64 total,
	bool finished, bool failed)
{
	DownloadItem* item = _ItemForID(id);
	if (item == NULL) {
		item = new DownloadItem(id);
		fList->AddItem(item);
	}
	if (filename != NULL && filename[0] != '\0')
		item->fFilename = filename;
	if (destination != NULL && destination[0] != '\0')
		item->fDestination = destination;
	item->fReceived = received;
	item->fTotal = total;
	item->fFinished = finished;
	item->fFailed = failed;
	_UpdateLabel(item);
}


//	#pragma mark - BWindow


void
DownloadsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_WEB_DOWNLOAD_STARTED:
		case B_WEB_DOWNLOAD_PROGRESS:
		case B_WEB_DOWNLOAD_FINISHED:
		case B_WEB_DOWNLOAD_FAILED:
		{
			uint64 id = 0;
			message->FindUInt64(B_WEB_DOWNLOAD_ID, &id);
			const char* filename = NULL;
			message->FindString(B_WEB_DOWNLOAD_FILENAME, &filename);
			const char* destination = NULL;
			message->FindString(B_WEB_DOWNLOAD_DESTINATION, &destination);
			uint64 received = 0;
			message->FindUInt64(B_WEB_DOWNLOAD_RECEIVED, &received);
			uint64 total = 0;
			message->FindUInt64(B_WEB_DOWNLOAD_TOTAL, &total);

			bool finished = message->what == B_WEB_DOWNLOAD_FINISHED;
			bool failed = message->what == B_WEB_DOWNLOAD_FAILED;
			_UpsertRow(id, filename, destination, received, total,
				finished, failed);

			if (finished && filename != NULL) {
				BNotification notification(B_INFORMATION_NOTIFICATION);
				notification.SetGroup("NetPlus");
				notification.SetTitle("Download finished");
				notification.SetContent(filename);
				notification.Send();
			}

			_UpdateButtons();
			break;
		}

		case kMsgCancelDownload:
		{
			DownloadItem* selected = _Selected();
			if (selected != NULL && fWebView != NULL)
				fWebView->CancelDownload(selected->fId);
			break;
		}

		case kMsgOpenDownload:
		{
			DownloadItem* selected = _Selected();
			if (selected == NULL || selected->fDestination.IsEmpty())
				break;

			BEntry entry(selected->fDestination.String());
			entry_ref ref;
			if (entry.GetRef(&ref) == B_OK)
				be_roster->Launch(&ref);
			break;
		}

		case kMsgShowInTracker:
		{
			DownloadItem* selected = _Selected();
			if (selected == NULL || selected->fDestination.IsEmpty())
				break;

			// Open the folder with the file selected, not the file.
			BEntry entry(selected->fDestination.String());
			BEntry parent;
			entry_ref parentRef;
			node_ref node;
			if (entry.GetNodeRef(&node) != B_OK
				|| entry.GetParent(&parent) != B_OK
				|| parent.GetRef(&parentRef) != B_OK)
				break;

			BMessage refs(B_REFS_RECEIVED);
			refs.AddRef("refs", &parentRef);
			refs.AddData("nodeRefToSelect", B_RAW_TYPE, &node, sizeof(node));
			be_roster->Launch("application/x-vnd.Be-TRAK", &refs);
			break;
		}

		case kMsgListSelection:
			_UpdateButtons();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}
