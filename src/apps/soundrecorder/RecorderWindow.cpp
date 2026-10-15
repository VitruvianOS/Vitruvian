/*
 * Copyright 2014, Dario Casalinuovo. All rights reserved.
 * Copyright 2005, Jérôme Duval. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Inspired by SoundCapture from Be newsletter (Media Kit Basics:
 *	Consumers and Producers)
 */

#include <Application.h>
#include <Alert.h>
#include <Debug.h>
#include <Screen.h>
#include <Button.h>
#include <CheckBox.h>
#include <TextControl.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MenuField.h>
#include <PopUpMenu.h>
#include <Box.h>
#include <ScrollView.h>
#include <Beep.h>
#include <StringView.h>
#include <String.h>
#include <Slider.h>
#include <Message.h>
#include <Messenger.h>
#include <Roster.h>

#include <Path.h>
#include <FindDirectory.h>
#include <MimeType.h>
#include <Node.h>

#include <assert.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#include <media2/MediaGraph.h>
#include <media2/MediaFormat.h>

#include "RecorderWindow.h"
#include "FileUtils.h"

#if ! NDEBUG
#define FPRINTF(args) fprintf args
#else
#define FPRINTF(args)
#endif

#define DEATH FPRINTF
#define CONNECT FPRINTF
#define WINDOW FPRINTF

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "RecorderWindow"


static const char* _ExtensionForEncoding(int32 encoding);


// default window positioning
static const float MIN_WIDTH = 400.0f;
static const float MIN_HEIGHT = 175.0f;
static const float XPOS = 100.0f;
static const float YPOS = 200.0f;


RecorderWindow::RecorderWindow()
	:
	BWindow(BRect(XPOS, YPOS, XPOS + MIN_WIDTH, YPOS + MIN_HEIGHT),
		B_TRANSLATE_SYSTEM_NAME("SoundRecorder"), B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_NOT_V_RESIZABLE | B_NOT_ZOOMABLE),
	fPlayer(NULL),
	fSoundList(NULL),
	fPlayFile(NULL),
	fPlayTrack(NULL),
	fPlayFrames(0),
	fLooping(false),
	fSavePanel(NULL),
	fOpenPanel(NULL),
	fFolderPanel(NULL),
	fInitCheck(B_OK)
{
	fRecordButton = NULL;
	fPlayButton = NULL;
	fStopButton = NULL;
	fSaveButton = NULL;
	fLoopButton = NULL;
	fRecorder = NULL;
	fRecording = false;
	fRecFrames = 0;
	fLastReport = 0;
	fBusy = false;
	fTempCount = -1;
	fButtonState = btnPaused;
	fInputDeviceId = 0;
	fHasConnectedSource = false;
	fMenuBar = NULL;
	fInputMenu = NULL;
	fFormatMenu = NULL;
	fRateMenu = NULL;
	fChannelsMenu = NULL;
	fQualityMenu = NULL;
	fAutoNameMenu = NULL;
	fWav16Item = NULL;
	fWav24Item = NULL;
	fWav32Item = NULL;
	fFlacItem = NULL;
	fOggItem = NULL;
	fOpusItem = NULL;
	fMp3Item = NULL;
	fRateDeviceItem = NULL;
	fRate44Item = NULL;
	fRate48Item = NULL;
	fRate96Item = NULL;
	fMonoItem = NULL;
	fStereoItem = NULL;
	fQualLowItem = NULL;
	fQualMedItem = NULL;
	fQualHighItem = NULL;
	fPatternDateItem = NULL;
	fPatternTimeItem = NULL;
	fPatternDateTimeItem = NULL;
	fScopeItem = NULL;
	fMeterItem = NULL;
	fTimeHmsItem = NULL;
	fTimeSamplesItem = NULL;
	fElapsedView = NULL;
	fMenuHeight = 0;
	fEncoding = B_CODEC_TYPE_WAV;
	fSampleFormat = BMediaRecorder::B_SAMPLE_FORMAT_S16;
	fRecordRate = 0;
	fRecordChannels = 2;
	fQuality = 0.5f;
	fAutoName = 0;
	fShowScope = true;
	fShowMeter = true;
	fTimeDisplay = 0;

	_LoadSettings();

	CalcSizes(MIN_WIDTH, MIN_HEIGHT);

	fInitCheck = InitWindow();
	if (fInitCheck != B_OK) {
		if (fInitCheck == B_NAME_NOT_FOUND)
			ErrorAlert(B_TRANSLATE("Cannot find default audio hardware"),
				fInitCheck);
		else
			ErrorAlert(B_TRANSLATE("Cannot connect to media server"),
				fInitCheck);
		PostMessage(B_QUIT_REQUESTED);
	} else
		Show();
}


RecorderWindow::~RecorderWindow()
{
	_JoinWorker();
	delete fRecorder;

	delete fPlayer;

	if (fPlayTrack && fPlayFile)
		fPlayFile->ReleaseTrack(fPlayTrack);

	if (fPlayFile)
		delete fPlayFile;
	fPlayTrack = NULL;
	fPlayFile = NULL;

	//	Clean up items in list view.
	if (fSoundList) {
		fSoundList->DeselectAll();
		for (int i = 0; i < fSoundList->CountItems(); i++) {
			WINDOW((stderr, "clean up item %d\n", i+1));
			SoundListItem* item = dynamic_cast<SoundListItem *>(fSoundList->ItemAt(i));
			if (item) {
				if (item->IsTemp())
					item->Entry().Remove();	//	delete temp file
				delete item;
			}
		}
		fSoundList->MakeEmpty();
	}
	//	Clean up currently recording file, if any.
	fRecEntry.Remove();
	fRecEntry.Unset();

	delete fSavePanel;
	delete fOpenPanel;
	delete fFolderPanel;
}


status_t
RecorderWindow::InitCheck()
{
	return fInitCheck;
}


void
RecorderWindow::CalcSizes(float min_width, float min_height)
{
	//	Set up size limits based on new screen size
	BScreen screen(this);
	BRect rect = screen.Frame();
	float width = rect.Width() - 12;
	SetSizeLimits(min_width, width, min_height, rect.Height() - 24);

	//	Don't zoom to cover all of screen; user can resize last bit if necessary.
	//	This leaves other windows visible.
	if (width > 640)
		width = 640 + (width - 640) / 2;
	SetZoomLimits(width, rect.Height() - 24);
}


void
RecorderWindow::_PopulateInputMenu(BMenu* menu)
{
	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL) {
		menu->AddItem(new BMenuItem(B_TRANSLATE("No inputs"), NULL));
		return;
	}

	BObjectList<media_client_id, true> clients;
	if (graph->GetClients(&clients) != B_OK) {
		menu->AddItem(new BMenuItem(B_TRANSLATE("No inputs"), NULL));
		return;
	}

	media_client_id defaultId = 0;
	graph->GetDefaultAudioInput(&defaultId);

	for (int32 i = 0; i < clients.CountItems(); i++) {
		media_client_id* idPtr = clients.ItemAt(i);
		if (idPtr == NULL)
			continue;
		media_client_id id = *idPtr;
		BMessage info;
		if (graph->GetClientInfo(id, &info) != B_OK)
			continue;
		bool isSource = false;
		if (info.FindBool("is.source", &isSource) != B_OK || !isSource)
			continue;

		BString name;
		if (info.FindString("name", &name) != B_OK
				|| name.Length() == 0)
			info.FindString("node_name", &name);
		if (name.Length() == 0)
			name.SetTo(B_TRANSLATE("Input"));

		BMessage* msg = new BMessage(INPUT_SELECTED);
		msg->AddInt32("device", id);
		BMenuItem* item = new BMenuItem(name.String(), msg);
		menu->AddItem(item);
		// The remembered device wins over the graph default.
		media_client_id wanted = fInputDeviceId != 0
			? fInputDeviceId : defaultId;
		if (id == wanted)
			item->SetMarked(true);
	}

	if (menu->CountItems() == 0)
		menu->AddItem(new BMenuItem(B_TRANSLATE("No inputs"), NULL));
}


