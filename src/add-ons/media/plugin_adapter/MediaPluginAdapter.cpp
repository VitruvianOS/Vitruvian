/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MediaPluginAdapter.h"

#include <dlfcn.h>
#include <math.h>
#include <new>
#include <string.h>
#include <sys/stat.h>

#include <String.h>

#include <media2/ParameterWeb.h>

#include <MediaUnitAddOn.h>


#ifdef VOS_HAS_LILV
#include <lilv/lilv.h>
#endif


BMediaPluginAdapter::BMediaPluginAdapter(const char* name)
	:
	BMediaUnit(name != NULL ? name : "Plugin", B_MEDIA_FILTER),
	fModule(NULL),
	fDescriptor(NULL),
	fHandle(NULL),
	fControllable(NULL),
	fConfiguredRate(0),
	fBlockSize(0),
	fInitStatus(B_NO_INIT)
#ifdef VOS_HAS_LILV
	, fLv2(NULL)
#endif
{
	fControllable = new(std::nothrow) BControllable();
	if (fControllable == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}
	fControllable->SetChangeHook(&BMediaPluginAdapter::_ChangeHook, this);
	SetControllable(fControllable);
	fInitStatus = B_OK;
}


BMediaPluginAdapter::~BMediaPluginAdapter()
{
	Stop();
	_TeardownPlugin();
	if (fControllable != NULL) {
		fControllable->UnbindNode();
		delete fControllable;
		fControllable = NULL;
	}
}


status_t
BMediaPluginAdapter::InitCheck() const
{
	return fInitStatus;
}


const char*
BMediaPluginAdapter::PluginName() const
{
	return fPluginName.String();
}


int32
BMediaPluginAdapter::AudioInputCount() const
{
	return (int32)fInputs.size();
}


int32
BMediaPluginAdapter::AudioOutputCount() const
{
	return (int32)fOutputs.size();
}


int32
BMediaPluginAdapter::ParameterCount() const
{
	if (fDescriptor == NULL)
		return 0;
	int32 n = 0;
	for (unsigned long i = 0; i < fDescriptor->PortCount; i++) {
		if ((fDescriptor->PortDescriptors[i] & LADSPA_PORT_CONTROL)
				&& (fDescriptor->PortDescriptors[i] & LADSPA_PORT_INPUT))
			n++;
	}
	return n;
}


void
BMediaPluginAdapter::_TeardownPlugin()
{
	if (fHandle != NULL && fDescriptor != NULL && fDescriptor->cleanup)
		fDescriptor->cleanup(fHandle);
	fHandle = NULL;
	fDescriptor = NULL;
	if (fModule != NULL) {
		dlclose(fModule);
		fModule = NULL;
	}
	for (size_t i = 0; i < fPortBuffers.size(); i++) {
		delete[] fPortBuffers[i];
		fPortBuffers[i] = NULL;
	}
	fPortBuffers.clear();
	fPortIsInput.clear();
	fControlValues.clear();
	for (size_t i = 0; i < fInputs.size(); i++) {
		// Ports are owned by BMediaClient registration; do not delete here
		// if already unregistered. Clear pointers only.
		fInputs[i] = NULL;
	}
	for (size_t i = 0; i < fOutputs.size(); i++)
		fOutputs[i] = NULL;
	fInputs.clear();
	fOutputs.clear();
	fPluginName = "";
#ifdef VOS_HAS_LILV
	if (fLv2 != NULL) {
		// Lilv world unref is done in the LV2 loader once linked.
		fLv2 = NULL;
	}
#endif
}


status_t
BMediaPluginAdapter::LoadLadspa(const char* path, unsigned long index)
{
	if (path == NULL || path[0] == '\0')
		return B_BAD_VALUE;
	if (fInitStatus != B_OK)
		return fInitStatus;

	Stop();
	_TeardownPlugin();

	fModule = dlopen(path, RTLD_NOW);
	if (fModule == NULL)
		return B_ERROR;

	LADSPA_Descriptor_Function entry
		= (LADSPA_Descriptor_Function)dlsym(fModule, "ladspa_descriptor");
	if (entry == NULL) {
		dlclose(fModule);
		fModule = NULL;
		return B_ERROR;
	}

	fDescriptor = entry(index);
	if (fDescriptor == NULL) {
		dlclose(fModule);
		fModule = NULL;
		return B_ENTRY_NOT_FOUND;
	}

	fPluginName = fDescriptor->Label != NULL ? fDescriptor->Label : "plugin";

	// Create one mono port pair per audio in/out; control ports become
	// SPA props published after Start.
	int32 nIn = 0;
	int32 nOut = 0;
	for (unsigned long i = 0; i < fDescriptor->PortCount; i++) {
		LADSPA_PortDescriptor pd = fDescriptor->PortDescriptors[i];
		if ((pd & LADSPA_PORT_AUDIO) && (pd & LADSPA_PORT_INPUT))
			nIn++;
		else if ((pd & LADSPA_PORT_AUDIO) && (pd & LADSPA_PORT_OUTPUT))
			nOut++;
	}
	if (nIn == 0 && nOut == 0) {
		_TeardownPlugin();
		return B_NOT_SUPPORTED;
	}

	_SetupPorts();
	return B_OK;
}


