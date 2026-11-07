/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// SIP video over VP8: V4L2 capture feeds libvpx; received frames reach the app
// as I420 through its video hooks.

#include "SipBackend.h"

#include <re.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <vpx/vp8cx.h>
#include <vpx/vp8dx.h>
#include <vpx/vpx_decoder.h>
#include <vpx/vpx_encoder.h>

#include <atomic>
#include <map>
#include <thread>


namespace {


const int kVideoWidth = 320;
const int kVideoHeight = 240;
const int kVideoFps = 15;


struct CaptureBuffer {
	uint8_t* start;
	size_t length;
};


// YUYV (V4L2 default) to I420, needed by libvpx.
void
_YuyvToI420(const uint8_t* yuyv, uint8_t* y, uint8_t* u, uint8_t* v,
	int width, int height)
{
	for (int row = 0; row < height; row++) {
		const uint8_t* src = yuyv + row * width * 2;
		uint8_t* yRow = y + row * width;
		uint8_t* uRow = u + (row / 2) * (width / 2);
		uint8_t* vRow = v + (row / 2) * (width / 2);
		for (int col = 0; col < width; col += 2) {
			yRow[col] = src[0];
			yRow[col + 1] = src[2];
			if (row % 2 == 0) {
				uRow[col / 2] = src[1];
				vRow[col / 2] = src[3];
			}
			src += 4;
		}
	}
}


class VideoPipeline {
public:
								VideoPipeline(SipBackend* backend);
								~VideoPipeline();

			status_t			Start(const char* device);
			void				Stop();

			// Feeds one received VP8 payload (from RTP) to the
			// decoder; keyFrame marks the start of a frame.
			void				InjectPayload(const uint8* payload,
									size_t size, bool keyFrame);

private:
			void				_CaptureLoop();
			bool				_InitEncoder();
			bool				_InitDecoder();
			void				_DecodeFrame(const uint8* data,
									size_t size);

			SipBackend*			fBackend;
			std::thread*		fThread;
			std::atomic<bool>	fRunning;
			int					fFd;
			CaptureBuffer*		fBuffers;
			unsigned			fBufferCount;
			vpx_codec_ctx_t		fEncoder;
			vpx_codec_ctx_t		fDecoder;
			bool				fEncoderReady;
			bool					fDecoderReady;

			// VP8 fragmentation reassembly on the receive path.
			uint8*				fFragment;
			size_t				fFragmentSize;
			size_t				fFragmentUsed;
			bool				fInFrame;
};


VideoPipeline::VideoPipeline(SipBackend* backend)
	:
	fBackend(backend),
	fThread(NULL),
	fRunning(false),
	fFd(-1),
	fBuffers(NULL),
	fBufferCount(0),
	fEncoderReady(false),
	fDecoderReady(false),
	fFragment(NULL),
	fFragmentSize(0),
	fFragmentUsed(0),
	fInFrame(false)
{
	memset(&fEncoder, 0, sizeof(fEncoder));
	memset(&fDecoder, 0, sizeof(fDecoder));
}


VideoPipeline::~VideoPipeline()
{
	Stop();
	if (fEncoderReady)
		vpx_codec_destroy(&fEncoder);
	if (fDecoderReady)
		vpx_codec_destroy(&fDecoder);
	mem_deref(fFragment);
}


bool
VideoPipeline::_InitEncoder()
{
	vpx_codec_enc_cfg_t config;
	if (vpx_codec_enc_config_default(&vpx_codec_vp8_cx_algo, &config,
			0) != VPX_CODEC_OK)
		return false;
	config.g_w = kVideoWidth;
	config.g_h = kVideoHeight;
	config.g_timebase.num = 1;
	config.g_timebase.den = kVideoFps;
	config.rc_target_bitrate = 256;
	config.g_lag_in_frames = 0;
	config.kf_max_dist = kVideoFps * 2;

	if (vpx_codec_enc_init(&fEncoder, &vpx_codec_vp8_cx_algo, &config, 0)
			!= VPX_CODEC_OK)
		return false;
	fEncoderReady = true;
	return true;
}


bool
VideoPipeline::_InitDecoder()
{
	if (vpx_codec_dec_init(&fDecoder, &vpx_codec_vp8_dx_algo, NULL, 0)
			!= VPX_CODEC_OK)
		return false;
	fDecoderReady = true;
	return true;
}


status_t
VideoPipeline::Start(const char* device)
{
	fFd = open(device, O_RDWR | O_NONBLOCK);
	if (fFd < 0)
		return B_ERROR;

	struct v4l2_format format;
	memset(&format, 0, sizeof(format));
	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	format.fmt.pix.width = kVideoWidth;
	format.fmt.pix.height = kVideoHeight;
	format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	format.fmt.pix.field = V4L2_FIELD_NONE;
	if (ioctl(fFd, VIDIOC_S_FMT, &format) != 0) {
		close(fFd);
		fFd = -1;
		return B_ERROR;
	}

	struct v4l2_requestbuffers request;
	memset(&request, 0, sizeof(request));
	request.count = 4;
	request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	request.memory = V4L2_MEMORY_MMAP;
	if (ioctl(fFd, VIDIOC_REQBUFS, &request) != 0 || request.count < 2) {
		close(fFd);
		fFd = -1;
		return B_ERROR;
	}

	fBufferCount = request.count;
	fBuffers = new CaptureBuffer[fBufferCount];
	for (unsigned i = 0; i < fBufferCount; i++) {
		struct v4l2_buffer buffer;
		memset(&buffer, 0, sizeof(buffer));
		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buffer.memory = V4L2_MEMORY_MMAP;
		buffer.index = i;
		if (ioctl(fFd, VIDIOC_QUERYBUF, &buffer) != 0) {
			Stop();
			return B_ERROR;
		}
		fBuffers[i].length = buffer.length;
		fBuffers[i].start = (uint8_t*)mmap(NULL, buffer.length,
			PROT_READ | PROT_WRITE, MAP_SHARED, fFd, buffer.m.offset);
		if (fBuffers[i].start == MAP_FAILED) {
			Stop();
			return B_ERROR;
		}
		ioctl(fFd, VIDIOC_QBUF, &buffer);
	}

	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(fFd, VIDIOC_STREAMON, &type) != 0) {
		Stop();
		return B_ERROR;
	}