status_t
RecorderWindow::InitWindow()
{
	status_t error = B_OK;

	try {
		//	Find temp directory for recorded sounds.
		BPath path;
		if (!(error = find_directory(B_SYSTEM_TEMP_DIRECTORY, &path)))
			error = fTempDir.SetTo(path.Path());
		if (error < 0)
			goto bad_mojo;

		if (BMediaGraph::Instance() == NULL) {
			error = B_DEVICE_NOT_FOUND;
			goto bad_mojo;
		}

		fRecorder = new BMediaRecorder("Sound Recorder",
			B_MEDIA_RAW_AUDIO);

		if (fRecorder->InitCheck() < B_OK) {
			error = fRecorder->InitCheck();
			goto bad_mojo;
		}

		fRecordFormat.SetToDefault();
		fRecorder->SetFormat(fRecordFormat);

		//	The menu bar lives at the top; content follows below it.
		fMenuBar = _BuildMenuBar();
		AddChild(fMenuBar);
		fMenuHeight = fMenuBar->Frame().Height() + 1;

		//	Create the window header with controls
		BRect r(Bounds());
		r.top += fMenuHeight;
		r.bottom = r.top + 175;
		BBox *background = new BBox(r, "_background",
			B_FOLLOW_LEFT_RIGHT	| B_FOLLOW_TOP, B_WILL_DRAW
			| B_FRAME_EVENTS | B_NAVIGABLE_JUMP, B_NO_BORDER);

		AddChild(background);

		r = background->Bounds();
		r.left = 0;
		r.right = r.left + 38;
		r.bottom = r.top + 104;
		fVUView = new VUView(r, B_FOLLOW_LEFT|B_FOLLOW_TOP);
		background->AddChild(fVUView);

		r = background->Bounds();
		r.left = r.left + 40;
		r.right -= 70;
		r.bottom = r.top + 104;
		fScopeView = new ScopeView(r, B_FOLLOW_LEFT_RIGHT|B_FOLLOW_TOP);
		background->AddChild(fScopeView);
		if (!fShowScope)
			fScopeView->Hide();
		if (!fShowMeter)
			fVUView->Hide();

		//	Elapsed recording time, driven by received frames.
		r = background->Bounds();
		r.left = r.right - 64;
		r.right -= 2;
		r.top = 2;
		r.bottom = r.top + 14;
		fElapsedView = new BStringView(r, "elapsed", "00:00:00",
			B_FOLLOW_RIGHT | B_FOLLOW_TOP);
		fElapsedView->SetAlignment(B_ALIGN_RIGHT);
		background->AddChild(fElapsedView);

		r = background->Bounds();
		r.left = 2;
		r.right -= 26;
		r.top = 115;
		r.bottom = r.top + 30;
		fTrackSlider = new TrackSlider(r, "trackSlider", new BMessage(POSITION_CHANGED),
			B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP);
		background->AddChild(fTrackSlider);

		BRect buttonRect;

		//	Button for rewinding
		buttonRect = BRect(BPoint(0,0), kSkipButtonSize);
		buttonRect.OffsetTo(background->Bounds().LeftBottom() - BPoint(-7, 25));
		fRewindButton = new TransportButton(buttonRect, B_TRANSLATE("Rewind"),
			kSkipBackBitmapBits, kPressedSkipBackBitmapBits,
			kDisabledSkipBackBitmapBits, new BMessage(REWIND));
		background->AddChild(fRewindButton);

		//	Button for stopping recording or playback
		buttonRect = BRect(BPoint(0,0), kStopButtonSize);
		buttonRect.OffsetTo(background->Bounds().LeftBottom() - BPoint(-48, 25));
		fStopButton = new TransportButton(buttonRect, B_TRANSLATE("Stop"),
			kStopButtonBitmapBits, kPressedStopButtonBitmapBits,
			kDisabledStopButtonBitmapBits, new BMessage(STOP));
		background->AddChild(fStopButton);

		//	Button for starting playback of selected sound
		BRect playRect(BPoint(0,0), kPlayButtonSize);
		playRect.OffsetTo(background->Bounds().LeftBottom() - BPoint(-82, 25));
		fPlayButton = new PlayPauseButton(playRect, B_TRANSLATE("Play"),
			new BMessage(PLAY), new BMessage(PLAY_PERIOD), ' ', 0);
		background->AddChild(fPlayButton);

		//	Button for forwarding
		buttonRect = BRect(BPoint(0,0), kSkipButtonSize);
		buttonRect.OffsetTo(background->Bounds().LeftBottom() - BPoint(-133, 25));
		fForwardButton = new TransportButton(buttonRect, B_TRANSLATE("Forward"),
			kSkipForwardBitmapBits, kPressedSkipForwardBitmapBits,
			kDisabledSkipForwardBitmapBits, new BMessage(FORWARD));
		background->AddChild(fForwardButton);

		//	Button to start recording (or waiting for sound)
		buttonRect = BRect(BPoint(0,0), kRecordButtonSize);
		buttonRect.OffsetTo(background->Bounds().LeftBottom() - BPoint(-174, 25));
		fRecordButton = new RecordButton(buttonRect, B_TRANSLATE("Record"),
			new BMessage(RECORD), new BMessage(RECORD_PERIOD));
		background->AddChild(fRecordButton);

		//	Button for saving selected sound
		buttonRect = BRect(BPoint(0,0), kDiskButtonSize);
		buttonRect.OffsetTo(background->Bounds().LeftBottom() - BPoint(-250, 21));
		fSaveButton = new TransportButton(buttonRect, B_TRANSLATE("Save"),
			kDiskButtonBitmapsBits, kPressedDiskButtonBitmapsBits,
			kDisabledDiskButtonBitmapsBits, new BMessage(SAVE));
		fSaveButton->SetResizingMode(B_FOLLOW_RIGHT | B_FOLLOW_TOP);
		background->AddChild(fSaveButton);

		//	Button Loop
		buttonRect = BRect(BPoint(0,0), kArrowSize);
		buttonRect.OffsetTo(background->Bounds().RightBottom() - BPoint(23, 48));
		fLoopButton = new DrawButton(buttonRect, B_TRANSLATE("Loop"),
			kLoopArrowBits, kArrowBits, new BMessage(LOOP));
		fLoopButton->SetResizingMode(B_FOLLOW_RIGHT | B_FOLLOW_TOP);
		fLoopButton->SetTarget(this);
		background->AddChild(fLoopButton);

		buttonRect = BRect(BPoint(0,0), kSpeakerIconBitmapSize);
		buttonRect.OffsetTo(background->Bounds().RightBottom() - BPoint(121, 17));
		SpeakerView *speakerView = new SpeakerView(buttonRect,
			B_FOLLOW_LEFT | B_FOLLOW_TOP);
		speakerView->SetResizingMode(B_FOLLOW_RIGHT | B_FOLLOW_TOP);
		background->AddChild(speakerView);

		buttonRect = BRect(BPoint(0,0), BPoint(84, 19));
		buttonRect.OffsetTo(background->Bounds().RightBottom() - BPoint(107, 20));
		fVolumeSlider = new VolumeSlider(buttonRect, "volumeSlider",
			B_FOLLOW_LEFT | B_FOLLOW_TOP);
		fVolumeSlider->SetResizingMode(B_FOLLOW_RIGHT | B_FOLLOW_TOP);
		background->AddChild(fVolumeSlider);

		//	Button to mask/see sounds list
		buttonRect = BRect(BPoint(0,0), kUpDownButtonSize);
		buttonRect.OffsetTo(background->Bounds().RightBottom() - BPoint(21, 25));
		fUpDownButton = new UpDownButton(buttonRect, new BMessage(VIEW_LIST));
		fUpDownButton->SetResizingMode(B_FOLLOW_RIGHT | B_FOLLOW_TOP);
		background->AddChild(fUpDownButton);

		r = Bounds();
		r.top = background->Frame().bottom + 1;
		fBottomBox = new BBox(r, "bottomBox", B_FOLLOW_ALL);
		fBottomBox->SetBorder(B_NO_BORDER);
		AddChild(fBottomBox);

		//	The actual list of recorded sounds (initially empty) sits
		//	below the header with the controls.
		r = fBottomBox->Bounds();
		r.left += 190;
		r.InsetBy(10, 10);
		r.left -= 10;
		r.top += 4;
		r.right -= B_V_SCROLL_BAR_WIDTH;
		r.bottom -= 25;
		fSoundList = new SoundListView(r, B_TRANSLATE("Sound List"),
			B_FOLLOW_ALL);
		fSoundList->SetSelectionMessage(new BMessage(SOUND_SELECTED));
		fSoundList->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		BScrollView *scroller = new BScrollView("scroller", fSoundList,
			B_FOLLOW_ALL, 0, false, true, B_FANCY_BORDER);
		fBottomBox->AddChild(scroller);

		r = fBottomBox->Bounds();
		r.right = r.left + 190;
		r.bottom -= 25;
		r.InsetBy(10, 8);
		r.top -= 1;
		fFileInfoBox = new BBox(r, "fileinfo", B_FOLLOW_LEFT);
		fFileInfoBox->SetLabel(B_TRANSLATE("File info"));

		fFileInfoBox->SetHighColor(ui_color(B_PANEL_TEXT_COLOR));

		BFont font = be_plain_font;
		font.SetSize(font.Size() * 0.92f);
		font_height height;
		font.GetHeight(&height);
		float fontHeight = height.ascent + height.leading + height.descent;

		r = fFileInfoBox->Bounds();
		r.left = 8;
		r.top = fontHeight + 6;
		r.bottom = r.top + fontHeight + 3;
		r.right -= 10;
		fFilename = new BStringView(r, "filename", B_TRANSLATE("File name:"));
		fFileInfoBox->AddChild(fFilename);
		fFilename->SetFont(&font, B_FONT_SIZE);
		fFilename->SetHighColor(ui_color(B_PANEL_TEXT_COLOR));

		r.top += fontHeight;
		r.bottom = r.top + fontHeight + 3;
		fFormat = new BStringView(r, "format", B_TRANSLATE("Format:"));
		fFileInfoBox->AddChild(fFormat);
		fFormat->SetFont(&font, B_FONT_SIZE);
		fFormat->SetHighColor(ui_color(B_PANEL_TEXT_COLOR));

		r.top += fontHeight;
		r.bottom = r.top + fontHeight + 3;
		fCompression = new BStringView(r, "compression",
			B_TRANSLATE("Compression:"));
		fFileInfoBox->AddChild(fCompression);
		fCompression->SetFont(&font, B_FONT_SIZE);
		fCompression->SetHighColor(ui_color(B_PANEL_TEXT_COLOR));

		r.top += fontHeight;
		r.bottom = r.top + fontHeight + 3;
		fChannels = new BStringView(r, "channels", B_TRANSLATE("Channels:"));
		fFileInfoBox->AddChild(fChannels);
		fChannels->SetFont(&font, B_FONT_SIZE);
		fChannels->SetHighColor(ui_color(B_PANEL_TEXT_COLOR));

		r.top += fontHeight;
		r.bottom = r.top + fontHeight + 3;
		fSampleSize = new BStringView(r, "samplesize",
			B_TRANSLATE("Sample size:"));
		fFileInfoBox->AddChild(fSampleSize);
		fSampleSize->SetFont(&font, B_FONT_SIZE);
		fSampleSize->SetHighColor(ui_color(B_PANEL_TEXT_COLOR));

		r.top += fontHeight;
		r.bottom = r.top + fontHeight + 3;
		fSampleRate = new BStringView(r, "samplerate",
			B_TRANSLATE("Sample rate:"));
		fFileInfoBox->AddChild(fSampleRate);
		fSampleRate->SetFont(&font, B_FONT_SIZE);
		fSampleRate->SetHighColor(ui_color(B_PANEL_TEXT_COLOR));

		r.top += fontHeight;
		r.bottom = r.top + fontHeight + 3;
		fDuration = new BStringView(r, "duration", B_TRANSLATE("Duration:"));
		fFileInfoBox->AddChild(fDuration);
		fDuration->SetFont(&font, B_FONT_SIZE);
		fDuration->SetHighColor(ui_color(B_PANEL_TEXT_COLOR));

		fFileInfoBox->ResizeTo(fFileInfoBox->Frame().Width(),
			r.bottom + fontHeight / 2.0f);
		fDeployedHeight = MIN_HEIGHT + fFileInfoBox->Bounds().Height() + 40.0f;

		fBottomBox->AddChild(fFileInfoBox);

		fBottomBox->Hide();
		CalcSizes(MIN_WIDTH, MIN_HEIGHT + fMenuHeight);
		ResizeTo(Frame().Width(), MIN_HEIGHT + fMenuHeight);

		// Make sure the save panel is happy.
		fSavePanel = new BFilePanel(B_SAVE_PANEL);
		fSavePanel->SetTarget(this);

		_UpdateRecordingMenus();
		_UpdateElapsed();
		_ApplyTimeDisplay();
	}
	catch (...) {
		goto bad_mojo;
	}
	UpdateButtons();
	return B_OK;

	//	Error handling.
bad_mojo:
	if (error >= 0)
		error = B_ERROR;
	delete fRecorder;
	fRecorder = NULL;

	delete fPlayer;
	fPlayer = NULL;
	return error;
}


