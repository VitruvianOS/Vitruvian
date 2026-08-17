/*
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_GRAPH_H
#define _MEDIA2_MEDIA_GRAPH_H


#include <media2/MediaClientDefs.h>

#include <Messenger.h>
#include <String.h>


class BMediaConnection;
class BMediaInput;
class BMediaOutput;
class BMessage;

template<class T, bool O> class BObjectList;


class BMediaGraph {
public:
	static	BMediaGraph*			Instance();

			status_t				Connect(BMediaOutput* output,
										BMediaInput* input);
			status_t				Disconnect(BMediaOutput* output,
										BMediaInput* input);


	enum {
		B_MEDIA_CLIENT_ADDED      = 1 << 0,
		B_MEDIA_CLIENT_REMOVED    = 1 << 1,
		B_MEDIA_DEFAULTS_CHANGED  = 1 << 2,
		B_MEDIA_VOLUME_CHANGED    = 1 << 3,
		B_MEDIA_STREAMS_CHANGED   = 1 << 4,
		B_MEDIA_LINKS_CHANGED     = 1 << 5,
		B_MEDIA_ALL_NOTIFICATIONS = 0x7fffffff
	};
			status_t				StartWatching(const BMessenger& messenger,
										int32 notificationMask
											= B_MEDIA_ALL_NOTIFICATIONS);
			status_t				StopWatching(const BMessenger& messenger);

			status_t				GetClients(BObjectList<media_client_id, true>* clients);
			status_t				GetClientInfo(media_client_id id, BMessage* info);

			status_t				GetDefaultAudioOutput(media_client_id* id);
			status_t				GetDefaultAudioInput(media_client_id* id);
			status_t				SetDefaultAudioOutput(media_client_id id);
			status_t				SetDefaultAudioInput(media_client_id id);


	struct StreamInfo {
		media_client_id	id;
		BString			name;
		BString			mediaClass;
		media_client_id	deviceId;
		bool			isOutput;
		float			volume;
		bool			mute;
	};
			status_t				GetStreams(
										BObjectList<StreamInfo, true>* outStreams);
			status_t				MoveStream(media_client_id streamId,
										media_client_id deviceId);
			status_t				SetStreamVolume(media_client_id streamId,
										float volume, bool mute);


			status_t				GetDeviceVolume(media_client_id id,
										float* outMaster, bool* outMute);
			status_t				SetDeviceVolume(media_client_id id,
										float master);
			status_t				SetDeviceChannelVolumes(media_client_id id,
										const float* volumes, size_t count);
			status_t				SetDeviceMute(media_client_id id, bool mute);
			status_t				SetDefaultDeviceVolume(float master);

	struct DevicePortInfo {
		media_client_id	id;
		BString			name;
		BString			description;
		int32			portId;
		bool			active;
	};
	struct DeviceProfileInfo {
		media_client_id	id;
		BString			name;
		BString			description;
		int32			index;
		bool			available;
	};
			status_t				GetDevicePorts(media_client_id id,
										BObjectList<DevicePortInfo, true>* outPorts);
			status_t				SetDevicePort(media_client_id id,
										int32 portId);
			status_t				GetDeviceProfiles(media_client_id id,
										BObjectList<DeviceProfileInfo, true>* outProfiles,
										int32* outActiveIndex);
			status_t				SetDeviceProfile(media_client_id id,
										int32 profileIndex);


			status_t				StartMeteringDevice(media_client_id id,
										const BMessenger& target,
										bigtime_t interval = 33333);
			status_t				StopMeteringDevice(media_client_id id);


	struct ForeignNode {
		uint32			id;
		BString			name;
		BString			description;
		BString			mediaClass;
		uint32			portCount;
		uint32			outputPorts;
		uint32			inputPorts;
	};
	struct ForeignPort {
		uint32			id;
		uint32			nodeId;
		BString			name;
		BString			direction;
		BString			format;
	};
			status_t				GetForeignNodes(
										BObjectList<ForeignNode, true>* outNodes);
			status_t				GetForeignPorts(uint32 nodeId,
										BObjectList<ForeignPort, true>* outPorts);


			status_t				Connect(BMediaOutput* output,
										const ForeignPort* foreignPort);

			status_t				Connect(const ForeignPort* foreignPort,
										BMediaInput* input);


			status_t				Disconnect(BMediaOutput* output,
										const ForeignPort* foreignPort);
			status_t				Disconnect(const ForeignPort* foreignPort,
										BMediaInput* input);

private:
									BMediaGraph();
									~BMediaGraph();
									BMediaGraph(const BMediaGraph&) = delete;
			BMediaGraph&			operator=(const BMediaGraph&) = delete;
};


inline BMediaGraph*	MediaGraph() { return BMediaGraph::Instance(); }


#endif
