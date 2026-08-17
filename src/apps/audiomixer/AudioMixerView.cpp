/*
 * Copyright 2003-2018, Haiku, Inc.
 * Copyright 2026, Vitruvian Project.
 * AudioMixer deskbar replicant — master volume, mute, device switch.
 * Distributed under the terms of the MIT License.
 */

#include "AudioMixerView.h"
#include "AudioMixerStatus.h"
#include "VolumePopupView.h"

#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <AboutWindow.h>
#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Deskbar.h>
#include <Dragger.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <IconUtils.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <ObjectList.h>
#include <OS.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Resources.h>
#include <Roster.h>
#include <Screen.h>
#include <StringView.h>
#include <TextView.h>
#include <ToolTip.h>
#include <ToolTipManager.h>

#include <media2/MediaGraph.h>

#include <MediaMessagingDefs.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AudioMixer"


static const bigtime_t kRefreshInterval = 2000000;


AudioMixerView::AudioMixerView(BRect frame, int32 resizingMode, bool inDeskbar)
	:
	BView(frame, "AudioMixer", resizingMode,
		B_WILL_DRAW | B_PULSE_NEEDED | B_FRAME_EVENTS),
	fInDeskbar(inDeskbar),
	fMuted(false),
	fVolume(0.7f),
	fDefaultSinkId(0),
	fDefaultSourceId(0),
	fIcon(NULL),
	fMutedIcon(NULL),
	fNoDeviceIcon(NULL),
	fRefreshRunner(NULL),
	fVolumePopup(NULL),
	fDraggingInDeskbar(false),
	fGraphReady(false),
	fWarmupPending(false)
{
	_Init();
}


AudioMixerView::AudioMixerView(BMessage* archive)
	:
	BView(archive),
	fInDeskbar(false),
	fMuted(false),
	fVolume(0.7f),
	fDefaultSinkId(0),
	fDefaultSourceId(0),
	fIcon(NULL),
	fMutedIcon(NULL),
	fNoDeviceIcon(NULL),
	fRefreshRunner(NULL),
	fVolumePopup(NULL),
	fDraggingInDeskbar(false),
	fGraphReady(false),
	fWarmupPending(false)
{
	app_info info;
	if (be_app != NULL && be_app->GetAppInfo(&info) == B_OK
		&& !strcasecmp(info.signature, "application/x-vnd.Be-TSKB")) {
		fInDeskbar = true;
	}

	_Init();
}


AudioMixerView::~AudioMixerView()
{
	if (fVolumePopup != NULL) {
		fVolumePopup->PostMessage(B_QUIT_REQUESTED);
		fVolumePopup = NULL;
	}
	delete fIcon;
	delete fMutedIcon;
	delete fNoDeviceIcon;
	delete fRefreshRunner;
}


AudioMixerView*
AudioMixerView::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "AudioMixerView"))
		return NULL;
	return new(std::nothrow) AudioMixerView(archive);
}


status_t
AudioMixerView::Archive(BMessage* into, bool deep) const
{
	status_t s = BView::Archive(into, deep);
	if (s < B_OK) return s;
	s = into->AddString("add_on", kSignature);
	if (s < B_OK) return s;
	return into->AddString("class", "AudioMixerView");
}


void
AudioMixerView::AttachedToWindow()
{
	BView::AttachedToWindow();
	AdoptParentColors();


	if (Parent() != NULL && fRefreshRunner == NULL) {
		fRefreshRunner = new BMessageRunner(this, BMessage(kMsgRefresh),
			kRefreshInterval);
	}

	if (fGraphReady || fWarmupPending) {
		_UpdateState();
		return;
	}
	BMessenger* messenger = new(std::nothrow) BMessenger(this);
	if (messenger == NULL) {
		_UpdateState();
		return;
	}
	thread_id warmup = spawn_thread(_WarmupThreadEntry, "audiomixer warmup",
		B_NORMAL_PRIORITY, messenger);
	if (warmup < 0 || resume_thread(warmup) != B_OK) {
		delete messenger;
		_UpdateState();
		return;
	}
	fWarmupPending = true;
}


status_t
AudioMixerView::_WarmupThreadEntry(void* data)
{
	BMessenger* messenger = (BMessenger*)data;
	BMediaGraph::Instance();
	if (messenger->IsValid())
		messenger->SendMessage(kMsgGraphReady);
	delete messenger;
	return B_OK;
}


void
AudioMixerView::DetachedFromWindow()
{
	delete fRefreshRunner;
	fRefreshRunner = NULL;
	BView::DetachedFromWindow();
}


void
AudioMixerView::Pulse()
{
	_UpdateState();
}