bool
RecorderWindow::QuitRequested()	//	this means Close pressed
{
	StopRecording();
	StopPlaying();
	_SaveSettings();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
RecorderWindow::MessageReceived(BMessage * message)
{
	//	Your average generic message dispatching switch() statement.
	switch (message->what) {
	case INPUT_SELECTED:
		Input(message);
		break;
	case SOUND_SELECTED:
		Selected(message);
		break;
	case STOP_PLAYING:
		StopPlaying();
		break;
	case STOP_RECORDING:
		_StopRecordingAsync();
		break;
	case RECORD_STARTED:
		_RecordStarted(message);
		break;
	case RECORD_STOPPED:
		_RecordStopped();
		break;
	case PLAY_PERIOD:
		if (fPlayer) {
			if (fPlayer->State() == BMediaPlayer::B_PLAYER_PLAYING)
				fPlayButton->SetPlaying();
			else
				fPlayButton->SetPaused();
			_UpdatePlayPosition();
		}
		break;
	case RECORD_PERIOD:
		fRecordButton->SetRecording();
		break;
	case RECORD:
		Record(message);
		break;
	case STOP:
		Stop(message);
		break;
	case PLAY:
		Play(message);
		break;
	case SAVE:
		Save(message);
		break;
	case B_SAVE_REQUESTED:
		DoSave(message);
		break;
	case VIEW_LIST:
		if (fUpDownButton->Value() == B_CONTROL_ON) {
			fBottomBox->Show();
			CalcSizes(MIN_WIDTH, fDeployedHeight + fMenuHeight);
			ResizeTo(Frame().Width(), fDeployedHeight + fMenuHeight);
		} else {
			fBottomBox->Hide();
			CalcSizes(MIN_WIDTH, MIN_HEIGHT + fMenuHeight);
			ResizeTo(Frame().Width(), MIN_HEIGHT + fMenuHeight);

		}
		break;
	case UPDATE_TRACKSLIDER:
		_UpdatePlayPosition();
		break;
	case POSITION_CHANGED:
		{
			bigtime_t right, left, main;
			if (message->FindInt64("main", &main) == B_OK) {
				if (fPlayer)
					fPlayer->SeekTo(main);
				if (fPlayTrack) {
					bigtime_t t = main;
					fPlayTrack->SeekToTime(&t);
					fPlayFrame = fPlayTrack->CurrentFrame();
				}
				fScopeView->SetMainTime(main);
			}
			if (message->FindInt64("right", &right) == B_OK) {
				if (fPlayTrack) {
					fPlayLimit = MIN(fPlayFrames,
						(off_t)(right * fPlayFormat.format.u.raw_audio.frame_rate
							/ 1000000LL));
				}
				fScopeView->SetRightTime(right);
			}
			if (message->FindInt64("left", &left) == B_OK)
				fScopeView->SetLeftTime(left);
			break;
		}
	case LOOP:
		fLooping = fLoopButton->ButtonState();
		break;
	case MENU_NEW:
		Record(message);
		break;
	case MENU_OPEN:
		if (message->HasRef("refs")) {
			RefsReceived(message);
			break;
		}
		_OpenPanel();
		break;
	case MENU_SAVE_AS:
		Save(message);
		break;
	case MENU_EXPORT:
		_ExportSelected();
		break;
	case MENU_SHOW_TRACKER:
		_ShowInTracker();
		break;
	case MENU_FILE_DELETE:
	case MENU_EDIT_DELETE:
		_DeleteSelected();
		break;
	case MENU_RENAME:
		_RenameSelected();
		break;
	case MENU_SELECT_ALL:
		if (fSoundList->CountItems() > 0)
			fSoundList->Select(0, fSoundList->CountItems() - 1);
		break;
	case MENU_FORMAT:
	{
		int32 encoding;
		if (message->FindInt32("encoding", &encoding) == B_OK) {
			fEncoding = encoding;
			int32 sampleFormat;
			if (message->FindInt32("sample_format", &sampleFormat) == B_OK)
				fSampleFormat = sampleFormat;
			_UpdateRecordingMenus();
			_SaveSettings();
		}
		break;
	}
	case MENU_RATE:
	{
		int32 rate;
		if (message->FindInt32("rate", &rate) == B_OK) {
			fRecordRate = (uint32)rate;
			_UpdateRecordingMenus();
			_SaveSettings();
		}
		break;
	}
	case MENU_CHANNELS:
	{
		int32 channels;
		if (message->FindInt32("channels", &channels) == B_OK) {
			fRecordChannels = (uint32)channels;
			_UpdateRecordingMenus();
			_SaveSettings();
		}
		break;
	}
	case MENU_QUALITY:
	{
		float quality;
		if (message->FindFloat("quality", &quality) == B_OK) {
			fQuality = quality;
			_UpdateRecordingMenus();
			_SaveSettings();
		}
		break;
	}
	case MENU_SAVE_FOLDER:
	{
		entry_ref ref;
		if (message->FindRef("refs", &ref) == B_OK) {
			BEntry entry(&ref, true);
			BPath path;
			if (entry.GetPath(&path) == B_OK) {
				fSaveFolder = path;
				_SaveSettings();
			}
			break;
		}
		_FolderPanel();
		break;
	}
	case MENU_AUTONAME:
	{
		int32 pattern;
		if (message->FindInt32("pattern", &pattern) == B_OK) {
			fAutoName = pattern;
			_UpdateRecordingMenus();
			_SaveSettings();
		}
		break;
	}
	case MENU_TOGGLE_SCOPE:
		fShowScope = fScopeItem->IsMarked();
		if (fShowScope)
			fScopeView->Show();
		else
			fScopeView->Hide();
		_SaveSettings();
		break;
	case MENU_TOGGLE_METER:
		fShowMeter = fMeterItem->IsMarked();
		if (fShowMeter)
			fVUView->Show();
		else
			fVUView->Hide();
		_SaveSettings();
		break;
	case MENU_TIME_DISPLAY:
	{
		int32 mode;
		if (message->FindInt32("mode", &mode) == B_OK) {
			fTimeDisplay = mode;
			_UpdateRecordingMenus();
			_ApplyTimeDisplay();
			_SaveSettings();
		}
		break;
	}
	case MENU_ABOUT:
		_About();
		break;
	case UPDATE_ELAPSED:
		_UpdateElapsed();
		break;
	case B_SIMPLE_DATA:
	case B_REFS_RECEIVED:
		{
			RefsReceived(message);
			break;
		}
	case B_COPY_TARGET:
		CopyTarget(message);
		break;
	case BMediaPlayer::B_PLAYER_STATE_CHANGED:
	case BMediaPlayer::B_PLAYER_END_OF_STREAM:
	case BMediaPlayer::B_PLAYER_ERROR:
	case BMediaPlayer::B_PLAYER_DURATION_CHANGED:
	case BMediaPlayer::B_PLAYER_POSITION_CHANGED:
		_ShowPlayerMessage(message);
		break;
	default:
		BWindow::MessageReceived(message);
		break;
	}
}


void
RecorderWindow::_ShowPlayerMessage(BMessage* message)
{
	if (fPlayer == NULL)
		return;

	int32 state = BMediaPlayer::B_PLAYER_STOPPED;
	message->FindInt32("state", &state);

	if (message->what == BMediaPlayer::B_PLAYER_END_OF_STREAM
			|| message->what == BMediaPlayer::B_PLAYER_ERROR
			|| state == BMediaPlayer::B_PLAYER_STOPPED) {
		if (fButtonState == btnPlaying)
			StopPlaying();
		return;
	}

	if (state == BMediaPlayer::B_PLAYER_PLAYING)
		fPlayButton->SetPlaying();
	else if (state == BMediaPlayer::B_PLAYER_PAUSED)
		fPlayButton->SetPaused();

	_UpdatePlayPosition();
}


void
RecorderWindow::_UpdatePlayPosition()
{
	if (fPlayer == NULL)
		return;

	bigtime_t position = fPlayer->Position();
	fTrackSlider->SetMainTime(position, false);
	fScopeView->SetMainTime(position);
}


void
RecorderWindow::Record(BMessage * message)
{
	//	User pressed Record button
	if (fBusy)
		return;
	fRecording = true;
	if (fButtonState != btnPaused) {
		_StopRecordingAsync();
		return;			//	user is too fast on the mouse
	}
	SetButtonState(btnRecording);
	fRecordButton->SetRecording();

	char name[256];
	//	Create a file with a temporary name
	status_t err = NewTempName(name);
	if (err < B_OK) {
		ErrorAlert(B_TRANSLATE("Cannot find an unused name to use for the "
			"new recording"), err);
		return;
	}
	//	Find the file so we can refer to it later
	err = fTempDir.FindEntry(name, &fRecEntry);
	if (err < B_OK) {
		ErrorAlert(B_TRANSLATE("Cannot find the temporary file created to "
			"hold the new recording"), err);
		return;
	}
	err = fRecFile.SetTo(&fTempDir, name, O_RDWR);
	if (err < B_OK) {
		ErrorAlert(B_TRANSLATE("Cannot open the temporary file created to "
			"hold the new recording"), err);
		fRecEntry.Unset();
		return;
	}
	//	Reserve space on disk (creates fewer fragments)
	err = fRecFile.SetSize(4 * fRecordFormat.format.u.raw_audio.channel_count
		* fRecordFormat.format.u.raw_audio.frame_rate
		* (fRecordFormat.format.u.raw_audio.format
			& media_raw_audio_format::B_AUDIO_SIZE_MASK));
	if (err < B_OK) {
		ErrorAlert(B_TRANSLATE("Cannot record a sound that long"), err);
		fRecEntry.Remove();
		fRecEntry.Unset();
		return;
	}
	fRecSize = 0;
	fRecFrames = 0;
	fLastReport = system_time();
	fVUView->ResetClip();
	_UpdateElapsed();

	// Encode the take through the recorder's GStreamer output pipeline.
	BPath recPath;
	err = fRecEntry.GetPath(&recPath);
	if (err < B_OK) {
		ErrorAlert(B_TRANSLATE("Cannot resolve the recording path"), err);
		fRecEntry.Remove();
		fRecEntry.Unset();
		return;
	}
	// Sample format only applies to WAV; the lossy encoders pick theirs.
	BMediaRecorder::sample_format sampleFormat
		= (BMediaRecorder::sample_format)fSampleFormat;
	if (fEncoding != B_CODEC_TYPE_WAV && fEncoding != B_CODEC_TYPE_PCM)
		sampleFormat = BMediaRecorder::B_SAMPLE_FORMAT_DEFAULT;
	BMediaFormat encoded = _MakeEncodedFormat();
	err = fRecorder->SetOutputFile(recPath.Path(), encoded, fRecordRate,
		fRecordChannels, sampleFormat, fQuality);
	if (err < B_OK) {
		ErrorAlert(B_TRANSLATE("Cannot set up the recording file"), err);
		fRecEntry.Remove();
		fRecEntry.Unset();
		fRecording = false;
		SetButtonState(btnPaused);
		fRecordButton->SetStopped();
		return;
	}

	// Connect and start off the window thread; RECORD_STARTED reports back.
	fBusy = true;
	UpdateButtons();
	_JoinWorker();
	fWorker = std::thread([this]() {
		status_t result = MakeRecordConnection(fInputDeviceId);
		if (result >= B_OK)
			result = fRecorder->Start();
		BMessage reply(RECORD_STARTED);
		reply.AddInt32("status", result);
		PostMessage(&reply);
	});
}


void
RecorderWindow::_RecordStarted(BMessage* message)
{
	_JoinWorker();
	fBusy = false;
	status_t err = message->FindInt32("status");
	if (err < B_OK) {
		ErrorAlert(B_TRANSLATE("Cannot connect to the selected sound input"),
			err);
		BreakRecordConnection();
		if (fRecorder != NULL) {
			if (fRecorder->IsRunning())
				fRecorder->Stop(true);
			fRecorder->SetHooks(NULL, NULL, NULL);
			fRecorder->ClearOutputFile();
		}
		fRecEntry.Remove();
		fRecEntry.Unset();
		fRecFile.Unset();
		fRecording = false;
		SetButtonState(btnPaused);
		fRecordButton->SetStopped();
	}
	UpdateButtons();
}


void
RecorderWindow::Play(BMessage * message)
{
	if (fPlayer) {
		//	User pressed Play button and playing
		if (fPlayer->State() == BMediaPlayer::B_PLAYER_PLAYING) {
			fPlayer->Pause();
			fPlayButton->SetPaused();
		} else {
			fPlayer->Play();
			fPlayButton->SetPlaying();
		}
		return;
	}

	SetButtonState(btnPlaying);
	fPlayButton->SetPlaying();

	if (!fPlayTrack) {
		ErrorAlert(B_TRANSLATE("Cannot get the file to play"), B_ERROR);
		return;
	}

	fPlayLimit = MIN(fPlayFrames, (off_t)(fTrackSlider->RightTime()
		* fPlayFormat.format.u.raw_audio.frame_rate / 1000000LL));

	entry_ref ref;
	if (fRecEntry.GetRef(&ref) != B_OK && fSoundList) {
		int32 index = fSoundList->CurrentSelection();
		SoundListItem* item = dynamic_cast<SoundListItem*>(
			fSoundList->ItemAt(index));
		if (item == NULL || item->Entry().GetRef(&ref) != B_OK) {
			ErrorAlert(B_TRANSLATE("Cannot get the file to play"), B_ERROR);
			return;
		}
	}

	fPlayer = new BMediaPlayer();
	if (fPlayer == NULL) {
		ErrorAlert(B_TRANSLATE("Cannot create the player"), B_NO_MEMORY);
		return;
	}

	status_t err = fPlayer->SetTo(&ref);
	if (err < B_OK) {
		delete fPlayer;
		fPlayer = NULL;
		ErrorAlert(B_TRANSLATE("Cannot open the file to play"), err);
		return;
	}

	fVolumeSlider->SetMediaPlayer(fPlayer);
	fPlayer->SetTarget(BMessenger(this));

	err = fPlayer->Play();
	if (err < B_OK) {
		delete fPlayer;
		fPlayer = NULL;
		fVolumeSlider->SetMediaPlayer(NULL);
		ErrorAlert(B_TRANSLATE("Cannot play the file"), err);
		return;
	}
	fPlayer->SetHasData(true);
}


void
RecorderWindow::Stop(BMessage * message)
{
	//	User pressed Stop button.
	if (fBusy)
		return;
	//	Stop recorder (finishes asynchronously).
	if (fRecording)
		_StopRecordingAsync();
	//	Stop player.
	StopPlaying();
}


void
RecorderWindow::Save(BMessage * message)
{
	//	User pressed Save button.
	//	Find the item to save.
	int32 index = fSoundList->CurrentSelection();
	SoundListItem* pItem = dynamic_cast<SoundListItem*>(fSoundList->ItemAt(index));
	if ((! pItem) || (pItem->Entry().InitCheck() != B_OK))
		return;

	// Update the save panel and show it.
	char automatic[B_FILE_NAME_LENGTH];
	_AutoName(automatic, sizeof(automatic));
	snprintf(automatic, sizeof(automatic), "%s.%s", automatic,
		_ExtensionForEncoding(fEncoding));
	BMessage saveMsg(B_SAVE_REQUESTED);
	entry_ref ref;
	pItem->Entry().GetRef(&ref);

	if (saveMsg.AddPointer("sound list item", pItem) != B_OK)
		fprintf(stderr, "failed to add pItem\n");
	fSavePanel->SetSaveText(automatic);
	fSavePanel->SetMessage(&saveMsg);
	if (fSaveFolder.InitCheck() == B_OK)
		fSavePanel->SetPanelDirectory(fSaveFolder.Path());
	fSavePanel->Show();
}


void
RecorderWindow::DoSave(BMessage * message)
{
	// User picked a place to put the file.
	// Find the location of the old (e.g.
	// temporary file), and the name of the
	// new file to save.
	entry_ref old_ref, new_dir_ref;
	const char* new_name;
	SoundListItem* pItem;

	if ((message->FindPointer("sound list item", (void**) &pItem) == B_OK)
		&& (message->FindRef("directory", &new_dir_ref) == B_OK)
		&& (message->FindString("name", &new_name) == B_OK)) {
		BEntry& oldEntry = pItem->Entry();
		BFile oldFile(&oldEntry, B_READ_WRITE);
		if (oldFile.InitCheck() != B_OK)
			return;

		BDirectory newDir(&new_dir_ref);
		if (newDir.InitCheck() != B_OK)
			return;

		BFile newFile;
		newDir.CreateFile(new_name, &newFile);

		if (newFile.InitCheck() != B_OK)
			return;

		status_t err = CopyFile(newFile, oldFile);

		if (err == B_OK) {
			// clean up the sound list and item
			if (pItem->IsTemp())
				oldEntry.Remove(); // blows away temp file!
			oldEntry.SetTo(&newDir, new_name);
			pItem->SetTemp(false);	// don't blow the new entry away when we exit!
			fSoundList->Invalidate();
		}
	} else {
		WINDOW((stderr, "Couldn't save file.\n"));
	}
}


void
RecorderWindow::Input(BMessage * message)
{
	//	User selected input from the Recording menu
	int32 id = 0;
	if (message->FindInt32("device", &id) != B_OK)
		return;
	fInputDeviceId = (media_client_id)id;
	for (int32 i = 0; i < fInputMenu->CountItems(); i++) {
		BMenuItem* item = fInputMenu->ItemAt(i);
		if (item == NULL || item->Message() == NULL)
			continue;
		int32 device;
		if (item->Message()->FindInt32("device", &device) == B_OK)
			item->SetMarked(device == id);
	}
	_SaveSettings();
}


void
RecorderWindow::Selected(BMessage * message)
{
	//	User selected a sound in list view
	int32 selIdx = fSoundList->CurrentSelection();
	SoundListItem* pItem = dynamic_cast<SoundListItem*>(fSoundList->ItemAt(selIdx));
	if (!pItem)
		return;
	status_t err = UpdatePlayFile(pItem, true);
	if (err != B_OK) {
		ErrorAlert(B_TRANSLATE("Cannot recognize this file as a media file"),
			err == B_MEDIA_NO_HANDLER ? B_OK : err);
		RemoveCurrentSoundItem();
	}
	UpdateButtons();
}


status_t
RecorderWindow::MakeRecordConnection(media_client_id deviceId)
{
	CONNECT((stderr, "RecorderWindow::MakeRecordConnection()\n"));

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;
	if (fRecorder == NULL || fRecorder->Input() == NULL)
		return B_NO_INIT;

	if (deviceId == 0) {
		status_t err = graph->GetDefaultAudioInput(&deviceId);
		if (err != B_OK)
			return err;
		fInputDeviceId = deviceId;
	}

	BObjectList<BMediaGraph::ForeignPort, true> ports;
	status_t err = graph->GetForeignPorts((uint32)deviceId, &ports);
	if (err != B_OK)
		return err;

	BMediaGraph::ForeignPort* source = NULL;
	for (int32 i = 0; i < ports.CountItems(); i++) {
		BMediaGraph::ForeignPort* port = ports.ItemAt(i);
		if (port == NULL)
			continue;
		if (port->direction == "out") {
			source = port;
			break;
		}
	}
	if (source == NULL)
		return B_ENTRY_NOT_FOUND;

	// Disconnect any previous capture source first.
	if (fHasConnectedSource)
		BreakRecordConnection();

	fRecorder->SetHooks(RecordFile, NotifyRecordFile, this);

	// The recorder's input stream must exist before the graph can
	// resolve node/port ids; Start() creates it.
	if (!fRecorder->IsRunning()) {
		err = fRecorder->Start();
		if (err < B_OK) {
			fRecorder->SetHooks(NULL, NULL, NULL);
			return err;
		}
	}

	fConnectedSource = *source;
	fHasConnectedSource = true;

	err = graph->ConnectChannels(fConnectedSource.nodeId, fRecorder->Input());
	if (err < B_OK) {
		fprintf(stderr, "SoundRecorder: connect to node %u failed (0x%x)\n",
			(unsigned)fConnectedSource.nodeId, (unsigned)err);
		CONNECT((stderr, "RecorderWindow::MakeRecordConnection():"
			" graph connect failed 0x%08" B_PRIx32 "\n", err));
		fHasConnectedSource = false;
		if (fRecorder->IsRunning())
			fRecorder->Stop(true);
		fRecorder->SetHooks(NULL, NULL, NULL);
		return err;
	}

	const BMediaFormat& connected = fRecorder->Format();
	if (connected.IsRawAudio())
		fRecordFormat = connected;

	return B_OK;
}


status_t
RecorderWindow::BreakRecordConnection()
{
	if (fRecorder == NULL || fRecorder->Input() == NULL)
		return B_OK;

	if (fHasConnectedSource) {
		BMediaGraph* graph = BMediaGraph::Instance();
		if (graph != NULL)
			graph->Disconnect(&fConnectedSource, fRecorder->Input());
		fHasConnectedSource = false;
	}
	return B_OK;
}


status_t
RecorderWindow::StopRecording()
{
	_JoinWorker();
	fBusy = false;
	if (!fRecording)
		return B_OK;
	fRecording = false;

	status_t err = B_OK;
	if (fRecorder != NULL) {
		BreakRecordConnection();
		err = fRecorder->Stop(true);
		if (err < B_OK)
			return err;
		fRecorder->SetHooks(NULL, NULL, NULL);
		fRecorder->ClearOutputFile();
	}

	_FinishRecording();
	return B_OK;
}


void
RecorderWindow::_StopRecordingAsync()
{
	if (!fRecording || fBusy)
		return;
	fRecording = false;
	fBusy = true;
	UpdateButtons();
	_JoinWorker();
	fWorker = std::thread([this]() {
		BreakRecordConnection();
		fRecorder->Stop(true);
		fRecorder->SetHooks(NULL, NULL, NULL);
		fRecorder->ClearOutputFile();
		PostMessage(RECORD_STOPPED);
	});
}


void
RecorderWindow::_RecordStopped()
{
	_JoinWorker();
	fBusy = false;
	_FinishRecording();
	UpdateButtons();
}


void
RecorderWindow::_JoinWorker()
{
	if (fWorker.joinable())
		fWorker.join();
}


void
RecorderWindow::_FinishRecording()
{
	if (fRecSize > 0) {
		AddSoundItem(fRecEntry, true);
	} else
		fRecEntry.Remove();

	//	We're done for this time.
	fRecEntry.Unset();
	//	Close the file.
	fRecFile.Unset();
	//	No more recording going on.
	fRecSize = 0;
	SetButtonState(btnPaused);
	fRecordButton->SetStopped();
}


status_t
RecorderWindow::StopPlaying()
{
	if (fPlayer) {
		fPlayer->Stop();
		fPlayer->SetTarget(BMessenger());
		delete fPlayer;
		fPlayer = NULL;
	}
	fVolumeSlider->SetMediaPlayer(NULL);
	SetButtonState(btnPaused);
	fPlayButton->SetStopped();
	fTrackSlider->ResetMainTime();
	if (fTrackSlider->MainTime() != NULL)
		fScopeView->SetMainTime(*fTrackSlider->MainTime());
	return B_OK;
}


void
RecorderWindow::SetButtonState(BtnState state)
{
	fButtonState = state;
	UpdateButtons();
}


void
RecorderWindow::UpdateButtons()
{
	bool hasSelection = (fSoundList->CurrentSelection() >= 0);
	if (fBusy) {
		fRecordButton->SetEnabled(false);
		fPlayButton->SetEnabled(false);
		fRewindButton->SetEnabled(false);
		fForwardButton->SetEnabled(false);
		fStopButton->SetEnabled(false);
		fSaveButton->SetEnabled(false);
		fInputMenu->Superitem()->SetEnabled(false);
		fFormatMenu->Superitem()->SetEnabled(false);
		fRateMenu->Superitem()->SetEnabled(false);
		fChannelsMenu->Superitem()->SetEnabled(false);
		fQualityMenu->Superitem()->SetEnabled(false);
		fAutoNameMenu->Superitem()->SetEnabled(false);
		return;
	}
	fRecordButton->SetEnabled(fButtonState != btnPlaying);
	fPlayButton->SetEnabled((fButtonState != btnRecording) && hasSelection);
	fRewindButton->SetEnabled((fButtonState != btnRecording) && hasSelection);
	fForwardButton->SetEnabled((fButtonState != btnRecording) && hasSelection);
	fStopButton->SetEnabled(fButtonState != btnPaused);
	fSaveButton->SetEnabled(hasSelection && (fButtonState != btnRecording));
	bool locked = fButtonState == btnRecording;
	bool lossy = fEncoding == B_CODEC_TYPE_OGG
		|| fEncoding == B_CODEC_TYPE_OPUS
		|| fEncoding == B_CODEC_TYPE_MP3;
	fInputMenu->Superitem()->SetEnabled(!locked);
	fFormatMenu->Superitem()->SetEnabled(!locked);
	fRateMenu->Superitem()->SetEnabled(!locked);
	fChannelsMenu->Superitem()->SetEnabled(!locked);
	fQualityMenu->Superitem()->SetEnabled(!locked && lossy);
	fAutoNameMenu->Superitem()->SetEnabled(!locked);
}


status_t
RecorderWindow::UpdatePlayFile(SoundListItem* item, bool updateDisplay)
{
	fScopeView->CancelRendering();
	StopPlaying();
	StopRecording();

	if (fPlayTrack && fPlayFile) {
		fPlayFile->ReleaseTrack(fPlayTrack);
		fPlayTrack = NULL;
	}
	if (fPlayFile) {
		delete fPlayFile;
		fPlayFile = NULL;
	}

	status_t err;
	BEntry& entry = item->Entry();
	entry_ref ref;
	entry.GetRef(&ref);
	fPlayFile = new BMediaFile(&ref);
	if ((err = fPlayFile->InitCheck()) < B_OK) {
		delete fPlayFile;
		fPlayFile = NULL;
		return err;
	}

	for (int ix=0; ix < fPlayFile->CountTracks(); ix++) {
		BMediaTrack * track = fPlayFile->TrackAt(ix);
		fPlayFormat.SetToDefault();
		fPlayFormat.format.type = B_MEDIA_RAW_AUDIO;
		if ((track->DecodedFormat(&fPlayFormat) == B_OK)
			&& (fPlayFormat.format.type == B_MEDIA_RAW_AUDIO)) {
			fPlayTrack = track;
			break;
		}
		if (track)
			fPlayFile->ReleaseTrack(track);
	}

	if (!fPlayTrack) {
		delete fPlayFile;
		fPlayFile = NULL;
		return B_STREAM_NOT_FOUND;
	}

	if (!updateDisplay)
		return B_OK;

	BString filename = B_TRANSLATE("File name: ");
	filename << ref.name;
	fFilename->SetText(filename.String());

	BString format = B_TRANSLATE("Format: ");
	format << ref.name;
	BString compression = B_TRANSLATE("Compression: ");
	media_codec_info codec_info;
	if (fPlayTrack->GetCodecInfo(&codec_info) == B_OK) {
		if (strcmp(codec_info.short_name, "raw")==0)
			compression << B_TRANSLATE("None");
		else
			compression << codec_info.short_name;
	} else
		compression << B_TRANSLATE("None");
	BString channels = B_TRANSLATE("Channels: ");
	channels << fPlayFormat.format.u.raw_audio.channel_count;
	BString samplesize = B_TRANSLATE("Sample size: ");
	samplesize << 8 * (fPlayFormat.format.u.raw_audio.format & 0xf)
		<< B_TRANSLATE(" bits");
	BString samplerate = B_TRANSLATE("Sample rate: ");
	samplerate << (int)fPlayFormat.format.u.raw_audio.frame_rate;
	BString durationString = B_TRANSLATE("Duration: ");
	bigtime_t duration = fPlayTrack->Duration();
	durationString << (float)(duration / 1000000.0) << B_TRANSLATE(" seconds");

	fFormat->SetText(format.String());
	fCompression->SetText(compression.String());
	fChannels->SetText(channels.String());
	fSampleSize->SetText(samplesize.String());
	fSampleRate->SetText(samplerate.String());
	fDuration->SetText(durationString.String());

	fTrackSlider->SetTotalTime(duration, true);
	fScopeView->SetTotalTime(duration, true);
	fScopeView->RenderTrack(fPlayTrack, fPlayFormat.format);

	fPlayFrames = fPlayTrack->CountFrames();
	fTrackSlider->SetSampleRate(fPlayFormat.format.u.raw_audio.frame_rate);
	return B_OK;
}


void
RecorderWindow::ErrorAlert(const char * action, status_t err)
{
	char msg[300];
	if (err != B_OK)
		sprintf(msg, "%s: %s. [%" B_PRIx32 "]", action, strerror(err), (int32) err);
	else
		sprintf(msg, "%s.", action);
	BAlert* alert = new BAlert("", msg, B_TRANSLATE("Stop"));
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
}


status_t
RecorderWindow::NewTempName(char * name)
{
	int init_count = fTempCount;
again:
	if (fTempCount-init_count > 25) {
		return B_ERROR;
	}
	else {
		fTempCount++;
		if (fTempCount==0)
			sprintf(name, "Audio Clip");
		else
			sprintf(name, "Audio Clip %d", fTempCount);
		BPath path;
		status_t err;
		BEntry tempEnt;
		if ((err = fTempDir.GetEntry(&tempEnt)) < B_OK) {
			return err;
		}
		if ((err = tempEnt.GetPath(&path)) < B_OK) {
			return err;
		}
		path.Append(name);
		int fd;
		//	Use O_EXCL so we know we created the file (sync with other instances)
		if ((fd = open(path.Path(), O_RDWR | O_CREAT | O_EXCL, 0666)) < 0) {
			goto again;
		}
		close(fd);
	}
	return B_OK;
}


void
RecorderWindow::AddSoundItem(const BEntry& entry, bool temp)
{
	//	Create list item to display.
	SoundListItem * listItem = new SoundListItem(entry, temp);
	fSoundList->AddItem(listItem);
	fSoundList->Invalidate();
	fSoundList->Select(fSoundList->IndexOf(listItem));
}


void
RecorderWindow::RemoveCurrentSoundItem() {
	int32 index = fSoundList->CurrentSelection();
	BListItem *item = fSoundList->RemoveItem(index);
	delete item;
	if (index >= fSoundList->CountItems())
		index = fSoundList->CountItems() - 1;
	fSoundList->Select(index);
}


void
RecorderWindow::RecordFile(void* cookie, bigtime_t timestamp,
	void* data, size_t size, const media_raw_audio_format &format)
{
	//	Callback called from the recorder when receiving buffers.
	RecorderWindow * window = (RecorderWindow *)cookie;

	if (window->fRecording) {
		window->fVUView->ComputeLevels(data, size, format.format);
		window->fRecSize += size;
		const uint32 stride = (format.format
				& media_raw_audio_format::B_AUDIO_SIZE_MASK)
			* format.channel_count;
		if (stride > 0)
			window->fRecFrames += size / stride;
		const bigtime_t now = system_time();
		if (now - window->fLastReport >= 1000000) {
			window->fLastReport = now;
			window->PostMessage(UPDATE_ELAPSED);
			fprintf(stderr, "SoundRecorder: %lld frames received\n",
				(long long)window->fRecFrames);
		}
	}
}


void
RecorderWindow::NotifyRecordFile(void * cookie,
	BMediaRecorder::notification code, ...)
{
	if (code == BMediaRecorder::B_STOPPED
			|| code == BMediaRecorder::B_INPUT_DISCONNECTED) {
		RecorderWindow * window = (RecorderWindow *)cookie;
		// Tell the window we've stopped, if it doesn't
		// already know.
		window->PostMessage(STOP_RECORDING);
	}
}


void
RecorderWindow::RefsReceived(BMessage *msg)
{
	entry_ref ref;
	int32 i = 0;
	int32 countGood = 0;
	int32 countBad = 0;

	while (msg->FindRef("refs", i++, &ref) == B_OK) {

		BEntry entry(&ref, true);
		BPath path(&entry);
		BNode node(&entry);

		if (node.IsFile()) {
			SoundListItem * listItem = new SoundListItem(entry, false);
			if (UpdatePlayFile(listItem) == B_OK) {
				fSoundList->AddItem(listItem);
				countGood++;
				continue;
			}
			delete listItem;
		} else if(node.IsDirectory()) {

		}
		countBad++;
	}

	if (countBad == 1 && countGood == 0) {
		BAlert* alert = new BAlert(B_TRANSLATE("Nothing to play"),
			B_TRANSLATE("The file doesn't appear to be an audio file."),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();
	} else if (countBad > 0 && countGood == 0) {
		BAlert* alert = new BAlert(B_TRANSLATE("Nothing to play"),
			B_TRANSLATE("None of the files appear to be audio files."),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();
	} else if (countGood > 0) {
		if (countBad > 0) {
			BAlert* alert = new BAlert(B_TRANSLATE("Invalid audio files"),
			B_TRANSLATE("Some of the files don't appear to be audio files."),
				B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
				B_WARNING_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
		}
		fSoundList->Select(fSoundList->CountItems() - 1);
	}
}


void
RecorderWindow::CopyTarget(BMessage *msg)
{
	const char *type = NULL;
	if (msg->FindString("be:types", &type) == B_OK) {
		if (!strcasecmp(type, B_FILE_MIME_TYPE)) {
			const char *name;
			entry_ref dir;
			if (msg->FindString("be:filetypes") != NULL
				&& msg->FindString("name", &name) == B_OK
				&& msg->FindRef("directory", &dir) == B_OK) {
				BDirectory directory(&dir);
				BFile file(&directory, name, O_RDWR | O_TRUNC);

				// seek time
				bigtime_t start = fTrackSlider->LeftTime();

				// write data
				bigtime_t diffTime = fTrackSlider->RightTime()
					- fTrackSlider->LeftTime();
				int64 framesToWrite = (int64) (diffTime
					* fPlayFormat.format.u.raw_audio.frame_rate / 1000000LL);
				int32 frameSize = (fPlayFormat.format.u.raw_audio.format & 0xf)
					* fPlayFormat.format.u.raw_audio.channel_count;

				if (fPlayTrack == NULL)
					return;

				char *data = (char *)malloc(fPlayFormat.format.u.raw_audio.buffer_size);
				if (data == NULL)
					return;

				fPlayTrack->SeekToTime(&start);
				fPlayFrame = fPlayTrack->CurrentFrame();
				while (framesToWrite > 0) {
					int64 frames = 0;
					status_t err = fPlayTrack->ReadFrames(data, &frames);
					if (frames <= 0 || err != B_OK) {
						if (err != B_OK)
							fprintf(stderr, "CopyTarget: ReadFrames failed\n");
						break;
					}
					file.Write(data, frames * frameSize);
					framesToWrite -= frames;
				}

				file.Sync();
				free(data);
			}
		} else {

		}
	}
}


// #pragma mark - menu bar, settings, recording options


static const char*
_ExtensionForEncoding(int32 encoding)
{
	switch (encoding) {
		case B_CODEC_TYPE_FLAC:	return "flac";
		case B_CODEC_TYPE_OGG:	return "ogg";
		case B_CODEC_TYPE_OPUS:	return "opus";
		case B_CODEC_TYPE_MP3:	return "mp3";
		default:			return "wav";
	}
}


static void
_FillFileFormat(media_file_format* fileFormat, int32 encoding)
{
	memset(fileFormat, 0, sizeof(*fileFormat));
	const char* mime = "audio/x-wav";
	switch (encoding) {
		case B_CODEC_TYPE_FLAC:
			mime = "audio/x-flac";
			break;
		case B_CODEC_TYPE_OGG:
		case B_CODEC_TYPE_OPUS:
			mime = "application/ogg";
			break;
		case B_CODEC_TYPE_MP3:
			mime = "audio/mpeg";
			break;
		default:
			break;
	}
	strncpy(fileFormat->mime_type, mime, sizeof(fileFormat->mime_type) - 1);
}


void
RecorderWindow::_LoadSettings()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK)
		return;
	path.Append("SoundRecorder");

	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BMessage settings;
	if (settings.Unflatten(&file) != B_OK)
		return;

	int32 value;
	if (settings.FindInt32("encoding", &value) == B_OK)
		fEncoding = value;
	if (settings.FindInt32("sample_format", &value) == B_OK)
		fSampleFormat = value;
	if (settings.FindInt32("rate", &value) == B_OK)
		fRecordRate = (uint32)value;
	if (settings.FindInt32("channels", &value) == B_OK)
		fRecordChannels = (uint32)value;
	float quality;
	if (settings.FindFloat("quality", &quality) == B_OK)
		fQuality = quality;
	const char* folder;
	if (settings.FindString("save_folder", &folder) == B_OK)
		fSaveFolder.SetTo(folder);
	if (settings.FindInt32("autoname", &value) == B_OK)
		fAutoName = value;
	bool flag;
	if (settings.FindBool("show_scope", &flag) == B_OK)
		fShowScope = flag;
	if (settings.FindBool("show_meter", &flag) == B_OK)
		fShowMeter = flag;
	if (settings.FindInt32("time_display", &value) == B_OK)
		fTimeDisplay = value;
	if (settings.FindInt32("input_device", &value) == B_OK)
		fInputDeviceId = (media_client_id)value;
}


void
RecorderWindow::_SaveSettings()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK)
		return;
	path.Append("SoundRecorder");

	BMessage settings;
	settings.AddInt32("encoding", fEncoding);
	settings.AddInt32("sample_format", fSampleFormat);
	settings.AddInt32("rate", (int32)fRecordRate);
	settings.AddInt32("channels", (int32)fRecordChannels);
	settings.AddFloat("quality", fQuality);
	if (fSaveFolder.InitCheck() == B_OK)
		settings.AddString("save_folder", fSaveFolder.Path());
	settings.AddInt32("autoname", fAutoName);
	settings.AddBool("show_scope", fShowScope);
	settings.AddBool("show_meter", fShowMeter);
	settings.AddInt32("time_display", fTimeDisplay);
	settings.AddInt32("input_device", (int32)fInputDeviceId);

	BFile file(path.Path(), O_WRONLY | O_CREAT | O_TRUNC);
	if (file.InitCheck() == B_OK)
		settings.Flatten(&file);
}


