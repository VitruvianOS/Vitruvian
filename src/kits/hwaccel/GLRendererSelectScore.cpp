/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT license.
 */

#include "GLRendererSelectScore.h"

#include <stdio.h>
#include <string.h>


namespace GLRendererSelectScore {

static const unsigned int kZinkMinDeviceId = 0x1e00;	// TU102 floor
static const unsigned int kNvidiaVendorId = 0x10de;


bool
ZinkWorthIt(bool archAmd64, unsigned int vendorId, unsigned int deviceId)
{
	if (!archAmd64)
		return false;
	if (vendorId != kNvidiaVendorId)
		return false;
	return deviceId >= kZinkMinDeviceId;
}


// Shipped default precedence, used as the first tie-break: mesa_egl always
// beats mesa_surfaceless when scores are equal, independent of readdir
// order. Anything else sorts after the base pair, by registration order.
static int
_PrecedenceIndex(const char* name)
{
	if (strcmp(name, "mesa_egl") == 0)
		return 0;
	if (strcmp(name, "mesa_surfaceless") == 0)
		return 1;
	return 1000;
}


// zink activation is opt-in by design: the auto-selection may
// recommend it, but only an explicit preflet pin (or the operator's
// MESA_LOADER_DRIVER_OVERRIDE) may make it the winner.
static bool
_IsOptIn(const Candidate* candidate)
{
	return candidate->zink;
}


static int
_CandidateScore(const Candidate* candidate)
{
	if (!candidate->installed)
		return kScoreAbsent;

	if (!candidate->devicePresent || !candidate->driverLoads
		|| !candidate->contextCreates) {
		// Disqualified, but rank partial progress deterministically so
		// the logged table is stable across boots.
		int score = kScoreFailBase;
		if (candidate->devicePresent)
			score += 100;
		if (candidate->driverLoads)
			score += 10;
		if (candidate->contextCreates)
			score += 1;
		return score;
	}

	int score = kScorePassBase;
	if (candidate->hardware)
		score += kScoreHardware;

	if (candidate->zink) {
		if (ZinkWorthIt(candidate->archAmd64, candidate->vendorId,
				candidate->deviceId)) {
			score += kScoreZinkWorth;
		} else {
			score += kScoreZinkNo;
		}
	}

	return score;
}


static bool
_PassesHardProbes(const Candidate* candidate)
{
	return candidate->installed && candidate->devicePresent
		&& candidate->driverLoads && candidate->contextCreates;
}


// Sort key: higher score first; then opt-in last (auto backends first);
// then the default precedence; then registration (enumeration) order.
// Returns true when 'a' sorts before 'b'.
static bool
_Better(const Candidate* candidates, int scores[], int a, int b)
{
	if (scores[a] != scores[b])
		return scores[a] > scores[b];

	bool optInA = _IsOptIn(&candidates[a]);
	bool optInB = _IsOptIn(&candidates[b]);
	if (optInA != optInB)
		return !optInA;

	int precA = _PrecedenceIndex(candidates[a].name);
	int precB = _PrecedenceIndex(candidates[b].name);
	if (precA != precB)
		return precA < precB;

	return candidates[a].registrationIndex
		< candidates[b].registrationIndex;
}


static void
_AppendFlag(char* buffer, size_t size, const char* flag)
{
	if (size == 0)
		return;

	size_t used = strlen(buffer);
	size_t flagLength = strlen(flag);
	size_t need = flagLength + 1;
	bool separator = used > 0 && buffer[used - 1] != '/'
		&& flag[0] != '/';
	if (separator)
		need += 1;
	if (used + need > size)
		return;

	if (separator)
		buffer[used++] = '/';
	memcpy(buffer + used, flag, flagLength + 1);
}


static void
_FormatFlags(const Candidate* candidate, char* buffer, size_t size)
{
	buffer[0] = '\0';

	if (!candidate->installed) {
		_AppendFlag(buffer, size, "absent");
		return;
	}

	if (!candidate->devicePresent || !candidate->driverLoads
		|| !candidate->contextCreates) {
		char fail[48];
		fail[0] = '\0';
		if (!candidate->devicePresent)
			strncat(fail, "dev,", sizeof(fail) - strlen(fail) - 1);
		if (!candidate->driverLoads)
			strncat(fail, "load,", sizeof(fail) - strlen(fail) - 1);
		if (!candidate->contextCreates)
			strncat(fail, "ctx,", sizeof(fail) - strlen(fail) - 1);
		size_t len = strlen(fail);
		if (len > 0 && fail[len - 1] == ',')
			fail[len - 1] = '\0';
		char flags[64];
		snprintf(flags, sizeof(flags), "fail(%s)", fail);
		_AppendFlag(buffer, size, flags);
		return;
	}

	_AppendFlag(buffer, size, "ok");
	_AppendFlag(buffer, size, candidate->hardware ? "hw" : "sw");
	if (candidate->zink)
		_AppendFlag(buffer, size, "zink");
	if (_IsOptIn(candidate))
		_AppendFlag(buffer, size, "optin");
}


static void
_FormatTable(const Candidate* candidates, const int scores[], int count,
	char* buffer, size_t size)
{
	buffer[0] = '\0';
	for (int i = 0; i < count; i++) {
		char flags[64];
		_FormatFlags(&candidates[i], flags, sizeof(flags));

		char entry[kMaxName + 96];
		snprintf(entry, sizeof(entry), "%s:%d:%s", candidates[i].name,
			scores[i], flags);

		size_t used = strlen(buffer);
		size_t need = strlen(entry) + (used > 0 ? 2 : 0);
		if (used + need + 1 >= size)
			break;
		if (used > 0)
			snprintf(buffer + used, size - used, "; %s", entry);
		else
			snprintf(buffer + used, size - used, "%s", entry);
	}
}


static void
_CopyBounded(char* dest, size_t size, const char* source)
{
	if (size == 0)
		return;
	snprintf(dest, size, "%s", source != NULL ? source : "");
}


void
Score(const Candidate* candidates, int count,
	const char* const* overrideList, int overrideCount, Result* result)
{
	if (result == NULL)
		return;

	memset(result, 0, sizeof(*result));
	result->winner = -1;
	result->recommended = -1;
	result->overrideActive = false;
	result->orderCount = 0;
	result->winnerReason[0] = '\0';
	result->table[0] = '\0';

	if (candidates == NULL || count <= 0 || count > kMaxCandidates) {
		_CopyBounded(result->winnerReason, sizeof(result->winnerReason),
			"no renderer candidates found");
		return;
	}

	int scores[kMaxCandidates];
	for (int i = 0; i < count; i++)
		scores[i] = _CandidateScore(&candidates[i]);

	_FormatTable(candidates, scores, count, result->table,
		sizeof(result->table));

	// Scored ranking: every installed candidate, best first.
	int ranked[kMaxCandidates];
	int rankedCount = 0;
	for (int i = 0; i < count; i++) {
		if (scores[i] == kScoreAbsent)
			continue;
		ranked[rankedCount++] = i;
	}
	for (int a = 1; a < rankedCount; a++) {
		int key = ranked[a];
		int b = a - 1;
		while (b >= 0 && _Better(candidates, scores, ranked[b], key) == false) {
			ranked[b + 1] = ranked[b];
			b--;
		}
		ranked[b + 1] = key;
	}

	// The recommendation: the best passing opt-in backend, when the zink
	// rule says its Vulkan driver is genuinely faster. zink is
	// never auto-activated; the operator activates it via the override.
	for (int r = 0; r < rankedCount; r++) {
		const Candidate* candidate = &candidates[ranked[r]];
		if (!_IsOptIn(candidate) || !_PassesHardProbes(candidate))
			continue;
		if (ZinkWorthIt(candidate->archAmd64, candidate->vendorId,
				candidate->deviceId)) {
			result->recommended = ranked[r];
			break;
		}
	}

	// The winner.
	if (overrideList != NULL && overrideCount > 0) {
		// Preflet contract: a non-empty pin list wins outright. First
		// pin that is installed AND passes every hard probe activates;
		// including zink, because pinning it IS the zink opt-in.
		bool sawInstalled = false;
		int fallback = -1;
		for (int p = 0; p < overrideCount; p++) {
			for (int i = 0; i < count; i++) {
				if (strcmp(candidates[i].name, overrideList[p]) != 0)
					continue;
				if (!candidates[i].installed)
					break;
				sawInstalled = true;
				if (_PassesHardProbes(&candidates[i])) {
					result->winner = i;
					result->overrideActive = true;
					char reason[kMaxReason];
					snprintf(reason, sizeof(reason),
						"override pin '%s' (render_priority)",
						candidates[i].name);
					_CopyBounded(result->winnerReason,
						sizeof(result->winnerReason), reason);
					break;
				}
				break;
			}
			if (result->winner >= 0)
				break;
		}

		if (result->winner < 0 && sawInstalled) {
			// Every installed pin failed its probes: fall back to the
			// scored winner below, but say so.
			for (int r = 0; r < rankedCount; r++) {
				if (!_IsOptIn(&candidates[ranked[r]])
					&& _PassesHardProbes(&candidates[ranked[r]])) {
					fallback = ranked[r];
					break;
				}
			}
			result->winner = fallback;
			char reason[kMaxReason];
			if (fallback >= 0) {
				snprintf(reason, sizeof(reason),
					"override exhausted (pins failed probe); scored "
					"fallback '%s'", candidates[fallback].name);
			} else {
				snprintf(reason, sizeof(reason),
					"override exhausted (pins failed probe); no scored "
					"fallback");
			}
			_CopyBounded(result->winnerReason,
				sizeof(result->winnerReason), reason);
		} else if (result->winner < 0) {
			// Pin names not installed at all: same fallback.
			for (int r = 0; r < rankedCount; r++) {
				if (!_IsOptIn(&candidates[ranked[r]])
					&& _PassesHardProbes(&candidates[ranked[r]])) {
					fallback = ranked[r];
					break;
				}
			}
			result->winner = fallback;
			char reason[kMaxReason];
			if (fallback >= 0) {
				snprintf(reason, sizeof(reason),
					"override miss (pins not installed); scored fallback "
					"'%s'", candidates[fallback].name);
			} else {
				snprintf(reason, sizeof(reason),
					"override miss (pins not installed); no scored "
					"fallback");
			}
			_CopyBounded(result->winnerReason,
				sizeof(result->winnerReason), reason);
		}
	} else {
		// Auto mode: best passing non-opt-in candidate (default precedence
		// breaks score ties; zink can only ever be the recommendation).
		for (int r = 0; r < rankedCount; r++) {
			if (_IsOptIn(&candidates[ranked[r]]))
				continue;
			if (!_PassesHardProbes(&candidates[ranked[r]]))
				continue;
			result->winner = ranked[r];
			break;
		}

		if (result->winner >= 0) {
			const Candidate* winner = &candidates[result->winner];
			char reason[kMaxReason];
			snprintf(reason, sizeof(reason), "scored %d (%s, %s)",
				scores[result->winner],
				winner->hardware ? "hw-accel" : "software",
				winner->zink ? "zink" : "native");
			_CopyBounded(result->winnerReason,
				sizeof(result->winnerReason), reason);
		} else if (result->recommended >= 0) {
			_CopyBounded(result->winnerReason,
				sizeof(result->winnerReason),
				"only opt-in backend passed probe; override to activate");
		} else {
			_CopyBounded(result->winnerReason, sizeof(result->winnerReason),
				"no candidate passed probe");
		}
	}

	if (result->winner >= 0 && result->recommended >= 0
		&& result->recommended != result->winner) {
		size_t used = strlen(result->winnerReason);
		snprintf(result->winnerReason + used,
			sizeof(result->winnerReason) - used,
			"; %s recommended (opt-in, override to activate)",
			candidates[result->recommended].name);
	}

	// Try order: winner first, then the scored ranking. Failed-but-
	// installed candidates stay reachable at the end (a partial probe
	// never hides an add-on the old directory walk would have tried).
	if (result->winner >= 0)
		result->order[result->orderCount++] = result->winner;
	for (int r = 0; r < rankedCount; r++) {
		if (ranked[r] == result->winner)
			continue;
		result->order[result->orderCount++] = ranked[r];
	}
}


static void
_Flatten(char* buffer)
{
	for (; *buffer != '\0'; buffer++) {
		if (*buffer == '\n' || *buffer == '\r')
			*buffer = ' ';
	}
}


int
FormatSelectionLine(const Result* result, const Candidate* candidates,
	int count, const char* stamp, char* buffer, size_t size)
{
	if (buffer == NULL || size == 0)
		return 0;

	if (result == NULL || candidates == NULL || count <= 0) {
		snprintf(buffer, size, "GLRendererSelect: winner=none "
			"reason=\"no renderer candidates found\" recommended=none "
			"table=[] stamp=%s", stamp != NULL ? stamp : "-");
		_Flatten(buffer);
		return strlen(buffer);
	}

	char winner[kMaxName];
	if (result->winner >= 0 && result->winner < count)
		snprintf(winner, sizeof(winner), "%s",
			candidates[result->winner].name);
	else
		snprintf(winner, sizeof(winner), "none");

	char recommended[kMaxName];
	if (result->recommended >= 0 && result->recommended < count)
		snprintf(recommended, sizeof(recommended), "%s",
			candidates[result->recommended].name);
	else
		snprintf(recommended, sizeof(recommended), "none");

	snprintf(buffer, size, "GLRendererSelect: winner=%s reason=\"%s\" "
		"recommended=%s table=[%s] stamp=%s", winner,
		result->winnerReason, recommended, result->table,
		stamp != NULL ? stamp : "-");

	// syslog convention: one tree line, always.
	_Flatten(buffer);
	return strlen(buffer);
}

} // namespace GLRendererSelectScore
