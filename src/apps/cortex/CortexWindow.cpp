/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "CortexWindow.h"

#include <Alert.h>
#include <GroupView.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <PopUpMenu.h>
#include <Slider.h>
#include <String.h>
#include <View.h>
#include <Window.h>

#include <stdio.h>
#include <string.h>

#include <media2/Controllable.h>
#include <media2/MediaTheme.h>
#include <media2/ParameterWeb.h>

#include "CortexGraphView.h"
#include "CortexNodes.h"


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
	fMeteredIds(true)
{
	BRect bounds = Bounds();

	BMenu* fileMenu = new BMenu("File");
	fileMenu->AddItem(new BMenuItem("Quit",
		new BMessage(B_QUIT_REQUESTED), 'Q'));

	BMenuBar* menuBar = new BMenuBar("menu bar");
	menuBar->AddItem(fileMenu);
	fileMenu->SetTargetForItems(this);

	BRect graphFrame = bounds;
	graphFrame.top += menuBar->Frame().Height() + 1.0f;

	fModel = new(std::nothrow) CortexModel();
	fView = new(std::nothrow) CortexGraphView(graphFrame);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0)
		.Add(menuBar)
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

	for (int32 i = 0; i < fOwnClients.CountItems(); i++) {
		BMediaClient* client = fOwnClients.ItemAt(i);
		if (client == NULL)
			continue;
		client->Stop();
		delete client;
	}
	fOwnClients.MakeEmpty();

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

		case 'PWDC':
		case 'PWSC':
		case 'PWDF':
		case 'PWVC':
		case 'PWPC':
		case 'PWPF':
		case 'PWLC':
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

	_Rebuild();
}


void
CortexWindow::_Rebuild()
{
	if (fModel == NULL || fView == NULL)
		return;

	media_client_id defaultOut = 0;
	media_client_id defaultIn = 0;
	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph != NULL) {
		graph->GetDefaultAudioOutput(&defaultOut);
		graph->GetDefaultAudioInput(&defaultIn);
	}

	fModel->Rebuild(&fOwnClients, &fForeignLinks);

	if (graph != NULL) {
		for (int32 i = 0; i < fMeteredIds.CountItems(); i++)
			graph->StopMeteringDevice((media_client_id)*fMeteredIds.ItemAt(i));
		fMeteredIds.MakeEmpty();

		media_client_id meters[2] = { defaultOut, defaultIn };
		for (int32 i = 0; i < 2; i++) {
			if (meters[i] == 0)
				continue;
			if (graph->StartMeteringDevice((media_client_id)meters[i],
					BMessenger(this)) == B_OK) {
				uint32* stored = new(std::nothrow) uint32(meters[i]);
				if (stored != NULL)
					fMeteredIds.AddItem(stored);
				int32 index = fModel->FindNode(kCortexNodeDevice, meters[i]);
				CortexNode* node = index >= 0
					? fModel->NodeAt(index) : NULL;
				if (node != NULL)
					node->metered = true;
			}
		}
	}

	fView->SetModel(fModel);
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

	if (outPinNode->ownClient == NULL && inPinNode->ownClient == NULL) {
		BAlert* alert = new(std::nothrow) BAlert("Connect",
			"Foreign to foreign links belong to WirePlumber. Cortex is a "
			"view and a trigger, not a policy engine.", "OK");
		if (alert != NULL)
			alert->Go();
		return;
	}

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

	if (status != B_OK)
		_ReportStatus("connect", status);

	_Rebuild();
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
