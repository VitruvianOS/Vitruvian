/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <media2/MediaFile.h>
#include <media2/MediaTrack.h>

#include <new>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include <DataIO.h>
#include <Entry.h>
#include <Path.h>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/pbutils/gstdiscoverer.h>

#include "GStreamerBackend.h"
#include "MediaEncodeTables.h"


using namespace BPrivate::media;


enum track_kind {
	kTrackAudio = 0,
	kTrackVideo,
	kTrackText
};


// #pragma mark - BMediaTrack::Impl


class BMediaTrack::Impl {
public:
	Impl()
		:
		fAppSink(NULL),
		fAppSrc(NULL),
		fPipeline(NULL),
		fPlayBin(NULL),
		fInitErr(B_NO_INIT),
		fWriteMode(false),
		fEOS(false),
		fKind(kTrackAudio),
		fStreamIndex(0),
		fFrameStride(0),
		fNextPts(0),
		fCodecId(B_CODEC_TYPE_UNKNOWN),
		fPendingFrames(0),
		fCarryOffset(0),
		fQuality(-1.0f),
		fQualityApplied(false)
	{
	}

	// Hands the pending raw chunk to the pipeline. WriteFrames keeps the
	// latest chunk pending so ReplaceFrames can still swap it.
	status_t PushPending()
	{
		if (fPending.empty())
			return B_OK;
		if (fAppSrc == NULL)
			return B_NO_INIT;

		size_t bytes = fPending.size();
		GstBuffer* gb = gst_buffer_new_allocate(NULL, bytes, NULL);
		if (gb == NULL)
			return B_NO_MEMORY;
		GstMapInfo map;
		if (!gst_buffer_map(gb, &map, GST_MAP_WRITE)) {
			gst_buffer_unref(gb);
			return B_ERROR;
		}
		memcpy(map.data, &fPending[0], bytes);
		gst_buffer_unmap(gb, &map);

		int64 frames = fPendingFrames;
		fPending.clear();
		fPendingFrames = 0;

		float rate = 0.0f;
		if (fKind == kTrackVideo)
			rate = fFormat.format.u.raw_video.field_rate;
		else
			rate = fFormat.format.u.raw_audio.frame_rate;
		if (rate > 0.0f) {
			const uint64 durationNs = (uint64)((frames * GST_SECOND)
				/ (int64)rate);
			GST_BUFFER_PTS(gb)      = fNextPts;
			GST_BUFFER_DURATION(gb) = durationNs;
			fNextPts += durationNs;
		}

		GstFlowReturn r = gst_app_src_push_buffer(GST_APP_SRC(fAppSrc), gb);
			// push_buffer takes ownership of gb on success
		if (r != GST_FLOW_OK)
			return B_ERROR;

		if (!fQualityApplied && fQuality >= 0.0f) {
			ApplyEncodeQuality(fPipeline, fCodecId, fQuality);
			fQualityApplied = true;
		}
		return B_OK;
	}

	GstElement*		fAppSink;	// read mode  (borrowed; owned by pipeline)
	GstElement*		fAppSrc;	// write mode (borrowed; owned by pipeline)
	GstElement*		fPipeline;	// borrowed
	GstElement*	fPlayBin;	// playbin for stream selection (borrowed)
	BMediaFormat	fFormat;
	BMediaFormat	fEncodedFormat;
	status_t		fInitErr;
	bool			fWriteMode;
	bool			fEOS;
	int32			fKind;
	int32			fStreamIndex;
	uint32			fFrameStride;
	uint64			fNextPts;	// nanoseconds since stream start
	int32			fCodecId;
	std::vector<uint8>	fPending;
	int64			fPendingFrames;
	// Decoded audio past what the last ReadFrames asked for.
	std::vector<uint8>	fCarry;
	size_t			fCarryOffset;
	float			fQuality;
	bool			fQualityApplied;
};


// #pragma mark - BDataIO source


struct DataIOSource {
	BDataIO*		io;
	BPositionIO*	position;
	off_t			offset;
};


static void
data_io_need_data(GstAppSrc* src, guint length, gpointer cookie)
{
	DataIOSource* source = (DataIOSource*)cookie;
	if (length == 0 || length > 65536)
		length = 65536;

	GstBuffer* buffer = gst_buffer_new_allocate(NULL, length, NULL);
	GstMapInfo map;
	if (buffer == NULL || !gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
		if (buffer != NULL)
			gst_buffer_unref(buffer);
		gst_app_src_end_of_stream(src);
		return;
	}

	ssize_t got = source->position != NULL
		? source->position->ReadAt(source->offset, map.data, length)
		: source->io->Read(map.data, length);
	gst_buffer_unmap(buffer, &map);

	if (got <= 0) {
		gst_buffer_unref(buffer);
		gst_app_src_end_of_stream(src);
		return;
	}

	source->offset += got;
	gst_buffer_set_size(buffer, got);
	gst_app_src_push_buffer(src, buffer);
}


static gboolean
data_io_seek_data(GstAppSrc*, guint64 offset, gpointer cookie)
{
	DataIOSource* source = (DataIOSource*)cookie;
	if (source->position == NULL)
		return FALSE;
	source->offset = offset;
	return TRUE;
}


static void
data_io_source_setup(GstElement*, GstElement* element, gpointer cookie)
{
	DataIOSource* source = (DataIOSource*)cookie;
	GstAppSrcCallbacks callbacks;
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.need_data = data_io_need_data;
	callbacks.seek_data = data_io_seek_data;
	gst_app_src_set_callbacks(GST_APP_SRC(element), &callbacks, source, NULL);

	off_t size = 0;
	if (source->position != NULL && source->position->GetSize(&size) == B_OK)
		gst_app_src_set_size(GST_APP_SRC(element), size);
	gst_app_src_set_stream_type(GST_APP_SRC(element),
		source->position != NULL ? GST_APP_STREAM_TYPE_RANDOM_ACCESS
			: GST_APP_STREAM_TYPE_STREAM);
}


// #pragma mark - BMediaFile::Impl


class BMediaFile::Impl {
public:
	Impl()
		:
		fPipeline(NULL),
		fAppSink(NULL),
		fVideoSink(NULL),
		fAppSrc(NULL),
		fInitErr(B_NO_INIT),
		fWriteMode(false),
		fHasSource(false),
		fFormatInfo(NULL),
		fCodecType(B_CODEC_TYPE_UNKNOWN),
		fDurationNs(0)
	{
	}

	~Impl()
	{
		for (size_t i = 0; i < fTracks.size(); i++)
			delete fTracks[i];
		fTracks.clear();
		if (fPipeline != NULL) {
			gst_element_set_state(fPipeline, GST_STATE_NULL);
			gst_object_unref(fPipeline);
			fPipeline = NULL;
		}
	}

	status_t Open(const entry_ref* ref)
	{
		if (ref == NULL)
			return B_BAD_VALUE;

		BPath path(ref);
		if (path.InitCheck() != B_OK)
			return path.InitCheck();

		gchar* uri = gst_filename_to_uri(path.Path(), NULL);
		if (uri == NULL)
			return B_BAD_VALUE;
		status_t status = OpenUri(uri);
		g_free(uri);
		return status;
	}

