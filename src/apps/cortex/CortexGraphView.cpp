/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "CortexGraphView.h"

#include <InterfaceDefs.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <PopUpMenu.h>
#include <Rect.h>
#include <Window.h>

#include <stdio.h>
#include <string.h>

#include <media2/FilterCatalog.h>

#include "CortexWindow.h"


static const float kNodeWidth = 220.0f;
static const float kTitleHeight = 28.0f;
static const float kPinRow = 16.0f;
static const float kPinSize = 8.0f;
static const float kColumnWidth = 240.0f;
static const float kMargin = 24.0f;


static rgb_color
_PinColor(cortex_pin_kind kind)
{
	rgb_color color = { 128, 128, 128, 255 };
	switch (kind) {
		case kCortexPinAudio:
			color = (rgb_color){ 70, 160, 70, 255 };
			break;
		case kCortexPinVideo:
			color = (rgb_color){ 80, 120, 220, 255 };
			break;
		case kCortexPinMidi:
			color = (rgb_color){ 200, 140, 40, 255 };
			break;
		case kCortexPinData:
			color = (rgb_color){ 170, 80, 170, 255 };
			break;
		default:
			break;
	}
	return color;
}


static rgb_color
_LinkColor(cortex_link_state state)
{
	rgb_color color = { 90, 170, 90, 255 };
	switch (state) {
		case kCortexLinkNegotiating:
			color = (rgb_color){ 210, 190, 60, 255 };
			break;
		case kCortexLinkError:
			color = (rgb_color){ 210, 60, 60, 255 };
			break;
		case kCortexLinkPartial:
			color = (rgb_color){ 220, 140, 40, 255 };
			break;
		case kCortexLinkRouting:
			color = (rgb_color){ 100, 140, 200, 255 };
			break;
		default:
			break;
	}
	return color;
}


static rgb_color
_NodeTitleColor(cortex_node_kind kind)
{
	rgb_color color = { 220, 220, 220, 255 };
	switch (kind) {
		case kCortexNodeOwn:
			color = (rgb_color){ 205, 235, 205, 255 };
			break;
		case kCortexNodeDevice:
			color = (rgb_color){ 205, 220, 240, 255 };
			break;
		case kCortexNodeStream:
			color = (rgb_color){ 240, 230, 200, 255 };
			break;
		default:
			break;
	}
	return color;
}


const char*
CortexKindLabel(cortex_node_kind kind)
{
	switch (kind) {
		case kCortexNodeOwn:
			return "Unit";
		case kCortexNodeDevice:
			return "Device";
		case kCortexNodeStream:
			return "Stream";
		default:
			return "Foreign";
	}
}


static cortex_pin_kind
_PinKindFromTypes(media_type_mask types)
{
	if ((types & B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO)) != 0)
		return kCortexPinAudio;
	if ((types & B_MEDIA_TYPE_BIT(B_MEDIA_RAW_VIDEO)) != 0)
		return kCortexPinVideo;
	if ((types & B_MEDIA_TYPE_BIT(B_MEDIA_MIDI)) != 0)
		return kCortexPinMidi;
	if ((types & B_MEDIA_TYPE_BIT(B_MEDIA_PARAMETERS)) != 0)
		return kCortexPinData;
	return kCortexPinUnknown;
}


static cortex_pin_kind
_PinKindFromMediaClass(const BString& mediaClass)
{
	if (mediaClass.FindFirst("Video") >= 0)
		return kCortexPinVideo;
	if (mediaClass.FindFirst("Midi") >= 0 || mediaClass.FindFirst("MIDI") >= 0)
		return kCortexPinMidi;
	if (mediaClass.FindFirst("Dsp") >= 0)
		return kCortexPinData;
	return kCortexPinAudio;
}


static BString
_FormatText(const BMediaFormat& format)
{
	BString text;
	if (format.Type() == B_MEDIA_RAW_AUDIO) {
		text.SetToFormat("%gHz %uch",
			(double)format.format.u.raw_audio.frame_rate,
			format.format.u.raw_audio.channel_count);
	} else if (format.Type() == B_MEDIA_RAW_VIDEO) {
		text.SetToFormat("%lux%lu",
			(unsigned long)format.format.u.raw_video.width,
			(unsigned long)format.format.u.raw_video.height);
	} else if (format.Type() == B_MEDIA_NO_TYPE)
		text.SetTo("none");
	else
		text.SetTo("typed");
	return text;
}


BRect
CortexNode::Frame() const
{
	float rows = pins.CountItems();
	if (rows < 1)
		rows = 1;
	float height = kTitleHeight + rows * kPinRow + 10.0f;
	return BRect(origin.x, origin.y, origin.x + kNodeWidth - 1.0f,
		origin.y + height - 1.0f);
}


