/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <media2/CameraSettings.h>
#include <media2/CameraSource.h>
#include <media2/MediaGraph.h>

#include <new>
#include <string.h>

#include <Autolock.h>
#include <Controllable.h>
#include <Message.h>
#include <ObjectList.h>
#include <ParameterWeb.h>

#include <pipewire/pipewire.h>

#include "PipeWireBackend.h"


using namespace BPrivate::media;


namespace {


class _CameraOutput : public BMediaOutput {
public:
								_CameraOutput(const char* name)
									:
									BMediaConnection(B_MEDIA_OUTPUT, name),
									BMediaOutput(name)
								{}
	virtual						~_CameraOutput() {}

protected:
	virtual	status_t			PrepareToConnect(BMediaFormat* format)
								{
									if (format != NULL && !format->IsRawVideo())
										return B_NOT_SUPPORTED;
									return B_OK;
								}
	virtual	status_t			FormatProposal(BMediaFormat* format)
								{
									if (format != NULL && !format->IsRawVideo())
										return B_NOT_SUPPORTED;
									return B_OK;
								}
};


static const pw_stream_events kCameraStreamEvents = {
	PW_VERSION_STREAM_EVENTS,
	.process = NULL,	// filled below via BCameraSource::_OnProcess
};


} // anonymous namespace


BCameraSource::BCameraSource(const char* name)
	:
	BMediaNode(name != NULL ? name : "BCameraSource",
		B_MEDIA_RAW_VIDEO,
		(media_client_kinds)(B_MEDIA_PHYSICAL_INPUT | B_MEDIA_CONTROLLABLE)),
	fFrameLock("camera frames"),
	fLastFrame(),
	fHaveFrame(false),
	fFrameTarget(),
	fControlLock("camera controls"),
	fBrightness(0.0f),
	fContrast(0.0f),
	fExposureAuto(true),
	fExposure(0.0f),
	fWhiteBalanceAuto(true),
	fWhiteBalance(0.0f),
	fFocusAuto(true),
	fFocus(0.0f),
	fPowerLineFreq(0.0f),
	fMirror(false),
	fDeviceId(0),
	fStableId(""),
	fNodeName(""),
	fLocation(B_CAMERA_LOCATION_UNKNOWN),
	fCaptureStream(NULL),
	fParameterWeb(NULL)
{
	memset(&fLastFormat, 0, sizeof(fLastFormat));
	fLastFormat.color_space = B_RGB32;
	fLastFormat.field_rate = 30.0f;
	fLastFormat.width = 640;
	fLastFormat.height = 480;

	_CameraOutput* output = new(std::nothrow) _CameraOutput("camera-out");
	if (output != NULL) {
		media_raw_video_format raw = fLastFormat;
		BMediaFormat format(raw);
		output->SetAcceptedFormat(format);
		output->SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_VIDEO));
		RegisterOutput(output);
	}

	_BuildParameterWeb();
}


BCameraSource::~BCameraSource()
{
	Stop();
	_StopConnections();

	if (CountOutputs() > 0)
		UnregisterOutput(OutputAt(0));

	delete (BParameterWeb*)fParameterWeb;
	fParameterWeb = NULL;

	_SaveControls();
}


status_t
BCameraSource::SetToDefault()
{
	BCameraSettings settings;
	if (settings.Load() != B_OK)
		return B_ERROR;

	BString defaultId = settings.DefaultCameraId();
	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	BObjectList<media_client_id, true> clients;
	if (graph->GetClients(&clients) != B_OK)
		return B_DEVICE_NOT_FOUND;

	for (int32 i = 0; i < clients.CountItems(); i++) {
		media_client_id id = *clients.ItemAt(i);
		BMessage msg;
		if (graph->GetClientInfo(id, &msg) != B_OK)
			continue;
		bool isVideo = false;
		msg.FindBool("is.video", &isVideo);
		if (!isVideo)
			continue;
		BString stable;
		msg.FindString("stable_id", &stable);
		if (!defaultId.IsEmpty() && stable != defaultId)
			continue;
		return SetToDevice(id);
	}

	if (!defaultId.IsEmpty()) {
		camera_device_info info;
		info.id = 0;
		info.stableId = defaultId;
		info.location = B_CAMERA_LOCATION_UNKNOWN;
		return SetToDevice(info);
	}
	return B_ENTRY_NOT_FOUND;
}


