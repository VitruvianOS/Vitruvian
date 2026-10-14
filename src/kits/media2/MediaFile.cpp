/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaFile.h>
#include <media2/MediaTrack.h>

#include <new>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include <Entry.h>
#include <Path.h>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/pbutils/gstdiscoverer.h>

#include "GStreamerBackend.h"


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
		fNextPts(0)
	{
	}

	GstElement*		fAppSink;	// read mode  (borrowed; owned by pipeline)
	GstElement*		fAppSrc;	// write mode (borrowed)
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
};


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
		fCodecType(0),
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
	status_t OpenUri(const char* uri)
	{
		if (uri == NULL)
			return B_BAD_VALUE;

		if (GStreamerBackend::GetInstance() == NULL)
			return B_NO_INIT;

		std::vector<BMediaFormat> encoded;
		std::vector<int32> kinds;
		std::vector<int32> streamIndexes;
		status_t discErr = _DiscoverStreams(uri, encoded, kinds,
			streamIndexes, &fDurationNs);
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

	status_t OpenWrite(const entry_ref* ref, int32 codecType)
	{
		if (ref == NULL)
			return B_BAD_VALUE;
		BPath path(ref);
		if (path.InitCheck() != B_OK)
			return path.InitCheck();
		if (GStreamerBackend::GetInstance() == NULL)
			return B_NO_INIT;
		fOutputPath = path.Path();
		fCodecType  = codecType;
		fWriteMode  = true;
		fInitErr    = B_OK;
		return B_OK;
	}

	BMediaTrack* CreateTrack(const BMediaFormat& outputFormat)
	{
		if (!fWriteMode || !fTracks.empty())
			return NULL;

		BMediaFormat encFmt = outputFormat;
		if (!encFmt.IsEncodedAudio()) {
			// Caller supplied a raw format — wrap it as encoded with our codec.
			BMediaFormat wrap;
			wrap.format.type = B_MEDIA_ENCODED_AUDIO;
			media_encoded_audio_format& enc = wrap.format.u.encoded_audio;
			memset(&enc, 0, sizeof(enc));
			enc.encoding = fCodecType;
			if (outputFormat.IsRawAudio())
				enc.output = outputFormat.format.u.raw_audio;
			encFmt = wrap;
		} else {
			encFmt.format.u.encoded_audio.encoding = fCodecType;
		}

		fPipeline = GStreamerBackend::GetInstance()->CreateEncodePipeline(
			fOutputPath.c_str(), encFmt);
		if (fPipeline == NULL)
			return NULL;

		fAppSrc = gst_bin_get_by_name(GST_BIN(fPipeline), "src");
		if (fAppSrc == NULL) {
			gst_element_set_state(fPipeline, GST_STATE_NULL);
			gst_object_unref(fPipeline);
			fPipeline = NULL;
			return NULL;
		}
		// gst_bin_get_by_name returns a new ref; we want a borrowed ref to
		// match the read-mode pattern. Drop the extra ref but keep the ptr —
		// the bin still owns it.
		gst_object_unref(fAppSrc);

		// Configure appsrc raw input caps so encodebin knows what's coming in.
		BMediaFormat rawIn;
		if (outputFormat.IsRawAudio()) {
			rawIn = outputFormat;
		} else {
			rawIn.format.type = B_MEDIA_RAW_AUDIO;
			rawIn.format.u.raw_audio = encFmt.format.u.encoded_audio.output;
			// Defaults to interleaved + default positions; matches our
			// CommitHeader expectations.
			rawIn.is_planar = false;
		}
		GstCaps* inCaps = GStreamerBackend::RawAudioToGstCaps(rawIn);
		if (inCaps != NULL) {
			g_object_set(fAppSrc, "caps", inCaps,
				"format", GST_FORMAT_TIME,
				"is-live", FALSE,
				NULL);
			gst_caps_unref(inCaps);
			GStreamerBackend::LinkEncodeSource(fPipeline);
		}

		BMediaTrack* track = new(std::nothrow) BMediaTrack();
		if (track == NULL)
			return NULL;
		track->fImpl->fAppSrc    = fAppSrc;
		track->fImpl->fPipeline  = fPipeline;
		track->fImpl->fFormat    = rawIn;
		track->fImpl->fEncodedFormat = encFmt;
		track->fImpl->fWriteMode = true;
		track->fImpl->fKind      = kTrackAudio;
		const media_raw_audio_format& raw = rawIn.format.u.raw_audio;
		track->fImpl->fFrameStride = (raw.format
			& media_raw_audio_format::B_AUDIO_SIZE_MASK) * raw.channel_count;
		track->fImpl->fInitErr   = B_OK;
		fTracks.push_back(track);
		return track;
	}

	status_t CommitHeader()
	{
		if (!fWriteMode || fPipeline == NULL)
			return B_NO_INIT;
		GstStateChangeReturn r = gst_element_set_state(fPipeline,
			GST_STATE_PLAYING);
		if (r == GST_STATE_CHANGE_FAILURE)
			return B_ERROR;
		return B_OK;
	}

	status_t CloseFile()
	{
		if (!fWriteMode || fPipeline == NULL)
			return B_OK;
		if (fAppSrc != NULL) {
			gst_app_src_end_of_stream(GST_APP_SRC(fAppSrc));
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
					GST_DISCOVERER_VIDEO_INFO(si));
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

	static BMediaFormat _VideoFormatFromDiscoverer(GstDiscovererVideoInfo* vi)
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
		raw.orientation = B_VIDEO_TOP_LEFT_RIGHT;
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
			ev.output.orientation = B_VIDEO_TOP_LEFT_RIGHT;
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
	int32			fCodecType;
	std::string		fOutputPath;
	gint64			fDurationNs;
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


BMediaFile::BMediaFile(const BUrl& url)
	:
	fImpl(new(std::nothrow) Impl())
{
	if (fImpl == NULL)
		return;
	fImpl->fInitErr = fImpl->OpenUri(url.UrlString().String());
}


static int32
CodecTypeFromFileFormat(const media_file_format* f)
{
	if (f == NULL)
		return B_CODEC_TYPE_UNKNOWN;
	// Match on mime_type first, then short_name; same table as
	// MediaFormats.cpp's MimeToCodecType but the file_format's mime
	// is the container mime, so it lines up.
	const char* mime = f->mime_type;
	const char* sn   = f->short_name;
	if ((mime && strstr(mime, "audio/x-wav"))
		|| (sn && (strstr(sn, "wav") || strstr(sn, "WAV"))))
		return B_CODEC_TYPE_WAV;
	if (mime && strstr(mime, "application/ogg"))
		return B_CODEC_TYPE_OGG;
	if (mime && strstr(mime, "audio/x-flac"))
		return B_CODEC_TYPE_FLAC;
	if (mime && strstr(mime, "audio/mpeg"))
		return B_CODEC_TYPE_MP3;
	if (mime && (strstr(mime, "video/quicktime")
			|| strstr(mime, "video/mp4")))
		return B_CODEC_TYPE_AAC;
	if (mime && strstr(mime, "audio/x-opus"))
		return B_CODEC_TYPE_OPUS;
	return B_CODEC_TYPE_WAV;	// safe default
}


BMediaFile::BMediaFile(const entry_ref* outputRef,
	const media_file_format* fileFormat, int32 /*flags*/)
	:
	fImpl(new(std::nothrow) Impl())
{
	if (fImpl == NULL)
		return;
	fImpl->fInitErr = fImpl->OpenWrite(outputRef,
		CodecTypeFromFileFormat(fileFormat));
}


BMediaTrack*
BMediaFile::CreateTrack(const BMediaFormat& outputFormat)
{
	return fImpl != NULL ? fImpl->CreateTrack(outputFormat) : NULL;
}


BMediaTrack*
BMediaFile::CreateTrack(media_format* mf, const media_codec_info* codecInfo)
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
		format->format.Clear();
		format->format.type = B_MEDIA_TEXT;
		return B_OK;
	}
	if (!fImpl->fEncodedFormat.IsEncodedAudio()
			&& !fImpl->fEncodedFormat.IsEncodedVideo()) {
		// Write-mode tracks store the wrapped encoded format; read-mode
		// discovery always sets an encoded type. Anything else is an error.
		if (fImpl->fFormat.IsRawAudio()) {
			*format = BMediaFormat();
			format->format.type = B_MEDIA_ENCODED_AUDIO;
			format->format.u.encoded_audio.output
				= fImpl->fFormat.format.u.raw_audio;
			format->format.u.encoded_audio.encoding = B_CODEC_TYPE_UNKNOWN;
			return B_OK;
		}
		return B_ERROR;
	}
	*format = fImpl->fEncodedFormat;
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

	if (fImpl->fEOS)
		return B_LAST_BUFFER_ERROR;

	GstAppSink* sink = GST_APP_SINK(fImpl->fAppSink);
	GstSample* sample = gst_app_sink_pull_sample(sink);
	if (sample == NULL) {
		if (gst_app_sink_is_eos(sink)) {
			fImpl->fEOS = true;
			return B_LAST_BUFFER_ERROR;
		}
		return B_ERROR;
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
BMediaTrack::SeekToTime(bigtime_t* inOutTime, int32 /*flags*/)
{
	if (inOutTime == NULL || fImpl == NULL || fImpl->fPipeline == NULL)
		return B_BAD_VALUE;
	if (fImpl->fKind == kTrackText)
		return B_NOT_SUPPORTED;
	const gint64 ns = (gint64)*inOutTime * 1000;
	if (!gst_element_seek_simple(fImpl->fPipeline, GST_FORMAT_TIME,
			(GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT), ns)) {
		return B_ERROR;
	}
	fImpl->fEOS = false;
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
		return SeekToTime(&t, flags);
	}
	const media_raw_audio_format& raw = fImpl->fFormat.format.u.raw_audio;
	if (raw.frame_rate <= 0.0f)
		return B_BAD_VALUE;
	bigtime_t t = (bigtime_t)((*inOutFrame * 1000000LL) / (int64)raw.frame_rate);
	return SeekToTime(&t, flags);
}


status_t
BMediaTrack::WriteFrames(const void* buffer, int64 frameCount,
	const media_header* /*mh*/)
{
	if (buffer == NULL || frameCount <= 0
			|| fImpl == NULL || !fImpl->fWriteMode || fImpl->fAppSrc == NULL) {
		return B_BAD_VALUE;
	}

	const media_raw_audio_format& raw = fImpl->fFormat.format.u.raw_audio;
	const size_t bytes = (size_t)frameCount * fImpl->fFrameStride;

	GstBuffer* gb = gst_buffer_new_allocate(NULL, bytes, NULL);
	if (gb == NULL)
		return B_NO_MEMORY;
	GstMapInfo map;
	if (!gst_buffer_map(gb, &map, GST_MAP_WRITE)) {
		gst_buffer_unref(gb);
		return B_ERROR;
	}
	memcpy(map.data, buffer, bytes);
	gst_buffer_unmap(gb, &map);

	if (raw.frame_rate > 0.0f) {
		const uint64 durationNs = (uint64)((frameCount * GST_SECOND)
			/ (int64)raw.frame_rate);
		GST_BUFFER_PTS(gb)      = fImpl->fNextPts;
		GST_BUFFER_DURATION(gb) = durationNs;
		fImpl->fNextPts += durationNs;
	}

	GstFlowReturn r = gst_app_src_push_buffer(GST_APP_SRC(fImpl->fAppSrc), gb);
		// push_buffer takes ownership of gb on success
	if (r != GST_FLOW_OK)
		return B_ERROR;
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
	info->id = encoding;
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
BMediaTrack::SetQuality(float /*quality*/)
{
	// Encoding profile already carries quality via the GstEncodingProfile API.
	// Per-track runtime quality changes aren't surfaced by GStreamer's encodebin
	// for typical audio encoders; returning B_OK keeps legacy callers happy.
	return B_OK;
}


status_t
BMediaTrack::Flush()
{
	if (fImpl == NULL || fImpl->fPipeline == NULL)
		return B_NO_INIT;
	return B_OK;
}


BView*
BMediaTrack::GetParameterView()
{
	// Codec-parameter UI not surfaced from GStreamer (the underlying encoder's
	// properties vary too widely to expose generically).
	return NULL;
}