void
CortexNode::LayoutPins()
{
	int32 outRow = 0;
	int32 inRow = 0;
	for (int32 p = 0; p < pins.CountItems(); p++) {
		CortexPin* pin = pins.ItemAt(p);
		if (pin == NULL)
			continue;
		float y = origin.y + kTitleHeight + 8.0f
			+ (pin->isOutput ? outRow++ : inRow++) * kPinRow;
		float x = pin->isOutput ? origin.x + kNodeWidth - 1.0f : origin.x;
		pin->position = BPoint(x, y);
	}
}


int32
CortexNode::CountPins(bool outputs) const
{
	int32 count = 0;
	for (int32 i = 0; i < pins.CountItems(); i++) {
		if (pins.ItemAt(i)->isOutput == outputs)
			count++;
	}
	return count;
}


CortexModel::CortexModel()
	:
	fNodes(true),
	fLinks(true),
	fOriginKeys(true),
	fOriginValues(true)
{
}


CortexModel::~CortexModel()
{
}


status_t
CortexModel::Rebuild(BObjectList<BMediaClient, true>* ownClients,
	const void* foreignLinks)
{
	fNodes.MakeEmpty();
	fLinks.MakeEmpty();

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	if (ownClients != NULL) {
		for (int32 i = 0; i < ownClients->CountItems(); i++) {
			BMediaClient* client = ownClients->ItemAt(i);
			if (client == NULL)
				continue;

			CortexNode* node = new(std::nothrow) CortexNode();
			if (node == NULL)
				continue;

			node->kind = kCortexNodeOwn;
			node->id = client->Id();
			node->pwNodeId = 0;
			node->name.SetTo(client->Name());
			node->mediaClass.SetTo("in-process client");
			node->ownClient = client;
			node->hasVolume = false;
			node->volume = 0.0f;
			node->muted = false;
			node->defaultRole = 0;
			node->hasProfiles = false;
			node->activeProfile = -1;
			node->hasPorts = false;
			node->activePortId = -1;
			node->peak = 0.0f;
			node->metered = false;
			node->isSink = false;
			node->isSource = false;
			node->description.SetTo("");

			for (int32 p = 0; p < client->CountOutputs(); p++) {
				BMediaOutput* output = client->OutputAt(p);
				if (output == NULL)
					continue;
				CortexPin* pin = new(std::nothrow) CortexPin();
				if (pin == NULL)
					continue;
				pin->name.SetTo(output->Name() != NULL
					? output->Name() : "out");
				pin->isOutput = true;
				pin->kind = _PinKindFromTypes(output->AcceptedTypes());
				pin->output = output;
				pin->input = NULL;
				pin->foreignPortId = 0;
				pin->foreignNodeId = 0;
				uint32 portBuf[64];
				uint32 portCount = graph->GetConnectionPortIds(output,
					portBuf, 64);
				for (uint32 pi = 0; pi < portCount; pi++)
					pin->portIds.push_back(portBuf[pi]);
				if (node->pwNodeId == 0)
					node->pwNodeId = graph->GetConnectionNodeId(output);
				pin->connected = output->IsConnected();
				if (pin->connected)
					pin->formatText = _FormatText(output->Format());
				node->pins.AddItem(pin);
			}

			for (int32 p = 0; p < client->CountInputs(); p++) {
				BMediaInput* input = client->InputAt(p);
				if (input == NULL)
					continue;
				CortexPin* pin = new(std::nothrow) CortexPin();
				if (pin == NULL)
					continue;
				pin->name.SetTo(input->Name() != NULL
					? input->Name() : "in");
				pin->isOutput = false;
				pin->kind = _PinKindFromTypes(input->AcceptedTypes());
				pin->input = input;
				pin->output = NULL;
				pin->foreignPortId = 0;
				pin->foreignNodeId = 0;
				uint32 portBuf[64];
				uint32 portCount = graph->GetConnectionPortIds(input,
					portBuf, 64);
				for (uint32 pi = 0; pi < portCount; pi++)
					pin->portIds.push_back(portBuf[pi]);
				if (node->pwNodeId == 0)
					node->pwNodeId = graph->GetConnectionNodeId(input);
				pin->connected = input->IsConnected();
				if (pin->connected)
					pin->formatText = _FormatText(input->Format());
				node->pins.AddItem(pin);
			}

			BPoint origin;
			if (LookupOrigin(node->id, node->kind, &origin))
				node->origin = origin;
			fNodes.AddItem(node);
		}
	}

	media_client_id defaultOut = 0;
	media_client_id defaultIn = 0;
	graph->GetDefaultAudioOutput(&defaultOut);
	graph->GetDefaultAudioInput(&defaultIn);

	BObjectList<BMediaGraph::ForeignNode, true> foreignNodes;
	graph->GetForeignNodes(&foreignNodes);

	BObjectList<BMediaGraph::StreamInfo, true> streams;
	graph->GetStreams(&streams);

	for (int32 i = 0; i < foreignNodes.CountItems(); i++) {
		BMediaGraph::ForeignNode* foreign = foreignNodes.ItemAt(i);
		if (foreign == NULL)
			continue;

		CortexNode* node = new(std::nothrow) CortexNode();
		if (node == NULL)
			continue;

		node->id = (media_client_id)foreign->id;
		node->pwNodeId = (uint32)foreign->id;
		node->name.SetTo(foreign->name.Length() > 0
			? foreign->name.String() : foreign->description.String());
		node->description.SetTo(foreign->description);
		node->mediaClass.SetTo(foreign->mediaClass);
		node->ownClient = NULL;
		node->isSink = false;
		node->isSource = false;
		node->hasVolume = false;
		node->volume = 0.0f;
		node->muted = false;
		node->defaultRole = 0;
		node->hasProfiles = false;
		node->activeProfile = -1;
		node->hasPorts = false;
		node->activePortId = -1;
		node->peak = 0.0f;
		node->metered = false;

		BMessage info;
		bool isDevice = graph->GetClientInfo(node->id, &info) == B_OK;
		if (isDevice) {
			node->kind = kCortexNodeDevice;
			info.FindBool("is.sink", &node->isSink);
			info.FindBool("is.source", &node->isSource);
			BString name;
			if (info.FindString("name", &name) == B_OK
				&& name.Length() > 0)
				node->name.SetTo(name);

			float master = 0.0f;
			if (graph->GetDeviceVolume(node->id, &master,
					&node->muted) == B_OK) {
				node->hasVolume = true;
				node->volume = master;
			}

			if (node->id == defaultOut)
				node->defaultRole = 1;
			else if (node->id == defaultIn)
				node->defaultRole = 2;

			BObjectList<BMediaGraph::DeviceProfileInfo, true> profiles;
			if (graph->GetDeviceProfiles(node->id, &profiles,
					&node->activeProfile) == B_OK)
				node->hasProfiles = true;

			BObjectList<BMediaGraph::DevicePortInfo, true> ports;
			if (graph->GetDevicePorts(node->id, &ports) == B_OK)
				node->hasPorts = true;
		} else {
			node->kind = kCortexNodeStream;
			for (int32 s = 0; s < streams.CountItems(); s++) {
				BMediaGraph::StreamInfo* stream = streams.ItemAt(s);
				if ((uint32)stream->id == (uint32)node->id) {
					node->hasVolume = true;
					node->volume = stream->volume;
					node->muted = stream->mute;
					break;
				}
			}
			if (foreign->mediaClass.FindFirst("Video") >= 0)
				node->kind = kCortexNodeForeign;
		}

		BObjectList<BMediaGraph::ForeignPort, true> ports;
		graph->GetForeignPorts((uint32)node->id, &ports);
		for (int32 p = 0; p < ports.CountItems(); p++) {
			BMediaGraph::ForeignPort* port = ports.ItemAt(p);
			if (port == NULL)
				continue;
			CortexPin* pin = new(std::nothrow) CortexPin();
			if (pin == NULL)
				continue;
			pin->name.SetTo(port->name.Length() > 0
				? port->name.String() : port->direction.String());
			pin->isOutput = port->direction == "out";
			pin->kind = port->format.Length() > 0
				? _PinKindFromMediaClass(port->format)
				: _PinKindFromMediaClass(node->mediaClass);
			pin->input = NULL;
			pin->output = NULL;
			pin->foreignPortId = port->id;
			pin->foreignNodeId = port->nodeId;
			pin->portIds.push_back(port->id);
			pin->formatText.SetTo(port->format);
			pin->connected = false;
			node->pins.AddItem(pin);
		}

		if (node->pins.CountItems() == 0) {
			cortex_pin_kind kind = _PinKindFromMediaClass(node->mediaClass);
			const bool outputs = !node->isSink;
			CortexPin* pin = new(std::nothrow) CortexPin();
			if (pin != NULL) {
				pin->name.SetTo(outputs ? "out" : "in");
				pin->isOutput = outputs;
				pin->kind = kind;
				pin->input = NULL;
				pin->output = NULL;
				pin->foreignPortId = 0;
				pin->foreignNodeId = (uint32)node->id;
				pin->connected = false;
				node->pins.AddItem(pin);
			}
		}

		BPoint origin;
		if (LookupOrigin(node->id, node->kind, &origin))
			node->origin = origin;
		fNodes.AddItem(node);
	}

	// Every real link the server sees, whoever created it, drawn pin to
	// pin through the port globals both sides reported.
	BObjectList<BMediaGraph::GraphLink, true> graphLinks;
	if (graph->GetLinks(&graphLinks) == B_OK) {
		for (int32 i = 0; i < graphLinks.CountItems(); i++) {
			BMediaGraph::GraphLink* link = graphLinks.ItemAt(i);
			if (link == NULL)
				continue;
			int32 outIndex = _FindNodeByPwId(link->outputNodeId);
			int32 inIndex = _FindNodeByPwId(link->inputNodeId);
			if (outIndex < 0 || inIndex < 0)
				continue;
			CortexNode* outNode = fNodes.ItemAt(outIndex);
			CortexNode* inNode = fNodes.ItemAt(inIndex);
			int32 outPin = _FindPinForPort(outNode, true,
				link->outputPortId);
			int32 inPin = _FindPinForPort(inNode, false,
				link->inputPortId);
			cortex_pin_kind kind = kCortexPinAudio;
			CortexPin* pin = outPin >= 0
				? outNode->pins.ItemAt(outPin) : NULL;
			if (pin != NULL)
				kind = pin->kind;
			_AddLink(outIndex, outPin, inIndex, inPin,
				kCortexLinkActive, kind, link->id);
		}
	}

	// In-process links never reach the registry; the kit lists them with
	// the connection objects, which the own pins carry.
	BObjectList<BMediaGraph::Link, true> processLinks;
	if (graph->GetLinks(&processLinks) == B_OK) {
		for (int32 i = 0; i < processLinks.CountItems(); i++) {
			BMediaGraph::Link* link = processLinks.ItemAt(i);
			if (link == NULL || !link->inProcess)
				continue;
			int32 outIndex = -1;
			int32 outPin = -1;
			int32 inIndex = -1;
			int32 inPin = -1;
			for (int32 n = 0; n < fNodes.CountItems(); n++) {
				CortexNode* node = fNodes.ItemAt(n);
				for (int32 p = 0; node != NULL
						&& p < node->pins.CountItems(); p++) {
					CortexPin* pin = node->pins.ItemAt(p);
					if (pin->output != NULL && pin->output == link->output) {
						outIndex = n;
						outPin = p;
					}
					if (pin->input != NULL && pin->input == link->input) {
						inIndex = n;
						inPin = p;
					}
				}
			}
			if (outIndex < 0 || inIndex < 0)
				continue;
			_AddLink(outIndex, outPin, inIndex, inPin, kCortexLinkActive,
				fNodes.ItemAt(outIndex)->pins.ItemAt(outPin)->kind);
		}
	}

	_Layout();
	return B_OK;
}