status_t
BCameraSource::SetToDevice(media_client_id id)
{
	if (id == 0)
		return B_BAD_VALUE;

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	BMessage msg;
	if (graph->GetClientInfo(id, &msg) != B_OK)
		return B_ENTRY_NOT_FOUND;

	bool isVideo = false;
	msg.FindBool("is.video", &isVideo);
	if (!isVideo)
		return B_NOT_SUPPORTED;

	camera_device_info info;
	info.id = id;
	BString name;
	msg.FindString("name", &name);
	info.name = name;
	BString stable;
	msg.FindString("stable_id", &stable);
	info.stableId = stable;
	BString node;
	msg.FindString("node_name", &node);
	info.nodeName = node;
	int32 loc = B_CAMERA_LOCATION_UNKNOWN;
	msg.FindInt32("location", &loc);
	info.location = (camera_location)loc;
	info.isDefault = false;
	return SetToDevice(info);
}


status_t
BCameraSource::SetToDevice(const camera_device_info& info)
{
	if (info.stableId.IsEmpty() && info.nodeName.IsEmpty())
		return B_BAD_VALUE;
	if (IsStarted())
		return B_NOT_ALLOWED;

	fDeviceId = info.id;
	fStableId = info.stableId;
	fNodeName = info.nodeName;
	fLocation = info.location;

	BCameraSettings settings;
	settings.Load();
	camera_device_info stored;
	BMessage controls;
	if (settings.GetDevice(fStableId, &stored, &controls) == B_OK) {
		if (fLocation == B_CAMERA_LOCATION_UNKNOWN)
			fLocation = stored.location;
		if (fNodeName.IsEmpty() && !stored.name.IsEmpty())
			fStableId = stored.stableId;

		float value = 0;
		bool flag = false;
		if (controls.FindFloat("brightness", &value) == B_OK)
			fBrightness = value;
		if (controls.FindFloat("contrast", &value) == B_OK)
			fContrast = value;
		if (controls.FindBool("exposure_auto", &flag) == B_OK)
			fExposureAuto = flag;
		if (controls.FindFloat("exposure", &value) == B_OK)
			fExposure = value;
		if (controls.FindBool("white_balance_auto", &flag) == B_OK)
			fWhiteBalanceAuto = flag;
		if (controls.FindFloat("white_balance", &value) == B_OK)
			fWhiteBalance = value;
		if (controls.FindBool("focus_auto", &flag) == B_OK)
			fFocusAuto = flag;
		if (controls.FindFloat("focus", &value) == B_OK)
			fFocus = value;
		if (controls.FindFloat("power_line_freq", &value) == B_OK)
			fPowerLineFreq = value;
		if (controls.FindBool("mirror", &flag) == B_OK)
			fMirror = flag;

		int32 width = 0;
		int32 height = 0;
		float rate = 0;
		if (controls.FindInt32("width", &width) == B_OK
			&& controls.FindInt32("height", &height) == B_OK
			&& width > 0 && height > 0) {
			fLastFormat.width = (uint32)width;
			fLastFormat.height = (uint32)height;
		}
		if (controls.FindFloat("field_rate", &rate) == B_OK && rate > 0)
			fLastFormat.field_rate = rate;
	}

	if (fLastFormat.width == 0)
		fLastFormat.width = 640;
	if (fLastFormat.height == 0)
		fLastFormat.height = 480;
	if (fLastFormat.field_rate <= 0)
		fLastFormat.field_rate = 30.0f;
	if (fLastFormat.color_space == 0)
		fLastFormat.color_space = B_RGB32;

	SetFormat(BMediaFormat(fLastFormat));
	return B_OK;
}


