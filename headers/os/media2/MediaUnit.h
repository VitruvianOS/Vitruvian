/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_UNIT_H
#define _MEDIA2_MEDIA_UNIT_H


#include <media2/MediaClient.h>


// Ids of published unit properties; custom ones start at the base.
enum {
	B_MEDIA_PROP_CUSTOM_BASE	= 0x1000000
};

// Value types accepted by PublishNodeParams().
enum media_prop_type {
	B_MEDIA_PROP_BOOL	= 2,
	B_MEDIA_PROP_ID		= 3,
	B_MEDIA_PROP_INT32	= 4,
	B_MEDIA_PROP_INT64	= 5,
	B_MEDIA_PROP_FLOAT	= 6,
	B_MEDIA_PROP_DOUBLE	= 7,
	B_MEDIA_PROP_STRING	= 8
};


class BMediaUnit : public BMediaClient {
public:
									BMediaUnit(const char* name,
										media_client_kinds kinds);
	virtual							~BMediaUnit();

			uint32					Quantum() const;
			uint32					SampleRate() const;
			bigtime_t				CycleStartTime() const;

	virtual	status_t				RegisterInput(BMediaInput* input) override;
	virtual	status_t				RegisterOutput(BMediaOutput* output) override;
	virtual	status_t				UnregisterInput(BMediaInput* input) override;
	virtual	status_t				UnregisterOutput(BMediaOutput* output) override;

	virtual	status_t				Bind(BMediaInput* input,
										BMediaOutput* output) override;
	virtual	status_t				Unbind(BMediaInput* input,
										BMediaOutput* output) override;

	virtual	status_t				Start() override;
	virtual	status_t				Stop() override;

	// A unit runs detached (no pw_filter, driven by the in-process sender)
	// when it is connected only to in-process peers; otherwise it is a
	// graph node. The mode cannot change while the unit is started or linked.
	virtual	bool					IsInProcessRouted() const override;
			bool					IsDetached() const;
			bool					IsDetachedOnly() const;

protected:
	// Detached-only units (GStreamer chains) refuse graph peers with
	// B_NOT_SUPPORTED; call from the constructor.
			void					SetDetachedOnly(bool detachedOnly);

	// Detached fan-in: the cycle runs when this input delivers; the other
	// inputs are latched. Defaults to the first non-MIDI input.
			void					SetDrivingInput(BMediaInput* input);

	// In graph mode ProcessCycle runs on the realtime thread and must not
	// block. In detached mode it runs on the sender's thread.
	virtual	void					ProcessCycle(uint32 frameCount);

			void*					BufferFor(BMediaConnection* connection,
										size_t* outSize);

public:
			void*					_GetFilter() const;

			// Graph node id of this unit (0 if not started).
			uint32					NodeId() const;

			// Publish properties on this unit's node so foreign clients and
			// BControllable::BindNode can see them. Ids start at
			// B_MEDIA_PROP_CUSTOM_BASE; valueTypes are media_prop_type.
			status_t				PublishNodeParams(
										const uint32* propIds,
										const char* const* names,
										const uint32* valueTypes,
										const float* minValues,
										const float* maxValues,
										const float* steps,
										const void* const* values,
										const size_t* valueSizes,
										int32 count);

private:
	virtual	status_t				_StartConnections(void* backend) override;
			status_t				_BuildFilter();
			void					_TeardownFilter();
			void					_Restart();

			status_t				_EnterDetached();
			void					_DetachedLink(BMediaConnection* connection,
										bool attach);
			status_t				_DetachedDeliver(BMediaInput* input,
										void* buffer, size_t size,
										const BMediaFormat& format);
			void*					_DetachedBufferFor(
										BMediaConnection* connection,
										size_t* outSize);

			struct Impl;
			Impl*					fImpl;

	friend class BMediaGraph;
	friend class BMediaOutput;
};


#endif
