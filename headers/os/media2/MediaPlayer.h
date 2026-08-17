/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_PLAYER_H
#define _MEDIA2_MEDIA_PLAYER_H


#include <Entry.h>
#include <Messenger.h>

#include <media2/MediaFormat.h>


class BView;


class BMediaPlayer {
public:
	enum player_state {
		B_PLAYER_STOPPED   = 0,
		B_PLAYER_PLAYING,
		B_PLAYER_PAUSED,
		B_PLAYER_BUFFERING
	};

	enum notification {
		B_PLAYER_STATE_CHANGED     = 'plsc',
		B_PLAYER_POSITION_CHANGED  = 'plpc',
		B_PLAYER_DURATION_CHANGED  = 'pldc',
		B_PLAYER_ERROR             = 'pler',
		B_PLAYER_END_OF_STREAM     = 'pleo'
	};

	enum sound_player_notification {
		B_STARTED = 1,
		B_STOPPED,
		B_SOUND_DONE
	};

	typedef void (*BufferPlayerFunc)(void* cookie, void* buffer, size_t size,
		const media_raw_audio_format& format);
	typedef void (*EventNotifierFunc)(void* cookie,
		sound_player_notification what, ...);


	typedef BufferPlayerFunc	FillFunc;
	typedef EventNotifierFunc	Notifier;

								BMediaPlayer();
								BMediaPlayer(const entry_ref* ref);
	virtual						~BMediaPlayer();

			status_t			InitCheck() const;

			status_t			SetTo(const entry_ref* ref);

			const BMediaFormat&	Format() const;
			status_t			SetFormat(const BMediaFormat& format);

			status_t			Play();
			status_t			Pause();
			status_t			Stop();

			status_t			SeekTo(bigtime_t position);
			bigtime_t			Position() const;
			bigtime_t			Duration() const;

			player_state		State() const;
			bool				IsPlaying() const;

			status_t			SetVolume(float volume);
			float				Volume() const;

			void				SetTarget(BMessenger target);

			void				SetVideoView(BView* view);


			status_t			SetHooks(BufferPlayerFunc fillFunc = NULL,
									EventNotifierFunc notifyFunc = NULL,
									void* cookie = NULL);
			void				SetCallbacks(FillFunc fillFunc = NULL,
									Notifier notifyFunc = NULL,
									void* cookie = NULL);

			bool				HasData() const;
			void				SetHasData(bool hasData);

			size_t				BufferSize() const;

protected:
	virtual	void				BufferReceived(void* buffer, size_t size,
									const media_raw_audio_format& format);

private:
			class Impl;
			Impl*				fImpl;

	friend class Impl;
};


#endif
