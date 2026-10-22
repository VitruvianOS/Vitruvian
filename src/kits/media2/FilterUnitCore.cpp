/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "FilterUnitCore.h"

#include <dlfcn.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

#include <media2/MediaDefs.h>
#include <media2/ParameterWeb.h>


// Minimal LADSPA ABI declarations; ladspa.h is not a build dependency and
// this interface has been frozen since 1.0.
typedef float LADSPA_Data;
typedef int LADSPA_PortDescriptor;
typedef int LADSPA_PortHintDescriptor;

enum {
	LADSPA_PORT_INPUT		= 0x1,
	LADSPA_PORT_CONTROL		= 0x4
};

enum {
	LADSPA_HINT_BOUNDED_BELOW	= 0x1,
	LADSPA_HINT_BOUNDED_ABOVE	= 0x2,
	LADSPA_HINT_TOGGLED		= 0x10,
	LADSPA_HINT_SAMPLE_RATE		= 0x20,
	LADSPA_HINT_LOGARITHMIC		= 0x40,
	LADSPA_HINT_INTEGER		= 0x100
};

struct LADSPA_PortRangeHint {
	LADSPA_PortDescriptor		PortDescriptor;
	LADSPA_PortHintDescriptor	HintDescriptor;
	LADSPA_Data			LowerBound;
	LADSPA_Data			UpperBound;
};

struct LADSPA_Descriptor {
	unsigned long			UniqueID;
	const char*			Label;
	unsigned long			Properties;
	const char*			Name;
	const char*			Maker;
	const char*			Copyright;
	unsigned long			PortCount;
	const LADSPA_PortRangeHint*	PortRangeHints;
	const char* const*		PortNames;
	void*				ImplementationData;
	void				(*instantiate)(const struct LADSPA_Descriptor*, unsigned long);
	void				(*connect_port)(void*, unsigned long, LADSPA_Data*);
	void				(*activate)(void*);
	void				(*run)(void*, unsigned long);
	void				(*run_adding)(void*, unsigned long);
	void				(*set_run_adding_gain)(void*, LADSPA_Data);
	void				(*deactivate)(void*);
	void				(*cleanup)(void*);
};

typedef const LADSPA_Descriptor* (*LADSPA_Descriptor_Function)(unsigned long);


namespace BPrivate { namespace media {


struct FilterUnitCore::Parameter {
	enum Kind {
		kFloatValue,
		kIntegerValue,
		kBooleanValue,
		kEnumerationValue
	};

	BString		name;
	BString		unit;
	GObject*	object;
	BString		property;
	Kind		kind;
	float		min;
	float		max;
	bool		logarithmic;
	// Enum value to nick name; empty for the other kinds.
	std::vector<std::pair<int, BString> > items;
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
			void				ApplyLadspaHints(GstElement* element,
									size_t first);
};


// "Gain (dB)" becomes name "Gain" with unit "dB".
static void
_SplitUnitSuffix(BString& name, BString& unit)
{
	int32 open = name.FindLast("(");
	if (open == B_ERROR || !name.EndsWith(")"))
		return;
	BString suffix = name.String() + open + 1;
	suffix.Truncate(suffix.Length() - 1);
	suffix.Trim();
	if (suffix.Length() == 0 || suffix.Length() > 8)
		return;
	unit = suffix;
	name.Truncate(open);
	name.Trim();
}


static bool
_NamesMatch(const BString& a, const BString& b)
{
	BString left(a);
	BString right(b);
	left.ToLower();
	right.ToLower();
	left.ReplaceAll(" ", "");
	right.ReplaceAll(" ", "");
	left.ReplaceAll("_", "");
	right.ReplaceAll("_", "");
	left.ReplaceAll("-", "");
	right.ReplaceAll("-", "");
	return left == right;
}


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


// Applies LADSPA port hints to the parameters collected from one element.
// GStreamer drops the hints; the descriptor still carries them.
void
FilterUnitCore::Impl::ApplyLadspaHints(GstElement* element, size_t first)
{
	GstElementFactory* factory = gst_element_get_factory(element);
	const char* factoryName = factory != NULL
		? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)) : NULL;
	if (factoryName == NULL || !g_str_has_prefix(factoryName, "ladspa-"))
		return;

