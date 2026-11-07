/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "LocationStatusView.h"
#include "LocationSettings.h"

#include <AboutWindow.h>
#include <Application.h>
#include <Catalog.h>
#include <Deskbar.h>
#include <Dragger.h>
#include <Entry.h>
#include <Locale.h>
#include <MenuItem.h>
#include <Menu.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <View.h>

#include <LocationProvider.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "LocationStatus"


static const char* kSignature = "application/x-vnd.vos-LocationStatus";
static const char* kDeskbarSignature = "application/x-vnd.Be-TSKB";
static const char* kDeskbarItemName = "LocationStatus";
static const char* kLocationPrefsPath = "/system/preferences/Location";


// Defined at file scope below; forward-declared so the replicant can resolve
// this add-on's own image if a tray icon is loaded from resources later.
extern "C" BView* instantiate_deskbar_item(float maxWidth, float maxHeight);


LocationStatusView::LocationStatusView(BRect frame, int32 resizingMode,
	bool inDeskbar)
	:
	BView(frame, kDeskbarItemName, resizingMode,
		B_WILL_DRAW | B_TRANSPARENT_BACKGROUND | B_FRAME_EVENTS),
	fInDeskbar(inDeskbar),
	fIndicator(kLocationIndicatorOffline),
	fProvider(NULL),
	fSettings(NULL),
	fSettingsRunner(NULL),
	fWantsEnabled(false),
	fSourcePort(0)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	SetLowColor(ViewColor());

	if (!inDeskbar) {
		frame.OffsetTo(B_ORIGIN);
		frame.top = frame.bottom - 7;
		frame.left = frame.right - 7;
		BDragger* dragger = new BDragger(frame, this,
			B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM);
		AddChild(dragger);
	}
}


LocationStatusView::LocationStatusView(BMessage* archive)
	:
	BView(archive),
	fInDeskbar(false),
	fIndicator(kLocationIndicatorOffline),
	fProvider(NULL),
	fSettings(NULL),
	fSettingsRunner(NULL),
	fWantsEnabled(false),
	fSourcePort(0)
{
	// Deskbar restores saved replicants through this archive constructor.
	app_info info;
	if (be_app->GetAppInfo(&info) == B_OK
		&& !strcasecmp(info.signature, kDeskbarSignature))
		fInDeskbar = true;

	SetViewColor(B_TRANSPARENT_COLOR);
	SetLowColor(ViewColor());
}


LocationStatusView::~LocationStatusView()
{
	if (fProvider != NULL) {
		fProvider->Unwatch(BMessenger(this));
		fProvider->Stop();
		fProvider->Lock();
		fProvider->Quit();
		fProvider = NULL;
	}
	delete fSettings;
}


LocationStatusView*
LocationStatusView::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "LocationStatusView"))
		return NULL;

	return new LocationStatusView(archive);
}


status_t
LocationStatusView::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, deep);
	if (status == B_OK)
		status = archive->AddString("add_on", kSignature);
	if (status == B_OK)
		status = archive->AddString("class", "LocationStatusView");

	return status;
}


void
LocationStatusView::AttachedToWindow()
{
	BView::AttachedToWindow();

	fSettings = new LocationSettings();
	fSettings->LoadSettings();

	fProvider = new LocationProvider(fSettings->Host().String(),
		fSettings->Port());
	fProvider->SetAccuracyLevel(fSettings->AccuracyLevel());
	fProvider->Run();
	fProvider->Watch(BMessenger(this));

	if (fSettings->Enabled())
		fProvider->Start();

	// Hand the replicant the current state without waiting for gpsd.
	BMessage status(B_LOCATION_STATUS);
	status.AddInt32("status", fSettings->Enabled()
		? B_LOCATION_CONNECTING : B_LOCATION_OFFLINE);
	_ApplyStatus(&status);

	fWantsEnabled = fSettings->Enabled();
	fSourceHost = fSettings->Host();
	fSourcePort = fSettings->Port();

	// The preflet owns the same settings file and has no channel to us;
	// re-read it on a tick so its on/off switch moves this indicator.
	fSettingsRunner = new BMessageRunner(BMessenger(this),
		new BMessage(kMsgReloadSettings), 1000000);
}


void
LocationStatusView::DetachedFromWindow()
{
	delete fSettingsRunner;
	fSettingsRunner = NULL;

	if (fProvider != NULL) {
		fProvider->Unwatch(BMessenger(this));
		fProvider->Stop();
		fProvider->Lock();
		fProvider->Quit();
		fProvider = NULL;
	}
	delete fSettings;
	fSettings = NULL;

	BView::DetachedFromWindow();
}


void
LocationStatusView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_LOCATION_STATUS:
		case B_LOCATION_CHANGED:
			_ApplyStatus(message);
			break;
		case kMsgOpenLocationPreferences:
			_OpenLocationPreferences();
			break;
		case kMsgRemoveFromDeskbar:
			_RemoveFromDeskbar();
			break;
		case kMsgReloadSettings:
			_ReloadSettings();
			break;
		case B_QUIT_REQUESTED:
			_Quit();
			break;
		case B_ABOUT_REQUESTED:
		{
			BAboutWindow* window = new BAboutWindow(
				B_TRANSLATE_SYSTEM_NAME("LocationStatus"), kSignature);
			window->AddCopyright(2026, "Dario Casalinuovo");
			window->Show();
			break;
		}
		default:
			BView::MessageReceived(message);
			break;
	}
}


