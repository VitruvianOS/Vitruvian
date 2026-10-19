/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "CortexWindow.h"

#include <Alert.h>
#include <Button.h>
#include <Directory.h>
#include <Entry.h>
#include <FilePanel.h>
#include <File.h>
#include <FindDirectory.h>
#include <GroupView.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Slider.h>
#include <String.h>
#include <View.h>
#include <Window.h>

#include <image.h>
#include <stdio.h>
#include <string.h>

#include <media2/AudioFilter.h>
#include <media2/Controllable.h>
#include <media2/FilterCatalog.h>
#include <media2/MediaTheme.h>
#include <media2/ParameterWeb.h>
#include <media2/RoutingStore.h>
#include <media2/VideoFilter.h>

#include <MediaUnitAddOn.h>

#include "CortexGraphView.h"
#include "CortexMonitor.h"
#include "CortexNodes.h"


// A scanned media add-on image and the units it offers.
struct CortexAddOn {
	BString			path;
	image_id		image;
	int32			count;
	BObjectList<BString, true>	names;
	BMediaUnit*		(*instantiate)(int32);
	BMediaUnit*		(*loadFile)(const char*);

	CortexAddOn()
		:
		image(-1),
		count(0),
		names(true),
		instantiate(NULL),
		loadFile(NULL)
	{
	}

	~CortexAddOn()
	{
		if (image >= 0)
			unload_add_on(image);
	}
};


struct CortexEndpoint {
	BMediaOutput*				output;
	BMediaInput*				input;
	BMediaGraph::ForeignPort	port;

	CortexEndpoint() : output(NULL), input(NULL) {}
};


// One open monitor: the sink, and the splitter that shares the source
// with its existing link when there is one.
struct CortexMonitorEntry {
	CortexMonitorWindow*		window;
	CortexVideoSink*			sink;
	BVideoSplitter*				splitter;
	BMediaOutput*				source;
	BMediaInput*				target;
	BMediaOutput*				targetTap;
	BMediaOutput*				sinkTap;
	CortexEndpoint				foreignSource;
	bool						isForeign;

	CortexMonitorEntry()
		:
		window(NULL), sink(NULL), splitter(NULL), source(NULL),
		target(NULL), targetTap(NULL), sinkTap(NULL), isForeign(false)
	{
	}
};


static bool
_IsFilterUnit(BMediaClient* client)
{
	return dynamic_cast<BVideoFilterUnit*>(client) != NULL
		|| dynamic_cast<BAudioFilterUnit*>(client) != NULL;
}


static status_t
_CortexWarmupEntry(void* data)
{
	BMessenger* messenger = (BMessenger*)data;
	BMediaGraph::Instance();
	if (messenger->IsValid())
		messenger->SendMessage(kMsgGraphReady);
	delete messenger;
	return B_OK;
}


CortexVolumeWindow::CortexVolumeWindow(media_client_id id, bool isStream,
	const BString& title, float value)
	:
	BWindow(BRect(120, 120, 420, 220), title.String(), B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_CLOSABLE | B_AVOID_FOCUS),
	fId(id),
	fIsStream(isStream),
	fSlider(NULL)
{
	BView* content = new BGroupView(B_VERTICAL, 0.0f);
	BLayoutBuilder::Group<>(content, B_VERTICAL, 10)
		.SetInsets(10)
		.Add(fSlider = new BSlider("volume", "Volume", NULL,
			0, 100, B_HORIZONTAL))
		.AddGlue();
	fSlider->SetValue((int32)(value * 100.0f));
	fSlider->SetModificationMessage(new BMessage(kMsgVolumeSlider));
	fSlider->SetTarget(this);
	AddChild(content);
}


void
CortexVolumeWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgVolumeSlider:
		{
			int32 value = 0;
			if (fSlider != NULL)
				value = fSlider->Value();
			float volume = (float)value / 100.0f;
			BMediaGraph* graph = BMediaGraph::Instance();
			if (graph != NULL) {
				if (fIsStream)
					graph->SetStreamVolume((media_client_id)fId, volume,
						false);
				else
					graph->SetDeviceVolume((media_client_id)fId, volume);
			}
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


CortexWindow::CortexWindow()
	:
	BWindow(BRect(80, 80, 1100, 760), "Cortex", B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS),
	fView(NULL),
	fModel(NULL),
	fOwnClients(true),
	fForeignLinks(true),
	fGraphReady(false),
	fRebuildPending(false),
	fMeteredIds(true),
	fMonitors(true),
	fAddMenu(NULL),
	fFilePanel(NULL),
	fAddOns(true)
{
	BRect bounds = Bounds();

	BMenu* fileMenu = new BMenu("File");
	fileMenu->AddItem(new BMenuItem("Quit",
		new BMessage(B_QUIT_REQUESTED), 'Q'));

	fAddMenu = new BMenu("Add");

	BMenuBar* menuBar = new BMenuBar("menu bar");
	menuBar->AddItem(fileMenu);
	menuBar->AddItem(fAddMenu);
	fileMenu->SetTargetForItems(this);

	BRect graphFrame = bounds;
	graphFrame.top += menuBar->Frame().Height() + 1.0f;

	fModel = new(std::nothrow) CortexModel();
	fView = new(std::nothrow) CortexGraphView(graphFrame);

	BButton* restoreButton = new BButton("restore", "Restore routes",
		new BMessage(kMsgRestoreRoutes));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0)
		.Add(menuBar)
		.AddGroup(B_HORIZONTAL, 8)
			.SetInsets(8, 4, 8, 4)
			.Add(restoreButton)
			.AddGlue()
		.End()
		.Add(fView);

	if (fView != NULL)
		fView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	BMessenger* messenger = new(std::nothrow) BMessenger(this);
	if (messenger != NULL) {
		thread_id thread = spawn_thread(_CortexWarmupEntry, "cortex warmup",
			B_NORMAL_PRIORITY, messenger);
		if (thread >= B_OK && resume_thread(thread) == B_OK)
			return;
		delete messenger;
	}

	_GraphReady();
}


CortexWindow::~CortexWindow()
{
	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph != NULL) {
		graph->StopWatching(BMessenger(this));
		for (int32 i = 0; i < fMeteredIds.CountItems(); i++)
			graph->StopMeteringDevice((media_client_id)*fMeteredIds.ItemAt(i));
	}

	while (fMonitors.CountItems() > 0)
		_CloseMonitor(fMonitors.ItemAt(0));

	for (int32 i = 0; i < fOwnClients.CountItems(); i++) {
		BMediaClient* client = fOwnClients.ItemAt(i);
		if (client == NULL)
			continue;
		client->Stop();
		delete client;
	}
	fOwnClients.MakeEmpty();

	// Units are gone; the add-on images can unload now.
	fAddOns.MakeEmpty();
	delete fFilePanel;

	delete fModel;
	fModel = NULL;
}


