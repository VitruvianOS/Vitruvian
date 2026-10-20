/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _VITRUVIAN_MEDIA2_PIPEWIRE_BACKEND_H
#define _VITRUVIAN_MEDIA2_PIPEWIRE_BACKEND_H

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <pipewire/pipewire.h>
#include <pipewire/stream.h>

#include <Messenger.h>
#include <SupportDefs.h>

#include <media2/MediaFormat.h>


class BControllable;


namespace BPrivate { namespace media {


class PipeWireBackend {
public:
	static	PipeWireBackend*		GetInstance();

			// Host performance clock for HandleStart/Stop/Seek. Unit RT
			// callbacks still read the PipeWire graph clock via
			// spa_io_position; this is the app-thread "now".
			static	bigtime_t		PerformanceTime();

			pw_loop*				GetMainLoop() const { return fLoop; }
			pw_core*				GetCore()     const { return fCore; }
			pw_registry*			GetRegistry() const { return fRegistry; }

			void					Lock();
			void					Unlock();

			pw_filter*				CreateFilter(const char* name,
										pw_properties* props);

			uint32					VideoNodeIdByName(const char* nodeName);

			pw_stream*				CreateAndConnectStream(
										const char* name,
										pw_direction direction,
										const BMediaFormat& format,
										const pw_stream_events* events,
										void* userdata,
										const char* targetNodeName = NULL,
										bool captureSinkMonitor = false,
										bool autoConnect = true,
										bool allowMjpeg = false);

			void					DestroyStream(pw_stream* stream);

		struct DeviceInfo {
			uint32			id;
			uint32			cardId;
			std::string		name;
			std::string		nodeName;
			std::string		mediaClass;
			bool			isSink;
			bool			isSource;
			// Camera devices (media.class Video/Source).
			bool			isVideo;
			std::string		stableId;
			std::string		location;
		};

		std::vector<DeviceInfo>	Devices();

			uint32					GetDefaultSinkId();
			uint32					GetDefaultSourceId();
			uint32					GetDefaultVideoSourceId();

			status_t				SetDefaultSink(uint32 deviceId);
			status_t				SetDefaultSource(uint32 deviceId);
			status_t				SetDefaultVideoSource(uint32 deviceId);

		// Camera controls: SPA_PARAM_Props on the Video/Source node.
		// The v4l2 SPA plugin maps those props to V4L2 controls.
		struct NodeControlInfo {
			uint32			propId;
			int32			value;
			int32			min;
			int32			max;
			int32			step;
			uint32			menuMask;	// menu controls: bit n = item n
			bool			isBool;
			bool			present;
		};

		status_t				QueryNodeControls(const char* nodeName,
										std::vector<NodeControlInfo>& out);
		status_t				SetNodeControls(const char* nodeName,
										const std::vector<NodeControlInfo>& props);

		// Capture sizes a Video/Source node offers, with the best frame
		// rate for each (30 when offered, else the highest).
		struct NodeVideoMode {
			uint32			width;
			uint32			height;
			float			frameRate;
		};

		status_t				QueryNodeVideoModes(const char* nodeName,
										std::vector<NodeVideoMode>& out);

		struct DevicePort {
			std::string		name;
			std::string		description;
			int32			id;
			bool			active;
		};
		struct DeviceProfile {
			std::string		name;
			std::string		description;
			int32			index;
			bool			available;
		};

		status_t				GetDevicePorts(uint32 deviceId,
									std::vector<DevicePort>& outPorts,
									int32* outActivePortId = NULL);
		status_t				SetDevicePort(uint32 deviceId, int32 portId);
		status_t				GetDeviceProfiles(uint32 deviceId,
									std::vector<DeviceProfile>& outProfiles,
									int32* outActiveIndex = NULL);
		status_t				SetDeviceProfile(uint32 deviceId, int32 index);

		status_t				GetDeviceVolume(uint32 deviceId,
									std::vector<float>& outVolumes,
									bool* outMute = NULL);
		status_t				SetDeviceVolume(uint32 deviceId,
									const float* volumes, size_t count);
		status_t				SetDeviceMute(uint32 deviceId, bool mute);

