/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _MEDIA2_MEDIA_ENCODER_H
#define _MEDIA2_MEDIA_ENCODER_H


#include <media2/MediaFormat.h>


struct media_encode_info {
	bigtime_t	start_time;
	uint32		flags;
	uint32		_reserved[4];
};


// Encoder wrapper over a codec backend. The raw input format
// comes from the output format's embedded raw parameters (or SetFormat).
// Each encoded chunk leaves through the WriteChunk() hook, so a subclass
// can pipe it into a BMediaFile track or collect it in a buffer.
class BMediaEncoder {
public:
								BMediaEncoder();
	explicit					BMediaEncoder(const BMediaFormat& outputFormat);
	virtual						~BMediaEncoder();

			status_t			InitCheck() const;

			status_t			SetTo(const BMediaFormat& outputFormat);
			status_t			SetFormat(BMediaFormat* inputFormat,
									BMediaFormat* outputFormat);

			status_t			Encode(const void* buffer, int64 frameCount,
									media_encode_info* info = NULL);
			status_t			SetQuality(float quality);
			status_t			Flush();

protected:
	virtual	status_t			WriteChunk(const void* buffer, size_t size,
									media_encode_info* info) = 0;
	virtual	status_t			AddTrackInfo(uint32 code, const char* data,
									size_t size);

private:
			class Impl;
			Impl*				fImpl;

			BMediaEncoder(const BMediaEncoder& other);
			BMediaEncoder&		operator=(const BMediaEncoder& other);
};


class BMediaBufferEncoder : public BMediaEncoder {
public:
								BMediaBufferEncoder();
	explicit					BMediaBufferEncoder(
									const BMediaFormat& outputFormat);

			status_t			EncodeToBuffer(void* outputBuffer,
									size_t* _size, const void* inputBuffer,
									int64 frameCount,
									media_encode_info* info = NULL);

protected:
	virtual	status_t			WriteChunk(const void* buffer, size_t size,
									media_encode_info* info);

			void*				fBuffer;
			int32				fBufferSize;
			size_t				fBufferUsed;
};


#endif // _MEDIA2_MEDIA_ENCODER_H
