/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MainWin.h"

#include <Application.h>
#include <Alert.h>
#include <Autolock.h>
#include <Catalog.h>
#include <Entry.h>
#include <FilePanel.h>
#include <GroupLayout.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Message.h>
#include <MessageRunner.h>
#include <OS.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Rect.h>
#include <Screen.h>
#include <String.h>
#include <TextView.h>

#include "Controller.h"
#include "ControllerView.h"
#include "InfoWin.h"
#include "LocationStringView.h"
#include "MediaMessages.h"
#include "PeakView.h"
#include "PlaybackState.h"
#include "Playlist.h"
#include "PlaylistItem.h"
#include "PlaylistWindow.h"
#include "VideoDisplay.h"
#include "VideoView.h"

#include <media2/MediaPlayer.h>
#include <SettingsMessage.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MediaPlayer"


static const float kMinWidth = 360;
static const float kMinHeight = 280;
static const float kAudioMinWidth = 250;
static const float kDefaultScale = 1.0f;


MainWin::MainWin(BRect frame)
	:
	BWindow(frame, B_TRANSLATE("MediaPlayer"), B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fController(NULL),
	fPlaylist(NULL),
	fVideoView(NULL),
	fControllerView(NULL),
	fLocationView(NULL),
	fPlaylistWindow(NULL),
	fMenuBar(NULL),
	fOpenPanel(NULL),
	fPositionRunner(NULL),
	fAlwaysOnTopItem(NULL),
	fHideInterfaceItem(NULL),
	fLoopItem(NULL),
	fSettings(NULL),
	fMuted(false),
	fLoop(false),
	fAlwaysOnTop(false),
	fHideInterface(false),
	fCursorHidden(false),
	fVideoScale(kDefaultScale),
	fStripLayoutIndex(-1),
	fStripHeight(0),
	fControlY(0),
	fVideoParNum(0),
	fVideoParDen(0),
	fVideoRotation(0)
{
	fController = new Controller(BMessenger(this));
	// A separate playlist here was never seen by the controller, so opened
	// files never played.
	fPlaylist = &fController->PlaylistRef();

	fSettings = new SettingsMessage(B_USER_SETTINGS_DIRECTORY, "MediaPlayer");
	fSettings->Load();

	_CreateMenu();
	_SetupWindow();
	_LoadSettings();

	// 30 Hz so the bar tracks short clips smoothly.
	fPositionRunner = new BMessageRunner(BMessenger(this),
		new BMessage(MSG_CONTROLLER_POSITION), 33333);
}


MainWin::~MainWin()
{
	delete fPositionRunner;
	delete fOpenPanel;
	_QuitInfoWin();
	delete fPlaylistWindow;
	delete fController;
	delete fSettings;
}


void
MainWin::_SetupWindow()
{
	fVideoView = new VideoView(BRect(0, 0, 360, 200), "video", B_FOLLOW_ALL);

	fLocationView = new LocationStringView("location", "");
	fLocationView->SetExplicitMinSize(BSize(250,
		be_plain_font->Size() + 8));
	fLocationView->Hide();

	fControllerView = new ControllerView(
		BRect(0, 200, 360, 300), fController, fPlaylist);
	fControllerView->SetVolume(fController->Volume());
	fControllerView->SetDisabledString(B_TRANSLATE("Drop files to play"));
	if (PeakView* peaks = fControllerView->GetPeakView())
		peaks->SetPeakNotificationWhat(BMediaPlayer::B_PLAYER_PEAKS);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0)
		.Add(fMenuBar)
		.Add(fVideoView, 1.0)
		.Add(fLocationView, 1.0)
		.Add(fControllerView);

	fController->SetVideoView(fVideoView);

	SetSizeLimits(kMinWidth, kMinHeight, 4000, 3000);
	if (!fSavedFrame.IsValid())
		ResizeTo(kMinWidth + 40, kMinHeight + 140);
	CenterOnScreen();
	fVideoView->MakeFocus(true);
	_UpdateTransport();
}