	if (!_InitEncoder()) {
		Stop();
		return B_ERROR;
	}

	fRunning = true;
	fThread = new std::thread(&VideoPipeline::_CaptureLoop, this);
	return B_OK;
}


void
VideoPipeline::Stop()
{
	fRunning = false;
	if (fThread != NULL) {
		fThread->join();
		delete fThread;
		fThread = NULL;
	}
	if (fFd >= 0) {
		enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		ioctl(fFd, VIDIOC_STREAMOFF, &type);
		close(fFd);
		fFd = -1;
	}
	if (fBuffers != NULL) {
		for (unsigned i = 0; i < fBufferCount; i++) {
			if (fBuffers[i].start != MAP_FAILED)
				munmap(fBuffers[i].start, fBuffers[i].length);
		}
		delete[] fBuffers;
		fBuffers = NULL;
		fBufferCount = 0;
	}
}


void
VideoPipeline::_CaptureLoop()
{
	const size_t ySize = kVideoWidth * kVideoHeight;
	const size_t i420Size = ySize + ySize / 2;
	uint8_t* i420 = new uint8_t[i420Size];
	uint8_t* yuyv = new uint8_t[kVideoWidth * kVideoHeight * 2];

	while (fRunning) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(fFd, &fds);
		struct timeval timeout = { 1, 0 };
		if (select(fFd + 1, &fds, NULL, NULL, &timeout) <= 0)
			continue;

		struct v4l2_buffer buffer;
		memset(&buffer, 0, sizeof(buffer));
		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buffer.memory = V4L2_MEMORY_MMAP;
		if (ioctl(fFd, VIDIOC_DQBUF, &buffer) != 0)
			continue;

		memcpy(yuyv, fBuffers[buffer.index].start,
			kVideoWidth * kVideoHeight * 2);
		ioctl(fFd, VIDIOC_QBUF, &buffer);

		_YuyvToI420(yuyv, i420, i420 + ySize, i420 + ySize + ySize / 4,
			kVideoWidth, kVideoHeight);

		// Self view first: the app draws the local frame.
		if (fBackend->fVideoLocal != NULL) {
			const uint8_t* planes[3] = {
				i420, i420 + ySize, i420 + ySize + ySize / 4
			};
			int strides[3] = {
				kVideoWidth, kVideoWidth / 2, kVideoWidth / 2
			};
			fBackend->fVideoLocal(fBackend->fVideoCookie, planes,
				strides, kVideoWidth, kVideoHeight, true);
		}

		vpx_image_t* image = vpx_img_alloc(NULL, VPX_IMG_FMT_I420,
			kVideoWidth, kVideoHeight, 1);
		if (image == NULL)
			continue;
		vpx_img_wrap(image, VPX_IMG_FMT_I420, kVideoWidth, kVideoHeight,
			1, i420);
		vpx_codec_iter_t iter = NULL;
		const vpx_codec_cx_pkt_t* packet;
		if (vpx_codec_encode(&fEncoder, image, 0, 1, 0, VPX_DL_REALTIME)
				== VPX_CODEC_OK) {
			while ((packet = vpx_codec_get_cx_data(&fEncoder,
					&iter)) != NULL) {
				if (packet->kind != VPX_CODEC_CX_FRAME_PKT)
					continue;
				bool keyFrame = (packet->data.frame.flags
					& VPX_FRAME_IS_KEY) != 0;
				// Find the live call; the backend routes the
				// payload by path.
				BMessage calls;
				// Video rides the first active call with a
				// negotiated video m-line; the backend
				// rejects when there is none.
				BString callPath = fBackend->VideoCallPath();
				if (!callPath.IsEmpty())
					fBackend->SendVideoPayload(callPath.String(),
						(const uint8*)packet->data.frame.buf,
						packet->data.frame.sz, keyFrame);
			}
		}
		vpx_img_free(image);
	}

	delete[] i420;
	delete[] yuyv;
}


