/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _MEDIA2_MEDIA_DECODER_H
#define _MEDIA2_MEDIA_DECODER_H


#include <media2/MediaFormat.h>


// Decoder wrapper over a codec backend. Video only for now: the output is
// always raw B_RGB32, tightly packed (width * 4 bytes per row). Compressed
// input arrives through the GetNextChunk() hook.
class BMediaDecoder {
public:
								BMediaDecoder();
	explicit					BMediaDecoder(const BMediaFormat& inputFormat);
	virtual						~BMediaDecoder();

			status_t			InitCheck() const;

			status_t			SetTo(const BMediaFormat& inputFormat);
			status_t			GetDecodedFormat(BMediaFormat* outFormat) const;

			// Decodes one frame into outBuffer, which must hold
			// width * height * 4 bytes.
			status_t			Decode(void* outBuffer, int64* outFrameCount,
									BMediaFormat* outFormat);

protected:
	virtual	status_t			GetNextChunk(const void** chunk,
									size_t* size) = 0;

private:
			friend class BMediaBufferDecoder;

			class Impl;
			Impl*				fImpl;
			size_t				fOutputLimit;
			size_t				fOutputSize;

			BMediaDecoder(const BMediaDecoder& other);
			BMediaDecoder&		operator=(const BMediaDecoder& other);
};


class BMediaBufferDecoder : public BMediaDecoder {
public:
								BMediaBufferDecoder();
	explicit					BMediaBufferDecoder(
									const BMediaFormat& inputFormat);

			// Pushes one chunk and returns one decoded frame. On entry
			// *outputSize is the capacity of output, on return the bytes
			// written.
			status_t			DecodeBuffer(const void* input,
									size_t inputSize, void* output,
									size_t* outputSize,
									BMediaFormat* outFormat = NULL);

protected:
	virtual	status_t			GetNextChunk(const void** chunk,
									size_t* size);

private:
			const void*			fChunk;
			size_t				fChunkSize;
};


#endif // _MEDIA2_MEDIA_DECODER_H
