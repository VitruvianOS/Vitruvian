/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "OutputAddonManager.h"

#include "AppServer.h"
#include "Desktop.h"
#include "EventDispatcher.h"
#include "RenderingBuffer.h"
#include "Screen.h"
#include "ScreenManager.h"

#include <AppMisc.h>
#include <MessengerPrivate.h>
#include <system_info.h>

#include <Autolock.h>
#include <Messenger.h>
#include <Message.h>

#include <errno.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <kernel/image.h>


extern char** environ;


OutputAddonManager* gOutputAddonManager;


namespace {


const int32 kMaxFramePayload = (int32)sizeof(output_addon_frame)
	+ kOutputAddonMaxRects * (int32)sizeof(output_addon_rect);


void
_CopyToWireRect(output_addon_rect* out, const clipping_rect& in,
	int32 width, int32 height)
{
	out->left = in.left < 0 ? 0 : in.left;
	out->top = in.top < 0 ? 0 : in.top;
	out->right = in.right + 1 > width ? width : in.right + 1;
	out->bottom = in.bottom + 1 > height ? height : in.bottom + 1;
}


void
_FlattenToPort(port_id port, BMessage& message)
{
	ssize_t size = message.FlattenedSize();
	if (size <= 0)
		return;

	char* flat = new(std::nothrow) char[size];
	if (flat == NULL)
		return;

	if (message.Flatten(flat, size) == B_OK) {
		// Timed: a wedged add-on must not block app_server, least of
		// all on the capture path.
		write_port_etc(port, message.what, flat, size, B_RELATIVE_TIMEOUT,
			100000);
	}

	delete[] flat;
}


} // namespace


OutputAddonManager::OutputAddonManager(AppServer* appServer)
	:
	BLooper(OUTPUT_ADDON_PORT_NAME),
	fAppServer(appServer),
	fInitStatus(B_NO_INIT),
	fCaptureSubscribers(0),
	fInputSubscribers(0),
	fLock("output addon manager")
{
}


OutputAddonManager::~OutputAddonManager()
{
}


status_t
OutputAddonManager::InitCheck()
{
	if (fInitStatus != B_NO_INIT)
		return fInitStatus;

	Run();

	// One kernel-side watch covers every add-on team: a crash arrives
	// as B_TEAM_DELETED even when nothing was in flight on its ports.
	BMessenger self(this);
	BMessenger::Private private_(self);
	fInitStatus = __start_watching_system(-1, B_WATCH_SYSTEM_TEAM_DELETION,
		private_.Port(), private_.Token());

	return fInitStatus;
}


bool
OutputAddonManager::_SenderAllowed(BMessage* message) const
{
	// Same policy as AS_GET_DESKTOP: the session user's own app_server
	// accepts its own user; a system-mode server accepts anyone and the
	// session namespace scopes the port once that lands.
	uid_t serverUid = getuid();
	if (serverUid == 0)
		return true;

	uid_t senderUid = message->SenderUid();
	return senderUid != (uid_t)-1 && senderUid == serverUid;
}


void
OutputAddonManager::_SendReply(BMessage* message, status_t status)
{
	int32 replyPort = message->GetInt32("reply port", -1);
	if (replyPort < 0)
		return;

	BMessage reply(message->what);
	reply.AddInt32("status", status);
	_FlattenToPort(replyPort, reply);
}


void
OutputAddonManager::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgOutputAddonRegister:
			_HandleRegister(message);
			break;

		case kMsgOutputAddonQuit:
			_HandleQuit(message);
			break;

		case kMsgOutputAddonCreateVirtual:
			_HandleCreateVirtual(message);
			break;

		case kMsgOutputAddonDeleteVirtual:
			_HandleDeleteVirtual(message);
			break;

		case kMsgOutputAddonAttach:
			_HandleAttach(message);
			break;

		case kMsgOutputAddonDetach:
			_HandleDetach(message);
			break;

		case kMsgOutputAddonInjectInput:
			_HandleInjectInput(message);
			break;

		case kMsgOutputAddonLaunch:
			_HandleLaunch(message);
			break;

		case B_SYSTEM_OBJECT_UPDATE:
			if (message->GetInt32("opcode", -1) == B_TEAM_DELETED) {
				_HandleTeamDeleted(message->GetInt32("team", -1));
			}
			break;

		default:
			BLooper::MessageReceived(message);
			break;
	}
}


