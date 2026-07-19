/*
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
										media_type type = B_MEDIA_UNKNOWN_TYPE,
										media_client_kinds kinds = B_MEDIA_PLAYER);
	virtual							~BMediaClient();

			media_client_id			Id() const;
			const char*				Name() const;
			media_client_kinds		Kinds() const;
			media_type				MediaType() const;
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

			bool					IsStarted() const;
	virtual	status_t				Start();
	virtual	status_t				Stop();
	virtual	status_t				Seek(bigtime_t mediaTime,
										bigtime_t performanceTime);

			bigtime_t				CurrentTime() const;

			BControllable*			Controllable() const;
			void					SetControllable(BControllable* controllable);

protected:
	// Subclass hooks
	virtual	void					HandleStart(bigtime_t performanceTime);
	virtual	void					HandleStop(bigtime_t performanceTime);
	virtual	void					HandleSeek(bigtime_t mediaTime,
										bigtime_t performanceTime);

	// Realtime — runs on the PipeWire data thread.
	// buffer is the planar uint8 region for the connection's data buffer;
	// for raw-audio nodes, cast to (float*)/(int16_t*) per format.
	// frameCount is the number of frames available in the buffer.
	// `connection` is the BMediaConnection whose port produced/needs data.
	virtual	void					ProcessCallback(BMediaConnection* connection,
										void* buffer, size_t bufferSize,
										uint32 frameCount);

	// Subclasses (BMediaNode) override to advertise their preferred format
	// before Start() builds the SPA pod params. Default reports the format
	// stored on the first registered output/input (whichever exists).
	virtual	status_t				GetStreamFormat(BMediaFormat* outFormat) const;

private:
			struct Impl;
			Impl*					fImpl;
};


#endif // _MEDIA2_MEDIA_CLIENT_H