status_t
BCameraSource::SetFrameTarget(const BMessenger& target)
{
	fFrameTarget = target;
	return B_OK;
}


media_raw_video_format
BCameraSource::VideoFormat() const
{
	BAutolock _(const_cast<BLocker&>(fFrameLock));
	return fLastFormat;
}


status_t
BCameraSource::CopyLastFrame(void* buffer, size_t size,
	media_raw_video_format* outFormat) const
{
	if (buffer == NULL)
		return B_BAD_VALUE;

	BAutolock _(const_cast<BLocker&>(fFrameLock));
	if (!fHaveFrame || fLastFrame.empty())
		return B_NO_INIT;
	const size_t copy = size < fLastFrame.size() ? size : fLastFrame.size();
	memcpy(buffer, fLastFrame.data(), copy);
	if (outFormat != NULL)
		*outFormat = fLastFormat;
	return B_OK;
}


status_t
BCameraSource::GetControl(int32 id, float* outValue) const
{
	if (outValue == NULL)
		return B_BAD_VALUE;
	BAutolock _(const_cast<BLocker&>(fControlLock));
	switch (id) {
		case B_CAMERA_PARAM_BRIGHTNESS:         *outValue = fBrightness; return B_OK;
		case B_CAMERA_PARAM_CONTRAST:           *outValue = fContrast; return B_OK;
		case B_CAMERA_PARAM_EXPOSURE_AUTO:      *outValue = fExposureAuto ? 1.0f : 0.0f; return B_OK;
		case B_CAMERA_PARAM_EXPOSURE:           *outValue = fExposure; return B_OK;
		case B_CAMERA_PARAM_WHITE_BALANCE_AUTO: *outValue = fWhiteBalanceAuto ? 1.0f : 0.0f; return B_OK;
		case B_CAMERA_PARAM_WHITE_BALANCE:      *outValue = fWhiteBalance; return B_OK;
		case B_CAMERA_PARAM_FOCUS_AUTO:         *outValue = fFocusAuto ? 1.0f : 0.0f; return B_OK;
		case B_CAMERA_PARAM_FOCUS:              *outValue = fFocus; return B_OK;
		case B_CAMERA_PARAM_POWER_LINE_FREQ:    *outValue = fPowerLineFreq; return B_OK;
		case B_CAMERA_PARAM_MIRROR:             *outValue = fMirror ? 1.0f : 0.0f; return B_OK;
		default:                                return B_BAD_VALUE;
	}
}


status_t
BCameraSource::SetControl(int32 id, float value)
{
	{
		BAutolock _(fControlLock);
		switch (id) {
			case B_CAMERA_PARAM_BRIGHTNESS:         fBrightness = value; break;
			case B_CAMERA_PARAM_CONTRAST:           fContrast = value; break;
			case B_CAMERA_PARAM_EXPOSURE_AUTO:      fExposureAuto = value != 0.0f; break;
			case B_CAMERA_PARAM_EXPOSURE:           fExposure = value; break;
			case B_CAMERA_PARAM_WHITE_BALANCE_AUTO: fWhiteBalanceAuto = value != 0.0f; break;
			case B_CAMERA_PARAM_WHITE_BALANCE:      fWhiteBalance = value; break;
			case B_CAMERA_PARAM_FOCUS_AUTO:         fFocusAuto = value != 0.0f; break;
			case B_CAMERA_PARAM_FOCUS:              fFocus = value; break;
			case B_CAMERA_PARAM_POWER_LINE_FREQ:    fPowerLineFreq = value; break;
			case B_CAMERA_PARAM_MIRROR:             fMirror = value != 0.0f; break;
			default:                                return B_BAD_VALUE;
		}
	}
	_SaveControls();
	return B_OK;
}


status_t
BCameraSource::ResetControls()
{
	{
		BAutolock _(fControlLock);
		fBrightness = 0.0f;
		fContrast = 0.0f;
		fExposureAuto = true;
		fExposure = 0.0f;
		fWhiteBalanceAuto = true;
		fWhiteBalance = 0.0f;
		fFocusAuto = true;
		fFocus = 0.0f;
		fPowerLineFreq = 0.0f;
		fMirror = false;
	}
	_SaveControls();
	return B_OK;
}


