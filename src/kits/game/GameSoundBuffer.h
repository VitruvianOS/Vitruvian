/*
 * Vitruvian — GameSoundBuffer
 * Distributed under the terms of the MIT License.
 *
 * Reworked for media2: no legacy BMediaNode/BBufferProducer machinery.
 * GameSoundBuffer represents one playable sound owned by BGameSoundDevice.
 * The device's BSoundPlayer callback drives the mixer and calls each active
 * buffer's Play() to read from FillBuffer() into the device's mix buffer.
 */

#ifndef _GAMESOUNDBUFFER_H
#define _GAMESOUNDBUFFER_H


#include <GameSoundDefs.h>


struct _gs_ramp;


class GameSoundBuffer {
public:
									GameSoundBuffer(const gs_audio_format* format);
	virtual							~GameSoundBuffer();

			status_t				StartPlaying();
			status_t				StopPlaying();
			bool					IsPlaying() const;

			// Pulled by BGameSoundDevice each callback. Reads up to `frames`
			// frames into `data`, applies gain/pan/looping.
			void					Play(void* data, int64 frames);
			void					UpdateMods();
	virtual void					Reset();

	virtual	void*					Data() { return NULL; }
			const gs_audio_format&	Format() const;

			bool					IsLooping() const;
			void					SetLooping(bool loop);
			float					Gain() const;
			status_t				SetGain(float gain, bigtime_t duration);
			float					Pan() const;
			status_t				SetPan(float pan, bigtime_t duration);

	virtual	status_t				GetAttributes(gs_attribute* attributes,
										size_t attributeCount);
	virtual	status_t				SetAttributes(gs_attribute* attributes,
										size_t attributeCount);

protected:
	virtual	void					FillBuffer(void* data, int64 frames) = 0;

			gs_audio_format			fFormat;
			bool					fLooping;
			size_t					fFrameSize;

private:
			bool					fIsPlaying;
			float					fGain;
			float					fPan;
			float					fPanLeft;
			float					fPanRight;
			_gs_ramp*				fGainRamp;
			_gs_ramp*				fPanRamp;
};


class SimpleSoundBuffer : public GameSoundBuffer {
public:
									SimpleSoundBuffer(const gs_audio_format* format,
										const void* data, int64 frames = 0);
	virtual							~SimpleSoundBuffer();

	virtual	void*					Data() { return fBuffer; }
	virtual	void					Reset();

protected:
	virtual	void					FillBuffer(void* data, int64 frames);

private:
			char*					fBuffer;
			size_t					fBufferSize;
			size_t					fPosition;
};


// Streaming buffer — owner is a BStreamingGameSound* whose virtual
// FillBuffer is called every callback. The owner pointer is kept opaque
// (void*) to avoid pulling StreamingGameSound.h into the buffer header.
class StreamingSoundBuffer : public GameSoundBuffer {
public:
									StreamingSoundBuffer(const gs_audio_format* format,
										void* streamHook);
	virtual							~StreamingSoundBuffer();

protected:
	virtual	void					FillBuffer(void* data, int64 frames);

private:
			void*					fStreamHook;	// BStreamingGameSound*
};


#endif // _GAMESOUNDBUFFER_H
