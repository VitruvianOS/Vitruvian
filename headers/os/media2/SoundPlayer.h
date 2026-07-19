/*
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_SOUND_PLAYER_H
#define _MEDIA2_SOUND_PLAYER_H


#include <media2/MediaFormat.h>


class BSoundPlayer {
public:
	enum sound_player_notification {
		B_STARTED = 1,
		B_STOPPED,
		B_SOUND_DONE
	};

	// haiku-latest names. media2 spellings (PlayBuffer / Notifier) used to
	// alias these but conflict with the protected virtual method
	// PlayBuffer() below — kept as a single set of names matching legacy.
	typedef void (*BufferPlayerFunc)(void* cookie, void* buffer, size_t size,
		const media_raw_audio_format& format);
	typedef void (*EventNotifierFunc)(void* cookie,
		sound_player_notification what, ...);

	// Convenience aliases retained for media2 call sites — these are
	// pointer-typedefs, no class-member name conflict.
	typedef BufferPlayerFunc	PlayBufferFunc;
	typedef EventNotifierFunc	NotifyFunc;

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

			status_t			SetVolume(float volume);	// 0..1
			float				Volume() const;
			status_t			SetVolumeDB(float dB);
			float				VolumeDB() const;

			void				SetCallbacks(BufferPlayerFunc playFunc = NULL,
									EventNotifierFunc notifyFunc = NULL,
									void* cookie = NULL);

			// haiku-latest split setters — both delegate to SetCallbacks.
			void				SetBufferPlayer(BufferPlayerFunc playFunc);
			void				SetNotifier(EventNotifierFunc notifyFunc);
			void*				Cookie() const;
			void				SetCookie(void* cookie);

			bigtime_t			Latency() const;
			size_t				BufferSize() const;

			bool				HasData() const;
			void				SetHasData(bool hasData);

protected:
	// Legacy override path. Default implementations route to the function
	// pointers set via SetCallbacks / SetBufferPlayer / SetNotifier. To
	// supply audio by subclassing, override PlayBuffer and leave the
	// function pointer unset. The RT-thread contract is identical either
	// way: fill `buffer` with `size` bytes of PCM in `format`.
	virtual	void				PlayBuffer(void* buffer, size_t size,
									const media_raw_audio_format& format);
	virtual	void				Notify(sound_player_notification what, ...);

private:
			class Impl;
			Impl*				fImpl;

	friend class Impl;
};


#endif // _MEDIA2_SOUND_PLAYER_H
