/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "FilterUnitCore.h"

#include <new>
#include <stdio.h>
#include <string.h>
#include <vector>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

#include <media2/MediaDefs.h>


namespace BPrivate { namespace media {


struct FilterUnitCore::Parameter {
	BString		name;
	GObject*	object;
	BString		property;
	float		min;
	float		max;
	bool		isInteger;
};


struct FilterUnitCore::Impl {
	BString					name;
	ForwardFunc				forward;
	void*					forwardCookie;
	media_type_mask			acceptedTypes;
	status_t				initErr;

	GstElement*				pipeline;
	GstElement*				appSrc;
	GstElement*				appSink;
	bool					capsSet;
	BMediaFormat			format;

	std::vector<Parameter>	params;

			void				CollectParameters(GstElement* element);
};


static bool
_IsHelperElement(GstElement* element)
{
	GstElementFactory* factory = gst_element_get_factory(element);
	if (factory == NULL)
		return true;

	const char* name = gst_plugin_feature_get_name(
		GST_PLUGIN_FEATURE(factory));
	if (name == NULL)
		return true;

	return strcmp(name, "appsrc") == 0 || strcmp(name, "appsink") == 0
		|| strcmp(name, "videoconvert") == 0
		|| strcmp(name, "audioconvert") == 0
		|| strcmp(name, "audioresample") == 0
		|| strcmp(name, "capsfilter") == 0 || strcmp(name, "queue") == 0;
}


void
FilterUnitCore::Impl::CollectParameters(GstElement* element)
{
	GObjectClass* klass = G_OBJECT_GET_CLASS(element);
	guint count = 0;
	GParamSpec** properties = g_object_class_list_properties(klass, &count);
	for (guint i = 0; i < count; i++) {
		GParamSpec* spec = properties[i];
		if ((spec->flags & (G_PARAM_WRITABLE | G_PARAM_READABLE))
				!= (G_PARAM_WRITABLE | G_PARAM_READABLE)) {
			continue;
		}
		if (strcmp(g_param_spec_get_name(spec), "name") == 0
			|| strcmp(g_param_spec_get_name(spec), "parent") == 0) {
			continue;
		}

		Parameter param;
		param.object = G_OBJECT(g_object_ref(element));
		param.property = g_param_spec_get_name(spec);
		param.name = param.property;

		if (G_IS_PARAM_SPEC_INT(spec)) {
			GParamSpecInt* intSpec = G_PARAM_SPEC_INT(spec);
			param.min = (float)intSpec->minimum;
			param.max = (float)intSpec->maximum;
			param.isInteger = true;
		} else if (G_IS_PARAM_SPEC_UINT(spec)) {
			GParamSpecUInt* uintSpec = G_PARAM_SPEC_UINT(spec);
			param.min = (float)uintSpec->minimum;
			param.max = (float)uintSpec->maximum;
			param.isInteger = true;
		} else if (G_IS_PARAM_SPEC_FLOAT(spec)) {
			GParamSpecFloat* floatSpec = G_PARAM_SPEC_FLOAT(spec);
			param.min = (float)floatSpec->minimum;
			param.max = (float)floatSpec->maximum;
			param.isInteger = false;
		} else if (G_IS_PARAM_SPEC_DOUBLE(spec)) {
			GParamSpecDouble* doubleSpec = G_PARAM_SPEC_DOUBLE(spec);
			param.min = (float)doubleSpec->minimum;
			param.max = (float)doubleSpec->maximum;
			param.isInteger = false;
		} else {
			continue;
		}

		params.push_back(param);
	}
	g_free(properties);
}


static GstCaps*
_CapsForFormat(const BMediaFormat& format)
{
	if (format.IsRawVideo()) {
		const media_raw_video_format& raw = format.format.u.raw_video;
		int numerator = 0;
		int denominator = 0;
		gst_util_double_to_fraction(raw.field_rate, &numerator, &denominator);
		if (numerator <= 0 || denominator <= 0) {
			numerator = 30;
			denominator = 1;
		}
		return gst_caps_new_simple("video/x-raw",
			"format", G_TYPE_STRING, "BGRx",
			"width", G_TYPE_INT, (int)raw.width,
			"height", G_TYPE_INT, (int)raw.height,
			"framerate", GST_TYPE_FRACTION, numerator, denominator,
			NULL);
	}

	if (format.IsRawAudio()) {
		const media_raw_audio_format& raw = format.format.u.raw_audio;
		const char* sampleFormat = "S16LE";
		switch (raw.format & media_raw_audio_format::B_AUDIO_SIZE_MASK) {
			case media_raw_audio_format::B_AUDIO_FLOAT:
				sampleFormat = "F32LE";
				break;
			case media_raw_audio_format::B_AUDIO_SHORT:
				sampleFormat = raw.byte_order == B_MEDIA_BIG_ENDIAN
					? "S16BE" : "S16LE";
				break;
			case media_raw_audio_format::B_AUDIO_INT:
				sampleFormat = raw.byte_order == B_MEDIA_BIG_ENDIAN
					? "S32BE" : "S32LE";
				break;
			default:
				break;
		}
		return gst_caps_new_simple("audio/x-raw",
			"format", G_TYPE_STRING, sampleFormat,
			"layout", G_TYPE_STRING, "interleaved",
			"rate", G_TYPE_INT, (int)raw.frame_rate,
			"channels", G_TYPE_INT, (int)raw.channel_count,
			NULL);
	}

	return NULL;
}


FilterUnitCore::FilterUnitCore(const char* name, const char* description,
	media_type_mask acceptedTypes, ForwardFunc forward, void* forwardCookie)
	:
	fImpl(new(std::nothrow) Impl())
{
	if (fImpl == NULL)
		return;

	fImpl->name = name != NULL ? name : "Filter";
	fImpl->forward = forward;
	fImpl->forwardCookie = forwardCookie;
	fImpl->acceptedTypes = acceptedTypes;
	fImpl->initErr = B_NO_INIT;
	fImpl->pipeline = NULL;
	fImpl->appSrc = NULL;
	fImpl->appSink = NULL;
	fImpl->capsSet = false;
	fImpl->format.SetToDefault();

	if (description == NULL || description[0] == '\0') {
		fImpl->initErr = B_OK;
		return;
	}

	if (gst_is_initialized() == FALSE) {
		if (!gst_init_check(NULL, NULL, NULL))
			return;
	}

	BString pipelineText;
	if ((acceptedTypes & B_MEDIA_TYPE_BIT(B_MEDIA_RAW_VIDEO)) != 0) {
		pipelineText << "appsrc name=src is-live=true format=time do-timestamp=true"
			<< " block=true ! videoconvert ! " << description
			<< " ! videoconvert ! video/x-raw,format=BGRx"
			<< " ! appsink name=sink sync=false max-buffers=2";
	} else {
		pipelineText << "appsrc name=src is-live=true format=time do-timestamp=true"
			<< " block=true ! audioconvert ! audioresample ! "
			<< description
			<< " ! audioconvert ! audioresample"
			<< " ! appsink name=sink sync=false max-buffers=8";
	}

	GError* error = NULL;
	GstElement* pipeline = gst_parse_launch(pipelineText.String(), &error);
	if (error != NULL)
		g_error_free(error);
	if (pipeline == NULL)
		return;

	fImpl->pipeline = pipeline;
	fImpl->appSrc = gst_bin_get_by_name(GST_BIN(pipeline), "src");
	fImpl->appSink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
	if (fImpl->appSrc == NULL || fImpl->appSink == NULL) {
		gst_object_unref(pipeline);
		fImpl->pipeline = NULL;
		fImpl->appSrc = NULL;
		fImpl->appSink = NULL;
		return;
	}

	// Without this the chain never produces output and the second push
	// blocks forever on a full appsrc queue.
	gst_element_set_state(pipeline, GST_STATE_PLAYING);

	// Parameter list over every non-helper element in the chain.
	GstIterator* it = gst_bin_iterate_elements(GST_BIN(pipeline));
	GValue item = G_VALUE_INIT;
	while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
		GstElement* element = (GstElement*)g_value_get_object(&item);
		if (!_IsHelperElement(element))
			fImpl->CollectParameters(element);
		g_value_reset(&item);
	}
	g_value_unset(&item);
	gst_iterator_free(it);

