/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#ifndef _AUDIO_CAPTURE_H
#define _AUDIO_CAPTURE_H


#include <ObjectList.h>
#include <String.h>

#include <media2/MediaClient.h>
#include <media2/MediaGraph.h>


class BMediaRecorder;


// Microphone as an in-process audio source: the capture device is linked
// through the media server and its buffers are forwarded to AudioOutput().
class AudioCapture : public BMediaClient {
public:
	struct Device {
		media_client_id	id;
		BString			name;
	};

								AudioCapture(const char* name = "Mic");
	virtual						~AudioCapture();

			status_t			InitCheck() const;

			// 0 follows the system default input.
			status_t			SetDevice(media_client_id id);

			BMediaOutput*		AudioOutput() const;

	virtual	bool				IsInProcessRouted() const;
	virtual	status_t			Start();
	virtual	status_t			Stop();

	static	status_t			GetDevices(BObjectList<Device, true>* outDevices);

protected:
	virtual	status_t			_StartConnections(void* backend);

private:
	class _Output;

	static	void				_Record(void* cookie, bigtime_t timestamp,
									void* data, size_t size,
									const media_raw_audio_format& format);

			media_client_id		fDevice;
			_Output*			fOutput;
			BMediaRecorder*		fRecorder;
			BMediaGraph::ForeignPort	fSource;
			bool				fLinked;
			status_t			fInitErr;
};


#endif // _AUDIO_CAPTURE_H