void
AudioMixerView::_Init()
{
	SetViewColor(B_TRANSPARENT_COLOR);
	_LoadIcons();
}


void
AudioMixerView::_LoadIcons()
{
	image_info info;
	if (our_image(info) != B_OK)
		return;

	BFile file(info.name, B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;
	BResources res(&file);
	if (res.InitCheck() != B_OK)
		return;

	const size_t dim = Bounds().IntegerWidth() + 1;
	const BRect r(0, 0, dim - 1, dim - 1);

	auto load = [&](const char* name) -> BBitmap* {
		size_t sz = 0;
		const void* data = res.LoadResource(B_VECTOR_ICON_TYPE, name, &sz);
		if (data == NULL) return NULL;
		BBitmap* bmp = new(std::nothrow) BBitmap(r, B_RGBA32);
		if (bmp == NULL) return NULL;
		if (BIconUtils::GetVectorIcon((const uint8*)data, sz, bmp) != B_OK) {
			delete bmp;
			return NULL;
		}
		return bmp;
	};

	if (fIcon == NULL)         fIcon         = load("Speaker");
	if (fMutedIcon == NULL)    fMutedIcon    = load("SpeakerMuted");
	if (fNoDeviceIcon == NULL) fNoDeviceIcon = load("SpeakerNoDevice");
}


void
AudioMixerView::_UpdateState()
{
	if (!fGraphReady) {
		fDefaultSinkId = 0;
		return;
	}

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL) {
		fDefaultSinkId = 0;
		Invalidate();
		return;
	}

	media_client_id sinkId = 0;
	if (graph->GetDefaultAudioOutput(&sinkId) == B_OK)
		fDefaultSinkId = (uint32)sinkId;

	media_client_id srcId = 0;
	if (graph->GetDefaultAudioInput(&srcId) == B_OK)
		fDefaultSourceId = (uint32)srcId;

	float master = fVolume;
	bool mute = fMuted;


	if (sinkId != 0
			&& graph->GetDeviceVolume(sinkId, &master, &mute) == B_OK) {
		if (master != fVolume || mute != fMuted) {
			fVolume = master;
			fMuted = mute;
			Invalidate();
		}
	}

	BMessage sinkInfo;
	if (graph->GetClientInfo(sinkId, &sinkInfo) == B_OK)
		sinkInfo.FindString("name", &fDefaultSinkName);
	BMessage sourceInfo;
	if (graph->GetClientInfo(srcId, &sourceInfo) == B_OK)
		sourceInfo.FindString("name", &fDefaultSourceName);
}