		enum TestChannel {
			kTestChannelFrontLeft	= 0,
			kTestChannelFrontRight	= 1,
			kTestChannelCenter		= 2,
			kTestChannelLFE			= 3,
			kTestChannelRearLeft		= 4,
			kTestChannelRearRight	= 5,
			kTestChannelSideLeft		= 6,
			kTestChannelSideRight	= 7,
			kTestChannelAll			= 8
		};
		status_t				TestSpeaker(uint32 deviceId, TestChannel channel,
									uint32 durationMs = 500);
		status_t				StopTestSpeaker(uint32 deviceId);

		struct StreamInfo {
			uint32			id;
			std::string		name;
			std::string		mediaClass;
			uint32			deviceId;
			bool			isOutput;
			float			volume;
			bool			mute;
		};
		std::vector<StreamInfo>	Streams();

		status_t				MoveStream(uint32 streamId, uint32 deviceId);
		status_t				GetStreamVolume(uint32 streamId,
									float* outVolume, bool* outMute);
		status_t				SetStreamVolume(uint32 streamId,
									float volume, bool mute);

		static const char* const kPeakMeterNodeName;

		enum {
			kMsgMidiNodesChanged    = 'PWMC'
		};

		// MIDI-class registry nodes (media.class "Midi/..."). Ports for these
		// nodes already live in fPorts; only the node list was missing.
		struct MidiNodeInfo {
			uint32			id;
			std::string		name;
			std::string		description;
			std::string		mediaClass;
		};
		std::vector<MidiNodeInfo>	MidiNodes();

		void					AddWatcher(const BMessenger& target,
									int32 notificationMask);
		void					RemoveWatcher(const BMessenger& target);

		void					_Broadcast(uint32 what, uint32 deviceId = 0);

		enum NotificationType {
			kNotificationDeviceAdded		= 'mdad',
			kNotificationDeviceRemoved	= 'mdre',
			kNotificationDeviceConnected	= 'mdcn',
			kNotificationDeviceDisconnected= 'mddc',
			kNotificationVolumeChanged	= 'mdvc',
			kNotificationDefaultChanged	= 'mdfc'
		};
		void					ShowNotification(NotificationType type,
									const char* title, const char* message,
									uint32 deviceId = 0);

		struct ForeignNodeInfo {
			uint32			id;
			std::string		name;
			std::string		description;
			std::string		mediaClass;
			uint32			portCount;
			uint32			outputPorts;
			uint32			inputPorts;
		};
		struct ForeignPortInfo {
			uint32			id;
			uint32			nodeId;
			std::string		name;
			std::string		direction;
			std::string		format;
		};
		std::vector<ForeignNodeInfo>	ForeignNodes();
		std::vector<ForeignPortInfo>	ForeignPorts(uint32 nodeId);

		// Every link the registry sees, whoever created it. Port ids are
		// registry global ids, 0 when the link is node-wide.
		struct RegistryLinkInfo {
			uint32			id;
			uint32			outputNodeId;
			uint32			outputPortId;
			uint32			inputNodeId;
			uint32			inputPortId;
		};
		std::vector<RegistryLinkInfo>	Links();
		status_t				DestroyLink(uint32 linkId);

		status_t				ResolveNodePorts(uint32 nodeId,
									pw_direction direction,
									std::vector<uint32>& outPortIds,
									bigtime_t timeoutUsecs = 2000000);

			status_t				RoundtripLocked(
									bigtime_t timeoutUsecs = 2000000);

		// SPA node params: PropInfo descriptors and Props values.
		// valueTypes use spa pod types (SPA_TYPE_Float, Bool, Int, Id, String).
		struct NodePropInfo {
			uint32			id;
			std::string		name;
			uint32			valueType;
			float			minValue;
			float			maxValue;
			float			step;
			bool			hasRange;
			std::vector<std::pair<uint32, std::string> > labels;
		};
		struct NodePropValue {
			uint32			id;
			uint32			valueType;
			std::vector<uint8_t> bytes;
		};