void
CortexWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgGraphReady:
			_GraphReady();
			break;

		case kMsgRebuild:
			fRebuildPending = false;
			_Rebuild();
			break;

		case BMediaGraph::B_MEDIA_CLIENT_ADDED:
		case BMediaGraph::B_MEDIA_CLIENT_REMOVED:
		case BMediaGraph::B_MEDIA_DEFAULTS_CHANGED:
		case BMediaGraph::B_MEDIA_VOLUME_CHANGED:
		case BMediaGraph::B_MEDIA_STREAMS_CHANGED:
		case BMediaGraph::B_MEDIA_LINKS_CHANGED:
			if (!fRebuildPending) {
				fRebuildPending = true;
				PostMessage(kMsgRebuild);
			}
			break;

		case kMsgPeakUpdate:
		{
			uint32 nodeId = 0;
			float peak = 0.0f;
			message->FindUInt32("node_id", &nodeId);
			message->FindFloat("peak", &peak);
			if (fModel != NULL) {
				int32 index = fModel->FindNode(kCortexNodeDevice,
					(media_client_id)nodeId);
				if (index >= 0) {
					CortexNode* node = fModel->NodeAt(index);
					if (node != NULL) {
						node->peak = peak;
						if (fView != NULL)
							fView->Invalidate(node->Frame());
					}
				}
			}
			break;
		}

		case kMsgVolumeSlider:
		case kMsgDeviceVolume:
		case kMsgStreamVolume:
		case kMsgToggleMute:
		case kMsgSetDefaultOut:
		case kMsgSetDefaultIn:
		case kMsgSetProfile:
		case kMsgSetPort:
		case kMsgMoveStream:
		case kMsgSendTestNote:
		case kMsgDisconnectOwn:
		{
			BMediaGraph* graph = BMediaGraph::Instance();
			if (graph == NULL)
				break;

			uint32 id = 0;
			message->FindUInt32("device_id", &id);

			switch (message->what) {
				case kMsgToggleMute:
				{
					bool mute = false;
					graph->GetDeviceVolume((media_client_id)id, NULL, &mute);
					graph->SetDeviceMute((media_client_id)id, !mute);
					break;
				}
				case kMsgSetDefaultOut:
					graph->SetDefaultAudioOutput((media_client_id)id);
					break;
				case kMsgSetDefaultIn:
					graph->SetDefaultAudioInput((media_client_id)id);
					break;
				case kMsgSetProfile:
				{
					int32 index = 0;
					message->FindInt32("index", &index);
					graph->SetDeviceProfile((media_client_id)id, index);
					break;
				}
				case kMsgSetPort:
				{
					int32 portId = 0;
					message->FindInt32("port_id", &portId);
					graph->SetDevicePort((media_client_id)id, portId);
					break;
				}
				case kMsgMoveStream:
				{
					uint32 target = 0;
					message->FindUInt32("target_id", &target);
					graph->MoveStream((media_client_id)id,
						(media_client_id)target);
					break;
				}
				case kMsgSendTestNote:
				{
					for (int32 i = 0; i < fOwnClients.CountItems(); i++) {
						BMediaClient* client = fOwnClients.ItemAt(i);
						if (client == NULL || client->Id() != (media_client_id)id)
							continue;
						CortexMidiOutputUnit* unit
							= dynamic_cast<CortexMidiOutputUnit*>(client);
						if (unit != NULL)
							_ReportStatus("send test note",
								unit->SendTestNote());
					}
					break;
				}
				case kMsgDisconnectOwn:
				{
					int32 nodeIndex = -1;
					int32 pinIndex = -1;
					message->FindInt32("node", &nodeIndex);
					message->FindInt32("pin", &pinIndex);
					_DisconnectOwn(nodeIndex, pinIndex);
					break;
				}
				default:
					break;
			}
			_Rebuild();
			break;
		}

		case kMsgControlPanel:
		{
			int32 nodeIndex = -1;
			message->FindInt32("node", &nodeIndex);
			_OpenControlPanel(nodeIndex);
			break;
		}

		case kMsgRestoreRoutes:
			_RestoreRoutes();
			break;

		case kMsgAddUnit:
			_AddUnit(message);
			break;

		case kMsgLoadPlugin:
		{
			if (fFilePanel == NULL) {
				BMessage panelMessage(B_REFS_RECEIVED);
				fFilePanel = new(std::nothrow) BFilePanel(B_OPEN_PANEL,
					new BMessenger(this), NULL, B_FILE_NODE | B_DIRECTORY_NODE,
					false, &panelMessage);
			}
			if (fFilePanel != NULL)
				fFilePanel->Show();
			else
				_ReportStatus("load plugin", B_NO_MEMORY);
			break;
		}

		case B_REFS_RECEIVED:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK) {
				BPath path(&ref);
				if (path.InitCheck() == B_OK)
					_LoadPluginFile(path.Path());
			}
			break;
		}

		case kMsgInsertFilter:
		{
			int32 linkIndex = -1;
			const char* id = NULL;
			message->FindInt32("link", &linkIndex);
			if (message->FindString("id", &id) == B_OK)
				_InsertFilter(linkIndex, id);
			break;
		}

		case kMsgRemoveFilter:
		{
			int32 linkIndex = -1;
			message->FindInt32("link", &linkIndex);
			_RemoveFilter(linkIndex);
			break;
		}

		case kMsgMonitorPin:
		{
			int32 nodeIndex = -1;
			int32 pinIndex = -1;
			message->FindInt32("node", &nodeIndex);
			message->FindInt32("pin", &pinIndex);
			_OpenMonitor(nodeIndex, pinIndex);
			break;
		}

		case kMsgMonitorClosed:
		{
			void* window = NULL;
			message->FindPointer("window", &window);
			for (int32 i = 0; i < fMonitors.CountItems(); i++) {
				if (fMonitors.ItemAt(i)->window == window) {
					_CloseMonitor(fMonitors.ItemAt(i));
					break;
				}
			}
			_Rebuild();
			break;
		}

		case kMsgRemoveLink:
		{
			int32 linkIndex = -1;
			message->FindInt32("link", &linkIndex);
			RemoveLink(linkIndex);
			break;
		}

		case kMsgInfo:
		{
			int32 nodeIndex = -1;
			message->FindInt32("node", &nodeIndex);
			_OpenInfo(nodeIndex);
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
CortexWindow::QuitRequested()
{
	_SaveSettings();
	return true;
}


void
CortexWindow::_GraphReady()
{
	if (fGraphReady)
		return;
	fGraphReady = true;

	int32 count = 0;
	BMediaClient** clients = CreateCortexDemoClients(&count);
	if (clients != NULL) {
		for (int32 i = 0; i < count; i++) {
			clients[i]->Start();
			fOwnClients.AddItem(clients[i]);
		}
		delete[] clients;
	}

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph != NULL)
		graph->StartWatching(BMessenger(this),
			BMediaGraph::B_MEDIA_ALL_NOTIFICATIONS);

	_LoadSettings();

	_Rebuild();
}


void
CortexWindow::_Rebuild()
{
	if (fModel == NULL || fView == NULL)
		return;

	// The kit meter keeps running across a model swap; carry the last
	// level over so the bar does not blink to zero on every rebuild.
	BObjectList<CortexNode, true> oldNodes(true);
	for (int32 i = 0; i < fModel->CountNodes(); i++) {
		CortexNode* node = fModel->NodeAt(i);
		if (node == NULL || !node->metered)
			continue;
		CortexNode* copy = new(std::nothrow) CortexNode();
		if (copy == NULL)
			continue;
		copy->kind = node->kind;
		copy->id = node->id;
		copy->peak = node->peak;
		oldNodes.AddItem(copy);
	}

	media_client_id defaultOut = 0;
	media_client_id defaultIn = 0;
	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph != NULL) {
		graph->GetDefaultAudioOutput(&defaultOut);
		graph->GetDefaultAudioInput(&defaultIn);
	}

	fModel->Rebuild(&fOwnClients, &fForeignLinks);

	if (graph != NULL) {
		// Drop meters for devices that left the default role, keep
		// the rest: restarting a running meter only loses samples.
		media_client_id meters[2] = { defaultOut, defaultIn };
		for (int32 i = fMeteredIds.CountItems() - 1; i >= 0; i--) {
			uint32 id = *fMeteredIds.ItemAt(i);
			if (meters[0] == (media_client_id)id
				|| meters[1] == (media_client_id)id) {
				continue;
			}
			graph->StopMeteringDevice((media_client_id)id);
			fMeteredIds.RemoveItemAt(i);
		}

		for (int32 i = 0; i < 2; i++) {
			if (meters[i] == 0)
				continue;
			bool alreadyMetering = false;
			for (int32 j = 0; j < fMeteredIds.CountItems(); j++) {
				if ((media_client_id)*fMeteredIds.ItemAt(j) == meters[i]) {
					alreadyMetering = true;
					break;
				}
			}
			if (alreadyMetering)
				continue;
			if (graph->StartMeteringDevice((media_client_id)meters[i],
					BMessenger(this)) == B_OK) {
				uint32* stored = new(std::nothrow) uint32(meters[i]);
				if (stored != NULL)
					fMeteredIds.AddItem(stored);
			}
		}

		for (int32 i = 0; i < fMeteredIds.CountItems(); i++) {
			int32 index = fModel->FindNode(kCortexNodeDevice,
				(media_client_id)*fMeteredIds.ItemAt(i));
			CortexNode* node = index >= 0 ? fModel->NodeAt(index) : NULL;
			if (node != NULL)
				node->metered = true;
		}
	}

	for (int32 i = 0; i < oldNodes.CountItems(); i++) {
		CortexNode* oldNode = oldNodes.ItemAt(i);
		if (oldNode == NULL)
			continue;
		int32 index = fModel->FindNode(oldNode->kind, oldNode->id);
		CortexNode* node = index >= 0 ? fModel->NodeAt(index) : NULL;
		if (node != NULL && node->metered)
			node->peak = oldNode->peak;
	}

	fView->SetModel(fModel);
}


