/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _CORTEX_WINDOW_H
#define _CORTEX_WINDOW_H


#include <ObjectList.h>
#include <String.h>
#include <Window.h>

#include <media2/MediaClient.h>
#include <media2/MediaGraph.h>


class BSlider;
class CortexGraphView;
class CortexModel;
class CortexMonitorWindow;
class CortexNode;
class CortexPin;
struct CortexEndpoint;
struct CortexMonitorEntry;


enum {
	kMsgGraphReady			= 'CRGR',
	kMsgRebuild			= 'CGRB',
	kMsgPeakUpdate			= 'PWPk',
	kMsgVolumeSlider		= 'CVSL',
	kMsgDeviceVolume		= 'CDVL',
	kMsgStreamVolume		= 'CSVL',
	kMsgToggleMute			= 'CTMG',
	kMsgSetDefaultOut		= 'CSDO',
	kMsgSetDefaultIn		= 'CSDI',
	kMsgSetProfile			= 'CSPF',
	kMsgSetPort			= 'CSPR',
	kMsgMoveStream			= 'CMVS',
	kMsgControlPanel		= 'CCPN',
	kMsgInfo			= 'CINF',
	kMsgSendTestNote		= 'CNOT',
	kMsgDisconnectOwn		= 'CDOI',
	kMsgRemoveLink			= 'CRLK',
	kMsgRestoreRoutes		= 'CRRT',
	kMsgInsertFilter		= 'CIFL',
	kMsgRemoveFilter		= 'CRFL',
	kMsgMonitorPin			= 'CMPN',
	kMsgMonitorClosed		= 'CMCL'
};


struct CortexForeignLinkEntry {
	bool		outputIsOwn;
	BMediaOutput*	ownOutput;
	BMediaInput*	ownInput;
	uint32		foreignNodeId;
	uint32		foreignPortId;
};


class CortexVolumeWindow : public BWindow {
public:
				CortexVolumeWindow(media_client_id id, bool isStream,
					const BString& title, float value);

	virtual	void		MessageReceived(BMessage* message);

private:
			media_client_id	fId;
			bool		fIsStream;
			BSlider*	fSlider;
};


class CortexWindow : public BWindow {
public:
				CortexWindow();
	virtual			~CortexWindow();

	virtual	void		MessageReceived(BMessage* message);
	virtual	bool		QuitRequested();

			void		ShowNodeMenu(int32 nodeIndex, BPoint screenPoint);
			void		TryConnectPins(int32 outNode, int32 outPin,
					int32 inNode, int32 inPin);
			void		RemoveLink(int32 linkIndex);
			bool		LinkHasFilter(int32 linkIndex) const;

private:
			void		_GraphReady();
			void		_Rebuild();
			void		_LoadSettings();
			void		_SaveSettings();
			void		_OpenInfo(int32 nodeIndex);
			void		_OpenControlPanel(int32 nodeIndex);
			void		_OpenVolume(media_client_id id, bool isStream,
					const BString& title, float value);
			void		_ReportStatus(const char* action, status_t status);
			status_t	_ForeignPortById(uint32 nodeId, uint32 portId,
					BMediaGraph::ForeignPort* outPort);
			void		_DisconnectOwn(int32 nodeIndex, int32 pinIndex);
			status_t	_ConnectPair(CortexNode* outNode, CortexPin* outPin,
					CortexNode* inNode, CortexPin* inPin);
			void		_RestoreRoutes();
			bool		_EndpointFor(CortexNode* node, CortexPin* pin,
					CortexEndpoint* outEndpoint);
			status_t	_Join(const CortexEndpoint& output,
					const CortexEndpoint& input);
			status_t	_Split(const CortexEndpoint& output,
					const CortexEndpoint& input);
			void		_InsertFilter(int32 linkIndex, const char* id);
			void		_RemoveFilter(int32 linkIndex);
			void		_OpenMonitor(int32 nodeIndex, int32 pinIndex);
			void		_CloseMonitor(CortexMonitorEntry* entry);
			void		_DropMonitorsOn(BMediaClient* client);
			void		_TearDownMonitor(CortexMonitorEntry* entry);

			CortexGraphView*	fView;
			CortexModel*		fModel;
			BObjectList<BMediaClient, true>	fOwnClients;
			BObjectList<CortexForeignLinkEntry, true>	fForeignLinks;
			bool			fGraphReady;
			bool			fRebuildPending;
			BObjectList<uint32, true>	fMeteredIds;
			BObjectList<CortexMonitorEntry, true>	fMonitors;
};


#endif
