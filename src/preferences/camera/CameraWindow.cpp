/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "CameraWindow.h"

#include <new>

#include <Alert.h>
#include <Application.h>
#include <Autolock.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Locale.h>
#include <ObjectList.h>
#include <ScrollView.h>
#include <StringItem.h>

#include <media2/MediaGraph.h>

#include "CameraMessages.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Camera"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Camera"


class CameraDeviceListItem : public BStringItem {
public:
								CameraDeviceListItem(media_client_id id,
									const BString& stableId, const char* label,
									bool isDefault)
									:
									BStringItem(label),
									fId(id),
									fStableId(stableId),
									fIsDefault(isDefault)
								{}
			media_client_id		Id() const { return fId; }
			const BString&		StableId() const { return fStableId; }
			bool				IsDefault() const { return fIsDefault; }

private:
			media_client_id		fId;
			BString				fStableId;
			bool				fIsDefault;
};


static const char*
_LocationLabel(camera_location location)
{
	switch (location) {
		case B_CAMERA_LOCATION_FRONT:    return "Front";
		case B_CAMERA_LOCATION_BACK:     return "Back";
		case B_CAMERA_LOCATION_EXTERNAL: return "External";
		default:                         return "Camera";
	}
}


CameraWindow::CameraWindow()
	:
	BWindow(BRect(120, 120, 720, 560), B_TRANSLATE("Camera"),
		B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS),
	fDeviceList(NULL),
	fDeviceScroll(NULL),
	fPreview(NULL),
	fStatusLabel(NULL),
	fEmptyView(NULL),
	fControlsBox(NULL),
	fDefaultButton(NULL),
	fResetButton(NULL),
	fMirrorCheck(NULL),
	fBrightnessSlider(NULL),
	fContrastSlider(NULL),
	fExposureSlider(NULL),
	fWhiteBalanceSlider(NULL),
	fCamera(NULL),
	fSelectedId(0),
	fSelectedStableId("")
{
	_BuildInterface();

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph != NULL)
		graph->StartWatching(BMessenger(this),
			BMediaGraph::B_MEDIA_ALL_NOTIFICATIONS);

	RefreshDevices();
}


void
CameraWindow::_BuildInterface()
{
	fDeviceList = new BListView("devices", B_SINGLE_SELECTION_LIST,
		B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE);
	fDeviceList->SetSelectionMessage(new BMessage(kMsgCameraDeviceSelected));
	fDeviceScroll = new BScrollView("device_scroll", fDeviceList, 0, false,
		true);

	fPreview = new CameraPreviewView("preview");

	fStatusLabel = new BStringView("status", B_TRANSLATE("No camera selected"));
	fEmptyView = new BStringView("empty", B_TRANSLATE(
		"No cameras found\n\n"
		"Connect a webcam or enable a built-in camera,\n"
		"then close and reopen this preflet."));
	fEmptyView->SetAlignment(B_ALIGN_CENTER);
	fEmptyView->SetHighUIColor(B_LIST_ITEM_TEXT_COLOR, B_DARKEN_2_TINT);
	fEmptyView->Hide();

	fBrightnessSlider = new BSlider("brightness", B_TRANSLATE("Brightness"),
		new BMessage(kMsgCameraControlChanged), 0, 100, B_HORIZONTAL);
	fBrightnessSlider->SetLimitLabels(B_TRANSLATE("Min"), B_TRANSLATE("Max"));
	fContrastSlider = new BSlider("contrast", B_TRANSLATE("Contrast"),
		new BMessage(kMsgCameraControlChanged), 0, 100, B_HORIZONTAL);
	fContrastSlider->SetLimitLabels(B_TRANSLATE("Min"), B_TRANSLATE("Max"));
	fExposureSlider = new BSlider("exposure", B_TRANSLATE("Exposure"),
		new BMessage(kMsgCameraControlChanged), 0, 100, B_HORIZONTAL);
	fExposureSlider->SetLimitLabels(B_TRANSLATE("Auto"), B_TRANSLATE("Manual"));
	fWhiteBalanceSlider = new BSlider("white_balance",
		B_TRANSLATE("White balance"),
		new BMessage(kMsgCameraControlChanged), 2500, 8000, B_HORIZONTAL);
	fWhiteBalanceSlider->SetLimitLabels(B_TRANSLATE("Cool"),
		B_TRANSLATE("Warm"));

	fMirrorCheck = new BCheckBox("mirror", B_TRANSLATE("Mirror preview"),
		new BMessage(kMsgCameraControlChanged));
	fDefaultButton = new BButton("default", B_TRANSLATE("Set as default"),
		new BMessage(kMsgCameraSetDefault));
	fResetButton = new BButton("reset", B_TRANSLATE("Reset"),
		new BMessage(kMsgCameraResetControls));

	fControlsBox = new BBox("controls");
	fControlsBox->SetLayout(new BGroupLayout(B_VERTICAL, 5));
	fControlsBox->AddChild(fBrightnessSlider);
	fControlsBox->AddChild(fContrastSlider);
	fControlsBox->AddChild(fExposureSlider);
	fControlsBox->AddChild(fWhiteBalanceSlider);
	fControlsBox->AddChild(fMirrorCheck);
	fControlsBox->AddChild(BLayoutBuilder::Group<>(B_HORIZONTAL)
		.Add(fResetButton)
		.AddGlue()
		.Add(fDefaultButton)
		.View());
	fControlsBox->Hide();

	// List and controls share a narrow sidebar; the preview takes the rest.
	float sidebar = be_plain_font->StringWidth("M") * 18;
	fDeviceScroll->SetExplicitMinSize(BSize(sidebar, B_SIZE_UNSET));
	fDeviceScroll->SetExplicitMaxSize(BSize(sidebar, B_SIZE_UNLIMITED));
	fControlsBox->SetExplicitMaxSize(BSize(sidebar, B_SIZE_UNSET));
	fPreview->SetExplicitMinSize(BSize(320, 240));
	fPreview->SetExplicitPreferredSize(BSize(640, 480));
	// String views cap at their text width and would pin the column.
	fPreview->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	fStatusLabel->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fEmptyView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BLayoutBuilder::Group<>(this, B_HORIZONTAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING, 0.0f)
			.Add(new BStringView("title", B_TRANSLATE("Cameras")))
			.Add(fDeviceScroll, 1.0f)
			.Add(fControlsBox)
		.End()
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING, 1.0f)
			.Add(fStatusLabel)
			.Add(fPreview, 1.0f)
			.Add(fEmptyView)
		.End();
}


