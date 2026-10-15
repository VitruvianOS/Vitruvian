/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "Controller.h"

#include <Autolock.h>
#include <Entry.h>
#include <Message.h>
#include <Path.h>
#include <String.h>
#include <string.h>

#include <media2/MediaDefs.h>
#include <media2/MediaFile.h>
#include <media2/MediaTrack.h>

#include "CDMediaSource.h"
#include "MidiPlayback.h"


Controller::Controller(const BMessenger& target)
	:
	fTarget(target),
	fMidi(NULL),
	fProbeFile(NULL),
	fVolume(1.0f),
	fMuted(false),
	fWasMuted(false),
	fAudioTrackCount(0),
	fVideoTrackCount(0),
	fHasVideo(false),
	fVideoWidth(0),
	fVideoHeight(0),
	fVideoParNum(0),
	fVideoParDen(0),
	fVideoRotation(0),
	fOpening(false),
	fLoop(false),
	fVideoFrameRate(0.0f),
	fAudioFrameRate(0.0f),
	fAudioChannels(0),
	fAudioBits(0),
	fAudioBitRate(0.0f)
{
	fVideoCodec[0] = '\0';
	fAudioCodec[0] = '\0';
	fPlaylist.AddListener(this);
	fPlayer.SetTarget(fTarget);
}


Controller::~Controller()
{
	fPlaylist.RemoveListener(this);
	_StopMidi();
	delete fProbeFile;
}


status_t
Controller::InitCheck() const
{
	return fPlayer.InitCheck();
}


void
Controller::SetVideoView(BView* view)
{
	fPlayer.SetVideoView(view);
}


void
Controller::_StopMidi()
{
	delete fMidi;
	fMidi = NULL;
}


void
Controller::_Notify(uint32 what)
{
	if (!fTarget.IsValid())
		return;
	BMessage msg(what);
	fTarget.SendMessage(&msg);
}


void
Controller::_NotifyState()
{
	if (!fTarget.IsValid())
		return;
	BMessage msg(MSG_CONTROLLER_STATE);
	msg.AddInt32("state", (int32)fPlayer.State());
	fTarget.SendMessage(&msg);
}


void
Controller::_NotifyPosition()
{
	if (!fTarget.IsValid())
		return;
	BMessage msg(MSG_CONTROLLER_POSITION);
	msg.AddInt64("position", Position());
	msg.AddInt64("duration", Duration());
	fTarget.SendMessage(&msg);
}


void
Controller::_NotifyFinished()
{
	if (!fTarget.IsValid())
		return;
	BMessage msg(MSG_CONTROLLER_FINISHED);
	fTarget.SendMessage(&msg);
}


void
Controller::_ApplyVolume()
{
	float volume = fMuted ? 0.0f : fVolume;
	if (fMidi != NULL)
		fMidi->SetVolume(volume);
	else
		fPlayer.SetVolume(volume);
}


