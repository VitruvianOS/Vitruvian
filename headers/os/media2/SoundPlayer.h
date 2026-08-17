/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_SOUND_PLAYER_H
#define _MEDIA2_SOUND_PLAYER_H


#include <media2/MediaFormat.h>


class BSound;


class BSoundPlayer {
public:
	enum sound_player_notification {
		B_STARTED = 1,
		B_STOPPED,
		B_SOUND_DONE
	};

	typedef void (*BufferPlayerFunc)(void* cookie, void* buffer, size_t size,
		const media_raw_audio_format& format);
	typedef void (*EventNotifierFunc)(void* cookie,
		sound_player_notification what, ...);

	typedef BufferPlayerFunc	PlayBufferFunc;
	typedef EventNotifierFunc	NotifyFunc;

	typedef int32 play_id;

								BSoundPlayer(const BMediaFormat* format,
									const char* name = NULL,
									BufferPlayerFunc playFunc = NULL,
									EventNotifierFunc notifyFunc = NULL,
									void* cookie = NULL);
								BSoundPlayer(const char* name,
									BufferPlayerFunc playFunc = NULL,
									EventNotifierFunc notifyFunc = NULL,
									void* cookie = NULL);
	virtual						~BSoundPlayer();

			status_t			InitCheck() const;

			const BMediaFormat&	Format() const;
			status_t			SetFormat(const BMediaFormat& format);

			status_t			Start();
			void				Stop(bool block = true, bool flush = true);
			bool				IsPlaying() const;

			status_t			SetVolume(float volume);
			float				Volume() const;
			status_t			SetVolumeDB(float dB);
			float				VolumeDB() const;

			void				SetCallbacks(BufferPlayerFunc playFunc = NULL,
									EventNotifierFunc notifyFunc = NULL,
									void* cookie = NULL);


			void				SetBufferPlayer(BufferPlayerFunc playFunc);
			void				SetNotifier(EventNotifierFunc notifyFunc);
			void*				Cookie() const;
			void				SetCookie(void* cookie);

			bigtime_t			Latency() const;
			size_t				BufferSize() const;

			bool				HasData() const;
			void				SetHasData(bool hasData);

			bigtime_t			CurrentTime() const;
			bigtime_t			PerformanceTime() const;
			status_t			Preroll();

			play_id				StartPlaying(BSound* sound,
									bigtime_t atTime = 0);
			play_id				StartPlaying(BSound* sound,
									bigtime_t atTime, float withVolume);
			status_t			SetSoundVolume(play_id id, float newVolume);
			bool				IsPlaying(play_id id);
			status_t			StopPlaying(play_id id);
			status_t			WaitForSound(play_id id);


			status_t			GetVolumeInfo(int32* _parameterID,
									float* _minDB, float* _maxDB);

protected:

	virtual	void				PlayBuffer(void* buffer, size_t size,
									const media_raw_audio_format& format);
	virtual	void				Notify(sound_player_notification what, ...);

private:
			class Impl;
			Impl*				fImpl;

	friend class Impl;
};


#endif