void
MainWin::_CreateMenu()
{
	fMenuBar = new BMenuBar("menu bar");

	BMenu* fileMenu = new BMenu(B_TRANSLATE("File"));
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Open…"),
		new BMessage(MSG_OPEN), 'O'));
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Open Location…"),
		new BMessage(MSG_OPEN_LOCATION), 'L'));
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Open MIDI…"),
		new BMessage(MSG_OPEN_MIDI), 'M'));
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Play Audio CD"),
		new BMessage(MSG_PLAY_CD), 'D'));
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("File Info"),
		new BMessage(MSG_FILE_INFO), 'I'));
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("New Player"),
		new BMessage(MSG_NEW_PLAYER), 'N'));
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Close"),
		new BMessage(B_CLOSE_REQUESTED), 'W'));
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(B_QUIT_REQUESTED), 'Q'));
	fMenuBar->AddItem(fileMenu);

	BMenu* playlistMenu = new BMenu(B_TRANSLATE("Playlist"));
	playlistMenu->AddItem(new BMenuItem(B_TRANSLATE("Show Playlist"),
		new BMessage(MSG_PLAYLIST_SHOW), 'P'));
	playlistMenu->AddItem(new BMenuItem(B_TRANSLATE("Add Files…"),
		new BMessage(MSG_PLAYLIST_ADD)));
	playlistMenu->AddItem(new BMenuItem(B_TRANSLATE("Clear"),
		new BMessage(MSG_PLAYLIST_CLEAR)));
	fLoopItem = new BMenuItem(B_TRANSLATE("Loop"),
		new BMessage(MSG_TOGGLE_LOOP));
	playlistMenu->AddItem(fLoopItem);
	fMenuBar->AddItem(playlistMenu);

	BMenu* videoMenu = new BMenu(B_TRANSLATE("Video"));
	videoMenu->AddItem(new BMenuItem(B_TRANSLATE("Full Screen"),
		new BMessage(MSG_TOGGLE_FULLSCREEN)));
	videoMenu->AddSeparatorItem();
	const float scales[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
	for (uint32 i = 0; i < sizeof(scales) / sizeof(scales[0]); i++) {
		BMessage* msg = new BMessage(MSG_VIDEO_SCALE);
		msg->AddFloat("scale", scales[i]);
		char label[16];
		snprintf(label, sizeof(label), "%d%%", (int)(scales[i] * 100));
		BMenuItem* item = new BMenuItem(B_TRANSLATE(label), msg);
		if (scales[i] == kDefaultScale)
			item->SetMarked(true);
		videoMenu->AddItem(item);
	}
	videoMenu->AddItem(new BMenuItem(B_TRANSLATE("Fit to Screen"),
		new BMessage(MSG_VIDEO_FIT)));
	videoMenu->AddSeparatorItem();
	videoMenu->AddItem(new BMenuItem(B_TRANSLATE("Zoom In"),
		new BMessage(MSG_ZOOM_IN), '+'));
	videoMenu->AddItem(new BMenuItem(B_TRANSLATE("Zoom Out"),
		new BMessage(MSG_ZOOM_OUT), '-'));
	fMenuBar->AddItem(videoMenu);

	BMenu* windowMenu = new BMenu(B_TRANSLATE("Window"));
	// Cmd+A stays free for select-all; Always on Top takes T for top.
	fAlwaysOnTopItem = new BMenuItem(B_TRANSLATE("Always on Top"),
		new BMessage(MSG_ALWAYS_ON_TOP), 'T');
	windowMenu->AddItem(fAlwaysOnTopItem);
	fHideInterfaceItem = new BMenuItem(B_TRANSLATE("Hide Interface"),
		new BMessage(MSG_HIDE_INTERFACE), 'H');
	windowMenu->AddItem(fHideInterfaceItem);
	fMenuBar->AddItem(windowMenu);

	BMenu* audioMenu = new BMenu(B_TRANSLATE("Audio"));
	audioMenu->AddItem(new BMenuItem(B_TRANSLATE("Mute"),
		new BMessage('mute')));
	fMenuBar->AddItem(audioMenu);
}


void
MainWin::_UpdateTitle(const char* name)
{
	if (name != NULL && name[0] != '\0') {
		BString title(name);
		title << " - " << B_TRANSLATE("MediaPlayer");
		SetTitle(title.String());
	} else
		SetTitle(B_TRANSLATE("MediaPlayer"));
}


float
MainWin::_ChromeHeight() const
{
	float menuH = fMenuBar != NULL ? fMenuBar->MinSize().height : 0;
	float stripH = fControllerView != NULL
		? fControllerView->GroupLayout()->MinSize().height : 0;
	return menuH + stripH;
}