static BMenuItem*
_AddOptionItem(BMenu* menu, const char* label, uint32 what,
	const char* field, int32 value)
{
	BMessage* message = new BMessage(what);
	message->AddInt32(field, value);
	BMenuItem* item = new BMenuItem(label, message);
	menu->AddItem(item);
	return item;
}


BMenuBar*
RecorderWindow::_BuildMenuBar()
{
	BMenuBar* bar = new BMenuBar(BRect(0, 0, 0, 0), "MenuBar");

	BMenu* file = new BMenu(B_TRANSLATE("File"));
	file->AddItem(new BMenuItem(B_TRANSLATE("New recording"),
		new BMessage(MENU_NEW), B_COMMAND_KEY, 'n'));
	file->AddItem(new BMenuItem(B_TRANSLATE("Open" B_UTF8_ELLIPSIS),
		new BMessage(MENU_OPEN), B_COMMAND_KEY, 'o'));
	file->AddItem(new BMenuItem(B_TRANSLATE("Save as" B_UTF8_ELLIPSIS),
		new BMessage(MENU_SAVE_AS), B_COMMAND_KEY, 's'));
	file->AddItem(new BMenuItem(B_TRANSLATE("Export"),
		new BMessage(MENU_EXPORT), B_COMMAND_KEY, 'e'));
	file->AddItem(new BMenuItem(B_TRANSLATE("Show in Tracker"),
		new BMessage(MENU_SHOW_TRACKER), B_COMMAND_KEY, 't'));
	file->AddItem(new BMenuItem(B_TRANSLATE("Delete"),
		new BMessage(MENU_FILE_DELETE), B_COMMAND_KEY, B_BACKSPACE));
	file->AddSeparatorItem();
	file->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(B_QUIT_REQUESTED), B_COMMAND_KEY, 'q'));
	bar->AddItem(file);

	BMenu* edit = new BMenu(B_TRANSLATE("Edit"));
	edit->AddItem(new BMenuItem(B_TRANSLATE("Rename" B_UTF8_ELLIPSIS),
		new BMessage(MENU_RENAME), B_COMMAND_KEY, 'r'));
	edit->AddItem(new BMenuItem(B_TRANSLATE("Delete"),
		new BMessage(MENU_EDIT_DELETE), B_COMMAND_KEY, B_BACKSPACE));
	edit->AddItem(new BMenuItem(B_TRANSLATE("Select all"),
		new BMessage(MENU_SELECT_ALL), B_COMMAND_KEY, 'a'));
	bar->AddItem(edit);

	BMenu* recording = new BMenu(B_TRANSLATE("Recording"));
	fInputMenu = new BMenu(B_TRANSLATE("Input"));
	_PopulateInputMenu(fInputMenu);
	recording->AddItem(fInputMenu);

	fFormatMenu = new BMenu(B_TRANSLATE("Format"));
	fWav16Item = _AddOptionItem(fFormatMenu, B_TRANSLATE("WAV 16-bit"),
		MENU_FORMAT, "encoding", B_CODEC_TYPE_WAV);
	fWav16Item->Message()->AddInt32("sample_format",
		BMediaRecorder::B_SAMPLE_FORMAT_S16);
	fWav24Item = _AddOptionItem(fFormatMenu, B_TRANSLATE("WAV 24-bit"),
		MENU_FORMAT, "encoding", B_CODEC_TYPE_WAV);
	fWav24Item->Message()->AddInt32("sample_format",
		BMediaRecorder::B_SAMPLE_FORMAT_S24);
	fWav32Item = _AddOptionItem(fFormatMenu, B_TRANSLATE("WAV 32-bit float"),
		MENU_FORMAT, "encoding", B_CODEC_TYPE_WAV);
	fWav32Item->Message()->AddInt32("sample_format",
		BMediaRecorder::B_SAMPLE_FORMAT_F32);
	fFlacItem = _AddOptionItem(fFormatMenu, B_TRANSLATE("FLAC"),
		MENU_FORMAT, "encoding", B_CODEC_TYPE_FLAC);
	fOggItem = _AddOptionItem(fFormatMenu, B_TRANSLATE("Ogg Vorbis"),
		MENU_FORMAT, "encoding", B_CODEC_TYPE_OGG);
	fOpusItem = _AddOptionItem(fFormatMenu, B_TRANSLATE("Opus"),
		MENU_FORMAT, "encoding", B_CODEC_TYPE_OPUS);
	fMp3Item = _AddOptionItem(fFormatMenu, B_TRANSLATE("MP3"),
		MENU_FORMAT, "encoding", B_CODEC_TYPE_MP3);
	recording->AddItem(fFormatMenu);

	fRateMenu = new BMenu(B_TRANSLATE("Sample rate"));
	fRateDeviceItem = _AddOptionItem(fRateMenu,
		B_TRANSLATE("Device default"), MENU_RATE, "rate", 0);
	fRate44Item = _AddOptionItem(fRateMenu, B_TRANSLATE("44.1 kHz"),
		MENU_RATE, "rate", 44100);
	fRate48Item = _AddOptionItem(fRateMenu, B_TRANSLATE("48 kHz"),
		MENU_RATE, "rate", 48000);
	fRate96Item = _AddOptionItem(fRateMenu, B_TRANSLATE("96 kHz"),
		MENU_RATE, "rate", 96000);
	recording->AddItem(fRateMenu);

	fChannelsMenu = new BMenu(B_TRANSLATE("Channels"));
	fMonoItem = _AddOptionItem(fChannelsMenu, B_TRANSLATE("Mono"),
		MENU_CHANNELS, "channels", 1);
	fStereoItem = _AddOptionItem(fChannelsMenu, B_TRANSLATE("Stereo"),
		MENU_CHANNELS, "channels", 2);
	recording->AddItem(fChannelsMenu);

	fQualityMenu = new BMenu(B_TRANSLATE("Quality"));
	fQualLowItem = new BMenuItem(B_TRANSLATE("Low"),
		new BMessage(MENU_QUALITY));
	fQualLowItem->Message()->AddFloat("quality", 0.25f);
	fQualityMenu->AddItem(fQualLowItem);
	fQualMedItem = new BMenuItem(B_TRANSLATE("Medium"),
		new BMessage(MENU_QUALITY));
	fQualMedItem->Message()->AddFloat("quality", 0.5f);
	fQualityMenu->AddItem(fQualMedItem);
	fQualHighItem = new BMenuItem(B_TRANSLATE("High"),
		new BMessage(MENU_QUALITY));
	fQualHighItem->Message()->AddFloat("quality", 0.85f);
	fQualityMenu->AddItem(fQualHighItem);
	recording->AddItem(fQualityMenu);

	recording->AddItem(new BMenuItem(
		B_TRANSLATE("Default save folder" B_UTF8_ELLIPSIS),
		new BMessage(MENU_SAVE_FOLDER)));

	fAutoNameMenu = new BMenu(B_TRANSLATE("Auto-name pattern"));
	fPatternDateTimeItem = _AddOptionItem(fAutoNameMenu,
		B_TRANSLATE("Date and time"), MENU_AUTONAME, "pattern", 0);
	fPatternDateItem = _AddOptionItem(fAutoNameMenu,
		B_TRANSLATE("Date"), MENU_AUTONAME, "pattern", 1);
	fPatternTimeItem = _AddOptionItem(fAutoNameMenu,
		B_TRANSLATE("Time"), MENU_AUTONAME, "pattern", 2);
	recording->AddItem(fAutoNameMenu);
	bar->AddItem(recording);

	BMenu* view = new BMenu(B_TRANSLATE("View"));
	fScopeItem = new BMenuItem(B_TRANSLATE("Scope"),
		new BMessage(MENU_TOGGLE_SCOPE));
	view->AddItem(fScopeItem);
	fMeterItem = new BMenuItem(B_TRANSLATE("Level meter"),
		new BMessage(MENU_TOGGLE_METER));
	view->AddItem(fMeterItem);
	view->AddSeparatorItem();
	fTimeHmsItem = _AddOptionItem(view, B_TRANSLATE("Time in hh:mm:ss"),
		MENU_TIME_DISPLAY, "mode", 0);
	fTimeSamplesItem = _AddOptionItem(view, B_TRANSLATE("Time in samples"),
		MENU_TIME_DISPLAY, "mode", 1);
	bar->AddItem(view);

	BMenu* help = new BMenu(B_TRANSLATE("Help"));
	help->AddItem(new BMenuItem(B_TRANSLATE("About"),
		new BMessage(MENU_ABOUT)));
	bar->AddItem(help);

	bar->SetTargetForItems(this);
	return bar;
}