static void
_CortexSettingsPath(BPath* path)
{
	if (path == NULL)
		return;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, path) != B_OK)
		return;
	path->Append("Cortex");
}


void
CortexWindow::_LoadSettings()
{
	if (fModel == NULL)
		return;

	BPath path;
	_CortexSettingsPath(&path);
	if (path.Path() == NULL)
		return;

	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BMessage archive;
	if (archive.Unflatten(&file) == B_OK)
		fModel->LoadOrigins(&archive);
}


void
CortexWindow::_SaveSettings()
{
	if (fModel == NULL)
		return;

	BPath path;
	_CortexSettingsPath(&path);
	if (path.Path() == NULL)
		return;

	status_t status = create_directory(path.Path(), 0755);
	if (status != B_OK && status != B_FILE_EXISTS)
		return;

	BMessage archive;
	fModel->SaveOrigins(&archive);

	BFile file(path.Path(), B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return;
	archive.Flatten(&file);
}


void
CortexWindow::ShowNodeMenu(int32 nodeIndex, BPoint screenPoint)
{
	if (fModel == NULL)
		return;
	CortexNode* node = fModel->NodeAt(nodeIndex);
	if (node == NULL)
		return;

	BMediaGraph* graph = BMediaGraph::Instance();
	BString title;
	title.SetToFormat("%s (%s)", node->name.String(),
		CortexKindLabel(node->kind));

	BPopUpMenu* menu = new BPopUpMenu("node menu");
	menu->AddItem(new BMenuItem("Info...",
		new BMessage(kMsgInfo)));
	menu->AddItem(new BMenuItem("Control panel",
		new BMessage(kMsgControlPanel)));

	BMessage* infoMessage = menu->ItemAt(0)->Message();
	infoMessage->AddInt32("node", nodeIndex);
	BMessage* panelMessage = menu->ItemAt(1)->Message();
	panelMessage->AddInt32("node", nodeIndex);

	if (node->ownClient == NULL)
		menu->ItemAt(1)->SetEnabled(false);

	if (node->kind == kCortexNodeDevice && graph != NULL) {
		menu->AddSeparatorItem();

		BMessage* muteMessage = new BMessage(kMsgToggleMute);
		muteMessage->AddUInt32("device_id", (uint32)node->id);
		BMenuItem* muteItem = new BMenuItem(
			node->muted ? "Unmute" : "Mute", muteMessage);
		muteItem->SetTarget(this);
		menu->AddItem(muteItem);

		BMessage* volumeMessage = new BMessage(kMsgDeviceVolume);
		volumeMessage->AddUInt32("device_id", (uint32)node->id);
		volumeMessage->AddFloat("value", node->volume);
		BMenuItem* volumeItem = new BMenuItem("Volume...", volumeMessage);
		volumeItem->SetTarget(this);
		menu->AddItem(volumeItem);

		if (node->isSink) {
			BMessage* defaultMessage = new BMessage(kMsgSetDefaultOut);
			defaultMessage->AddUInt32("device_id", (uint32)node->id);
			BMenuItem* defaultItem = new BMenuItem("Set default output",
				defaultMessage);
			defaultItem->SetTarget(this);
			menu->AddItem(defaultItem);
		}
		if (node->isSource) {
			BMessage* defaultMessage = new BMessage(kMsgSetDefaultIn);
			defaultMessage->AddUInt32("device_id", (uint32)node->id);
			BMenuItem* defaultItem = new BMenuItem("Set default input",
				defaultMessage);
			defaultItem->SetTarget(this);
			menu->AddItem(defaultItem);
		}

		if (node->hasProfiles && graph != NULL) {
			BObjectList<BMediaGraph::DeviceProfileInfo, true> profiles;
			int32 active = -1;
			if (graph->GetDeviceProfiles((media_client_id)node->id,
					&profiles, &active) == B_OK
				&& profiles.CountItems() > 0) {
				BMenu* profileMenu = new BMenu("Profile");
				for (int32 i = 0; i < profiles.CountItems(); i++) {
					BMediaGraph::DeviceProfileInfo* profile
						= profiles.ItemAt(i);
					BMessage* profileMessage = new BMessage(kMsgSetProfile);
					profileMessage->AddUInt32("device_id", (uint32)node->id);
					profileMessage->AddInt32("index", profile->index);
					BMenuItem* item = new BMenuItem(profile->name.String(),
						profileMessage);
					item->SetTarget(this);
					if (profile->index == active)
						item->SetMarked(true);
					if (!profile->available)
						item->SetEnabled(false);
					profileMenu->AddItem(item);
				}
				menu->AddItem(profileMenu);
			}
		}

		if (node->hasPorts && graph != NULL) {
			BObjectList<BMediaGraph::DevicePortInfo, true> ports;
			if (graph->GetDevicePorts((media_client_id)node->id,
					&ports) == B_OK && ports.CountItems() > 0) {
				BMenu* portMenu = new BMenu("Port");
				for (int32 i = 0; i < ports.CountItems(); i++) {
					BMediaGraph::DevicePortInfo* port = ports.ItemAt(i);
					BMessage* portMessage = new BMessage(kMsgSetPort);
					portMessage->AddUInt32("device_id", (uint32)node->id);
					portMessage->AddInt32("port_id", port->portId);
					BMenuItem* item = new BMenuItem(port->name.String(),
						portMessage);
					item->SetTarget(this);
					if (port->active)
						item->SetMarked(true);
					portMenu->AddItem(item);
				}
				menu->AddItem(portMenu);
			}
		}
	}

	if (node->kind == kCortexNodeStream && graph != NULL) {
		menu->AddSeparatorItem();

		BMessage* volumeMessage = new BMessage(kMsgStreamVolume);
		volumeMessage->AddUInt32("device_id", (uint32)node->id);
		volumeMessage->AddFloat("value", node->volume);
		BMenuItem* volumeItem = new BMenuItem("Volume...", volumeMessage);
		volumeItem->SetTarget(this);
		menu->AddItem(volumeItem);

		BObjectList<BMediaGraph::DevicePortInfo, true> unused;
		BObjectList<media_client_id, true> devices;
		if (graph->GetClients(&devices) == B_OK && devices.CountItems() > 0) {
			BMenu* moveMenu = new BMenu("Move to device");
			for (int32 i = 0; i < devices.CountItems(); i++) {
				media_client_id deviceId = *devices.ItemAt(i);
				BMessage info;
				BString name;
				if (graph->GetClientInfo(deviceId, &info) == B_OK)
					info.FindString("name", &name);
				BMessage* moveMessage = new BMessage(kMsgMoveStream);
				moveMessage->AddUInt32("device_id", (uint32)node->id);
				moveMessage->AddUInt32("target_id", (uint32)deviceId);
				BMenuItem* item = new BMenuItem(
					name.Length() > 0 ? name.String() : "device",
					moveMessage);
				item->SetTarget(this);
				moveMenu->AddItem(item);
			}
			menu->AddItem(moveMenu);
		}
	}

	if (node->kind == kCortexNodeOwn && node->ownClient != NULL) {
		menu->AddSeparatorItem();

		bool isMidi = dynamic_cast<CortexMidiOutputUnit*>(node->ownClient)
			!= NULL;
		if (isMidi) {
			BMessage* noteMessage = new BMessage(kMsgSendTestNote);
			noteMessage->AddUInt32("device_id", (uint32)node->id);
			BMenuItem* noteItem = new BMenuItem("Send test note",
				noteMessage);
			noteItem->SetTarget(this);
			menu->AddItem(noteItem);
		}

		for (int32 p = 0; p < node->pins.CountItems(); p++) {
			CortexPin* pin = node->pins.ItemAt(p);
			if (pin == NULL || !pin->connected)
				continue;
			BMessage* disconnectMessage = new BMessage(kMsgDisconnectOwn);
			disconnectMessage->AddInt32("node", nodeIndex);
			disconnectMessage->AddInt32("pin", p);
			BMenuItem* item = new BMenuItem(
				BString().SetToFormat("Disconnect %s",
					pin->name.String()).String(),
				disconnectMessage);
			item->SetTarget(this);
			menu->AddItem(item);
		}
	}

	bool separated = false;
	for (int32 p = 0; p < node->pins.CountItems(); p++) {
		CortexPin* pin = node->pins.ItemAt(p);
		if (pin == NULL || !pin->isOutput || pin->kind != kCortexPinVideo)
			continue;
		if (!separated) {
			menu->AddSeparatorItem();
			separated = true;
		}
		BMessage* monitorMessage = new BMessage(kMsgMonitorPin);
		monitorMessage->AddInt32("node", nodeIndex);
		monitorMessage->AddInt32("pin", p);
		BMenuItem* monitorItem = new BMenuItem(
			BString().SetToFormat("Monitor %s",
				pin->name.String()).String(), monitorMessage);
		monitorItem->SetTarget(this);
		menu->AddItem(monitorItem);
	}

	menu->AddSeparatorItem();
	BMessage* bottomInfoMessage = new BMessage(kMsgInfo);
	bottomInfoMessage->AddInt32("node", nodeIndex);
	BMenuItem* bottomInfo = new BMenuItem("Graph info...", bottomInfoMessage);
	bottomInfo->SetTarget(this);
	menu->AddItem(bottomInfo);

	BMenuItem* selected = menu->Go(screenPoint);
	if (selected != NULL && selected->Message() != NULL
		&& selected->Message()->what == kMsgDeviceVolume) {
		float value = node->volume;
		selected->Message()->FindFloat("value", &value);
		_OpenVolume((media_client_id)node->id, false, title, value);
	} else if (selected != NULL && selected->Message() != NULL
		&& selected->Message()->what == kMsgStreamVolume) {
		float value = node->volume;
		selected->Message()->FindFloat("value", &value);
		_OpenVolume((media_client_id)node->id, true, title, value);
	}

	delete menu;
}


void
CortexWindow::TryConnectPins(int32 outNodeIndex, int32 outPinIndex,
	int32 inNodeIndex, int32 inPinIndex)
{
	if (fModel == NULL)
		return;

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return;

	CortexNode* outNode = fModel->NodeAt(outNodeIndex);
	CortexNode* inNode = fModel->NodeAt(inNodeIndex);
	if (outNode == NULL || inNode == NULL)
		return;

	CortexPin* a = outNode->pins.ItemAt(outPinIndex);
	CortexPin* b = inNode->pins.ItemAt(inPinIndex);
	if (a == NULL || b == NULL)
		return;

	CortexPin* outPin = a->isOutput ? a : b;
	CortexPin* inPin = a->isOutput ? b : a;
	CortexNode* outPinNode = a->isOutput ? outNode : inNode;
	CortexNode* inPinNode = a->isOutput ? inNode : outNode;

	if (outPin == inPin || !outPin->isOutput || inPin->isOutput) {
		BAlert* alert = new(std::nothrow) BAlert("Connect",
			"A link needs one output pin and one input pin.", "OK");
		if (alert != NULL)
			alert->Go();
		return;
	}

	if (outPin->kind != kCortexPinUnknown && inPin->kind != kCortexPinUnknown
		&& outPin->kind != inPin->kind) {
		BAlert* alert = new(std::nothrow) BAlert("Connect",
			"Those pins carry different media types.", "OK");
		if (alert != NULL)
			alert->Go();
		return;
	}

	if (outPinNode->ownClient == NULL && inPinNode->ownClient == NULL) {
		BAlert* alert = new(std::nothrow) BAlert("Connect",
			"Foreign to foreign links belong to WirePlumber. Cortex is a "
			"view and a trigger, not a policy engine.", "OK");
		if (alert != NULL)
			alert->Go();
		return;
	}

	status_t status = _ConnectPair(outPinNode, outPin, inPinNode, inPin);
	if (status == B_OK) {
		BMediaRoute route;
		route.fromNode = outPinNode->name;
		route.fromPort = outPin->name;
		route.toNode = inPinNode->name;
		route.toPort = inPin->name;
		BRoutingStore* store = BRoutingStore::Instance();
		if (store != NULL)
			store->AddRoute(route);
	}

	if (status != B_OK)
		_ReportStatus("connect", status);

	_Rebuild();
}


status_t
CortexWindow::_ConnectPair(CortexNode* outPinNode, CortexPin* outPin,
	CortexNode* inPinNode, CortexPin* inPin)
{
	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	status_t status = B_BAD_VALUE;

	if (outPinNode->ownClient != NULL && inPinNode->ownClient != NULL
		&& outPin->output != NULL && inPin->input != NULL)
		status = graph->Connect(outPin->output, inPin->input);
	else if (outPinNode->ownClient != NULL && outPin->output != NULL
		&& inPin->foreignPortId != 0) {
		BMediaGraph::ForeignPort port;
		status = _ForeignPortById(inPin->foreignNodeId, inPin->foreignPortId,
			&port);
		if (status == B_OK) {
			status = graph->Connect(outPin->output, &port);
			if (status == B_OK) {
				CortexForeignLinkEntry* entry
					= new(std::nothrow) CortexForeignLinkEntry();
				if (entry != NULL) {
					entry->outputIsOwn = true;
					entry->ownOutput = outPin->output;
					entry->ownInput = NULL;
					entry->foreignNodeId = inPin->foreignNodeId;
					entry->foreignPortId = inPin->foreignPortId;
					fForeignLinks.AddItem(entry);
				}
			}
		}
	} else if (inPinNode->ownClient != NULL && inPin->input != NULL
		&& outPin->foreignPortId != 0) {
		BMediaGraph::ForeignPort port;
		status = _ForeignPortById(outPin->foreignNodeId, outPin->foreignPortId,
			&port);
		if (status == B_OK) {
			status = graph->Connect(&port, inPin->input);
			if (status == B_OK) {
				CortexForeignLinkEntry* entry
					= new(std::nothrow) CortexForeignLinkEntry();
				if (entry != NULL) {
					entry->outputIsOwn = false;
					entry->ownOutput = NULL;
					entry->ownInput = inPin->input;
					entry->foreignNodeId = outPin->foreignNodeId;
					entry->foreignPortId = outPin->foreignPortId;
					fForeignLinks.AddItem(entry);
				}
			}
		}
	}

	return status;
}


void
CortexWindow::RemoveLink(int32 linkIndex)
{
	if (fModel == NULL)
		return;

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return;

	CortexLink* link = fModel->LinkAt(linkIndex);
	if (link == NULL)
		return;

	CortexNode* outNode = fModel->NodeAt(link->outputNode);
	CortexNode* inNode = fModel->NodeAt(link->inputNode);
	CortexPin* outPin = link->outputPin >= 0 && outNode != NULL
		? outNode->pins.ItemAt(link->outputPin) : NULL;
	CortexPin* inPin = link->inputPin >= 0 && inNode != NULL
		? inNode->pins.ItemAt(link->inputPin) : NULL;

	// In-process links have a proper teardown path; everything else is
	// destroyed by its registry id, whoever created it.
	status_t status = B_ENTRY_NOT_FOUND;
	if (outPin != NULL && outPin->output != NULL
		&& inPin != NULL && inPin->input != NULL)
		status = graph->Disconnect(outPin->output, inPin->input);

	if (status != B_OK && link->linkId != 0)
		status = graph->DestroyLink(link->linkId);

	if (status == B_OK) {
		for (int32 i = fForeignLinks.CountItems() - 1; i >= 0; i--) {
			CortexForeignLinkEntry* entry = fForeignLinks.ItemAt(i);
			if (entry == NULL)
				continue;
			bool match = (outPin != NULL && entry->outputIsOwn
					&& entry->ownOutput == outPin->output)
				|| (inPin != NULL && !entry->outputIsOwn
					&& entry->ownInput == inPin->input);
			if (match)
				fForeignLinks.RemoveItemAt(i);
		}
	}

	if (status == B_OK && outNode != NULL && inNode != NULL
		&& outPin != NULL && inPin != NULL) {
		BMediaRoute route;
		route.fromNode = outNode->name;
		route.fromPort = outPin->name;
		route.toNode = inNode->name;
		route.toPort = inPin->name;
		BRoutingStore* store = BRoutingStore::Instance();
		if (store != NULL)
			store->RemoveRoute(route);
	}

	_ReportStatus("remove link", status);

	_Rebuild();
}


static CortexPin*
_FindNamedPin(CortexModel* model, const BString& nodeName,
	const BString& pinName, bool output, CortexNode** outNode,
	int32* outNodeIndex, int32* outPinIndex)
{
	for (int32 i = 0; i < model->CountNodes(); i++) {
		CortexNode* node = model->NodeAt(i);
		if (node == NULL || node->name != nodeName)
			continue;
		for (int32 p = 0; p < node->pins.CountItems(); p++) {
			CortexPin* pin = node->pins.ItemAt(p);
			if (pin != NULL && pin->isOutput == output
				&& pin->name == pinName) {
				*outNode = node;
				*outNodeIndex = i;
				*outPinIndex = p;
				return pin;
			}
		}
	}
	return NULL;
}


// Re-applies the saved links whose ends are present and not yet linked.
void
CortexWindow::_RestoreRoutes()
{
	BRoutingStore* store = BRoutingStore::Instance();
	if (fModel == NULL || store == NULL)
		return;

	int32 restored = 0;
	int32 skipped = 0;
	for (int32 i = 0; i < store->CountRoutes(); i++) {
		BMediaRoute route;
		if (store->GetRoute(i, &route) != B_OK)
			continue;

		CortexNode* outNode = NULL;
		CortexNode* inNode = NULL;
		int32 outNodeIndex = -1;
		int32 outPinIndex = -1;
		int32 inNodeIndex = -1;
		int32 inPinIndex = -1;
		CortexPin* outPin = _FindNamedPin(fModel, route.fromNode,
			route.fromPort, true, &outNode, &outNodeIndex, &outPinIndex);
		CortexPin* inPin = _FindNamedPin(fModel, route.toNode, route.toPort,
			false, &inNode, &inNodeIndex, &inPinIndex);
		if (outPin == NULL || inPin == NULL) {
			skipped++;
			continue;
		}

		bool linked = false;
		for (int32 l = 0; l < fModel->CountLinks(); l++) {
			CortexLink* link = fModel->LinkAt(l);
			if (link != NULL && link->outputNode == outNodeIndex
				&& link->outputPin == outPinIndex
				&& link->inputNode == inNodeIndex
				&& link->inputPin == inPinIndex) {
				linked = true;
				break;
			}
		}
		if (linked)
			continue;

		if (_ConnectPair(outNode, outPin, inNode, inPin) == B_OK)
			restored++;
		else
			skipped++;
	}

	_Rebuild();

	BString text;
	text.SetToFormat("Restored %ld routes, %ld could not be applied.",
		(long)restored, (long)skipped);
	BAlert* alert = new(std::nothrow) BAlert("Cortex", text.String(), "OK");
	if (alert != NULL)
		alert->Go(NULL);
}


void
CortexWindow::_DisconnectOwn(int32 nodeIndex, int32 pinIndex)
{
	if (fModel == NULL)
		return;

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return;

	CortexNode* node = fModel->NodeAt(nodeIndex);
	if (node == NULL || node->ownClient == NULL)
		return;

	CortexPin* pin = node->pins.ItemAt(pinIndex);
	if (pin == NULL || !pin->connected)
		return;

	BMediaConnection* binding = pin->output != NULL
		? pin->output->Binding() : pin->input != NULL
			? pin->input->Binding() : NULL;
	if (binding == NULL)
		return;

	if (pin->output != NULL)
		_ReportStatus("disconnect", graph->Disconnect(pin->output,
			dynamic_cast<BMediaInput*>(binding)));
	else
		_ReportStatus("disconnect", graph->Disconnect(
			dynamic_cast<BMediaOutput*>(binding), pin->input));
}


status_t
CortexWindow::_ForeignPortById(uint32 nodeId, uint32 portId,
	BMediaGraph::ForeignPort* outPort)
{
	if (outPort == NULL)
		return B_BAD_VALUE;

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	BObjectList<BMediaGraph::ForeignPort, true> ports;
	if (graph->GetForeignPorts(nodeId, &ports) != B_OK)
		return B_ENTRY_NOT_FOUND;

	for (int32 i = 0; i < ports.CountItems(); i++) {
		BMediaGraph::ForeignPort* port = ports.ItemAt(i);
		if (port != NULL && port->id == portId) {
			*outPort = *port;
			return B_OK;
		}
	}
	return B_ENTRY_NOT_FOUND;
}


void
CortexWindow::_OpenInfo(int32 nodeIndex)
{
	if (fModel == NULL)
		return;
	CortexNode* node = fModel->NodeAt(nodeIndex);
	if (node == NULL)
		return;

	BString text;
	text.SetToFormat(
		"Name: %s\n"
		"Kind: %s\n"
		"Media class: %s\n"
		"Id: %ld\n",
		node->name.String(),
		CortexKindLabel(node->kind),
		node->mediaClass.String(),
		(long)node->id);

	if (node->ownClient != NULL) {
		text << "Inputs: " << node->ownClient->CountInputs() << "\n";
		text << "Outputs: " << node->ownClient->CountOutputs() << "\n";
		for (int32 p = 0; p < node->pins.CountItems(); p++) {
			CortexPin* pin = node->pins.ItemAt(p);
			if (pin == NULL || pin->formatText.Length() == 0)
				continue;
			text << (pin->isOutput ? "Out " : "In  ") << pin->name << ": "
				<< pin->formatText << "\n";
		}
	} else if (node->kind == kCortexNodeDevice) {
		text << "Role: ";
		if (node->defaultRole == 1)
			text << "default audio output\n";
		else if (node->defaultRole == 2)
			text << "default audio input\n";
		else
			text << "none\n";
		if (node->hasVolume)
			text << "Volume: " << node->volume
				<< (node->muted ? " (muted)" : "") << "\n";
	} else if (node->kind == kCortexNodeStream) {
		if (node->hasVolume)
			text << "Volume: " << node->volume
				<< (node->muted ? " (muted)" : "") << "\n";
	}

	text << "\nLatency, driver and clock role are not queryable through\n";
	text << "BMediaGraph today; listed as kit follow-ups in REPORT.md.\n";

	BAlert* alert = new(std::nothrow) BAlert("Node info", text.String(), "OK");
	if (alert != NULL)
		alert->Go();
}


void
CortexWindow::_OpenControlPanel(int32 nodeIndex)
{
	if (fModel == NULL)
		return;
	CortexNode* node = fModel->NodeAt(nodeIndex);
	if (node == NULL || node->ownClient == NULL)
		return;

	BControllable* controllable = node->ownClient->Controllable();
	if (controllable == NULL || controllable->Web() == NULL) {
		_ReportStatus("control panel", B_NOT_SUPPORTED);
		return;
	}

	BView* view = BMediaTheme::ViewFor(controllable->Web());
	if (view == NULL) {
		_ReportStatus("control panel", B_ERROR);
		return;
	}

	BString title;
	title.SetToFormat("%s: Control panel", node->name.String());
	BRect frame(150, 150, 150 + 360, 150 + 280);
	BWindow* panel = new(std::nothrow) BWindow(frame, title.String(),
		B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS);
	if (panel == NULL) {
		delete view;
		return;
	}

	BGroupView* group = new(std::nothrow) BGroupView(B_VERTICAL, 0.0f);
	if (group == NULL) {
		delete view;
		delete panel;
		return;
	}
	group->AddChild(view);
	panel->AddChild(group);
	panel->Show();
}


void
CortexWindow::_OpenVolume(media_client_id id, bool isStream,
	const BString& title, float value)
{
	CortexVolumeWindow* window = new(std::nothrow) CortexVolumeWindow(id,
		isStream, title, value);
	if (window != NULL)
		window->Show();
}


void
CortexWindow::_ReportStatus(const char* action, status_t status)
{
	if (status == B_OK)
		return;

	BString text;
	text.SetToFormat("%s failed: %s", action, strerror(status));
	BAlert* alert = new(std::nothrow) BAlert("Cortex", text.String(), "OK");
	if (alert != NULL)
		alert->Go();
}


bool
CortexWindow::_EndpointFor(CortexNode* node, CortexPin* pin,
	CortexEndpoint* outEndpoint)
{
	if (node == NULL || pin == NULL)
		return false;
	if (pin->output != NULL || pin->input != NULL) {
		outEndpoint->output = pin->output;
		outEndpoint->input = pin->input;
		return true;
	}
	return pin->foreignPortId != 0
		&& _ForeignPortById(pin->foreignNodeId, pin->foreignPortId,
			&outEndpoint->port) == B_OK;
}


status_t
CortexWindow::_Join(const CortexEndpoint& output, const CortexEndpoint& input)
{
	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	if (output.output != NULL && input.input != NULL)
		return graph->Connect(output.output, input.input);
	if (output.output != NULL)
		return graph->Connect(output.output, &input.port);
	if (input.input != NULL)
		return graph->Connect(&output.port, input.input);
	return graph->Connect(&output.port, &input.port);
}


status_t
CortexWindow::_Split(const CortexEndpoint& output, const CortexEndpoint& input)
{
	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	if (output.output != NULL && input.input != NULL)
		return graph->Disconnect(output.output, input.input);
	if (output.output != NULL)
		return graph->Disconnect(output.output, &input.port);
	if (input.input != NULL)
		return graph->Disconnect(&output.port, input.input);
	return graph->Disconnect(&output.port, &input.port);
}


bool
CortexWindow::LinkHasFilter(int32 linkIndex) const
{
	CortexLink* link = fModel != NULL ? fModel->LinkAt(linkIndex) : NULL;
	if (link == NULL)
		return false;
	CortexNode* outNode = fModel->NodeAt(link->outputNode);
	CortexNode* inNode = fModel->NodeAt(link->inputNode);
	return (outNode != NULL && _IsFilterUnit(outNode->ownClient))
		|| (inNode != NULL && _IsFilterUnit(inNode->ownClient));
}


static void
_SetRoute(CortexNode* outNode, CortexPin* outPin, CortexNode* inNode,
	CortexPin* inPin, bool add)
{
	BRoutingStore* store = BRoutingStore::Instance();
	if (store == NULL)
		return;
	BMediaRoute route;
	route.fromNode = outNode->name;
	route.fromPort = outPin->name;
	route.toNode = inNode->name;
	route.toPort = inPin->name;
	if (add)
		store->AddRoute(route);
	else
		store->RemoveRoute(route);
}


// A -> B becomes A -> filter -> B. The unit is an in-process client
// owned by the window, so the model lists it like any own node.
void
CortexWindow::_InsertFilter(int32 linkIndex, const char* id)
{
	CortexLink* link = fModel != NULL ? fModel->LinkAt(linkIndex) : NULL;
	if (link == NULL || link->outputPin < 0 || link->inputPin < 0)
		return;

	const char* kind = link->kind == kCortexPinVideo ? "video"
		: link->kind == kCortexPinAudio ? "audio" : NULL;
	if (kind == NULL)
		return;

	BObjectList<BFilterCatalog::Entry, true> entries;
	BFilterCatalog::ListEntries(&entries, kind);
	BFilterCatalog::Entry* chosen = NULL;
	for (int32 i = 0; i < entries.CountItems(); i++) {
		if (entries.ItemAt(i)->id == id) {
			chosen = entries.ItemAt(i);
			break;
		}
	}
	if (chosen == NULL) {
		_ReportStatus("insert filter", B_ENTRY_NOT_FOUND);
		return;
	}

	CortexNode* outNode = fModel->NodeAt(link->outputNode);
	CortexNode* inNode = fModel->NodeAt(link->inputNode);
	CortexPin* outPin = outNode != NULL
		? outNode->pins.ItemAt(link->outputPin) : NULL;
	CortexPin* inPin = inNode != NULL
		? inNode->pins.ItemAt(link->inputPin) : NULL;
	CortexEndpoint from;
	CortexEndpoint to;
	if (!_EndpointFor(outNode, outPin, &from)
		|| !_EndpointFor(inNode, inPin, &to)) {
		_ReportStatus("insert filter", B_ENTRY_NOT_FOUND);
		return;
	}

	BMediaClient* unit = link->kind == kCortexPinVideo
		? (BMediaClient*)BFilterCatalog::CreateVideoUnit(*chosen)
		: (BMediaClient*)BFilterCatalog::CreateAudioUnit(*chosen);
	if (unit == NULL || unit->InitCheck() != B_OK
		|| unit->InputAt(0) == NULL || unit->OutputAt(0) == NULL) {
		delete unit;
		_ReportStatus("insert filter", B_NO_INIT);
		return;
	}

	CortexEndpoint filterIn;
	CortexEndpoint filterOut;
	filterIn.input = unit->InputAt(0);
	filterOut.output = unit->OutputAt(0);

	status_t status = _Split(from, to);
	if (status == B_OK) {
		status = _Join(from, filterIn);
		if (status == B_OK) {
			status = _Join(filterOut, to);
			if (status != B_OK)
				_Split(from, filterIn);
		}
		if (status != B_OK)
			_Join(from, to);
	}

	if (status != B_OK) {
		delete unit;
		_ReportStatus("insert filter", status);
		_Rebuild();
		return;
	}

	fOwnClients.AddItem(unit);
	_SetRoute(outNode, outPin, inNode, inPin, false);
	_Rebuild();
}


// Reverses _InsertFilter: the filter's upstream and downstream ends are
// joined again and the unit goes away.
void
CortexWindow::_RemoveFilter(int32 linkIndex)
{
	CortexLink* link = fModel != NULL ? fModel->LinkAt(linkIndex) : NULL;
	if (link == NULL)
		return;

	int32 filterIndex = link->outputNode;
	CortexNode* filterNode = fModel->NodeAt(filterIndex);
	if (filterNode == NULL || !_IsFilterUnit(filterNode->ownClient)) {
		filterIndex = link->inputNode;
		filterNode = fModel->NodeAt(filterIndex);
	}
	if (filterNode == NULL || !_IsFilterUnit(filterNode->ownClient))
		return;

	CortexLink* upstream = NULL;
	CortexLink* downstream = NULL;
	for (int32 i = 0; i < fModel->CountLinks(); i++) {
		CortexLink* candidate = fModel->LinkAt(i);
		if (candidate->inputNode == filterIndex && upstream == NULL)
			upstream = candidate;
		if (candidate->outputNode == filterIndex && downstream == NULL)
			downstream = candidate;
	}

	CortexEndpoint from;
	CortexEndpoint filterIn;
	CortexEndpoint filterOut;
	CortexEndpoint to;
	CortexNode* fromNode = NULL;
	CortexNode* toNode = NULL;
	CortexPin* fromPin = NULL;
	CortexPin* toPin = NULL;
	bool haveFrom = false;
	bool haveTo = false;
	if (upstream != NULL && upstream->outputPin >= 0
		&& upstream->inputPin >= 0) {
		fromNode = fModel->NodeAt(upstream->outputNode);
		fromPin = fromNode != NULL
			? fromNode->pins.ItemAt(upstream->outputPin) : NULL;
		haveFrom = _EndpointFor(fromNode, fromPin, &from)
			&& _EndpointFor(filterNode,
				filterNode->pins.ItemAt(upstream->inputPin), &filterIn);
	}
	if (downstream != NULL && downstream->outputPin >= 0
		&& downstream->inputPin >= 0) {
		toNode = fModel->NodeAt(downstream->inputNode);
		toPin = toNode != NULL
			? toNode->pins.ItemAt(downstream->inputPin) : NULL;
		haveTo = _EndpointFor(toNode, toPin, &to)
			&& _EndpointFor(filterNode,
				filterNode->pins.ItemAt(downstream->outputPin), &filterOut);
	}

	BMediaClient* unit = filterNode->ownClient;
	_DropMonitorsOn(unit);

	if (haveFrom)
		_Split(from, filterIn);
	if (haveTo)
		_Split(filterOut, to);

	status_t status = B_OK;
	if (haveFrom && haveTo)
		status = _Join(from, to);

	unit->Stop();
	fOwnClients.RemoveItem(unit);

	if (haveFrom && haveTo)
		_SetRoute(fromNode, fromPin, toNode, toPin, true);
	_ReportStatus("remove filter", status);
	_Rebuild();
}


// Taps a video output without taking its link: a linked output is
// spliced through a splitter, an unlinked one feeds the sink directly.
void
CortexWindow::_OpenMonitor(int32 nodeIndex, int32 pinIndex)
{
	CortexNode* node = fModel != NULL ? fModel->NodeAt(nodeIndex) : NULL;
	CortexPin* pin = node != NULL ? node->pins.ItemAt(pinIndex) : NULL;
	BMediaGraph* graph = BMediaGraph::Instance();
	if (pin == NULL || !pin->isOutput || graph == NULL)
		return;

	CortexMonitorEntry* entry = new(std::nothrow) CortexMonitorEntry();
	if (entry == NULL)
		return;

	BString title;
	title.SetToFormat("%s: %s", node->name.String(), pin->name.String());
	entry->window = new(std::nothrow) CortexMonitorWindow(title.String(),
		BMessenger(this));
	if (entry->window == NULL) {
		delete entry;
		return;
	}
	entry->sink = new(std::nothrow) CortexVideoSink(entry->window->View());

	status_t status = B_NO_MEMORY;
	if (entry->sink != NULL && entry->sink->Input() != NULL) {
		BMediaInput* sinkInput = entry->sink->Input();
		if (pin->output == NULL) {
			entry->isForeign = true;
			status = _EndpointFor(node, pin, &entry->foreignSource)
				? graph->Connect(&entry->foreignSource.port, sinkInput)
				: B_ENTRY_NOT_FOUND;
		} else if (!pin->output->IsConnected()) {
			entry->source = pin->output;
			status = graph->Connect(pin->output, sinkInput);
		} else {
			entry->source = pin->output;
			entry->target = dynamic_cast<BMediaInput*>(
				pin->output->Binding());
			BVideoSplitter* splitter = new(std::nothrow) BVideoSplitter();
			if (entry->target == NULL)
				status = B_NOT_SUPPORTED;
			else if (splitter != NULL) {
				status = B_NO_INIT;
				entry->targetTap = splitter->AddOutput();
				entry->sinkTap = splitter->AddOutput();
				if (entry->targetTap != NULL && entry->sinkTap != NULL
					&& graph->Disconnect(entry->source, entry->target)
						== B_OK) {
					entry->splitter = splitter;
					splitter = NULL;
					status = graph->Connect(entry->source,
						entry->splitter->Input());
					if (status == B_OK) {
						status = graph->Connect(entry->targetTap,
							entry->target);
					}
					if (status == B_OK) {
						status = graph->Connect(entry->sinkTap, sinkInput);
					}
				}
			}
			delete splitter;
		}
	}

	if (status != B_OK) {
		if (entry->splitter != NULL) {
			_TearDownMonitor(entry);
			delete entry->splitter;
		}
		delete entry->sink;
		entry->window->Lock();
		entry->window->Quit();
		delete entry;
		_ReportStatus("monitor", status);
		_Rebuild();
		return;
	}

	fOwnClients.AddItem(entry->sink);
	if (entry->splitter != NULL)
		fOwnClients.AddItem(entry->splitter);
	fMonitors.AddItem(entry);
	entry->window->Show();
	_Rebuild();
}


// Unlinks the tap and puts the spliced link back; does not free anything.
void
CortexWindow::_TearDownMonitor(CortexMonitorEntry* entry)
{
	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL || entry->sink == NULL)
		return;

	BMediaInput* sinkInput = entry->sink->Input();
	if (entry->splitter != NULL) {
		graph->Disconnect(entry->sinkTap, sinkInput);
		graph->Disconnect(entry->targetTap, entry->target);
		graph->Disconnect(entry->source, entry->splitter->Input());
		graph->Connect(entry->source, entry->target);
	} else if (entry->isForeign)
		graph->Disconnect(&entry->foreignSource.port, sinkInput);
	else if (entry->source != NULL)
		graph->Disconnect(entry->source, sinkInput);
}


