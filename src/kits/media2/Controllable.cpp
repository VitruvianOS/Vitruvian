/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/Controllable.h>

#include <new>
#include <stdlib.h>
#include <string.h>

#include <Message.h>
#include <OS.h>

#include <media2/MediaTheme.h>
#include <media2/ParameterWeb.h>


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
	fChangeHookCookie(NULL)
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
	return B_OK;
}


status_t
BControllable::GetParameterValue(int32 id, bigtime_t* lastChangeTime,
	void* value, size_t* ioSize)
{
	if (ioSize == NULL)
		return B_BAD_VALUE;
	ValueEntry* e = NULL;
	for (int32 i = 0; i < fValues.CountItems(); i++) {
		if (fValues.ItemAt(i)->id == id) { e = fValues.ItemAt(i); break; }
	}
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
	ValueEntry* e = NULL;
	for (int32 i = 0; i < fValues.CountItems(); i++) {
		if (fValues.ItemAt(i)->id == id) { e = fValues.ItemAt(i); break; }
	}
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
	e->lastChange = changeTime;

	if (fChangeHook != NULL)
		fChangeHook(id, changeTime, value, size, fChangeHookCookie);

	return B_OK;
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
	for (int32 i = 0; i < fValues.CountItems(); i++) {
		if (fValues.ItemAt(i)->id == id) { e = fValues.ItemAt(i); break; }
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