OutputAddonManager::addon_team*
OutputAddonManager::_TeamEntry(int32 team, bool create)
{
	for (int32 i = 0; i < fTeams.CountItems(); i++) {
		addon_team* entry = fTeams.ItemAt(i);
		if (entry->team == team)
			return entry;
	}

	if (!create)
		return NULL;

	addon_team* entry = new(std::nothrow) addon_team;
	if (entry == NULL)
		return NULL;

	entry->team = team;
	entry->connections = 0;
	entry->spawned = false;
	entry->respawn = false;
	if (!fTeams.AddItem(entry)) {
		delete entry;
		return NULL;
	}

	return entry;
}


void
OutputAddonManager::_HandleRegister(BMessage* message)
{
	if (!_SenderAllowed(message)) {
		_SendReply(message, B_NOT_ALLOWED);
		return;
	}

	int32 team = message->GetInt32("team", -1);
	if (team < 0) {
		_SendReply(message, B_BAD_VALUE);
		return;
	}

	BAutolock locker(fLock);
	addon_team* entry = _TeamEntry(team, true);
	if (entry == NULL) {
		_SendReply(message, B_NO_MEMORY);
		return;
	}

	entry->connections++;

	BMessage reply(message->what);
	reply.AddInt32("status", B_OK);
	reply.AddInt32("server team", (int32)BPrivate::current_team());
	reply.AddInt32("protocol", kOutputAddonProtocolVersion);

	int32 replyPort = message->GetInt32("reply port", -1);
	if (replyPort >= 0)
		_FlattenToPort(replyPort, reply);
}


void
OutputAddonManager::_HandleQuit(BMessage* message)
{
	if (!_SenderAllowed(message)) {
		_SendReply(message, B_NOT_ALLOWED);
		return;
	}

	int32 team = message->GetInt32("team", -1);

	BAutolock locker(fLock);
	_RemoveSubscriptions(team);

	for (int32 i = 0; i < fTeams.CountItems(); i++) {
		addon_team* entry = fTeams.ItemAt(i);
		if (entry->team != team)
			continue;

		entry->connections--;
		if (entry->connections <= 0)
			fTeams.RemoveItem(entry);
		break;
	}

	_SendReply(message, B_OK);
}


void
OutputAddonManager::_HandleCreateVirtual(BMessage* message)
{
	if (!_SenderAllowed(message)) {
		_SendReply(message, B_NOT_ALLOWED);
		return;
	}

	int32 width = message->GetInt32("width", 0);
	int32 height = message->GetInt32("height", 0);
	bool testPattern = message->GetBool("test pattern", false);
	const char* name = message->GetString("name", "virtual");

	if (width <= 0 || height <= 0
		|| width > kOutputAddonMaxWidth || height > kOutputAddonMaxHeight) {
		_SendReply(message, B_BAD_VALUE);
		return;
	}

	int32 id = -1;
	status_t status = gScreenManager->AddVirtualOutput((uint16)width,
		(uint16)height, name, testPattern, &id);
	if (status != B_OK) {
		_SendReply(message, status);
		return;
	}

	BMessage reply(message->what);
	reply.AddInt32("status", B_OK);
	reply.AddInt32("screen", id);

	int32 replyPort = message->GetInt32("reply port", -1);
	if (replyPort >= 0)
		_FlattenToPort(replyPort, reply);
}