#ifdef VOS_HAS_LILV
status_t
BMediaPluginAdapter::LoadLv2(const char* uri)
{
	if (uri == NULL || uri[0] == '\0')
		return B_BAD_VALUE;

	Stop();
	_TeardownPlugin();

	LilvWorld* world = lilv_world_new();
	if (world == NULL)
		return B_NO_MEMORY;
	lilv_world_load_all(world);

	LilvNode* uriNode = lilv_new_uri(world, uri);
	const LilvPlugin* plugin = lilv_plugins_get_by_uri(
		lilv_world_get_plugins(world), uriNode);
	if (plugin == NULL) {
		lilv_node_free(uriNode);
		lilv_world_free(world);
		return B_ENTRY_NOT_FOUND;
	}

	const LilvNodes* ports = lilv_plugin_get_port_indices(plugin);
	// Port enumeration continues in the LV2 loader; without a live graph
	// this path is compile-checked only when lilv headers are present.
	(void)ports;
	fPluginName = uri;
	fLv2 = world;
	lilv_node_free(uriNode);
	return B_NOT_SUPPORTED;
}
#endif


void
BMediaPluginAdapter::_SetupPorts()
{
	if (fDescriptor == NULL)
		return;

	int32 nIn = 0;
	int32 nOut = 0;
	for (unsigned long i = 0; i < fDescriptor->PortCount; i++) {
		LADSPA_PortDescriptor pd = fDescriptor->PortDescriptors[i];
		if ((pd & LADSPA_PORT_AUDIO) && (pd & LADSPA_PORT_INPUT))
			nIn++;
		else if ((pd & LADSPA_PORT_AUDIO) && (pd & LADSPA_PORT_OUTPUT))
			nOut++;
	}

	const int32 kMaxPorts = 64;
	if (nIn > kMaxPorts)
		nIn = kMaxPorts;
	if (nOut > kMaxPorts)
		nOut = kMaxPorts;

	for (int32 i = 0; i < nIn; i++) {
		BString name;
		name.SetToFormat("in_%d", i);
		PluginInput* input = new(std::nothrow) PluginInput(name.String());
		if (input == NULL)
			return;
		input->SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));
		if (RegisterInput(input) != B_OK) {
			delete input;
			return;
		}
		fInputs.push_back(input);
	}
	for (int32 i = 0; i < nOut; i++) {
		BString name;
		name.SetToFormat("out_%d", i);
		PluginOutput* output = new(std::nothrow) PluginOutput(name.String());
		if (output == NULL)
			return;
		output->SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));
		if (RegisterOutput(output) != B_OK) {
			delete output;
			return;
		}
		fOutputs.push_back(output);
	}

	fPortBuffers.assign(fDescriptor->PortCount, NULL);
	fPortIsInput.assign(fDescriptor->PortCount, 0);
	fControlValues.assign(fDescriptor->PortCount, 0.0f);

	int32 audioIn = 0;
	int32 audioOut = 0;
	for (unsigned long i = 0; i < fDescriptor->PortCount; i++) {
		LADSPA_PortDescriptor pd = fDescriptor->PortDescriptors[i];
		if (pd & LADSPA_PORT_AUDIO) {
			fPortBuffers[i] = new(std::nothrow) float[kMaxPorts + 8];
			if (fPortBuffers[i] != NULL)
				memset(fPortBuffers[i], 0, (kMaxPorts + 8) * sizeof(float));
			if (pd & LADSPA_PORT_INPUT) {
				fPortIsInput[i] = 1;
				audioIn++;
			} else {
				fPortIsInput[i] = 0;
				audioOut++;
			}
		} else if ((pd & LADSPA_PORT_CONTROL)
				&& (pd & LADSPA_PORT_INPUT)) {
			fPortIsInput[i] = 1;
			float def = 0.0f;
			if (fDescriptor->PortRangeHints != NULL) {
				const LADSPA_PortRangeHint* hint
					= &fDescriptor->PortRangeHints[i];
				if (hint->LowerBound <= hint->UpperBound)
					def = hint->LowerBound;
			}
			fControlValues[i] = def;
			fPortBuffers[i] = &fControlValues[i];
		}
	}
	(void)audioIn;
	(void)audioOut;
}