void
MainWin::_SizeForVideo(uint32 width, uint32 height)
{
	if (width == 0 || height == 0 || fVideoView == NULL
			|| fFullScreen.IsFullscreen()) {
		return;
	}

	video_display_size display = compute_video_display_size(width, height,
		fVideoParNum, fVideoParDen, fVideoRotation);

	// HiDPI: the window works in logical units, video arrives in pixels.
	const float backing = fVideoView->Scale();
	if (backing > 1.0f) {
		display.width = (uint32)(display.width / backing + 0.5f);
		display.height = (uint32)(display.height / backing + 0.5f);
	}

	float targetW = display.width * fVideoScale;
	float targetH = display.height * fVideoScale;
	if (targetW < kMinWidth)
		targetW = kMinWidth;
	if (targetH < 40)
		targetH = 40;

	// Clamp to the screen the window is on, reserving the chrome.
	BRect screen = BScreen(this).Frame();
	video_display_size target = { (uint32)targetW, (uint32)targetH };
	const video_display_size clamped = clamp_video_display_size(target,
		screen.Width(), screen.Height() - _ChromeHeight());

	// Chrome comes from the layout, not constants, so HiDPI scaling holds.
	ResizeTo(clamped.width, clamped.height + _ChromeHeight());
	fLastFrame = Frame();
}


void
MainWin::_PollPosition()
{
	if (fControllerView == NULL || fController == NULL)
		return;
	bigtime_t duration = fController->Duration();
	if (duration <= 0)
		return;
	float position = (float)fController->Position() / (float)duration;
	fControllerView->SetPosition(position, fController->Position(), duration);
}


void
MainWin::_UpdateTransport()
{
	if (fControllerView == NULL || fController == NULL)
		return;

	uint32 state = PLAYBACK_STATE_STOPPED;
	switch (fController->State()) {
		case BMediaPlayer::B_PLAYER_PLAYING:
			state = PLAYBACK_STATE_PLAYING;
			break;
		case BMediaPlayer::B_PLAYER_PAUSED:
			state = PLAYBACK_STATE_PAUSED;
			break;
		default:
			state = PLAYBACK_STATE_STOPPED;
			break;
	}
	fControllerView->SetPlaybackState(state);
	fControllerView->SetAudioEnabled(true);
	fControllerView->SetMuted(fController->IsMuted());
	fControllerView->SetVolume(fController->Volume());

	bool hasItem = fPlaylist->CurrentItem() != NULL;
	uint32 buttons = VOLUME_ENABLED;
	if (hasItem) {
		buttons |= PLAYBACK_ENABLED | SEEK_BACK_ENABLED | SEEK_FORWARD_ENABLED;
		int32 count = fPlaylist->CountItems();
		int32 index = fPlaylist->CurrentItemIndex();
		if (index > 0)
			buttons |= SKIP_BACK_ENABLED;
		if (index < count - 1)
			buttons |= SKIP_FORWARD_ENABLED;
		if (fController->Duration() > 0)
			buttons |= SEEK_ENABLED;
	}
	fControllerView->SetEnabled(buttons);

	bigtime_t position = fController->Position();
	bigtime_t duration = fController->Duration();
	if (duration > 0) {
		float value = (float)position / (float)duration;
		fControllerView->SetPosition(value, position, duration);
	}
}


void
MainWin::_UpdateLocation()
{
	if (fLocationView == NULL)
		return;
	PlaylistItem* item = fPlaylist->CurrentItem();
	BString text;
	if (item != NULL) {
		BPath path(&item->Ref());
		// A file: URL lets the location line act as a reveal-in-Tracker link.
		if (path.InitCheck() == B_OK && path.Path()[0] == '/')
			text << "file://" << path.Path();
		else
			text = path.Path();
	}
	fLocationView->SetText(text.String());
	fLocationView->CheckAndSetStyleForLink();
}


void
MainWin::_UpdateVideoMode()
{
	bool hasVideo = fController != NULL && fController->HasVideo();
	if (fVideoView != NULL) {
		if (hasVideo)
			fVideoView->Show();
		else
			fVideoView->Hide();
	}
	if (fLocationView != NULL) {
		if (hasVideo)
			fLocationView->Hide();
		else
			fLocationView->Show();
	}

	if (fFullScreen.IsFullscreen())
		return;

	if (hasVideo) {
		SetSizeLimits(kMinWidth, kMinHeight, 4000, 3000);
		_SizeForVideo(fController->VideoWidth(), fController->VideoHeight());
	} else {
		SetSizeLimits(kAudioMinWidth, _ChromeHeight() + 40, 4000, 3000);
		float audioW = kAudioMinWidth;
		if (fSettings != NULL)
			audioW = fSettings->GetValue("audio_width", (float)kAudioMinWidth);
		if (audioW < kAudioMinWidth)
			audioW = kAudioMinWidth;
		ResizeTo(audioW, _ChromeHeight() + 60);
	}
	_UpdateLocation();
}