void
CortexWindow::_CloseMonitor(CortexMonitorEntry* entry)
{
	_TearDownMonitor(entry);

	CortexMonitorWindow* window = entry->window;
	if (entry->splitter != NULL) {
		entry->splitter->Stop();
		fOwnClients.RemoveItem(entry->splitter);
	}
	fOwnClients.RemoveItem(entry->sink);

	if (window->Lock())
		window->Quit();

	fMonitors.RemoveItem(entry);
}


// Monitors whose tap touches a client that is about to go away.
void
CortexWindow::_DropMonitorsOn(BMediaClient* client)
{
	for (int32 i = fMonitors.CountItems() - 1; i >= 0; i--) {
		CortexMonitorEntry* entry = fMonitors.ItemAt(i);
		if ((entry->source != NULL && entry->source->Client() == client)
			|| (entry->target != NULL && entry->target->Client() == client))
			_CloseMonitor(entry);
	}
}


void
CortexWindow::MenusBeginning()
{
	if (fAddMenu != NULL)
		_PopulateAddMenu(fAddMenu, NULL);
}


void
CortexWindow::ShowAddMenu(BPoint viewPoint, BPoint screenPoint)
{
	BPopUpMenu* menu = new(std::nothrow) BPopUpMenu("add menu", false, false);
	if (menu == NULL)
		return;
	_PopulateAddMenu(menu, &viewPoint);
	menu->Go(screenPoint, true, true, true);
	delete menu;
}