	// Discover every demuxed stream (audio, video, subtitle) via
	// GstDiscoverer, then build a playbin pipeline that can actually
	// deliver decoded frames for the readable ones.
	// The source must outlive this object.
	status_t OpenIO(BDataIO* io)
	{
		if (io == NULL)
			return B_BAD_VALUE;
		fSource.io = io;
		fSource.position = dynamic_cast<BPositionIO*>(io);
		fSource.offset = 0;
		fHasSource = true;
		return OpenUri("appsrc://");
	}

	status_t OpenUri(const char* uri)
	{
		if (uri == NULL)
			return B_BAD_VALUE;

		if (GStreamerBackend::GetInstance() == NULL)
			return B_NO_INIT;

		std::vector<BMediaFormat> encoded;
		std::vector<int32> kinds;
		std::vector<int32> streamIndexes;
		// discovery cannot pull from a BDataIO
		status_t discErr = fHasSource ? B_NOT_SUPPORTED
			: _DiscoverStreams(uri, encoded, kinds, streamIndexes,
				&fDurationNs);
		if (discErr != B_OK) {
			// Discovery failed; still open a playbin so single-stream
			// files keep working. Track list is rebuilt after preroll.
			encoded.clear();
			kinds.clear();
			streamIndexes.clear();
		}

		fPipeline = gst_element_factory_make("playbin", "vitruvian-playbin");
		if (fPipeline == NULL)
			return B_ERROR;

		// Restrict the audio sink to F32LE interleaved so the rest of the
		// kit deals with a known format. PipeWire and BSoundPlayer can both
		// consume this without further conversion.
		GstCaps* audioCaps = gst_caps_new_simple("audio/x-raw",
			"format",   G_TYPE_STRING, "F32LE",
			"layout",   G_TYPE_STRING, "interleaved",
			NULL);

		fAppSink = gst_element_factory_make("appsink", "vitruvian-appsink");
		if (fAppSink == NULL) {
			gst_caps_unref(audioCaps);
			gst_object_unref(fPipeline);
			fPipeline = NULL;
			return B_ERROR;
		}
		g_object_set(fAppSink,
			"caps",          audioCaps,
			"sync",          FALSE,
			"max-buffers",   8,
			"drop",          FALSE,
			NULL);
		gst_caps_unref(audioCaps);

		GstCaps* videoCaps = gst_caps_new_simple("video/x-raw",
			"format", G_TYPE_STRING, "BGRx",
			NULL);
		fVideoSink = gst_element_factory_make("appsink", "vitruvian-video");
		if (fVideoSink == NULL) {
			gst_object_unref(fAppSink);
			gst_object_unref(fPipeline);
			fAppSink = NULL;
			fPipeline = NULL;
			return B_ERROR;
		}
		// Video holds frames until read: dropping here lost a short clip
		// before the first pull. Audio-only readers flip it to drop (see
		// ReadFrames) so an unread video sink cannot stall the demuxer.
		g_object_set(fVideoSink,
			"caps",        videoCaps,
			"sync",        FALSE,
			"max-buffers", 8,
			"drop",        FALSE,
			NULL);
		gst_caps_unref(videoCaps);

		// Text is listed as a track but has no sink: media_format cannot
		// describe a subtitle payload without inventing a type.
		GstElement* textSink = gst_element_factory_make("fakesink",
			"vitruvian-text");

		if (fHasSource) {
			g_signal_connect(fPipeline, "source-setup",
				G_CALLBACK(data_io_source_setup), &fSource);
		}

		g_object_set(fPipeline,
			"uri",         uri,
			"audio-sink",  fAppSink,
			"video-sink",  fVideoSink,
			"text-sink",   textSink,
			NULL);

		// PAUSED runs preroll; by the time it completes, caps are negotiated.
		GstStateChangeReturn r = gst_element_set_state(fPipeline,
			GST_STATE_PAUSED);
		if (r == GST_STATE_CHANGE_FAILURE)
			return B_ERROR;
		r = gst_element_get_state(fPipeline, NULL, NULL, 5 * GST_SECOND);
		if (r == GST_STATE_CHANGE_FAILURE)
			return B_ERROR;

		if (encoded.empty()) {
			// Discovery was unavailable; enumerate playbin stream counts.
			_BuildTracksFromPlayBin(encoded, kinds, streamIndexes);
		}

		// Negotiated caps fill in what the decoder will actually emit.
		_FillDecodedFromSinks(encoded, kinds);

		if (encoded.empty()) {
			// No demuxed streams at all: not a media file we can open.
			gst_element_set_state(fPipeline, GST_STATE_NULL);
			return B_ERROR;
		}

		for (size_t i = 0; i < encoded.size(); i++) {
			BMediaTrack* track = new(std::nothrow) BMediaTrack();
			if (track == NULL)
				return B_NO_MEMORY;
			track->fImpl->fKind          = kinds[i];
			track->fImpl->fStreamIndex   = streamIndexes[i];
			track->fImpl->fEncodedFormat = encoded[i];
			track->fImpl->fFormat        = encoded[i];
			track->fImpl->fPipeline      = fPipeline;
			track->fImpl->fPlayBin       = fPipeline;
			if (kinds[i] == kTrackAudio)
				track->fImpl->fAppSink = fAppSink;
			else if (kinds[i] == kTrackVideo)
				track->fImpl->fAppSink = fVideoSink;
			else {
				// Listed, not readable: no decoded union member exists.
				track->fImpl->fInitErr = B_NOT_SUPPORTED;
				track->fImpl->fAppSink = NULL;
			}
			if (track->fImpl->fInitErr == B_NOT_SUPPORTED) {
				// Keep the track in the list; only reading is refused.
			} else {
				track->fImpl->fInitErr = B_OK;
			}
			fTracks.push_back(track);
		}

		// Transition to PLAYING so the pipeline pulls data on demand.
		gst_element_set_state(fPipeline, GST_STATE_PLAYING);

		fInitErr = B_OK;
		return B_OK;
	}

	status_t OpenWrite(const entry_ref* ref, const media_file_format* format)
	{
		if (ref == NULL || format == NULL)
			return B_BAD_VALUE;
		BPath path(ref);
		if (path.InitCheck() != B_OK)
			return path.InitCheck();
		if (GStreamerBackend::GetInstance() == NULL)
			return B_NO_INIT;

		const EncodeFileFormat* info = FindEncodeFileFormat(
			format->short_name);
		if (info == NULL) {
			for (int32 i = 0; ; i++) {
				const EncodeFileFormat* candidate
					= EncodeFileFormatAt(i);
				if (candidate == NULL)
					break;
				if (strcmp(candidate->mimeType,
						format->mime_type) == 0) {
					info = candidate;
					break;
				}
			}
		}
		if (info == NULL || !IsEncodeFileFormatAvailable(info))
			return B_BAD_VALUE;

		fOutputPath = path.Path();
		fFormatInfo = info;
		fCodecType  = DefaultCodecFor(info, true);
		fWriteMode  = true;
		fInitErr    = B_OK;
		return B_OK;
	}

	// First codec the container accepts for the requested media kind.
	static int32 DefaultCodecFor(const EncodeFileFormat* format, bool audio)
	{
		for (int32 i = 0; i < format->codecCount; i++) {
			const EncodeCodecInfo* codec = FindEncodeCodec(
				format->codecs[i]);
			if (codec != NULL && codec->audio == audio)
				return codec->id;
		}
		return B_CODEC_TYPE_UNKNOWN;
	}

