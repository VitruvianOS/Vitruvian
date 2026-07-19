/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _VITRUVIAN_MEDIA2_PIPEWIRE_BACKEND_H
#define _VITRUVIAN_MEDIA2_PIPEWIRE_BACKEND_H


#include <mutex>
#include <string>
#include <vector>

#include <pipewire/pipewire.h>
#include <pipewire/stream.h>

#include <SupportDefs.h>

#include <media2/MediaFormat.h>


namespace BPrivate { namespace media {


class PipeWireBackend {
public:
	static	PipeWireBackend*		GetInstance();
									// Returns NULL if PipeWire is unavailable
									// (server unreachable). Callers must
									// null-check; the API surface above this
									// layer reports B_DEVICE_NOT_FOUND in that
									// case.

			pw_loop*				GetMainLoop() const { return fLoop; }
			pw_core*				GetCore()     const { return fCore; }
			pw_registry*			GetRegistry() const { return fRegistry; }

			void					Lock();
			void					Unlock();

			pw_filter*				CreateFilter(const char* name,
										pw_properties* props);

			// High-level pw_stream helpers — the only paths the rest of
			// media2 should use to talk to PipeWire.
			pw_stream*				CreateAndConnectStream(
										const char* name,
										pw_direction direction,
										const BMediaFormat& format,
										const pw_stream_events* events,
										void* userdata);
				// On success returns a connected, autoconnect, RT-process
				// stream. Returns NULL on failure (caller may inspect
				// errno-style status via subsequent state_changed events).

			void					DestroyStream(pw_stream* stream);
				// Disconnects and destroys, taking the loop lock as needed.

			// ── Device enumeration (B.13) ───────────────────────────────
			struct DeviceInfo {
				uint32			id;			// pw global id
				std::string		name;		// node.description or node.name
				std::string		nodeName;	// always node.name (for metadata match)
				std::string		mediaClass;	// "Audio/Sink", "Audio/Source", …
				bool			isSink;
				bool			isSource;
			};

			std::vector<DeviceInfo>	Devices();

				// Default device queries via pw_metadata (`default` object).
				// Returns 0 if metadata not yet received or the named node
				// hasn't shown up in Devices() yet.
				uint32					GetDefaultSinkId();
				uint32					GetDefaultSourceId();
				// Returns a snapshot of current audio sinks/sources. Updated
				// asynchronously as PipeWire nodes appear/disappear.

private:
			void					_OnRegistryGlobal(uint32 id, uint32 perms,
										const char* type, uint32 version,
										const struct spa_dict* props);
			void					_OnRegistryGlobalRemove(uint32 id);
			static void				_GlobalThunk(void* data, uint32 id,
										uint32 perms, const char* type,
										uint32 version, const struct spa_dict*);
			static void				_GlobalRemoveThunk(void* data, uint32 id);

public:

private:
									PipeWireBackend();
									~PipeWireBackend();
									PipeWireBackend(const PipeWireBackend&) = delete;
			PipeWireBackend&		operator=(const PipeWireBackend&) = delete;

			status_t				_Init();
			void					_Teardown();

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

			void*					fMetadata;	// pw_metadata*
			spa_hook				fMetadataListener;
			std::mutex				fMetadataLock;
			std::string				fDefaultSinkName;
			std::string				fDefaultSourceName;

			static int				_MetadataPropertyThunk(void* data,
										uint32 id, const char* key,
										const char* type, const char* value);
};


} } // namespace BPrivate::media


#endif // _VITRUVIAN_MEDIA2_PIPEWIRE_BACKEND_H