void
Controller::_ProbeTracks(const entry_ref* ref, const BUrl* url)
{
	fAudioTrackCount = 0;
	fVideoTrackCount = 0;
	fHasVideo = false;
	fVideoWidth = 0;
	fVideoHeight = 0;
	fVideoParNum = 0;
	fVideoParDen = 0;
	fVideoRotation = 0;
	fVideoFrameRate = 0.0f;
	fAudioFrameRate = 0.0f;
	fAudioChannels = 0;
	fAudioBits = 0;
	fAudioBitRate = 0.0f;
	fVideoCodec[0] = '\0';
	fAudioCodec[0] = '\0';

	delete fProbeFile;
	fProbeFile = NULL;

	// Separate probe open: BMediaPlayer owns its own BMediaFile and plays
	// the first audio track; enumeration here is what tells has_video.
	if (ref != NULL)
		fProbeFile = new BMediaFile(ref);
	else if (url != NULL)
		fProbeFile = new BMediaFile(*url);
	if (fProbeFile == NULL || fProbeFile->InitCheck() != B_OK) {
		delete fProbeFile;
		fProbeFile = NULL;
		return;
	}

	int32 count = fProbeFile->CountTracks();
	for (int32 i = 0; i < count; i++) {
		BMediaTrack* track = fProbeFile->TrackAt(i);
		if (track == NULL)
			continue;
		BMediaFormat format;
		if (track->EncodedFormat(&format) == B_OK) {
			media_codec_info codecInfo;
			bool haveCodec = track->GetCodecInfo(&codecInfo) == B_OK;
			if (format.IsRawAudio() || format.IsEncodedAudio()) {
				fAudioTrackCount++;
				if (fAudioCodec[0] == '\0' && haveCodec)
					strlcpy(fAudioCodec, codecInfo.pretty_name,
						sizeof(fAudioCodec));
				const media_format& mf = format.format;
				if (mf.type == B_MEDIA_RAW_AUDIO) {
					fAudioFrameRate = mf.u.raw_audio.frame_rate;
					fAudioChannels = mf.u.raw_audio.channel_count;
					fAudioBits = (mf.u.raw_audio.format
						& media_raw_audio_format::B_AUDIO_SIZE_MASK) * 8;
				} else if (mf.type == B_MEDIA_ENCODED_AUDIO) {
					fAudioFrameRate = mf.u.encoded_audio.output.frame_rate;
					fAudioChannels = mf.u.encoded_audio.output.channel_count;
					fAudioBitRate = mf.u.encoded_audio.bit_rate;
				}
			} else if (format.IsRawVideo() || format.IsEncodedVideo()) {
				fVideoTrackCount++;
				fHasVideo = true;
				if (fVideoCodec[0] == '\0' && haveCodec)
					strlcpy(fVideoCodec, codecInfo.pretty_name,
						sizeof(fVideoCodec));
				const media_format& mf = format.format;
				if (mf.type == B_MEDIA_RAW_VIDEO) {
					fVideoWidth = mf.u.raw_video.width;
					fVideoHeight = mf.u.raw_video.height;
					fVideoFrameRate = mf.u.raw_video.field_rate;
					fVideoParNum = mf.u.raw_video.pixel_width_aspect;
					fVideoParDen = mf.u.raw_video.pixel_height_aspect;
					fVideoRotation = mf.u.raw_video.orientation;
				} else if (mf.type == B_MEDIA_ENCODED_VIDEO) {
					const media_raw_video_format& rv
						= mf.u.encoded_video.output;
					fVideoWidth = rv.width;
					fVideoHeight = rv.height;
					fVideoFrameRate = rv.field_rate;
					fVideoParNum = rv.pixel_width_aspect;
					fVideoParDen = rv.pixel_height_aspect;
					fVideoRotation = rv.orientation;
				}
			}
		}
		fProbeFile->ReleaseTrack(track);
	}
	if (fAudioTrackCount == 0 && count > 0)
		fAudioTrackCount = 1;

	// Probe only: a kept file leaves a second full pipeline running.
	delete fProbeFile;
	fProbeFile = NULL;
}


status_t
Controller::_SetToRef(const entry_ref& ref)
{
	_StopMidi();
	status_t err = fPlayer.SetTo(&ref);
	if (err != B_OK)
		return err;
	_ProbeTracks(&ref, NULL);
	_ApplyVolume();
	_NotifyState();
	_Notify(MSG_CONTROLLER_DURATION);
	if (fTarget.IsValid()) {
		BMessage msg(MSG_CONTROLLER_VIDEO);
		msg.AddBool("has_video", fHasVideo);
		msg.AddInt32("width", (int32)fVideoWidth);
		msg.AddInt32("height", (int32)fVideoHeight);
		msg.AddInt32("par_num", (int32)fVideoParNum);
		msg.AddInt32("par_den", (int32)fVideoParDen);
		msg.AddInt32("rotation", (int32)fVideoRotation);
		fTarget.SendMessage(&msg);
	}
	return B_OK;
}


status_t
Controller::_SetToUrl(const BUrl& url)
{
	_StopMidi();
	status_t err = fPlayer.SetTo(url);
	if (err != B_OK)
		return err;
	_ProbeTracks(NULL, &url);
	_ApplyVolume();
	_NotifyState();
	_Notify(MSG_CONTROLLER_DURATION);
	return B_OK;
}