	BMediaTrack* CreateTrack(const BMediaFormat& outputFormat)
	{
		if (!fWriteMode || fFormatInfo == NULL || fTracks.size() >= 2)
			return NULL;
		const bool isAudio = outputFormat.IsRawAudio()
			|| outputFormat.IsEncodedAudio();
		const bool isVideo = outputFormat.IsRawVideo()
			|| outputFormat.IsEncodedVideo();
		if (!isAudio && !isVideo)
			return NULL;

		for (size_t i = 0; i < fTracks.size(); i++) {
			if (fTracks[i]->fImpl->fKind
					== (isVideo ? kTrackVideo : kTrackAudio)) {
				return NULL;	// one track per media kind
			}
		}

		BMediaFormat rawIn;
		BMediaFormat encFmt;
		int32 codecId = B_CODEC_TYPE_UNKNOWN;
		if (outputFormat.IsEncodedAudio()) {
			codecId = outputFormat.format.u.encoded_audio.encoding;
			encFmt = outputFormat;
			rawIn.format.type = B_MEDIA_RAW_AUDIO;
			rawIn.format.u.raw_audio
				= outputFormat.format.u.encoded_audio.output;
			rawIn.is_planar = false;
		} else if (outputFormat.IsEncodedVideo()) {
			codecId = outputFormat.format.u.encoded_video.encoding;
			encFmt = outputFormat;
			rawIn.format.type = B_MEDIA_RAW_VIDEO;
			rawIn.format.u.raw_video
				= outputFormat.format.u.encoded_video.output;
		} else if (outputFormat.IsRawAudio()) {
			codecId = fCodecType != B_CODEC_TYPE_UNKNOWN
				? fCodecType : DefaultCodecFor(fFormatInfo, true);
			rawIn = outputFormat;
			encFmt.format.type = B_MEDIA_ENCODED_AUDIO;
			media_encoded_audio_format& enc
				= encFmt.format.u.encoded_audio;
			memset(&enc, 0, sizeof(enc));
			enc.encoding = codecId;
			enc.output = outputFormat.format.u.raw_audio;
		} else {
			codecId = DefaultCodecFor(fFormatInfo, false);
			rawIn = outputFormat;
			encFmt.format.type = B_MEDIA_ENCODED_VIDEO;
			media_encoded_video_format& enc
				= encFmt.format.u.encoded_video;
			memset(&enc, 0, sizeof(enc));
			enc.encoding = codecId;
			enc.output = outputFormat.format.u.raw_video;
		}

		if (codecId == B_CODEC_TYPE_UNKNOWN)
			return NULL;
		const EncodeCodecInfo* codec = FindEncodeCodec(codecId);
		if (codec == NULL || codec->audio == isVideo)
			return NULL;
		bool accepts = false;
		for (int32 i = 0; i < fFormatInfo->codecCount; i++) {
			if (fFormatInfo->codecs[i] == codecId)
				accepts = true;
		}
		if (!accepts || !IsEncodeCodecAvailable(codecId))
			return NULL;

		BMediaTrack* track = new(std::nothrow) BMediaTrack();
		if (track == NULL)
			return NULL;
		track->fImpl->fFormat        = rawIn;
		track->fImpl->fEncodedFormat = encFmt;
		track->fImpl->fWriteMode     = true;
		track->fImpl->fKind          = isVideo ? kTrackVideo : kTrackAudio;
		track->fImpl->fCodecId       = codecId;
		if (isVideo) {
			track->fImpl->fFrameStride = rawIn.format.u.raw_video.display
				.bytes_per_row;
		} else {
			const media_raw_audio_format& raw
				= rawIn.format.u.raw_audio;
			track->fImpl->fFrameStride = (raw.format
				& media_raw_audio_format::B_AUDIO_SIZE_MASK)
				* raw.channel_count;
		}
		track->fImpl->fInitErr = B_OK;
		fTracks.push_back(track);
		return track;
	}

	status_t CommitHeader()
	{
		if (!fWriteMode || fFormatInfo == NULL)
			return B_NO_INIT;
		if (fTracks.empty())
			return B_NO_INIT;
		if (fPipeline != NULL)
			return B_OK;

		const int32 count = (int32)fTracks.size();
		int32 codecIds[2] = { 0, 0 };
		GstCaps* caps[2] = { NULL, NULL };
		GstElement* appSrcs[2] = { NULL, NULL };
		for (int32 i = 0; i < count; i++) {
			BMediaTrack::Impl* track = fTracks[i]->fImpl;
			codecIds[i] = track->fCodecId;
			if (track->fKind == kTrackVideo)
				caps[i] = GStreamerBackend::RawVideoToGstCaps(
					track->fFormat);
			else
				caps[i] = GStreamerBackend::RawAudioToGstCaps(
					track->fFormat);
		}

		fPipeline = CreateEncodePipeline(fOutputPath.c_str(), fFormatInfo,
			codecIds, count, caps, appSrcs);
		for (int32 i = 0; i < count; i++) {
			if (caps[i] != NULL)
				gst_caps_unref(caps[i]);
		}
		if (fPipeline == NULL)
			return B_ERROR;

		for (int32 i = 0; i < count; i++) {
			fTracks[i]->fImpl->fPipeline = fPipeline;
			fTracks[i]->fImpl->fAppSrc = appSrcs[i];
		}
		// The single-appsrc shortcuts in the read path never apply here.
		fAppSrc = appSrcs[0];

		GstStateChangeReturn r = gst_element_set_state(fPipeline,
			GST_STATE_PLAYING);
		if (r == GST_STATE_CHANGE_FAILURE)
			return B_ERROR;
		r = gst_element_get_state(fPipeline, NULL, NULL, 5 * GST_SECOND);
		if (r == GST_STATE_CHANGE_FAILURE)
			return B_ERROR;
		return B_OK;
	}

