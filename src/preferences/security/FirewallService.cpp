/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "FirewallService.h"

#include "FirewallBackend.h"
#include "FirewallRunner.h"

#include <Autolock.h>
#include <File.h>
#include <FindDirectory.h>
#include <Locker.h>
#include <Node.h>
#include <NodeMonitor.h>
#include <OS.h>
#include <Path.h>
#include <String.h>

#include <stdio.h>
#include <string.h>


// Never destroyed: a worker may still hold it while the app exits.
static BLocker* sFirewallLock = new BLocker("firewall");


struct FirewallJob {
	int32		op;
	BMessage	args;
	BMessenger	replyTo;
};


void
FirewallPutRule(BMessage& message, const FirewallRule& rule)
{
	BMessage sub;
	sub.AddBool("allow", rule.allow);
	sub.AddString("protocol", rule.protocol.String());
	sub.AddInt32("port", rule.port);
	sub.AddString("label", rule.label.String());
	message.AddMessage("rule", &sub);
}


void
FirewallPutState(BMessage& message, const FirewallState& state)
{
	message.AddBool("enabled", state.enabled);
	message.AddInt32("inPolicy", state.inPolicy);
	message.AddInt32("outPolicy", state.outPolicy);
	message.AddBool("logging", state.logging);
	message.AddInt32("profile", state.profile);
	for (size_t i = 0; i < state.rules.size(); i++)
		FirewallPutRule(message, state.rules[i]);
}


bool
FirewallGetRule(const BMessage& message, int32 index, FirewallRule& rule)
{
	BMessage sub;
	if (message.FindMessage("rule", index, &sub) != B_OK)
		return false;

	bool allow = false;
	const char* protocol = NULL;
	int32 port = 0;
	const char* label = NULL;
	sub.FindBool("allow", &allow);
	sub.FindString("protocol", &protocol);
	sub.FindInt32("port", &port);
	sub.FindString("label", &label);

	rule.allow = allow;
	rule.protocol = protocol != NULL ? protocol : "";
	rule.port = (uint16)port;
	rule.label = label != NULL ? label : "";
	rule.handle = "";
	return true;
}


int32
FirewallCountRules(const BMessage& message)
{
	int32 count = 0;
	BMessage sub;
	while (message.FindMessage("rule", count, &sub) == B_OK)
		count++;
	return count;
}


void
FirewallGetState(const BMessage& message, FirewallState& state)
{
	state.enabled = false;
	state.inPolicy = FIREWALL_POLICY_ACCEPT;
	state.outPolicy = FIREWALL_POLICY_ACCEPT;
	state.logging = false;
	state.profile = FIREWALL_PROFILE_HOME;
	state.rules.clear();

	message.FindBool("enabled", &state.enabled);
	message.FindInt32("inPolicy", &state.inPolicy);
	message.FindInt32("outPolicy", &state.outPolicy);
	message.FindBool("logging", &state.logging);
	message.FindInt32("profile", &state.profile);

	int32 count = FirewallCountRules(message);
	for (int32 i = 0; i < count; i++) {
		FirewallRule rule;
		if (FirewallGetRule(message, i, rule))
			state.rules.push_back(rule);
	}
}


static const uint32 kSecuritySettingsWhat = 'SECS';


void
FirewallLoadSettings(FirewallState& state, bool& seeded)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;
	path.Append("Security");

	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BMessage settings;
	if (settings.Unflatten(&file) != B_OK)
		return;
	if (settings.what != kSecuritySettingsWhat)
		return;

	FirewallGetState(settings, state);
	settings.FindBool("seeded", &seeded);
}


void
FirewallSaveSettings(const FirewallState& state, bool seeded)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;
	path.Append("Security");

	BMessage settings(kSecuritySettingsWhat);
	FirewallPutState(settings, state);
	settings.AddBool("seeded", seeded);

	BFile file(path.Path(), B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK)
		settings.Flatten(&file);
}


// Watch the stamp file when it exists, else its directory, else /run, so
// a recreated file is caught. Any change re-arms the watch.
void
FirewallWatchStamp(const BMessenger& target)
{
	FirewallStopWatchingStamp(target);

	const char* paths[] = { kFirewallStampPath, kFirewallStampDirectory,
		"/run" };
	for (size_t i = 0; i < 3; i++) {
		BNode node(paths[i]);
		node_ref ref;
		if (node.InitCheck() != B_OK || node.GetNodeRef(&ref) != B_OK)
			continue;
		uint32 flags = i == 0 ? B_WATCH_STAT : B_WATCH_DIRECTORY;
		watch_node(&ref, flags, target);
		// A replaced file shows up as a directory entry change.
		if (i == 0) {
			BNode directory(kFirewallStampDirectory);
			if (directory.GetNodeRef(&ref) == B_OK)
				watch_node(&ref, B_WATCH_DIRECTORY, target);
		}
		return;
	}
}


void
FirewallStopWatchingStamp(const BMessenger& target)
{
	watch_node(NULL, B_STOP_WATCHING, target);
}


static int32
_FirewallThread(void* data)
{
	FirewallJob* job = static_cast<FirewallJob*>(data);

	// Serialize: two interleaved nft transactions would race on the
	// vitruvian table.
	BAutolock lock(sFirewallLock);

	BMessage result(kFirewallResultWhat);
	result.AddInt32("op", job->op);

	PrivilegedFirewallRunner runner;
	const char* prefer = NULL;
	job->args.FindString("prefer", &prefer);
	FirewallBackend* backend = create_firewall_backend(&runner, prefer);

	if (backend == NULL) {
		result.AddInt32("status", B_UNSUPPORTED);
		result.AddString("error",
			"No firewall tool is installed on this system.");
		result.AddBool("noBackend", true);
	} else {
		result.AddString("backend", backend->Name());

		status_t status = B_BAD_VALUE;
		if (job->op == kFirewallOpFetch) {
			FirewallState state;
			status = backend->FetchState(state);
			if (status == B_OK) {
				job->args.FindInt32("profile", &state.profile);
				FirewallPutState(result, state);
			}
		} else if (job->op == kFirewallOpApply) {
			FirewallState state;
			FirewallGetState(job->args, state);
			FirewallState applied = state;
			FirewallEffectiveState(applied);
			BString error;
			status = backend->ApplyState(applied, error);
			if (status == B_OK)
				FirewallPublishState(&runner, state);
			if (status != B_OK && error.Length() > 0)
				result.AddString("error", error.String());
			// Read back so the UI reports the realized state, never
			// the requested one.
			if (status == B_OK) {
				FirewallState readBack;
				if (backend->FetchState(readBack) == B_OK) {
					readBack.profile = state.profile;
					FirewallPutState(result, readBack);
				}
			}
		}
		result.AddInt32("status", status);
		if (status != B_OK && runner.Canceled())
			result.AddBool("canceled", true);
	}

	delete backend;

	job->replyTo.SendMessage(&result);
	delete job;
	return 0;
}


status_t
FirewallAsyncRun(int32 op, const BMessage& args, const BMessenger& replyTo)
{
	FirewallJob* job = new FirewallJob;
	job->op = op;
	job->args = args;
	job->replyTo = replyTo;

	thread_id thread = spawn_thread(_FirewallThread, "firewall worker",
		B_NORMAL_PRIORITY, job);
	if (thread < 0) {
		delete job;
		return B_ERROR;
	}
	resume_thread(thread);
	return B_OK;
}
