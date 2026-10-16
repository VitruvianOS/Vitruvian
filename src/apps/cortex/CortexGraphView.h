/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _CORTEX_GRAPH_VIEW_H
#define _CORTEX_GRAPH_VIEW_H


#include <ObjectList.h>
#include <Point.h>
#include <Rect.h>
#include <String.h>
#include <View.h>

#include <vector>

#include <media2/MediaClient.h>
#include <media2/MediaConnection.h>
#include <media2/MediaGraph.h>


enum cortex_node_kind {
	kCortexNodeOwn,
	kCortexNodeDevice,
	kCortexNodeStream,
	kCortexNodeForeign
};


enum cortex_pin_kind {
	kCortexPinAudio,
	kCortexPinVideo,
	kCortexPinMidi,
	kCortexPinData,
	kCortexPinUnknown
};


enum cortex_link_state {
	kCortexLinkActive,
	kCortexLinkNegotiating,
	kCortexLinkError,
	kCortexLinkPartial,
	kCortexLinkRouting
};


class CortexPin {
public:
	BString			name;
	bool			isOutput;
	cortex_pin_kind		kind;
	BMediaInput*		input;
	BMediaOutput*		output;
	uint32			foreignPortId;
	uint32			foreignNodeId;
	// Registry global ids of the PipeWire ports this pin stands for.
	std::vector<uint32>	portIds;
	BString			formatText;
	bool			connected;
	BPoint			position;
};


class CortexNode {
public:
	cortex_node_kind	kind;
	media_client_id		id;
	// PipeWire node id: same as id for registry nodes, resolved from
	// the connections for in-process clients.
	uint32			pwNodeId;
	BString			name;
	BString			description;
	BString			mediaClass;
	bool			isSink;
	bool			isSource;
	bool			hasVolume;
	float			volume;
	bool			muted;
	int32			defaultRole;
	bool			hasProfiles;
	int32			activeProfile;
	bool			hasPorts;
	int32			activePortId;
	BObjectList<CortexPin, true>	pins;
	BMediaClient*		ownClient;
	BPoint			origin;
	float			peak;
	bool			metered;

	BRect			Frame() const;
	void			LayoutPins();
	int32			CountPins(bool outputs) const;
};


class CortexLink {
public:
	int32			outputNode;
	int32			outputPin;
	int32			inputNode;
	int32			inputPin;
	// Registry id of the PipeWire link, for removal; 0 if none.
	uint32			linkId;
	cortex_link_state	state;
	cortex_pin_kind		kind;
	BString			label;
};


const char* CortexKindLabel(cortex_node_kind kind);


class CortexModel {
public:
				CortexModel();
				~CortexModel();

	status_t		Rebuild(BObjectList<BMediaClient, true>* ownClients,
					const void* foreignLinks);

	int32			CountNodes() const;
	CortexNode*		NodeAt(int32 index) const;
	int32			CountLinks() const;
	CortexLink*		LinkAt(int32 index) const;

	int32			FindNode(cortex_node_kind kind, media_client_id id) const;
	void			RememberOrigin(media_client_id id, cortex_node_kind kind,
					const BPoint& origin);
	bool			LookupOrigin(media_client_id id, cortex_node_kind kind,
					BPoint* outOrigin) const;

private:
	void			_Layout();
	void			_AddLink(int32 outNode, int32 outPin, int32 inNode,
					int32 inPin, cortex_link_state state,
					cortex_pin_kind kind, uint32 linkId = 0,
					const char* label = NULL);
	int32			_FindNodeByPwId(uint32 pwNodeId) const;
	int32			_FindPinForPort(CortexNode* node, bool output,
					uint32 portId) const;

	BObjectList<CortexNode, true>	fNodes;
	BObjectList<CortexLink, true>	fLinks;
	BObjectList<BString, true>	fOriginKeys;
	BObjectList<BPoint, true>	fOriginValues;
};


class CortexGraphView : public BView {
public:
				CortexGraphView(BRect frame);

	void			SetModel(CortexModel* model);

	virtual	void		AttachedToWindow();
	virtual	void		Draw(BRect updateRect);
	virtual	void		MouseDown(BPoint where);
	virtual	void		MouseMoved(BPoint where, uint32 transit,
					const BMessage* message);
	virtual	void		MouseUp(BPoint where);

private:
	int32			_NodeAt(BPoint where, BPoint* outOffset) const;
	int32			_PinAt(BPoint where, int32* outNode,
					int32* outPin) const;
	void			_ShowNodeMenu(int32 nodeIndex, BPoint where);
	void			_TrackLink(BPoint where);

	CortexModel*		fModel;
	int32			fDragNode;
	BPoint			fDragOffset;
	bool			fDragging;
	int32			fLinkFromNode;
	int32			fLinkFromPin;
	bool			fTrackingLink;
	BPoint			fTrackPoint;
};


#endif
