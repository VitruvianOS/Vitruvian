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

private:
			struct Impl;
			Impl*					fImpl;
};


#endif
