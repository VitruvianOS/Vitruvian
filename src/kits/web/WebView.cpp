/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the LGPL License.
 *
 * BWebView: BView wrapper for the WPE WebKit2 compositor bridge.
 */

#include <web/WebView.h>
#include <web/WebDefs.h>

#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <Clipboard.h>
#include <Cursor.h>
#include <Entry.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <String.h>
#include <TextControl.h>
#include <Window.h>

#include <cstdio>
#include <cstring>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include <Entry.h>
#include <Node.h>

#ifdef HAS_WPE
#include "clipboard_dragdrop.h"
#include "notifications_bridge.h"
#include "compositor_adapter.h"
#include "download_handler.h"
#include "event_adapter.h"
#include "frame_notifier.h"
#include "key_tables.h"
#include "session_adapter.h"
#include "webkit_bridge.h"
#include "process_manager.h"


static const uint32 kMsgFrameReady = 'wfrm';
// Private messages: view thread only, not part of WebDefs.
static const uint32 kMsgCursorChanged = '_wcu';
static const uint32 kMsgShowContextMenu = '_wcm';
static const uint32 kMsgFullScreen = '_wfs';
static const uint32 kMsgDialogReply = '_wdr';
static const uint32 kMsgFileChooserDone = '_wfd';

// State shared between the GLib thread (which waits) and the window
// thread (which runs the dialog). std primitives only: the no-raw-
// semaphore rule applies here.
struct DialogState {
	std::mutex				mutex;
	std::condition_variable	cond;
	bool					done = false;
	bool					accepted = false;
	std::string				text;
};

struct FileChooserState {
	WebKitFileChooserRequest*	request;
	bool						multiple;
	std::vector<std::string>	paths;
	bool						cancelled;
};

static SessionAdapter* sDefaultSession = NULL;
static std::string sDefaultProfileDir;
#endif


struct BWebView::WebViewImpl {
	BString		fCurrentURL;
	BString		fTitle;
	double		fProgress;
	bool		fLoading;
	BView*		fHostView;

#ifdef HAS_WPE
	CompositorAdapter*	fCompositor;
	EventAdapter*		fEventAdapter;
	WebKitBridge*		fBridge;
	FrameNotifier*		fNotifier;
	ClipboardDragDrop*	fClipboard;
	SessionAdapter*		fSession;
	bool				fOwnsSession;
	BMessenger			fSelfMessenger;
	BPoint				fLastMouse;
	std::string			fHoverLink;
	std::string			fHoverImage;
	int32				fCursorId;
	bool				fFullScreen;
	bool				fDragPending;
	BPoint				fDragStart;
	std::string			fDragLink;
	DialogState*		fPendingDialog;
	FileChooserState*	fPendingFileChooser;
	float				fZoom;
#endif

	WebViewImpl()
		:
		fProgress(0.0),
		fLoading(false),
		fHostView(NULL)
#ifdef HAS_WPE
		,
		fCompositor(NULL),
		fEventAdapter(NULL),
		fBridge(NULL),
		fNotifier(NULL),
		fClipboard(NULL),
		fSession(NULL),
		fOwnsSession(false),
		fCursorId(B_CURSOR_ID_SYSTEM_DEFAULT),
		fFullScreen(false),
		fDragPending(false),
		fDragStart(BPoint(0, 0)),
		fPendingDialog(NULL),
		fPendingFileChooser(NULL),
		fZoom(1.0)
#endif
	{
	}
};


#ifdef HAS_WPE
static void
waitForDialog(DialogState* state)
{
	std::unique_lock<std::mutex> lock(state->mutex);
	state->cond.wait(lock, [state]() { return state->done; });
}


static void
finishDialog(DialogState* state, bool accepted, const std::string& text)
{
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		state->accepted = accepted;
		state->text = text;
		state->done = true;
	}
	state->cond.notify_all();
}
#endif


