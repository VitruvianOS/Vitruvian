/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/Controllable.h>

#include <new>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include <Message.h>
#include <Locker.h>
#include <Autolock.h>
#include <OS.h>

#include <spa/param/props.h>
#include <spa/pod/iter.h>
#include <spa/utils/type.h>

#include <media2/MediaTheme.h>
#include <media2/ParameterWeb.h>

#include "PipeWireBackend.h"


using BPrivate::media::PipeWireBackend;


struct BControllable::ValueEntry {
	int32		id;
	bigtime_t	lastChange;
	void*		data;
	size_t		size;

	ValueEntry()
		: id(0), lastChange(0), data(NULL), size(0) {}
	~ValueEntry() { free(data); }
};


BControllable::BControllable()
	:
	fWeb(NULL),
	fValues(),
	fWatchers(),
	fChangeHook(NULL),
	fChangeHookCookie(NULL),
	fLock("BControllable"),
	fNodeId(0)
{
}


void
BControllable::SetChangeHook(change_hook hook, void* cookie)
{
	fChangeHook = hook;
	fChangeHookCookie = cookie;
}


BControllable::~BControllable()
{
	UnbindNode();
	delete fWeb;
}


BParameterWeb*
BControllable::Web() const
{
	return fWeb;
}


status_t
BControllable::SetParameterWeb(BParameterWeb* web)
{
	if (fWeb == web)
		return B_OK;
	delete fWeb;
	fWeb = web;
	if (fWeb != NULL) {
		for (int32 g = 0; g < fWeb->CountGroups(); g++)
			fWeb->GroupAt(g)->SetControllable(this);
	}
	return B_OK;
}


uint32
BControllable::BoundNodeId() const
{
	return fNodeId;
}


BControllable::ValueEntry*
BControllable::_FindValue(int32 id) const
{
	for (int32 i = 0; i < fValues.CountItems(); i++) {
		if (fValues.ItemAt(i)->id == id)
			return fValues.ItemAt(i);
	}
	return NULL;
}


status_t
BControllable::_StoreValue(int32 id, bigtime_t when,
	const void* value, size_t size)
{
	ValueEntry* e = _FindValue(id);
	if (e == NULL) {
		e = new(std::nothrow) ValueEntry();
		if (e == NULL)
			return B_NO_MEMORY;
		e->id = id;
		fValues.AddItem(e);
	}
	void* newData = malloc(size);
	if (newData == NULL)
		return B_NO_MEMORY;
	memcpy(newData, value, size);
	free(e->data);
	e->data       = newData;
	e->size       = size;
	e->lastChange = when;
	return B_OK;
}


