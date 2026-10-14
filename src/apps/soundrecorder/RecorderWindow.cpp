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
#include <MenuField.h>
#include <PopUpMenu.h>
#include <MenuItem.h>
#include <Box.h>
#include <ScrollView.h>
#include <Beep.h>
#include <StringView.h>
#include <String.h>
#include <Slider.h>
#include <Message.h>
#include <Messenger.h>

#include <Path.h>
#include <FindDirectory.h>
#include <MimeType.h>
#include <Node.h>

#include <assert.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
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
	fInitCheck(B_OK)
{
	fRecordButton = NULL;
	fPlayButton = NULL;
	fStopButton = NULL;
	fSaveButton = NULL;
	fLoopButton = NULL;
	fInputField = NULL;
	fRecorder = NULL;
	fRecording = false;
	fRecFrames = 0;
	fLastReport = 0;
	fBusy = false;
	fTempCount = -1;
	fButtonState = btnPaused;
	fInputDeviceId = 0;
	fHasConnectedSource = false;

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
RecorderWindow::_PopulateInputMenu()
{
	BPopUpMenu* popup = new BPopUpMenu(B_TRANSLATE("Input"));

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL) {
		popup->AddItem(new BMenuItem(B_TRANSLATE("No inputs"), NULL));
		fInputField = new BMenuField(BRect(0, 0, 100, 20), "Input",
			B_TRANSLATE("Input:"), popup);
		return;
	}

	BObjectList<media_client_id, true> clients;
	if (graph->GetClients(&clients) != B_OK) {
		popup->AddItem(new BMenuItem(B_TRANSLATE("No inputs"), NULL));
		fInputField = new BMenuField(BRect(0, 0, 100, 20), "Input",
			B_TRANSLATE("Input:"), popup);
		return;
	}

	media_client_id defaultId = 0;
	graph->GetDefaultAudioInput(&defaultId);

	const char* selectedName = B_TRANSLATE("Default input");
	BObjectList<media_client_id, true> sources(true);
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
		sources.AddItem(new media_client_id(id));

		BString name;
		if (info.FindString("name", &name) != B_OK
				|| name.Length() == 0)
			info.FindString("node_name", &name);
		if (name.Length() == 0)
			name.SetTo(B_TRANSLATE("Input"));

		BMessage* msg = new BMessage(INPUT_SELECTED);
		msg->AddInt32("device", id);
		BMenuItem* item = new BMenuItem(name.String(), msg);
		popup->AddItem(item);
		if (id == defaultId) {
			selectedName = name.String();
			item->SetMarked(true);
		}
	}

	if (sources.CountItems() == 0)
		popup->AddItem(new BMenuItem(B_TRANSLATE("No inputs"), NULL));

	fInputField = new BMenuField(BRect(0, 0, 100, 20), "Input",
		B_TRANSLATE("Input:"), popup);
	if (fInputField->Menu() != NULL && fInputField->Menu()->Superitem() != NULL)
		fInputField->Menu()->Superitem()->SetLabel(selectedName);
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

		//	Create the window header with controls
		BRect r(Bounds());
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
		r.bottom = r.top + 104;
		fScopeView = new ScopeView(r, B_FOLLOW_LEFT_RIGHT|B_FOLLOW_TOP);
		background->AddChild(fScopeView);

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
		r.top = background->Bounds().bottom + 1;
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

		//	Input selection lists physical sources from the media2 graph.
		_PopulateInputMenu();

		//	Create the actual widget
		r = fFileInfoBox->Bounds();
		r.top = r.bottom + 2;
		r.bottom = r.top + 18;
		r.InsetBy(10, 10);
		fBottomBox->AddChild(fInputField);
		fInputField->SetDivider(fInputField->StringWidth(B_TRANSLATE("Input:"))
			+ 4.0f);

		fBottomBox->AddChild(fFileInfoBox);

		fBottomBox->Hide();
		CalcSizes(MIN_WIDTH, MIN_HEIGHT);
		ResizeTo(Frame().Width(), MIN_HEIGHT);

		// Make sure the save panel is happy.
		fSavePanel = new BFilePanel(B_SAVE_PANEL);
		fSavePanel->SetTarget(this);
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
			CalcSizes(MIN_WIDTH, fDeployedHeight);
			ResizeTo(Frame().Width(), fDeployedHeight);
		} else {
			fBottomBox->Hide();
			CalcSizes(MIN_WIDTH, MIN_HEIGHT);
			ResizeTo(Frame().Width(), MIN_HEIGHT);

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

	// Encode the take through the recorder's GStreamer output pipeline.
	BPath recPath;
	err = fRecEntry.GetPath(&recPath);
	if (err < B_OK) {
		ErrorAlert(B_TRANSLATE("Cannot resolve the recording path"), err);
		fRecEntry.Remove();
		fRecEntry.Unset();
		return;
	}
	BMediaFormat encoded;
	encoded.format.type = B_MEDIA_ENCODED_AUDIO;
	encoded.format.u.encoded_audio.encoding = B_CODEC_TYPE_WAV;
	err = fRecorder->SetOutputFile(recPath.Path(), encoded);
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
	char filename[B_FILE_NAME_LENGTH];
	pItem->Entry().GetName(filename);
	BMessage saveMsg(B_SAVE_REQUESTED);
	entry_ref ref;
	pItem->Entry().GetRef(&ref);

	if (saveMsg.AddPointer("sound list item", pItem) != B_OK)
		fprintf(stderr, "failed to add pItem\n");
	fSavePanel->SetSaveText(filename);
	fSavePanel->SetMessage(&saveMsg);
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
	//	User selected input from pop-up
	int32 id = 0;
	if (message->FindInt32("device", &id) != B_OK)
		return;
	fInputDeviceId = (media_client_id)id;
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
		fInputField->SetEnabled(false);
		return;
	}
	fRecordButton->SetEnabled(fButtonState != btnPlaying);
	fPlayButton->SetEnabled((fButtonState != btnRecording) && hasSelection);
	fRewindButton->SetEnabled((fButtonState != btnRecording) && hasSelection);
	fForwardButton->SetEnabled((fButtonState != btnRecording) && hasSelection);
	fStopButton->SetEnabled(fButtonState != btnPaused);
	fSaveButton->SetEnabled(hasSelection && (fButtonState != btnRecording));
	fInputField->SetEnabled(fButtonState != btnRecording);
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