void
MainWin::_SeekRelative(bigtime_t delta)
{
	if (fController == NULL)
		return;
	bigtime_t position = fController->Position();
	// At the ends the arrow keys skip item instead of seeking past it.
	if (delta < 0 && position <= 0) {
		fController->SkipBackward();
		return;
	}
	bigtime_t duration = fController->Duration();
	if (delta > 0 && duration > 0 && position >= duration - 1) {
		fController->SkipForward();
		return;
	}
	fController->SeekBy(delta);
	_PollPosition();
}


bool
MainWin::_ShouldHandleKey(BHandler* handler)
{
	if (handler == NULL || handler == this)
		return true;
	BView* view = dynamic_cast<BView*>(handler);
	if (view == NULL)
		return false;
	for (BView* v = view; v != NULL; v = v->Parent()) {
		if (dynamic_cast<BTextView*>(v) != NULL)
			return false;
		if (v == fVideoView || v == fControllerView)
			return true;
	}
	return false;
}


void
MainWin::_ToggleFullscreen()
{
	_ApplyFullScreenResult(fFullScreen.Toggle(
		fVideoView != NULL && fVideoView->HasVideo(), system_time()));
}


void
MainWin::_ApplyFullScreenResult(const FullScreenState::Result& result)
{
	if (result.enter)
		_EnterFullscreen();
	else if (result.exit)
		_ExitFullscreen();
	_SetCursorHidden(result.cursorHidden);
}


void
MainWin::_SetCursorHidden(bool hidden)
{
	if (hidden == fCursorHidden)
		return;
	fCursorHidden = hidden;
	if (hidden)
		be_app->HideCursor();
	else
		be_app->ShowCursor();
}


void
MainWin::_EnterFullscreen()
{
	if (fVideoView == NULL || !fVideoView->HasVideo())
		return;

	fSavedFrame = Frame();
	fStripHeight = fControllerView->Bounds().Height();

	BGroupLayout* layout = dynamic_cast<BGroupLayout*>(GetLayout());
	if (layout != NULL) {
		// Remember the slot so leaving fullscreen restores the order.
		fStripLayoutIndex = layout->IndexOfView(fControllerView);
		layout->RemoveView(fControllerView);
	}
	fVideoView->AddChild(fControllerView);
	fControllerView->SetResizingMode(B_FOLLOW_NONE);
	fControllerView->SetSymbolScale(1.5f);
	fStripHeight = fControllerView->Bounds().Height();

	fMenuBar->Hide();
	BRect screen = BScreen(this).Frame();
	MoveTo(screen.left, screen.top);
	ResizeTo(screen.Width(), screen.Height());

	fControlY = fVideoView->Bounds().bottom - fStripHeight + 1;
	_PositionControls();
	fVideoView->SetFullscreenMode(true);
}


void
MainWin::_ExitFullscreen()
{
	fVideoView->SetFullscreenMode(false);

	fVideoView->RemoveChild(fControllerView);
	fControllerView->SetSymbolScale(1.0f);
	BGroupLayout* layout = dynamic_cast<BGroupLayout*>(GetLayout());
	if (layout != NULL) {
		if (fStripLayoutIndex >= 0)
			layout->AddView(fStripLayoutIndex, fControllerView);
		else
			layout->AddView(fControllerView);
	}
	fStripLayoutIndex = -1;
	fMenuBar->Show();

	if (fSavedFrame.IsValid()) {
		MoveTo(fSavedFrame.LeftTop());
		ResizeTo(fSavedFrame.Width(), fSavedFrame.Height());
		fLastFrame = Frame();
	}
	BLayout* top = GetLayout();
	if (top != NULL)
		top->InvalidateLayout();
}


void
MainWin::_PositionControls()
{
	if (!fFullScreen.IsFullscreen() || fVideoView == NULL
			|| fControllerView == NULL) {
		return;
	}
	BRect bounds = fVideoView->Bounds();
	fControllerView->MoveTo(0, fControlY);
	fControllerView->ResizeTo(bounds.Width(), fStripHeight);
	fVideoView->Invalidate();
}


void
MainWin::_ToggleHideInterface()
{
	fHideInterface = !fHideInterface;
	if (fFullScreen.IsFullscreen())
		return;
	if (fMenuBar != NULL) {
		if (fHideInterface)
			fMenuBar->Hide();
		else
			fMenuBar->Show();
	}
	if (fControllerView != NULL) {
		if (fHideInterface)
			fControllerView->Hide();
		else
			fControllerView->Show();
	}
	if (fHideInterfaceItem != NULL)
		fHideInterfaceItem->SetMarked(fHideInterface);
	BLayout* layout = GetLayout();
	if (layout != NULL)
		layout->InvalidateLayout();
}