void
CortexModel::_Layout()
{
	int32 columns[kCortexNodeForeign + 1];
	for (int32 i = 0; i <= kCortexNodeForeign; i++)
		columns[i] = 0;

	for (int32 i = 0; i < fNodes.CountItems(); i++) {
		CortexNode* node = fNodes.ItemAt(i);
		if (node == NULL)
			continue;
		int32 column = node->kind;
		if (column < 0 || column > kCortexNodeForeign)
			column = kCortexNodeForeign;
		float x = kMargin + column * kColumnWidth;
		float y = kMargin;
		for (int32 c = 0; c < columns[column]; c++) {
			CortexNode* earlier = NULL;
			int32 seen = 0;
			for (int32 j = 0; j < i; j++) {
				CortexNode* candidate = fNodes.ItemAt(j);
				if (candidate == NULL || (int32)candidate->kind != column)
					continue;
				if (seen == c) {
					earlier = candidate;
					break;
				}
				seen++;
			}
			if (earlier != NULL)
				y = earlier->Frame().bottom + 20.0f;
		}
		if (!LookupOrigin(node->id, node->kind, NULL))
			node->origin = BPoint(x, y);
		columns[column]++;
	}

	for (int32 i = 0; i < fNodes.CountItems(); i++) {
		CortexNode* node = fNodes.ItemAt(i);
		if (node != NULL)
			node->LayoutPins();
	}
}