void
OutputAddonManager::_HandleDeleteVirtual(BMessage* message)
{
	if (!_SenderAllowed(message)) {
		_SendReply(message, B_NOT_ALLOWED);
		return;
	}

	int32 screen = message->GetInt32("screen", -1);
	if (screen < 0) {
		_SendReply(message, B_BAD_VALUE);
		return;
	}

	// Detach whatever is still watching before the output goes away.
	BAutolock locker(fLock);
	for (int32 i = fSubscriptions.CountItems(); i-- > 0;) {
		subscription* sub = fSubscriptions.ItemAt(i);
		if (sub->screen == screen)
			_DetachLocked(sub);
	}

	locker.Unlock();

	status_t status = gScreenManager->RemoveVirtualOutput(screen);
	_SendReply(message, status);
}


OutputAddonManager::subscription*
OutputAddonManager::_FindSubscription(int32 team, int32 screen)
{
	for (int32 i = 0; i < fSubscriptions.CountItems(); i++) {
		subscription* sub = fSubscriptions.ItemAt(i);
		if (sub->team == team && sub->screen == screen)
			return sub;
	}

	return NULL;
}


void
OutputAddonManager::_HandleAttach(BMessage* message)
{
	if (!_SenderAllowed(message)) {
		_SendReply(message, B_NOT_ALLOWED);
		return;
	}

	int32 team = message->GetInt32("team", -1);
	int32 screenID = message->GetInt32("screen", -1);
	uint32 transport = (uint32)message->GetInt32("transport", 0);
	port_id updatePort = message->GetInt32("update port", -1);
	port_id eventPort = message->GetInt32("event port", -1);

	if (team < 0 || screenID < 0 || updatePort < B_OK) {
		_SendReply(message, B_BAD_VALUE);
		return;
	}

	if (transport != kOutputAddonTransportSharedDamage) {
		// The picture and video lanes share the control channel and the
		// damage accounting; only the payload producer differs. Their
		// producers land with the X and streaming consumers.
		_SendReply(message, B_UNSUPPORTED);
		return;
	}

	port_info portInfo;
	if (get_port_info(updatePort, &portInfo) != B_OK
		|| (eventPort >= B_OK && get_port_info(eventPort, &portInfo) != B_OK)) {
		_SendReply(message, B_BAD_VALUE);
		return;
	}

	if (gScreenManager == NULL) {
		_SendReply(message, B_NO_INIT);
		return;
	}

	gScreenManager->Lock();
	Screen* screen = gScreenManager->ScreenByID(screenID);
	HWInterface* interface = screen != NULL ? screen->HWInterface() : NULL;
	gScreenManager->Unlock();

	if (interface == NULL) {
		_SendReply(message, B_BAD_VALUE);
		return;
	}

	// Lock order: interface, then fLock (the capture hook runs with the
	// interface locked and takes fLock).
	if (!interface->LockExclusiveAccess()) {
		_SendReply(message, B_ERROR);
		return;
	}

	BAutolock locker(fLock);

	if (_FindSubscription(team, screenID) != NULL) {
		interface->UnlockExclusiveAccess();
		_SendReply(message, B_BAD_VALUE);
		return;
	}

	RenderingBuffer* buffer = interface->DrawingBuffer();
	if (buffer == NULL || buffer->InitCheck() != B_OK
		|| buffer->Width() == 0 || buffer->Height() == 0
		|| buffer->Width() > (uint32)kOutputAddonMaxWidth
		|| buffer->Height() > (uint32)kOutputAddonMaxHeight) {
		interface->UnlockExclusiveAccess();
		_SendReply(message, B_NO_INIT);
		return;
	}

	subscription* sub = new(std::nothrow) subscription;
	if (sub == NULL) {
		interface->UnlockExclusiveAccess();
		_SendReply(message, B_NO_MEMORY);
		return;
	}

	sub->team = team;
	sub->screen = screenID;
	sub->interface = interface;
	sub->transport = transport;
	sub->updatePort = updatePort;
	sub->eventPort = eventPort;
	sub->area = -1;
	sub->header = NULL;
	sub->bytesPerRow = buffer->BytesPerRow();
	sub->width = buffer->Width();
	sub->height = buffer->Height();
	sub->generation = 0;

	int32 areaSize = output_addon_fb_area_size(sub->bytesPerRow,
		sub->height, 2);
	void* base = NULL;
	sub->area = create_area("output addon frame", &base, B_ANY_ADDRESS,
		areaSize, B_LAZY_LOCK, B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);
	if (sub->area < B_OK || base == NULL) {
		status_t status = sub->area < B_OK ? sub->area : B_NO_MEMORY;
		delete sub;
		interface->UnlockExclusiveAccess();
		_SendReply(message, status);
		return;
	}

	sub->header = (output_addon_fb_header*)base;
	memset(sub->header, 0, sizeof(output_addon_fb_header));
	sub->header->magic = OUTPUT_ADDON_MAGIC;
	sub->header->width = sub->width;
	sub->header->height = sub->height;
	sub->header->bytes_per_row = sub->bytesPerRow;
	sub->header->buffer_count = 2;
	sub->header->transport = transport;

	if (!fSubscriptions.AddItem(sub)) {
		delete_area(sub->area);
		delete sub;
		interface->UnlockExclusiveAccess();
		_SendReply(message, B_NO_MEMORY);
		return;
	}

	interface->AddListener(this);
	area_id area = sub->area;
	int32 width = sub->width;
	int32 height = sub->height;
	int32 bytesPerRow = sub->bytesPerRow;
	atomic_add(&fCaptureSubscribers, 1);
	if (eventPort >= B_OK)
		atomic_add(&fInputSubscribers, 1);

	// Prime the client with one full-frame damage so it does not wait
	// for the first compositor activity.
	sub->pending.Set(BRect(0, 0, sub->width - 1, sub->height - 1));
	_PublishLocked(sub);

	interface->UnlockExclusiveAccess();

	BMessage reply(message->what);
	reply.AddInt32("status", B_OK);
	reply.AddInt32("area", area);
	reply.AddInt32("width", width);
	reply.AddInt32("height", height);
	reply.AddInt32("bytes_per_row", bytesPerRow);
	reply.AddInt32("buffer_count", 2);

	int32 replyPort = message->GetInt32("reply port", -1);
	if (replyPort >= 0)
		_FlattenToPort(replyPort, reply);
}