void
RecorderWindow::_UpdateRecordingMenus()
{
	fWav16Item->SetEnabled(BMediaRecorder::IsEncodingAvailable(
		B_CODEC_TYPE_WAV));
	fWav24Item->SetEnabled(fWav16Item->IsEnabled());
	fWav32Item->SetEnabled(fWav16Item->IsEnabled());
	fFlacItem->SetEnabled(BMediaRecorder::IsEncodingAvailable(
		B_CODEC_TYPE_FLAC));
	fOggItem->SetEnabled(BMediaRecorder::IsEncodingAvailable(
		B_CODEC_TYPE_OGG));
	fOpusItem->SetEnabled(BMediaRecorder::IsEncodingAvailable(
		B_CODEC_TYPE_OPUS));
	fMp3Item->SetEnabled(BMediaRecorder::IsEncodingAvailable(
		B_CODEC_TYPE_MP3));

	fWav16Item->SetMarked(fEncoding == B_CODEC_TYPE_WAV
		&& fSampleFormat == BMediaRecorder::B_SAMPLE_FORMAT_S16);
	fWav24Item->SetMarked(fEncoding == B_CODEC_TYPE_WAV
		&& fSampleFormat == BMediaRecorder::B_SAMPLE_FORMAT_S24);
	fWav32Item->SetMarked(fEncoding == B_CODEC_TYPE_WAV
		&& fSampleFormat == BMediaRecorder::B_SAMPLE_FORMAT_F32);
	fFlacItem->SetMarked(fEncoding == B_CODEC_TYPE_FLAC);
	fOggItem->SetMarked(fEncoding == B_CODEC_TYPE_OGG);
	fOpusItem->SetMarked(fEncoding == B_CODEC_TYPE_OPUS);
	fMp3Item->SetMarked(fEncoding == B_CODEC_TYPE_MP3);

	fRateDeviceItem->SetMarked(fRecordRate == 0);
	fRate44Item->SetMarked(fRecordRate == 44100);
	fRate48Item->SetMarked(fRecordRate == 48000);
	fRate96Item->SetMarked(fRecordRate == 96000);

	fMonoItem->SetMarked(fRecordChannels == 1);
	fStereoItem->SetMarked(fRecordChannels == 2);

	fQualLowItem->SetMarked(fQuality < 0.4f);
	fQualMedItem->SetMarked(fQuality >= 0.4f && fQuality < 0.7f);
	fQualHighItem->SetMarked(fQuality >= 0.7f);
	bool lossy = fEncoding == B_CODEC_TYPE_OGG
		|| fEncoding == B_CODEC_TYPE_OPUS
		|| fEncoding == B_CODEC_TYPE_MP3;
	fQualityMenu->Superitem()->SetEnabled(lossy);

	fPatternDateTimeItem->SetMarked(fAutoName == 0);
	fPatternDateItem->SetMarked(fAutoName == 1);
	fPatternTimeItem->SetMarked(fAutoName == 2);

	fScopeItem->SetMarked(fShowScope);
	fMeterItem->SetMarked(fShowMeter);
	fTimeHmsItem->SetMarked(fTimeDisplay == 0);
	fTimeSamplesItem->SetMarked(fTimeDisplay == 1);
}


