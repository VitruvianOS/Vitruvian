/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "CameraWindow.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <Application.h>
#include <Autolock.h>
#include <Bitmap.h>
#include <Button.h>
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Size.h>
#include <Slider.h>
#include <StringView.h>

#include <media2/FilterCatalog.h>
#include <media2/MediaGraph.h>
#include <media2/MediaRecorder.h>
#include <media2/PlaySound.h>
#include <media2/VideoFilter.h>

#include "AudioCapture.h"
#include "CameraApp.h"
#include "CameraView.h"
#include "PhotoWriter.h"


static const uint32 kMsgModeChanged = 'MODE';
static const uint32 kMsgCameraChanged = 'CAMS';
static const uint32 kMsgResChanged = 'RESS';
static const uint32 kMsgFilterChanged = 'FILT';
static const uint32 kMsgMicChanged = 'MICS';
static const uint32 kMsgShutter = 'SHUT';
static const uint32 kMsgZoomChanged = 'ZOOM';
static const uint32 kMsgBrightChanged = 'BRGT';
static const uint32 kMsgContrastChanged = 'CNTR';
static const uint32 kMsgSaturChanged = 'SATR';
static const uint32 kMsgThumbClicked = 'THMB';
static const uint32 kMsgRecordTick = 'TICK';
static const uint32 kMsgCaptureNow = 'CAPN';
static const uint32 kMsgBurstStep = 'BURS';

// Capture sizes offered in the menu; the device may settle on another one.
static const struct {
	uint32	width;
	uint32	height;
	float	rate;
} kResolutions[] = {
	{ 640, 480, 30.0f },
	{ 1280, 720, 30.0f },
	{ 1920, 1080, 30.0f },
	{ 320, 240, 30.0f }
};
static const int32 kResolutionCount
	= sizeof(kResolutions) / sizeof(kResolutions[0]);

// A device that ignores the requested size is followed at most this often.
static const int32 kMaxReopens = 2;


static BMediaFormat
_RawVideoFormat(uint32 width, uint32 height, float rate)
{
	BMediaFormat format;
	format.SetToDefault();
	format.format.type = B_MEDIA_RAW_VIDEO;
	media_raw_video_format& raw = format.format.u.raw_video;
	raw.field_rate = rate;
	raw.width = width;
	raw.height = height;
	raw.color_space = B_RGB32;
	return format;
}


static BMediaFormat
_RawAudioFormat()
{
	BMediaFormat format;
	format.SetToDefault();
	format.format.type = B_MEDIA_RAW_AUDIO;
	media_raw_audio_format& raw = format.format.u.raw_audio;
	raw.frame_rate = 48000.0f;
	raw.channel_count = 2;
	raw.format = media_raw_audio_format::B_AUDIO_SHORT;
	raw.byte_order = B_MEDIA_HOST_ENDIAN;
	raw.buffer_size = 4096;
	return format;
}


static BMediaFormat
_Vp8Encoding()
{
	BMediaFormat format;
	format.SetToDefault();
	format.format.type = B_MEDIA_ENCODED_VIDEO;
	format.format.u.encoded_video.encoding = B_CODEC_TYPE_VP8;
	return format;
}


static BMediaFormat
_OpusEncoding()
{
	BMediaFormat format;
	format.SetToDefault();
	format.format.type = B_MEDIA_ENCODED_AUDIO;
	format.format.u.encoded_audio.encoding = B_CODEC_TYPE_OPUS;
	return format;
}


// End of the chain: hands the frames that survive the filter to the window.
class CameraPreviewTap : public BMediaClient {
public:
								CameraPreviewTap(CameraWindow* window)
									:
									BMediaClient("preview", B_MEDIA_RECORDER),
									fInput(NULL),
									fWindow(window)
	{
		fInput = new(std::nothrow) _Input(this);
		if (fInput != NULL)
			RegisterInput(fInput);
	}

	virtual						~CameraPreviewTap()
	{
		if (fInput != NULL) {
			UnregisterInput(fInput);
			delete fInput;
		}
	}

	virtual	bool				IsInProcessRouted() const { return true; }