BWebView::BWebView(BRect frame, const char* name)
	:
	BView(frame, name, B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP,
		B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fImpl(new WebViewImpl())
{
}


BWebView::~BWebView()
{
#ifdef HAS_WPE
	delete fImpl->fNotifier;
	delete fImpl->fBridge;
	delete fImpl->fEventAdapter;
	delete fImpl->fClipboard;
	if (fImpl->fOwnsSession)
		delete fImpl->fSession;
#endif
	delete fImpl;
}


#ifdef HAS_WPE
void
BWebView::SetDefaultProfileDir(const char* path)
{
	sDefaultProfileDir = path != NULL ? path : "";
}


void
BWebView::SetSession(const char* profileDir, bool ephemeral)
{
	if (fImpl->fBridge != NULL)
		return;	// too late: the view is already attached

	if (fImpl->fOwnsSession) {
		delete fImpl->fSession;
		fImpl->fSession = NULL;
		fImpl->fOwnsSession = false;
	}

	if (ephemeral) {
		fImpl->fSession = new SessionAdapter("", true);
		fImpl->fOwnsSession = true;
		return;
	}

	// The default profile is shared: cookies and logins must survive
	// across windows.
	if (sDefaultSession == NULL) {
		std::string dir = profileDir != NULL && profileDir[0] != '\0'
			? profileDir : sDefaultProfileDir;
		if (dir.empty()) {
			const char* home = getenv("HOME");
			dir = home != NULL
				? std::string(home) + "/config/settings/NetPlus/profile"
				: "/tmp/netplus-profile";
		}
		sDefaultSession = new SessionAdapter(dir, false);
	}
	fImpl->fSession = sDefaultSession;
	fImpl->fOwnsSession = false;
}


void
BWebView::ClearDefaultProfileData()
{
	if (sDefaultSession != NULL) {
		sDefaultSession->ClearCookies();
		sDefaultSession->ClearCache();
	}
}
#endif


void
BWebView::AttachedToWindow()
{
	BView::AttachedToWindow();
	fImpl->fHostView = this;

#ifdef HAS_WPE
	// Start the GLib event loop if not already running.
	ProcessManager::Instance().StartGLibLoop();

	int w = (int)(Bounds().Width() + 1);
	int h = (int)(Bounds().Height() + 1);

	fImpl->fBridge = new WebKitBridge(w, h, fImpl->fSession);
	if (!fImpl->fBridge->IsValid()) {
		delete fImpl->fBridge;
		fImpl->fBridge = NULL;
		return;
	}

	fImpl->fCompositor = fImpl->fBridge->compositor.get();
	fImpl->fEventAdapter = new EventAdapter(fImpl->fBridge->backend);
	fImpl->fNotifier = new FrameNotifier();
	fImpl->fClipboard = new ClipboardDragDrop();
	fImpl->fClipboard->Attach(fImpl->fBridge->webView);
	// Frames arrive on the GLib thread: ask the window thread to redraw
	// instead of taking the window lock from there.
	BMessenger self(this);
	fImpl->fBridge->compositor->onFrameReady = [self]() {
		self.SendMessage(kMsgFrameReady);
	};

	// Wire bridge callbacks to BWebView target messenger.
	fImpl->fBridge->onTitleChanged = [this](const std::string& t) {
		fImpl->fTitle.SetTo(t.c_str());
		if (fTarget.IsValid()) {
			BMessage msg(B_WEB_PAGE_TITLE_CHANGED);
			msg.AddString(B_WEB_TITLE_FIELD, t.c_str());
			fTarget.SendMessage(&msg);
		}
	};

	fImpl->fBridge->onURLChanged = [this](const std::string& u) {
		fImpl->fCurrentURL.SetTo(u.c_str());
		if (fTarget.IsValid()) {
			BMessage msg(B_WEB_PAGE_URL_CHANGED);
			msg.AddString(B_WEB_URL_FIELD, u.c_str());
			fTarget.SendMessage(&msg);
		}
	};

	fImpl->fBridge->onProgressChanged = [this](double p) {
		fImpl->fProgress = p;
		if (fTarget.IsValid()) {
			BMessage msg(B_WEB_PAGE_PROGRESS_CHANGED);
			msg.AddDouble(B_WEB_PROGRESS_FIELD, p);
			fTarget.SendMessage(&msg);
		}
	};

	fImpl->fBridge->onLoadFinished = [this]() {
		fImpl->fLoading = false;
		fImpl->fProgress = 1.0;
		if (fTarget.IsValid()) {
			BMessage msg(B_WEB_PAGE_LOAD_FINISHED);
			msg.AddBool("web:can_go_back", fImpl->fBridge->CanGoBack());
			msg.AddBool("web:can_go_forward",
				fImpl->fBridge->CanGoForward());
			fTarget.SendMessage(&msg);
		}
	};

	fImpl->fBridge->onLoadFailed = [this](const std::string& e) {
		fImpl->fLoading = false;
		if (fTarget.IsValid()) {
			BMessage msg(B_WEB_PAGE_LOAD_FAILED);
			msg.AddString(B_WEB_ERROR_FIELD, e.c_str());
			fTarget.SendMessage(&msg);
		}
	};

	fImpl->fBridge->onHoverChanged = [this](const std::string& link,
		const std::string& image) {
		fImpl->fHoverLink = link;
		fImpl->fHoverImage = image;
		if (fTarget.IsValid()) {
			BMessage msg(B_WEB_PAGE_HOVER_CHANGED);
			msg.AddString(B_WEB_URL_FIELD, link.c_str());
			msg.AddString(B_WEB_IMAGE_FIELD, image.c_str());
			fTarget.SendMessage(&msg);
		}
	};

	fImpl->fBridge->onCursorChanged = [this](int32 cursorId) {
		if (cursorId == fImpl->fCursorId)
			return;
		fImpl->fCursorId = cursorId;
		fImpl->fSelfMessenger.SendMessage(kMsgCursorChanged);
	};

	fImpl->fBridge->onContextMenu = [this](const std::string& link,
		const std::string& image, int32 /*x*/, int32 /*y*/) {
		BMessage msg(kMsgShowContextMenu);
		msg.AddString(B_WEB_URL_FIELD, link.c_str());
		msg.AddString(B_WEB_IMAGE_FIELD, image.c_str());
		fImpl->fSelfMessenger.SendMessage(&msg);
	};

	fImpl->fBridge->onNewWindowRequested = [this](const std::string& url) {
		if (fTarget.IsValid()) {
			BMessage msg(B_WEB_PAGE_NEW_WINDOW);
			msg.AddString(B_WEB_URL_FIELD, url.c_str());
			fTarget.SendMessage(&msg);
		}
	};

	fImpl->fBridge->onWebProcessCrashed = [this]() {
		// Show an error page with a Reload link instead of a dead
		// view: the next load restarts the web process.
		std::string url = fImpl->fBridge->CurrentURL();
		std::string html =
			"<!DOCTYPE html><html><head><title>Page crashed</title></head>"
			"<body style='font-family:sans-serif;text-align:center;"
			"padding:3em'>"
			"<h1 style='color:#c00'>The page crashed</h1>"
			"<p>The web process for this page stopped unexpectedly.</p>"
			"<p><a href='" + url + "'>Reload the page</a></p>"
			"</body></html>";
		fImpl->fBridge->LoadHTML(html, "vos-error://crash");
		if (fTarget.IsValid()) {
			BMessage msg(B_WEB_PAGE_PROCESS_CRASHED);
			fTarget.SendMessage(&msg);
		}
	};

	fImpl->fBridge->onFullscreenChanged = [this](bool entered) {
		fImpl->fFullScreen = entered;
		BMessage msg(kMsgFullScreen);
		msg.AddBool(B_WEB_FULLSCREEN_FIELD, entered);
		fImpl->fSelfMessenger.SendMessage(&msg);
	};

	// Script dialogs block the GLib thread until the window thread
	// answered through the DialogState.
	fImpl->fBridge->onAlert = [this](const std::string& message) {
		DialogState state;
		BMessage msg(kMsgDialogReply);
		msg.AddInt32("what", B_WEB_SCRIPT_ALERT);
		msg.AddString(B_WEB_MESSAGE_FIELD, message.c_str());
		msg.AddPointer("state", &state);
		fImpl->fSelfMessenger.SendMessage(&msg);
		waitForDialog(&state);
		return true;
	};

	fImpl->fBridge->onConfirm = [this](const std::string& message) {
		DialogState state;
		BMessage msg(kMsgDialogReply);
		msg.AddInt32("what", B_WEB_SCRIPT_CONFIRM);
		msg.AddString(B_WEB_MESSAGE_FIELD, message.c_str());
		msg.AddPointer("state", &state);
		fImpl->fSelfMessenger.SendMessage(&msg);
		waitForDialog(&state);
		return state.accepted;
	};

	fImpl->fBridge->onPrompt = [this](const std::string& message,
		const std::string& initial) {
		DialogState state;
		BMessage msg(kMsgDialogReply);
		msg.AddInt32("what", B_WEB_SCRIPT_PROMPT);
		msg.AddString(B_WEB_MESSAGE_FIELD, message.c_str());
		msg.AddString(B_WEB_DIALOG_TEXT, initial.c_str());
		msg.AddPointer("state", &state);
		fImpl->fSelfMessenger.SendMessage(&msg);
		waitForDialog(&state);
		return state.text;
	};

	// The file chooser answers asynchronously: the request pointer
	// (ref-held by the bridge handler) rides to the window thread,
	// which shows a BFilePanel and calls back into CompleteFileChooser.
	// WebKit calls stay on the GLib thread: the multiple flag is read
	// here, not in MessageReceived.
	fImpl->fBridge->onRunFileChooser
		= [this](WebKitFileChooserRequest* request) {
		FileChooserState* state = new FileChooserState;
		state->request = request;
		state->multiple = webkit_file_chooser_request_get_select_multiple(
			request);
		state->cancelled = true;
		fImpl->fPendingFileChooser = state;

		BMessage msg(kMsgFileChooserDone);
		fImpl->fSelfMessenger.SendMessage(&msg);
		return true;
	};

	fImpl->fBridge->onShowNotification = [this](const std::string& title,
		const std::string& body) {
		fImpl->fBridge->notifications->Show(title, body);
		if (fTarget.IsValid()) {
			BMessage msg(B_WEB_NOTIFICATION_SHOW);
			msg.AddString(B_WEB_TITLE_FIELD, title.c_str());
			msg.AddString(B_WEB_MESSAGE_FIELD, body.c_str());
			fTarget.SendMessage(&msg);
		}
	};

	fImpl->fBridge->SetNotificationPermissionHandler(
		[this](const std::string& site) -> bool {
		DialogState state;
		BMessage msg(kMsgDialogReply);
		msg.AddInt32("what", B_WEB_NOTIFICATION_ASK);
		msg.AddString(B_WEB_URL_FIELD, site.c_str());
		msg.AddPointer("state", &state);
		fImpl->fSelfMessenger.SendMessage(&msg);
		waitForDialog(&state);
		return state.accepted;
	});

	// Download events fan in from the shared per-session handler.
	if (fImpl->fBridge->downloadHandler != NULL) {
		DownloadHandler::Listener listener;
		listener.onStarted = [this](const DownloadEntry& e) {
			if (!fTarget.IsValid())
				return;
			BMessage msg(B_WEB_DOWNLOAD_STARTED);
			msg.AddUInt64(B_WEB_DOWNLOAD_ID, e.id);
			msg.AddString(B_WEB_DOWNLOAD_URL, e.url.c_str());
			msg.AddString(B_WEB_DOWNLOAD_FILENAME, e.filename.c_str());
			msg.AddString(B_WEB_DOWNLOAD_DESTINATION,
				e.destination.c_str());
			msg.AddUInt64(B_WEB_DOWNLOAD_RECEIVED, e.receivedBytes);
			msg.AddUInt64(B_WEB_DOWNLOAD_TOTAL, e.totalBytes);
			fTarget.SendMessage(&msg);
		};
		listener.onProgress = [this](const DownloadEntry& e) {
			if (!fTarget.IsValid())
				return;
			BMessage msg(B_WEB_DOWNLOAD_PROGRESS);
			msg.AddUInt64(B_WEB_DOWNLOAD_ID, e.id);
			msg.AddString(B_WEB_DOWNLOAD_FILENAME, e.filename.c_str());
			msg.AddUInt64(B_WEB_DOWNLOAD_RECEIVED, e.receivedBytes);
			msg.AddUInt64(B_WEB_DOWNLOAD_TOTAL, e.totalBytes);
			fTarget.SendMessage(&msg);
		};
		listener.onFinished = [this](const DownloadEntry& e) {
			if (!fTarget.IsValid())
				return;
			BMessage msg(B_WEB_DOWNLOAD_FINISHED);
			msg.AddUInt64(B_WEB_DOWNLOAD_ID, e.id);
			msg.AddString(B_WEB_DOWNLOAD_FILENAME, e.filename.c_str());
			msg.AddString(B_WEB_DOWNLOAD_DESTINATION,
				e.destination.c_str());
			fTarget.SendMessage(&msg);
		};
		listener.onFailed = [this](const DownloadEntry& e) {
			if (!fTarget.IsValid())
				return;
			BMessage msg(B_WEB_DOWNLOAD_FAILED);
			msg.AddUInt64(B_WEB_DOWNLOAD_ID, e.id);
			msg.AddString(B_WEB_DOWNLOAD_FILENAME, e.filename.c_str());
			fTarget.SendMessage(&msg);
		};
		fImpl->fBridge->downloadHandler->AddListener(this, listener);
	}

	fImpl->fSelfMessenger = BMessenger(this);
#endif
}


void
BWebView::DetachedFromWindow()
{
#ifdef HAS_WPE
	// Unblock any dialog waiter before tearing the bridge down: the
	// window is going away and nobody will answer.
	if (fImpl->fPendingDialog != NULL)
		finishDialog(fImpl->fPendingDialog, false, "");
	fImpl->fPendingDialog = NULL;

	if (fImpl->fBridge != NULL && fImpl->fBridge->downloadHandler != NULL)
		fImpl->fBridge->downloadHandler->RemoveListener(this);

	// Destroy bridge objects; order matters.
	delete fImpl->fNotifier;
	fImpl->fNotifier = NULL;
	delete fImpl->fEventAdapter;
	fImpl->fEventAdapter = NULL;
	delete fImpl->fBridge;
	fImpl->fBridge = NULL;
	fImpl->fCompositor = NULL;
	fImpl->fSelfMessenger = BMessenger();
#endif

	fImpl->fHostView = NULL;
	BView::DetachedFromWindow();
}


void
BWebView::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);