void
RecorderWindow::_ApplyTimeDisplay()
{
	fTrackSlider->SetSamplesMode(fTimeDisplay == 1);
	fTrackSlider->SetSampleRate(fPlayFormat.format.u.raw_audio.frame_rate);
	_UpdateElapsed();
}


void
RecorderWindow::_UpdateElapsed()
{
	if (fElapsedView == NULL)
		return;

	const media_raw_audio_format& raw = fRecordFormat.format.u.raw_audio;
	bigtime_t elapsed = 0;
	if (raw.frame_rate > 0.0f)
		elapsed = (bigtime_t)((double)fRecFrames * 1000000.0
			/ raw.frame_rate);

	char text[64];
	if (fTimeDisplay == 1) {
		snprintf(text, sizeof(text), "%lld", (long long)fRecFrames);
	} else {
		uint32 seconds = (uint32)(elapsed / 1000000LL);
		snprintf(text, sizeof(text), "%02" B_PRIu32 ":%02" B_PRIu32
			":%02" B_PRIu32, seconds / 3600, (seconds / 60) % 60,
			seconds % 60);
	}
	fElapsedView->SetText(text);
}


BMediaFormat
RecorderWindow::_MakeEncodedFormat()
{
	BMediaFormat encoded;
	encoded.format.type = B_MEDIA_ENCODED_AUDIO;
	memset(&encoded.format.u.encoded_audio, 0,
		sizeof(encoded.format.u.encoded_audio));
	encoded.format.u.encoded_audio.encoding = fEncoding;
	return encoded;
}