status_t
BCameraSource::_StartConnections(void* backendPtr)
{
	PipeWireBackend* backend = (PipeWireBackend*)backendPtr;
	if (backend == NULL)
		return B_BAD_VALUE;
	if (fNodeName.IsEmpty())
		return B_ENTRY_NOT_FOUND;

	BMediaFormat format(VideoFormat());
	if (format.Type() != B_MEDIA_RAW_VIDEO) {
		media_raw_video_format raw;
		memset(&raw, 0, sizeof(raw));
		raw.width = 640;
		raw.height = 480;
		raw.field_rate = 30.0f;
		raw.color_space = B_RGB32;
		format = BMediaFormat(raw);
		SetFormat(format);
	}

	// Capture stream: CPU-mapped frames from the PipeWire camera node.
	BString streamName = Name();
	streamName << "-capture";

	pw_stream_events events = kCameraStreamEvents;
	events.process = &BCameraSource::_OnProcess;

	pw_stream* stream = backend->CreateAndConnectStream(
		streamName.String(), PW_DIRECTION_INPUT, format,
		&events, this, fNodeName.String());
	if (stream == NULL)
		return B_ERROR;

	fCaptureStream = stream;
	return B_OK;
}


void
BCameraSource::_StopConnections()
{
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend != NULL && fCaptureStream != NULL) {
		pw_stream* stream = (pw_stream*)fCaptureStream;
		fCaptureStream = NULL;
		backend->DestroyStream(stream);
	}
}


void
BCameraSource::_OnProcess(void* userdata)
{
	BCameraSource* self = (BCameraSource*)userdata;
	if (self == NULL || self->fCaptureStream == NULL)
		return;

	pw_stream* stream = (pw_stream*)self->fCaptureStream;
	pw_buffer* pb = pw_stream_dequeue_buffer(stream);
	if (pb == NULL)
		return;

	spa_buffer* spaBuf = pb->buffer;
	if (spaBuf == NULL || spaBuf->datas[0].data == NULL) {
		pw_stream_queue_buffer(stream, pb);
		return;
	}

	uint8* data = (uint8*)spaBuf->datas[0].data;
	size_t size = spaBuf->datas[0].chunk != NULL
		? spaBuf->datas[0].chunk->size : spaBuf->datas[0].maxsize;

	media_raw_video_format format = self->VideoFormat();
	if (size > 0)
		self->_OnCaptureFrame(data, size, format);

	pw_stream_queue_buffer(stream, pb);
}


void
BCameraSource::_OnCaptureFrame(void* data, size_t size,
	const media_raw_video_format& format)
{
	if (data == NULL || size == 0)
		return;

	{
		BAutolock _(fFrameLock);
		fLastFormat = format;
		fLastFrame.resize(size);
		if (fLastFrame.size() >= size)
			memcpy(fLastFrame.data(), data, size);
		fHaveFrame = true;
	}

	if (fFrameTarget.IsValid()) {
		BMessage msg(B_CAMERA_FRAME);
		msg.AddInt32("width", (int32)format.width);
		msg.AddInt32("height", (int32)format.height);
		msg.AddInt32("color_space", (int32)format.color_space);
		msg.AddFloat("field_rate", format.field_rate);
		msg.AddInt32("bytes", (int32)size);
		msg.AddString("stable_id", fStableId);
		fFrameTarget.SendMessage(&msg);
	}

	// Fan out to any media2 consumer bound to our video output.
	for (int32 i = 0; i < CountOutputs(); i++) {
		BMediaOutput* output = OutputAt(i);
		if (output == NULL || !output->IsConnected())
			continue;
		BMediaConnection* binding = output->Binding();
		BMediaInput* input = dynamic_cast<BMediaInput*>(binding);
		if (input != NULL)
			input->HandleBuffer(data, size, BMediaFormat(format));
	}
}