void
MainWin::_SetAlwaysOnTop(bool on)
{
	fAlwaysOnTop = on;
	SetFeel(on ? B_FLOATING_ALL_WINDOW_FEEL : B_NORMAL_WINDOW_FEEL);
	if (fAlwaysOnTopItem != NULL)
		fAlwaysOnTopItem->SetMarked(on);
}


void
MainWin::_SetLoop(bool on)
{
	fLoop = on;
	if (fController != NULL)
		fController->SetLoop(on);
	if (fLoopItem != NULL)
		fLoopItem->SetMarked(on);
}


void
MainWin::_ShowInfo()
{
	PlaylistItem* item = fPlaylist->CurrentItem();
	if (item == NULL)
		return;
	// A closed info window frees itself, so only the messenger may be
	// trusted to tell whether one is still alive.
	if (fInfoMessenger.LockTarget()) {
		BLooper* looper = NULL;
		fInfoMessenger.Target(&looper);
		BWindow* window = dynamic_cast<BWindow*>(looper);
		if (window != NULL)
			window->Activate();
		if (looper != NULL)
			looper->Unlock();
		return;
	}
	BRect frame(100, 100, 460, 360);
	fInfoMessenger = BMessenger(new InfoWin(frame, fController, item->Ref()));
}


void
MainWin::_QuitInfoWin()
{
	// Quit() deletes the window; deleting it here would race its thread.
	if (fInfoMessenger.LockTarget()) {
		BLooper* looper = NULL;
		fInfoMessenger.Target(&looper);
		BWindow* window = dynamic_cast<BWindow*>(looper);
		if (window != NULL)
			window->Quit();
		else if (looper != NULL)
			looper->Unlock();
	}
	fInfoMessenger = BMessenger();
}


void
MainWin::_Zoom(float factor)
{
	fVideoScale *= factor;
	if (fVideoScale < 0.1f)
		fVideoScale = 0.1f;
	if (fVideoScale > 8.0f)
		fVideoScale = 8.0f;
	if (fController->HasVideo())
		_SizeForVideo(fController->VideoWidth(), fController->VideoHeight());
}


void
MainWin::_FitToScreen()
{
	if (fController == NULL || !fController->HasVideo() || fVideoView == NULL)
		return;
	const video_display_size display = compute_video_display_size(
		fController->VideoWidth(), fController->VideoHeight(),
		fController->VideoPixelWidthAspect(),
		fController->VideoPixelHeightAspect(), fController->VideoRotation());
	BRect screen = BScreen(this).Frame();
	const video_display_size fit = clamp_video_display_size(display,
		screen.Width(), screen.Height() - _ChromeHeight());
	if (display.width == 0)
		return;
	const float backing = fVideoView->Scale();
	fVideoScale = (float)fit.width * (backing > 1.0f ? backing : 1.0f)
		/ (float)display.width;
	if (fVideoScale < 0.1f)
		fVideoScale = 0.1f;
	if (fVideoScale > 8.0f)
		fVideoScale = 8.0f;
	_SizeForVideo(fController->VideoWidth(), fController->VideoHeight());
}


void
MainWin::_LoadSettings()
{
	if (fSettings == NULL)
		return;
	fSavedFrame = fSettings->GetValue("window_frame", BRect());
	if (fSavedFrame.IsValid()) {
		MoveTo(fSavedFrame.LeftTop());
		ResizeTo(fSavedFrame.Width(), fSavedFrame.Height());
		fLastFrame = Frame();
	} else
		CenterOnScreen();
	fVideoScale = fSettings->GetValue("video_scale", kDefaultScale);
	_SetAlwaysOnTop(fSettings->GetValue("always_on_top", false));
	_SetLoop(fSettings->GetValue("loop", false));
}


void
MainWin::_SaveSettings()
{
	if (fSettings == NULL)
		return;
	BRect frame = Frame();
	if (fFullScreen.IsFullscreen() && fSavedFrame.IsValid())
		frame = fSavedFrame;
	fSettings->SetValue("window_frame", frame);
	fSettings->SetValue("video_scale", fVideoScale);
	fSettings->SetValue("always_on_top", fAlwaysOnTop);
	fSettings->SetValue("loop", fLoop);
	if (fController != NULL && !fController->HasVideo())
		fSettings->SetValue("audio_width", Frame().Width());
	fSettings->Save();
}