#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid())
		fImpl->fBridge->Resize((int)width, (int)height);
#endif
}


void
BWebView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgFrameReady:
			Invalidate();
			break;
#ifdef HAS_WPE
		case B_MOUSE_WHEEL_CHANGED:
			if (fImpl->fEventAdapter != NULL) {
				BMessage wheel(*message);
				wheel.RemoveName("where");
				wheel.AddPoint("where", fImpl->fLastMouse);
				fImpl->fEventAdapter->TranslateMouseWheel(&wheel);
			}
			break;

		case kMsgCursorChanged:
		{
			static BCursor* sCursors[32] = { NULL };
			int32 id = fImpl->fCursorId;
			if (id >= 0 && id < 32) {
				if (sCursors[id] == NULL)
					sCursors[id] = new BCursor((BCursorID)id);
				SetViewCursor(sCursors[id], true);
			}
			break;
		}

		case kMsgShowContextMenu:
		{
			const char* link = NULL;
			const char* image = NULL;
			message->FindString(B_WEB_URL_FIELD, &link);
			message->FindString(B_WEB_IMAGE_FIELD, &image);

			BPopUpMenu* menu = new BPopUpMenu("context", false, false);
			BMenuItem* backItem = new BMenuItem("Back",
				new BMessage(B_WEB_NAV_BACK), B_LEFT_ARROW);
			BMenuItem* forwardItem = new BMenuItem("Forward",
				new BMessage(B_WEB_NAV_FORWARD), B_RIGHT_ARROW);
			BMenuItem* reloadItem = new BMenuItem("Reload",
				new BMessage(B_WEB_NAV_RELOAD));
			menu->AddItem(backItem);
			menu->AddItem(forwardItem);
			menu->AddItem(reloadItem);
			menu->AddSeparatorItem();

			BMenuItem* openNewItem = NULL;
			BMenuItem* saveLinkItem = NULL;
			BMenuItem* copyLinkItem = NULL;
			if (link != NULL && link[0] != '\0') {
				BMessage* openMsg = new BMessage(B_WEB_OPEN_LOCATION);
				openMsg->AddString(B_WEB_URL_FIELD, link);
				openMsg->AddBool("new_window", true);
				openNewItem = new BMenuItem("Open link in new window",
					openMsg);
				menu->AddItem(openNewItem);

				saveLinkItem = new BMenuItem("Save link as...",
					new BMessage(B_WEB_SAVE_LINK_AS));
				saveLinkItem->Message()->AddString(B_WEB_URL_FIELD, link);
				menu->AddItem(saveLinkItem);

				copyLinkItem = new BMenuItem("Copy link",
					new BMessage(B_WEB_COPY_LINK));
				copyLinkItem->Message()->AddString(B_WEB_URL_FIELD, link);
				menu->AddItem(copyLinkItem);
				menu->AddSeparatorItem();
			}

			BMenuItem* cutItem = new BMenuItem("Cut",
				new BMessage(B_WEB_EDIT_CUT));
			BMenuItem* copyItem = new BMenuItem("Copy",
				new BMessage(B_WEB_EDIT_COPY));
			BMenuItem* pasteItem = new BMenuItem("Paste",
				new BMessage(B_WEB_EDIT_PASTE));
			BMenuItem* selectAllItem = new BMenuItem("Select all",
				new BMessage(B_WEB_EDIT_SELECT_ALL));
			menu->AddItem(cutItem);
			menu->AddItem(copyItem);
			menu->AddItem(pasteItem);
			menu->AddItem(selectAllItem);

			BMenuItem* saveImageItem = NULL;
			if (image != NULL && image[0] != '\0') {
				menu->AddSeparatorItem();
				saveImageItem = new BMenuItem("Save image as...",
					new BMessage(B_WEB_SAVE_IMAGE_AS));
				saveImageItem->Message()->AddString(B_WEB_URL_FIELD, image);
				menu->AddItem(saveImageItem);
			}

			backItem->SetEnabled(CanGoBack());
			forwardItem->SetEnabled(CanGoForward());

			BMenuItem* selected = menu->Go(fImpl->fLastMouse, true);
			if (selected != NULL) {
				uint32 what = selected->Message()->what;
				switch (what) {
					case B_WEB_NAV_BACK:
						GoBack();
						break;
					case B_WEB_NAV_FORWARD:
						GoForward();
						break;
					case B_WEB_NAV_RELOAD:
						Reload();
						break;
					case B_WEB_EDIT_CUT:
						Cut();
						break;
					case B_WEB_EDIT_COPY:
						Copy();
						break;
					case B_WEB_EDIT_PASTE:
						Paste();
						break;
					case B_WEB_EDIT_SELECT_ALL:
						SelectAll();
						break;
					default:
						// Link/image actions and new-window open go to
						// the window through the item's message.
						if (Window() != NULL) {
							BMessage copy(*selected->Message());
							Window()->PostMessage(&copy);
						}
						break;
				}
			}
			delete menu;
			break;
		}

		case kMsgFullScreen:
		{
			bool entered = false;
			message->FindBool(B_WEB_FULLSCREEN_FIELD, &entered);
			// The window thread resizes the window itself (there is
			// no SetFullScreen); then WebKit is told the transition
			// is done so the page lays out for the new size.
			if (fTarget.IsValid()) {
				BMessage msg(B_WEB_PAGE_FULLSCREEN);
				msg.AddBool(B_WEB_FULLSCREEN_FIELD, entered);
				fTarget.SendMessage(&msg);
			}
			if (entered)
				fImpl->fBridge->NotifyDidEnterFullscreen();
			else
				fImpl->fBridge->NotifyDidExitFullscreen();
			break;
		}

		case kMsgDialogReply:
		{
			int32 what = 0;
			message->FindInt32("what", &what);
			DialogState* state = NULL;
			message->FindPointer("state", (void**)&state);
			if (state == NULL)
				break;
			fImpl->fPendingDialog = state;

			const char* text = NULL;
			message->FindString(B_WEB_MESSAGE_FIELD, &text);
			const char* site = NULL;
			message->FindString(B_WEB_URL_FIELD, &site);

			switch (what) {
				case B_WEB_SCRIPT_ALERT:
				{
					BAlert* alert = new BAlert("Alert",
						text != NULL ? text : "", "OK");
					alert->Go(NULL);
					finishDialog(state, true, "");
					break;
				}
				case B_WEB_SCRIPT_CONFIRM:
				{
					BAlert* alert = new BAlert("Confirm",
						text != NULL ? text : "", "Cancel", "OK");
					int32 result = alert->Go();
					finishDialog(state, result == 1, "");
					break;
				}
				case B_WEB_SCRIPT_PROMPT:
				{
					const char* initial = NULL;
					message->FindString(B_WEB_DIALOG_TEXT, &initial);
					BAlert* alert = new BAlert("Prompt",
						text != NULL ? text : "", "Cancel", "OK");
					BTextControl* field = new BTextControl("input", "",
						initial != NULL ? initial : "", NULL);
					field->SetExplicitMaxSize(
						BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
					alert->GetLayout()->AddView(field);
					field->MakeFocus(true);
					int32 result = alert->Go();
					finishDialog(state, result == 1,
						result == 1 ? field->Text() : "");
					break;
				}
				case B_WEB_NOTIFICATION_ASK:
				{
					BString message2("Allow notifications from ");
					message2 << (site != NULL && site[0] != '\0'
						? site : "this site") << "?";
					BAlert* alert = new BAlert("Notifications",
						message2.String(), "Deny", "Allow");
					int32 result = alert->Go();
					finishDialog(state, result == 1, "");
					break;
				}
				default:
					finishDialog(state, false, "");
					break;
			}
			fImpl->fPendingDialog = NULL;
			break;
		}

		case kMsgFileChooserDone:
		{
			FileChooserState* state = fImpl->fPendingFileChooser;
			if (state == NULL || Window() == NULL)
				break;

			BMessage panelMessage(B_WEB_FILE_PANEL_DONE);
			panelMessage.AddPointer("chooser", state);
			BFilePanel* panel = new BFilePanel(B_OPEN_PANEL,
				new BMessenger(this), NULL, 0, state->multiple,
				&panelMessage);
			panel->Show();
			break;
		}

		case B_WEB_FILE_PANEL_DONE:
		{
			FileChooserState* state = NULL;
			message->FindPointer("chooser", (void**)&state);
			if (state == NULL)
				break;
			fImpl->fPendingFileChooser = NULL;

			state->paths.clear();
			entry_ref ref;
			for (int32 i = 0;
				message->FindRef("refs", i, &ref) == B_OK; i++) {
				BPath path(&ref);
				if (path.InitCheck() == B_OK)
					state->paths.push_back(path.Path());
			}

			WebKitFileChooserRequest* request = state->request;
			if (request != NULL) {
				struct Args {
					WebKitFileChooserRequest* request;
					std::vector<std::string> paths;
				};
				Args* args = new Args{request, state->paths};
				ProcessManager::Instance().InvokeOnGLibThread(
					[](void* d) -> gboolean {
						Args* a = static_cast<Args*>(d);
						if (a->paths.empty()) {
							webkit_file_chooser_request_select_files(
								a->request, NULL);
						} else {
							std::vector<const gchar*> files;
							for (const auto& p : a->paths)
								files.push_back(p.c_str());
							files.push_back(NULL);
							webkit_file_chooser_request_select_files(
								a->request, files.data());
						}
						g_object_unref(a->request);
						delete a;
						return G_SOURCE_REMOVE;
					}, args);
			}
			delete state;
			break;
		}

		case B_WEB_EDIT_CUT:
			Cut();
			break;

		case B_WEB_EDIT_COPY:
			Copy();
			break;

		case B_WEB_EDIT_PASTE:
			Paste();
			break;

		case B_WEB_EDIT_SELECT_ALL:
			SelectAll();
			break;

		case B_WEB_SAVE_IMAGE_AS:
		case B_WEB_SAVE_LINK_AS:
		{
			const char* url = NULL;
			message->FindString("url", &url);
			if (url == NULL || fImpl->fBridge == NULL)
				break;
			BMessage panelMessage(B_WEB_SAVE_PANEL_DONE);
			panelMessage.AddString("url", url);
			BFilePanel* panel = new BFilePanel(B_SAVE_PANEL,
				new BMessenger(this), NULL, 0, false, &panelMessage);
			if (Window() != NULL)
			panel->Show();
			break;
		}

		case B_WEB_SAVE_PANEL_DONE:
		{
			const char* url = NULL;
			message->FindString("url", &url);
			entry_ref ref;
			if (url != NULL && message->FindRef("refs", &ref) == B_OK) {
				BPath path(&ref);
				if (path.InitCheck() == B_OK)
					fImpl->fBridge->DownloadURI(url, path.Path());
			}
			break;
		}
#endif
		default:
			BView::MessageReceived(message);
			break;
	}
}