// Lists catalog units by group, then add-on units, then "Load plugin...".
// With a point the unit lands there; otherwise the model lays it out.
void
CortexWindow::_PopulateAddMenu(BMenu* menu, const BPoint* at)
{
	while (menu->CountItems() > 0)
		delete menu->RemoveItem((int32)0);

	BObjectList<BFilterCatalog::Entry, true> entries;
	BFilterCatalog::ListEntries(&entries, NULL);
	BObjectList<BString, true> groups(true);
	for (int32 i = 0; i < entries.CountItems(); i++) {
		const BString& group = entries.ItemAt(i)->group;
		bool known = false;
		for (int32 j = 0; j < groups.CountItems(); j++) {
			if (*groups.ItemAt(j) == group) {
				known = true;
				break;
			}
		}
		if (!known)
			groups.AddItem(new BString(group));
	}

	for (int32 g = 0; g < groups.CountItems(); g++) {
		BMenu* groupMenu = new BMenu(groups.ItemAt(g)->String());
		for (int32 i = 0; i < entries.CountItems(); i++) {
			BFilterCatalog::Entry* entry = entries.ItemAt(i);
			if (entry->group != *groups.ItemAt(g))
				continue;
			BString label(entry->name);
			label << " (" << entry->kind << ")";
			BMessage* message = new BMessage(kMsgAddUnit);
			message->AddString("source", "catalog");
			message->AddString("id", entry->id);
			message->AddString("kind", entry->kind);
			if (at != NULL) {
				message->AddFloat("x", at->x);
				message->AddFloat("y", at->y);
			}
			groupMenu->AddItem(new BMenuItem(label.String(), message));
		}
		groupMenu->SetTargetForItems(this);
		menu->AddItem(groupMenu);
	}

	_ScanAddOns();
	bool separated = false;
	for (int32 i = 0; i < fAddOns.CountItems(); i++) {
		CortexAddOn* addOn = fAddOns.ItemAt(i);
		for (int32 u = 0; u < addOn->count; u++) {
			if (!separated) {
				if (menu->CountItems() > 0)
					menu->AddSeparatorItem();
				separated = true;
			}
			BMessage* message = new BMessage(kMsgAddUnit);
			message->AddString("source", "addon");
			message->AddString("path", addOn->path);
			message->AddInt32("index", u);
			if (at != NULL) {
				message->AddFloat("x", at->x);
				message->AddFloat("y", at->y);
			}
			BMenuItem* item = new BMenuItem(addOn->names.ItemAt(u)->String(),
				message);
			item->SetTarget(this);
			menu->AddItem(item);
		}
	}

	if (menu->CountItems() > 0)
		menu->AddSeparatorItem();
	BMenuItem* loadItem = new BMenuItem("Load plugin" B_UTF8_ELLIPSIS,
		new BMessage(kMsgLoadPlugin));
	loadItem->SetTarget(this);
	menu->AddItem(loadItem);
}


