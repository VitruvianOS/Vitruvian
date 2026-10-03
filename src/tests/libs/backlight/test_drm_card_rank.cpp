/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Host test for drm_card_display_rank: Kasumi binds amdgpu, not simpledrm.
 */

#include <stdio.h>

#include <device/DrmDeviceSelect.h>

static int g_failures = 0;

static void
expect(int condition, const char* what)
{
	if (condition) {
		printf("ok   %s\n", what);
	} else {
		printf("FAIL %s\n", what);
		g_failures++;
	}
}

int
main()
{
	// Kasumi: simpledrm connected (boot fb), amdgpu boot_vga + eDP
	// possibly still disconnected while DC probes.
	int simpledrm = drm_card_display_rank(true, true, true, false);
	int amdgpu = drm_card_display_rank(false, true, true, true);
	expect(amdgpu > simpledrm,
		"boot_vga+eDP amdgpu outranks connected simpledrm");

	// Both connected with eDP: boot_vga amdgpu still wins.
	int amdgpuConnected = drm_card_display_rank(true, true, true, true);
	int simpledrmConnected = drm_card_display_rank(true, true, true, false);
	expect(amdgpuConnected > simpledrmConnected,
		"connected amdgpu outranks connected simpledrm on rank");

	// Optimus: Intel connected eDP vs NVIDIA boot_vga HDMI only.
	int intel = drm_card_display_rank(true, true, true, false);
	int nvidia = drm_card_display_rank(false, true, false, true);
	expect(intel > nvidia,
		"connected internal Intel outranks boot_vga HDMI-only NVIDIA");

	// Only render-capable card with no connectors.
	expect(drm_card_display_rank(false, false, false, false) < 0,
		"no connectors ranks below zero");

	if (g_failures != 0) {
		printf("%d failure(s)\n", g_failures);
		return 1;
	}
	printf("all drm card rank tests passed\n");
	return 0;
}