void
BWebView::MakeFocus(bool focused)
{
	BView::MakeFocus(focused);
#ifdef HAS_WPE
	// Keys held when focus moves away (a game's arrow keys, Ctrl held
	// into the URL bar) would stay pressed in the page forever.
	if (!focused && fImpl->fEventAdapter != NULL)
		fImpl->fEventAdapter->ReleaseAllKeys();

	// WebKit only routes keys to page fields while the view is focused.
	if (fImpl->fBridge != NULL)
		fImpl->fBridge->HandleFocusChange(focused);
#endif
}


void
BWebView::Draw(BRect updateRect)
{
	BView::Draw(updateRect);

#ifdef HAS_WPE
	if (fImpl->fCompositor) {
		BBitmap* front = fImpl->fCompositor->AcquireFront();
		if (front && front->IsValid()) {
			DrawBitmap(front, front->Bounds(),
				Bounds(), B_FILTER_BITMAP_BILINEAR);
		}
		fImpl->fCompositor->ReleaseFront();
		return;
	}
#endif

	// Stub fallback when WPE is not linked.
	SetHighColor(255, 255, 255);
	FillRect(updateRect);
	SetHighColor(0, 0, 0);
	MovePenTo(BPoint(10, 30));
	DrawString("NetPlus web view (stub: WPE not linked)");
}


