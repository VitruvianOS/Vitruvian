/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <media2/CameraSettings.h>
#include <media2/CameraSource.h>
#include <media2/MediaGraph.h>

#include <new>
#include <algorithm>
#include <stdio.h>
#include <string.h>

#include <Autolock.h>
#include <Controllable.h>
#include <Message.h>
#include <ObjectList.h>
#include <ParameterWeb.h>

#include <linux/v4l2-controls.h>
#include <pipewire/pipewire.h>
#include <pipewire/link.h>
#include <spa/param/props.h>
#include <spa/param/video/format-utils.h>

#include <media2/MediaDecoder.h>

#include "FormatConversion.h"
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
	.param_changed = NULL,	// filled below via BCameraSource::_OnParamChanged
	.process = NULL,	// filled below via BCameraSource::_OnProcess
};


// v4l2 SPA plugin maps these to SPA_PROP_* or SPA_PROP_START_CUSTOM+CID.
static uint32
_CameraSpaProp(int32 id)
{
	switch (id) {
		case B_CAMERA_PARAM_BRIGHTNESS:         return SPA_PROP_brightness;
		case B_CAMERA_PARAM_CONTRAST:           return SPA_PROP_contrast;
		case B_CAMERA_PARAM_EXPOSURE:           return SPA_PROP_exposure;
		case B_CAMERA_PARAM_EXPOSURE_AUTO:
			return SPA_PROP_START_CUSTOM + V4L2_CID_EXPOSURE_AUTO;
		case B_CAMERA_PARAM_WHITE_BALANCE:
			return SPA_PROP_START_CUSTOM + V4L2_CID_WHITE_BALANCE_TEMPERATURE;
		case B_CAMERA_PARAM_WHITE_BALANCE_AUTO:
			return SPA_PROP_START_CUSTOM + V4L2_CID_AUTO_WHITE_BALANCE;
		case B_CAMERA_PARAM_FOCUS:
			return SPA_PROP_START_CUSTOM + V4L2_CID_FOCUS_ABSOLUTE;
		case B_CAMERA_PARAM_FOCUS_AUTO:
			return SPA_PROP_START_CUSTOM + V4L2_CID_FOCUS_AUTO;
		case B_CAMERA_PARAM_POWER_LINE_FREQ:
			return SPA_PROP_START_CUSTOM + V4L2_CID_POWER_LINE_FREQUENCY;
		default:
			return 0;
	}
}


// Many webcams only expose absolute exposure, not the legacy CID.
static uint32
_CameraSpaPropAlt(int32 id)
{
	if (id == B_CAMERA_PARAM_EXPOSURE)
		return SPA_PROP_START_CUSTOM + V4L2_CID_EXPOSURE_ABSOLUTE;
	return 0;
}


// Exposure is a menu, not a bool: 1 is manual, auto is 3 on most UVC
// cameras and 0 on the rest.
static int32
_ExposureAutoToV4L2(bool autoExposure, uint32 menuMask)
{
	if (!autoExposure)
		return V4L2_EXPOSURE_MANUAL;
	if ((menuMask & (1u << V4L2_EXPOSURE_APERTURE_PRIORITY)) != 0
		|| (menuMask & (1u << V4L2_EXPOSURE_AUTO)) == 0)
		return V4L2_EXPOSURE_APERTURE_PRIORITY;
	return V4L2_EXPOSURE_AUTO;
}


// V4L2 power-line ids: 0 disabled, 1 = 50Hz, 2 = 60Hz.
static int32
_PowerLineToV4L2(float hz)
{
	if (hz >= 55.0f)
		return 2;
	if (hz >= 45.0f)
		return 1;
	return 0;
}


static int32
_ToDeviceUnits(bool isBool, int32 min, int32 max, float ui, float uiMin,
	float uiMax)
{
	if (isBool)
		return ui != 0.0f ? 1 : 0;
	if (uiMax <= uiMin || max <= min)
		return min;
	float t = (ui - uiMin) / (uiMax - uiMin);
	if (t < 0.0f)
		t = 0.0f;
	if (t > 1.0f)
		t = 1.0f;
	return min + (int32)(t * (float)(max - min) + 0.5f);
}