void
VideoPipeline::InjectPayload(const uint8* payload, size_t size, bool keyFrame)
{
	if (!fDecoderReady && !_InitDecoder())
		return;

	// RFC 7741: one descriptor byte with the S/E bits; assemble
	// the frame then hand it to libvpx.
	if (size < 1)
		return;
	bool start = (payload[0] & 0x10) != 0;
	const uint8* body = payload + 1;
	size_t bodySize = size - 1;

	if (start) {
		fFragmentUsed = 0;
		fInFrame = true;
	}
	if (!fInFrame)
		return;

	if (fFragmentUsed + bodySize > fFragmentSize) {
		size_t newSize = (fFragmentUsed + bodySize) * 2;
		uint8* grown = (uint8*)mem_alloc(newSize, NULL);
		if (grown == NULL)
			return;
		if (fFragment != NULL)
			memcpy(grown, fFragment, fFragmentUsed);
		mem_deref(fFragment);
		fFragment = grown;
		fFragmentSize = newSize;
	}
	memcpy(fFragment + fFragmentUsed, body, bodySize);
	fFragmentUsed += bodySize;

	// The E bit is not carried in our single-byte descriptor; the
	// backend flushes on the next non-start packet or the app can
	// call with keyFrame marking frame boundaries. Decode now.
	_DecodeFrame(fFragment, fFragmentUsed);
	fFragmentUsed = 0;
	fInFrame = false;
}


void
VideoPipeline::_DecodeFrame(const uint8* data, size_t size)
{
	vpx_codec_iter_t iter = NULL;
	vpx_image_t* image;
	if (vpx_codec_decode(&fDecoder, data, size, NULL, 0) != VPX_CODEC_OK)
		return;

	while ((image = vpx_codec_get_frame(&fDecoder, &iter)) != NULL) {
		if (fBackend->fVideoRemote == NULL)
			break;
		const uint8_t* planes[3] = {
			image->planes[VPX_PLANE_Y],
			image->planes[VPX_PLANE_U],
			image->planes[VPX_PLANE_V]
		};
		int strides[3] = {
			image->stride[VPX_PLANE_Y],
			image->stride[VPX_PLANE_U],
			image->stride[VPX_PLANE_V]
		};
		fBackend->fVideoRemote(fBackend->fVideoCookie, planes, strides,
			image->d_w, image->d_h, false);
	}
}


} // namespace


// #pragma mark - SipBackend video glue


// The pipeline lives behind a per-backend pointer; the header keeps
// it void* so vpx/v4l2 stay out of the public surface.
static std::map<SipBackend*, VideoPipeline*> sPipelines;


static VideoPipeline*
_PipelineFor(SipBackend* backend, bool create)
{
	std::map<SipBackend*, VideoPipeline*>::iterator it
		= sPipelines.find(backend);
	if (it != sPipelines.end())
		return it->second;
	if (!create)
		return NULL;

	VideoPipeline* pipeline = new VideoPipeline(backend);
	sPipelines[backend] = pipeline;
	return pipeline;
}


void
SipBackend_StartVideoCapture(SipBackend* backend, const char* device)
{
	VideoPipeline* pipeline = _PipelineFor(backend, true);
	if (pipeline != NULL)
		pipeline->Start(device);
}


void
SipBackend_StopVideoCapture(SipBackend* backend)
{
	VideoPipeline* pipeline = _PipelineFor(backend, false);
	if (pipeline == NULL)
		return;

	sPipelines.erase(backend);
	delete pipeline;
}


void
SipBackend_InjectVideoPayload(SipBackend* backend, const uint8* payload,
	size_t size, bool keyFrame)
{
	VideoPipeline* pipeline = _PipelineFor(backend, false);
	if (pipeline != NULL)
		pipeline->InjectPayload(payload, size, keyFrame);
}