void
OutputAddonManager::_DetachLocked(subscription* sub)
{
	// The list owns its items; take it out without deleting. The
	// interface keeps its listener entry: removing it needs the interface
	// lock, and FrameCommitted() ignores outputs without subscriptions.
	fSubscriptions.RemoveItem(sub, false);

	atomic_add(&fCaptureSubscribers, -1);
	if (sub->eventPort >= B_OK)
		atomic_add(&fInputSubscribers, -1);

	if (sub->area >= B_OK)
		delete_area(sub->area);

	delete sub;
}


void
OutputAddonManager::_HandleDetach(BMessage* message)
{
	if (!_SenderAllowed(message)) {
		_SendReply(message, B_NOT_ALLOWED);
		return;
	}

	int32 team = message->GetInt32("team", -1);
	int32 screenID = message->GetInt32("screen", -1);

	BAutolock locker(fLock);
	subscription* sub = _FindSubscription(team, screenID);
	if (sub == NULL) {
		_SendReply(message, B_BAD_VALUE);
		return;
	}

	_DetachLocked(sub);
	_SendReply(message, B_OK);
}


void
OutputAddonManager::_RemoveSubscriptions(int32 team)
{
	for (int32 i = fSubscriptions.CountItems(); i-- > 0;) {
		subscription* sub = fSubscriptions.ItemAt(i);
		if (sub->team == team)
			_DetachLocked(sub);
	}
}


