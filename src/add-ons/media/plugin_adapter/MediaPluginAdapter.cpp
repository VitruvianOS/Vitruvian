/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MediaPluginAdapter.h"

#include <dlfcn.h>
#include <math.h>
#include <new>
#include <set>
#include <map>
#include <string.h>
#include <sys/stat.h>

#include <Autolock.h>
#include <Locker.h>
#include <OS.h>
#include <String.h>

#include <media2/ParameterWeb.h>

#include <MediaUnitAddOn.h>


#ifdef VOS_HAS_LILV
#include "Lv2PluginState.h"
#endif

#ifdef VOS_HAS_SUIL
#include <suil/suil.h>

#include "Lv2UIHost.h"
#endif


#ifdef VOS_HAS_LILV
namespace {


static LV2_URID
_Lv2MapUri(LV2_URID_Map_Handle handle, const char* uri)
{
	Lv2PluginState* state = (Lv2PluginState*)handle;
	if (state == NULL || uri == NULL)
		return 0;

	std::map<std::string, LV2_URID>::iterator found = state->uridTable.find(uri);
	if (found != state->uridTable.end())
		return found->second;

	char* copy = strdup(uri);
	if (copy == NULL)
		return 0;
	LV2_URID id = (LV2_URID)state->uridStrings.size() + 1;
	state->uridStrings.push_back(copy);
	state->uridTable[copy] = id;
	return id;
}


static const char*
_Lv2UnmapUri(LV2_URID_Unmap_Handle handle, LV2_URID id)
{
	Lv2PluginState* state = (Lv2PluginState*)handle;
	if (state == NULL || id == 0 || id > state->uridStrings.size())
		return NULL;
	return state->uridStrings[id - 1];
}


static float
_Lv2NodeValue(const LilvNode* node, float fallback)
{
	if (node == NULL)
		return fallback;
	if (lilv_node_is_float(node))
		return lilv_node_as_float(node);
	if (lilv_node_is_int(node))
		return (float)lilv_node_as_int(node);
	return fallback;
}


// Frees everything the state owns; safe on a partly built state.
static void
_Lv2DestroyState(Lv2PluginState* state)
{
	if (state->instance != NULL) {
		lilv_instance_deactivate(state->instance);
		lilv_instance_free(state->instance);
	}
	for (size_t i = 0; i < state->audioBuffers.size(); i++)
		delete[] state->audioBuffers[i];
	for (size_t i = 0; i < state->uridStrings.size(); i++)
		free(state->uridStrings[i]);
	if (state->world != NULL)
		lilv_world_free(state->world);
	delete state;
}


} // anonymous namespace
#endif // VOS_HAS_LILV


// The C entry point must not guess whether a BMediaUnit is one of ours;
// live adapters register here and the export checks membership.
static BLocker sAdapterLock("plugin adapters");
static std::set<BMediaPluginAdapter*> sLiveAdapters;


static void
_RegisterAdapter(BMediaPluginAdapter* adapter)
{
	BAutolock _(sAdapterLock);
	sLiveAdapters.insert(adapter);
}


static void
_UnregisterAdapter(BMediaPluginAdapter* adapter)
{
	BAutolock _(sAdapterLock);
	sLiveAdapters.erase(adapter);
}


static BMediaPluginAdapter*
_AdapterForUnit(BMediaUnit* unit)
{
	BAutolock _(sAdapterLock);
	std::set<BMediaPluginAdapter*>::iterator found
		= sLiveAdapters.find((BMediaPluginAdapter*)unit);
	return found != sLiveAdapters.end() ? *found : NULL;
}


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
	, fUiHost(NULL)
#endif
{
	fControllable = new(std::nothrow) BControllable();
	if (fControllable == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}
	fControllable->SetChangeHook(&BMediaPluginAdapter::_ChangeHook, this);
	SetControllable(fControllable);
	_RegisterAdapter(this);
	fInitStatus = B_OK;
}