void
BWebView::MouseDown(BPoint where)
{
	BView::MouseDown(where);
	if (!IsFocus())
		MakeFocus(true);

#ifdef HAS_WPE
	fImpl->fLastMouse = where;

	// Middle click opens the link under the pointer in a new window.
	if (fImpl->fBridge != NULL && fImpl->fBridge->IsValid()) {
		int32 buttons = 0;
		if (Window() != NULL && Window()->CurrentMessage() != NULL)
			Window()->CurrentMessage()->FindInt32("buttons", &buttons);
		if ((buttons & B_TERTIARY_MOUSE_BUTTON) != 0
			&& !fImpl->fHoverLink.empty() && fTarget.IsValid()) {
			BMessage msg(B_WEB_PAGE_NEW_WINDOW);
			msg.AddString(B_WEB_URL_FIELD, fImpl->fHoverLink.c_str());
			fTarget.SendMessage(&msg);
			return;
		}

		// A drag starts over a link: remember it until the pointer
		// moves far enough to hand the URL to DragMessage.
		fImpl->fDragPending = !fImpl->fHoverLink.empty();
		fImpl->fDragStart = where;
		fImpl->fDragLink = fImpl->fHoverLink;

		// Forward the window's own message (buttons, clicks, modifiers)
		// with the position in view coordinates.
		BMessage msg(B_MOUSE_DOWN);
		if (Window() != NULL && Window()->CurrentMessage() != NULL)
			msg = *Window()->CurrentMessage();
		msg.RemoveName("where");
		msg.AddPoint("where", where);
		fImpl->fEventAdapter->TranslateMouseDown(&msg);
	}
#endif
}


