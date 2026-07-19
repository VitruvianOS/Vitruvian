/*
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_RECORDER_H
#define _MEDIA2_MEDIA_RECORDER_H


#include <media2/MediaFormat.h>


class BFile;


class BMediaRecorder {
public:
	enum notification {	// haiku-latest spelling
		B_STARTED = 1,
		B_STOPPED,
		B_INPUT_DISCONNECTED
	};

	// haiku-latest method-pointer names. The buffer argument is `void*`
	// (not const) in legacy — kept that way for source compat.
	typedef void (*ProcessFunc)(void* cookie, bigtime_t timestamp,
		void* data, size_t size, const media_raw_audio_format& format);
	typedef void (*NotifyFunc)(void* cookie, notification what, ...);

	// media2 spellings (aliases — keep both)
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

			// haiku-latest: SetHooks(process, notify, cookie). media2:
			// SetCallbacks is an alias kept for the existing call sites.
			status_t			SetHooks(ProcessFunc recordFunc = NULL,
									NotifyFunc notifyFunc = NULL,
									void* cookie = NULL);
			void				SetCallbacks(RecordBuffer recordFunc = NULL,
									Notifier notifyFunc = NULL,
									void* cookie = NULL);

protected:
	// Legacy override path. Default forwards to the function pointer set via
	// SetHooks / SetCallbacks. Override to consume buffers via subclassing.
	virtual	void				BufferReceived(void* buffer, size_t size,
									const media_raw_audio_format& format);

public:
			// Encode captured PCM to a file. `path` is the destination file
			// (created/overwritten). `encodedFormat` is an encoded BMediaFormat
			// — its `encoding` (media_codec_type) selects the codec/container.
			// Call after SetFormat() and before Start().
			status_t			SetOutputFile(const char* path,
									const BMediaFormat& encodedFormat);
			status_t			SetOutputFile(BFile* file,
									const BMediaFormat& encodedFormat);
			void				ClearOutputFile();

			size_t				BufferSize() const;

private:
			class Impl;
			Impl*				fImpl;

	friend class Impl;
};


#endif // _MEDIA2_MEDIA_RECORDER_H
