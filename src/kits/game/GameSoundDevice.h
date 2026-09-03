/*
 * Copyright 2001-2002, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _GAMESOUNDDEVICE_H
#define _GAMESOUNDDEVICE_H

#include <GameSoundDefs.h>


class GameSoundBuffer;

class BGameSoundDevice {
public:
	static	BGameSoundDevice*		GetDefaultDevice();
	static	void					ReleaseDevice();

			class Mixer;

									BGameSoundDevice();
	virtual							~BGameSoundDevice();

	status_t						InitCheck() const;
	virtual const gs_audio_format &	Format() const;		
	virtual const gs_audio_format &	Format(gs_id sound) const;
		
	virtual	status_t				CreateBuffer(gs_id * sound,
										const gs_audio_format * format,
										const void * data,
										int64 frames);
	virtual status_t				CreateBuffer(gs_id * sound,
										const void * object,
										const gs_audio_format * format,
										size_t inBufferFrameCount = 0,
										size_t inBufferCount = 0);
	virtual void					ReleaseBuffer(gs_id sound);
	
	virtual status_t				Buffer(gs_id sound,
										gs_audio_format * format,
										void *& data);
			
	virtual	bool					IsPlaying(gs_id sound);
	virtual	status_t				StartPlaying(gs_id sound);
	virtual status_t				StopPlaying(gs_id sound);
			
	virtual	status_t				GetAttributes(gs_id sound,
										gs_attribute * attributes,
										size_t attributeCount);
	virtual status_t				SetAttributes(gs_id sound,
										gs_attribute * attributes,
										size_t attributeCount);
											  	
protected:
			void					SetInitError(status_t error);
				
			gs_audio_format			fFormat;											

private:
			int32					AllocateSound();

			status_t				fInitError;
			bool					fIsConnected;
			int32					fSoundCount;
			GameSoundBuffer **		fSounds;
			Mixer*					fMixer;

	friend class Mixer;
};

#endif
