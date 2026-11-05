/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "TeamRef.h"

#include <new>

#include <Message.h>
#include <MessengerPrivate.h>
#include <Roster.h>
#include <TokenSpace.h>

#include <system_info.h>


BTeamRef::BTeamRef(team_id team, status_t* result)
	:
	fTeam(B_BAD_TEAM_ID),
	fRefCount(0)
{
	app_info info;
	if (team < 0 || be_roster == NULL
			|| be_roster->GetRunningAppInfo(team, &info) != B_OK) {
		if (result != NULL)
			*result = B_BAD_VALUE;
		return;
	}

	// Only registered apps expose their looper port today; other teams
	// need the kernel port lookup that lands with the nexus slots.
	fTeam = team;
	BMessenger::Private messengerPrivate(fMessenger);
	messengerPrivate.SetTo(team, info.port, B_NULL_TOKEN);
	if (result != NULL)
		*result = fMessenger.IsValid() ? B_OK : B_BAD_VALUE;
}


BTeamRef::BTeamRef(const BMessenger& messenger, status_t* result)
	:
	fTeam(messenger.Team()),
	fMessenger(messenger),
	fRefCount(0)
{
	if (result != NULL)
		*result = fTeam >= 0 && fMessenger.IsValid() ? B_OK : B_BAD_VALUE;
}


BTeamRef::BTeamRef(const BTeamRef& other)
	:
	fTeam(other.fTeam),
	fMessenger(other.fMessenger),
	fRefCount(0)
{
	Acquire();
}


BTeamRef::~BTeamRef()
{
	Release();
}


team_id
BTeamRef::Team() const
{
	return fTeam;
}


const BMessenger&
BTeamRef::Messenger() const
{
	return fMessenger;
}


bool
BTeamRef::IsValid() const
{
	team_info info;
	return fTeam >= 0 && fMessenger.IsValid()
		&& get_team_info(fTeam, &info) == B_OK;
}


status_t
BTeamRef::Acquire()
{
	if (fTeam < 0)
		return B_BAD_VALUE;

	fRefCount.fetch_add(1, std::memory_order_relaxed);
	return B_OK;
}


void
BTeamRef::Release()
{
	int32 previous = fRefCount.fetch_sub(1, std::memory_order_acq_rel);
	if (previous <= 0) {
		// Underflow guard: nothing kernel-side to drop yet.
		fRefCount.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	// The last release will ask the lifetime policy owner to drop the
	// kernel slot once the nexus team refs exist.
}


status_t
BTeamRef::Watch(BMessenger target)
{
	if (fTeam < 0 || !target.IsValid())
		return B_BAD_VALUE;

	BMessenger::Private private_(target);
	return __start_watching_system(-1, B_WATCH_SYSTEM_TEAM_DELETION,
		private_.Port(), private_.Token());
}


status_t
BTeamRef::Unwatch(BMessenger target)
{
	if (fTeam < 0 || !target.IsValid())
		return B_BAD_VALUE;

	BMessenger::Private private_(target);
	return __stop_watching_system(-1, B_WATCH_SYSTEM_TEAM_DELETION,
		private_.Port(), private_.Token());
}


status_t
BTeamRef::SendMessage(BMessage* message, BMessage* reply,
	bigtime_t deliveryTimeout, bigtime_t replyTimeout) const
{
	if (fTeam < 0)
		return B_BAD_VALUE;

	return fMessenger.SendMessage(message, reply, deliveryTimeout,
		replyTimeout);
}


bool
BTeamRef::HandlesDeath(const BMessage& message, team_id team)
{
	if (team < 0 || message.what != B_SYSTEM_OBJECT_UPDATE)
		return false;

	return message.GetInt32("opcode", -1) == B_TEAM_DELETED
		&& message.GetInt32("team", -1) == team;
}