void
CortexModel::_AddLink(int32 outNode, int32 outPin, int32 inNode, int32 inPin,
	cortex_link_state state, cortex_pin_kind kind, uint32 linkId,
	const char* label)
{
	CortexLink* link = new(std::nothrow) CortexLink();
	if (link == NULL)
		return;
	link->outputNode = outNode;
	link->outputPin = outPin;
	link->inputNode = inNode;
	link->inputPin = inPin;
	link->linkId = linkId;
	link->state = state;
	link->kind = kind;
	if (label != NULL)
		link->label.SetTo(label);
	fLinks.AddItem(link);
}


int32
CortexModel::_FindNodeByPwId(uint32 pwNodeId) const
{
	if (pwNodeId == 0)
		return -1;
	for (int32 i = 0; i < fNodes.CountItems(); i++) {
		CortexNode* node = fNodes.ItemAt(i);
		if (node != NULL && node->pwNodeId == pwNodeId)
			return i;
	}
	return -1;
}


int32
CortexModel::_FindPinForPort(CortexNode* node, bool output,
	uint32 portId) const
{
	if (node == NULL)
		return -1;
	int32 firstMatch = -1;
	for (int32 p = 0; p < node->pins.CountItems(); p++) {
		CortexPin* pin = node->pins.ItemAt(p);
		if (pin == NULL || pin->isOutput != output)
			continue;
		if (firstMatch < 0)
			firstMatch = p;
		if (portId != 0) {
			for (size_t i = 0; i < pin->portIds.size(); i++) {
				if (pin->portIds[i] == portId)
					return p;
			}
		}
	}
	// A node-wide link lands on the first pin of that direction.
	return portId == 0 ? firstMatch : -1;
}