			BMediaInput*		Input() const { return fInput; }

protected:
	virtual	status_t			_StartConnections(void* /*backend*/)
	{
		return B_OK;
	}

private:
	class _Input : public BMediaInput {
	public:
								_Input(CameraPreviewTap* owner)
									:
									BMediaConnection(B_MEDIA_INPUT, "in"),
									BMediaInput("in"),
									fOwner(owner)
		{
			SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_VIDEO));
		}

		virtual					~_Input() {}

	protected:
		virtual status_t		AcceptFormat(BMediaFormat* format)
		{
			if (format == NULL || !format->IsRawVideo())
				return B_BAD_VALUE;
			return B_OK;
		}

		virtual void			HandleBuffer(void* buffer, size_t size,
									const BMediaFormat& format)
		{
			if (!format.IsRawVideo())
				return;
			fOwner->fWindow->HandleFrame((const uint8*)buffer, size,
				format.format.u.raw_video.width,
				format.format.u.raw_video.height);
		}

	private:
		CameraPreviewTap*		fOwner;
	};

			_Input*				fInput;
			CameraWindow*		fWindow;
};


static const char*
_LocationLabel(camera_location location)
{
	switch (location) {
		case B_CAMERA_LOCATION_FRONT:
			return "Front";
		case B_CAMERA_LOCATION_BACK:
			return "Back";
		case B_CAMERA_LOCATION_EXTERNAL:
			return "External";
		default:
			return "Camera";
	}
}


/* static */ status_t
CameraWindow::_TimerEntry(void* data)
{
	return ((CameraWindow*)data)->_RunTimer();
}


CameraWindow::CameraWindow(BRect frame)
	:
	BWindow(frame, "Camera", B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE),
	fView(NULL),
	fStatusLabel(NULL),
	fShutterButton(NULL),
	fThumbButton(NULL),
	fCameraField(NULL),
	fModeField(NULL),
	fResolutionField(NULL),
	fFilterField(NULL),
	fMicField(NULL),
	fModeMenu(NULL),
	fCameraMenu(NULL),
	fResolutionMenu(NULL),
	fFilterMenu(NULL),
	fMicMenu(NULL),
	fZoomSlider(NULL),
	fBrightnessSlider(NULL),
	fContrastSlider(NULL),
	fSaturationSlider(NULL),
	fSource(NULL),
	fSplitter(NULL),
	fFilter(NULL),
	fRecorder(NULL),
	fAudio(NULL),
	fTap(NULL),
	fFrameLock("camera frames"),
	fLastFrame(NULL),
	fCameras(NULL),
	fCameraIndex(0),
	fCameraStableId(),
	fFilterId("none"),
	fMicId(0),
	fMode(0),
	fTimerDelay(0),
	fReopens(0),
	fTicker(NULL),
	fRecording(false),
	fBurstCancel(false),
	fRecordStart(0),
	fWidth(640),
	fHeight(480),
	fFrameRate(30.0f),
	fGrid(false),
	fMirrorEnabled(true),
	fQuitRequested(false)
{
	fCameras = new BObjectList<camera_device_info, true>();

	const float kControl = 48;	// touch-sized

	fView = new CameraView(BRect(0, 0, 640, 480), "preview");

	fModeMenu = new BPopUpMenu("mode");
	static const char* const kModeNames[] = { "Photo", "Video", "Burst",
		"Timer 3s", "Timer 10s" };
	for (int32 i = 0; i < 5; i++) {
		BMessage* message = new BMessage(kMsgModeChanged);
		message->AddInt32("index", i);
		fModeMenu->AddItem(new BMenuItem(kModeNames[i], message));
	}
	fModeMenu->SetTargetForItems(this);
	fModeMenu->ItemAt(0)->SetMarked(true);
	fModeField = new BMenuField("mode", "Mode", fModeMenu);

	fCameraMenu = new BPopUpMenu("camera");
	fCameraField = new BMenuField("camera", "Camera", fCameraMenu);

	fResolutionMenu = new BPopUpMenu("resolution");
	fResolutionField = new BMenuField("res", "Resolution", fResolutionMenu);

	fFilterMenu = new BPopUpMenu("filter");
	fFilterField = new BMenuField("filter", "Filter", fFilterMenu);

	fMicMenu = new BPopUpMenu("mic");
	fMicField = new BMenuField("mic", "Microphone", fMicMenu);

	fZoomSlider = new BSlider("zoom", "Zoom",
		new BMessage(kMsgZoomChanged), 10, 40, B_HORIZONTAL);
	fZoomSlider->SetValue(10);

	fBrightnessSlider = new BSlider("brightness", "Brightness",
		new BMessage(kMsgBrightChanged), -50, 50, B_HORIZONTAL);
	fContrastSlider = new BSlider("contrast", "Contrast",
		new BMessage(kMsgContrastChanged), -50, 50, B_HORIZONTAL);
	fSaturationSlider = new BSlider("saturation", "Saturation",
		new BMessage(kMsgSaturChanged), -50, 50, B_HORIZONTAL);
	fBrightnessSlider->SetEnabled(false);
	fContrastSlider->SetEnabled(false);
	fSaturationSlider->SetEnabled(false);

	fShutterButton = new BButton("shutter", "Capture",
		new BMessage(kMsgShutter));
	fShutterButton->SetExplicitMinSize(BSize(4 * kControl, kControl));

	fThumbButton = new BButton("thumb", "Last", new BMessage(kMsgThumbClicked));
	fThumbButton->SetExplicitMinSize(BSize(2 * kControl, kControl));
	fThumbButton->SetEnabled(false);

	fStatusLabel = new BStringView("status", "Starting...");

	fView->SetExplicitMinSize(BSize(320, 240));
	fView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));

	BLayoutBuilder::Group<>(this, B_HORIZONTAL, 0)
		.Add(fView, 3.0f)
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING, 1.0f)
			.SetInsets(B_USE_WINDOW_SPACING)
			.AddGrid(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING)
				.AddMenuField(fCameraField, 0, 0)
				.AddMenuField(fModeField, 0, 1)
				.AddMenuField(fResolutionField, 0, 2)
				.AddMenuField(fFilterField, 0, 3)
				.AddMenuField(fMicField, 0, 4)
			.End()
			.Add(fZoomSlider)
			.Add(fBrightnessSlider)
			.Add(fContrastSlider)
			.Add(fSaturationSlider)
			.AddGlue()
			.Add(fStatusLabel)
			.AddGroup(B_HORIZONTAL)
				.Add(fShutterButton, 3.0f)
				.Add(fThumbButton, 2.0f)
			.End()
		.End();

	_BuildGraph();
	_RefreshFilterMenu();
	_LoadSettings();

}