BMediaPluginAdapter::~BMediaPluginAdapter()
{
	_UnregisterAdapter(this);
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
#ifdef VOS_HAS_LILV
	if (fLv2 != NULL)
		return (int32)((Lv2PluginState*)fLv2)->controlPorts.size();
#endif

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
	// A reload registers new ports, so the old ones go here.
	for (size_t i = 0; i < fInputs.size(); i++) {
		UnregisterInput(fInputs[i]);
		delete fInputs[i];
	}
	for (size_t i = 0; i < fOutputs.size(); i++) {
		UnregisterOutput(fOutputs[i]);
		delete fOutputs[i];
	}
	fInputs.clear();
	fOutputs.clear();
	fPluginName = "";
#ifdef VOS_HAS_LILV
#ifdef VOS_HAS_SUIL
	if (fUiHost != NULL) {
		// The UI thread uses the plugin state; it must be gone first.
		Lv2UIHost* host = (Lv2UIHost*)fUiHost;
		fUiHost = NULL;
		if (host->Lock())
			host->Quit();
	}
#endif
	if (fLv2 != NULL) {
		_Lv2DestroyState((Lv2PluginState*)fLv2);
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
	if (fInitStatus != B_OK)
		return fInitStatus;

	Stop();
	_TeardownPlugin();

	Lv2PluginState* state = new(std::nothrow) Lv2PluginState();
	if (state == NULL)
		return B_NO_MEMORY;

	state->world = lilv_world_new();
	if (state->world == NULL) {
		delete state;
		return B_NO_MEMORY;
	}
	lilv_world_load_all(state->world);

	LilvNode* uriNode = lilv_new_uri(state->world, uri);
	state->plugin = lilv_plugins_get_by_uri(
		lilv_world_get_all_plugins(state->world), uriNode);
	if (state->plugin == NULL) {
		lilv_node_free(uriNode);
		_Lv2DestroyState(state);
		return B_ENTRY_NOT_FOUND;
	}

	const uint32 portCount = lilv_plugin_get_num_ports(state->plugin);
	state->portKind.assign(portCount, 0);
	state->audioBuffers.assign(portCount, NULL);
	state->portValues.assign(portCount, 0.0f);

	LilvNode* inputNode = lilv_new_uri(state->world,
		"http://lv2plug.in/ns/lv2core#InputPort");
	LilvNode* audioNode = lilv_new_uri(state->world,
		"http://lv2plug.in/ns/lv2core#AudioPort");
	LilvNode* controlNode = lilv_new_uri(state->world,
		"http://lv2plug.in/ns/lv2core#ControlPort");

	const int32 kMaxAudioPorts = 64;
	int32 audioIn = 0;
	int32 audioOut = 0;
	int32 controlIn = 0;
	for (uint32 i = 0; i < portCount; i++) {
		const LilvPort* port = lilv_plugin_get_port_by_index(state->plugin,
			i);
		if (port == NULL)
			continue;

		bool isInput = lilv_port_is_a(state->plugin, port, inputNode);
		uint8 kind = isInput ? 1 : 0;
		if (lilv_port_is_a(state->plugin, port, audioNode)) {
			kind |= 2;
			if (isInput) {
				if (audioIn >= kMaxAudioPorts)
					continue;
				audioIn++;
				state->audioInPorts.push_back(i);
			} else {
				if (audioOut >= kMaxAudioPorts)
					continue;
				audioOut++;
				state->audioOutPorts.push_back(i);
			}
			state->audioBuffers[i] = new(std::nothrow) float[kMaxBlockSize];
			if (state->audioBuffers[i] == NULL) {
				lilv_node_free(inputNode);
				lilv_node_free(audioNode);
				lilv_node_free(controlNode);
				lilv_node_free(uriNode);
				_Lv2DestroyState(state);
				return B_NO_MEMORY;
			}
			memset(state->audioBuffers[i], 0,
				kMaxBlockSize * sizeof(float));
		} else if (lilv_port_is_a(state->plugin, port, controlNode)) {
			kind |= 4;
			// Every control port gets a cell; the plugin writes outputs.
			state->portKind[i] = kind;
			if (!isInput || controlIn >= kMaxAudioPorts)
				continue;
			controlIn++;

			float value = 0.0f;
			float min = 0.0f;
			float max = 1.0f;
			LilvNode* defNode = NULL;
			LilvNode* minNode = NULL;
			LilvNode* maxNode = NULL;
			lilv_port_get_range(state->plugin, port, &defNode, &minNode,
				&maxNode);
			value = _Lv2NodeValue(defNode, 0.0f);
			min = _Lv2NodeValue(minNode, 0.0f);
			max = _Lv2NodeValue(maxNode, 1.0f);
			if (defNode != NULL)
				lilv_node_free(defNode);
			if (minNode != NULL)
				lilv_node_free(minNode);
			if (maxNode != NULL)
				lilv_node_free(maxNode);
			if (max <= min) {
				min = 0.0f;
				max = 1.0f;
			}
			state->portValues[i] = value;
			state->controlPorts.push_back(i);
			state->controlMin.push_back(min);
			state->controlMax.push_back(max);
		} else {
			// Atom/MIDI ports need a worker and patch support; the
			// known limit of this host. Left unconnected.
			continue;
		}
		state->portKind[i] = kind;
	}

	lilv_node_free(inputNode);
	lilv_node_free(audioNode);
	lilv_node_free(controlNode);

	if (audioIn == 0 && audioOut == 0) {
		lilv_node_free(uriNode);
		_Lv2DestroyState(state);
		return B_NOT_SUPPORTED;
	}

	status_t status = B_OK;
	for (int32 i = 0; status == B_OK && i < audioIn; i++) {
		BString name;
		name.SetToFormat("in_%d", i);
		BMediaInput* input = new(std::nothrow) BMediaInput(name.String());
		if (input == NULL) {
			status = B_NO_MEMORY;
			break;
		}
		input->SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));
		status = RegisterInput(input);
		if (status != B_OK)
			delete input;
		else
			fInputs.push_back(input);
	}
	for (int32 i = 0; status == B_OK && i < audioOut; i++) {
		BString name;
		name.SetToFormat("out_%d", i);
		BMediaOutput* output = new(std::nothrow) BMediaOutput(name.String());
		if (output == NULL) {
			status = B_NO_MEMORY;
			break;
		}
		output->SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));
		status = RegisterOutput(output);
		if (status != B_OK)
			delete output;
		else
			fOutputs.push_back(output);
	}
	if (status != B_OK) {
		lilv_node_free(uriNode);
		_Lv2DestroyState(state);
		_TeardownPlugin();
		return status;
	}

	fPluginName = uri;
	fLv2 = state;