void
BCameraSource::_BuildParameterWeb()
{
	BParameterWeb* web = new(std::nothrow) BParameterWeb();
	if (web == NULL)
		return;
	fParameterWeb = web;

	BParameterGroup* group = web->MakeGroup("Camera");
	if (group == NULL)
		return;

	group->MakeContinuousParameter(B_CAMERA_PARAM_BRIGHTNESS, B_MEDIA_RAW_VIDEO,
		"Brightness", B_GENERIC, "", -1.0f, 1.0f, 0.01f);
	group->MakeContinuousParameter(B_CAMERA_PARAM_CONTRAST, B_MEDIA_RAW_VIDEO,
		"Contrast", B_GENERIC, "", -1.0f, 1.0f, 0.01f);
	group->MakeContinuousParameter(B_CAMERA_PARAM_EXPOSURE_AUTO, B_MEDIA_RAW_VIDEO,
		"Exposure auto", B_ENABLE, "", 0.0f, 1.0f, 1.0f);
	group->MakeContinuousParameter(B_CAMERA_PARAM_EXPOSURE, B_MEDIA_RAW_VIDEO,
		"Exposure", B_GENERIC, "ms", 0.0f, 1000.0f, 1.0f);
	group->MakeContinuousParameter(B_CAMERA_PARAM_WHITE_BALANCE_AUTO,
		B_MEDIA_RAW_VIDEO, "White balance auto", B_ENABLE, "", 0.0f, 1.0f, 1.0f);
	group->MakeContinuousParameter(B_CAMERA_PARAM_WHITE_BALANCE, B_MEDIA_RAW_VIDEO,
		"White balance", B_GENERIC, "K", 2500.0f, 8000.0f, 50.0f);
	group->MakeContinuousParameter(B_CAMERA_PARAM_FOCUS_AUTO, B_MEDIA_RAW_VIDEO,
		"Focus auto", B_ENABLE, "", 0.0f, 1.0f, 1.0f);
	group->MakeContinuousParameter(B_CAMERA_PARAM_FOCUS, B_MEDIA_RAW_VIDEO,
		"Focus", B_GENERIC, "", 0.0f, 1.0f, 0.01f);
	group->MakeContinuousParameter(B_CAMERA_PARAM_POWER_LINE_FREQ, B_MEDIA_RAW_VIDEO,
		"Power line frequency", B_GENERIC, "Hz", 0.0f, 60.0f, 50.0f);
	group->MakeContinuousParameter(B_CAMERA_PARAM_MIRROR, B_MEDIA_RAW_VIDEO,
		"Mirror", B_ENABLE, "", 0.0f, 1.0f, 1.0f);
}


void
BCameraSource::_SaveControls()
{
	if (fStableId.IsEmpty())
		return;

	BCameraSettings settings;
	settings.Load();
	camera_device_info info;
	info.id = fDeviceId;
	info.stableId = fStableId;
	info.location = fLocation;

	BMessage controls;
	{
		BAutolock _(fControlLock);
		controls.AddFloat("brightness", fBrightness);
		controls.AddFloat("contrast", fContrast);
		controls.AddBool("exposure_auto", fExposureAuto);
		controls.AddFloat("exposure", fExposure);
		controls.AddBool("white_balance_auto", fWhiteBalanceAuto);
		controls.AddFloat("white_balance", fWhiteBalance);
		controls.AddBool("focus_auto", fFocusAuto);
		controls.AddFloat("focus", fFocus);
		controls.AddFloat("power_line_freq", fPowerLineFreq);
		controls.AddBool("mirror", fMirror);
		controls.AddInt32("width", (int32)fLastFormat.width);
		controls.AddInt32("height", (int32)fLastFormat.height);
		controls.AddFloat("field_rate", fLastFormat.field_rate);
	}

	camera_device_info stored;
	if (settings.GetDevice(fStableId, &stored) == B_OK) {
		info.name = stored.name;
		info.location = stored.location;
	}
	settings.SetDevice(info, &controls);
	settings.Save();
}