		status_t				QueryNodePropInfo(uint32 nodeId,
									std::vector<NodePropInfo>& outInfos,
									bigtime_t timeoutUsecs = 2000000);
		status_t				QueryNodeProps(uint32 nodeId,
									std::vector<NodePropValue>& outValues,
									bigtime_t timeoutUsecs = 2000000);
		status_t				SetNodeProp(uint32 nodeId, uint32 propId,
									uint32 valueType, const void* value,
									size_t size);
		status_t				WatchNodeParams(uint32 nodeId,
									BControllable* target);
		status_t				UnwatchNodeParams(uint32 nodeId,
									BControllable* target);

		// Publish PropInfo/Props on a filter we created so foreign clients
		// (and our own BControllable bind) can see and set them.
		status_t				PublishFilterParams(struct pw_filter* filter,
									const std::vector<NodePropInfo>& infos,
									const std::vector<NodePropValue>& values);

private:
			void					_OnRegistryGlobal(uint32 id, uint32 perms,
										const char* type, uint32 version,
										const struct spa_dict* props);
			void					_OnRegistryGlobalRemove(uint32 id);
			static void				_GlobalThunk(void* data, uint32 id,
										uint32 perms, const char* type,
										uint32 version, const struct spa_dict*);
			static void				_GlobalRemoveThunk(void* data, uint32 id);

			static void				_LinkEventThunk(void* data, uint32 id,
										uint32 permissions, const char* type,
										uint32 version, const struct spa_dict* props);
			static void				_LinkRemoveThunk(void* data, uint32 id);
			void					_OnLinkEvent(uint32 id, uint32 permissions,
										const char* type, uint32 version,
										const struct spa_dict* props);
			void					_OnLinkRemove(uint32 id);

			static void				_NodeEventThunk(void* data, uint32 id,
										uint32 permissions, const char* type,
										uint32 version, const struct spa_dict* props);
			static void				_NodeRemoveThunk(void* data, uint32 id);
			void					_OnNodeEvent(uint32 id, uint32 permissions,
										const char* type, uint32 version,
										const struct spa_dict* props);
			void					_OnNodeRemove(uint32 id);

public:

private:
									PipeWireBackend();
									~PipeWireBackend();
									PipeWireBackend(const PipeWireBackend&) = delete;
			PipeWireBackend&		operator=(const PipeWireBackend&) = delete;

			status_t				_Init();
			void					_Teardown();

			struct pw_node*			_BindNode(uint32 id);

			static void				_CoreDoneThunk(void* data, uint32 id, int seq);

			struct VolumeQuery {
				std::vector<float>	volumes;
				bool				mute;
				bool				haveMute;
				bool				done;
			};
			static void				_NodeParamThunk(void* data, int seq,
										uint32 id, uint32 index, uint32 next,
										const struct spa_pod* param);

			struct NodeControlQuery {
				std::vector<NodeControlInfo>	info;
				bool					done;
			};
			static void				_NodeControlParamThunk(void* data, int seq,
										uint32 id, uint32 index, uint32 next,
										const struct spa_pod* param);

			static void				_NodeVideoModeThunk(void* data, int seq,
										uint32 id, uint32 index, uint32 next,
										const struct spa_pod* param);

			struct RouteVolumeQuery {
				int32				wantDirection;
				int32				index;
				int32				device;
				std::vector<float>	volumes;
				bool				mute;
				bool				haveMute;
				bool				haveRoute;
			};
			static void				_RouteVolumeParamThunk(void* data,
										int seq, uint32 id, uint32 index,
										uint32 next,
										const struct spa_pod* param);

			struct PropInfoQuery {
				std::vector<NodePropInfo>	infos;
				bool						done;
			};
			static void				_PropInfoThunk(void* data, int seq,
										uint32 id, uint32 index, uint32 next,
										const struct spa_pod* param);

			struct NodePropsQuery {
				std::vector<NodePropValue>	values;
				bool						done;
			};
			static void				_NodePropsThunk(void* data, int seq,
										uint32 id, uint32 index, uint32 next,
										const struct spa_pod* param);

			status_t				_QueryActiveRoute(uint32 cardId,
										int32 wantDirection,
										RouteVolumeQuery& outQuery);