void
BMediaPluginAdapter::_ChangeHook(int32 id, bigtime_t, const void* value,
	size_t size, void* cookie)
{
	BMediaPluginAdapter* self = (BMediaPluginAdapter*)cookie;
	if (self != NULL)
		self->_ApplyParam(id, value, size);
}


void
BMediaPluginAdapter::_ApplyParam(int32 id, const void* value, size_t size)
{
	if (value == NULL || size < sizeof(float) || fDescriptor == NULL)
		return;
	int32 port = id - kPropPortBase;
	if (port < 0 || (uint32)port >= fDescriptor->PortCount)
		return;
	fControlValues[port] = *(const float*)value;
}


status_t
BMediaPluginAdapter::Start()
{
	if (fDescriptor == NULL)
		return B_NO_INIT;

	status_t result = BMediaUnit::Start();
	if (result != B_OK)
		return result;

	if (fHandle == NULL && fDescriptor->instantiate != NULL) {
		uint32 rate = SampleRate();
		if (rate == 0)
			rate = 48000;
		fHandle = fDescriptor->instantiate(fDescriptor, (unsigned long)rate);
		if (fHandle == NULL)
			return B_ERROR;
	}

	if (fHandle != NULL && fDescriptor->connect_port != NULL) {
		for (unsigned long i = 0; i < fDescriptor->PortCount; i++) {
			if (fPortBuffers[i] != NULL)
				fDescriptor->connect_port(fHandle, i, fPortBuffers[i]);
		}
		if (fDescriptor->activate != NULL)
			fDescriptor->activate(fHandle);
	}

	_PublishAndBind();
	return B_OK;
}


void
BMediaPluginAdapter::_PublishAndBind()
{
	uint32 nodeId = NodeId();
	if (nodeId == 0 || fControllable == NULL || fDescriptor == NULL)
		return;

	int32 nCtrl = 0;
	for (unsigned long i = 0; i < fDescriptor->PortCount; i++) {
		LADSPA_PortDescriptor pd = fDescriptor->PortDescriptors[i];
		if ((pd & LADSPA_PORT_CONTROL) && (pd & LADSPA_PORT_INPUT))
			nCtrl++;
	}
	if (nCtrl == 0)
		return;

	std::vector<uint32> propIds;
	std::vector<BString> propNames;
	std::vector<uint32> valueTypes;
	std::vector<float> minValues;
	std::vector<float> maxValues;
	std::vector<float> steps;
	std::vector<float> values;
	std::vector<const void*> valuePtrs;
	std::vector<size_t> valueSizes;

	for (unsigned long i = 0; i < fDescriptor->PortCount; i++) {
		LADSPA_PortDescriptor pd = fDescriptor->PortDescriptors[i];
		if (!((pd & LADSPA_PORT_CONTROL) && (pd & LADSPA_PORT_INPUT)))
			continue;
		propIds.push_back(kPropPortBase + (uint32)i);
		const char* pname = fDescriptor->PortNames != NULL
			? fDescriptor->PortNames[i] : "param";
		propNames.push_back(BString(pname));
		valueTypes.push_back((uint32)B_MEDIA_PROP_FLOAT);
		float lo = 0.0f;
		float hi = 1.0f;
		if (fDescriptor->PortRangeHints != NULL) {
			const LADSPA_PortRangeHint* hint
				= &fDescriptor->PortRangeHints[i];
			lo = hint->LowerBound;
			hi = hint->UpperBound;
			if (hi < lo) {
				lo = 0.0f;
				hi = 1.0f;
			}
		}
		minValues.push_back(lo);
		maxValues.push_back(hi);
		steps.push_back(0.01f);
		values.push_back(fControlValues[i]);
	}

	// PublishNodeParams wants parallel C arrays; build them once.
	int32 n = (int32)propIds.size();
	std::vector<uint32> ids(n);
	std::vector<const char*> names(n);
	std::vector<uint32> types(n);
	std::vector<float> mins(n);
	std::vector<float> maxs(n);
	std::vector<float> sts(n);
	std::vector<const void*> ptrs(n);
	std::vector<size_t> szs(n);
	for (int32 i = 0; i < n; i++) {
		ids[i] = propIds[i];
		names[i] = propNames[i].String();
		types[i] = valueTypes[i];
		mins[i] = minValues[i];
		maxs[i] = maxValues[i];
		sts[i] = steps[i];
		ptrs[i] = &values[i];
		szs[i] = sizeof(float);
	}

	PublishNodeParams(ids.data(), names.data(), types.data(), mins.data(),
		maxs.data(), sts.data(), ptrs.data(), szs.data(), n);
	fControllable->BindNode(nodeId);
}