void
OutputAddonManager::_HandleTeamDeleted(team_id team)
{
	BAutolock locker(fLock);

	addon_team* entry = NULL;
	for (int32 i = 0; i < fTeams.CountItems(); i++) {
		if (fTeams.ItemAt(i)->team == team) {
			entry = fTeams.ItemAt(i);
			break;
		}
	}

	_RemoveSubscriptions(team);

	if (entry == NULL)
		return;

	bool respawn = entry->respawn && entry->spawned;
	BString path = entry->path;
	fTeams.RemoveItem(entry);

	if (!respawn || path.Length() == 0)
		return;

	const char* args[2] = { path.String(), NULL };
	thread_id thread = load_image(1, args, environ);
	if (thread >= B_OK) {
		resume_thread(thread);
		syslog(LOG_NOTICE, "output add-on %s respawned", path.String());
	} else
		syslog(LOG_ERR, "output add-on %s respawn failed: %s",
			path.String(), strerror(thread));
}


void
OutputAddonManager::_HandleInjectInput(BMessage* message)
{
	if (!_SenderAllowed(message)) {
		_SendReply(message, B_NOT_ALLOWED);
		return;
	}

	BMessage event;
	if (message->FindMessage("event", &event) != B_OK) {
		_SendReply(message, B_BAD_VALUE);
		return;
	}

	Desktop* desktop = fAppServer == NULL ? NULL
		: fAppServer->DesktopForUser(message->SenderUid());
	if (desktop == NULL) {
		_SendReply(message, B_ENTRY_NOT_FOUND);
		return;
	}

	// Ownership moves to the stream queue.
	desktop->EventDispatcher().InjectEvent(&event);
	_SendReply(message, B_OK);
}


void
OutputAddonManager::_HandleLaunch(BMessage* message)
{
	if (!_SenderAllowed(message)) {
		_SendReply(message, B_NOT_ALLOWED);
		return;
	}

	const char* path = message->GetString("path");
	if (path == NULL || path[0] == '\0') {
		_SendReply(message, B_BAD_VALUE);
		return;
	}

	bool respawn = message->GetBool("respawn", false);

	const char* args[3];
	args[0] = path;
	int32 argCount = 1;
	const char* second = message->GetString("arg");
	if (second != NULL)
		args[argCount++] = second;
	args[argCount] = NULL;

	thread_id thread = load_image(argCount, args, environ);
	if (thread < B_OK) {
		_SendReply(message, thread);
		return;
	}

	thread_info threadInfo;
	status_t status = get_thread_info(thread, &threadInfo);
	if (status != B_OK) {
		kill_thread(thread);
		_SendReply(message, status);
		return;
	}

	resume_thread(thread);

	BAutolock locker(fLock);
	addon_team* entry = _TeamEntry(threadInfo.team, true);
	if (entry != NULL) {
		entry->spawned = true;
		entry->respawn = respawn;
		entry->path = path;
	}

	BMessage reply(message->what);
	reply.AddInt32("status", B_OK);
	reply.AddInt32("team", threadInfo.team);

	int32 replyPort = message->GetInt32("reply port", -1);
	if (replyPort >= 0)
		_FlattenToPort(replyPort, reply);
}


void
OutputAddonManager::FrameCommitted(HWInterface* interface, const BRect& frame)
{
	if (atomic_get(&fCaptureSubscribers) <= 0)
		return;

	BAutolock locker(fLock);
	if (fSubscriptions.IsEmpty())
		return;

	BRegion dirty(frame);
	for (int32 i = fSubscriptions.CountItems(); i-- > 0;) {
		subscription* sub = fSubscriptions.ItemAt(i);
		if (sub->interface != interface)
			continue;

		sub->pending.Include(&dirty);
		_PublishLocked(sub);
	}
}