CameraWindow::~CameraWindow()
{
	delete fTicker;
	delete fSplitter;
	delete fFilter;
	delete fRecorder;
	delete fAudio;
	delete fSource;
	delete fTap;
	delete fLastFrame;
	delete fCameras;
}


bool
CameraWindow::QuitRequested()
{
	fQuitRequested = true;
	fBurstCancel = true;
	if (fSource != NULL)
		fSource->SetFrameTarget(BMessenger());
	if (fRecording)
		_StopRecording();
	if (fSource != NULL)
		fSource->Stop();
	_DestroyChain();
	_StoreSettings();
	((CameraApp*)be_app)->SaveSettings();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
CameraWindow::_BuildGraph()
{
	fSource = new BCameraSource("camera");
	fSplitter = new BVideoSplitter();
	fRecorder = new BMediaRecorder("camera-recorder", B_MEDIA_RAW_VIDEO);
	fAudio = new AudioCapture("camera-mic");
	fTap = new CameraPreviewTap(this);

	fRecorder->SetFormat(_RawVideoFormat(fWidth, fHeight, fFrameRate));
	fRecorder->SetAudioFormat(_RawAudioFormat());
}


void
CameraWindow::_RebuildChain()
{
	if (fSource == NULL || fSplitter == NULL)
		return;

	_DestroyChain();

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return;

	BMediaOutput* head = fSource->OutputAt(0);
	if (fFilter != NULL && fFilter->InitCheck() == B_OK
		&& graph->Connect(head, fFilter->Input()) == B_OK) {
		head = fFilter->Output();
	}

	graph->Connect(head, fSplitter->Input());

	BMediaOutput* tapOutput = fSplitter->AddOutput();
	if (tapOutput != NULL)
		graph->Connect(tapOutput, fTap->Input());
}


static void
_Unlink(BMediaGraph* graph, BMediaOutput* output)
{
	if (output == NULL || !output->IsConnected())
		return;
	BMediaInput* input = dynamic_cast<BMediaInput*>(output->Binding());
	if (input != NULL)
		graph->Disconnect(output, input);
}


void
CameraWindow::_DestroyChain()
{
	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return;

	if (fSource != NULL)
		_Unlink(graph, fSource->OutputAt(0));
	if (fFilter != NULL)
		_Unlink(graph, fFilter->Output());
	if (fSplitter == NULL)
		return;

	while (fSplitter->CountOutputs() > 0) {
		BMediaOutput* output = fSplitter->OutputAt(0);
		if (output == NULL)
			break;
		fSplitter->RemoveOutput(output);
	}
}


void
CameraWindow::HandleFrame(const uint8* bits, size_t size, uint32 width,
	uint32 height)
{
	if (bits == NULL || width == 0 || height == 0 || fQuitRequested)
		return;

	{
		BAutolock _(fFrameLock);
		if (fLastFrame == NULL
			|| (uint32)fLastFrame->Bounds().Width() + 1 != width
			|| (uint32)fLastFrame->Bounds().Height() + 1 != height) {
			delete fLastFrame;
			fLastFrame = new BBitmap(BRect(0, 0, width - 1, height - 1),
				B_RGB32);
			if (fLastFrame == NULL || fLastFrame->InitCheck() != B_OK) {
				delete fLastFrame;
				fLastFrame = NULL;
				return;
			}
		}
		const size_t frameSize = (size_t)fLastFrame->BytesPerRow() * height;
		if (size >= frameSize)
			memcpy(fLastFrame->Bits(), bits, frameSize);
	}

	if (fView != NULL)
		fView->SetFrame(bits, size, width, height);
}


// Frame notifications carry the size the device settled on; follow it so
// the filter and recorder are built for the real frames.
void
CameraWindow::_FrameArrived(BMessage* message)
{
	int32 width = 0;
	int32 height = 0;
	if (fRecording || fReopens >= kMaxReopens
		|| message->FindInt32("width", &width) != B_OK
		|| message->FindInt32("height", &height) != B_OK
		|| width <= 0 || height <= 0) {
		return;
	}
	if ((uint32)width == fWidth && (uint32)height == fHeight)
		return;

	fWidth = (uint32)width;
	fHeight = (uint32)height;
	float rate = 0;
	if (message->FindFloat("field_rate", &rate) == B_OK && rate > 0)
		fFrameRate = rate;
	fReopens++;
	_OpenCamera(fCameraIndex);
}




void
CameraWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_CAMERA_FRAME:
			_FrameArrived(message);
			break;

		case kMsgModeChanged:
		{
			int32 index = 0;
			message->FindInt32("index", &index);
			_SetMode(index);
			break;
		}

		case kMsgCameraChanged:
		{
			int32 index = -1;
			if (message->FindInt32("index", &index) == B_OK && !fRecording) {
				fCameraIndex = index;
				fReopens = 0;
				_OpenCamera(fCameraIndex);
				_StoreSettings();
			}
			break;
		}

		case kMsgResChanged:
		{
			int32 index = -1;
			message->FindInt32("index", &index);
			if (index < 0 || index >= kResolutionCount || fRecording)
				break;
			fWidth = kResolutions[index].width;
			fHeight = kResolutions[index].height;
			fFrameRate = kResolutions[index].rate;
			fReopens = 0;
			_OpenCamera(fCameraIndex);
			_StoreSettings();
			break;
		}

		case kMsgFilterChanged:
		{
			BString id;
			if (message->FindString("id", &id) == B_OK && !fRecording) {
				fFilterId = id;
				_SetFilter(id.String());
			}
			_StoreSettings();
			break;
		}

		case kMsgMicChanged:
		{
			int32 device = 0;
			message->FindInt32("device", &device);
			if (!fRecording)
				fMicId = (media_client_id)device;
			_StoreSettings();
			break;
		}

		case kMsgShutter:
			_ShutterPressed();
			break;

		case kMsgZoomChanged:
			fView->SetZoom(fZoomSlider->Value() / 10.0f);
			break;

		case kMsgBrightChanged:
		case kMsgContrastChanged:
		case kMsgSaturChanged:
		{
			BVideoBalanceFilter* balance
				= dynamic_cast<BVideoBalanceFilter*>(fFilter);
			if (balance != NULL) {
				balance->SetBrightness(fBrightnessSlider->Value() / 100.0f);
				balance->SetContrast(1.0f + fContrastSlider->Value() / 100.0f);
				balance->SetSaturation(
					1.0f + fSaturationSlider->Value() / 100.0f);
			}
			break;
		}

		case kMsgThumbClicked:
		{
			entry_ref ref;
			if (fLastPath.Path() != NULL
				&& get_ref_for_path(fLastPath.Path(), &ref) == B_OK) {
				be_roster->Launch(&ref);
			}
			break;
		}

		case kMsgRecordTick:
			if (fRecording) {
				char text[64];
				const bigtime_t elapsed = system_time() - fRecordStart;
				snprintf(text, sizeof(text), "REC %02lld:%02lld",
					(long long)(elapsed / 60000000LL),
					(long long)((elapsed / 1000000LL) % 60));
				fStatusLabel->SetText(text);
			}
			break;

		case kMsgCameraTap:
		{
			// Tap-to-focus: toggling autofocus makes the device search
			// again, and exposure follows in auto mode.
			if (fSource == NULL)
				break;
			if (fSource->ControlSupported(B_CAMERA_PARAM_FOCUS_AUTO) != B_OK) {
				fStatusLabel->SetText("Focus not supported");
				break;
			}
			fSource->SetControl(B_CAMERA_PARAM_FOCUS_AUTO, 0.0f);
			fSource->SetControl(B_CAMERA_PARAM_FOCUS_AUTO, 1.0f);
			if (fSource->ControlSupported(B_CAMERA_PARAM_EXPOSURE_AUTO)
					== B_OK) {
				fSource->SetControl(B_CAMERA_PARAM_EXPOSURE_AUTO, 1.0f);
			}
			fStatusLabel->SetText("Focusing...");
			break;
		}

		case kMsgCaptureNow:
			_CapturePhoto();
			break;

		case kMsgBurstStep:
		{
			int32 step = 0;
			message->FindInt32("step", &step);
			_CapturePhoto();
			if (step < 4 && !fBurstCancel && !fQuitRequested) {
				BMessage next(kMsgBurstStep);
				next.AddInt32("step", step + 1);
				PostMessage(&next, this);
			}
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
CameraWindow::_SetMode(int32 mode)
{
	if (fRecording)
		_StopRecording();

	fMode = mode;
	fTimerDelay = 0;
	switch (mode) {
		case 0:
			fShutterButton->SetLabel("Capture");
			fStatusLabel->SetText("Photo");
			break;
		case 1:
			fShutterButton->SetLabel("Record");
			fStatusLabel->SetText("Video");
			break;
		case 2:
			fShutterButton->SetLabel("Burst");
			fStatusLabel->SetText("Burst");
			break;
		case 3:
			fTimerDelay = 3;
			fShutterButton->SetLabel("Timer 3s");
			fStatusLabel->SetText("Timer 3s");
			break;
		case 4:
			fTimerDelay = 10;
			fShutterButton->SetLabel("Timer 10s");
			fStatusLabel->SetText("Timer 10s");
			break;
		default:
			break;
	}
	_StoreSettings();
}


void
CameraWindow::_SetFilter(const char* filterId)
{
	const bool wasStarted = fSource != NULL && fSource->IsStarted();
	if (wasStarted)
		fSource->Stop();
	_DestroyChain();

	delete fFilter;
	fFilter = NULL;

	if (filterId != NULL && strcmp(filterId, "none") != 0) {
		BObjectList<BFilterCatalog::Entry, true> entries;
		if (BFilterCatalog::ListEntries(&entries, "video") == B_OK) {
			for (int32 i = 0; i < entries.CountItems(); i++) {
				BFilterCatalog::Entry* entry = entries.ItemAt(i);
				if (entry->id == filterId) {
					fFilter = BFilterCatalog::CreateVideoUnit(*entry);
					break;
				}
			}
		}
	}

	_RebuildChain();
	if (wasStarted)
		fSource->Start();

	const bool balance = dynamic_cast<BVideoBalanceFilter*>(fFilter) != NULL;
	fBrightnessSlider->SetEnabled(balance);
	fContrastSlider->SetEnabled(balance);
	fSaturationSlider->SetEnabled(balance);
}


status_t
CameraWindow::_OpenCamera(int32 index)
{
	if (fSource == NULL)
		return B_NO_INIT;

	fSource->Stop();
	_DestroyChain();

	if (fCameras == NULL || index < 0 || index >= fCameras->CountItems()) {
		fStatusLabel->SetText("No camera found");
		return B_ENTRY_NOT_FOUND;
	}

	camera_device_info* info = fCameras->ItemAt(index);
	status_t status = fSource->SetToDevice(info->id);
	if (status != B_OK) {
		fStatusLabel->SetText("Camera unavailable");
		return status;
	}
	fCameraStableId = fSource->StableId();
	fSource->SetMode(fWidth, fHeight, fFrameRate);
	fSource->SetFrameTarget(BMessenger(this));
	fRecorder->SetFormat(_RawVideoFormat(fWidth, fHeight, fFrameRate));

	_RebuildChain();
	status = fSource->Start();
	if (status != B_OK) {
		fStatusLabel->SetText("Camera start failed");
		return status;
	}

	fView->SetMirrored(fMirrorEnabled
		&& fSource->Location() == B_CAMERA_LOCATION_FRONT);
	return B_OK;
}


void
CameraWindow::_RefreshCameraMenu()
{
	while (fCameraMenu->CountItems() > 0)
		delete fCameraMenu->RemoveItem(0);
	fCameras->MakeEmpty();

	BMediaGraph* graph = BMediaGraph::Instance();
	BObjectList<media_client_id, true> clients;
	media_client_id defaultId = 0;
	if (graph != NULL) {
		graph->GetDefaultCamera(&defaultId);
		graph->GetClients(&clients);
	}

	for (int32 i = 0; i < clients.CountItems(); i++) {
		const media_client_id id = *clients.ItemAt(i);
		BMessage info;
		bool isVideo = false;
		if (graph->GetClientInfo(id, &info) != B_OK
			|| info.FindBool("is.video", &isVideo) != B_OK || !isVideo) {
			continue;
		}

		camera_device_info* device = new camera_device_info();
		device->id = id;
		info.FindString("name", &device->name);
		info.FindString("stable_id", &device->stableId);
		device->location = B_CAMERA_LOCATION_UNKNOWN;
		BString location;
		info.FindString("location", &location);
		if (location == "front")
			device->location = B_CAMERA_LOCATION_FRONT;
		else if (location == "back")
			device->location = B_CAMERA_LOCATION_BACK;
		else if (location == "external" || location == "libcamera")
			device->location = B_CAMERA_LOCATION_EXTERNAL;
		device->isDefault = id == defaultId;
		fCameras->AddItem(device);

		BString label(_LocationLabel(device->location));
		if (!device->name.IsEmpty())
			label << ": " << device->name;
		BMessage* message = new BMessage(kMsgCameraChanged);
		message->AddInt32("index", i);
		fCameraMenu->AddItem(new BMenuItem(label.String(), message));
	}

	if (fCameraMenu->CountItems() == 0) {
		fCameraMenu->AddItem(new BMenuItem("No camera found", NULL));
		fCameraMenu->ItemAt(0)->SetEnabled(false);
	}
	fCameraMenu->SetTargetForItems(this);

	// The remembered camera wins, then the system default.
	fCameraIndex = 0;
	for (int32 i = 0; i < fCameras->CountItems(); i++) {
		camera_device_info* device = fCameras->ItemAt(i);
		if (!fCameraStableId.IsEmpty() && device->stableId == fCameraStableId) {
			fCameraIndex = i;
			break;
		}
		if (fCameraStableId.IsEmpty() && device->isDefault)
			fCameraIndex = i;
	}
	if (fCameraIndex < fCameraMenu->CountItems())
		fCameraMenu->ItemAt(fCameraIndex)->SetMarked(true);
}


void
CameraWindow::_RefreshResolutionMenu()
{
	while (fResolutionMenu->CountItems() > 0)
		delete fResolutionMenu->RemoveItem(0);

	int32 marked = 0;
	for (int32 i = 0; i < kResolutionCount; i++) {
		char label[64];
		snprintf(label, sizeof(label), "%ux%u @ %.0f", kResolutions[i].width,
			kResolutions[i].height, kResolutions[i].rate);
		BMessage* message = new BMessage(kMsgResChanged);
		message->AddInt32("index", i);
		fResolutionMenu->AddItem(new BMenuItem(label, message));
		if (kResolutions[i].width == fWidth
			&& kResolutions[i].height == fHeight) {
			marked = i;
		}
	}
	fResolutionMenu->SetTargetForItems(this);
	fResolutionMenu->ItemAt(marked)->SetMarked(true);
}


void
CameraWindow::_RefreshFilterMenu()
{
	while (fFilterMenu->CountItems() > 0)
		delete fFilterMenu->RemoveItem(0);

	BMessage noneMessage(kMsgFilterChanged);
	noneMessage.AddString("id", "none");
	fFilterMenu->AddItem(new BMenuItem("None", new BMessage(noneMessage)));

	BObjectList<BFilterCatalog::Entry, true> entries;
	BFilterCatalog::ListEntries(&entries, "video");
	BPopUpMenu* builtIn = new BPopUpMenu("built-in");
	BPopUpMenu* frei0r = new BPopUpMenu("frei0r");
	for (int32 i = 0; i < entries.CountItems(); i++) {
		BFilterCatalog::Entry* entry = entries.ItemAt(i);
		BMessage message(kMsgFilterChanged);
		message.AddString("id", entry->id);
		BMenuItem* item = new BMenuItem(entry->name.String(),
			new BMessage(message));
		if (entry->group == "frei0r")
			frei0r->AddItem(item);
		else
			builtIn->AddItem(item);
	}
	if (frei0r->CountItems() > 0)
		builtIn->AddItem(new BMenuItem(frei0r));
	else
		delete frei0r;
	builtIn->SetTargetForItems(this);
	fFilterMenu->AddItem(new BMenuItem(builtIn));
	fFilterMenu->ItemAt(0)->SetMarked(true);
	fFilterMenu->SetTargetForItems(this);
}


void
CameraWindow::_RefreshMicMenu()
{
	while (fMicMenu->CountItems() > 0)
		delete fMicMenu->RemoveItem(0);

	BMessage defaultMessage(kMsgMicChanged);
	defaultMessage.AddInt32("device", 0);
	fMicMenu->AddItem(new BMenuItem("Default", new BMessage(defaultMessage)));

	BObjectList<AudioCapture::Device, true> devices;
	AudioCapture::GetDevices(&devices);
	for (int32 i = 0; i < devices.CountItems(); i++) {
		AudioCapture::Device* device = devices.ItemAt(i);
		BMessage message(kMsgMicChanged);
		message.AddInt32("device", (int32)device->id);
		BMenuItem* item = new BMenuItem(device->name.String(),
			new BMessage(message));
		fMicMenu->AddItem(item);
		if (device->id == fMicId)
			item->SetMarked(true);
	}
	fMicMenu->SetTargetForItems(this);
	if (fMicMenu->FindMarked() == NULL)
		fMicMenu->ItemAt(0)->SetMarked(true);
}


void
CameraWindow::_ShutterPressed()
{
	if (fMode == 1) {
		if (fRecording)
			_StopRecording();
		else
			_StartRecording();
		return;
	}

	if (fMode == 2) {
		fBurstCancel = false;
		BMessage step(kMsgBurstStep);
		step.AddInt32("step", 0);
		PostMessage(&step);
		return;
	}

	if (fTimerDelay > 0) {
		char text[32];
		snprintf(text, sizeof(text), "Timer %d...", (int)fTimerDelay);
		fStatusLabel->SetText(text);
		thread_id id = spawn_thread(&CameraWindow::_TimerEntry,
			"camera-timer", B_NORMAL_PRIORITY, this);
		if (id >= 0)
			resume_thread(id);
		return;
	}

	_CapturePhoto();
}


void
CameraWindow::_CapturePhoto()
{
	BBitmap* frame = NULL;
	{
		BAutolock _(fFrameLock);
		if (fLastFrame != NULL)
			frame = new BBitmap(fLastFrame);
	}

	if (frame == NULL || frame->InitCheck() != B_OK) {
		delete frame;
		fStatusLabel->SetText("No frame yet");
		return;
	}

	_PlayShutter();

	BPath userDir;
	if (find_directory(B_USER_DIRECTORY, &userDir, true) != B_OK) {
		delete frame;
		return;
	}
	BString directory(userDir.Path());
	directory << "/Pictures/Camera";

	BString cameraName("Camera");
	camera_device_info* info = fCameras->ItemAt(fCameraIndex);
	if (info != NULL && !info->name.IsEmpty())
		cameraName = info->name;

	BPath saved;
	status_t status = PhotoWriter::Save(frame, directory.String(),
		cameraName.String(), 1, &saved);
	delete frame;
	if (status != B_OK) {
		char text[64];
		snprintf(text, sizeof(text), "Save failed (%s)", strerror(status));
		fStatusLabel->SetText(text);
		return;
	}

	fLastPath = saved;
	fThumbButton->SetEnabled(true);
	fStatusLabel->SetText(saved.Leaf());
}


void
CameraWindow::_StartRecording()
{
	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL || fSource == NULL || !fSource->IsStarted())
		return;

	BPath userDir;
	if (find_directory(B_USER_DIRECTORY, &userDir, true) != B_OK)
		return;
	BString directory(userDir.Path());
	directory << "/Videos/Camera";
	if (create_directory(directory.String(), 0755) != B_OK) {
		fStatusLabel->SetText("Cannot create Videos/Camera");
		return;
	}

	time_t now = time(NULL);
	struct tm parts;
	localtime_r(&now, &parts);
	char name[64];
	snprintf(name, sizeof(name), "Video-%04d%02d%02d-%02d%02d%02d.webm",
		parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
		parts.tm_hour, parts.tm_min, parts.tm_sec);
	BString path(directory);
	path << "/" << name;

	fRecorder->SetFormat(_RawVideoFormat(fWidth, fHeight, fFrameRate));
	fRecorder->SetAudioFormat(_RawAudioFormat());
	status_t status = fRecorder->SetOutputFile(path.String(),
		_Vp8Encoding(), _OpusEncoding());
	if (status != B_OK) {
		char text[64];
		snprintf(text, sizeof(text), "Record init failed (%s)",
			strerror(status));
		fStatusLabel->SetText(text);
		return;
	}

	BMediaOutput* recordOutput = fSplitter->AddOutput();
	if (recordOutput == NULL
		|| graph->Connect(recordOutput, fRecorder->VideoInput()) != B_OK) {
		fStatusLabel->SetText("Video link failed");
		fRecorder->ClearOutputFile();
		return;
	}

	// Without a microphone the file carries video only.
	bool hasAudio = false;
	if (graph->Connect(fAudio->AudioOutput(), fRecorder->AudioInput())
			== B_OK) {
		fAudio->SetDevice(fMicId);
		hasAudio = fAudio->Start() == B_OK;
	}

	status = fRecorder->Start();
	if (status != B_OK) {
		fStatusLabel->SetText("Record start failed");
		if (hasAudio)
			fAudio->Stop();
		_Unlink(graph, fAudio->AudioOutput());
		fSplitter->RemoveOutput(recordOutput);
		fRecorder->ClearOutputFile();
		return;
	}

	fRecording = true;
	fRecordStart = system_time();
	fLastPath.SetTo(path.String());
	fThumbButton->SetEnabled(true);
	fShutterButton->SetLabel("Stop");
	delete fTicker;
	fTicker = new BMessageRunner(this, new BMessage(kMsgRecordTick), 500000);
	if (!hasAudio)
		fStatusLabel->SetText("REC (no microphone)");
}


void
CameraWindow::_StopRecording()
{
	if (!fRecording)
		return;

	fRecording = false;
	delete fTicker;
	fTicker = NULL;

	fRecorder->Stop();
	fAudio->Stop();

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph != NULL) {
		_Unlink(graph, fAudio->AudioOutput());
		// Output 0 is the preview tap; the recorder holds the others.
		while (fSplitter->CountOutputs() > 1)
			fSplitter->RemoveOutput(fSplitter->OutputAt(1));
	}
	fRecorder->ClearOutputFile();

	fShutterButton->SetLabel("Record");
	if (fLastPath.Path() != NULL)
		fStatusLabel->SetText(fLastPath.Leaf());
}