int32
CortexModel::CountNodes() const
{
	return fNodes.CountItems();
}


CortexNode*
CortexModel::NodeAt(int32 index) const
{
	return fNodes.ItemAt(index);
}


int32
CortexModel::CountLinks() const
{
	return fLinks.CountItems();
}


CortexLink*
CortexModel::LinkAt(int32 index) const
{
	return fLinks.ItemAt(index);
}


int32
CortexModel::FindNode(cortex_node_kind kind, media_client_id id) const
{
	for (int32 i = 0; i < fNodes.CountItems(); i++) {
		CortexNode* node = fNodes.ItemAt(i);
		if (node != NULL && node->kind == kind && node->id == id)
			return i;
	}
	return -1;
}


void
CortexModel::RememberOrigin(media_client_id id, cortex_node_kind kind,
	const BPoint& origin)
{
	BString key;
	key.SetToFormat("%d:%ld", (int)kind, (long)id);
	for (int32 i = 0; i < fOriginKeys.CountItems(); i++) {
		if (*fOriginKeys.ItemAt(i) == key) {
			*fOriginValues.ItemAt(i) = origin;
			return;
		}
	}
	BString* storedKey = new(std::nothrow) BString(key);
	BPoint* storedValue = new(std::nothrow) BPoint(origin);
	if (storedKey == NULL || storedValue == NULL) {
		delete storedKey;
		delete storedValue;
		return;
	}
	fOriginKeys.AddItem(storedKey);
	fOriginValues.AddItem(storedValue);
}


bool
CortexModel::LookupOrigin(media_client_id id, cortex_node_kind kind,
	BPoint* outOrigin) const
{
	BString key;
	key.SetToFormat("%d:%ld", (int)kind, (long)id);
	for (int32 i = 0; i < fOriginKeys.CountItems(); i++) {
		if (*fOriginKeys.ItemAt(i) == key) {
			if (outOrigin != NULL)
				*outOrigin = *fOriginValues.ItemAt(i);
			return true;
		}
	}
	return false;
}


status_t
CortexModel::SaveOrigins(BMessage* into) const
{
	if (into == NULL)
		return B_BAD_VALUE;

	into->MakeEmpty();
	for (int32 i = 0; i < fOriginKeys.CountItems(); i++) {
		int32 kind = 0;
		long id = 0;
		if (sscanf(fOriginKeys.ItemAt(i)->String(), "%d:%ld", &kind, &id)
				!= 2)
			continue;
		into->AddInt32("kind", kind);
		into->AddInt32("id", (int32)id);
		into->AddFloat("x", fOriginValues.ItemAt(i)->x);
		into->AddFloat("y", fOriginValues.ItemAt(i)->y);
	}
	return B_OK;
}


status_t
CortexModel::LoadOrigins(const BMessage* from)
{
	if (from == NULL)
		return B_BAD_VALUE;

	fOriginKeys.MakeEmpty();
	fOriginValues.MakeEmpty();

	int32 kind = 0;
	int32 index = 0;
	while (from->FindInt32("kind", index, &kind) == B_OK) {
		int32 id = 0;
		float x = 0.0f;
		float y = 0.0f;
		if (from->FindInt32("id", index, &id) != B_OK
			|| from->FindFloat("x", index, &x) != B_OK
			|| from->FindFloat("y", index, &y) != B_OK) {
			index++;
			continue;
		}
		RememberOrigin((media_client_id)id, (cortex_node_kind)kind,
			BPoint(x, y));
		index++;
	}
	return B_OK;
}


CortexGraphView::CortexGraphView(BRect frame)
	:
	BView(frame, "cortex graph", B_FOLLOW_ALL,
		B_WILL_DRAW | B_PULSE_NEEDED),
	fModel(NULL),
	fDragNode(-1),
	fDragOffset(0, 0),
	fDragging(false),
	fLinkFromNode(-1),
	fLinkFromPin(-1),
	fTrackingLink(false),
	fTrackPoint(0, 0),
	fSelectedLink(-1)
{
}


void
CortexGraphView::SetModel(CortexModel* model)
{
	fModel = model;
	fSelectedLink = -1;
	Invalidate();
}


void
CortexGraphView::AttachedToWindow()
{
	BView::AttachedToWindow();
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
}


int32
CortexGraphView::_NodeAt(BPoint where, BPoint* outOffset) const
{
	if (fModel == NULL)
		return -1;
	for (int32 i = fModel->CountNodes() - 1; i >= 0; i--) {
		CortexNode* node = fModel->NodeAt(i);
		if (node == NULL)
			continue;
		if (node->Frame().Contains(where)) {
			if (outOffset != NULL)
				*outOffset = where - node->origin;
			return i;
		}
	}
	return -1;
}


