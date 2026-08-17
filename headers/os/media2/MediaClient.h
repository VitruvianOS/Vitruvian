/*
 * Copyright 2015-2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_CLIENT_H
#define _MEDIA2_MEDIA_CLIENT_H


#include <String.h>

#include <media2/MediaClientDefs.h>
#include <media2/MediaConnection.h>
#include <media2/MediaFormat.h>


class BControllable;


class BMediaClient {
public:
									BMediaClient(const char* name,
										media_client_kinds kinds);
	virtual							~BMediaClient();

			media_client_id			Id() const;
			const char*				Name() const;
			media_client_kinds		Kinds() const;

			media_type_mask			MediaTypes() const;
			status_t				InitCheck() const;

	virtual	status_t				RegisterInput(BMediaInput* input);
	virtual	status_t				RegisterOutput(BMediaOutput* output);
	virtual	status_t				UnregisterInput(BMediaInput* input);
	virtual	status_t				UnregisterOutput(BMediaOutput* output);

			int32					CountInputs() const;
			int32					CountOutputs() const;
			BMediaInput*			InputAt(int32 index) const;
			BMediaOutput*			OutputAt(int32 index) const;

	virtual	status_t				Bind(BMediaInput* input, BMediaOutput* output);
	virtual	status_t				Unbind(BMediaInput* input, BMediaOutput* output);


			status_t				Connect(BMediaOutput* output,
										BMediaInput* input);
			status_t				Disconnect(BMediaOutput* output,
										BMediaInput* input);

			bool					IsStarted() const;
	virtual	status_t				Start();
	virtual	status_t				Stop();
	virtual	status_t				Seek(bigtime_t mediaTime,
										bigtime_t performanceTime);

			bigtime_t				CurrentTime() const;

			BControllable*			Controllable() const;
			void					SetControllable(BControllable* controllable);

protected:
	virtual	void					HandleStart(bigtime_t performanceTime);
	virtual	void					HandleStop(bigtime_t performanceTime);
	virtual	void					HandleSeek(bigtime_t mediaTime,
										bigtime_t performanceTime);

	virtual	void					ProcessCallback(BMediaConnection* connection,
										void* buffer, size_t bufferSize,
										uint32 frameCount);

	virtual	status_t				_StartConnections(void* backend);
	virtual	void					_StopConnections();

	static const struct pw_stream_events*	_GetStreamEvents();

private:
			struct Impl;
			Impl*					fImpl;

	friend class BMediaNode;
	friend class BMidiUnit;
	friend class BSimpleMediaNode;
};


#endif