void
CameraWindow::RefreshDevices()
{
	fDeviceList->MakeEmpty();

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL) {
		_SetEmptyState(true);
		return;
	}

	media_client_id defaultId = 0;
	graph->GetDefaultCamera(&defaultId);

	BObjectList<media_client_id, true> clients;
	if (graph->GetClients(&clients) != B_OK) {
		_SetEmptyState(true);
		return;
	}

	int32 count = 0;
	for (int32 i = 0; i < clients.CountItems(); i++) {
		media_client_id id = *clients.ItemAt(i);
		BMessage info;
		if (graph->GetClientInfo(id, &info) != B_OK)
			continue;
		bool isVideo = false;
		info.FindBool("is.video", &isVideo);
		if (!isVideo)
			continue;

		BString name;
		info.FindString("name", &name);
		BString stable;
		info.FindString("stable_id", &stable);
		BString location;
		info.FindString("location", &location);

		camera_device_info device;
		device.id = id;
		device.name = name;
		device.stableId = stable;
		device.nodeName = "";
		device.location = B_CAMERA_LOCATION_UNKNOWN;
		if (location == "front")
			device.location = B_CAMERA_LOCATION_FRONT;
		else if (location == "back")
			device.location = B_CAMERA_LOCATION_BACK;
		else if (location == "external" || location == "libcamera")
			device.location = B_CAMERA_LOCATION_EXTERNAL;
		device.isDefault = (id == defaultId);

		BString label = BString(_LocationLabel(device.location));
		if (!name.IsEmpty())
			label << ": " << name;
		if (device.isDefault)
			label.Prepend("Default: ");

		fDeviceList->AddItem(new CameraDeviceListItem(id, stable,
			label.String(), device.isDefault));
		count++;
	}

	_SetEmptyState(count == 0);
	if (count > 0 && fSelectedId == 0)
		fDeviceList->Select(0);
	else if (count > 0) {
		for (int32 i = 0; i < fDeviceList->CountItems(); i++) {
			CameraDeviceListItem* item
				= (CameraDeviceListItem*)fDeviceList->ItemAt(i);
			if (item != NULL && item->Id() == fSelectedId) {
				fDeviceList->Select(i);
				break;
			}
		}
	} else {
		_SelectDevice(0);
	}
}


void
CameraWindow::_SelectDevice(media_client_id id)
{
	fSelectedId = id;
	fSelectedStableId = "";

	fCamera = NULL;
	fPreview->SetCamera(NULL);

	if (id == 0) {
		fStatusLabel->SetText(B_TRANSLATE("No camera selected"));
		fControlsBox->Hide();
		return;
	}

	fCamera = new BCameraSource("preflet-camera");
	if (fCamera->SetToDevice(id) != B_OK) {
		delete fCamera;
		fCamera = NULL;
		fStatusLabel->SetText(B_TRANSLATE("Camera unavailable"));
		fControlsBox->Hide();
		return;
	}

	fSelectedStableId = fCamera->StableId();
	fPreview->SetCamera(fCamera);

	// Preview: start capture so frames arrive for the BView.
	fCamera->Start();

	BString status;
	status << B_TRANSLATE("Ready");
	if (!fSelectedStableId.IsEmpty())
		status << " (" << fSelectedStableId << ")";
	fStatusLabel->SetText(status.String());
	fControlsBox->Show();
	_UpdateControls();
}


