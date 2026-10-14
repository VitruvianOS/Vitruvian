/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _GRANT_STORE_H
#define _GRANT_STORE_H

#include <stddef.h>
#include <stdint.h>

// Pure grant/rule logic shared by vos-polkit-grant-helper, the polkit
// agent and the host test. No Be API and no libsystemd here, so it also
// compiles and runs on a plain Linux host.

static const int kGrantMaxRecords = 16;
static const int kGrantMaxAlwaysAsk = 32;

// Per-session remembered-admin grant, mirrored in the state file and in
// the generated polkit rule.
struct GrantRecord {
	char		user[64];
	char		session[128];
	int		minutes;
	int64_t		expiresUnix;	// seconds since the epoch
};

// Escapes text for a JavaScript string literal. Returns the number of
// bytes written excluding the terminator, or -1 on bad input or when
// outSize is too small.
int grant_escape_js(const char* text, char* out, size_t outSize);

// Renders one grant as the state file body. Returns bytes written or -1.
int grant_format_state(const GrantRecord& record, char* out, size_t outSize);

// Parses a state file body. Returns false when malformed or incomplete.
bool grant_parse_state(const char* text, GrantRecord& record);

// Renders the polkit rules file covering the given grants. Grants whose
// deadline already passed at nowUnix are omitted. alwaysAsk holds action
// ids the operator marked with the org.vos.polkit.always-ask annotation.
// Returns bytes written or -1.
int grant_render_rules(const GrantRecord* records, int recordCount,
	const char* const* alwaysAsk, int alwaysAskCount, char* out,
	size_t outSize, int64_t nowUnix);

// Scans one .policy file for action ids carrying
// <annotate key="org.vos.polkit.always-ask">true</annotate>. Appends up
// to maxIds freshly malloc()ed id strings (caller frees) and returns how
// many were appended.
int grant_scan_always_ask(const char* policyText, const char** ids,
	int maxIds);

// True when the session id is safe to embed in a file name.
bool grant_session_is_sane(const char* session);

#endif // _GRANT_STORE_H