void
CameraWindow::_PlayShutter()
{
	entry_ref ref;
	if (((CameraApp*)be_app)->ShutterSoundRef(&ref) == B_OK)
		play_sound(&ref, true, false, true);
}


void
CameraWindow::_LoadSettings()
{
	BMessage* settings = ((CameraApp*)be_app)->Settings();

	settings->FindBool("grid", &fGrid);
	settings->FindBool("mirror", &fMirrorEnabled);
	settings->FindString("camera", &fCameraStableId);
	settings->FindString("filter", &fFilterId);

	int32 width = 0;
	int32 height = 0;
	float rate = 0;
	if (settings->FindInt32("width", &width) == B_OK
		&& settings->FindInt32("height", &height) == B_OK
		&& width > 0 && height > 0) {
		fWidth = (uint32)width;
		fHeight = (uint32)height;
		if (settings->FindFloat("fps", &rate) == B_OK && rate > 0)
			fFrameRate = rate;
	}

	int32 mic = 0;
	if (settings->FindInt32("mic", &mic) == B_OK)
		fMicId = (media_client_id)mic;

	int32 mode = 0;
	if (settings->FindInt32("mode", &mode) == B_OK) {
		_SetMode(mode);
		BMenuItem* item = fModeMenu->ItemAt(mode);
		if (item != NULL)
			item->SetMarked(true);
	}

	_RefreshMicMenu();
	_RefreshCameraMenu();
	_RefreshResolutionMenu();
	fView->SetGridVisible(fGrid);

	if (fFilterId != "none")
		_SetFilter(fFilterId.String());

	_OpenCamera(fCameraIndex);
}