// Loads every media add-on not seen yet. Images stay in fAddOns; failed
// ones are remembered with image -1 so they are not retried per click.
void
CortexWindow::_ScanAddOns()
{
	static const directory_which kDirectories[]
		= { B_SYSTEM_ADDONS_DIRECTORY, B_USER_NONPACKAGED_ADDONS_DIRECTORY };

	for (size_t d = 0; d < sizeof(kDirectories) / sizeof(kDirectories[0]);
			d++) {
		BPath dirPath;
		if (find_directory(kDirectories[d], &dirPath) != B_OK
			|| dirPath.Append("media") != B_OK) {
			continue;
		}

		BDirectory directory(dirPath.Path());
		if (directory.InitCheck() != B_OK)
			continue;

		BEntry entry;
		while (directory.GetNextEntry(&entry, true) == B_OK) {
			if (!entry.IsFile())
				continue;
			BPath path;
			if (entry.GetPath(&path) != B_OK)
				continue;

			bool known = false;
			for (int32 i = 0; i < fAddOns.CountItems(); i++) {
				if (fAddOns.ItemAt(i)->path == path.Path()) {
					known = true;
					break;
				}
			}
			if (known)
				continue;

			CortexAddOn* addOn = new(std::nothrow) CortexAddOn();
			if (addOn == NULL)
				return;
			addOn->path = path.Path();
			fAddOns.AddItem(addOn);

			image_id image = load_add_on(path.Path());
			if (image < 0)
				continue;

			int32 (*countFn)() = NULL;
			const char* (*nameFn)(int32) = NULL;
			get_image_symbol(image, MEDIA_PLUGIN_LOAD_FILE_SYMBOL,
				B_SYMBOL_TYPE_TEXT, (void**)&addOn->loadFile);
			if (get_image_symbol(image, MEDIA_UNIT_COUNT_SYMBOL,
					B_SYMBOL_TYPE_TEXT, (void**)&countFn) == B_OK
				&& get_image_symbol(image, MEDIA_UNIT_NAME_SYMBOL,
					B_SYMBOL_TYPE_TEXT, (void**)&nameFn) == B_OK
				&& get_image_symbol(image, MEDIA_UNIT_INSTANTIATE_SYMBOL,
					B_SYMBOL_TYPE_TEXT, (void**)&addOn->instantiate) == B_OK) {
				int32 count = countFn();
				for (int32 u = 0; u < count; u++) {
					const char* name = nameFn(u);
					addOn->names.AddItem(new BString(
						name != NULL ? name : path.Leaf()));
				}
				addOn->count = addOn->names.CountItems();
			}

			if (addOn->count == 0 && addOn->loadFile == NULL) {
				unload_add_on(image);
				continue;
			}
			addOn->image = image;
		}
	}
}