status_t
BControllable::BindNode(uint32 nodeId)
{
	if (nodeId == 0)
		return B_BAD_VALUE;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_ERROR;

	UnbindNode();

	std::vector<PipeWireBackend::NodePropInfo> infos;
	status_t result = backend->QueryNodePropInfo(nodeId, infos);
	if (result != B_OK)
		return result;

	std::vector<PipeWireBackend::NodePropValue> values;
	result = backend->QueryNodeProps(nodeId, values);
	if (result != B_OK && result != B_ENTRY_NOT_FOUND)
		return result;

	BParameterWeb* web = new(std::nothrow) BParameterWeb();
	if (web == NULL)
		return B_NO_MEMORY;
	BParameterGroup* group = web->MakeGroup("Node Parameters");
	if (group == NULL) {
		delete web;
		return B_NO_MEMORY;
	}

	for (size_t i = 0; i < infos.size(); i++) {
		const PipeWireBackend::NodePropInfo& info = infos[i];
		const char* name = info.name.empty()
			? "parameter" : info.name.c_str();

		if (info.hasRange && info.valueType == SPA_TYPE_Float) {
			group->MakeContinuousParameter((int32)info.id,
				B_MEDIA_NO_TYPE, name, B_GENERIC, "",
				info.minValue, info.maxValue,
				info.step > 0.0f ? info.step : 0.01f);
		} else if (info.labels.size() > 0) {
			BDiscreteParameter* dp = group->MakeDiscreteParameter(
				(int32)info.id, B_MEDIA_NO_TYPE, name, B_ENABLE);
			if (dp != NULL) {
				for (size_t l = 0; l < info.labels.size(); l++)
					dp->AddItem((int32)info.labels[l].first,
						info.labels[l].second.c_str());
			}
		} else if (info.valueType == SPA_TYPE_Bool) {
			BDiscreteParameter* dp = group->MakeDiscreteParameter(
				(int32)info.id, B_MEDIA_NO_TYPE, name, B_ENABLE);
			if (dp != NULL) {
				dp->AddItem(0, "Off");
				dp->AddItem(1, "On");
			}
		} else if (info.valueType == SPA_TYPE_Int
				|| info.valueType == SPA_TYPE_Id) {
			if (info.hasRange) {
				group->MakeContinuousParameter((int32)info.id,
					B_MEDIA_NO_TYPE, name, B_GENERIC, "",
					info.minValue, info.maxValue,
					info.step > 0.0f ? info.step : 1.0f);
			} else {
				group->MakeDiscreteParameter((int32)info.id,
					B_MEDIA_NO_TYPE, name, B_GENERIC);
			}
		} else if (info.valueType == SPA_TYPE_Float) {
			group->MakeContinuousParameter((int32)info.id,
				B_MEDIA_NO_TYPE, name, B_GENERIC, "", 0.0f, 1.0f, 0.01f);
		}
	}

	SetParameterWeb(web);

	{
		BAutolock _(fLock);
		for (size_t i = 0; i < values.size(); i++) {
			const PipeWireBackend::NodePropValue& v = values[i];
			_StoreValue((int32)v.id, system_time(), v.bytes.data(),
				v.bytes.size());
		}
		fNodeId = nodeId;
	}

	result = backend->WatchNodeParams(nodeId, this);
	if (result != B_OK) {
		BAutolock _(fLock);
		fNodeId = 0;
		return result;
	}

	return B_OK;
}


status_t
BControllable::UnbindNode()
{
	if (fNodeId == 0)
		return B_OK;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend != NULL)
		backend->UnwatchNodeParams(fNodeId, this);

	BAutolock _(fLock);
	fNodeId = 0;
	return B_OK;
}


status_t
BControllable::GetParameterValue(int32 id, bigtime_t* lastChangeTime,
	void* value, size_t* ioSize)
{
	if (ioSize == NULL)
		return B_BAD_VALUE;

	if (fNodeId != 0) {
		PipeWireBackend* backend = PipeWireBackend::GetInstance();
		if (backend != NULL) {
			std::vector<PipeWireBackend::NodePropValue> values;
			status_t result = backend->QueryNodeProps(fNodeId, values);
			if (result == B_OK) {
				BAutolock _(fLock);
				for (size_t i = 0; i < values.size(); i++) {
					if ((int32)values[i].id == id) {
						const size_t copy = *ioSize < values[i].bytes.size()
							? *ioSize : values[i].bytes.size();
						if (value != NULL && copy > 0)
							memcpy(value, values[i].bytes.data(), copy);
						*ioSize = values[i].bytes.size();
						if (lastChangeTime != NULL)
							*lastChangeTime = system_time();
						_StoreValue(id, system_time(),
							values[i].bytes.data(), values[i].bytes.size());
						return B_OK;
					}
				}
			}
		}
	}

	BAutolock _(fLock);
	ValueEntry* e = _FindValue(id);
	if (e == NULL || e->data == NULL) {
		*ioSize = 0;
		return B_ENTRY_NOT_FOUND;
	}
	if (lastChangeTime != NULL)
		*lastChangeTime = e->lastChange;
	const size_t copy = *ioSize < e->size ? *ioSize : e->size;
	if (value != NULL && copy > 0)
		memcpy(value, e->data, copy);
	*ioSize = e->size;
	return B_OK;
}