void
BWebView::MouseUp(BPoint where)
{
	BView::MouseUp(where);

#ifdef HAS_WPE
	fImpl->fDragPending = false;

	if (fImpl->fEventAdapter && fImpl->fBridge
		&& fImpl->fBridge->IsValid()) {
		// Forward the window's own message (buttons, clicks, modifiers)
		// with the position in view coordinates.
		BMessage msg(B_MOUSE_UP);
		if (Window() != NULL && Window()->CurrentMessage() != NULL)
			msg = *Window()->CurrentMessage();
		msg.RemoveName("where");
		msg.AddPoint("where", where);
		fImpl->fEventAdapter->TranslateMouseUp(&msg);
	}
#endif
}


void
BWebView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* message)
{
	BView::MouseMoved(where, transit, message);

#ifdef HAS_WPE
	if (fImpl->fEventAdapter && fImpl->fBridge
		&& fImpl->fBridge->IsValid()) {
		// Dragging a link out of the page hands the URL to the
		// drag-and-drop system.
		if (fImpl->fDragPending && message == NULL) {
			float dx = where.x - fImpl->fDragStart.x;
			float dy = where.y - fImpl->fDragStart.y;
			if (dx * dx + dy * dy > 64.0f && !fImpl->fDragLink.empty()) {
				BMessage drag(B_SIMPLE_DATA);
				drag.AddString("url", fImpl->fDragLink.c_str());
				drag.AddString("text/plain", fImpl->fDragLink.c_str());
				DragMessage(&drag, BRect(fImpl->fDragStart,
					fImpl->fDragStart + BPoint(8, 8)), this);
				fImpl->fDragPending = false;
				return;
			}
		}

		// Forward the window's own message (buttons, clicks, modifiers)
		// with the position in view coordinates.
		BMessage msg(B_MOUSE_MOVED);
		if (Window() != NULL && Window()->CurrentMessage() != NULL)
			msg = *Window()->CurrentMessage();
		msg.RemoveName("where");
		msg.AddPoint("where", where);
		msg.AddInt32("be:transit", transit);
		fImpl->fLastMouse = where;
		fImpl->fEventAdapter->TranslateMouseMoved(&msg);
	}
#endif
}