void
AudioMixerView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRefresh:
			_UpdateState();
			break;

		case kMsgGraphReady:
			fGraphReady = true;
			fWarmupPending = false;
			_UpdateState();
			Invalidate();
			break;

		case 'AMPG':
			fVolumePopup = NULL;
			break;

		case B_MOUSE_WHEEL_CHANGED:
		{
			if (!fGraphReady) break;
			float dy = 0.0f;
			message->FindFloat("be:wheel_delta_y", &dy);
			if (dy == 0.0f) break;
			float step = 0.05f * (-dy);
			float v = fVolume + step;
			if (v < 0.0f) v = 0.0f;
			if (v > 1.0f) v = 1.0f;
			if (v != fVolume) {
				fVolume = v;
				BMediaGraph* g = BMediaGraph::Instance();
				if (g != NULL && fDefaultSinkId != 0)
					g->SetDeviceVolume((media_client_id)fDefaultSinkId, v);
				Invalidate();
			}
			break;
		}

		case kMsgVolumeChanged:
		case kMsgVolumeFinal:
		{
			if (!fGraphReady)
				break;
			float v;
			if (message->FindFloat("volume", &v) == B_OK) {
				fVolume = v;
				BMediaGraph* g = BMediaGraph::Instance();
				if (g != NULL && fDefaultSinkId != 0)
					g->SetDeviceVolume((media_client_id)fDefaultSinkId, v);
				_UpdateState();
				if (message->what == kMsgVolumeFinal && fVolumePopup != NULL)
					fVolumePopup->PostMessage(B_QUIT_REQUESTED);
				Invalidate();
			}
			break;
		}

		case kMsgToggleMute:
		{
			if (!fGraphReady) break;
			fMuted = !fMuted;
			BMediaGraph* g = BMediaGraph::Instance();
			if (g != NULL && fDefaultSinkId != 0)
				g->SetDeviceMute((media_client_id)fDefaultSinkId, fMuted);
			Invalidate();
			break;
		}

		case kMsgSelectOutput:
		case kMsgSelectInput:
		{
			if (!fGraphReady) break;
			uint32 deviceId;
			if (message->FindUInt32("device_id", &deviceId) != B_OK)
				break;
			BMediaGraph* g = BMediaGraph::Instance();
			if (g == NULL) break;
			if (message->what == kMsgSelectOutput)
				g->SetDefaultAudioOutput((media_client_id)deviceId);
			else
				g->SetDefaultAudioInput((media_client_id)deviceId);
			_UpdateState();
			break;
		}

		case kMsgOpenMediaPrefs:
			_OpenMixer();
			break;

		case kMsgQuit:
			if (fInDeskbar) {
				BDeskbar deskbar;
				status_t err = deskbar.RemoveItem(kDeskbarItemName);
				if (err != B_OK) {
					BString text;
					text.SetToFormat(
						B_TRANSLATE("Removing from Deskbar failed:\n%s"),
						strerror(err));
					BAlert* alert = new(std::nothrow) BAlert(
						B_TRANSLATE("Error"), text.String(),
						B_TRANSLATE("OK"));
					if (alert != NULL)
						alert->Go();
				}
			} else
				be_app->PostMessage(B_QUIT_REQUESTED);
			break;

		case kMsgOpenSoundPrefs:
			_LaunchBySig("application/x-vnd.Haiku-Sounds",
				"/system/preferences/Sounds");
			break;

		case B_ABOUT_REQUESTED:
		{
			// Heap, never the stack: Show() starts the window's own looper
			// thread, and a stack BWindow is destroyed at scope exit while
			// that thread still runs. A BWindow deletes itself when it
			// quits. In a replicant that crash lands in Deskbar's process.
			BAboutWindow* about = new BAboutWindow(
				B_TRANSLATE_SYSTEM_NAME("AudioMixer"), kSignature);
			about->AddCopyright(2026, "Vitruvian Project");
			about->AddDescription(
				B_TRANSLATE("Audio mixer — volume, mute, device switch"));
			about->Show();
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
AudioMixerView::MouseDown(BPoint where)
{
	int32 buttons = B_PRIMARY_MOUSE_BUTTON;
	if (Looper() != NULL && Looper()->CurrentMessage() != NULL)
		Looper()->CurrentMessage()->FindInt32("buttons", &buttons);

	BPoint screen = ConvertToScreen(where);

	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		_ShowContextMenu(screen);
	} else if ((buttons & B_TERTIARY_MOUSE_BUTTON) != 0) {
		if (Looper() != NULL)
			Looper()->PostMessage(kMsgToggleMute);
	} else {
		_ShowVolumePopup(screen);
	}
}


void
AudioMixerView::MouseUp(BPoint)
{
	fDraggingInDeskbar = false;
}


void
AudioMixerView::MouseMoved(BPoint, uint32,
	const BMessage*)
{
}


void
AudioMixerView::Draw(BRect)
{
	if (fIcon == NULL) return;
	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);

	BBitmap* icon = fNoDeviceIcon;
	if (fDefaultSinkId != 0)
		icon = fMuted ? fMutedIcon : fIcon;
	if (icon == NULL)
		icon = fIcon;
	if (icon == NULL) return;

	const BRect src = icon->Bounds();
	const BRect dst = Bounds();

	float scale = std::min(dst.Width() / src.Width(),
		dst.Height() / src.Height());
	float w = src.Width()  * scale;
	float h = src.Height() * scale;
	BRect target((dst.left + dst.right - w) / 2.0f,
		(dst.top + dst.bottom - h) / 2.0f,
		(dst.left + dst.right + w) / 2.0f,
		(dst.top + dst.bottom + h) / 2.0f);
	DrawBitmap(icon, src, target);
}


void
AudioMixerView::FrameResized(float, float)
{
	_LoadIcons();
	Invalidate();
}


void
AudioMixerView::_ShowVolumePopup(BPoint where)
{
	if (fVolumePopup != NULL) {


		fVolumePopup->PostMessage(B_QUIT_REQUESTED);
		return;
	}
	fVolumePopup = new VolumePopupView(this, where, fVolume, fMuted,
		fDefaultSinkName);
	fVolumePopup->Show();
}