void
LocationStatusView::Draw(BRect updateRect)
{
	BRect bounds = Bounds();
	float w = bounds.Width();
	float h = bounds.Height();
	float cx = bounds.left + w / 2.0f;
	float cy = bounds.top + h / 2.0f;
	float headR = (w < h ? w : h) * 0.22f;
	if (headR < 2.0f)
		headR = 2.0f;

	rgb_color color = { 128, 128, 128, 255 };
	switch (fIndicator) {
		case kLocationIndicatorNoFix:
			color = ui_color(B_NAVIGATION_PULSE_COLOR);
			break;
		case kLocationIndicator2DFix:
		case kLocationIndicator3DFix:
			color = ui_color(B_SUCCESS_COLOR);
			break;
		default:
			break;
	}

	// Pin glyph: head circle over a downward wedge, sized for the tray slot.
	SetHighColor(color);
	FillEllipse(BRect(cx - headR, cy - headR * 1.4f,
		cx + headR, cy + headR * 0.6f));
	FillTriangle(BPoint(cx - headR * 0.85f, cy - headR * 0.1f),
		BPoint(cx + headR * 0.85f, cy - headR * 0.1f),
		BPoint(cx, cy + headR * 1.2f));

	if (fIndicator == kLocationIndicatorOffline) {
		SetHighColor(ui_color(B_FAILURE_COLOR));
		SetPenSize(1.5f);
		StrokeLine(bounds.LeftTop(), bounds.RightBottom());
		SetPenSize(1.0f);
	}
}


void
LocationStatusView::MouseDown(BPoint where)
{
	_ShowMenu(where);
}


void
LocationStatusView::_ApplyStatus(BMessage* message)
{
	bool enabled = true;
	message->FindBool("enabled", &enabled);
	bool connected = false;
	message->FindBool("connected", &connected);

	int32 fix = 0;
	message->FindInt32("fix", &fix);

	int32 indicator = kLocationIndicatorOffline;
	if (!enabled)
		indicator = kLocationIndicatorOffline;
	else if (fix >= 3)
		indicator = kLocationIndicator3DFix;
	else if (fix == 2)
		indicator = kLocationIndicator2DFix;
	else if (connected)
		indicator = kLocationIndicatorNoFix;

	if (indicator != fIndicator) {
		fIndicator = indicator;
		Invalidate();
	}
}


void
LocationStatusView::_ShowMenu(BPoint where)
{
	// One menu per click, heap allocated with async auto-destruct; a
	// synchronous Go() would block Deskbar's tray thread while open.
	BPopUpMenu* menu = new BPopUpMenu(B_EMPTY_STRING, false, false);
	menu->SetAsyncAutoDestruct(true);
	menu->SetFont(be_plain_font);

	menu->AddItem(new BMenuItem(B_TRANSLATE("Open Location preferences"),
		new BMessage(kMsgOpenLocationPreferences)));
	if (fInDeskbar)
		menu->AddItem(new BMenuItem(B_TRANSLATE("Remove from Deskbar"),
			new BMessage(kMsgRemoveFromDeskbar)));

	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("About"),
		new BMessage(B_ABOUT_REQUESTED)));
	if (fInDeskbar)
		menu->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
			new BMessage(B_QUIT_REQUESTED)));

	menu->SetTargetForItems(this);
	ConvertToScreen(&where);
	menu->Go(where, true, true, true);
}


void
LocationStatusView::_OpenLocationPreferences()
{
	entry_ref ref;
	BPath path(kLocationPrefsPath);
	if (get_ref_for_path(path.Path(), &ref) == B_OK)
		be_roster->Launch(&ref);
}


void
LocationStatusView::_RemoveFromDeskbar()
{
	thread_id thread = spawn_thread(_RemoveFromDeskbarThread,
		"remove location status", B_NORMAL_PRIORITY, NULL);
	if (thread >= 0)
		resume_thread(thread);
}


int32
LocationStatusView::_RemoveFromDeskbarThread(void*)
{
	BDeskbar deskbar;
	deskbar.RemoveItem(kDeskbarItemName);
	return 0;
}


void
LocationStatusView::_Quit()
{
	// In the tray "Quit" means remove the replicant: forwarding
	// B_QUIT_REQUESTED to this looper would ask Deskbar itself to quit.
	if (fInDeskbar) {
		thread_id thread = spawn_thread(_RemoveFromDeskbarThread,
			"remove_location_status", B_NORMAL_PRIORITY, NULL);
		if (thread >= B_OK)
			resume_thread(thread);
	} else
		be_app->PostMessage(B_QUIT_REQUESTED);
}


void
LocationStatusView::_ReloadSettings()
{
	if (fSettings == NULL || fProvider == NULL)
		return;

	fSettings->LoadSettings();
	if (fSettings->Enabled() != fWantsEnabled) {
		fWantsEnabled = fSettings->Enabled();
		if (fWantsEnabled)
			fProvider->Start();
		else
			fProvider->Stop();
	}

	if (fSettings->Host() != fSourceHost
		|| fSettings->Port() != fSourcePort) {
		fSourceHost = fSettings->Host();
		fSourcePort = fSettings->Port();
		fProvider->SetEndpoint(fSourceHost.String(), fSourcePort);
	}
}


//	#pragma mark -


extern "C" BView*
instantiate_deskbar_item(float maxWidth, float maxHeight)
{
	// Square, driven by height: a replicant wider than one row makes
	// TReplicantTray::LocationForReplicant() loop forever.
	return new LocationStatusView(BRect(0, 0, maxHeight - 1, maxHeight - 1),
		B_FOLLOW_LEFT | B_FOLLOW_TOP, true);
}


extern "C" _EXPORT status_t
instantiate_deskbar_item_for_width(float* width, float* height)
{
	*width = 16;
	*height = 16;
	return B_OK;
}
