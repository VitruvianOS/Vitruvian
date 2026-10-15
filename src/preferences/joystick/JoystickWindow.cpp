/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "JoystickWindow.h"

#include "TestPanel.h"

#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <MessageRunner.h>
#include <Notification.h>
#include <ScrollView.h>
#include <Size.h>
#include <Slider.h>
#include <StringView.h>

#include <string.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Joystick"


static const uint32 kMsgPoll = 'jPol';
static const uint32 kMsgSelection = 'jSel';
static const uint32 kMsgRefresh = 'jRef';
static const uint32 kMsgCalibrationChanged = 'jCal';
static const uint32 kMsgResetCalibration = 'jRst';

static const bigtime_t kPollInterval = 33000;
static const bigtime_t kReconnectInterval = 2000000;


class DeviceItem : public BStringItem {
public:
								DeviceItem(const char* label,
									const char* path);

			const char*			Path() const { return fPath.String(); }

private:
			BString				fPath;
};


DeviceItem::DeviceItem(const char* label, const char* path)
	:
	BStringItem(label),
	fPath(path)
{
}


static BString
_DeviceLabel(const char* name, const char* path)
{
	// basename tells two identical pads apart
	const char* base = strrchr(path, '/');
	BString label(name != NULL && name[0] != '\0' ? name : path);
	label << " (" << (base != NULL ? base + 1 : path) << ")";
	return label;
}


JoystickWindow::JoystickWindow()
	:
	BWindow(BRect(100, 100, 740, 560), B_TRANSLATE("Joystick"),
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE
			| B_AUTO_UPDATE_SIZE_LIMITS),
	fDeviceList(NULL),
	fStatusView(NULL),
	fTestPanel(NULL),
	fDeadZoneSlider(NULL),
	fCurveSlider(NULL),
	fRefreshButton(NULL),
	fPollRunner(NULL),
	fConnected(false),
	fIgnoreSliders(false),
	fLastReconnect(0)
{
	fDeviceList = new BListView("devices", B_SINGLE_SELECTION_LIST,
		B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_FRAME_EVENTS);
	fDeviceList->SetSelectionMessage(new BMessage(kMsgSelection));

	fStatusView = new BStringView("status", "");
	fStatusView->SetTruncation(B_TRUNCATE_END);
	fStatusView->SetExplicitMaxSize(BSize(220, B_SIZE_UNSET));
	fRefreshButton = new BButton(B_TRANSLATE("Refresh"),
		new BMessage(kMsgRefresh));

	fTestPanel = new TestPanel();

	fDeadZoneSlider = new BSlider("dead zone", B_TRANSLATE("Dead zone"),
		new BMessage(kMsgCalibrationChanged), 0, 50, B_HORIZONTAL);
	fDeadZoneSlider->SetModificationMessage(
		new BMessage(kMsgCalibrationChanged));
	fDeadZoneSlider->SetHashMarkCount(6);
	fDeadZoneSlider->SetLimitLabels(B_TRANSLATE("0%"), B_TRANSLATE("50%"));

	fCurveSlider = new BSlider("curve", B_TRANSLATE("Stick curve"),
		new BMessage(kMsgCalibrationChanged), 0, 100, B_HORIZONTAL);
	fCurveSlider->SetModificationMessage(
		new BMessage(kMsgCalibrationChanged));
	fCurveSlider->SetHashMarkCount(5);
	fCurveSlider->SetLimitLabels(B_TRANSLATE("Linear"), B_TRANSLATE("Cubic"));

	BButton* resetButton = new BButton(B_TRANSLATE("Reset"),
		new BMessage(kMsgResetCalibration));

	BScrollView* listScroll = new BScrollView("device_scroll", fDeviceList,
		0, false, true);
	listScroll->SetExplicitMinSize(BSize(180, B_SIZE_UNSET));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
				.Add(listScroll)
				.Add(fStatusView)
				.Add(fRefreshButton)
			.End()
			.Add(fTestPanel)
		.End()
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.Add(fDeadZoneSlider)
			.Add(fCurveSlider)
			.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
				.Add(resetButton)
				.AddGlue()
			.End()
		.End()
		.SetInsets(B_USE_SMALL_INSETS, B_USE_SMALL_INSETS,
			B_USE_SMALL_INSETS, B_USE_SMALL_INSETS);

	SetSizeLimits(560, 2000, 400, 2000);

	_ScanDevices();
	_UpdateCalibrationControls();

	BMessage poll(kMsgPoll);
	fPollRunner = new BMessageRunner(BMessenger(this), &poll, kPollInterval,
		-1);
}


JoystickWindow::~JoystickWindow()
{
	delete fPollRunner;
	fPollRunner = NULL;
}