void
CameraWindow::_StoreSettings()
{
	BMessage* settings = ((CameraApp*)be_app)->Settings();

	settings->RemoveName("mode");
	settings->AddInt32("mode", fMode);
	settings->RemoveName("grid");
	settings->AddBool("grid", fGrid);
	settings->RemoveName("mirror");
	settings->AddBool("mirror", fMirrorEnabled);
	settings->RemoveName("width");
	settings->AddInt32("width", (int32)fWidth);
	settings->RemoveName("height");
	settings->AddInt32("height", (int32)fHeight);
	settings->RemoveName("fps");
	settings->AddFloat("fps", fFrameRate);
	settings->RemoveName("mic");
	settings->AddInt32("mic", (int32)fMicId);
	settings->RemoveName("camera");
	settings->AddString("camera", fCameraStableId);

	settings->RemoveName("filter");
	settings->AddString("filter", fFilterId);
}


status_t
CameraWindow::_RunTimer()
{
	bigtime_t delay = (bigtime_t)fTimerDelay * 1000000LL;
	while (delay > 0 && !fQuitRequested) {
		const bigtime_t slice = delay > 100000 ? 100000 : delay;
		snooze(slice);
		delay -= slice;
	}
	if (!fQuitRequested)
		PostMessage(kMsgCaptureNow);
	return 0;
}