	fImpl->initErr = B_OK;
}


FilterUnitCore::~FilterUnitCore()
{
	if (fImpl == NULL)
		return;

	if (fImpl->pipeline != NULL)
		gst_element_set_state(fImpl->pipeline, GST_STATE_NULL);
	for (size_t i = 0; i < fImpl->params.size(); i++)
		g_object_unref(fImpl->params[i].object);
	if (fImpl->appSrc != NULL)
		gst_object_unref(fImpl->appSrc);
	if (fImpl->appSink != NULL)
		gst_object_unref(fImpl->appSink);
	if (fImpl->pipeline != NULL)
		gst_object_unref(fImpl->pipeline);
	delete fImpl;
	fImpl = NULL;
}


status_t
FilterUnitCore::InitCheck() const
{
	return fImpl != NULL ? fImpl->initErr : B_NO_MEMORY;
}


status_t
FilterUnitCore::ProcessBuffer(void* buffer, size_t size,
	const BMediaFormat& format, CpuFilterFunc cpuFilter, void* cpuCookie)
{
	if (fImpl == NULL || fImpl->initErr != B_OK)
		return B_NO_INIT;
	if (buffer == NULL || size == 0)
		return B_BAD_VALUE;

	if (fImpl->pipeline == NULL || fImpl->appSrc == NULL) {
		if (cpuFilter != NULL) {
			void* copy = malloc(size);
			if (copy == NULL)
				return B_NO_MEMORY;
			memcpy(copy, buffer, size);
			cpuFilter(copy, size, cpuCookie);
			fImpl->forward(fImpl->forwardCookie, copy, size, format);
			free(copy);
			return B_OK;
		}
		fImpl->forward(fImpl->forwardCookie, buffer, size, format);
		return B_OK;
	}

	if (fImpl->capsSet && format.IsRawVideo()) {
		const media_raw_video_format& now = format.format.u.raw_video;
		const media_raw_video_format& was = fImpl->format.format.u.raw_video;
		if (now.width != was.width || now.height != was.height
			|| now.color_space != was.color_space)
			fImpl->capsSet = false;
	}
	if (!fImpl->capsSet) {
		GstCaps* caps = _CapsForFormat(format);
		if (caps == NULL)
			return B_BAD_VALUE;
		g_object_set(fImpl->appSrc, "caps", caps, NULL);
		gst_caps_unref(caps);
		fImpl->format = format;
		fImpl->capsSet = true;
	}

	GstBuffer* gstBuffer = gst_buffer_new_allocate(NULL, size, NULL);
	if (gstBuffer == NULL)
		return B_NO_MEMORY;
	GstMapInfo map;
	if (!gst_buffer_map(gstBuffer, &map, GST_MAP_WRITE)) {
		gst_buffer_unref(gstBuffer);
		return B_ERROR;
	}
	memcpy(map.data, buffer, size);
	gst_buffer_unmap(gstBuffer, &map);

	GstFlowReturn flow = gst_app_src_push_buffer(GST_APP_SRC(fImpl->appSrc),
		gstBuffer);
	if (flow != GST_FLOW_OK) {
		gst_buffer_unref(gstBuffer);
		return B_ERROR;
	}

	GstSample* sample = gst_app_sink_try_pull_sample(
		GST_APP_SINK(fImpl->appSink), 100 * GST_MSECOND);
	if (sample == NULL) {
		// Chain dropped or delayed this frame; deliver the input unfiltered.
		fImpl->forward(fImpl->forwardCookie, buffer, size, format);
		return B_OK;
	}

	GstBuffer* outBuffer = gst_sample_get_buffer(sample);
	GstMapInfo outMap;
	if (gst_buffer_map(outBuffer, &outMap, GST_MAP_READ)) {
		fImpl->forward(fImpl->forwardCookie, outMap.data, outMap.size, format);
		gst_buffer_unmap(outBuffer, &outMap);
	} else {
		fImpl->forward(fImpl->forwardCookie, buffer, size, format);
	}
	gst_sample_unref(sample);
	return B_OK;
}


int32
FilterUnitCore::CountParameters() const
{
	return fImpl != NULL ? (int32)fImpl->params.size() : 0;
}


status_t
FilterUnitCore::GetParameterInfo(int32 index, BString* outName,
	float* outMin, float* outMax, float* outValue)
{
	if (fImpl == NULL || index < 0 || index >= (int32)fImpl->params.size())
		return B_BAD_VALUE;

	const Parameter& param = fImpl->params[index];
	if (outName != NULL)
		*outName = param.name;
	if (outMin != NULL)
		*outMin = param.min;
	if (outMax != NULL)
		*outMax = param.max;
	if (outValue != NULL) {
		if (param.isInteger) {
			gint value = 0;
			g_object_get(param.object, param.property.String(), &value, NULL);
			*outValue = (float)value;
		} else {
			gdouble value = 0;
			g_object_get(param.object, param.property.String(), &value, NULL);
			*outValue = (float)value;
		}
	}
	return B_OK;
}


status_t
FilterUnitCore::SetParameterValue(int32 index, float value)
{
	if (fImpl == NULL || index < 0 || index >= (int32)fImpl->params.size())
		return B_BAD_VALUE;

	const Parameter& param = fImpl->params[index];
	if (value < param.min)
		value = param.min;
	if (value > param.max)
		value = param.max;

	if (param.isInteger)
		g_object_set(param.object, param.property.String(), (gint)value,
			NULL);
	else
		g_object_set(param.object, param.property.String(), (gdouble)value,
			NULL);
	return B_OK;
}


float
FilterUnitCore::ParameterValue(int32 index) const
{
	float value = 0.0f;
	if (GetParameterInfo(index, NULL, NULL, NULL, &value) != B_OK)
		return 0.0f;
	return value;
}


} }