void
CortexWindow::_AddUnit(BMessage* message)
{
	BPoint point;
	bool hasPoint = message->FindFloat("x", &point.x) == B_OK
		&& message->FindFloat("y", &point.y) == B_OK;

	const char* source = NULL;
	message->FindString("source", &source);
	if (source == NULL)
		return;

	if (strcmp(source, "catalog") == 0) {
		const char* id = NULL;
		const char* kind = NULL;
		if (message->FindString("id", &id) != B_OK
			|| message->FindString("kind", &kind) != B_OK) {
			return;
		}

		BObjectList<BFilterCatalog::Entry, true> entries;
		BFilterCatalog::ListEntries(&entries, kind);
		BFilterCatalog::Entry* chosen = NULL;
		for (int32 i = 0; i < entries.CountItems(); i++) {
			if (entries.ItemAt(i)->id == id) {
				chosen = entries.ItemAt(i);
				break;
			}
		}
		if (chosen == NULL) {
			_ReportStatus("add unit", B_ENTRY_NOT_FOUND);
			return;
		}

		BMediaClient* unit = strcmp(kind, "video") == 0
			? (BMediaClient*)BFilterCatalog::CreateVideoUnit(*chosen)
			: (BMediaClient*)BFilterCatalog::CreateAudioUnit(*chosen);
		_AdoptUnit(unit, "add unit", hasPoint ? &point : NULL);
		return;
	}

	const char* path = NULL;
	int32 index = 0;
	message->FindString("path", &path);
	message->FindInt32("index", &index);
	for (int32 i = 0; path != NULL && i < fAddOns.CountItems(); i++) {
		CortexAddOn* addOn = fAddOns.ItemAt(i);
		if (addOn->path != path)
			continue;
		if (addOn->instantiate == NULL || index < 0 || index >= addOn->count)
			break;
		_AdoptUnit(addOn->instantiate(index), "add unit",
			hasPoint ? &point : NULL);
		return;
	}
	_ReportStatus("add unit", B_ENTRY_NOT_FOUND);
}


void
CortexWindow::_LoadPluginFile(const char* path)
{
	_ScanAddOns();
	for (int32 i = 0; i < fAddOns.CountItems(); i++) {
		CortexAddOn* addOn = fAddOns.ItemAt(i);
		if (addOn->loadFile == NULL)
			continue;
		BMediaClient* unit = addOn->loadFile(path);
		_AdoptUnit(unit, "load plugin", NULL);
		return;
	}
	_ReportStatus("load plugin", B_NOT_SUPPORTED);
}


// Takes ownership of unit (may be NULL) and shows it as an own node.
void
CortexWindow::_AdoptUnit(BMediaClient* unit, const char* action,
	const BPoint* at)
{
	if (unit == NULL || unit->InitCheck() != B_OK) {
		delete unit;
		_ReportStatus(action, B_NO_INIT);
		return;
	}

	// A failed start still leaves wireable pins; the link reports its own
	// errors.
	unit->Start();
	if (!fOwnClients.AddItem(unit)) {
		delete unit;
		_ReportStatus(action, B_NO_MEMORY);
		return;
	}

	if (at != NULL && fModel != NULL)
		fModel->RememberOrigin(unit->Id(), kCortexNodeOwn, *at);
	_Rebuild();
}
