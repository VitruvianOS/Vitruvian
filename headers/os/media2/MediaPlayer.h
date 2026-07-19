/*
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

								BMediaPlayer();
								BMediaPlayer(const entry_ref* ref);
	virtual						~BMediaPlayer();

			status_t			InitCheck() const;

			status_t			SetTo(const entry_ref* ref);

			status_t			Play();
			status_t			Pause();
			status_t			Stop();

			status_t			SeekTo(bigtime_t position);
			bigtime_t			Position() const;
			bigtime_t			Duration() const;

			player_state		State() const;
			bool				IsPlaying() const;

			status_t			SetVolume(float volume);	// 0..1
			float				Volume() const;

			void				SetTarget(BMessenger target);
				// Receives B_PLAYER_* notification BMessages.

			void				SetVideoView(BView* view);
				// Stored but unused — video decode/render lands when the
				// MediaPlayer app needs it.

private:
			class Impl;
			Impl*				fImpl;
};


#endif // _MEDIA2_MEDIA_PLAYER_H