int32
CortexGraphView::_PinAt(BPoint where, int32* outNode, int32* outPin) const
{
	if (fModel == NULL)
		return -1;
	for (int32 i = 0; i < fModel->CountNodes(); i++) {
		CortexNode* node = fModel->NodeAt(i);
		if (node == NULL)
			continue;
		for (int32 p = 0; p < node->pins.CountItems(); p++) {
			CortexPin* pin = node->pins.ItemAt(p);
			if (pin == NULL)
				continue;
			float dx = pin->position.x - where.x;
			float dy = pin->position.y - where.y;
			if (dx * dx + dy * dy <= kPinSize * kPinSize) {
				if (outNode != NULL)
					*outNode = i;
				if (outPin != NULL)
					*outPin = p;
				return i;
			}
		}
	}
	return -1;
}


bool
CortexGraphView::_LinkPoints(int32 linkIndex, BPoint* outFrom,
	BPoint* outTo) const
{
	if (fModel == NULL)
		return false;
	CortexLink* link = fModel->LinkAt(linkIndex);
	if (link == NULL)
		return false;
	CortexNode* outNode = fModel->NodeAt(link->outputNode);
	CortexNode* inNode = fModel->NodeAt(link->inputNode);
	if (outNode == NULL || inNode == NULL)
		return false;

	BPoint from;
	BPoint to;
	if (link->outputPin >= 0) {
		CortexPin* pin = outNode->pins.ItemAt(link->outputPin);
		from = pin != NULL ? pin->position : outNode->Frame().RightTop();
	} else
		from = BPoint(outNode->Frame().right, outNode->Frame().top + 16.0f);
	if (link->inputPin >= 0) {
		CortexPin* pin = inNode->pins.ItemAt(link->inputPin);
		to = pin != NULL ? pin->position : inNode->Frame().LeftTop();
	} else
		to = BPoint(inNode->Frame().left, inNode->Frame().top + 16.0f);

	if (outFrom != NULL)
		*outFrom = from;
	if (outTo != NULL)
		*outTo = to;
	return true;
}


int32
CortexGraphView::_LinkAt(BPoint where) const
{
	if (fModel == NULL)
		return -1;
	const float kHitDistance = 7.0f;
	for (int32 i = fModel->CountLinks() - 1; i >= 0; i--) {
		BPoint from;
		BPoint to;
		if (!_LinkPoints(i, &from, &to))
			continue;
		BPoint control1(from.x + (to.x - from.x) * 0.5f, from.y);
		BPoint control2(from.x + (to.x - from.x) * 0.5f, to.y);
		// Sample the curve; hit testing the control polygon would
		// swallow clicks near the nodes.
		for (int32 step = 1; step <= 24; step++) {
			float t = (float)step / 24.0f;
			float u = 1.0f - t;
			BPoint point(u * u * u * from.x + 3 * u * u * t * control1.x
				+ 3 * u * t * t * control2.x + t * t * t * to.x,
				u * u * u * from.y + 3 * u * u * t * control1.y
				+ 3 * u * t * t * control2.y + t * t * t * to.y);
			BPoint delta = point - where;
			if (delta.x * delta.x + delta.y * delta.y
					<= kHitDistance * kHitDistance)
				return i;
		}
	}
	return -1;
}