void
CameraWindow::_UpdateControls()
{
	if (fCamera == NULL)
		return;

	float value = 0;
	if (fCamera->GetControl(B_CAMERA_PARAM_BRIGHTNESS, &value) == B_OK)
		fBrightnessSlider->SetValue((int32)(value * 100.0f + 0.5f));
	if (fCamera->GetControl(B_CAMERA_PARAM_CONTRAST, &value) == B_OK)
		fContrastSlider->SetValue((int32)(value * 100.0f + 0.5f));
	if (fCamera->GetControl(B_CAMERA_PARAM_EXPOSURE, &value) == B_OK)
		fExposureSlider->SetValue((int32)value);
	if (fCamera->GetControl(B_CAMERA_PARAM_WHITE_BALANCE, &value) == B_OK)
		fWhiteBalanceSlider->SetValue((int32)value);
	if (fCamera->GetControl(B_CAMERA_PARAM_MIRROR, &value) == B_OK)
		fMirrorCheck->SetValue(value != 0.0f);
	fPreview->SetMirror(fMirrorCheck->Value() == B_CONTROL_ON);

	// Controls the device does not expose are disabled, not faked.
	fBrightnessSlider->SetEnabled(
		fCamera->ControlSupported(B_CAMERA_PARAM_BRIGHTNESS) == B_OK);
	fContrastSlider->SetEnabled(
		fCamera->ControlSupported(B_CAMERA_PARAM_CONTRAST) == B_OK);
	fExposureSlider->SetEnabled(
		fCamera->ControlSupported(B_CAMERA_PARAM_EXPOSURE) == B_OK
		|| fCamera->ControlSupported(B_CAMERA_PARAM_EXPOSURE_AUTO) == B_OK);
	fWhiteBalanceSlider->SetEnabled(
		fCamera->ControlSupported(B_CAMERA_PARAM_WHITE_BALANCE) == B_OK
		|| fCamera->ControlSupported(B_CAMERA_PARAM_WHITE_BALANCE_AUTO) == B_OK);
}


void
CameraWindow::_SaveDeviceSettings()
{
	if (fCamera == NULL || fSelectedStableId.IsEmpty())
		return;

	// BCameraSource persists each control through _SaveControls.
	fCamera->SetControl(B_CAMERA_PARAM_BRIGHTNESS,
		fBrightnessSlider->Value() / 100.0f);
	fCamera->SetControl(B_CAMERA_PARAM_CONTRAST,
		fContrastSlider->Value() / 100.0f);
	fCamera->SetControl(B_CAMERA_PARAM_EXPOSURE,
		(float)fExposureSlider->Value());
	fCamera->SetControl(B_CAMERA_PARAM_EXPOSURE_AUTO,
		fExposureSlider->Value() < 50 ? 1.0f : 0.0f);
	fCamera->SetControl(B_CAMERA_PARAM_WHITE_BALANCE,
		(float)fWhiteBalanceSlider->Value());
	fCamera->SetControl(B_CAMERA_PARAM_MIRROR,
		fMirrorCheck->Value() == B_CONTROL_ON ? 1.0f : 0.0f);
}


void
CameraWindow::_SetEmptyState(bool empty)
{
	if (empty) {
		fEmptyView->Show();
		fStatusLabel->SetText(B_TRANSLATE("No cameras found"));
		fControlsBox->Hide();
		if (fCamera != NULL) {
			fCamera->Stop();
			delete fCamera;
			fCamera = NULL;
			fPreview->SetCamera(NULL);
		}
		fSelectedId = 0;
	} else {
		fEmptyView->Hide();
	}
}


void
CameraWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgCameraDeviceSelected:
		{
			int32 index = fDeviceList->CurrentSelection();
			CameraDeviceListItem* item = index >= 0
				? (CameraDeviceListItem*)fDeviceList->ItemAt(index) : NULL;
			_SelectDevice(item != NULL ? item->Id() : 0);
			break;
		}
		case kMsgCameraControlChanged:
			_SaveDeviceSettings();
			_UpdateControls();
			break;
		case kMsgCameraResetControls:
			if (fCamera != NULL) {
				fCamera->ResetControls();
				_UpdateControls();
			}
			break;
		case kMsgCameraSetDefault:
			if (fSelectedId != 0) {
				BMediaGraph* graph = BMediaGraph::Instance();
				if (graph != NULL)
					graph->SetDefaultCamera(fSelectedId);
				RefreshDevices();
			}
			break;
		case kMsgCameraRefresh:
		case BMediaGraph::B_MEDIA_CLIENT_ADDED:
		case BMediaGraph::B_MEDIA_CLIENT_REMOVED:
		case BMediaGraph::B_MEDIA_DEFAULTS_CHANGED:
			RefreshDevices();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
CameraWindow::QuitRequested()
{
	if (fCamera != NULL) {
		fCamera->Stop();
		delete fCamera;
		fCamera = NULL;
	}
	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph != NULL)
		graph->StopWatching(BMessenger(this));
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}