void
MainWin::_OpenRefs(BMessage* message)
{
	if (fPlaylist == NULL || fController == NULL)
		return;

	type_code type;
	int32 count = 0;
	if (message->GetInfo("refs", &type, &count) != B_OK)
		return;

	uint32 modifiers = 0;
	message->FindInt32("modifiers", (int32*)&modifiers);
	bool append = (modifiers & B_SHIFT_KEY) != 0;
	if (!append)
		fPlaylist->Clear();

	bool play = fPlaylist->CountItems() == 0;
	int32 firstIndex = fPlaylist->CountItems();
	for (int32 i = 0; i < count; i++) {
		entry_ref ref;
		if (message->FindRef("refs", i, &ref) != B_OK)
			continue;
		BString name(ref.name);
		name.ToLower();
		fPlaylist->AddItem(ref);
	}

	if (play && fPlaylist->CountItems() > 0)
		fPlaylist->SetCurrentItemIndex(firstIndex, true);
	_UpdateTransport();
	_UpdateVideoMode();
	_UpdateTitle(fPlaylist->CurrentItem() != NULL
		? fPlaylist->CurrentItem()->Name().String() : NULL);
}


void
MainWin::_OpenLocation()
{
	BAlert* alert = new BAlert(B_TRANSLATE("Open Location"),
		B_TRANSLATE("Use Play Audio CD for cdda:// discs. "
			"Other locations open through BMediaFile once provided."),
		B_TRANSLATE("OK"));
	alert->Go();
}


void
MainWin::_OpenMidiRef(const entry_ref& ref)
{
	if (fController == NULL)
		return;
	fController->OpenMidi(ref);
	_UpdateTransport();
}


void
MainWin::_PlayCd()
{
	if (fController == NULL)
		return;
	fController->OpenCd(1);
	_UpdateTransport();
	_UpdateVideoMode();
}


void
MainWin::_ShowPlaylist()
{
	if (fPlaylistWindow == NULL) {
		BRect frame(80, 80, 420, 380);
		fPlaylistWindow = new PlaylistWindow(frame, fController, fPlaylist);
	}
	if (fPlaylistWindow->IsHidden())
		fPlaylistWindow->Show();
	else
		fPlaylistWindow->Activate();
}