void
OutputAddonManager::_PublishLocked(subscription* sub)
{
	if (sub->pending.CountRects() == 0)
		return;

	RenderingBuffer* buffer = sub->interface->DrawingBuffer();
	if (buffer == NULL || buffer->InitCheck() != B_OK)
		return;

	if (buffer->Width() != (uint32)sub->width
		|| buffer->Height() != (uint32)sub->height
		|| buffer->BytesPerRow() != (uint32)sub->bytesPerRow) {
		// Mode changed under us; drop the damage and let the add-on
		// re-attach with the new geometry.
		sub->pending.MakeEmpty();
		write_port(sub->updatePort, kOutputAddonCodeOutputRemoved, NULL, 0);
		return;
	}

	// Never overwrite a slot the client has not acked yet; damage stays
	// pending and coalesces until the client catches up.
	uint32 next = sub->generation + 1;
	uint32 slot = next % 2;
	uint32 oldestInSlot = next >= 2 ? next - 2 : 0;
	if (oldestInSlot > 0 && sub->header->acked < oldestInSlot)
		return;

	int32 rectCount = sub->pending.CountRects();
	if (rectCount > kOutputAddonMaxRects) {
		// Collapse to the bounding box rather than dropping damage.
		sub->pending.Set(sub->pending.Frame());
		rectCount = 1;
	}

	output_addon_rect rects[kOutputAddonMaxRects];
	for (int32 i = 0; i < rectCount; i++) {
		_CopyToWireRect(&rects[i], sub->pending.RectAtInt(i), sub->width,
			sub->height);
		if (output_addon_rect_empty(&rects[i])) {
			rectCount = i;
			break;
		}
	}

	if (rectCount == 0) {
		sub->pending.MakeEmpty();
		return;
	}

	uint8* slotBase = (uint8*)output_addon_fb_buffer(sub->header,
		sub->bytesPerRow, sub->height, (int32)slot);
	output_addon_copy_rect(slotBase, sub->bytesPerRow,
		(const uint8*)buffer->Bits(), buffer->BytesPerRow(), rects, rectCount);

	output_addon_publish(&sub->header->front, next, slot);

	output_addon_frame frame;
	frame.screen = sub->screen;
	frame.generation = next;
	frame.rect_count = rectCount;

	uint8 payload[kMaxFramePayload];
	int32 size = output_addon_frame_encode(&frame, rects, payload,
		sizeof(payload));
	if (size > 0) {
		// Timed write: a full client port drops this frame instead of
		// stalling the compositor; the damage stays pending and merges
		// with the next one.
		status_t status = write_port_etc(sub->updatePort,
			kOutputAddonCodeFrame, payload, size, B_RELATIVE_TIMEOUT, 5000);
		if (status == B_BAD_PORT_ID) {
			// The add-on is gone without a team notice yet; fLock is
			// already held here, so detach inline.
			_DetachLocked(sub);
			return;
		}
	}

	sub->generation = next;
	sub->pending.MakeEmpty();
}


void
OutputAddonManager::PublishInput(const BMessage& event)
{
	if (atomic_get(&fInputSubscribers) <= 0)
		return;

	ssize_t size = event.FlattenedSize();
	if (size <= 0)
		return;

	char* flat = new(std::nothrow) char[size];
	if (flat == NULL)
		return;

	if (event.Flatten(flat, size) != B_OK) {
		delete[] flat;
		return;
	}

	BAutolock locker(fLock);
	for (int32 i = 0; i < fSubscriptions.CountItems(); i++) {
		subscription* sub = fSubscriptions.ItemAt(i);
		if (sub->eventPort < B_OK)
			continue;

		// Non-blocking: dropped input is preferable to a stalled
		// dispatcher thread.
		write_port_etc(sub->eventPort, kOutputAddonCodeInput, flat, size,
			B_RELATIVE_TIMEOUT, 0);
	}

	delete[] flat;
}
