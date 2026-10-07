/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MainWin.h"

#include <Alert.h>
#include <Autolock.h>
#include <Catalog.h>
#include <Entry.h>
#include <FilePanel.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Path.h>
#include <String.h>

#include "Controller.h"
#include "ControllerView.h"
#include "MediaMessages.h"
#include "PlaybackState.h"
#include "Playlist.h"
#include "PlaylistItem.h"
#include "PlaylistWindow.h"
#include "VideoView.h"

#include <media2/MediaPlayer.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MediaPlayer"


static const float kMinWidth = 360;
static const float kMinHeight = 280;


MainWin::MainWin(BRect frame)
	:
	BWindow(frame, B_TRANSLATE("MediaPlayer"), B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fController(NULL),
	fPlaylist(NULL),
	fVideoView(NULL),
	fControllerView(NULL),
	fPlaylistWindow(NULL),
	fMenuBar(NULL),
	fOpenPanel(NULL),
	fPositionRunner(NULL),
	fMuted(false)
{
	fPlaylist = new Playlist();
	fController = new Controller(BMessenger(this));

	_CreateMenu();
	_SetupWindow();

	fPositionRunner = new BMessageRunner(BMessenger(this),
		new BMessage(MSG_CONTROLLER_POSITION), 100000);
}


MainWin::~MainWin()
{
	delete fPositionRunner;
	delete fOpenPanel;
	delete fPlaylistWindow;
	delete fController;
	delete fPlaylist;
}


void
MainWin::_SetupWindow()
{
	fVideoView = new VideoView(BRect(0, 0, 360, 200), "video", B_FOLLOW_ALL);

	fControllerView = new ControllerView(
		BRect(0, 200, 360, 300), fController, fPlaylist);
	fControllerView->SetVolume(fController->Volume());

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0)
		.Add(fMenuBar)
		.Add(fVideoView, 1.0)
		.Add(fControllerView);

	SetSizeLimits(kMinWidth, kMinHeight, 4000, 3000);
	ResizeTo(kMinWidth + 40, kMinHeight + 140);
	CenterOnScreen();
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
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Play Audio CD"),
		new BMessage(MSG_PLAY_CD), 'D'));
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(B_QUIT_REQUESTED), 'Q'));
	fMenuBar->AddItem(fileMenu);

	BMenu* playlistMenu = new BMenu(B_TRANSLATE("Playlist"));
	playlistMenu->AddItem(new BMenuItem(B_TRANSLATE("Show Playlist"),
		new BMessage(MSG_PLAYLIST_SHOW), 'P'));
	playlistMenu->AddItem(new BMenuItem(B_TRANSLATE("Add Files…"),
		new BMessage(MSG_PLAYLIST_ADD), 'A'));
	playlistMenu->AddItem(new BMenuItem(B_TRANSLATE("Clear"),
		new BMessage(MSG_PLAYLIST_CLEAR)));
	fMenuBar->AddItem(playlistMenu);
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


void
MainWin::_SizeForVideo(uint32 width, uint32 height)
{
	if (width == 0 || height == 0 || fVideoView == NULL)
		return;

	float viewWidth = fVideoView->Bounds().Width();
	float viewHeight = fVideoView->Bounds().Height();
	if (viewWidth < 1 || viewHeight < 1)
		return;

	float scale = 1.0f;
	if (width > (uint32)viewWidth || height > (uint32)viewHeight) {
		float sx = viewWidth / (float)width;
		float sy = viewHeight / (float)height;
		scale = sx < sy ? sx : sy;
	}
	float targetW = width * scale;
	float targetH = height * scale;
	if (targetW < kMinWidth)
		targetW = kMinWidth;
	if (targetH < 40)
		targetH = 40;

	float menuHeight = 22;
	float transportHeight = 100;
	float frameW = targetW;
	float frameH = menuHeight + targetH + transportHeight;
	if (frameW < Bounds().Width())
		frameW = Bounds().Width();
	if (frameH < Bounds().Height())
		frameH = Bounds().Height();
	ResizeTo(frameW, frameH);
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
	fControllerView->SetMuted(fMuted);
	fControllerView->SetVolume(fController->Volume());

	bigtime_t position = fController->Position();
	bigtime_t duration = fController->Duration();
	if (duration > 0) {
		float value = (float)position / (float)duration;
		fControllerView->SetPosition(value, position, duration);
	}
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

	bool play = fPlaylist->CountItems() == 0;
	int32 firstIndex = fPlaylist->CountItems();
	for (int32 i = 0; i < count; i++) {
		entry_ref ref;
		if (message->FindRef("refs", i, &ref) != B_OK)
			continue;
		BString name(ref.name);
		name.ToLower();
		fPlaylist->AddItem(ref);
		if ((name.EndsWith(".mid") || name.EndsWith(".midi"))
				&& (play || i == 0))
			_OpenMidiRef(ref);
	}

	if (play && fPlaylist->CountItems() > 0)
		fPlaylist->SetCurrentItemIndex(firstIndex, true);
	_UpdateTransport();
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
			if (fPlaylist != NULL)
				fPlaylist->Clear();
			break;

		case MSG_PLAYLIST_ITEMS_CHANGED:
		case MSG_PLAYLIST_CURRENT_CHANGED:
			_UpdateTransport();
			break;

		case MSG_CONTROLLER_POSITION:
			_PollPosition();
			break;

		case MSG_CONTROLLER_STATE:
		case MSG_CONTROLLER_DURATION:
			_UpdateTransport();
			_PollPosition();
			break;

		case MSG_CONTROLLER_EOS:
		case MSG_CONTROLLER_FINISHED:
			if (fPlaylist != NULL && fPlaylist->CountItems() > 1)
				fController->SkipForward();
			else
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
			message->FindBool("has_video", &hasVideo);
			message->FindInt32("width", &width);
			message->FindInt32("height", &height);
			if (fVideoView != NULL)
				fVideoView->SetVideoSize((uint32)width, (uint32)height);
			if (hasVideo)
				_SizeForVideo((uint32)width, (uint32)height);
			break;
		}

		case MSG_CONTROLLER_ITEM:
		{
			const char* name = NULL;
			message->FindString("name", &name);
			_UpdateTitle(name);
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
MainWin::DispatchMessage(BMessage* message, BHandler* handler)
{
	if (message->what == B_KEY_DOWN) {
		int32 key = 0;
		int32 modifiers = 0;
		if (message->FindInt32("key", &key) == B_OK
				&& message->FindInt32("modifiers", &modifiers) == B_OK) {
			// Space toggles play/pause, matching the legacy player.
			if (key == 0x20 && !(modifiers & B_COMMAND_KEY)) {
				fController->TogglePlaying();
				_UpdateTransport();
				return;
			}
			if (key == B_LEFT_ARROW) {
				fController->Rewind();
				_PollPosition();
				return;
			}
			if (key == B_RIGHT_ARROW) {
				fController->Forward();
				_PollPosition();
				return;
			}
		}
	}
	BWindow::DispatchMessage(message, handler);
}


void
MainWin::FrameResized(float newWidth, float newHeight)
{
	BWindow::FrameResized(newWidth, newHeight);
	_UpdateTransport();
}


bool
MainWin::QuitRequested()
{
	if (fPlaylistWindow != NULL && !fPlaylistWindow->IsHidden())
		fPlaylistWindow->Quit();
	return true;
}
