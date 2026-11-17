/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "DisplayProfile.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Autolock.h>
#include <Locker.h>

#include "HWInterface.h"
#include "ViewSwapChain.h"


static void
append(BString& text, const char* format, ...)
{
	char buffer[512];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	text << buffer;
}


static const char*
yesno(int32 value)
{
	return value < 0 ? "?" : value != 0 ? "yes" : "no";
}


DisplayProfile::DisplayProfile()
{
	memset(&publish, 0, sizeof(publish));
}


void
DisplayProfile::Format(BString& text, const char* reason, bool verbose) const
{
	append(text, "display profile (%s): backend %s, %u gpu, %u output\n",
		reason != NULL ? reason : "", backend.String(), (unsigned)gpus.size(),
		(unsigned)outputs.size());

	for (size_t i = 0; i < gpus.size(); i++) {
		const DisplayGpuProfile& gpu = gpus[i];
		append(text, "  gpu %d: %s %s pci %s %s%s\n", (int)gpu.index,
			gpu.driver.String(), gpu.version.String(),
			gpu.pciId.Length() > 0 ? gpu.pciId.String() : "-",
			gpu.node.String(), gpu.renders ? " [renders]" : "");
		append(text, "    atomic %s, async flip %s, modifiers %s, cursor "
			"%ux%u, gbm %s%s%s, gpu copy %s\n", yesno(gpu.atomic),
			yesno(gpu.asyncFlip), yesno(gpu.modifiers), gpu.cursorWidth,
			gpu.cursorHeight, gpu.gbm ? "yes" : "no",
			gpu.gbm ? " " : "", gpu.gbm ? gpu.gbmBackend.String() : "",
			gpu.gpuCopy ? "yes" : "no");
	}

	for (size_t i = 0; i < outputs.size(); i++) {
		const DisplayOutputProfile& output = outputs[i];
		append(text, "  output %s: connector %u crtc %u gpu %d%s%s%s%s\n",
			output.name.String(), (unsigned)output.connector,
			(unsigned)output.crtc, (int)output.gpu,
			output.first ? " first" : "", output.internal ? " internal" : "",
			output.disabled ? " disabled" : "",
			output.connected ? "" : " disconnected");
		if (output.gpu >= 0 && (size_t)output.gpu < gpus.size()
				&& !gpus[output.gpu].renders) {
			append(text, "    scans out on a gpu that does not render: "
				"frames cross devices\n");
		}
		append(text, "    mode %ux%u@%.2f at %d,%d, period %lld us\n",
			output.width, output.height, output.refresh, (int)output.x,
			(int)output.y, (long long)output.period);
		if (output.vrrMax > 0) {
			append(text, "    vrr %s, enabled %s, range %u-%u Hz\n",
				yesno(output.vrrCapable), yesno(output.vrrEnabled),
				output.vrrMin, output.vrrMax);
		} else {
			append(text, "    vrr %s, enabled %s, range unknown\n",
				yesno(output.vrrCapable), yesno(output.vrrEnabled));
		}

		if (output.ddc.Length() > 0)
			append(text, "    ddc/ci %s\n", output.ddc.String());

		float lowest = 0, highest = 0;
		for (size_t m = 0; m < output.modes.size(); m++) {
			const float hz = output.modes[m].refresh;
			if (lowest == 0 || hz < lowest)
				lowest = hz;
			if (hz > highest)
				highest = hz;
		}
		append(text, "    %u modes, %.2f-%.2f Hz", (unsigned)output.modes.size(),
			lowest, highest);
		if (verbose) {
			for (size_t m = 0; m < output.modes.size(); m++) {
				const DisplayModeProfile& mode = output.modes[m];
				append(text, "%s%ux%u@%.2f%s%s", m % 6 == 0 ? "\n      " : " ",
					mode.width, mode.height, mode.refresh,
					mode.preferred ? "*" : "", mode.current ? "+" : "");
			}
		}
		append(text, "\n");

		uint32 overlays = 0, unused = 0;
		for (size_t p = 0; p < output.planes.size(); p++) {
			if (strcmp(output.planes[p].type, "overlay") == 0) {
				overlays++;
				if (!output.planes[p].claimed)
					unused++;
			}
		}
		append(text, "    planes: %u overlay (%u free), cursor plane %s\n",
			overlays, unused, output.cursorPlane ? "yes" : "no");
		for (size_t p = 0; p < output.planes.size(); p++) {
			const DisplayPlaneProfile& plane = output.planes[p];
			append(text, "      %s %u: %u formats, %u modifiers%s%s%s",
				plane.type, (unsigned)plane.id, plane.formatCount,
				plane.modifierCount, plane.argb8888 ? ", argb" : "",
				plane.xrgb8888 ? ", xrgb" : "", plane.linear ? ", linear" : "");
			if (plane.maxSize != 0) {
				append(text, ", size %u-%u", plane.minSize, plane.maxSize);
			}
			append(text, "%s\n", plane.claimed ? ", claimed" : "");
		}
	}

	append(text, "  publish: %s, %s, %s; %u rects %u batches %u timeouts, "
		"%u pending\n", publish.deferred ? "deferred" : "immediate",
		publish.batchedCommit ? "batched commit" : "legacy flip",
		publish.damageFlush ? "damage flush" : "page flip", publish.rects,
		publish.batches, publish.timeouts, publish.pending);

	append(text, "  swap chains: %u\n", (unsigned)chains.size());
	for (size_t i = 0; i < chains.size(); i++) {
		const DisplayChainProfile& chain = chains[i];
		append(text, "    %s: path %s%s, (%d,%d,%d,%d), period %lld us, "
			"lead %lld us, latency %lld us", chain.view.String(), chain.path,
			chain.trial ? " (measuring)" : "", (int)chain.left, (int)chain.top,
			(int)chain.right, (int)chain.bottom, (long long)chain.period,
			(long long)chain.lead, (long long)chain.latency);
		if (chain.gpuCost != 0 || chain.cpuCost != 0) {
			append(text, ", gpu read %lld us, cpu read %lld us",
				(long long)chain.gpuCost, (long long)chain.cpuCost);
		}
		if (chain.statsOn) {
			append(text, ", shown %u presented %u replaced %u worst gap "
				"%.1f ms", chain.shown, chain.presented, chain.replaced,
				chain.worstGap / 1000.0);
		}
		append(text, "\n");
	}
}


void
DumpDisplayProfile(HWInterface* interface, const char* reason)
{
	if (interface == NULL)
		return;

	static BLocker sLock("display profile");
	BAutolock locker(sLock);

	DisplayProfile profile;
	interface->GetDisplayProfile(profile);
	ViewSwapChain::Snapshot(profile.chains);

	BString text;
	const char* verbose = getenv("VOS_COMPOSE_HOLD_STATS");
	profile.Format(text, reason, verbose != NULL && atoi(verbose) != 0);
	fputs(text.String(), stderr);
}
