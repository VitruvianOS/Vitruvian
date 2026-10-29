/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT license.
 */
#ifndef HWACCEL_GL_RENDERER_SELECT_SCORE_H
#define HWACCEL_GL_RENDERER_SELECT_SCORE_H


#include <stddef.h>


// Pure scoring and tie-break model behind GLRendererSelect, host-testable.
// Zink outranks native only on amd64 NVIDIA Turing or newer and is never auto-activated.
namespace GLRendererSelectScore {

enum {
	kMaxCandidates	= 16,
	kMaxName		= 64,
	kMaxLine		= 512,
	kMaxTable		= 320,
	kMaxReason		= 192,

	// Score bands (asserted by the model test):
	//   absent:      -2000
	//   failed:      -1000 + 100*device + 10*load + 1*context
	//   passing:     1000 + 100*hardware [+110 zink worth it | -50 not]
	kScoreAbsent	= -2000,
	kScoreFailBase	= -1000,
	kScorePassBase	= 1000,
	kScoreHardware	= 100,
	kScoreZinkWorth	= 110,
	kScoreZinkNo	= -50,
};


struct Candidate {
	char	name[kMaxName];
	int		registrationIndex;	// enumeration order; stable last resort
	bool	installed;
	bool	devicePresent;		// drm node / veno / Vulkan ICD, by kind
	bool	driverLoads;		// add-on loads, factory symbol resolves
	bool	contextCreates;		// smoke: 1-context mini-create ok
	bool	hardware;			// context structurally on a hw device
	bool	zink;				// zink (Gallium-on-Vulkan) backend
	bool	archAmd64;
	unsigned int	vendorId;	// PCI vendor of the primary GPU, 0 = unknown
	unsigned int	deviceId;	// PCI device id, 0 = unknown
};


struct Result {
	int		winner;				// index into candidates, -1 = none
	int		recommended;		// opt-in backend worth activating, or -1
	bool	overrideActive;	// a render_priority pin won
	int		order[kMaxCandidates];	// try order, winner first; -1 ends
	int		orderCount;
	char	winnerReason[kMaxReason];
	char	table[kMaxTable];
};


// The zink decision rule: the zink path is ONLY worth it on
// amd64 + NVIDIA whose PCI device id is Turing (0x1e00) or newer; the
// zink-on-Vulkan configuration Mesa upstream tests most. Mirrors
// build/scripts/lib/zink-decide.sh (same floor, same vendor gate).
bool ZinkWorthIt(bool archAmd64, unsigned int vendorId,
	unsigned int deviceId);

// Deterministic scoring + ordering. 'overrideList' is the preflet's pin
// list (render_priority order, may be NULL/empty for auto mode).
// Pure function of its inputs; no globals, no I/O.
void Score(const Candidate* candidates, int count,
	const char* const* overrideList, int overrideCount, Result* result);

// Formats the single-line selection log record: one physical line
// (any embedded newline in a component is flattened to a space), always
// NUL-terminated, at most size-1 bytes. Returns strlen(buffer).
int FormatSelectionLine(const Result* result, const Candidate* candidates,
	int count, const char* stamp, char* buffer, size_t size);

} // namespace GLRendererSelectScore


#endif // HWACCEL_GL_RENDERER_SELECT_SCORE_H