status_t
Controller::OpenRef(const entry_ref& ref)
{
	// MIDI goes through the synth, never the media player.
	BString lower(ref.name);
	lower.ToLower();
	if (lower.EndsWith(".mid") || lower.EndsWith(".midi"))
		return OpenMidi(ref);
	{
		BAutolock _(fLock);
		if (fOpening)
			return B_BUSY;
		fOpening = true;
	}
	status_t err = _SetToRef(ref);
	{
		BAutolock _(fLock);
		fOpening = false;
	}
	if (err == B_OK)
		err = fPlayer.Play();
	if (err != B_OK) {
		if (fTarget.IsValid()) {
			BMessage msg(MSG_CONTROLLER_ERROR);
			msg.AddString("message", "Cannot play this file.");
			msg.AddInt32("error", err);
			fTarget.SendMessage(&msg);
		}
	}
	return err;
}


status_t
Controller::OpenUrl(const BUrl& url, const char* name)
{
	(void)name;
	{
		BAutolock _(fLock);
		if (fOpening)
			return B_BUSY;
		fOpening = true;
	}
	status_t err = _SetToUrl(url);
	{
		BAutolock _(fLock);
		fOpening = false;
	}
	if (err == B_OK)
		err = fPlayer.Play();
	if (err != B_OK && fTarget.IsValid()) {
		BMessage msg(MSG_CONTROLLER_ERROR);
		msg.AddString("message", "Cannot open this location.");
		msg.AddInt32("error", err);
		fTarget.SendMessage(&msg);
	}
	return err;
}


status_t
Controller::OpenMidi(const entry_ref& ref)
{
	_StopMidi();
	delete fMidi;
	fMidi = new MidiPlayback();
	if (fMidi == NULL)
		return B_NO_MEMORY;
	status_t err = fMidi->SetTo(ref);
	if (err != B_OK) {
		_StopMidi();
		if (fTarget.IsValid()) {
			BMessage msg(MSG_CONTROLLER_ERROR);
			BString text("Cannot play this MIDI file: ");
			text << strerror(err);
			msg.AddString("message", text.String());
			msg.AddInt32("error", err);
			fTarget.SendMessage(&msg);
		}
		return err;
	}
	fPlayer.Stop();
	_ApplyVolume();
	_NotifyState();
	return fMidi->Play();
}


status_t
Controller::OpenCd(int32 track)
{
	CDMediaSource source;
	status_t err = source.Probe();
	if (err != B_OK) {
		if (fTarget.IsValid()) {
			BMessage msg(MSG_CONTROLLER_ERROR);
			msg.AddString("message",
				"No audio CD found (GStreamer cdda:// unavailable "
				"or no disc).");
			msg.AddInt32("error", err);
			fTarget.SendMessage(&msg);
		}
		return err;
	}

	BUrl url;
	err = source.TrackUrl(track, &url);
	if (err != B_OK) {
		if (fTarget.IsValid()) {
			BMessage msg(MSG_CONTROLLER_ERROR);
			msg.AddString("message", "CD track not available.");
			msg.AddInt32("error", err);
			fTarget.SendMessage(&msg);
		}
		return err;
	}
	return OpenUrl(url, source.Device().String());
}


status_t
Controller::Play()
{
	if (fMidi != NULL)
		return fMidi->Play();
	return fPlayer.Play();
}


status_t
Controller::Pause()
{
	if (fMidi != NULL)
		return fMidi->Pause();
	return fPlayer.Pause();
}


status_t
Controller::Stop()
{
	if (fMidi != NULL)
		return fMidi->Stop();
	return fPlayer.Stop();
}


status_t
Controller::TogglePlaying()
{
	if (fMidi != NULL) {
		if (fMidi->IsPlaying())
			return fMidi->Pause();
		return fMidi->Play();
	}
	if (fPlayer.IsPlaying())
		return fPlayer.Pause();
	return fPlayer.Play();
}


status_t
Controller::SeekTo(bigtime_t position, bool accurate)
{
	if (fMidi != NULL)
		return fMidi->SeekTo(position);
	status_t err = fPlayer.SeekTo(position,
		accurate ? 0 : B_MEDIA_SEEK_KEY_UNIT);
	_NotifyPosition();
	return err;
}


status_t
Controller::SkipForward()
{
	PlaylistItem* item = fPlaylist.Advance(true, true);
	if (item == NULL)
		return B_ERROR;
	return OpenRef(item->Ref());
}


