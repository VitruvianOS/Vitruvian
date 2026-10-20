/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_RECORDER_H
#define _MEDIA2_MEDIA_RECORDER_H


#include <media2/MediaClientDefs.h>
#include <media2/MediaFormat.h>


class BFile;
class BMediaInput;


class BMediaRecorder {
public:
	enum notification {
		B_STARTED = 1,
		B_STOPPED,
		B_INPUT_DISCONNECTED
	};

	// Requested output sample format for SetOutputFile(). DEFAULT keeps
	// the encoder's own preference (S16 for WAV).
	enum sample_format {
		B_SAMPLE_FORMAT_DEFAULT = 0,
		B_SAMPLE_FORMAT_S16,
		B_SAMPLE_FORMAT_S24,
		B_SAMPLE_FORMAT_S32,
		B_SAMPLE_FORMAT_F32
	};

	typedef void (*ProcessFunc)(void* cookie, bigtime_t timestamp,
		void* data, size_t size, const media_raw_audio_format& format);
	typedef void (*NotifyFunc)(void* cookie, notification what, ...);

	// Format-carrying hook for any media type: raw audio or raw video
	// buffers arrive with their BMediaFormat.
	typedef void (*BufferFunc)(void* cookie, bigtime_t timestamp,
		void* data, size_t size, const BMediaFormat& format);


	typedef ProcessFunc	RecordBuffer;
	typedef NotifyFunc	Notifier;

								BMediaRecorder(const char* name = "BMediaRecorder",
									media_type type = B_MEDIA_RAW_AUDIO);
	virtual						~BMediaRecorder();

			status_t			InitCheck() const;

			const BMediaFormat&	Format() const;
			status_t			SetFormat(const BMediaFormat& format);

	// Enables the second (audio) input of a B_MEDIA_RAW_VIDEO recorder so
	// one muxed file carries both streams.
			status_t			SetAudioFormat(const BMediaFormat& format);

	// B_MEDIA_RAW_VIDEO recorders: the recorder owns a capture stream from
	// the given audio device (0 follows the system default input) and feeds
	// the audio leg with it. Linked at Start(); without a usable device the
	// file carries video only.
			status_t			SetAudioSource(media_client_id device = 0);

	virtual	status_t			Start(bool force = false);
	virtual	status_t			Stop(bool force = false);
			bool				IsRunning() const;
			bool				IsRecording() const { return IsRunning(); }

			status_t			SetHooks(ProcessFunc recordFunc = NULL,
									NotifyFunc notifyFunc = NULL,
									void* cookie = NULL);
			// Every buffer reaching the recorder, video included, goes to
			// bufferFunc before it is encoded (if an output file is set).
			status_t			SetBufferHooks(BufferFunc bufferFunc = NULL,
									NotifyFunc notifyFunc = NULL,
									void* cookie = NULL);
			void				SetCallbacks(RecordBuffer recordFunc = NULL,
									Notifier notifyFunc = NULL,
									void* cookie = NULL);

protected:
		virtual	void				BufferReceived(void* buffer, size_t size,
									const media_raw_audio_format& format);

public:
			// Capture endpoint for BMediaGraph::Connect(); Start() must have
			// created the recorder's stream before the connection is made.
			BMediaInput*			Input() const;

		// sampleRate/channelCount of 0 keep the device's; sampleFormat
		// DEFAULT keeps the encoder's; quality below 0 keeps the
		// encoder default. Resampling and channel conversion happen
		// inside the encode pipeline.
		status_t			SetOutputFile(const char* path,
									const BMediaFormat& encodedFormat,
									uint32 sampleRate = 0,
									uint32 channelCount = 0,
									sample_format sampleFormat =
										B_SAMPLE_FORMAT_DEFAULT,
									float quality = -1.0f);
		status_t			SetOutputFile(BFile* file,
									const BMediaFormat& encodedFormat,
									uint32 sampleRate = 0,
									uint32 channelCount = 0,
									sample_format sampleFormat =
										B_SAMPLE_FORMAT_DEFAULT,
									float quality = -1.0f);
		// Muxed video+audio output for B_MEDIA_RAW_VIDEO recorders: VP8 and
		// Opus go to webm, H264 and AAC to mp4.
		status_t			SetOutputFile(const char* path,
									const BMediaFormat& encodedVideoFormat,
									const BMediaFormat& encodedAudioFormat);
		void				ClearOutputFile();

		// True when an encoder for 'encoding' (media_codec_type)
		// is present; menus can grey out the rest.
		static	bool		IsEncodingAvailable(int32 encoding);

			size_t				BufferSize() const;

			// Valid for B_MEDIA_RAW_VIDEO recorders.
			BMediaInput*		VideoInput() const;
			BMediaInput*		AudioInput() const;

private:
			class Impl;
			Impl*				fImpl;

	friend class Impl;
};


#endif