status_t
BControllable::SetParameterValue(int32 id, bigtime_t changeTime,
	const void* value, size_t size)
{
	if (value == NULL || size == 0)
		return B_BAD_VALUE;

	if (fNodeId != 0) {
		PipeWireBackend* backend = PipeWireBackend::GetInstance();
		if (backend != NULL) {
			// Value type is taken from the web when available.
			uint32 valueType = SPA_TYPE_Float;
			if (fWeb != NULL) {
				for (int32 g = 0; g < fWeb->CountGroups(); g++) {
					BParameterGroup* group = fWeb->GroupAt(g);
					for (int32 p = 0; p < group->CountParameters(); p++) {
						BParameter* param = group->ParameterAt(p);
						if (param == NULL || param->ID() != id)
							continue;
						if (param->Type()
								== BParameter::B_CONTINUOUS_PARAMETER) {
							if (param->ValueType() == B_INT32_TYPE)
								valueType = SPA_TYPE_Int;
							else
								valueType = SPA_TYPE_Float;
						} else if (param->Type()
								== BParameter::B_DISCRETE_PARAMETER) {
							if (param->ValueType() == B_INT32_TYPE
									&& size == sizeof(int32))
								valueType = SPA_TYPE_Int;
							else if (size == 1)
								valueType = SPA_TYPE_Bool;
							else
								valueType = SPA_TYPE_Int;
						}
						break;
					}
				}
			}
			status_t result = backend->SetNodeProp(fNodeId, (uint32)id,
				valueType, value, size);
			if (result != B_OK)
				return result;
		}
	}

	{
		BAutolock _(fLock);
		status_t result = _StoreValue(id, changeTime, value, size);
		if (result != B_OK)
			return result;
	}

	// Keep the web's parameter stores in sync for UI GetValue/SetValue.
	if (fWeb != NULL) {
		for (int32 g = 0; g < fWeb->CountGroups(); g++) {
			BParameterGroup* group = fWeb->GroupAt(g);
			for (int32 p = 0; p < group->CountParameters(); p++) {
				BParameter* param = group->ParameterAt(p);
				if (param == NULL || param->ID() != id)
					continue;
				param->SetControllable(NULL);
				if (param->Type() == BParameter::B_CONTINUOUS_PARAMETER) {
					BContinuousParameter* cp
						= (BContinuousParameter*)param;
					cp->SetValue((void*)value, size, changeTime);
				} else if (param->Type()
						== BParameter::B_DISCRETE_PARAMETER) {
					BDiscreteParameter* dp
						= (BDiscreteParameter*)param;
					dp->SetValue((void*)value, size, changeTime);
				}
				param->SetControllable(this);
			}
		}
	}

	if (fChangeHook != NULL)
		fChangeHook(id, changeTime, value, size, fChangeHookCookie);

	return B_OK;
}


