/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_UNIT_H
#define _MEDIA2_MEDIA_UNIT_H


#include <media2/MediaClient.h>


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

protected:
	virtual	void					ProcessCycle(uint32 frameCount);

			void*					BufferFor(BMediaConnection* connection,
										size_t* outSize);

public:
			void*					_GetFilter() const;

			// PipeWire node id of this unit's filter (0 if not started).
			uint32					NodeId() const;

			// Publish SPA PropInfo/Props on this unit's filter node so
			// foreign clients and BControllable::BindNode can see them.
			// Prop ids should use SPA_PROP_START_CUSTOM or higher.
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
			status_t				_BuildFilter();
			void					_TeardownFilter();
			void					_Restart();

			struct Impl;
			Impl*					fImpl;
};


#endif