			static void				_ParsePropInfoPod(const struct spa_pod* pod,
										NodePropInfo& outInfo);
			static void				_ParsePropsPod(const struct spa_pod* pod,
										std::vector<NodePropValue>& outValues);
			static bool				_ValueFromPod(const struct spa_pod* pod,
										uint32& outType,
										std::vector<uint8_t>& outBytes);
			static const struct spa_pod* _PodFromValue(
										const NodePropValue& value,
										void* buffer, size_t bufferSize);

private:
			pw_thread_loop*			fThreadLoop;
			pw_loop*				fLoop;
			pw_context*				fContext;
			pw_core*				fCore;
			pw_registry*			fRegistry;
			status_t				fInitStatus;

			spa_hook				fRegistryListener;
			std::mutex				fDevicesLock;
			std::vector<DeviceInfo>	fDevices;

			struct CardWatch {
				PipeWireBackend*	backend;
				uint32				cardId;
				pw_device*			proxy;
				spa_hook			hook;

				std::map<int32, int32>	lastRoute;
				int32				lastProfile;
			};

			std::map<uint32, CardWatch*>	fCardWatches;
			static void				_CardParamThunk(void* data, int seq,
										uint32 id, uint32 index, uint32 next,
										const struct spa_pod* param);
			void					_OnCardParam(CardWatch* watch, uint32 id,
										const struct spa_pod* param);

			void					_DestroyCardWatch(uint32 cardId);

			std::vector<StreamInfo>	fStreams;
			std::mutex				fStreamsLock;

			std::vector<MidiNodeInfo>	fMidiNodes;
			std::mutex				fMidiNodesLock;

			void*					fMetadata;
			spa_hook				fMetadataListener;
			std::mutex				fMetadataLock;
			std::string				fDefaultSinkName;
			std::string				fDefaultSourceName;
			std::string				fDefaultVideoSourceName;

			struct Watcher {
				BMessenger			messenger;
				int32				mask;
			};
			std::mutex				fWatchersLock;
			std::vector<Watcher>	fWatchers;

			struct TestTone {
				pw_stream*			stream;
				std::atomic<double>	phase;
				double				phaseIncrement;
				int					channel;
				int					channelCount;
				std::thread			stopper;
			};
			std::mutex					fTestTonesLock;
			std::map<uint32, TestTone*>	fTestTones;
			static void					_TestToneProcessThunk(void* data);
			void						_StopTestSpeakerLocked(uint32 deviceId);

			struct PortRegistryEntry {
				uint32			nodeId;
				bool			isOutput;
				uint32			portOrdinal;
				std::string		name;
			};
			std::mutex				fPortsLock;
			std::map<uint32, PortRegistryEntry>	fPorts;

			spa_hook				fLinkListener;
			std::mutex					fLinkToDeviceMapLock;
			std::map<uint32, uint32>	fLinkToDeviceMap;
			// Full registry link table for BMediaGraph::GetLinks.
			std::mutex					fRegistryLinksLock;
			std::map<uint32, RegistryLinkInfo>	fRegistryLinks;
			spa_hook				fNodeListener;
			std::map<uint32, std::string> fNodeNames;
			std::map<uint32, std::string> fNodeApps;

			struct NodeParamWatch {
				PipeWireBackend*	backend;
				uint32				nodeId;
				struct pw_node*		proxy;
				spa_hook			hook;
				std::vector<BControllable*>	targets;
			};
			std::mutex				fNodeParamWatchesLock;
			std::map<uint32, NodeParamWatch*>	fNodeParamWatches;
			static void				_NodeParamWatchThunk(void* data, int seq,
										uint32 id, uint32 index, uint32 next,
										const struct spa_pod* param);
			void					_OnNodeParamWatch(NodeParamWatch* watch,
										uint32 id, const struct spa_pod* param);
			void					_DestroyNodeParamWatch(uint32 nodeId);

			// PropInfo/Props published on filters we created, so QueryNode*
			// can serve them even if the server-side filter does not yet.
			std::mutex				fPublishedParamsLock;
			std::map<uint32, std::vector<NodePropInfo> >	fPublishedInfos;
			std::map<uint32, std::vector<NodePropValue> >	fPublishedValues;

			static int				_MetadataPropertyThunk(void* data,
										uint32 id, const char* key,
										const char* type, const char* value);
};

} }

#endif
