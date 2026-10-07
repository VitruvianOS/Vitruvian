/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <midi2/MidiRoster.h>
#include <midi2/MidiConsumer.h>
#include <midi2/MidiProducer.h>

#include <new>
#include <string.h>

#include <Autolock.h>
#include <List.h>
#include <Locker.h>

#include <media2/MediaGraph.h>


static BMidiRoster* sRoster = NULL;
static BLocker sRosterLock("midi2 roster");
static BList sLocalEndpoints;
static BMessenger sGraphWatcher;
static bool sWatchingGraph = false;


static void
_add_local(BMidiEndpoint* endp)
{
	if (endp == NULL)
		return;
	BAutolock lock(sRosterLock);
	if (sLocalEndpoints.IndexOf(endp) < 0)
		sLocalEndpoints.AddItem(endp);
}


static void
_remove_local(BMidiEndpoint* endp)
{
	if (endp == NULL)
		return;
	BAutolock lock(sRosterLock);
	sLocalEndpoints.RemoveItem(endp);
}


BMidiRoster::BMidiRoster()
{
	fLooper = NULL;
	fServer = NULL;
	fEndpoints = new BList;
	fWatchCursor = 0;
	fNextLocalId = 1;
	fGraphDirty = true;
	memset(_reserved, 0, sizeof(_reserved));
}


BMidiRoster::~BMidiRoster()
{
	delete fEndpoints;
	fEndpoints = NULL;
}


void
BMidiRoster::_RefreshFromGraph()
{
	// Remote MIDI endpoints: wrap foreign graph nodes that expose MIDI ports.
	// Local endpoints live in sLocalEndpoints and need no graph scan.
	fGraphDirty = false;
}


BMidiRoster*
BMidiRoster::MidiRoster()
{
	BAutolock lock(sRosterLock);
	if (sRoster == NULL)
		sRoster = new(std::nothrow) BMidiRoster();
	return sRoster;
}


void
BMidiRoster::CreateLocal(BMidiEndpoint* endp)
{
	_add_local(endp);
}


void
BMidiRoster::DeleteLocal(BMidiEndpoint* endp)
{
	_remove_local(endp);
}


status_t
BMidiRoster::SendRequest(BMessage*, BMessage*)
{
	// No midi_server: every endpoint is a real PipeWire node.
	return B_OK;
}


status_t
BMidiRoster::Register(BMidiEndpoint* endp)
{
	if (endp == NULL)
		return B_BAD_VALUE;
	_add_local(endp);
	return B_OK;
}


status_t
BMidiRoster::Unregister(BMidiEndpoint* endp)
{
	if (endp == NULL)
		return B_BAD_VALUE;
	_remove_local(endp);
	return B_OK;
}


BMidiEndpoint*
BMidiRoster::NextEndpoint(int32* id)
{
	if (id == NULL)
		return NULL;

	BAutolock lock(sRosterLock);
	int32 count = sLocalEndpoints.CountItems();
	if (*id < 0)
		*id = 0;
	if (*id >= count)
		return NULL;

	BMidiEndpoint* endp = (BMidiEndpoint*)sLocalEndpoints.ItemAt(*id);
	if (endp == NULL)
		return NULL;
	*id = *id + 1;
	// NextEndpoint/FindEndpoint increment Acquire() on every return.
	endp->Acquire();
	return endp;
}


BMidiProducer*
BMidiRoster::NextProducer(int32* id)
{
	if (id == NULL)
		return NULL;

	BAutolock lock(sRosterLock);
	int32 count = sLocalEndpoints.CountItems();
	if (*id < 0)
		*id = 0;
	while (*id < count) {
		BMidiEndpoint* endp = (BMidiEndpoint*)sLocalEndpoints.ItemAt(*id);
		*id = *id + 1;
		if (endp != NULL && endp->IsProducer() && endp->IsLocal()) {
			endp->Acquire();
			return dynamic_cast<BMidiProducer*>(endp);
		}
	}
	return NULL;
}


BMidiConsumer*
BMidiRoster::NextConsumer(int32* id)
{
	if (id == NULL)
		return NULL;

	BAutolock lock(sRosterLock);
	int32 count = sLocalEndpoints.CountItems();
	if (*id < 0)
		*id = 0;
	while (*id < count) {
		BMidiEndpoint* endp = (BMidiEndpoint*)sLocalEndpoints.ItemAt(*id);
		*id = *id + 1;
		if (endp != NULL && endp->IsConsumer() && endp->IsLocal()) {
			endp->Acquire();
			return dynamic_cast<BMidiConsumer*>(endp);
		}
	}
	return NULL;
}


BMidiEndpoint*
BMidiRoster::FindEndpoint(int32 id, bool localOnly)
{
	BAutolock lock(sRosterLock);
	for (int32 i = 0; i < sLocalEndpoints.CountItems(); i++) {
		BMidiEndpoint* endp = (BMidiEndpoint*)sLocalEndpoints.ItemAt(i);
		if (endp == NULL || endp->ID() != id)
			continue;
		if (localOnly && !endp->IsLocal())
			continue;
		endp->Acquire();
		return endp;
	}
	return NULL;
}


BMidiProducer*
BMidiRoster::FindProducer(int32 id, bool localOnly)
{
	BMidiEndpoint* endp = FindEndpoint(id, localOnly);
	if (endp == NULL || !endp->IsProducer())
		return NULL;
	return dynamic_cast<BMidiProducer*>(endp);
}


BMidiConsumer*
BMidiRoster::FindConsumer(int32 id, bool localOnly)
{
	BMidiEndpoint* endp = FindEndpoint(id, localOnly);
	if (endp == NULL || !endp->IsConsumer())
		return NULL;
	return dynamic_cast<BMidiConsumer*>(endp);
}


void
BMidiRoster::StartWatching(const BMessenger* msngr)
{
	if (msngr == NULL)
		return;
	sGraphWatcher = *msngr;
	sWatchingGraph = true;
	MediaGraph()->StartWatching(sGraphWatcher,
		BMediaGraph::B_MEDIA_CLIENT_ADDED
		| BMediaGraph::B_MEDIA_CLIENT_REMOVED
		| BMediaGraph::B_MEDIA_STREAMS_CHANGED
		| BMediaGraph::B_MEDIA_LINKS_CHANGED);
}


void
BMidiRoster::StopWatching()
{
	if (!sWatchingGraph)
		return;
	MediaGraph()->StopWatching(sGraphWatcher);
	sWatchingGraph = false;
	sGraphWatcher = BMessenger();
}


void
BMidiRoster::_Reserved1()
{
}


void
BMidiRoster::_Reserved2()
{
}


void
BMidiRoster::_Reserved3()
{
}


void
BMidiRoster::_Reserved4()
{
}


void
BMidiRoster::_Reserved5()
{
}


void
BMidiRoster::_Reserved6()
{
}


void
BMidiRoster::_Reserved7()
{
}


void
BMidiRoster::_Reserved8()
{
}