static float
_FromDeviceUnits(bool isBool, int32 min, int32 max, int32 value, float uiMin,
	float uiMax)
{
	if (isBool)
		return value != 0 ? 1.0f : 0.0f;
	if (max <= min)
		return uiMin;
	float t = (float)(value - min) / (float)(max - min);
	return uiMin + t * (uiMax - uiMin);
}


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
	fCaptureLink(NULL),
	fParameterWeb(NULL),
	fControllable(NULL),
	fMjpeg(NULL)
{
	memset(&fLastFormat, 0, sizeof(fLastFormat));
	fLastFormat.color_space = B_YCbCr422;
	fLastFormat.field_rate = 30.0f;
	fLastFormat.width = 640;
	fLastFormat.height = 480;

	for (int32 i = 0; i < 12; i++)
		fHaveStored[i] = false;

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


namespace {

struct MjpegState {
	BMediaBufferDecoder		decoder;
	std::vector<uint8>		frame;
	media_raw_video_format	format;
};

}


BCameraSource::~BCameraSource()
{
	Stop();
	_StopConnections();

	if (CountOutputs() > 0)
		UnregisterOutput(OutputAt(0));

	delete fControllable;
	fControllable = NULL;
	fParameterWeb = NULL;

	_SaveControls();
}


status_t
BCameraSource::SetToDefault()
{
	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	media_client_id defaultId = 0;
	if (graph->GetDefaultCamera(&defaultId) != B_OK || defaultId == 0)
		return B_ENTRY_NOT_FOUND;

	return SetToDevice(defaultId);
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
		if (controls.FindFloat("brightness", &value) == B_OK) {
			fBrightness = value;
			fHaveStored[B_CAMERA_PARAM_BRIGHTNESS] = true;
		}
		if (controls.FindFloat("contrast", &value) == B_OK) {
			fContrast = value;
			fHaveStored[B_CAMERA_PARAM_CONTRAST] = true;
		}
		if (controls.FindBool("exposure_auto", &flag) == B_OK) {
			fExposureAuto = flag;
			fHaveStored[B_CAMERA_PARAM_EXPOSURE_AUTO] = true;
		}
		if (controls.FindFloat("exposure", &value) == B_OK) {
			fExposure = value;
			fHaveStored[B_CAMERA_PARAM_EXPOSURE] = true;
		}
		if (controls.FindBool("white_balance_auto", &flag) == B_OK) {
			fWhiteBalanceAuto = flag;
			fHaveStored[B_CAMERA_PARAM_WHITE_BALANCE_AUTO] = true;
		}
		if (controls.FindFloat("white_balance", &value) == B_OK) {
			fWhiteBalance = value;
			fHaveStored[B_CAMERA_PARAM_WHITE_BALANCE] = true;
		}
		if (controls.FindBool("focus_auto", &flag) == B_OK) {
			fFocusAuto = flag;
			fHaveStored[B_CAMERA_PARAM_FOCUS_AUTO] = true;
		}
		if (controls.FindFloat("focus", &value) == B_OK) {
			fFocus = value;
			fHaveStored[B_CAMERA_PARAM_FOCUS] = true;
		}
		if (controls.FindFloat("power_line_freq", &value) == B_OK) {
			fPowerLineFreq = value;
			fHaveStored[B_CAMERA_PARAM_POWER_LINE_FREQ] = true;
		}
		if (controls.FindBool("mirror", &flag) == B_OK) {
			fMirror = flag;
			fHaveStored[B_CAMERA_PARAM_MIRROR] = true;
		}

		int32 width = 0;
		int32 height = 0;
		float rate = 0;
		// Stored width/height/rate were negotiated, not chosen: keep the
		// 640x480 at 30 default so the lowest mode never sticks.
		(void)width; (void)height; (void)rate;
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
	_SeedOutputFormat();

	// See which controls the device exposes; stored values still win.
	_QueryDeviceControls();
	_AdoptDeviceValues();
	// The controllable owns the old web and swaps in the new one.
	_BuildParameterWeb();
	return B_OK;
}


bool
BCameraSource::IsInProcessRouted() const
{
	return true;
}


status_t
BCameraSource::SetMode(uint32 width, uint32 height, float frameRate)
{
	if (width == 0 || height == 0 || frameRate <= 0.0f)
		return B_BAD_VALUE;
	if (IsStarted())
		return B_NOT_ALLOWED;

	{
		BAutolock _(fFrameLock);
		fLastFormat.width = width;
		fLastFormat.height = height;
		fLastFormat.field_rate = frameRate;
		fHaveFrame = false;
	}
	SetFormat(BMediaFormat(VideoFormat()));
	_SeedOutputFormat();
	return B_OK;
}


// In-process consumers always get tightly packed RGB32.
void
BCameraSource::_SeedOutputFormat()
{
	BMediaOutput* output = OutputAt(0);
	if (output == NULL)
		return;

	const media_raw_video_format current = VideoFormat();
	BMediaFormat format;
	format.SetToDefault();
	format.format.type = B_MEDIA_RAW_VIDEO;
	media_raw_video_format& raw = format.format.u.raw_video;
	raw.field_rate = current.field_rate;
	raw.width = current.width;
	raw.height = current.height;
	raw.first_active = 0;
	raw.last_active = current.height > 0 ? current.height - 1 : 0;
	raw.color_space = B_RGB32;
	raw.pixel_width_aspect = 1;
	raw.pixel_height_aspect = 1;
	output->SetFormat(format);
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
BCameraSource::ControlSupported(int32 id) const
{
	// Preview mirror is applied in software, not by the device.
	if (id == B_CAMERA_PARAM_MIRROR)
		return B_OK;
	if (_CameraSpaProp(id) == 0 && _CameraSpaPropAlt(id) == 0)
		return B_UNSUPPORTED;

	BAutolock _(const_cast<BLocker&>(fDeviceControlsLock));
	std::map<int32, device_control>::const_iterator it
		= fDeviceControls.find(id);
	if (it == fDeviceControls.end() || !it->second.present)
		return B_UNSUPPORTED;
	return B_OK;
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
	// Push to the device; controls it does not expose are skipped.
	_ApplyControlsToDevice();
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

	// Static: the stream keeps this pointer for its whole lifetime.
	static pw_stream_events events;
	events = kCameraStreamEvents;
	events.process = &BCameraSource::_OnProcess;
	events.param_changed = [](void* data, uint32 id, const spa_pod* param) {
		BCameraSource::_OnParamChanged(data, id, param);
	};

	pw_stream* stream = backend->CreateAndConnectStream(
		streamName.String(), PW_DIRECTION_INPUT, format,
		&events, this, fNodeName.String(), false, false, true);
	if (stream == NULL)
		return B_ERROR;

	fCaptureStream = stream;
	_LinkCapture(backend);

	// Device may only now be enumerable; apply stored controls on start.
	_QueryDeviceControls();
	_AdoptDeviceValues();
	_ApplyControlsToDevice();
	return B_OK;
}


// The app routes its own stream: link camera out ports to our input ports.
void
BCameraSource::_LinkCapture(void* backendPtr)
{
	PipeWireBackend* backend = (PipeWireBackend*)backendPtr;
	pw_stream* stream = (pw_stream*)fCaptureStream;
	const uint32 camNode = backend->VideoNodeIdByName(fNodeName.String());
	if (camNode == 0) {
		fprintf(stderr, "Camera: node %s not found\n", fNodeName.String());
		return;
	}

	const bigtime_t deadline = system_time() + 2000000;
	uint32 inNode = SPA_ID_INVALID;
	while (system_time() < deadline) {
		backend->Lock();
		backend->RoundtripLocked(500000);
		inNode = pw_stream_get_node_id(stream);
		backend->Unlock();
		if (inNode != SPA_ID_INVALID)
			break;
		snooze(20000);
	}
	if (inNode == SPA_ID_INVALID) {
		fprintf(stderr, "Camera: stream has no node id\n");
		return;
	}

	std::vector<uint32> inIds;
	if (backend->ResolveNodePorts(inNode, PW_DIRECTION_INPUT, inIds,
			std::max<bigtime_t>(deadline - system_time(), 200000)) != B_OK
		|| inIds.empty()) {
		fprintf(stderr, "Camera: stream has no input port\n");
		return;
	}
	std::vector<PipeWireBackend::ForeignPortInfo> ports
		= backend->ForeignPorts(camNode);
	uint32 outPort = 0;
	for (size_t i = 0; i < ports.size(); i++) {
		if (ports[i].direction == "out") {
			outPort = ports[i].id;
			break;
		}
	}
	if (outPort == 0) {
		fprintf(stderr, "Camera: no output port on node %u\n",
			(unsigned)camNode);
		return;
	}

	pw_core* core = backend->GetCore();
	pw_properties* props = pw_properties_new(NULL, NULL);
	if (core == NULL || props == NULL)
		return;
	pw_properties_setf(props, PW_KEY_LINK_OUTPUT_NODE, "%u", camNode);
	pw_properties_setf(props, PW_KEY_LINK_OUTPUT_PORT, "%u", outPort);
	pw_properties_setf(props, PW_KEY_LINK_INPUT_NODE, "%u", inNode);
	pw_properties_setf(props, PW_KEY_LINK_INPUT_PORT, "%u", inIds[0]);
	backend->Lock();
	fCaptureLink = pw_core_create_object(core, "link-factory",
		PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &props->dict, 0);
	pw_properties_free(props);
	backend->RoundtripLocked();
	backend->Unlock();
	if (fCaptureLink == NULL)
		fprintf(stderr, "Camera: link creation failed\n");
}


void
BCameraSource::_StopConnections()
{
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend != NULL && fCaptureLink != NULL) {
		pw_proxy* link = (pw_proxy*)fCaptureLink;
		fCaptureLink = NULL;
		backend->Lock();
		pw_proxy_destroy(link);
		backend->Unlock();
	}
	if (backend != NULL && fCaptureStream != NULL) {
		pw_stream* stream = (pw_stream*)fCaptureStream;
		fCaptureStream = NULL;
		backend->DestroyStream(stream);
	}
	delete (MjpegState*)fMjpeg;
	fMjpeg = NULL;
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

	if (self->fMjpeg != NULL) {
		MjpegState* mjpeg = (MjpegState*)self->fMjpeg;
		if (size > 0) {
			size_t outSize = mjpeg->frame.size();
			if (mjpeg->decoder.DecodeBuffer(data, size, mjpeg->frame.data(),
					&outSize) == B_OK && outSize > 0) {
				self->_OnCaptureFrame(mjpeg->frame.data(), outSize,
					mjpeg->format);
			}
		}
		pw_stream_queue_buffer(stream, pb);
		return;
	}

	media_raw_video_format format = self->VideoFormat();
	const uint32 stride = spaBuf->datas[0].chunk != NULL
		? spaBuf->datas[0].chunk->stride : 0;
	self->_DiagFrame(data, size, stride, format);
	// Rows may be padded; repack to tight rows for the converters.
	uint32 bpp = format.color_space == B_YCbCr422 ? 2
		: (format.color_space == B_RGB32 ? 4 : 0);
	const size_t tight = (size_t)format.width * bpp;
	if (bpp != 0 && stride > tight && format.height > 0
		&& size >= (size_t)stride * format.height) {
		std::vector<uint8> packed(tight * format.height);
		for (uint32 y = 0; y < format.height; y++)
			memcpy(packed.data() + y * tight, data + (size_t)y * stride,
				tight);
		self->_OnCaptureFrame(packed.data(), packed.size(), format);
	} else if (size > 0)
		self->_OnCaptureFrame(data, size, format);

	pw_stream_queue_buffer(stream, pb);
}


// Once per second: what arrived and in which negotiated format.
void
BCameraSource::_DiagFrame(const uint8* data, size_t size, uint32 stride,
	const media_raw_video_format& format)
{
	fDiagFrames++;
	const bigtime_t now = system_time();
	if (now - fDiagLast < 1000000)
		return;
	fDiagLast = now;
	uint32 lo = 255, hi = 0;
	for (size_t i = 0; i < 64 && i < size; i++) {
		if (data[i] < lo)
			lo = data[i];
		if (data[i] > hi)
			hi = data[i];
	}
	fprintf(stderr, "Camera: frames=%u chunk=%zu cs=0x%x %ux%u stride=%u"
		" first64 min=%u max=%u\n", (unsigned)fDiagFrames, size,
		(unsigned)format.color_space, (unsigned)format.width,
		(unsigned)format.height, (unsigned)stride, lo, hi);
	fDiagFrames = 0;
}


void
BCameraSource::_OnParamChanged(void* data, uint32 id, const void* paramData)
{
	const spa_pod* param = (const spa_pod*)paramData;
	BCameraSource* self = (BCameraSource*)data;
	if (self == NULL || id != SPA_PARAM_Format || param == NULL)
		return;

	uint32 mediaType = 0, mediaSubtype = 0;
	if (spa_format_parse(param, &mediaType, &mediaSubtype) < 0)
		return;

	if (mediaSubtype == SPA_MEDIA_SUBTYPE_mjpg) {
		spa_video_info_mjpg mjpg;
		memset(&mjpg, 0, sizeof(mjpg));
		if (spa_format_video_mjpg_parse(param, &mjpg) < 0
			|| mjpg.size.width == 0 || mjpg.size.height == 0) {
			return;
		}

		BMediaFormat in;
		in.format.type = B_MEDIA_ENCODED_VIDEO;
		media_encoded_video_format& enc = in.format.u.encoded_video;
		memset(&enc, 0, sizeof(enc));
		enc.encoding = B_CODEC_TYPE_MJPEG;
		enc.output.width = mjpg.size.width;
		enc.output.height = mjpg.size.height;
		enc.output.color_space = B_RGB32;
		enc.output.field_rate = mjpg.framerate.denom != 0
			? (float)mjpg.framerate.num / (float)mjpg.framerate.denom : 0.0f;

		MjpegState* state = new(std::nothrow) MjpegState;
		if (state == NULL)
			return;
		if (state->decoder.SetTo(in) != B_OK) {
			fprintf(stderr, "Camera: MJPEG decoder unavailable\n");
			delete state;
			return;
		}
		state->format = enc.output;
		state->format.pixel_width_aspect = 1;
		state->format.pixel_height_aspect = 1;
		state->format.first_active = 0;
		state->format.last_active = enc.output.height - 1;
		state->frame.resize((size_t)enc.output.width * enc.output.height * 4);

		delete (MjpegState*)self->fMjpeg;
		self->fMjpeg = state;
		{
			BAutolock _(self->fFrameLock);
			self->fLastFormat = state->format;
		}
		self->SetFormat(BMediaFormat(state->format));
		self->_SeedOutputFormat();
		return;
	}

	delete (MjpegState*)self->fMjpeg;
	self->fMjpeg = NULL;

	spa_video_info_raw info;
	memset(&info, 0, sizeof(info));
	if (spa_format_video_raw_parse(param, &info) < 0)
		return;

	BMediaFormat format;
	ExtractFromSPAVideo(info, &format);
	const media_raw_video_format& rv = format.format.u.raw_video;
	if (rv.width == 0 || rv.height == 0)
		return;

	{
		BAutolock _(self->fFrameLock);
		self->fLastFormat = rv;
	}
	self->SetFormat(format);
	self->_SeedOutputFormat();
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
	std::vector<uint8> rgb;
	const void* out = data;
	size_t outSize = size;
	media_raw_video_format outFormat = format;
	for (int32 i = 0; i < CountOutputs(); i++) {
		BMediaOutput* output = OutputAt(i);
		if (output == NULL || !output->IsConnected())
			continue;
		if (dynamic_cast<BMediaInput*>(output->Binding()) == NULL)
			continue;

		if (rgb.empty() && format.color_space != B_RGB32) {
			rgb.resize((size_t)format.width * format.height * 4);
			const size_t pixels = (size_t)format.width * format.height;
			bool converted = false;
			if (format.color_space == B_YCbCr422 && size >= pixels * 2) {
				converted = ConvertYUY2ToRGB32(
					(const uint8*)data, format.width, format.height,
					rgb.data(), format.width * 4);
			} else if (format.color_space == B_YUV420
					&& size >= pixels * 3 / 2) {
				converted = ConvertI420ToRGB32(
					(const uint8*)data, format.width, format.height,
					rgb.data(), format.width * 4);
			}
			if (!converted)
				return;
			out = rgb.data();
			outSize = rgb.size();
			outFormat.color_space = B_RGB32;
		}
		output->_SendBuffer(const_cast<void*>(out), outSize,
			BMediaFormat(outFormat));
	}
}


void
BCameraSource::_BuildParameterWeb()
{
	BParameterWeb* web = new(std::nothrow) BParameterWeb();
	if (web == NULL)
		return;

	BParameterGroup* group = web->MakeGroup("Camera");
	if (group == NULL) {
		delete web;
		return;
	}

	// Skip device controls the hardware does not expose.
	auto supported = [this](int32 id) {
		return ControlSupported(id) == B_OK;
	};

	if (supported(B_CAMERA_PARAM_BRIGHTNESS))
		group->MakeContinuousParameter(B_CAMERA_PARAM_BRIGHTNESS,
			B_MEDIA_RAW_VIDEO, "Brightness", B_GENERIC, "", 0.0f, 1.0f, 0.01f);
	if (supported(B_CAMERA_PARAM_CONTRAST))
		group->MakeContinuousParameter(B_CAMERA_PARAM_CONTRAST,
			B_MEDIA_RAW_VIDEO, "Contrast", B_GENERIC, "", 0.0f, 1.0f, 0.01f);
	if (supported(B_CAMERA_PARAM_EXPOSURE_AUTO))
		group->MakeContinuousParameter(B_CAMERA_PARAM_EXPOSURE_AUTO,
			B_MEDIA_RAW_VIDEO, "Exposure auto", B_ENABLE, "", 0.0f, 1.0f, 1.0f);
	if (supported(B_CAMERA_PARAM_EXPOSURE))
		group->MakeContinuousParameter(B_CAMERA_PARAM_EXPOSURE,
			B_MEDIA_RAW_VIDEO, "Exposure", B_GENERIC, "", 0.0f, 100.0f, 1.0f);
	if (supported(B_CAMERA_PARAM_WHITE_BALANCE_AUTO))
		group->MakeContinuousParameter(B_CAMERA_PARAM_WHITE_BALANCE_AUTO,
			B_MEDIA_RAW_VIDEO, "White balance auto", B_ENABLE, "", 0.0f, 1.0f, 1.0f);
	if (supported(B_CAMERA_PARAM_WHITE_BALANCE))
		group->MakeContinuousParameter(B_CAMERA_PARAM_WHITE_BALANCE,
			B_MEDIA_RAW_VIDEO, "White balance", B_GENERIC, "K", 2500.0f,
			8000.0f, 50.0f);
	if (supported(B_CAMERA_PARAM_FOCUS_AUTO))
		group->MakeContinuousParameter(B_CAMERA_PARAM_FOCUS_AUTO,
			B_MEDIA_RAW_VIDEO, "Focus auto", B_ENABLE, "", 0.0f, 1.0f, 1.0f);
	if (supported(B_CAMERA_PARAM_FOCUS))
		group->MakeContinuousParameter(B_CAMERA_PARAM_FOCUS,
			B_MEDIA_RAW_VIDEO, "Focus", B_GENERIC, "", 0.0f, 1.0f, 0.01f);
	if (supported(B_CAMERA_PARAM_POWER_LINE_FREQ))
		group->MakeContinuousParameter(B_CAMERA_PARAM_POWER_LINE_FREQ,
			B_MEDIA_RAW_VIDEO, "Power line frequency", B_GENERIC, "Hz",
			0.0f, 60.0f, 50.0f);
	group->MakeContinuousParameter(B_CAMERA_PARAM_MIRROR, B_MEDIA_RAW_VIDEO,
		"Mirror", B_ENABLE, "", 0.0f, 1.0f, 1.0f);

	// Same web behind the Control panel; the hook feeds SetControl().
	if (fControllable == NULL)
		fControllable = new(std::nothrow) BControllable();
	if (fControllable == NULL) {
		delete web;
		return;
	}
	fParameterWeb = web;
	fControllable->SetChangeHook(&BCameraSource::_ControlChangeHook, this);
	fControllable->SetParameterWeb(web);
	SetControllable(fControllable);
}


/* static */ void
BCameraSource::_ControlChangeHook(int32 id, bigtime_t /*when*/,
	const void* value, size_t size, void* cookie)
{
	BCameraSource* self = (BCameraSource*)cookie;
	if (self == NULL || value == NULL || size < sizeof(float))
		return;
	self->SetControl(id, *(const float*)value);
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


status_t
BCameraSource::GetModes(std::vector<camera_mode>* outModes) const
{
	if (outModes == NULL)
		return B_BAD_VALUE;
	outModes->clear();
	if (fNodeName.IsEmpty())
		return B_NO_INIT;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	std::vector<PipeWireBackend::NodeVideoMode> modes;
	status_t status = backend->QueryNodeVideoModes(fNodeName.String(), modes);
	if (status != B_OK)
		return status;
	for (size_t i = 0; i < modes.size(); i++) {
		camera_mode mode = { modes[i].width, modes[i].height,
			modes[i].frameRate > 0 ? modes[i].frameRate : 30.0f };
		outModes->push_back(mode);
	}
	return B_OK;
}


void
BCameraSource::_QueryDeviceControls()
{
	{
		BAutolock _(fDeviceControlsLock);
		fDeviceControls.clear();
	}
	if (fNodeName.IsEmpty())
		return;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return;

	std::vector<PipeWireBackend::NodeControlInfo> info;
	if (backend->QueryNodeControls(fNodeName.String(), info) != B_OK)
		return;

	static const int32 kParams[] = {
		B_CAMERA_PARAM_BRIGHTNESS,
		B_CAMERA_PARAM_CONTRAST,
		B_CAMERA_PARAM_EXPOSURE_AUTO,
		B_CAMERA_PARAM_EXPOSURE,
		B_CAMERA_PARAM_WHITE_BALANCE_AUTO,
		B_CAMERA_PARAM_WHITE_BALANCE,
		B_CAMERA_PARAM_FOCUS_AUTO,
		B_CAMERA_PARAM_FOCUS,
		B_CAMERA_PARAM_POWER_LINE_FREQ
	};

	BAutolock _(fDeviceControlsLock);
	for (uint32 i = 0; i < sizeof(kParams) / sizeof(kParams[0]); i++) {
		const int32 id = kParams[i];
		const uint32 want[] = {
			_CameraSpaProp(id), _CameraSpaPropAlt(id)
		};

		device_control dc;
		dc.propId = 0;
		dc.present = false;
		dc.isBool = false;
		dc.min = 0;
		dc.max = 0;
		dc.step = 1;
		dc.value = 0;
		dc.menuMask = 0;

		for (uint32 w = 0; w < 2 && !dc.present; w++) {
			if (want[w] == 0)
				continue;
			for (size_t n = 0; n < info.size(); n++) {
				if (info[n].propId != want[w] || !info[n].present)
					continue;
				dc.propId = want[w];
				dc.present = true;
				dc.isBool = info[n].isBool;
				dc.min = info[n].min;
				dc.max = info[n].max;
				dc.step = info[n].step != 0 ? info[n].step : 1;
				dc.value = info[n].value;
				dc.menuMask = info[n].menuMask;
				break;
			}
		}
		fDeviceControls[id] = dc;
	}
}


void
BCameraSource::_AdoptDeviceValues()
{
	// Stored settings win; otherwise take what the device reports.
	BAutolock _(fDeviceControlsLock);
	BAutolock __(fControlLock);

	auto adopt = [this](int32 id, float uiMin, float uiMax, float* out) {
		if (fHaveStored[id])
			return;
		std::map<int32, device_control>::const_iterator it
			= fDeviceControls.find(id);
		if (it == fDeviceControls.end() || !it->second.present)
			return;
		*out = _FromDeviceUnits(it->second.isBool, it->second.min,
			it->second.max, it->second.value, uiMin, uiMax);
	};

	adopt(B_CAMERA_PARAM_BRIGHTNESS, 0.0f, 1.0f, &fBrightness);
	adopt(B_CAMERA_PARAM_CONTRAST, 0.0f, 1.0f, &fContrast);
	if (!fHaveStored[B_CAMERA_PARAM_EXPOSURE_AUTO]) {
		std::map<int32, device_control>::const_iterator it
			= fDeviceControls.find(B_CAMERA_PARAM_EXPOSURE_AUTO);
		if (it != fDeviceControls.end() && it->second.present)
			fExposureAuto = it->second.value != V4L2_EXPOSURE_MANUAL;
	}
	adopt(B_CAMERA_PARAM_EXPOSURE, 50.0f, 100.0f, &fExposure);
	if (!fHaveStored[B_CAMERA_PARAM_WHITE_BALANCE_AUTO]) {
		std::map<int32, device_control>::const_iterator it
			= fDeviceControls.find(B_CAMERA_PARAM_WHITE_BALANCE_AUTO);
		if (it != fDeviceControls.end() && it->second.present)
			fWhiteBalanceAuto = it->second.value != 0;
	}
	adopt(B_CAMERA_PARAM_WHITE_BALANCE, 2500.0f, 8000.0f, &fWhiteBalance);
	if (!fHaveStored[B_CAMERA_PARAM_FOCUS_AUTO]) {
		std::map<int32, device_control>::const_iterator it
			= fDeviceControls.find(B_CAMERA_PARAM_FOCUS_AUTO);
		if (it != fDeviceControls.end() && it->second.present)
			fFocusAuto = it->second.value != 0;
	}
	adopt(B_CAMERA_PARAM_FOCUS, 0.0f, 1.0f, &fFocus);
	if (!fHaveStored[B_CAMERA_PARAM_POWER_LINE_FREQ]) {
		std::map<int32, device_control>::const_iterator it
			= fDeviceControls.find(B_CAMERA_PARAM_POWER_LINE_FREQ);
		if (it != fDeviceControls.end() && it->second.present) {
			// Reverse of _PowerLineToV4L2.
			if (it->second.value >= 2)
				fPowerLineFreq = 60.0f;
			else if (it->second.value >= 1)
				fPowerLineFreq = 50.0f;
			else
				fPowerLineFreq = 0.0f;
		}
	}
}


void
BCameraSource::_ApplyControlsToDevice()
{
	if (fNodeName.IsEmpty())
		return;
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return;

	std::vector<PipeWireBackend::NodeControlInfo> props;
	{
		BAutolock _(fDeviceControlsLock);
		BAutolock __(fControlLock);

		for (std::map<int32, device_control>::const_iterator it
				= fDeviceControls.begin();
				it != fDeviceControls.end(); ++it) {
			if (!it->second.present || it->second.propId == 0)
				continue;
			const int32 id = it->first;
			const device_control& dc = it->second;

			PipeWireBackend::NodeControlInfo p;
			p.propId = dc.propId;
			p.isBool = dc.isBool;
			p.min = dc.min;
			p.max = dc.max;
			p.step = dc.step;
			p.menuMask = 0;
			p.present = true;

			switch (id) {
				case B_CAMERA_PARAM_BRIGHTNESS:
					p.value = _ToDeviceUnits(dc.isBool, dc.min, dc.max,
						fBrightness, 0.0f, 1.0f);
					break;
				case B_CAMERA_PARAM_CONTRAST:
					p.value = _ToDeviceUnits(dc.isBool, dc.min, dc.max,
						fContrast, 0.0f, 1.0f);
					break;
				case B_CAMERA_PARAM_EXPOSURE_AUTO:
					p.value = _ExposureAutoToV4L2(fExposureAuto, dc.menuMask);
					break;
				case B_CAMERA_PARAM_EXPOSURE:
					if (fExposureAuto)
						continue;
					p.value = _ToDeviceUnits(dc.isBool, dc.min, dc.max,
						fExposure, 50.0f, 100.0f);
					break;
				case B_CAMERA_PARAM_WHITE_BALANCE_AUTO:
					p.value = fWhiteBalanceAuto ? 1 : 0;
					break;
				case B_CAMERA_PARAM_WHITE_BALANCE:
					if (fWhiteBalanceAuto)
						continue;
					p.value = _ToDeviceUnits(dc.isBool, dc.min, dc.max,
						fWhiteBalance, 2500.0f, 8000.0f);
					break;
				case B_CAMERA_PARAM_FOCUS_AUTO:
					p.value = fFocusAuto ? 1 : 0;
					break;
				case B_CAMERA_PARAM_FOCUS:
					if (fFocusAuto)
						continue;
					p.value = _ToDeviceUnits(dc.isBool, dc.min, dc.max,
						fFocus, 0.0f, 1.0f);
					break;
				case B_CAMERA_PARAM_POWER_LINE_FREQ:
					p.value = _PowerLineToV4L2(fPowerLineFreq);
					break;
				default:
					continue;
			}
			props.push_back(p);
		}
	}

	if (!props.empty())
		backend->SetNodeControls(fNodeName.String(), props);
}
