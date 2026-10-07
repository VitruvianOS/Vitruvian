/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_CONTROLLABLE_H
#define _MEDIA2_CONTROLLABLE_H


#include <Locker.h>
#include <Messenger.h>
#include <ObjectList.h>

#include <media2/MediaDefs.h>


#define B_MEDIA_NEW_PARAMETER_VALUE 'mnpv'


class BParameterWeb;
class BView;

namespace BPrivate { namespace media {
class PipeWireBackend;
} }


class BControllable {
public:
	typedef void (*change_hook)(int32 id, bigtime_t when,
								const void* value, size_t size, void* cookie);

								BControllable();
	virtual						~BControllable();

			void					SetChangeHook(change_hook hook, void* cookie);

			BParameterWeb*			Web() const;
			status_t				SetParameterWeb(BParameterWeb* web);

	// Bind this controllable to a PipeWire node's SPA_PARAM_PropInfo/Props.
	// Works on any node, including devices and foreign nodes we did not create.
			status_t				BindNode(uint32 nodeId);
			status_t				UnbindNode();
			uint32					BoundNodeId() const;

	virtual	status_t				GetParameterValue(int32 id,
									bigtime_t* lastChangeTime,
									void* value, size_t* ioSize);
	virtual	status_t				SetParameterValue(int32 id,
									bigtime_t changeTime,
									const void* value, size_t size);

			status_t				StartControlPanel(BMessenger watcher);
			status_t				StopControlPanel(BMessenger watcher);
			void					BroadcastChangedParameter(int32 id);

	virtual	BView*					MakeView();

private:
			struct ValueEntry;
			BParameterWeb*			fWeb;
			BObjectList<ValueEntry, true>		fValues;
			BObjectList<BMessenger, true>		fWatchers;
			change_hook				fChangeHook;
			void*					fChangeHookCookie;

			// Bound-node state. fLock is inner to PipeWireBackend's lock
			// (backend lock is always outer when both are taken).
			BLocker					fLock;
			uint32					fNodeId;

			status_t				_StoreValue(int32 id, bigtime_t when,
									const void* value, size_t size);
			ValueEntry*				_FindValue(int32 id) const;
			void					_ApplyProps(const void* pod);

	friend class BPrivate::media::PipeWireBackend;
};


#endif