	status_t CloseFile()
	{
		if (!fWriteMode || fPipeline == NULL)
			return B_OK;
		for (size_t i = 0; i < fTracks.size(); i++) {
			BMediaTrack::Impl* track = fTracks[i]->fImpl;
			status_t err = track->PushPending();
			if (err != B_OK)
				return err;
			if (track->fAppSrc != NULL) {
				gst_app_src_end_of_stream(
					GST_APP_SRC(track->fAppSrc));
			}
		}
		// Wait for EOS to propagate to filesink, then bring pipeline down.
		GstBus* bus = gst_element_get_bus(fPipeline);
		if (bus != NULL) {
			gst_bus_timed_pop_filtered(bus, 5 * GST_SECOND,
				(GstMessageType)(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
			gst_object_unref(bus);
		}
		gst_element_set_state(fPipeline, GST_STATE_NULL);
		return B_OK;
	}

	bool OwnsTrack(const BMediaTrack* track) const
	{
		for (size_t i = 0; i < fTracks.size(); i++) {
			if (fTracks[i] == track)
				return true;
		}
		return false;
	}

	// Select the playbin stream a track reads from, so ReadFrames pulls
	// the demuxed branch the caller asked for, not always stream 0.
	void SelectStream(BMediaTrack::Impl* t)
	{
		if (t == NULL || fPipeline == NULL || t->fWriteMode)
			return;
		if (t->fKind == kTrackAudio) {
			g_object_set(fPipeline, "current-audio", t->fStreamIndex, NULL);
		} else if (t->fKind == kTrackVideo) {
			g_object_set(fPipeline, "current-video", t->fStreamIndex, NULL);
		}
	}

	status_t _DiscoverStreams(const char* uri,
		std::vector<BMediaFormat>& encoded, std::vector<int32>& kinds,
		std::vector<int32>& streamIndexes, gint64* outDurationNs)
	{
		GError* err = NULL;
		GstDiscoverer* disc = gst_discoverer_new(10 * GST_SECOND, &err);
		if (disc == NULL) {
			if (err != NULL)
				g_error_free(err);
			return B_ERROR;
		}

		GstDiscovererInfo* info = gst_discoverer_discover_uri(disc, uri, &err);
		if (info == NULL) {
			if (err != NULL)
				g_error_free(err);
			g_object_unref(disc);
			return B_ERROR;
		}

		if (outDurationNs != NULL) {
			*outDurationNs = (gint64)gst_discoverer_info_get_duration(info);
		}

		// Walk container children in order; a bare stream is its own list.
		GstDiscovererStreamInfo* root
			= gst_discoverer_info_get_stream_info(info);
		GList* children = NULL;
		bool rootInList = false;
		if (root != NULL && GST_IS_DISCOVERER_CONTAINER_INFO(root)) {
			children = gst_discoverer_container_info_get_streams(
				GST_DISCOVERER_CONTAINER_INFO(root));
		} else if (root != NULL) {
			children = g_list_append(NULL, root);
			rootInList = true;
		}

		int32 audioIndex = 0;
		int32 videoIndex = 0;
		int32 textIndex = 0;

		for (GList* l = children; l != NULL; l = l->next) {
			GstDiscovererStreamInfo* si
				= GST_DISCOVERER_STREAM_INFO(l->data);
			if (si == NULL)
				continue;

			if (GST_IS_DISCOVERER_AUDIO_INFO(si)) {
				BMediaFormat fmt = _AudioFormatFromDiscoverer(
					GST_DISCOVERER_AUDIO_INFO(si));
				encoded.push_back(fmt);
				kinds.push_back(kTrackAudio);
				streamIndexes.push_back(audioIndex++);
			} else if (GST_IS_DISCOVERER_VIDEO_INFO(si)) {
				BMediaFormat fmt = _VideoFormatFromDiscoverer(
					GST_DISCOVERER_VIDEO_INFO(si),
					gst_discoverer_stream_info_get_tags(si));
				encoded.push_back(fmt);
				kinds.push_back(kTrackVideo);
				streamIndexes.push_back(videoIndex++);
			} else if (GST_IS_DISCOVERER_SUBTITLE_INFO(si)) {
				// List it; media_format has no subtitle union member.
				BMediaFormat fmt;
				fmt.format.type = B_MEDIA_TEXT;
				encoded.push_back(fmt);
				kinds.push_back(kTrackText);
				streamIndexes.push_back(textIndex++);
			}
		}

		if (children != NULL)
			gst_discoverer_stream_info_list_free(children);
		if (root != NULL && !rootInList)
			gst_discoverer_stream_info_unref(root);
		gst_discoverer_info_unref(info);
		g_object_unref(disc);

		return encoded.empty() ? B_ERROR : B_OK;
	}

	static BMediaFormat _AudioFormatFromDiscoverer(GstDiscovererAudioInfo* ai)
	{
		BMediaFormat enc;
		enc.format.type = B_MEDIA_ENCODED_AUDIO;
		media_encoded_audio_format& ea = enc.format.u.encoded_audio;
		memset(&ea, 0, sizeof(ea));
		ea.encoding = B_CODEC_TYPE_UNKNOWN;
		media_raw_audio_format& raw = ea.output;
		raw.frame_rate    = (float)gst_discoverer_audio_info_get_sample_rate(ai);
		raw.channel_count = gst_discoverer_audio_info_get_channels(ai);
		raw.format        = media_raw_audio_format::B_AUDIO_FLOAT;
		raw.byte_order    = B_MEDIA_HOST_ENDIAN;
		raw.buffer_size   = 0;
		const GstTagList* tags = gst_discoverer_stream_info_get_tags(
			GST_DISCOVERER_STREAM_INFO(ai));
		if (tags != NULL) {
			gchar* codec = NULL;
			if (gst_tag_list_get_string(tags, GST_TAG_AUDIO_CODEC, &codec)
					&& codec != NULL) {
				// Keep UNKNOWN; codec pretty names live in GetCodecInfo.
				g_free(codec);
			}
		}

		BMediaFormat rawFmt = enc;
		rawFmt.format.type = B_MEDIA_RAW_AUDIO;
		rawFmt.format.u.raw_audio = raw;
		// Store encoded in fEncodedFormat; decoded filled from sink caps.
		(void)rawFmt;
		return enc;
	}

	// Container rotation metadata: the degrees the stored image must be
	// rotated clockwise for display (phone videos carry rotate-90).
	static uint32 _OrientationFromTags(const GstTagList* tags)
	{
		if (tags == NULL)
			return B_VIDEO_TOP_LEFT_RIGHT;
		gchar* orientation = NULL;
		if (!gst_tag_list_get_string(tags, GST_TAG_IMAGE_ORIENTATION,
				&orientation) || orientation == NULL) {
			return B_VIDEO_TOP_LEFT_RIGHT;
		}
		uint32 value = B_VIDEO_TOP_LEFT_RIGHT;
		if (g_strcmp0(orientation, "rotate-90") == 0)
			value = B_VIDEO_ROTATE_90;
		else if (g_strcmp0(orientation, "rotate-180") == 0)
			value = B_VIDEO_ROTATE_180;
		else if (g_strcmp0(orientation, "rotate-270") == 0)
			value = B_VIDEO_ROTATE_270;
		g_free(orientation);
		return value;
	}

	static BMediaFormat _VideoFormatFromDiscoverer(GstDiscovererVideoInfo* vi,
		const GstTagList* tags)
	{
		BMediaFormat enc;
		enc.format.type = B_MEDIA_ENCODED_VIDEO;
		media_encoded_video_format& ev = enc.format.u.encoded_video;
		memset(&ev, 0, sizeof(ev));
		ev.encoding = B_CODEC_TYPE_UNKNOWN;
		media_raw_video_format& raw = ev.output;
		raw.width  = gst_discoverer_video_info_get_width(vi);
		raw.height = gst_discoverer_video_info_get_height(vi);
		guint num = gst_discoverer_video_info_get_framerate_num(vi);
		guint den = gst_discoverer_video_info_get_framerate_denom(vi);
		raw.field_rate = den != 0 ? (float)num / (float)den : 0.0f;
		guint parNum = gst_discoverer_video_info_get_par_num(vi);
		guint parDen = gst_discoverer_video_info_get_par_denom(vi);
		raw.pixel_width_aspect  = (uint16)parNum;
		raw.pixel_height_aspect = (uint16)parDen;
		raw.orientation = _OrientationFromTags(tags);
		raw.color_space = B_RGB32;
		raw.interlace = gst_discoverer_video_info_is_interlaced(vi) ? 2 : 1;
		raw.first_active = 0;
		raw.last_active = raw.height > 0 ? raw.height - 1 : 0;
		return enc;
	}

	// Fallback when GstDiscoverer is unavailable: ask playbin how many
	// streams of each type it exposed after preroll.
	void _BuildTracksFromPlayBin(std::vector<BMediaFormat>& encoded,
		std::vector<int32>& kinds, std::vector<int32>& streamIndexes)
	{
		if (fPipeline == NULL)
			return;
		gint nAudio = 0, nVideo = 0, nText = 0;
		g_object_get(fPipeline,
			"n-audio", &nAudio,
			"n-video", &nVideo,
			"n-text",  &nText,
			NULL);

		for (gint i = 0; i < nAudio; i++) {
			BMediaFormat fmt;
			fmt.format.type = B_MEDIA_ENCODED_AUDIO;
			media_encoded_audio_format& ea = fmt.format.u.encoded_audio;
			memset(&ea, 0, sizeof(ea));
			ea.encoding = B_CODEC_TYPE_UNKNOWN;
			gint channels = 0;
			g_object_get(fPipeline, "get-audio-channels", i, &channels, NULL);
			ea.output.channel_count = (uint32)channels;
			ea.output.frame_rate = 48000.0f;
			ea.output.format = media_raw_audio_format::B_AUDIO_FLOAT;
			ea.output.byte_order = B_MEDIA_HOST_ENDIAN;
			encoded.push_back(fmt);
			kinds.push_back(kTrackAudio);
			streamIndexes.push_back(i);
		}
		for (gint i = 0; i < nVideo; i++) {
			BMediaFormat fmt;
			fmt.format.type = B_MEDIA_ENCODED_VIDEO;
			media_encoded_video_format& ev = fmt.format.u.encoded_video;
			memset(&ev, 0, sizeof(ev));
			ev.encoding = B_CODEC_TYPE_UNKNOWN;
			GstTagList* tags = NULL;
			g_object_get(fPipeline, "get-video-tags", i, &tags, NULL);
			ev.output.orientation = _OrientationFromTags(tags);
			if (tags != NULL)
				gst_tag_list_unref(tags);
			ev.output.color_space = B_RGB32;
			encoded.push_back(fmt);
			kinds.push_back(kTrackVideo);
			streamIndexes.push_back(i);
		}
		for (gint i = 0; i < nText; i++) {
			BMediaFormat fmt;
			fmt.format.type = B_MEDIA_TEXT;
			encoded.push_back(fmt);
			kinds.push_back(kTrackText);
			streamIndexes.push_back(i);
		}
	}

	// Overwrite decoded/raw members from the negotiated sink caps so
	// ReadFrames and DecodedFormat report what the decoder will emit.
	void _FillDecodedFromSinks(std::vector<BMediaFormat>& encoded,
		const std::vector<int32>& kinds)
	{
		for (size_t i = 0; i < encoded.size(); i++) {
			if (kinds[i] == kTrackAudio && fAppSink != NULL) {
				GstPad* pad = gst_element_get_static_pad(fAppSink, "sink");
				if (pad == NULL)
					continue;
				GstCaps* negotiated = gst_pad_get_current_caps(pad);
				gst_object_unref(pad);
				if (negotiated == NULL)
					continue;
				BMediaFormat raw;
				if (GStreamerBackend::GstCapsToRawAudio(negotiated, &raw)) {
					if (encoded[i].IsEncodedAudio())
						encoded[i].format.u.encoded_audio.output
							= raw.format.u.raw_audio;
				}
				gst_caps_unref(negotiated);
			} else if (kinds[i] == kTrackVideo && fVideoSink != NULL) {
				GstPad* pad = gst_element_get_static_pad(fVideoSink, "sink");
				if (pad == NULL)
					continue;
				GstCaps* negotiated = gst_pad_get_current_caps(pad);
				gst_object_unref(pad);
				if (negotiated == NULL)
					continue;
				BMediaFormat raw;
				if (GStreamerBackend::GstCapsToRawVideo(negotiated, &raw)) {
					if (encoded[i].IsEncodedVideo())
						encoded[i].format.u.encoded_video.output
							= raw.format.u.raw_video;
				}
				gst_caps_unref(negotiated);
			}
		}
	}

	GstElement*		fPipeline;
	GstElement*		fAppSink;
	GstElement*		fVideoSink;
	GstElement*		fAppSrc;
	std::vector<BMediaTrack*>	fTracks;
	status_t		fInitErr;
	bool			fWriteMode;
	const EncodeFileFormat*	fFormatInfo;
	int32			fCodecType;
	std::string		fOutputPath;
	gint64			fDurationNs;
	DataIOSource	fSource;
	bool			fHasSource;
};


// #pragma mark - BMediaFile public surface


BMediaFile::BMediaFile(const entry_ref* ref)
	:
	fImpl(new(std::nothrow) Impl())
{
	if (fImpl == NULL)
		return;
	fImpl->fInitErr = fImpl->Open(ref);
}


BMediaFile::BMediaFile(const entry_ref* ref, int32 /*flags*/)
	:
	fImpl(new(std::nothrow) Impl())
{
	if (fImpl == NULL)
		return;
	fImpl->fInitErr = fImpl->Open(ref);
}


BMediaFile::BMediaFile(BDataIO* source, int32 /*flags*/)
	:
	fImpl(new(std::nothrow) Impl())
{
	if (fImpl == NULL)
		return;
	fImpl->fInitErr = fImpl->OpenIO(source);
}


BMediaFile::BMediaFile(const BUrl& url)
	:
	fImpl(new(std::nothrow) Impl())
{
	if (fImpl == NULL)
		return;
	fImpl->fInitErr = fImpl->OpenUri(url.UrlString().String());
}


BMediaFile::BMediaFile(const entry_ref* outputRef,
	const media_file_format* fileFormat, int32 /*flags*/)
	:
	fImpl(new(std::nothrow) Impl())
{
	if (fImpl == NULL)
		return;
	fImpl->fInitErr = fImpl->OpenWrite(outputRef, fileFormat);
}


BMediaTrack*
BMediaFile::CreateTrack(const BMediaFormat& outputFormat)
{
	return fImpl != NULL ? fImpl->CreateTrack(outputFormat) : NULL;
}


BMediaTrack*
BMediaFile::CreateTrack(media_format* mf, const media_codec_info* codecInfo,
	uint32 /*flags*/)
{
	if (mf == NULL)
		return NULL;
	// Wrap the legacy media_format in a BMediaFormat; if a codec is supplied,
	// promote audio/video raw to encoded and set encoding to codecInfo->id.
	BMediaFormat fmt;
	fmt.format = *mf;
	if (codecInfo != NULL) {
		if (fmt.format.type == B_MEDIA_RAW_AUDIO) {
			media_encoded_audio_format ea = {};
			ea.output   = fmt.format.u.raw_audio;
			ea.encoding = codecInfo->id;
			fmt.format.type = B_MEDIA_ENCODED_AUDIO;
			fmt.format.u.encoded_audio = ea;
		} else if (fmt.format.type == B_MEDIA_RAW_VIDEO) {
			media_encoded_video_format ev = {};
			ev.output   = fmt.format.u.raw_video;
			ev.encoding = codecInfo->id;
			fmt.format.type = B_MEDIA_ENCODED_VIDEO;
			fmt.format.u.encoded_video = ev;
		} else if (fmt.format.type == B_MEDIA_ENCODED_AUDIO) {
			fmt.format.u.encoded_audio.encoding = codecInfo->id;
		} else if (fmt.format.type == B_MEDIA_ENCODED_VIDEO) {
			fmt.format.u.encoded_video.encoding = codecInfo->id;
		}
	}
	return CreateTrack(fmt);
}


status_t
BMediaFile::CommitHeader()
{
	return fImpl != NULL ? fImpl->CommitHeader() : B_NO_INIT;
}


status_t
BMediaFile::CloseFile()
{
	return fImpl != NULL ? fImpl->CloseFile() : B_OK;
}


BMediaFile::~BMediaFile()
{
	delete fImpl;
}


status_t
BMediaFile::InitCheck() const
{
	return fImpl != NULL ? fImpl->fInitErr : B_NO_MEMORY;
}


int32
BMediaFile::CountTracks() const
{
	if (fImpl == NULL || fImpl->fInitErr != B_OK)
		return 0;
	return (int32)fImpl->fTracks.size();
}


BMediaTrack*
BMediaFile::TrackAt(int32 index)
{
	if (fImpl == NULL || fImpl->fInitErr != B_OK)
		return NULL;
	if (index < 0 || (size_t)index >= fImpl->fTracks.size())
		return NULL;
	return fImpl->fTracks[index];
}


status_t
BMediaFile::ReleaseTrack(BMediaTrack* track)
{
	// Tracks are owned by BMediaFile; this validates and returns.
	if (fImpl == NULL || track == NULL)
		return B_BAD_VALUE;
	if (!fImpl->OwnsTrack(track))
		return B_BAD_VALUE;
	return B_OK;
}


// #pragma mark - BMediaTrack public surface


BMediaTrack::BMediaTrack()
	:
	fImpl(new(std::nothrow) Impl())
{
}


BMediaTrack::~BMediaTrack()
{
	delete fImpl;
}


status_t
BMediaTrack::InitCheck() const
{
	return fImpl != NULL ? fImpl->fInitErr : B_NO_MEMORY;
}


status_t
BMediaTrack::DecodedFormat(BMediaFormat* format)
{
	if (format == NULL || fImpl == NULL || fImpl->fInitErr != B_OK)
		return B_BAD_VALUE;
	if (fImpl->fKind == kTrackText)
		return B_NOT_SUPPORTED;
	// Report the raw side like the legacy overload; encoded view is EncodedFormat.
	media_format raw;
	status_t err = DecodedFormat(&raw, 0);
	if (err != B_OK)
		return err;
	format->format = raw;
	return B_OK;
}


status_t
BMediaTrack::DecodedFormat(media_format* format, uint32 /*flags*/)
{
	// Media Kit 1.0 shape: caller supplies a raw type as a hint; we fill
	// in what the decoder will actually emit. flags are accepted and
	// ignored; the GStreamer path has no negotiation flags to honour.
	if (format == NULL || fImpl == NULL || fImpl->fInitErr != B_OK)
		return B_BAD_VALUE;
	if (fImpl->fKind == kTrackText)
		return B_NOT_SUPPORTED;
	if (fImpl->fFormat.IsRawAudio() || fImpl->fFormat.IsRawVideo()) {
		*format = fImpl->fFormat.format;
		return B_OK;
	}
	// Track still holds the encoded view only; synthesise the raw side
	// from the encoded output parameters.
	if (fImpl->fEncodedFormat.IsEncodedAudio()) {
		format->type = B_MEDIA_RAW_AUDIO;
		format->u.raw_audio = fImpl->fEncodedFormat.format.u.encoded_audio.output;
		return B_OK;
	}
	if (fImpl->fEncodedFormat.IsEncodedVideo()) {
		format->type = B_MEDIA_RAW_VIDEO;
		format->u.raw_video = fImpl->fEncodedFormat.format.u.encoded_video.output;
		return B_OK;
	}
	return B_ERROR;
}


status_t
BMediaTrack::EncodedFormat(BMediaFormat* format) const
{
	// Forwarding through const_cast picked this same overload again and
	// recursed until the stack overflowed.
	if (format == NULL || fImpl == NULL || fImpl->fInitErr != B_OK)
		return B_BAD_VALUE;
	if (fImpl->fKind == kTrackText)
		return B_NOT_SUPPORTED;
	*format = fImpl->fEncodedFormat;
	return B_OK;
}


status_t
BMediaTrack::EncodedFormat(media_format* format) const
{
	// Report the demuxed/encoded view. playbin sits after decoding, so
	// codec id stays UNKNOWN; the raw output parameters still describe
	// the track for callers probing type, rate and channel count.
	if (format == NULL || fImpl == NULL || fImpl->fInitErr != B_OK)
		return B_BAD_VALUE;
	if (fImpl->fKind == kTrackText) {
		format->Clear();
		format->type = B_MEDIA_TEXT;
		return B_OK;
	}
	if (!fImpl->fEncodedFormat.IsEncodedAudio()
			&& !fImpl->fEncodedFormat.IsEncodedVideo()) {
		// Write-mode tracks store the wrapped encoded format; read-mode
		// discovery always sets an encoded type. Anything else is an error.
		if (fImpl->fFormat.IsRawAudio()) {
			format->Clear();
			format->type = B_MEDIA_ENCODED_AUDIO;
			format->u.encoded_audio.output
				= fImpl->fFormat.format.u.raw_audio;
			format->u.encoded_audio.encoding = B_CODEC_TYPE_UNKNOWN;
			return B_OK;
		}
		return B_ERROR;
	}
	*format = fImpl->fEncodedFormat.format;
	return B_OK;
}


int64
BMediaTrack::CountFrames() const
{
	if (fImpl == NULL)
		return 0;
	if (fImpl->fKind == kTrackText)
		return 0;
	if (fImpl->fPipeline == NULL)
		return 0;
	gint64 dur = 0;
	if (!gst_element_query_duration(fImpl->fPipeline, GST_FORMAT_TIME, &dur))
		return 0;
	if (fImpl->fKind == kTrackVideo) {
		const media_raw_video_format& rv
			= fImpl->fFormat.format.u.raw_video;
		// If DecodedFormat has not been filled, fall back to encoded view.
		float rate = rv.field_rate;
		if (rate <= 0.0f && fImpl->fEncodedFormat.IsEncodedVideo())
			rate = fImpl->fEncodedFormat.format.u.encoded_video.output
				.field_rate;
		if (rate <= 0.0f)
			return 0;
		return (int64)((dur * (gint64)(rate + 0.5f)) / GST_SECOND);
	}
	const media_raw_audio_format& raw = fImpl->fFormat.format.u.raw_audio;
	float rate = raw.frame_rate;
	if (rate <= 0.0f && fImpl->fEncodedFormat.IsEncodedAudio())
		rate = fImpl->fEncodedFormat.format.u.encoded_audio.output.frame_rate;
	if (rate <= 0.0f)
		return 0;
	return (int64)((dur * (gint64)rate) / GST_SECOND);
}


bigtime_t
BMediaTrack::Duration() const
{
	if (fImpl == NULL || fImpl->fPipeline == NULL)
		return 0;
	gint64 dur = 0;
	if (!gst_element_query_duration(fImpl->fPipeline, GST_FORMAT_TIME, &dur))
		return 0;
	return (bigtime_t)(dur / 1000);	// ns -> µs
}


bigtime_t
BMediaTrack::CurrentTime() const
{
	if (fImpl == NULL || fImpl->fPipeline == NULL)
		return 0;
	gint64 pos = 0;
	if (!gst_element_query_position(fImpl->fPipeline, GST_FORMAT_TIME, &pos))
		return 0;
	return (bigtime_t)(pos / 1000);	// ns -> µs
}


int64
BMediaTrack::CurrentFrame() const
{
	if (fImpl == NULL)
		return 0;
	if (fImpl->fKind == kTrackVideo) {
		const media_raw_video_format& rv
			= fImpl->fFormat.format.u.raw_video;
		float rate = rv.field_rate;
		if (rate <= 0.0f)
			return 0;
		return (int64)((CurrentTime() * (int64)(rate + 0.5f)) / 1000000LL);
	}
	const media_raw_audio_format& raw = fImpl->fFormat.format.u.raw_audio;
	if (raw.frame_rate <= 0.0f)
		return 0;
	return (int64)((CurrentTime() * (int64)raw.frame_rate) / 1000000LL);
}


status_t
BMediaTrack::ReadFrames(void* buffer, int64* outFrameCount, media_header* mh)
{
	if (buffer == NULL || outFrameCount == NULL
			|| fImpl == NULL || fImpl->fAppSink == NULL) {
		return B_BAD_VALUE;
	}
	if (fImpl->fKind == kTrackText)
		return B_NOT_SUPPORTED;
	if (fImpl->fKind == kTrackAudio || fImpl->fKind == kTrackVideo) {
		// Point playbin at this track's demuxed branch before pulling.
		if (fImpl->fPlayBin != NULL) {
			if (fImpl->fKind == kTrackAudio) {
				g_object_set(fImpl->fPlayBin, "current-audio",
					fImpl->fStreamIndex, NULL);
			} else {
				g_object_set(fImpl->fPlayBin, "current-video",
					fImpl->fStreamIndex, NULL);
			}
		}
	}
	const int64 requested = *outFrameCount;
	*outFrameCount = 0;
	if (requested <= 0)
		return B_BAD_VALUE;

	if (fImpl->fPlayBin != NULL) {
		GstElement* vsink = NULL;
		g_object_get(fImpl->fPlayBin, "video-sink", &vsink, NULL);
		if (vsink != NULL) {
			if (fImpl->fKind == kTrackVideo) {
				g_object_set_data(G_OBJECT(vsink), "vos-read",
					GINT_TO_POINTER(1));
			} else if (g_object_get_data(G_OBJECT(vsink), "vos-read")
					== NULL) {
				// Nobody pulls video: after 3s of audio, drop instead of
				// letting the full video sink block the audio branch.
				gint64 now = g_get_monotonic_time();
				gpointer first = g_object_get_data(G_OBJECT(vsink),
					"vos-audio-first");
				if (first == NULL) {
					g_object_set_data(G_OBJECT(vsink), "vos-audio-first",
						GSIZE_TO_POINTER((gsize)now));
				} else if (now - (gint64)(gsize)first > 3000000) {
					g_object_set(vsink, "drop", TRUE, NULL);
				}
			}
			gst_object_unref(vsink);
		}
	}

	if (fImpl->fKind != kTrackVideo
		&& fImpl->fCarryOffset < fImpl->fCarry.size()) {
		const media_raw_audio_format& raw = fImpl->fFormat.format.u.raw_audio;
		const uint32 stride = (raw.format
			& media_raw_audio_format::B_AUDIO_SIZE_MASK) * raw.channel_count;
		const int64 framesAvail = stride > 0
			? (int64)((fImpl->fCarry.size() - fImpl->fCarryOffset) / stride) : 0;
		if (framesAvail > 0) {
			const int64 frames = framesAvail < requested ? framesAvail : requested;
			memcpy(buffer, &fImpl->fCarry[fImpl->fCarryOffset], frames * stride);
			fImpl->fCarryOffset += frames * stride;
			*outFrameCount = frames;
			if (mh != NULL) {
				memset(mh, 0, sizeof(*mh));
				mh->size_used = (uint32)(frames * stride);
			}
			return B_OK;
		}
	}
	fImpl->fCarry.clear();
	fImpl->fCarryOffset = 0;

	GstAppSink* sink = GST_APP_SINK(fImpl->fAppSink);
	// A seek through a sibling track on the shared pipeline clears the
	// sink's EOS without touching this track's flag.
	if (fImpl->fEOS && !gst_app_sink_is_eos(sink))
		fImpl->fEOS = false;
	if (fImpl->fEOS)
		return B_LAST_BUFFER_ERROR;

	// Video is pulled from window threads: wait briefly, never forever. A
	// frame that is not decoded yet is picked up on the next tick.
	GstSample* sample = fImpl->fKind == kTrackVideo
		? gst_app_sink_try_pull_sample(sink, 20 * GST_MSECOND)
		: gst_app_sink_pull_sample(sink);
	if (sample == NULL) {
		if (gst_app_sink_is_eos(sink)) {
			fImpl->fEOS = true;
			return B_LAST_BUFFER_ERROR;
		}
		return fImpl->fKind == kTrackVideo ? B_WOULD_BLOCK : B_ERROR;
	}

	GstBuffer* gb = gst_sample_get_buffer(sample);
	GstMapInfo map;
	if (gb == NULL || !gst_buffer_map(gb, &map, GST_MAP_READ)) {
		gst_sample_unref(sample);
		return B_ERROR;
	}

	size_t frameBytes = 0;
	if (fImpl->fKind == kTrackVideo) {
		const media_raw_video_format& rv
			= fImpl->fFormat.format.u.raw_video;
		uint32 bpp = 4;	// BGRx appsink; matches B_RGB32 display layout
		frameBytes = (size_t)rv.width * rv.height * bpp;
		if (frameBytes == 0) {
			gst_buffer_unmap(gb, &map);
			gst_sample_unref(sample);
			return B_ERROR;
		}
		int64 framesAvail = (int64)(map.size / frameBytes);
		int64 framesToCopy = framesAvail < requested ? framesAvail : requested;
		memcpy(buffer, map.data, framesToCopy * frameBytes);
		*outFrameCount = framesToCopy;
		if (mh != NULL) {
			memset(mh, 0, sizeof(*mh));
			if (GST_BUFFER_PTS_IS_VALID(gb))
				mh->start_time = (bigtime_t)(GST_BUFFER_PTS(gb) / 1000);
			mh->size_used = (uint32)(framesToCopy * frameBytes);
			mh->u.video.field_flags = 0;
		}
	} else {
		const media_raw_audio_format& raw
			= fImpl->fFormat.format.u.raw_audio;
		const uint32 stride = (raw.format
			& media_raw_audio_format::B_AUDIO_SIZE_MASK)
			* raw.channel_count;
		int64 framesAvail = stride > 0 ? (int64)(map.size / stride) : 0;
		int64 framesToCopy = framesAvail < requested ? framesAvail : requested;
		const size_t bytes = framesToCopy * stride;
		memcpy(buffer, map.data, bytes);
		if (framesAvail > framesToCopy) {
			fImpl->fCarry.assign(map.data + bytes,
				map.data + framesAvail * stride);
			fImpl->fCarryOffset = 0;
		}
		*outFrameCount = framesToCopy;
		if (mh != NULL) {
			memset(mh, 0, sizeof(*mh));
			if (GST_BUFFER_PTS_IS_VALID(gb))
				mh->start_time = (bigtime_t)(GST_BUFFER_PTS(gb) / 1000);
			mh->size_used = (uint32)bytes;
		}
	}

	gst_buffer_unmap(gb, &map);
	gst_sample_unref(sample);
	return B_OK;
}


status_t
BMediaTrack::SeekToTime(bigtime_t* inOutTime, int32 flags)
{
	if (inOutTime == NULL || fImpl == NULL || fImpl->fPipeline == NULL)
		return B_BAD_VALUE;
	if (fImpl->fKind == kTrackText)
		return B_NOT_SUPPORTED;
	if (fImpl->fWriteMode)
		return B_NOT_ALLOWED;

	// FLUSH drops queued pre-seek buffers so no stale frame or audio
	// burst follows the seek. Key-unit stops on the key frame for fast
	// scrubbing; accurate refines onto the exact time so playback and
	// paused seeks land at the right frame, not the previous key frame.
	GstSeekFlags seekFlags = (GstSeekFlags)(GST_SEEK_FLAG_FLUSH
		| GST_SEEK_FLAG_ACCURATE);
	if ((flags & B_MEDIA_SEEK_KEY_UNIT) != 0)
		seekFlags = (GstSeekFlags)(GST_SEEK_FLAG_FLUSH
			| GST_SEEK_FLAG_KEY_UNIT);

	const gint64 ns = (gint64)*inOutTime * 1000;
	if (!gst_element_seek_simple(fImpl->fPipeline, GST_FORMAT_TIME,
			seekFlags, ns)) {
		return B_ERROR;
	}
	fImpl->fEOS = false;
	fImpl->fCarry.clear();
	fImpl->fCarryOffset = 0;
	*inOutTime = (bigtime_t)(ns / 1000);
	return B_OK;
}


status_t
BMediaTrack::SeekToFrame(int64* inOutFrame, int32 flags)
{
	if (inOutFrame == NULL || fImpl == NULL)
		return B_BAD_VALUE;
	if (fImpl->fKind == kTrackVideo) {
		const media_raw_video_format& rv
			= fImpl->fFormat.format.u.raw_video;
		float rate = rv.field_rate;
		if (rate <= 0.0f)
			return B_BAD_VALUE;
		bigtime_t t = (bigtime_t)((*inOutFrame * 1000000LL)
			/ (int64)(rate + 0.5f));
		status_t err = SeekToTime(&t, flags);
		if (err != B_OK)
			return err;
		*inOutFrame = (int64)((t * (int64)(rate + 0.5f)) / 1000000LL);
		return B_OK;
	}
	const media_raw_audio_format& raw = fImpl->fFormat.format.u.raw_audio;
	if (raw.frame_rate <= 0.0f)
		return B_BAD_VALUE;
	bigtime_t t = (bigtime_t)((*inOutFrame * 1000000LL) / (int64)raw.frame_rate);
	status_t err = SeekToTime(&t, flags);
	if (err != B_OK)
		return err;
	*inOutFrame = (int64)((t * (int64)raw.frame_rate) / 1000000LL);
	return B_OK;
}


status_t
BMediaTrack::WriteFrames(const void* buffer, int64 frameCount, int32 /*flags*/)
{
	if (buffer == NULL || frameCount <= 0
			|| fImpl == NULL || !fImpl->fWriteMode || fImpl->fAppSrc == NULL) {
		return B_BAD_VALUE;
	}

	const size_t bytes = (size_t)frameCount * fImpl->fFrameStride;

	// The previous chunk goes out first so ReplaceFrames can still swap
	// the chunk the caller most recently wrote.
	status_t err = fImpl->PushPending();
	if (err != B_OK)
		return err;

	fImpl->fPending.assign((const uint8*)buffer, (const uint8*)buffer + bytes);
	fImpl->fPendingFrames = frameCount;
	return B_OK;
}


status_t
BMediaTrack::ReplaceFrames(const void* buffer, int64 frameCount, int32 /*flags*/)
{
	if (buffer == NULL || frameCount <= 0 || fImpl == NULL
			|| !fImpl->fWriteMode || fImpl->fPending.empty()) {
		return B_BAD_VALUE;
	}

	const size_t bytes = (size_t)frameCount * fImpl->fFrameStride;
	fImpl->fPending.assign((const uint8*)buffer, (const uint8*)buffer + bytes);
	fImpl->fPendingFrames = frameCount;
	return B_OK;
}


status_t
BMediaTrack::GetCodecInfo(media_codec_info* info) const
{
	if (info == NULL || fImpl == NULL)
		return B_BAD_VALUE;
	memset(info, 0, sizeof(*info));
	int32 encoding = B_CODEC_TYPE_UNKNOWN;
	if (fImpl->fEncodedFormat.format.type == B_MEDIA_ENCODED_AUDIO)
		encoding = fImpl->fEncodedFormat.format.u.encoded_audio.encoding;
	else if (fImpl->fEncodedFormat.format.type == B_MEDIA_ENCODED_VIDEO)
		encoding = fImpl->fEncodedFormat.format.u.encoded_video.encoding;
	if (fImpl->fWriteMode)
		encoding = fImpl->fCodecId;
	info->id = encoding;
	const EncodeCodecInfo* codec = fImpl->fWriteMode
		? FindEncodeCodec(encoding) : NULL;
	if (codec != NULL) {
		snprintf(info->short_name, sizeof(info->short_name), "%s",
			codec->factory != NULL ? codec->factory : "raw");
		snprintf(info->pretty_name, sizeof(info->pretty_name), "%s",
			codec->srcCaps);
		return B_OK;
	}
	if (fImpl->fKind == kTrackVideo) {
		snprintf(info->short_name, sizeof(info->short_name), "video");
		snprintf(info->pretty_name, sizeof(info->pretty_name),
			"Video track");
	} else if (fImpl->fKind == kTrackText) {
		snprintf(info->short_name, sizeof(info->short_name), "text");
		snprintf(info->pretty_name, sizeof(info->pretty_name),
			"Subtitle track");
	} else {
		snprintf(info->short_name, sizeof(info->short_name), "codec-%d",
			(int)encoding);
		snprintf(info->pretty_name, sizeof(info->pretty_name), "Codec %d",
			(int)encoding);
	}
	return B_OK;
}


status_t
BMediaTrack::SetQuality(float quality)
{
	if (fImpl == NULL || !fImpl->fWriteMode)
		return B_NO_INIT;
	if (quality < 0.0f)
		quality = 0.0f;
	if (quality > 1.0f)
		quality = 1.0f;
	fImpl->fQuality = quality;
	// The encoder element exists once caps have negotiated; before the
	// first push this is a no-op and PushPending applies it later.
	if (fImpl->fPipeline != NULL)
		ApplyEncodeQuality(fImpl->fPipeline, fImpl->fCodecId, quality);
	return B_OK;
}


status_t
BMediaTrack::Flush()
{
	if (fImpl == NULL)
		return B_NO_INIT;
	if (!fImpl->fWriteMode)
		return fImpl->fPipeline != NULL ? B_OK : B_NO_INIT;
	return fImpl->PushPending();
}


BView*
BMediaTrack::GetParameterView()
{
	// Codec-parameter UI not surfaced from GStreamer (the underlying encoder's
	// properties vary too widely to expose generically).
	return NULL;
}
