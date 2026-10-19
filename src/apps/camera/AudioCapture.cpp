/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "AudioCapture.h"

#include <new>

#include <media2/MediaConnection.h>
#include <media2/MediaFormat.h>
#include <media2/MediaGraph.h>
#include <media2/MediaRecorder.h>


class AudioCapture::_Output : public BMediaOutput {
public:
								_Output()
									:
									BMediaConnection(B_MEDIA_OUTPUT, "audio"),
									BMediaOutput("audio")
	{
		SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));
	}

	virtual						~_Output() {}

protected:
	virtual	status_t			PrepareToConnect(BMediaFormat* format)
	{
		if (format == NULL || !format->IsRawAudio())
			return B_BAD_VALUE;
		return B_OK;
	}

	virtual	status_t			FormatProposal(BMediaFormat* /*format*/)
	{
		return B_OK;
	}
};


static BMediaFormat
_CaptureFormat()
{
	BMediaFormat format;
	format.SetToDefault();
	format.format.type = B_MEDIA_RAW_AUDIO;
	media_raw_audio_format& raw = format.format.u.raw_audio;
	raw.frame_rate = 48000.0f;
	raw.channel_count = 2;
	raw.format = media_raw_audio_format::B_AUDIO_SHORT;
	raw.byte_order = B_MEDIA_HOST_ENDIAN;
	raw.buffer_size = 4096;
	return format;
}


AudioCapture::AudioCapture(const char* name)
	:
	BMediaClient(name != NULL ? name : "Mic",
		(media_client_kinds)(B_MEDIA_PLAYER | B_MEDIA_PHYSICAL_INPUT)),
	fDevice(0),
	fOutput(NULL),
	fRecorder(NULL),
	fSource(),
	fLinked(false),
	fInitErr(B_NO_INIT)
{
	fOutput = new(std::nothrow) _Output();
	if (fOutput == NULL)
		return;
	if (RegisterOutput(fOutput) != B_OK) {
		delete fOutput;
		fOutput = NULL;
		return;
	}
	fOutput->SetFormat(_CaptureFormat());

	fRecorder = new(std::nothrow) BMediaRecorder(name != NULL ? name : "Mic",
		B_MEDIA_RAW_AUDIO);
	if (fRecorder == NULL || fRecorder->InitCheck() != B_OK
		|| fRecorder->SetFormat(_CaptureFormat()) != B_OK) {
		return;
	}

	fInitErr = B_OK;
}


AudioCapture::~AudioCapture()
{
	Stop();
	delete fRecorder;
	if (fOutput != NULL) {
		UnregisterOutput(fOutput);
		delete fOutput;
	}
}


status_t
AudioCapture::InitCheck() const
{
	return fInitErr;
}


status_t
AudioCapture::SetDevice(media_client_id id)
{
	if (IsStarted())
		return B_NOT_ALLOWED;
	fDevice = id;
	return B_OK;
}


BMediaOutput*
AudioCapture::AudioOutput() const
{
	return fOutput;
}


bool
AudioCapture::IsInProcessRouted() const
{
	return true;
}


status_t
AudioCapture::_StartConnections(void* /*backend*/)
{
	return B_OK;
}


/* static */ status_t
AudioCapture::GetDevices(BObjectList<Device, true>* outDevices)
{
	if (outDevices == NULL)
		return B_BAD_VALUE;
	outDevices->MakeEmpty();

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	BObjectList<media_client_id, true> clients;
	status_t status = graph->GetClients(&clients);
	if (status != B_OK)
		return status;

	for (int32 i = 0; i < clients.CountItems(); i++) {
		const media_client_id id = *clients.ItemAt(i);
		BMessage info;
		if (graph->GetClientInfo(id, &info) != B_OK)
			continue;
		bool isSource = false;
		bool isVideo = false;
		info.FindBool("is.source", &isSource);
		info.FindBool("is.video", &isVideo);
		if (!isSource || isVideo)
			continue;

		Device* device = new(std::nothrow) Device();
		if (device == NULL)
			break;
		device->id = id;
		if (info.FindString("name", &device->name) != B_OK
			|| device->name.Length() == 0) {
			info.FindString("node_name", &device->name);
		}
		if (!outDevices->AddItem(device))
			delete device;
	}
	return B_OK;
}


/* static */ void
AudioCapture::_Record(void* cookie, bigtime_t /*timestamp*/, void* data,
	size_t size, const media_raw_audio_format& /*format*/)
{
	AudioCapture* self = (AudioCapture*)cookie;
	if (self->fOutput != NULL && self->fOutput->IsConnected())
		self->fOutput->SendBuffer(data, size);
}


status_t
AudioCapture::Start()
{
	if (IsStarted())
		return B_OK;
	if (fInitErr != B_OK)
		return fInitErr;

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	media_client_id device = fDevice;
	if (device == 0) {
		status_t status = graph->GetDefaultAudioInput(&device);
		if (status != B_OK)
			return status;
	}

	BObjectList<BMediaGraph::ForeignPort, true> ports;
	status_t status = graph->GetForeignPorts((uint32)device, &ports);
	if (status != B_OK)
		return status;

	BMediaGraph::ForeignPort* source = NULL;
	for (int32 i = 0; i < ports.CountItems(); i++) {
		if (ports.ItemAt(i)->direction == "out") {
			source = ports.ItemAt(i);
			break;
		}
	}
	if (source == NULL)
		return B_ENTRY_NOT_FOUND;
	fSource = *source;

	status = BMediaClient::Start();
	if (status != B_OK)
		return status;

	// The recorder's stream must exist before the server can link it.
	fRecorder->SetHooks(&AudioCapture::_Record, NULL, this);
	status = fRecorder->Start();
	if (status == B_OK)
		status = graph->ConnectChannels(fSource.nodeId, fRecorder->Input());
	if (status == B_OK) {
		fLinked = true;
		return B_OK;
	}

	fRecorder->Stop(true);
	fRecorder->SetHooks(NULL, NULL, NULL);
	BMediaClient::Stop();
	return status;
}


status_t
AudioCapture::Stop()
{
	if (!IsStarted())
		return B_OK;

	BMediaGraph* graph = BMediaGraph::Instance();
	if (fLinked && graph != NULL)
		graph->Disconnect(&fSource, fRecorder->Input());
	fLinked = false;

	fRecorder->Stop(true);
	fRecorder->SetHooks(NULL, NULL, NULL);
	return BMediaClient::Stop();
}
