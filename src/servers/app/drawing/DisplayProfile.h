/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef DISPLAY_PROFILE_H
#define DISPLAY_PROFILE_H


#include <String.h>
#include <SupportDefs.h>

#include <vector>

class HWInterface;


// What the display stack can do and what it chose, per output. Filled by
// HWInterface::GetDisplayProfile(); no backend types leak through it.

struct DisplayPlaneProfile {
	uint32		id;
	// "primary", "overlay" or "cursor".
	const char*	type;
	uint32		formatCount;
	uint32		modifierCount;
	bool		argb8888;
	bool		xrgb8888;
	bool		linear;
	uint32		minSize;
	uint32		maxSize;
	// Held by a swap chain or video overlay.
	bool		claimed;
};

struct DisplayModeProfile {
	uint32		width;
	uint32		height;
	float		refresh;
	bool		preferred;
	bool		current;
};

struct DisplayGpuProfile {
	int32		index;
	BString		driver;
	BString		version;
	BString		pciId;
	BString		node;
	// -1 when not known.
	int32		atomic;
	int32		asyncFlip;
	int32		modifiers;
	uint32		cursorWidth;
	uint32		cursorHeight;
	bool		gbm;
	BString		gbmBackend;
	// This device renders the desktop; the others scan out a copy.
	bool		renders;
	// A GPU copy of swap chain buffers can run on it.
	bool		gpuCopy;
};

struct DisplayOutputProfile {
	BString		name;
	uint32		connector;
	uint32		crtc;
	// The GpuProfile index scanning this output out.
	int32		gpu;
	bool		connected;
	bool		disabled;
	bool		first;
	bool		internal;
	int32		x;
	int32		y;
	uint32		width;
	uint32		height;
	float		refresh;
	// Frame period the output is paced by, microseconds.
	bigtime_t	period;
	bool		vrrCapable;
	bool		vrrEnabled;
	// Panel's vertical range in Hz, 0 when it does not say.
	uint32		vrrMin;
	uint32		vrrMax;
	bool		cursorPlane;
	std::vector<DisplayModeProfile>	modes;
	std::vector<DisplayPlaneProfile>	planes;
};

// Runtime state of one swap chain.
struct DisplayChainProfile {
	BString		view;
	// "plane", "scanout", "gpu" or "cpu".
	const char*	path;
	bool		trial;
	int32		left;
	int32		top;
	int32		right;
	int32		bottom;
	// Output period, lead before a flip and last flip latency, microseconds.
	bigtime_t	period;
	bigtime_t	lead;
	bigtime_t	latency;
	// Per frame cost of each read while measuring, 0 when not measured.
	bigtime_t	gpuCost;
	bigtime_t	cpuCost;
	bool		statsOn;
	uint32		shown;
	uint32		presented;
	uint32		replaced;
	bigtime_t	worstGap;
};

struct DisplayPublishProfile {
	bool		deferred;
	bool		batchedCommit;
	bool		damageFlush;
	// Since the last publish report.
	uint32		rects;
	uint32		batches;
	uint32		timeouts;
	uint32		pending;
};

struct DisplayProfile {
								DisplayProfile();

	BString						backend;
	std::vector<DisplayGpuProfile>		gpus;
	std::vector<DisplayOutputProfile>	outputs;
	std::vector<DisplayChainProfile>	chains;
	DisplayPublishProfile		publish;

	// Human readable block; verbose adds the mode lists.
			void				Format(BString& text, const char* reason,
									bool verbose) const;
};

// Collects the profile of the interface and the live swap chains, and
// prints it to stderr. Safe to call from any thread.
void	DumpDisplayProfile(HWInterface* interface, const char* reason);


#endif	// DISPLAY_PROFILE_H