void
RecorderWindow::_AutoName(char* name, size_t size)
{
	time_t now = time(NULL);
	struct tm nowParts;
	localtime_r(&now, &nowParts);
	const char* format = "Recording %Y-%m-%d %H-%M-%S";
	switch (fAutoName) {
		case 1:
			format = "Recording %Y-%m-%d";
			break;
		case 2:
			format = "Recording %H-%M-%S";
			break;
		default:
			break;
	}
	strftime(name, size, format, &nowParts);
}


void
RecorderWindow::_OpenPanel()
{
	if (fOpenPanel == NULL) {
		fOpenPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this),
			NULL, B_FILE_NODE, true, new BMessage(MENU_OPEN));
	} else {
		fOpenPanel->SetMessage(new BMessage(MENU_OPEN));
	}
	fOpenPanel->Show();
}


void
RecorderWindow::_FolderPanel()
{
	if (fFolderPanel == NULL) {
		fFolderPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this),
			NULL, B_DIRECTORY_NODE, false, new BMessage(MENU_SAVE_FOLDER));
	} else {
		fFolderPanel->SetMessage(new BMessage(MENU_SAVE_FOLDER));
	}
	fFolderPanel->Show();
}


void
RecorderWindow::_DeleteSelected()
{
	int32 index = fSoundList->CurrentSelection();
	SoundListItem* item
		= dynamic_cast<SoundListItem*>(fSoundList->ItemAt(index));
	if (item == NULL)
		return;
	if (fButtonState == btnPlaying)
		StopPlaying();
	item->Entry().Remove();
	RemoveCurrentSoundItem();
	UpdateButtons();
}