void
AudioMixerView::_ShowContextMenu(BPoint where)
{
	BPopUpMenu menu("AudioMixer", false, false);
	menu.SetFont(be_plain_font);


	BMenuItem* muteItem = new BMenuItem(
		fMuted ? B_TRANSLATE("Unmute") : B_TRANSLATE("Mute"),
		new BMessage(kMsgToggleMute));
	menu.AddItem(muteItem);

	menu.AddSeparatorItem();

		BMediaGraph* g = fGraphReady ? BMediaGraph::Instance() : NULL;
		if (g != NULL) {
			BMenu* outMenu = new BMenu(B_TRANSLATE("Output Devices"));
			BObjectList<media_client_id, true> clients;
			clients.MakeEmpty();
			g->GetClients(&clients);
			for (int32 i = 0; i < clients.CountItems(); i++) {
				media_client_id id = *clients.ItemAt(i);
				BMessage info;
				if (g->GetClientInfo(id, &info) != B_OK) continue;
				bool isSink = false;
				info.FindBool("is.sink", &isSink);
				if (!isSink) continue;
				BString name;
				info.FindString("name", &name);
				BMessage* msg = new BMessage(kMsgSelectOutput);
				msg->AddUInt32("device_id", (uint32)id);
				BMenuItem* item = new BMenuItem(name.String(), msg);
				item->SetMarked((uint32)id == fDefaultSinkId);
				outMenu->AddItem(item);
			}
			BMenuItem* outItem = new BMenuItem(outMenu);
			menu.AddItem(outItem);
			outMenu->SetTargetForItems(this);


			BMenu* inMenu = new BMenu(B_TRANSLATE("Input Devices"));
			for (int32 i = 0; i < clients.CountItems(); i++) {
				media_client_id id = *clients.ItemAt(i);
				BMessage info;
				if (g->GetClientInfo(id, &info) != B_OK) continue;
				bool isSource = false;
				info.FindBool("is.source", &isSource);
				if (!isSource) continue;
				BString name;
				info.FindString("name", &name);
				BMessage* msg = new BMessage(kMsgSelectInput);
				msg->AddUInt32("device_id", (uint32)id);
				BMenuItem* item = new BMenuItem(name.String(), msg);
				item->SetMarked((uint32)id == fDefaultSourceId);
				inMenu->AddItem(item);
			}
			BMenuItem* inItem = new BMenuItem(inMenu);
			menu.AddItem(inItem);
			inMenu->SetTargetForItems(this);
		}

	menu.AddSeparatorItem();
	menu.AddItem(new BMenuItem(
		B_TRANSLATE("Media preferences" B_UTF8_ELLIPSIS),
		new BMessage(kMsgOpenMediaPrefs)));
	menu.AddItem(new BMenuItem(
		B_TRANSLATE("Sounds preferences" B_UTF8_ELLIPSIS),
		new BMessage(kMsgOpenSoundPrefs)));

	menu.AddSeparatorItem();
	menu.AddItem(new BMenuItem(B_TRANSLATE("About AudioMixer" B_UTF8_ELLIPSIS),
		new BMessage(B_ABOUT_REQUESTED)));

	menu.AddSeparatorItem();
	menu.AddItem(new BMenuItem(
		fInDeskbar ? B_TRANSLATE("Remove AudioMixer")
			: B_TRANSLATE("Quit AudioMixer"),
		new BMessage(kMsgQuit)));

	menu.SetTargetForItems(this);
	menu.Go(where, true, true, BRect(where - BPoint(4, 4),
		where + BPoint(4, 4)));
}


void
AudioMixerView::_OpenMixer()
{
	BMessage select(kMsgSelectSection);
	select.AddInt32("section", kMediaSectionOutput);

	app_info info;
	if (be_roster->GetAppInfo(kMediaAppSignature, &info) == B_OK) {
		BMessenger(kMediaAppSignature).SendMessage(&select);
		be_roster->ActivateApp(info.team);
		return;
	}

	if (be_roster->Launch(kMediaAppSignature, &select) == B_OK)
		return;


	entry_ref ref;
	if (get_ref_for_path("/system/preferences/Media", &ref) == B_OK)
		be_roster->Launch(&ref, &select);
}


void
AudioMixerView::_LaunchBySig(const char* sig, const char* path)
{
	status_t s = be_roster->Launch(sig);
	if (s == B_ALREADY_RUNNING) {
		app_info ai;
		if (be_roster->GetAppInfo(sig, &ai) == B_OK)
			be_roster->ActivateApp(ai.team);
		return;
	}
	if (s == B_OK)
		return;

	entry_ref ref;
	if (get_ref_for_path(path, &ref) == B_OK)
		be_roster->Launch(&ref);
}


extern "C" _EXPORT BView*
instantiate_deskbar_item(float maxWidth, float maxHeight)
{
	return new(std::nothrow) AudioMixerView(
		BRect(0, 0, maxHeight - 1, maxHeight - 1),
		B_FOLLOW_LEFT | B_FOLLOW_TOP, true);
}