	gchar* pluginPath = NULL;
	gchar* label = NULL;
	g_object_get(element, "plugin", &pluginPath, "label", &label, NULL);
	if (pluginPath == NULL || label == NULL) {
		g_free(pluginPath);
		g_free(label);
		return;
	}

	void* library = dlopen(pluginPath, RTLD_NOW | RTLD_LOCAL);
	if (library == NULL) {
		g_free(pluginPath);
		g_free(label);
		return;
	}

	LADSPA_Descriptor_Function descriptorFunction
		= (LADSPA_Descriptor_Function)dlsym(library, "ladspa_descriptor");
	if (descriptorFunction == NULL) {
		dlclose(library);
		g_free(pluginPath);
		g_free(label);
		return;
	}

	const LADSPA_Descriptor* descriptor = NULL;
	for (unsigned long i = 0; ; i++) {
		const LADSPA_Descriptor* candidate = descriptorFunction(i);
		if (candidate == NULL)
			break;
		if (candidate->Label != NULL && strcmp(candidate->Label, label) == 0) {
			descriptor = candidate;
			break;
		}
	}

	if (descriptor != NULL && descriptor->PortNames != NULL) {
		for (size_t p = first; p < params.size(); p++) {
			Parameter& param = params[p];
			for (unsigned long port = 0; port < descriptor->PortCount;
					port++) {
				const char* portName = descriptor->PortNames[port];
				if (portName == NULL
					|| !_NamesMatch(param.name, BString(portName))) {
					continue;
				}
				const LADSPA_PortRangeHint& hint
					= descriptor->PortRangeHints[port];
				if ((hint.HintDescriptor & LADSPA_HINT_LOGARITHMIC) != 0)
					param.logarithmic = true;
				if ((hint.HintDescriptor & LADSPA_HINT_TOGGLED) != 0
						&& param.kind == Parameter::kIntegerValue) {
					param.kind = Parameter::kBooleanValue;
					param.min = 0;
					param.max = 1;
				}
				break;
			}
		}
	}

	dlclose(library);
	g_free(pluginPath);
	g_free(label);
}