#ifdef VOS_HAS_SUIL
	state->ui = Lv2SelectUi(state->world, state->plugin, &state->uiExternal,
		&state->uiTypeUri);
#endif
	lilv_node_free(uriNode);
	return B_OK;
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
		BMediaInput* input = new(std::nothrow) BMediaInput(name.String());
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
		BMediaOutput* output = new(std::nothrow) BMediaOutput(name.String());
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
	if (value == NULL || size < sizeof(float))
		return;
	int32 port = id - kPropPortBase;
	if (port < 0)
		return;

#ifdef VOS_HAS_LILV
	if (fLv2 != NULL) {
		Lv2PluginState* state = (Lv2PluginState*)fLv2;
		if ((uint32)port >= state->portValues.size())
			return;
		if ((state->portKind[port] & 5) != 5)
			return;
		state->portValues[port] = *(const float*)value;
		_PushUiPortEvent((uint32)port, *(const float*)value);
		return;
	}
#endif

	if (fDescriptor == NULL || (uint32)port >= fDescriptor->PortCount)
		return;
	fControlValues[port] = *(const float*)value;
}


status_t
BMediaPluginAdapter::Start()
{
#ifdef VOS_HAS_LILV
	if (fLv2 != NULL) {
		status_t result = BMediaUnit::Start();
		if (result != B_OK)
			return result;
		result = _StartLv2();
		if (result != B_OK)
			return result;
		_PublishAndBindLv2();
		return B_OK;
	}
#endif

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
#ifdef VOS_HAS_LILV
	if (fLv2 != NULL) {
		_ProcessLv2(frameCount);
		return;
	}
#endif

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
status_t
BMediaPluginAdapter::_StartLv2()
{
	Lv2PluginState* state = (Lv2PluginState*)fLv2;
	if (state == NULL)
		return B_NO_INIT;

	if (state->instance != NULL)
		return B_OK;

	uint32 rate = SampleRate();
	if (rate == 0)
		rate = 48000;

	state->map.handle = state;
	state->map.map = _Lv2MapUri;
	state->unmap.handle = state;
	state->unmap.unmap = _Lv2UnmapUri;

	state->options[0].context = LV2_OPTIONS_INSTANCE;
	state->options[0].subject = 0;
	state->options[0].key = _Lv2MapUri(state, LV2_BUF_SIZE__maxBlockLength);
	state->options[0].size = sizeof(int32);
	state->options[0].type = _Lv2MapUri(state, LV2_ATOM__Int);
	state->options[0].value = &state->maxBlock;
	state->options[1].context = LV2_OPTIONS_INSTANCE;
	state->options[1].subject = 0;
	state->options[1].key = _Lv2MapUri(state, LV2_BUF_SIZE__minBlockLength);
	state->options[1].size = sizeof(int32);
	state->options[1].type = _Lv2MapUri(state, LV2_ATOM__Int);
	state->options[1].value = &state->minBlock;

	state->featureMap.URI = LV2_URID__map;
	state->featureMap.data = &state->map;
	state->featureUnmap.URI = LV2_URID__unmap;
	state->featureUnmap.data = &state->unmap;
	state->featureOptions.URI = LV2_OPTIONS__options;
	state->featureOptions.data = state->options;
	state->featureBounded.URI = LV2_BUF_SIZE__boundedBlockLength;
	state->featureBounded.data = NULL;
	state->featureList[0] = &state->featureMap;
	state->featureList[1] = &state->featureUnmap;
	state->featureList[2] = &state->featureOptions;
	state->featureList[3] = &state->featureBounded;
	state->featureList[4] = NULL;

	state->instance = lilv_plugin_instantiate(state->plugin, rate,
		state->featureList);
	if (state->instance == NULL)
		return B_NOT_SUPPORTED;

	for (uint32 i = 0; i < state->portKind.size(); i++) {
		uint8 kind = state->portKind[i];
		if ((kind & 2) != 0)
			lilv_instance_connect_port(state->instance, i,
				state->audioBuffers[i]);
		else if ((kind & 4) != 0)
			lilv_instance_connect_port(state->instance, i,
				&state->portValues[i]);
		else
			lilv_instance_connect_port(state->instance, i, NULL);
	}
	lilv_instance_activate(state->instance);

	state->featureInstanceAccess.URI = LV2_INSTANCE_ACCESS_URI;
	state->featureInstanceAccess.data
		= lilv_instance_get_handle(state->instance);
	state->featureDataAccess.URI = LV2_DATA_ACCESS_URI;
	state->featureDataAccess.data = (void*)
		lilv_instance_get_descriptor(state->instance)->extension_data;
	state->featureIdle.URI = LV2_UI__idleInterface;
	state->featureIdle.data = NULL;
	return B_OK;
}


void
BMediaPluginAdapter::_ProcessLv2(uint32 frameCount)
{
	Lv2PluginState* state = (Lv2PluginState*)fLv2;
	if (state == NULL || state->instance == NULL || frameCount == 0)
		return;
	fBlockSize.store(frameCount < kMaxBlockSize ? frameCount : kMaxBlockSize);

	float* inBuffers[kMaxLv2AudioPorts];
	size_t inFrames[kMaxLv2AudioPorts];
	size_t inCount = state->audioInPorts.size();
	if (inCount > kMaxLv2AudioPorts)
		inCount = kMaxLv2AudioPorts;
	for (size_t c = 0; c < inCount; c++) {
		size_t size = 0;
		inBuffers[c] = c < fInputs.size()
			? (float*)BufferFor(fInputs[c], &size) : NULL;
		inFrames[c] = inBuffers[c] != NULL ? size / sizeof(float) : 0;
	}

	float* outBuffers[kMaxLv2AudioPorts];
	size_t outFrames[kMaxLv2AudioPorts];
	size_t outCount = state->audioOutPorts.size();
	if (outCount > fOutputs.size())
		outCount = fOutputs.size();
	if (outCount > kMaxLv2AudioPorts)
		outCount = kMaxLv2AudioPorts;
	for (size_t c = 0; c < outCount; c++) {
		size_t size = 0;
		outBuffers[c] = (float*)BufferFor(fOutputs[c], &size);
		outFrames[c] = outBuffers[c] != NULL ? size / sizeof(float) : 0;
	}

	// The port buffers hold kMaxBlockSize frames; run the cycle in chunks.
	for (uint32 offset = 0; offset < frameCount; offset += kMaxBlockSize) {
		uint32 chunk = frameCount - offset;
		if (chunk > kMaxBlockSize)
			chunk = kMaxBlockSize;

		for (size_t c = 0; c < inCount; c++) {
			float* port = state->audioBuffers[state->audioInPorts[c]];
			if (port == NULL)
				continue;
			size_t have = inFrames[c] > offset ? inFrames[c] - offset : 0;
			if (have > chunk)
				have = chunk;
			if (have > 0)
				memcpy(port, inBuffers[c] + offset, have * sizeof(float));
			memset(port + have, 0, (chunk - have) * sizeof(float));
		}

		lilv_instance_run(state->instance, chunk);

		for (size_t c = 0; c < outCount; c++) {
			float* port = state->audioBuffers[state->audioOutPorts[c]];
			if (outBuffers[c] == NULL || port == NULL)
				continue;
			size_t room = outFrames[c] > offset ? outFrames[c] - offset : 0;
			if (room > chunk)
				room = chunk;
			memcpy(outBuffers[c] + offset, port, room * sizeof(float));
		}
	}
}


void
BMediaPluginAdapter::_PublishAndBindLv2()
{
	Lv2PluginState* state = (Lv2PluginState*)fLv2;
	uint32 nodeId = NodeId();
	if (state == NULL || nodeId == 0 || fControllable == NULL)
		return;

	int32 n = (int32)state->controlPorts.size();
	if (n == 0)
		return;

	std::vector<uint32> ids(n);
	std::vector<BString> names(n);
	std::vector<const char*> namePtrs(n);
	std::vector<uint32> types(n);
	std::vector<float> mins(n);
	std::vector<float> maxs(n);
	std::vector<float> steps(n);
	std::vector<const void*> values(n);
	std::vector<size_t> sizes(n);

	for (int32 i = 0; i < n; i++) {
		uint32 port = state->controlPorts[i];
		const LilvPort* lilvPort = lilv_plugin_get_port_by_index(
			state->plugin, port);
		const LilvNode* nameNode = lilvPort != NULL
			? lilv_port_get_name(state->plugin, lilvPort) : NULL;
		if (nameNode != NULL)
			names[i] = lilv_node_as_string(nameNode);
		else
			names[i].SetToFormat("port_%u", port);
		namePtrs[i] = names[i].String();
		ids[i] = kPropPortBase + port;
		types[i] = (uint32)B_MEDIA_PROP_FLOAT;
		mins[i] = state->controlMin[i];
		maxs[i] = state->controlMax[i];
		steps[i] = 0.01f;
		values[i] = &state->portValues[port];
		sizes[i] = sizeof(float);
	}

	PublishNodeParams(ids.data(), namePtrs.data(), types.data(), mins.data(),
		maxs.data(), steps.data(), values.data(), sizes.data(), n);
	fControllable->BindNode(nodeId);
}


void
BMediaPluginAdapter::_PushUiPortEvent(uint32 portIndex, float value)
{
#ifdef VOS_HAS_SUIL
	if (fUiHost == NULL)
		return;
	((Lv2UIHost*)fUiHost)->PortEvent(portIndex, value);
#endif
}


int32
BMediaPluginAdapter::Lv2PortIndexForSymbol(const char* symbol) const
{
#ifdef VOS_HAS_LILV
	Lv2PluginState* state = (Lv2PluginState*)fLv2;
	if (state != NULL && symbol != NULL) {
		LilvNode* symbolNode = lilv_new_uri(state->world, symbol);
		const LilvPort* port = lilv_plugin_get_port_by_symbol(state->plugin,
			symbolNode);
		lilv_node_free(symbolNode);
		if (port != NULL)
			return (int32)lilv_port_get_index(state->plugin, port);
	}
#endif
	return -1;
}


void*
BMediaPluginAdapter::Lv2State() const
{
#ifdef VOS_HAS_LILV
	return fLv2;
#else
	return NULL;
#endif
}


uint32
BMediaPluginAdapter::UiFlags() const
{
#ifdef VOS_HAS_SUIL
	if (fUiHost != NULL && ((Lv2UIHost*)fUiHost)->HasSupportedUi())
		return kMediaPluginUIFlagHasUi;
#endif
	return 0;
}


status_t
BMediaPluginAdapter::OpenUi(uint32 parentWindow)
{
#ifdef VOS_HAS_SUIL
	if (fLv2 == NULL || ((Lv2PluginState*)fLv2)->instance == NULL)
		return B_NO_INIT;
	if (fUiHost == NULL) {
		Lv2UIHost* host = new(std::nothrow) Lv2UIHost(this);
		if (host == NULL)
			return B_NO_MEMORY;
		host->RunUiThread();
		fUiHost = host;
	}
	return ((Lv2UIHost*)fUiHost)->Open(parentWindow);
#else
	return B_NOT_SUPPORTED;
#endif
}


void
BMediaPluginAdapter::CloseUi()
{
#ifdef VOS_HAS_SUIL
	if (fUiHost != NULL)
		((Lv2UIHost*)fUiHost)->Close();
#endif
}


void
BMediaPluginAdapter::UiPortWritten(uint32 portIndex, float value)
{
#ifdef VOS_HAS_LILV
	Lv2PluginState* state = (Lv2PluginState*)fLv2;
	if (state == NULL || portIndex >= state->portValues.size())
		return;
	if ((state->portKind[portIndex] & 5) != 5)
		return;
#endif

	// The controllable syncs the web and the change hook writes the
	// control cell the DSP reads.
	if (fControllable != NULL) {
		fControllable->SetParameterValue((int32)(kPropPortBase + portIndex),
			system_time(), &value, sizeof(value));
	} else
		_ApplyParam((int32)(kPropPortBase + portIndex), &value,
			sizeof(value));
}
#endif // VOS_HAS_LILV


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


#ifdef VOS_HAS_LILV
namespace {


struct Lv2CatalogEntry {
	BString	uri;
	BString	name;
};


// Built on first use and kept until the add-on unloads.
LilvWorld* sCatalogWorld = NULL;
std::vector<Lv2CatalogEntry>* sCatalog = NULL;


bool
_Lv2IsHostable(LilvWorld* world, const LilvPlugin* plugin)
{
	static const char* const kProvided[] = {
		LV2_URID__map, LV2_URID__unmap, LV2_OPTIONS__options,
		LV2_BUF_SIZE__boundedBlockLength, NULL
	};

	LilvNodes* required = lilv_plugin_get_required_features(plugin);
	bool hostable = true;
	LILV_FOREACH(nodes, i, required) {
		const char* uri = lilv_node_as_uri(lilv_nodes_get(required, i));
		bool known = false;
		for (int32 f = 0; kProvided[f] != NULL; f++) {
			if (uri != NULL && strcmp(uri, kProvided[f]) == 0)
				known = true;
		}
		if (!known)
			hostable = false;
	}
	lilv_nodes_free(required);
	if (!hostable)
		return false;

	LilvNode* inputNode = lilv_new_uri(world, LV2_CORE__InputPort);
	LilvNode* outputNode = lilv_new_uri(world, LV2_CORE__OutputPort);
	LilvNode* audioNode = lilv_new_uri(world, LV2_CORE__AudioPort);
	LilvNode* controlNode = lilv_new_uri(world, LV2_CORE__ControlPort);
	LilvNode* optionalNode = lilv_new_uri(world,
		LV2_CORE__connectionOptional);

	int32 audioPorts = 0;
	const uint32 count = lilv_plugin_get_num_ports(plugin);
	for (uint32 i = 0; i < count && hostable; i++) {
		const LilvPort* port = lilv_plugin_get_port_by_index(plugin, i);
		if (port == NULL)
			continue;
		if (lilv_port_is_a(plugin, port, audioNode))
			audioPorts++;
		else if (!lilv_port_is_a(plugin, port, controlNode)
				&& !lilv_port_has_property(plugin, port, optionalNode)) {
			// Atom and CV ports are not served by this host.
			hostable = false;
		}
	}
	lilv_node_free(inputNode);
	lilv_node_free(outputNode);
	lilv_node_free(audioNode);
	lilv_node_free(controlNode);
	lilv_node_free(optionalNode);
	return hostable && audioPorts > 0;
}


void
_Lv2BuildCatalog()
{
	if (sCatalog != NULL)
		return;

	sCatalog = new(std::nothrow) std::vector<Lv2CatalogEntry>();
	sCatalogWorld = lilv_world_new();
	if (sCatalog == NULL || sCatalogWorld == NULL)
		return;
	lilv_world_load_all(sCatalogWorld);

	const LilvPlugins* plugins = lilv_world_get_all_plugins(sCatalogWorld);
	LILV_FOREACH(plugins, i, plugins) {
		const LilvPlugin* plugin = lilv_plugins_get(plugins, i);
		if (!_Lv2IsHostable(sCatalogWorld, plugin))
			continue;

		Lv2CatalogEntry entry;
		entry.uri = lilv_node_as_uri(lilv_plugin_get_uri(plugin));
		LilvNode* name = lilv_plugin_get_name(plugin);
		entry.name = name != NULL ? lilv_node_as_string(name)
			: entry.uri.String();
		if (name != NULL)
			lilv_node_free(name);
		sCatalog->push_back(entry);
	}
}


__attribute__((destructor)) void
_Lv2FreeCatalog()
{
	delete sCatalog;
	sCatalog = NULL;
	if (sCatalogWorld != NULL)
		lilv_world_free(sCatalogWorld);
	sCatalogWorld = NULL;
}


} // anonymous namespace
#endif


extern "C" int32
media_unit_count()
{
#ifdef VOS_HAS_LILV
	BAutolock _(sAdapterLock);
	_Lv2BuildCatalog();
	return sCatalog != NULL ? (int32)sCatalog->size() : 0;
#else
	return 0;
#endif
}


extern "C" const char*
media_unit_name(int32 index)
{
#ifdef VOS_HAS_LILV
	BAutolock _(sAdapterLock);
	_Lv2BuildCatalog();
	if (sCatalog != NULL && index >= 0 && (size_t)index < sCatalog->size())
		return (*sCatalog)[index].name.String();
#endif
	return NULL;
}


extern "C" const char*
media_unit_group(int32 index)
{
#ifdef VOS_HAS_LILV
	BAutolock _(sAdapterLock);
	if (sCatalog != NULL && index >= 0 && (size_t)index < sCatalog->size())
		return "LV2";
#endif
	return NULL;
}


extern "C" BMediaUnit*
media_unit_instantiate(int32 index)
{
#ifdef VOS_HAS_LILV
	BString uri;
	{
		BAutolock _(sAdapterLock);
		_Lv2BuildCatalog();
		if (sCatalog == NULL || index < 0 || (size_t)index >= sCatalog->size())
			return NULL;
		uri = (*sCatalog)[index].uri;
	}

	BMediaPluginAdapter* unit = new(std::nothrow) BMediaPluginAdapter();
	if (unit == NULL || unit->InitCheck() != B_OK
			|| unit->LoadLv2(uri.String()) != B_OK) {
		delete unit;
		return NULL;
	}
	return unit;
#else
	return NULL;
#endif
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


extern "C" status_t
media_plugin_ui_command(BMediaUnit* unit, int32 command, void* arg)
{
	BMediaPluginAdapter* adapter = _AdapterForUnit(unit);
	if (adapter == NULL)
		return B_NOT_SUPPORTED;

#ifdef VOS_HAS_LILV
	switch (command) {
		case kMediaPluginUIQuery:
			if (arg != NULL)
				*(uint32*)arg = adapter->UiFlags();
			return B_OK;
		case kMediaPluginUIOpen:
			return adapter->OpenUi((uint32)(uintptr_t)arg);
		case kMediaPluginUIClose:
			adapter->CloseUi();
			return B_OK;
		default:
			return B_BAD_VALUE;
	}
#else
	return B_NOT_SUPPORTED;
#endif
}