void
MainWin::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_REFS_RECEIVED:
			_OpenRefs(message);
			break;

		case MSG_OPEN:
		case MSG_PLAYLIST_ADD:
		case MSG_OPEN_MIDI:
		{
			if (fOpenPanel != NULL) {
				delete fOpenPanel;
				fOpenPanel = NULL;
			}
			fOpenPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this),
				NULL, B_FILE_NODE, false, message, NULL, true);
			fOpenPanel->Window()->Show();
			break;
		}

		case MSG_OPEN_LOCATION:
			_OpenLocation();
			break;

		case MSG_PLAY_CD:
			_PlayCd();
			break;

		case MSG_PLAYLIST_SHOW:
			_ShowPlaylist();
			break;

		case MSG_PLAYLIST_CLEAR:
			if (fPlaylist != NULL) {
				fPlaylist->Clear();
				_UpdateTransport();
				_UpdateLocation();
			}
			break;

		case MSG_PLAYLIST_ITEMS_CHANGED:
		case MSG_PLAYLIST_CURRENT_CHANGED:
			_UpdateTransport();
			_UpdateLocation();
			break;

		case MSG_CONTROLLER_POSITION:
			_PollPosition();
			break;

		case BMediaPlayer::B_PLAYER_PEAKS:
			if (PeakView* peaks = fControllerView->GetPeakView())
				peaks->MessageReceived(message);
			break;

		case MSG_CONTROLLER_STATE:
		case MSG_CONTROLLER_DURATION:
			if (fController != NULL)
				fControllerView->SetAudioChannelCount(
					fController->AudioChannels() > 0
						? fController->AudioChannels() : 2);
			_UpdateTransport();
			_PollPosition();
			break;

		case MSG_CONTROLLER_EOS:
		case MSG_CONTROLLER_FINISHED:
			if (fController != NULL)
				fController->HandleEOS();
			_UpdateTransport();
			break;

		case MSG_CONTROLLER_ERROR:
		{
			const char* text = "Playback error.";
			message->FindString("message", &text);
			BAlert* alert = new BAlert(B_TRANSLATE("MediaPlayer"),
				text, B_TRANSLATE("OK"));
			alert->Go();
			_UpdateTransport();
			break;
		}

		case MSG_CONTROLLER_VIDEO:
		{
			bool hasVideo = false;
			int32 width = 0;
			int32 height = 0;
			int32 parNum = 0;
			int32 parDen = 0;
			int32 rotation = 0;
			message->FindBool("has_video", &hasVideo);
			message->FindInt32("width", &width);
			message->FindInt32("height", &height);
			message->FindInt32("par_num", &parNum);
			message->FindInt32("par_den", &parDen);
			message->FindInt32("rotation", &rotation);
			fVideoParNum = (uint32)parNum;
			fVideoParDen = (uint32)parDen;
			fVideoRotation = (uint32)rotation;
			if (fVideoView != NULL) {
				fVideoView->SetVideoFormat((uint32)width, (uint32)height,
					fVideoParNum, fVideoParDen, fVideoRotation);
			}
			_UpdateVideoMode();
			break;
		}

		case MSG_CONTROLLER_ITEM:
		{
			const char* name = NULL;
			message->FindString("name", &name);
			_UpdateTitle(name);
			_UpdateLocation();
			break;
		}

		case MSG_TOGGLE_FULLSCREEN:
			_ToggleFullscreen();
			break;

		case MSG_REVEAL_CONTROLS:
			fFullScreen.MouseMoved(system_time());
			_SetCursorHidden(fFullScreen.CursorHidden());
			break;

		case MSG_FULLSCREEN_TICK:
		{
			if (!fFullScreen.IsFullscreen() || fVideoView == NULL)
				break;
			_SetCursorHidden(fFullScreen.Tick(system_time()).cursorHidden);
			const bool controlsVisible = fFullScreen.ControlsVisible();
			float restY = fVideoView->Bounds().bottom - fStripHeight + 1;
			float hiddenY = fVideoView->Bounds().bottom + 1;
			float step = fStripHeight > 6 ? fStripHeight / 6 : 6;
			if (controlsVisible && fControlY > restY) {
				fControlY -= step;
				if (fControlY < restY)
					fControlY = restY;
				_PositionControls();
			} else if (!controlsVisible && fControlY < hiddenY) {
				fControlY += step;
				if (fControlY > hiddenY)
					fControlY = hiddenY;
				_PositionControls();
			}
			break;
		}

		case MSG_TOGGLE_LOOP:
			_SetLoop(!fLoop);
			break;

		case MSG_FILE_INFO:
			_ShowInfo();
			break;

		case MSG_NEW_PLAYER:
		{
			BRect frame = Frame().OffsetByCopy(20, 20);
			MainWin* win = new MainWin(frame);
			win->Show();
			break;
		}

		case MSG_ALWAYS_ON_TOP:
			_SetAlwaysOnTop(!fAlwaysOnTop);
			break;

		case MSG_HIDE_INTERFACE:
			_ToggleHideInterface();
			break;

		case MSG_VIDEO_SCALE:
		{
			float scale = kDefaultScale;
			if (message->FindFloat("scale", &scale) == B_OK) {
				fVideoScale = scale;
				if (fController->HasVideo())
					_SizeForVideo(fController->VideoWidth(),
						fController->VideoHeight());
			}
			break;
		}

		case MSG_VIDEO_FIT:
			_FitToScreen();
			break;

		case MSG_ZOOM_IN:
			_Zoom(1.1f);
			break;

		case MSG_ZOOM_OUT:
			_Zoom(1.0f / 1.1f);
			break;

		case MSG_CONTEXT_MENU:
		{
			BPoint where;
			fVideoView->GetMouse(&where, NULL, false);
			fVideoView->ConvertToScreen(&where);
			BPopUpMenu* menu = new BPopUpMenu("context");
			menu->AddItem(new BMenuItem(B_TRANSLATE("Full Screen"),
				new BMessage(MSG_TOGGLE_FULLSCREEN)));
			menu->AddItem(new BMenuItem(B_TRANSLATE("Hide Interface"),
				new BMessage(MSG_HIDE_INTERFACE)));
			menu->AddItem(new BMenuItem(B_TRANSLATE("Always on Top"),
				new BMessage(MSG_ALWAYS_ON_TOP)));
			menu->AddSeparatorItem();
			menu->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
				new BMessage(B_QUIT_REQUESTED)));
			menu->SetTargetForItems(this);
			menu->Go(where, true, true, true);
			delete menu;
			break;
		}

		case 'mute':
			if (fController != NULL) {
				fController->SetMute(!fController->IsMuted());
				_UpdateTransport();
			}
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
MainWin::DispatchMessage(BMessage* message, BHandler* handler)
{
	if (message->what == B_KEY_DOWN && _ShouldHandleKey(handler)) {
		int32 key = 0;
		int32 rawChar = 0;
		int32 modifiers = 0;
		message->FindInt32("key", &key);
		message->FindInt32("raw_char", &rawChar);
		message->FindInt32("modifiers", &modifiers);
		bool cmd = (modifiers & B_COMMAND_KEY) != 0;
		bool shift = (modifiers & B_SHIFT_KEY) != 0;

		if (cmd) {
			switch (rawChar) {
				case B_ENTER:
					_ToggleFullscreen();
					return;
				case B_LEFT_ARROW:
					fController->SkipBackward();
					return;
				case B_RIGHT_ARROW:
					fController->SkipForward();
					return;
				default:
					break;
			}
			BWindow::DispatchMessage(message, handler);
			return;
		}

		// Printable and space keys are matched on raw_char so non-US
		// layouts keep working; navigation keys use the key code.
		switch (rawChar) {
			case B_SPACE:
				fController->TogglePlaying();
				_UpdateTransport();
				return;
			case 'm':
				fController->SetMute(!fController->IsMuted());
				_UpdateTransport();
				return;
			case 'f':
				_ToggleFullscreen();
				return;
			case 'z':
				fController->SkipBackward();
				return;
			case 'x':
				fController->Play();
				return;
			case 'c':
				fController->Pause();
				return;
			case 'v':
				fController->Stop();
				return;
			case 'b':
				fController->SkipForward();
				return;
			case B_HOME:
				fController->SeekTo(0);
				_PollPosition();
				return;
			case B_END:
				if (fController->Duration() > 0) {
					fController->SeekTo(fController->Duration());
					_PollPosition();
				}
				return;
			case '+':
			case '=':
				_Zoom(1.1f);
				return;
			case '-':
				_Zoom(1.0f / 1.1f);
				return;
			default:
				break;
		}

		switch (key) {
			case B_UP_ARROW:
				fController->VolumeStep(true);
				_UpdateTransport();
				return;
			case B_DOWN_ARROW:
				fController->VolumeStep(false);
				_UpdateTransport();
				return;
			case B_LEFT_ARROW:
				_SeekRelative(shift ? -30 * 1000000 : -5 * 1000000);
				return;
			case B_RIGHT_ARROW:
				_SeekRelative(shift ? 30 * 1000000 : 5 * 1000000);
				return;
			case B_PAGE_UP:
				fController->SkipBackward();
				return;
			case B_PAGE_DOWN:
				fController->SkipForward();
				return;
			case B_ENTER:
			case B_TAB:
				_ToggleFullscreen();
				return;
			case B_ESCAPE:
				_ApplyFullScreenResult(fFullScreen.Escape(system_time()));
				return;
			default:
				break;
		}
	}
	BWindow::DispatchMessage(message, handler);
}


