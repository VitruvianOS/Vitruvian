/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _CONTROLLER_H
#define _CONTROLLER_H


#include <Entry.h>
#include <Locker.h>
#include <Messenger.h>
#include <String.h>
#include <Url.h>

#include <media2/MediaPlayer.h>

#include "MediaMessages.h"
#include "Playlist.h"


class BMediaFile;
class BView;
class MidiPlayback;


class Controller : public Playlist::Listener {
public:
								Controller(const BMessenger& target);
								~Controller();

			status_t			InitCheck() const;

			void				SetVideoView(BView* view);

			status_t			OpenRef(const entry_ref& ref);
			status_t			OpenUrl(const BUrl& url, const char* name);
			status_t			OpenMidi(const entry_ref& ref);
			status_t			OpenCd(int32 track);

			status_t			Play();
			status_t			Pause();
			status_t			Stop();
			status_t			TogglePlaying();
			status_t			SeekTo(bigtime_t position);
			status_t			SeekBy(bigtime_t delta);
			status_t			SkipForward();
			status_t			SkipBackward();
			status_t			Rewind();
			status_t			Forward();
			status_t			StepFrame(bool forward);
			void				HandleEOS();

			status_t			SetVolume(float volume);
			float				Volume() const;
			status_t			VolumeStep(bool up);
			status_t			SetMute(bool mute);
			bool				IsMuted() const
									{ return fMuted; }

			void				SetLoop(bool loop);
			bool				IsLooping() const
									{ return fLoop; }

			bigtime_t			Position() const;
			bigtime_t			Duration() const;
			BMediaPlayer::player_state	State() const;
			bool				IsPlaying() const;

			Playlist&			PlaylistRef()
									{ return fPlaylist; }

			int32				AudioTrackCount() const
									{ return fAudioTrackCount; }
			int32				VideoTrackCount() const
									{ return fVideoTrackCount; }
			bool				HasVideo() const
									{ return fHasVideo; }
			uint32				VideoWidth() const
									{ return fVideoWidth; }
			uint32				VideoHeight() const
									{ return fVideoHeight; }
			float				VideoFrameRate() const
									{ return fVideoFrameRate; }
			float				AudioFrameRate() const
									{ return fAudioFrameRate; }
			uint32				AudioChannels() const
									{ return fAudioChannels; }
			uint32				AudioBits() const
									{ return fAudioBits; }
			float				AudioBitRate() const
									{ return fAudioBitRate; }
			const char*			VideoCodec() const
									{ return fVideoCodec; }
			const char*			AudioCodec() const
									{ return fAudioCodec; }

			// Playlist::Listener
	virtual	void				ItemsChanged();
	virtual	void				CurrentItemChanged(int32 newIndex, bool play);

private:
			status_t			_SetToRef(const entry_ref& ref);
			status_t			_SetToUrl(const BUrl& url);
			void				_ProbeTracks(const entry_ref* ref, const BUrl* url);
			void				_Notify(uint32 what);
			void				_NotifyState();
			void				_NotifyPosition();
			void				_NotifyFinished();
			void				_ApplyVolume();
			void				_StopMidi();

private:
			BMessenger			fTarget;
			BMediaPlayer		fPlayer;
			MidiPlayback*		fMidi;
			BMediaFile*			fProbeFile;
			Playlist			fPlaylist;
			mutable BLocker		fLock;
			float				fVolume;
			bool				fMuted;
			bool				fWasMuted;
			int32				fAudioTrackCount;
			int32				fVideoTrackCount;
			bool				fHasVideo;
			uint32				fVideoWidth;
			uint32				fVideoHeight;
			bool				fOpening;
			BString				fPendingName;
			bool				fLoop;
			float				fVideoFrameRate;
			float				fAudioFrameRate;
			uint32				fAudioChannels;
			uint32				fAudioBits;
			float				fAudioBitRate;
			char				fVideoCodec[96];
			char				fAudioCodec[96];
};


#endif // _CONTROLLER_H