void
FilterUnitCore::Impl::CollectParameters(GstElement* element)
{
	const size_t first = params.size();

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
			|| strcmp(g_param_spec_get_name(spec), "parent") == 0
			|| strcmp(g_param_spec_get_name(spec), "plugin") == 0
			|| strcmp(g_param_spec_get_name(spec), "label") == 0) {
			continue;
		}

		Parameter param;
		param.object = G_OBJECT(g_object_ref(element));
		param.property = g_param_spec_get_name(spec);
		param.name = param.property;
		param.unit = "";
		param.logarithmic = false;

		if (G_IS_PARAM_SPEC_BOOLEAN(spec)) {
			param.kind = Parameter::kBooleanValue;
			param.min = 0.0f;
			param.max = 1.0f;
		} else if (G_IS_PARAM_SPEC_ENUM(spec)) {
			param.kind = Parameter::kEnumerationValue;
			param.min = 0.0f;
			param.max = 0.0f;
			GEnumClass* enumClass = (GEnumClass*)g_type_class_ref(
				G_PARAM_SPEC_VALUE_TYPE(spec));
			if (enumClass != NULL) {
				for (guint e = 0; e < enumClass->n_values; e++) {
					const GEnumValue& value = enumClass->values[e];
					const char* nick = value.value_nick != NULL
						? value.value_nick : value.value_name;
					param.items.push_back(std::make_pair(value.value,
						BString(nick != NULL ? nick : "value")));
					if ((float)value.value > param.max)
						param.max = (float)value.value;
				}
				g_type_class_unref(enumClass);
			}
		} else if (G_IS_PARAM_SPEC_INT(spec)) {
			GParamSpecInt* intSpec = G_PARAM_SPEC_INT(spec);
			param.kind = Parameter::kIntegerValue;
			param.min = (float)intSpec->minimum;
			param.max = (float)intSpec->maximum;
		} else if (G_IS_PARAM_SPEC_UINT(spec)) {
			GParamSpecUInt* uintSpec = G_PARAM_SPEC_UINT(spec);
			param.kind = Parameter::kIntegerValue;
			param.min = (float)uintSpec->minimum;
			param.max = (float)uintSpec->maximum;
		} else if (G_IS_PARAM_SPEC_FLOAT(spec)) {
			GParamSpecFloat* floatSpec = G_PARAM_SPEC_FLOAT(spec);
			param.kind = Parameter::kFloatValue;
			param.min = (float)floatSpec->minimum;
			param.max = (float)floatSpec->maximum;
		} else if (G_IS_PARAM_SPEC_DOUBLE(spec)) {
			GParamSpecDouble* doubleSpec = G_PARAM_SPEC_DOUBLE(spec);
			param.kind = Parameter::kFloatValue;
			param.min = (float)doubleSpec->minimum;
			param.max = (float)doubleSpec->maximum;
		} else {
			g_object_unref(param.object);
			continue;
		}

		// Units travel in the property name, e.g. "threshold (dB)".
		_SplitUnitSuffix(param.name, param.unit);

		// Ports spanning decades (20 Hz to 20 kHz) get a log slider when
		// the plugin did not say anything.
		if (param.kind == Parameter::kFloatValue && param.min > 0.0f
			&& param.max / param.min >= 100.0f) {
			param.logarithmic = true;
		}

		params.push_back(param);
	}
	g_free(properties);

	ApplyLadspaHints(element, first);
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
	float* outMin, float* outMax, float* outValue) const
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
		if (param.kind == Parameter::kFloatValue) {
			gdouble value = 0;
			g_object_get(param.object, param.property.String(), &value, NULL);
			*outValue = (float)value;
		} else {
			gint value = 0;
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

	if (param.kind == Parameter::kFloatValue)
		g_object_set(param.object, param.property.String(), (gdouble)value,
			NULL);
	else
		g_object_set(param.object, param.property.String(), (gint)value,
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


// One group, one control per collected property; ids are the indexes.
status_t
FilterUnitCore::BuildParameterWeb(BParameterGroup* group)
{
	if (fImpl == NULL || group == NULL)
		return B_NO_INIT;

	for (size_t i = 0; i < fImpl->params.size(); i++) {
		const Parameter& param = fImpl->params[i];
		const int32 id = (int32)i;

		if (param.kind == Parameter::kBooleanValue) {
			BDiscreteParameter* discrete = group->MakeDiscreteParameter(id,
				B_MEDIA_NO_TYPE, param.name.String(), B_SWITCH);
			if (discrete != NULL) {
				discrete->AddItem(0, "Off");
				discrete->AddItem(1, "On");
			}
		} else if (param.kind == Parameter::kEnumerationValue) {
			BDiscreteParameter* discrete = group->MakeDiscreteParameter(id,
				B_MEDIA_NO_TYPE, param.name.String(), B_GENERIC);
			if (discrete != NULL) {
				for (size_t e = 0; e < param.items.size(); e++)
					discrete->AddItem(param.items[e].first,
						param.items[e].second.String());
			}
		} else {
			BContinuousParameter* continuous
				= group->MakeContinuousParameter(id, B_MEDIA_NO_TYPE,
					param.name.String(), B_GENERIC, param.unit.String(),
					param.min, param.max,
					param.kind == Parameter::kIntegerValue
						? 1.0f : (param.max - param.min) / 1000.0f);
			if (continuous != NULL && param.logarithmic)
				continuous->SetFlags(B_LOGARITHMIC_PARAMETER);
		}
	}
	return B_OK;
}


} }