void
BWebView::KeyDown(const char* bytes, int32 numBytes)
{
	BView::KeyDown(bytes, numBytes);

#ifdef HAS_WPE
	if (fImpl->fEventAdapter && fImpl->fBridge
		&& fImpl->fBridge->IsValid()) {
		// The window message carries "bytes", "key", "raw_char" and
		// "modifiers", which the adapter needs to build the WPE event.
		BMessage msg(B_KEY_DOWN);
		if (Window() != NULL && Window()->CurrentMessage() != NULL)
			msg = *Window()->CurrentMessage();
		else
			msg.AddString("bytes", BString(bytes, numBytes));
		fImpl->fEventAdapter->TranslateKeyDown(&msg);
	}
#endif
}


void
BWebView::KeyUp(const char* bytes, int32 numBytes)
{
	BView::KeyUp(bytes, numBytes);

#ifdef HAS_WPE
	if (fImpl->fEventAdapter && fImpl->fBridge
		&& fImpl->fBridge->IsValid()) {
		// The window message carries "bytes", "key", "raw_char" and
		// "modifiers", which the adapter needs to build the WPE event.
		BMessage msg(B_KEY_UP);
		if (Window() != NULL && Window()->CurrentMessage() != NULL)
			msg = *Window()->CurrentMessage();
		else
			msg.AddString("bytes", BString(bytes, numBytes));
		fImpl->fEventAdapter->TranslateKeyUp(&msg);
	}
#endif
}


void
BWebView::LoadURL(const char* url)
{
	if (!url)
		return;

#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid()) {
		fImpl->fLoading = true;
		fImpl->fProgress = 0.0;
		fImpl->fBridge->LoadURL(std::string(url));
		return;
	}
#endif

	// Stub path: simulate load-finished.
	fImpl->fCurrentURL.SetTo(url);
	fImpl->fLoading = true;
	fImpl->fProgress = 0.0;

	if (fTarget.IsValid()) {
		BMessage msg(B_WEB_PAGE_URL_CHANGED);
		msg.AddString(B_WEB_URL_FIELD, url);
		fTarget.SendMessage(&msg);
	}

	fprintf(stderr, "BWebView::LoadURL(\"%s\") - stub\n", url);

	fImpl->fLoading = false;
	fImpl->fProgress = 1.0;
	fImpl->fTitle.SetTo(url);

	if (fTarget.IsValid()) {
		BMessage titleMsg(B_WEB_PAGE_TITLE_CHANGED);
		titleMsg.AddString(B_WEB_TITLE_FIELD, fImpl->fTitle.String());
		fTarget.SendMessage(&titleMsg);

		BMessage progressMsg(B_WEB_PAGE_PROGRESS_CHANGED);
		progressMsg.AddDouble(B_WEB_PROGRESS_FIELD, fImpl->fProgress);
		fTarget.SendMessage(&progressMsg);

		BMessage finishedMsg(B_WEB_PAGE_LOAD_FINISHED);
		fTarget.SendMessage(&finishedMsg);
	}
}


void
BWebView::LoadHTML(const char* html, const char* baseURL)
{
#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid()) {
		fImpl->fBridge->LoadHTML(
			std::string(html ? html : ""),
			std::string(baseURL ? baseURL : ""));
		return;
	}
#endif

	fprintf(stderr, "BWebView::LoadHTML - stub\n");
}


void
BWebView::GoBack()
{
#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid()) {
		fImpl->fBridge->GoBack();
		return;
	}
#endif

	fprintf(stderr, "BWebView::GoBack - stub\n");
}