status_t
Controller::SkipBackward()
{
	if (Position() > 3 * 1000000)
		return SeekTo(0);
	PlaylistItem* item = fPlaylist.Advance(false, true);
	if (item == NULL)
		return SeekTo(0);
	return OpenRef(item->Ref());
}


status_t
Controller::Rewind()
{
	return SeekTo(0);
}


status_t
Controller::Forward()
{
	bigtime_t duration = Duration();
	if (duration <= 0)
		return B_ERROR;
	bigtime_t next = Position() + 5 * 1000000;
	if (next >= duration)
		next = duration - 1;
	if (next < 0)
		next = 0;
	return SeekTo(next);
}


status_t
Controller::SeekBy(bigtime_t delta)
{
	bigtime_t duration = Duration();
	bigtime_t next = Position() + delta;
	if (next < 0)
		next = 0;
	if (duration > 0 && next >= duration)
		next = duration - 1;
	return SeekTo(next);
}


// Frame step is a bounded SeekTo: media2 has no frame API, so move by one
// frame period using the probed video rate. No-op without video.
status_t
Controller::StepFrame(bool forward)
{
	if (!fHasVideo || fVideoFrameRate <= 0.0f)
		return B_ERROR;
	bigtime_t frame = (bigtime_t)(1000000.0 / fVideoFrameRate);
	if (frame <= 0)
		frame = 1;
	return SeekBy(forward ? frame : -frame);
}


void
Controller::HandleEOS()
{
	if (fLoop) {
		// Loop repeats the current media from the start.
		if (fMidi != NULL) {
			fMidi->SeekTo(0);
			fMidi->Play();
		} else {
			fPlayer.SeekTo(0);
			fPlayer.Play();
		}
		return;
	}
	if (fPlaylist.CountItems() > 1)
		SkipForward();
	else
		_NotifyState();
}


status_t
Controller::SetVolume(float volume)
{
	if (volume < 0.0f)
		volume = 0.0f;
	if (volume > 1.0f)
		volume = 1.0f;
	fVolume = volume;
	if (fVolume > 0.0f)
		fWasMuted = false;
	_ApplyVolume();
	return B_OK;
}


float
Controller::Volume() const
{
	return fVolume;
}


status_t
Controller::VolumeStep(bool up)
{
	// Match the volume slider granularity (roughly 5% per keyboard step).
	float step = 0.05f;
	float next = fVolume + (up ? step : -step);
	if (next < 0.0f)
		next = 0.0f;
	if (next > 1.0f)
		next = 1.0f;
	if (next > 0.0f)
		fWasMuted = false;
	return SetVolume(next);
}


status_t
Controller::SetMute(bool mute)
{
	fMuted = mute;
	_ApplyVolume();
	return B_OK;
}


void
Controller::SetLoop(bool loop)
{
	fLoop = loop;
}


bigtime_t
Controller::Position() const
{
	if (fMidi != NULL)
		return fMidi->Position();
	return fPlayer.Position();
}


bigtime_t
Controller::Duration() const
{
	if (fMidi != NULL)
		return fMidi->Duration();
	return fPlayer.Duration();
}


BMediaPlayer::player_state
Controller::State() const
{
	if (fMidi != NULL)
		return fMidi->IsPlaying()
			? BMediaPlayer::B_PLAYER_PLAYING
			: BMediaPlayer::B_PLAYER_STOPPED;
	return fPlayer.State();
}


bool
Controller::IsPlaying() const
{
	if (fMidi != NULL)
		return fMidi->IsPlaying();
	return fPlayer.IsPlaying();
}


void
Controller::ItemsChanged()
{
	_Notify(MSG_PLAYLIST_ITEMS_CHANGED);
}


void
Controller::CurrentItemChanged(int32 newIndex, bool play)
{
	PlaylistItem* item = fPlaylist.CurrentItem();
	if (item == NULL)
		return;
	if (fTarget.IsValid()) {
		BMessage msg(MSG_CONTROLLER_ITEM);
		msg.AddInt32("index", newIndex);
		msg.AddString("name", item->Name().String());
		fTarget.SendMessage(&msg);
	}
	if (play)
		OpenRef(item->Ref());
}
