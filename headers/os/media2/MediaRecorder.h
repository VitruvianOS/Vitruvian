/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_RECORDER_H
#define _MEDIA2_MEDIA_RECORDER_H


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


	typedef ProcessFunc	RecordBuffer;
	typedef NotifyFunc	Notifier;

								BMediaRecorder(const char* name = "BMediaRecorder",
									media_type type = B_MEDIA_RAW_AUDIO);
	virtual						~BMediaRecorder();

			status_t			InitCheck() const;

			const BMediaFormat&	Format() const;
			status_t			SetFormat(const BMediaFormat& format);

	virtual	status_t			Start(bool force = false);
	virtual	status_t			Stop(bool force = false);
			bool				IsRunning() const;
			bool				IsRecording() const { return IsRunning(); }

			status_t			SetHooks(ProcessFunc recordFunc = NULL,
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
		void				ClearOutputFile();

		// True when a GStreamer encoder for 'encoding' (media_codec_type)
		// is present; menus can grey out the rest.
		static	bool		IsEncodingAvailable(int32 encoding);

			size_t				BufferSize() const;

private:
			class Impl;
			Impl*				fImpl;

	friend class Impl;
};


#endif