void
JoystickWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSelection:
		{
			int32 index = fDeviceList->CurrentSelection();
			if (index < 0)
				break;
			DeviceItem* item
				= dynamic_cast<DeviceItem*>(fDeviceList->ItemAt(index));
			if (item != NULL)
				_SelectDevice(item->Path());
			break;
		}

		case kMsgRefresh:
			_ScanDevices();
			break;

		case kMsgCalibrationChanged:
			if (!fIgnoreSliders && fDevicePath.Length() > 0) {
				fCalibration.deadZone = fDeadZoneSlider->Value() / 100.0f;
				fCalibration.curve = fCurveSlider->Value() / 100.0f;
				fCalibration.Save(fDevicePath.String());
			}
			break;

		case kMsgResetCalibration:
			fCalibration = CalibrationSettings();
			if (fDevicePath.Length() > 0)
				fCalibration.Save(fDevicePath.String());
			_UpdateCalibrationControls();
			break;

		case kMsgPoll:
			_Poll();
			break;

		case B_JOYSTICK_DEVICES_CHANGED:
			_ScanDevices();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
JoystickWindow::QuitRequested()
{
	watch_joystick_devices(BMessenger(this), false);
	delete fPollRunner;
	fPollRunner = NULL;
	be_app_messenger.SendMessage(B_QUIT_REQUESTED);
	return BWindow::QuitRequested();
}


void
JoystickWindow::Show()
{
	BWindow::Show();
	watch_joystick_devices(BMessenger(this), true);
}


void
JoystickWindow::_ScanDevices()
{
	BString selectedPath(fDevicePath);

	fDeviceList->MakeEmpty();

	// a second BJoystick so naming devices does not disturb the open one
	BJoystick scanner;
	scanner.RescanDevices();

	int32 count = scanner.CountDevices();
	for (int32 i = 0; i < count; i++) {
		char path[B_OS_NAME_LENGTH];
		if (scanner.GetDeviceName(i, path, sizeof(path)) != B_OK)
			continue;

		BString name;
		if (scanner.Open(path) == B_OK) {
			scanner.GetControllerName(&name);
			scanner.Close();
		}

		DeviceItem* item = new DeviceItem(
			_DeviceLabel(name.String(), path).String(), path);
		fDeviceList->AddItem(item);

		if (selectedPath == path)
			fDeviceList->Select(i);
	}

	if (fDeviceList->CurrentSelection() < 0 && count > 0) {
		fDeviceList->Select(0);
		DeviceItem* item = dynamic_cast<DeviceItem*>(
			fDeviceList->ItemAt(0));
		if (item != NULL)
			_SelectDevice(item->Path());
	}

	if (count == 0) {
		fDevicePath = "";
		fJoystick.Close();
		fConnected = false;
		fTestPanel->SetDevice(NULL, NULL);
		_SetStatus(B_TRANSLATE(
			"No joysticks found. Connect a gamepad and press Refresh."));
	}
}


void
JoystickWindow::_SelectDevice(const char* path)
{
	if (path == NULL || fDevicePath == path)
		return;

	fJoystick.Close();
	fConnected = false;
	fDevicePath = path;

	if (fJoystick.Open(path) == B_OK) {
		fConnected = true;
		fTestPanel->SetDevice(&fJoystick, path);
		BString controller;
		fJoystick.GetControllerName(&controller);
		BString status(B_TRANSLATE("Connected"));
		status << ": " << controller;
		_SetStatus(status.String());
	} else {
		fTestPanel->SetDevice(NULL, NULL);
		_SetStatus(B_TRANSLATE("This device cannot be opened."));
	}

	fCalibration.Load(path);
	_UpdateCalibrationControls();
}


void
JoystickWindow::_SetStatus(const char* text)
{
	fStatusView->SetText(text);
}


void
JoystickWindow::_Poll()
{
	if (fDevicePath.Length() == 0)
		return;

	if (!fConnected) {
		bigtime_t now = system_time();
		if (now - fLastReconnect < kReconnectInterval)
			return;
		fLastReconnect = now;

		fJoystick.RescanDevices();
		if (fJoystick.Open(fDevicePath.String()) != B_OK)
			return;

		fConnected = true;
		fTestPanel->SetDevice(&fJoystick, fDevicePath.String());
		BString controller;
		fJoystick.GetControllerName(&controller);
		BString status(B_TRANSLATE("Connected"));
		status << ": " << controller;
		_SetStatus(status.String());
		_Notify(B_TRANSLATE("Joystick connected"), controller.String());
		return;
	}

	if (fJoystick.Update() != B_OK) {
		fJoystick.Close();
		fConnected = false;
		fTestPanel->SetDevice(NULL, NULL);
		_SetStatus(B_TRANSLATE("Disconnected. Waiting for the device."));
		_Notify(B_TRANSLATE("Joystick disconnected"),
			fDevicePath.String());
		return;
	}

	fTestPanel->Refresh(&fJoystick, fCalibration);
}


void
JoystickWindow::_Notify(const char* title, const char* content)
{
	BNotification notification(B_INFORMATION_NOTIFICATION);
	notification.SetTitle(title);
	notification.SetContent(content);
	notification.Send();
}


void
JoystickWindow::_UpdateCalibrationControls()
{
	fIgnoreSliders = true;
	fDeadZoneSlider->SetValue((int32)(fCalibration.deadZone * 100.0f + 0.5f));
	fCurveSlider->SetValue((int32)(fCalibration.curve * 100.0f + 0.5f));
	fIgnoreSliders = false;
}