void
BWebView::GoForward()
{
#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid()) {
		fImpl->fBridge->GoForward();
		return;
	}
#endif

	fprintf(stderr, "BWebView::GoForward - stub\n");
}


void
BWebView::Reload()
{
#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid()) {
		fImpl->fBridge->Reload(false);
		return;
	}
#endif

	fprintf(stderr, "BWebView::Reload - stub\n");
}


void
BWebView::Stop()
{
	fImpl->fLoading = false;

#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid()) {
		fImpl->fBridge->Stop();
		return;
	}
#endif

	fprintf(stderr, "BWebView::Stop - stub\n");
}


bool
BWebView::CanGoBack() const
{
#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid())
		return fImpl->fBridge->CanGoBack();
#endif

	return false;
}


bool
BWebView::CanGoForward() const
{
#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid())
		return fImpl->fBridge->CanGoForward();
#endif

	return false;
}


const char*
BWebView::CurrentURL() const
{
#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid()) {
		std::string url = fImpl->fBridge->CurrentURL();
		const_cast<BWebView*>(this)->fImpl->fCurrentURL.SetTo(
			url.c_str());
	}
#endif

	return fImpl->fCurrentURL.String();
}


const char*
BWebView::Title() const
{
#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid()) {
		std::string title = fImpl->fBridge->Title();
		const_cast<BWebView*>(this)->fImpl->fTitle.SetTo(
			title.c_str());
	}
#endif

	return fImpl->fTitle.String();
}


double
BWebView::LoadProgress() const
{
#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid())
		return fImpl->fBridge->LoadProgress();
#endif

	return fImpl->fProgress;
}


bool
BWebView::IsLoading() const
{
#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid())
		return fImpl->fBridge->IsLoading();
#endif

	return fImpl->fLoading;
}


void
BWebView::SetTarget(const BMessenger& target)
{
	fTarget = target;
}


#ifdef HAS_WPE
void
BWebView::SetBlockedDomains(const char* commaSeparated)
{
	if (fImpl->fBridge == NULL)
		return;
	std::vector<std::string> domains;
	std::string input = commaSeparated != NULL ? commaSeparated : "";
	size_t start = 0;
	while (start <= input.size()) {
		size_t comma = input.find(',', start);
		std::string entry = input.substr(start,
			comma == std::string::npos ? std::string::npos : comma - start);
		// Trim spaces.
		while (!entry.empty() && entry.front() == ' ')
			entry.erase(entry.begin());
		while (!entry.empty() && entry.back() == ' ')
			entry.pop_back();
		if (!entry.empty())
			domains.push_back(entry);
		if (comma == std::string::npos)
			break;
		start = comma + 1;
	}
	fImpl->fBridge->SetBlockedDomains(domains);
}


void
BWebView::SetJavaScriptEnabled(bool enabled)
{
	if (fImpl->fBridge != NULL)
		fImpl->fBridge->SetJavaScriptEnabled(enabled);
}


void
BWebView::SetImagesEnabled(bool enabled)
{
	if (fImpl->fBridge != NULL)
		fImpl->fBridge->SetImagesEnabled(enabled);
}


void
BWebView::SetUserAgentSuffix(const char* suffix)
{
	if (fImpl->fBridge != NULL)
		fImpl->fBridge->SetUserAgentSuffix(suffix != NULL ? suffix : "");
}


void
BWebView::SetZoomLevel(float zoom)
{
	fImpl->fZoom = zoom;
	if (fImpl->fBridge != NULL)
		fImpl->fBridge->SetZoomLevel(zoom);
}


float
BWebView::ZoomLevel() const
{
	return fImpl->fZoom;
}


void
BWebView::Cut()
{
	if (fImpl->fBridge == NULL)
		return;
	fImpl->fBridge->Cut();
}


void
BWebView::Copy()
{
	if (fImpl->fBridge == NULL)
		return;
	fImpl->fBridge->Copy();
}


void
BWebView::Paste()
{
	if (fImpl->fBridge == NULL)
		return;
	fImpl->fBridge->Paste();
}


void
BWebView::SelectAll()
{
	if (fImpl->fBridge == NULL)
		return;
	fImpl->fBridge->SelectAll();
}


void
BWebView::DownloadURI(const char* uri, const char* destination)
{
	if (fImpl->fBridge == NULL || uri == NULL)
		return;
	fImpl->fBridge->DownloadURI(uri,
		destination != NULL ? destination : "");
}


void
BWebView::NotifyFullScreen(bool entered)
{
	if (fImpl->fBridge == NULL)
		return;
	if (entered)
		fImpl->fBridge->NotifyDidEnterFullscreen();
	else
		fImpl->fBridge->NotifyDidExitFullscreen();
}
#else // !HAS_WPE
void
BWebView::SetDefaultProfileDir(const char*) {}
void
BWebView::SetSession(const char*, bool) {}
void
BWebView::ClearDefaultProfileData() {}
void
BWebView::SetBlockedDomains(const char*) {}
void
BWebView::SetJavaScriptEnabled(bool) {}
void
BWebView::SetImagesEnabled(bool) {}
void
BWebView::SetUserAgentSuffix(const char*) {}
void
BWebView::SetZoomLevel(float) {}
float
BWebView::ZoomLevel() const { return 1.0f; }
void
BWebView::Cut() {}
void
BWebView::Copy() {}
void
BWebView::Paste() {}
void
BWebView::SelectAll() {}
void
BWebView::DownloadURI(const char*, const char*) {}
void
BWebView::NotifyFullScreen(bool) {}
#endif