void
RecorderWindow::_RenameSelected()
{
	int32 index = fSoundList->CurrentSelection();
	SoundListItem* item
		= dynamic_cast<SoundListItem*>(fSoundList->ItemAt(index));
	if (item == NULL)
		return;

	char oldName[B_FILE_NAME_LENGTH];
	item->Entry().GetName(oldName);

	BAlert* alert = new BAlert(B_TRANSLATE("Rename"),
		B_TRANSLATE("New name:"), B_TRANSLATE("Rename"),
		B_TRANSLATE("Cancel"), NULL, B_WIDTH_AS_USUAL, B_IDEA_ALERT);
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	float width = alert->Bounds().Width() - 20;
	BTextControl* text = new BTextControl(BRect(10, 35, 10 + width, 60),
		"name", NULL, oldName, NULL);
	text->ResizeTo(width, text->Bounds().Height());
	alert->AddChild(text);

	if (alert->Go() != 0)
		return;

	const char* newName = text->Text();
	if (newName != NULL && newName[0] != '\0'
			&& strcmp(newName, oldName) != 0) {
		item->Entry().Rename(newName);
		char name[B_FILE_NAME_LENGTH];
		item->Entry().GetName(name);
		item->SetText(name);
		fSoundList->Invalidate();
	}
}


void
RecorderWindow::_ShowInTracker()
{
	int32 index = fSoundList->CurrentSelection();
	SoundListItem* item
		= dynamic_cast<SoundListItem*>(fSoundList->ItemAt(index));
	if (item == NULL)
		return;

	entry_ref ref;
	if (item->Entry().GetRef(&ref) != B_OK)
		return;

	BMessage message(B_REFS_RECEIVED);
	message.AddRef("refs", &ref);
	be_roster->Launch("application/x-vnd.Be-TRAK", &message);
}


void
RecorderWindow::_ExportSelected()
{
	int32 index = fSoundList->CurrentSelection();
	SoundListItem* item
		= dynamic_cast<SoundListItem*>(fSoundList->ItemAt(index));
	if (item == NULL || item->Entry().InitCheck() != B_OK)
		return;

	if (fSaveFolder.InitCheck() != B_OK) {
		BPath home;
		if (find_directory(B_USER_DIRECTORY, &home, true) == B_OK) {
			home.Append("Music");
			home.Append("Recordings");
			fSaveFolder = home;
		} else
			fSaveFolder.SetTo("/tmp");
	}
	create_directory(fSaveFolder.Path(), 0755);
	BDirectory dir(fSaveFolder.Path());
	if (dir.InitCheck() != B_OK) {
		ErrorAlert(B_TRANSLATE("Cannot open the save folder"),
			dir.InitCheck());
		return;
	}

	char name[B_FILE_NAME_LENGTH];
	_AutoName(name, sizeof(name));
	snprintf(name, sizeof(name), "%s.%s", name,
		_ExtensionForEncoding(fEncoding));

	status_t err = _ReEncode(item, dir, name);
	if (err < B_OK) {
		ErrorAlert(B_TRANSLATE("Cannot export the recording"), err);
		return;
	}

	BEntry entry(&dir, name);
	if (entry.InitCheck() == B_OK)
		AddSoundItem(entry, false);
	UpdateButtons();
}


status_t
RecorderWindow::_ReEncode(SoundListItem* item, const BDirectory& dir,
	const char* name)
{
	if (item == NULL || name == NULL)
		return B_BAD_VALUE;

	entry_ref ref;
	if (item->Entry().GetRef(&ref) != B_OK)
		return B_ERROR;

	BMediaFile source(&ref);
	status_t err = source.InitCheck();
	if (err < B_OK)
		return err;

	BMediaTrack* in = NULL;
	BMediaFormat raw;
	for (int32 i = 0; i < source.CountTracks(); i++) {
		BMediaTrack* track = source.TrackAt(i);
		if (track == NULL)
			continue;
		raw.SetToDefault();
		raw.format.type = B_MEDIA_RAW_AUDIO;
		if (track->DecodedFormat(&raw) == B_OK
				&& raw.format.type == B_MEDIA_RAW_AUDIO) {
			in = track;
			break;
		}
		source.ReleaseTrack(track);
	}
	if (in == NULL)
		return B_STREAM_NOT_FOUND;

	// Create the destination file first so it can be referenced.
	BFile created;
	if (dir.CreateFile(name, &created) != B_OK)
		return B_ERROR;
	created.Unset();
	entry_ref outRef;
	BEntry outEntry(&dir, name);
	if (outEntry.GetRef(&outRef) != B_OK)
		return B_ERROR;

	media_file_format fileFormat;
	_FillFileFormat(&fileFormat, fEncoding);
	BMediaFile destination(&outRef, &fileFormat, 0);
	err = destination.InitCheck();
	if (err < B_OK)
		return err;

	BMediaFormat encoded = _MakeEncodedFormat();
	encoded.format.u.encoded_audio.output
		= raw.format.u.raw_audio;
	BMediaTrack* out = destination.CreateTrack(encoded);
	if (out == NULL)
		return B_ERROR;

	err = destination.CommitHeader();
	if (err < B_OK)
		return err;

	const media_raw_audio_format& audio = raw.format.u.raw_audio;
	const uint32 stride = (audio.format
		& media_raw_audio_format::B_AUDIO_SIZE_MASK) * audio.channel_count;
	if (stride == 0) {
		destination.CloseFile();
		return B_ERROR;
	}

	const int32 bufferFrames = 4096;
	char* buffer = (char*)malloc((size_t)bufferFrames * stride);
	if (buffer == NULL) {
		destination.CloseFile();
		return B_NO_MEMORY;
	}

	int64 remaining = in->CountFrames();
	while (remaining > 0) {
		int64 frames = MIN(remaining, (int64)bufferFrames);
		err = in->ReadFrames(buffer, &frames);
		if (err < B_OK || frames <= 0)
			break;
		err = out->WriteFrames(buffer, frames);
		if (err < B_OK)
			break;
		remaining -= frames;
	}
	free(buffer);
	out->Flush();
	status_t closeErr = destination.CloseFile();
	if (err >= B_OK)
		err = closeErr;
	return err;
}


void
RecorderWindow::_About()
{
	BAlert* alert = new BAlert(B_TRANSLATE("About SoundRecorder"),
		B_TRANSLATE("SoundRecorder\n\nRecords and plays audio files.\n"
			"Part of V\\OS."),
		B_TRANSLATE("OK"));
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
}