void
BMediaPluginAdapter::ProcessCycle(uint32 frameCount)
{
	if (frameCount == 0 || fHandle == NULL || fDescriptor == NULL)
		return;
	if (fDescriptor->run == NULL)
		return;

	uint32 rate = SampleRate();
	if (rate != 0 && rate != fConfiguredRate.load()) {
		// LADSPA instances are rate-fixed at instantiate; restart cost is
		// accepted here rather than inventing a resampler (§0.9).
		fConfiguredRate.store(rate);
	}

	const uint32 kMax = 256;
	if (frameCount > kMax)
		frameCount = kMax;
	fBlockSize.store(frameCount);

	// Dequeue input, run, queue output. Never cache frameCount across cycles.
	int32 port = 0;
	for (size_t c = 0; c < fInputs.size() && port >= 0; c++) {
		size_t size = 0;
		float* buf = (float*)BufferFor(fInputs[c], &size);
		if (buf == NULL)
			continue;
		// Find next audio input port.
		while ((uint32)port < fDescriptor->PortCount
				&& !((fDescriptor->PortDescriptors[port] & LADSPA_PORT_AUDIO)
					&& (fDescriptor->PortDescriptors[port] & LADSPA_PORT_INPUT)))
			port++;
		if ((uint32)port >= fDescriptor->PortCount)
			break;
		if (fPortBuffers[port] != NULL && fPortBuffers[port] != buf)
			memcpy(fPortBuffers[port], buf, frameCount * sizeof(float));
		port++;
	}

	fDescriptor->run(fHandle, frameCount);

	port = 0;
	for (size_t c = 0; c < fOutputs.size() && port >= 0; c++) {
		size_t size = 0;
		float* buf = (float*)BufferFor(fOutputs[c], &size);
		if (buf == NULL)
			continue;
		while ((uint32)port < fDescriptor->PortCount
				&& !((fDescriptor->PortDescriptors[port] & LADSPA_PORT_AUDIO)
					&& !(fDescriptor->PortDescriptors[port] & LADSPA_PORT_INPUT)))
			port++;
		if ((uint32)port >= fDescriptor->PortCount)
			break;
		if (fPortBuffers[port] != NULL && fPortBuffers[port] != buf)
			memcpy(buf, fPortBuffers[port], frameCount * sizeof(float));
		port++;
	}
}


#ifdef VOS_HAS_LILV
// Resolves an LV2 bundle directory to the URI of its first plugin.
static bool
_Lv2UriForBundle(const char* path, BString* outUri)
{
	BString bundle(path);
	if (!bundle.EndsWith("/"))
		bundle << "/";
	BString bundleUri("file://");
	bundleUri << bundle;

	LilvWorld* world = lilv_world_new();
	if (world == NULL)
		return false;
	LilvNode* node = lilv_new_uri(world, bundleUri.String());
	lilv_world_load_bundle(world, node);
	lilv_node_free(node);

	bool found = false;
	const LilvPlugins* plugins = lilv_world_get_all_plugins(world);
	LILV_FOREACH(plugins, i, plugins) {
		const LilvPlugin* plugin = lilv_plugins_get(plugins, i);
		*outUri = lilv_node_as_uri(lilv_plugin_get_uri(plugin));
		found = true;
		break;
	}
	lilv_world_free(world);
	return found;
}
#endif


extern "C" int32
media_unit_count()
{
	return 0;
}


extern "C" const char*
media_unit_name(int32)
{
	return NULL;
}


extern "C" BMediaUnit*
media_unit_instantiate(int32)
{
	return NULL;
}


// LADSPA libraries use descriptor 0; with lilv, a URI or bundle
// directory selects an LV2 plugin.
extern "C" BMediaUnit*
media_plugin_load_file(const char* path)
{
	if (path == NULL || path[0] == '\0')
		return NULL;

	BMediaPluginAdapter* unit = new(std::nothrow) BMediaPluginAdapter();
	if (unit == NULL || unit->InitCheck() != B_OK) {
		delete unit;
		return NULL;
	}

	status_t status = B_NOT_SUPPORTED;
	struct stat st;
	bool isDirectory = stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#ifdef VOS_HAS_LILV
	if (isDirectory) {
		BString uri;
		if (_Lv2UriForBundle(path, &uri))
			status = unit->LoadLv2(uri.String());
	} else if (strstr(path, "://") != NULL) {
		status = unit->LoadLv2(path);
	} else
#endif
	if (!isDirectory)
		status = unit->LoadLadspa(path, 0);

	if (status != B_OK) {
		delete unit;
		return NULL;
	}
	return unit;
}
