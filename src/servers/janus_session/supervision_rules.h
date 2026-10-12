/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// Pure predicates of the fan-out restart policy; kept out of
// janus_session.cpp so the host test can exercise them.

#ifndef _JANUS_SESSION_SUPERVISION_RULES_H
#define _JANUS_SESSION_SUPERVISION_RULES_H

#include <string.h>
#include <sys/wait.h>


// comm is capped at 15 chars; a longer fan-out name still matches its
// truncated form.
static bool
comm_matches(const char* comm, const char* name)
{
	size_t len = strlen(name);
	if (len > 15)
		len = 15;
	if (strncmp(comm, name, len) != 0)
		return false;
	return comm[len] == '\0' || comm[len] == '\n';
}


// A cgroup is in the session when it is the session's own or nested
// below it; a bare uid cannot tell two logins of one user apart.
static bool
cgroup_in_session(const char* cgroup, const char* session)
{
	if (cgroup == NULL || session == NULL || session[0] == '\0')
		return false;

	size_t len = strlen(session);
	if (strncmp(cgroup, session, len) != 0)
		return false;
	if (cgroup[len] == '\0')
		return true;
	// The separator is either already in the session path, as in "/", or
	// the next char of the cgroup path.
	return session[len - 1] == '/' || cgroup[len] == '/';
}


#endif // _JANUS_SESSION_SUPERVISION_RULES_H