void
CortexGraphView::Draw(BRect updateRect)
{
	if (fModel == NULL)
		return;

	// Saved origins can be restored after the layout pass.
	for (int32 i = 0; i < fModel->CountNodes(); i++) {
		CortexNode* node = fModel->NodeAt(i);
		if (node != NULL)
			node->LayoutPins();
	}

	for (int32 i = 0; i < fModel->CountLinks(); i++) {
		CortexLink* link = fModel->LinkAt(i);
		if (link == NULL)
			continue;

		BPoint from;
		BPoint to;
		if (!_LinkPoints(i, &from, &to))
			continue;

		rgb_color color = _LinkColor(link->state);
		SetHighColor(color);
		SetPenSize(i == fSelectedLink ? 3.0f
			: (link->state == kCortexLinkRouting ? 1.0f : 2.0f));
		BPoint control1(from.x + (to.x - from.x) * 0.5f, from.y);
		BPoint control2(from.x + (to.x - from.x) * 0.5f, to.y);
		BPoint bezier[4] = { from, control1, control2, to };
		StrokeBezier(bezier);
	}
	SetPenSize(1.0f);

	for (int32 i = 0; i < fModel->CountNodes(); i++) {
		CortexNode* node = fModel->NodeAt(i);
		if (node == NULL)
			continue;

		BRect frame = node->Frame();
		if (!frame.Intersects(updateRect))
			continue;

		SetHighColor(255, 255, 255);
		SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
		FillRect(frame, B_SOLID_LOW);
		SetHighColor(0, 0, 0);
		StrokeRect(frame);

		BRect title = BRect(frame.left + 1.0f, frame.top + 1.0f,
			frame.right - 1.0f, frame.top + kTitleHeight - 2.0f);
		SetHighColor(_NodeTitleColor(node->kind));
		FillRect(title);
		SetHighColor(0, 0, 0);
		StrokeRect(title);

		float textWidth = frame.Width() - 12.0f;
		BString label;
		label.SetToFormat("%s  [%s]", node->name.String(),
			CortexKindLabel(node->kind));
		TruncateString(&label, B_TRUNCATE_MIDDLE, textWidth);
		DrawString(label.String(), BPoint(frame.left + 6.0f, frame.top + 12.0f));

		BString sub(node->mediaClass);
		if (node->defaultRole != 0) {
			if (sub.Length() > 0)
				sub << "  ";
			sub << (node->defaultRole == 1 ? "default out" : "default in");
		}
		if (sub.Length() > 0) {
			TruncateString(&sub, B_TRUNCATE_END, textWidth);
			DrawString(sub.String(), BPoint(frame.left + 6.0f, frame.top + 24.0f));
		}

		if (node->hasVolume) {
			BRect bar(frame.left + 4.0f, frame.bottom - 8.0f,
				frame.right - 4.0f, frame.bottom - 4.0f);
			SetHighColor(230, 230, 230);
			FillRect(bar);
			BRect fill = bar;
			fill.right = fill.left + (bar.Width() - 1.0f)
				* (node->muted ? 0.0f : node->volume);
			SetHighColor(node->muted ? (rgb_color){ 180, 80, 80, 255 }
				: (rgb_color){ 70, 140, 70, 255 });
			FillRect(fill);
		}

		if (node->metered) {
			BRect meter(frame.left + 4.0f, frame.bottom - 14.0f,
				frame.right - 4.0f, frame.bottom - 10.0f);
			SetHighColor(40, 40, 40);
			FillRect(meter);
			float level = node->peak;
			if (level > 1.0f)
				level = 1.0f;
			if (level < 0.0f)
				level = 0.0f;
			BRect levelRect = meter;
			levelRect.right = levelRect.left + (meter.Width() - 1.0f) * level;
			SetHighColor(90, 200, 90);
			FillRect(levelRect);
		}

		for (int32 p = 0; p < node->pins.CountItems(); p++) {
			CortexPin* pin = node->pins.ItemAt(p);
			if (pin == NULL)
				continue;
			BRect pinRect(pin->position.x - kPinSize * 0.5f,
				pin->position.y - kPinSize * 0.5f,
				pin->position.x + kPinSize * 0.5f,
				pin->position.y + kPinSize * 0.5f);
			SetHighColor(_PinColor(pin->kind));
			FillRect(pinRect);
			SetHighColor(pin->connected ? (rgb_color){ 0, 0, 0, 255 }
				: (rgb_color){ 150, 150, 150, 255 });
			StrokeRect(pinRect);

			BString label(pin->name);
			if (pin->connected && pin->formatText.Length() > 0)
				label << "  " << pin->formatText;
			float labelWidth = frame.Width() * 0.5f - 14.0f;
			TruncateString(&label, B_TRUNCATE_END, labelWidth);
			float textX = pin->isOutput
				? pin->position.x - 6.0f - StringWidth(label.String())
				: pin->position.x + 6.0f;
			DrawString(label.String(),
				BPoint(textX, pin->position.y + 4.0f));
		}
	}

	if (fTrackingLink && fModel != NULL) {
		CortexNode* node = fModel->NodeAt(fLinkFromNode);
		CortexPin* pin = node != NULL
			? node->pins.ItemAt(fLinkFromPin) : NULL;
		if (pin != NULL) {
			SetHighColor(80, 80, 80);
			StrokeLine(pin->position, fTrackPoint);
		}
	}
}


void
CortexGraphView::MouseDown(BPoint where)
{
	if (fModel == NULL)
		return;

	MakeFocus();

	BMessage* message = Window()->CurrentMessage();
	int32 buttons = 0;
	if (message != NULL)
		message->FindInt32("buttons", &buttons);

	int32 pinNode = -1;
	int32 pinIndex = -1;
	if (_PinAt(where, &pinNode, &pinIndex) >= 0) {
		if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
			_ShowNodeMenu(pinNode, where);
			return;
		}
		fLinkFromNode = pinNode;
		fLinkFromPin = pinIndex;
		fTrackingLink = true;
		fTrackPoint = where;
		SetMouseEventMask(B_PRIMARY_MOUSE_BUTTON | B_SECONDARY_MOUSE_BUTTON);
		return;
	}

	int32 linkIndex = _LinkAt(where);
	if (linkIndex >= 0) {
		if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
			_ShowLinkMenu(linkIndex, where);
			return;
		}
		fSelectedLink = linkIndex;
		Invalidate();
		return;
	}

	fSelectedLink = -1;

	BPoint offset;
	int32 nodeIndex = _NodeAt(where, &offset);
	if (nodeIndex < 0) {
		Invalidate();
		return;
	}

	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		_ShowNodeMenu(nodeIndex, where);
		return;
	}

	fDragNode = nodeIndex;
	fDragOffset = offset;
	fDragging = true;
	SetMouseEventMask(B_PRIMARY_MOUSE_BUTTON);
}


