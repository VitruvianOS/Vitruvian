/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _VITRUVIAN_MEDIA2_PIPEWIRE_BACKEND_H
#define _VITRUVIAN_MEDIA2_PIPEWIRE_BACKEND_H

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <pipewire/pipewire.h>
#include <pipewire/stream.h>

#include <Messenger.h>
#include <SupportDefs.h>

#include <media2/MediaFormat.h>


namespace BPrivate { namespace media {


class PipeWireBackend {
public:
	static	PipeWireBackend*		GetInstance();

			pw_loop*				GetMainLoop() const { return fLoop; }
			pw_core*				GetCore()     const { return fCore; }
			pw_registry*			GetRegistry() const { return fRegistry; }

			void					Lock();
			void					Unlock();

			pw_filter*				CreateFilter(const char* name,
										pw_properties* props);

			pw_stream*				CreateAndConnectStream(
										const char* name,
										pw_direction direction,
										const BMediaFormat& format,
										const pw_stream_events* events,
										void* userdata,
										const char* targetNodeName = NULL,
										bool captureSinkMonitor = false);

			void					DestroyStream(pw_stream* stream);

		struct DeviceInfo {
			uint32			id;
			uint32			cardId;
			std::string		name;
			std::string		nodeName;
			std::string		mediaClass;
			bool			isSink;
			bool			isSource;
		};

		std::vector<DeviceInfo>	Devices();

			uint32					GetDefaultSinkId();
			uint32					GetDefaultSourceId();

			status_t				SetDefaultSink(uint32 deviceId);
			status_t				SetDefaultSource(uint32 deviceId);

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
			kMsgDevicesChanged      = 'PWDC',
			kMsgStreamsChanged      = 'PWSC',
			kMsgDefaultChanged      = 'PWDF',
			kMsgDeviceVolumeChanged = 'PWVC',

			kMsgDevicePortChanged   = 'PWPC',
			kMsgDeviceProfileChanged= 'PWPF'
		};

		void					AddWatcher(const BMessenger& target);
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

		status_t				ResolveNodePorts(uint32 nodeId,
									pw_direction direction,
									std::vector<uint32>& outPortIds,
									bigtime_t timeoutUsecs = 2000000);

			status_t				RoundtripLocked(
									bigtime_t timeoutUsecs = 2000000);

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

			status_t				_QueryActiveRoute(uint32 cardId,
										int32 wantDirection,
										RouteVolumeQuery& outQuery);

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

			void*					fMetadata;
			spa_hook				fMetadataListener;
			std::mutex				fMetadataLock;
			std::string				fDefaultSinkName;
			std::string				fDefaultSourceName;

			std::mutex				fWatchersLock;
			std::vector<BMessenger>	fWatchers;

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
			spa_hook				fNodeListener;
			std::map<uint32, std::string> fNodeNames;
			std::map<uint32, std::string> fNodeApps;

			static int				_MetadataPropertyThunk(void* data,
										uint32 id, const char* key,
										const char* type, const char* value);
};

} }

#endif