// Called on the PipeWire loop thread when another client changes a param.
void
BControllable::_ApplyProps(const void* pod)
{
	const spa_pod* spaPod = (const spa_pod*)pod;
	if (spaPod == NULL || !spa_pod_is_object(spaPod))
		return;

	std::vector<PipeWireBackend::NodePropValue> values;
	const spa_pod_object* object = (const spa_pod_object*)spaPod;
	const spa_pod_prop* prop;
	SPA_POD_OBJECT_FOREACH(object, prop) {
		PipeWireBackend::NodePropValue v;
		v.id = prop->key;
		v.valueType = SPA_POD_TYPE(&prop->value);

		const char* str = NULL;
		float f = 0.0f;
		int32_t i = 0;
		bool b = false;
		if (spa_pod_get_string(&prop->value, &str) >= 0 && str != NULL) {
			size_t len = strlen(str) + 1;
			v.bytes.resize(len);
			memcpy(v.bytes.data(), str, len);
		} else if (spa_pod_get_float(&prop->value, &f) >= 0) {
			v.bytes.resize(sizeof(float));
			memcpy(v.bytes.data(), &f, sizeof(float));
		} else if (spa_pod_get_int(&prop->value, &i) >= 0) {
			v.bytes.resize(sizeof(int32_t));
			memcpy(v.bytes.data(), &i, sizeof(int32_t));
		} else if (spa_pod_get_bool(&prop->value, &b) >= 0) {
			v.bytes.resize(sizeof(bool));
			memcpy(v.bytes.data(), &b, sizeof(bool));
		} else {
			continue;
		}
		values.push_back(v);
	}

	{
		BAutolock _(fLock);
		for (size_t i = 0; i < values.size(); i++) {
			_StoreValue((int32)values[i].id, system_time(),
				values[i].bytes.data(), values[i].bytes.size());
		}
	}

	if (fWeb != NULL) {
		for (size_t i = 0; i < values.size(); i++) {
			for (int32 g = 0; g < fWeb->CountGroups(); g++) {
				BParameterGroup* group = fWeb->GroupAt(g);
				for (int32 p = 0; p < group->CountParameters(); p++) {
					BParameter* param = group->ParameterAt(p);
					if (param == NULL
							|| param->ID() != (int32)values[i].id)
						continue;
					param->SetControllable(NULL);
					if (param->Type()
							== BParameter::B_CONTINUOUS_PARAMETER) {
						BContinuousParameter* cp
							= (BContinuousParameter*)param;
						cp->SetValue(values[i].bytes.data(),
							values[i].bytes.size(), system_time());
					} else if (param->Type()
							== BParameter::B_DISCRETE_PARAMETER) {
						BDiscreteParameter* dp
							= (BDiscreteParameter*)param;
						dp->SetValue(values[i].bytes.data(),
							values[i].bytes.size(), system_time());
					}
					param->SetControllable(this);
				}
			}
		}
	}

	// External changes only update kit-side state and notify; never call
	// the change hook (app code) from the loop thread.
	for (size_t i = 0; i < values.size(); i++)
		BroadcastChangedParameter((int32)values[i].id);
}


status_t
BControllable::StartControlPanel(BMessenger watcher)
{
	if (!watcher.IsValid())
		return B_BAD_VALUE;
	for (int32 i = 0; i < fWatchers.CountItems(); i++) {
		if (*fWatchers.ItemAt(i) == watcher)
			return B_OK;
	}
	BMessenger* copy = new(std::nothrow) BMessenger(watcher);
	if (copy == NULL)
		return B_NO_MEMORY;
	fWatchers.AddItem(copy);
	return B_OK;
}


status_t
BControllable::StopControlPanel(BMessenger watcher)
{
	for (int32 i = 0; i < fWatchers.CountItems(); i++) {
		if (*fWatchers.ItemAt(i) == watcher) {
			fWatchers.RemoveItemAt(i);
			return B_OK;
		}
	}
	return B_ENTRY_NOT_FOUND;
}


void
BControllable::BroadcastChangedParameter(int32 id)
{
	ValueEntry* e = NULL;
	{
		BAutolock _(fLock);
		e = _FindValue(id);
	}

	BMessage msg(B_MEDIA_NEW_PARAMETER_VALUE);
	msg.AddInt32("be:parameter", id);
	if (e != NULL) {
		msg.AddInt64("be:when", e->lastChange);
		if (e->data != NULL && e->size > 0)
			msg.AddData("be:value", B_RAW_TYPE, e->data, e->size);
	}
	for (int32 i = 0; i < fWatchers.CountItems(); i++)
		fWatchers.ItemAt(i)->SendMessage(&msg);
}


BView*
BControllable::MakeView()
{
	if (fWeb == NULL)
		return NULL;
	return BMediaTheme::ViewFor(fWeb);
}