void
MainWin::FrameResized(float newWidth, float newHeight)
{
	BWindow::FrameResized(newWidth, newHeight);
	// A size we set ourselves is already aspect-correct; anything else
	// was a user drag and gets snapped.
	if (Frame().Width() != fLastFrame.Width()
			|| Frame().Height() != fLastFrame.Height()) {
		_KeepAspectOnResize();
	}
	fLastFrame = Frame();
	if (fFullScreen.IsFullscreen())
		_PositionControls();
	_UpdateTransport();
}


// Window drags keep the video aspect; hold Shift while resizing for a
// free aspect.
void
MainWin::_KeepAspectOnResize()
{
	if (fFullScreen.IsFullscreen() || fController == NULL
			|| !fController->HasVideo() || fVideoView == NULL) {
		return;
	}
	if ((modifiers() & B_SHIFT_KEY) != 0)
		return;

	const video_display_size display = compute_video_display_size(
		fController->VideoWidth(), fController->VideoHeight(),
		fVideoParNum, fVideoParDen, fVideoRotation);
	if (display.width == 0 || display.height == 0)
		return;

	const float aspect = (float)display.width / (float)display.height;
	const float chrome = _ChromeHeight();
	const float contentW = Frame().Width();
	const float contentH = Frame().Height() - chrome;
	const float oldContentW = fLastFrame.Width();
	const float oldContentH = fLastFrame.Height() - chrome;
	if (contentW < 1 || contentH < 1)
		return;

	// Whichever edge moved furthest leads; the other follows the aspect.
	float width = contentW;
	float height = contentW / aspect;
	if (fabs(contentH - oldContentH) > fabs(contentW - oldContentW)) {
		width = contentH * aspect;
		height = contentH;
	}
	if (height < 40) {
		height = 40;
		width = height * aspect;
	}
	ResizeTo(width, height + chrome);
}


bool
MainWin::QuitRequested()
{
	_SaveSettings();
	if (fPlaylistWindow != NULL && !fPlaylistWindow->IsHidden())
		fPlaylistWindow->Quit();
	// The main window is the app: closing it quits the player.
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}