void
CortexGraphView::KeyDown(const char* bytes, int32 numBytes)
{
	if (numBytes < 1 || fSelectedLink < 0 || fModel == NULL) {
		BView::KeyDown(bytes, numBytes);
		return;
	}

	// Delete or backspace cuts the selected link.
	if (bytes[0] == B_DELETE || bytes[0] == B_BACKSPACE) {
		CortexWindow* window = dynamic_cast<CortexWindow*>(Window());
		if (window != NULL)
			window->RemoveLink(fSelectedLink);
		fSelectedLink = -1;
		Invalidate();
		return;
	}

	BView::KeyDown(bytes, numBytes);
}


void
CortexGraphView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* /*message*/)
{
	if (fDragging && fModel != NULL) {
		CortexNode* node = fModel->NodeAt(fDragNode);
		if (node != NULL) {
			node->origin = where - fDragOffset;
			node->LayoutPins();
			Invalidate();
		}
		return;
	}

	if (fTrackingLink) {
		fTrackPoint = where;
		Invalidate();
	}
}


void
CortexGraphView::MouseUp(BPoint where)
{
	if (fDragging && fModel != NULL) {
		CortexNode* node = fModel->NodeAt(fDragNode);
		if (node != NULL) {
			fModel->RememberOrigin(node->id, node->kind, node->origin);
			Invalidate();
		}
	}

	if (fTrackingLink && fModel != NULL) {
		int32 targetNode = -1;
		int32 targetPin = -1;
		_PinAt(where, &targetNode, &targetPin);
		if (targetNode >= 0 && targetPin >= 0
			&& (targetNode != fLinkFromNode || targetPin != fLinkFromPin)) {
			CortexWindow* window = dynamic_cast<CortexWindow*>(Window());
			if (window != NULL)
				window->TryConnectPins(fLinkFromNode, fLinkFromPin,
					targetNode, targetPin);
		}
		Invalidate();
	}

	fDragging = false;
	fDragNode = -1;
	fTrackingLink = false;
	fLinkFromNode = -1;
	fLinkFromPin = -1;
}


void
CortexGraphView::_ShowLinkMenu(int32 linkIndex, BPoint where)
{
	CortexWindow* window = dynamic_cast<CortexWindow*>(Window());
	if (window == NULL)
		return;

	BPopUpMenu* menu = new BPopUpMenu("link menu");
	BMessage* removeMessage = new BMessage(kMsgRemoveLink);
	removeMessage->AddInt32("link", linkIndex);
	BMenuItem* removeItem = new BMenuItem("Remove link", removeMessage);
	removeItem->SetTarget(window);
	menu->AddItem(removeItem);

	const char* filterKind = NULL;
	CortexLink* link = fModel != NULL ? fModel->LinkAt(linkIndex) : NULL;
	if (link != NULL && link->kind == kCortexPinVideo)
		filterKind = "video";
	else if (link != NULL && link->kind == kCortexPinAudio)
		filterKind = "audio";

	if (filterKind != NULL) {
		menu->AddSeparatorItem();

		BObjectList<BFilterCatalog::Entry, true> entries;
		BFilterCatalog::ListEntries(&entries, filterKind);
		BMenu* insertMenu = new BMenu("Insert filter");
		for (int32 i = 0; i < entries.CountItems(); i++) {
			BFilterCatalog::Entry* entry = entries.ItemAt(i);
			BMessage* insertMessage = new BMessage(kMsgInsertFilter);
			insertMessage->AddInt32("link", linkIndex);
			insertMessage->AddString("id", entry->id);
			BMenuItem* item = new BMenuItem(entry->name.String(),
				insertMessage);
			item->SetTarget(window);
			insertMenu->AddItem(item);
		}
		if (insertMenu->CountItems() == 0)
			insertMenu->SetEnabled(false);
		menu->AddItem(insertMenu);

		BMessage* filterMessage = new BMessage(kMsgRemoveFilter);
		filterMessage->AddInt32("link", linkIndex);
		BMenuItem* filterItem = new BMenuItem("Remove filter", filterMessage);
		filterItem->SetTarget(window);
		filterItem->SetEnabled(window->LinkHasFilter(linkIndex));
		menu->AddItem(filterItem);
	}

	if (link != NULL && link->kind == kCortexPinVideo) {
		BMessage* monitorMessage = new BMessage(kMsgMonitorPin);
		monitorMessage->AddInt32("node", link->outputNode);
		monitorMessage->AddInt32("pin", link->outputPin);
		BMenuItem* monitorItem = new BMenuItem("Monitor video",
			monitorMessage);
		monitorItem->SetTarget(window);
		menu->AddItem(monitorItem);
	}

	menu->Go(ConvertToScreen(where));
	delete menu;
}


void
CortexGraphView::_ShowNodeMenu(int32 nodeIndex, BPoint where)
{
	CortexWindow* window = dynamic_cast<CortexWindow*>(Window());
	if (window != NULL)
		window->ShowNodeMenu(nodeIndex, ConvertToScreen(where));
}


void
CortexGraphView::_TrackLink(BPoint where)
{
	fTrackPoint = where;
}
