/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _MEDIA2_MIXER_PARAMS_H
#define _MEDIA2_MIXER_PARAMS_H


#include <stddef.h>
#include <stdint.h>

#include <spa/param/props.h>
#include <spa/pod/pod.h>


namespace BPrivate { namespace media {


// No libbe on purpose, so the host tests can compile this file.

// SPA_PARAM_Props pod with channelVolumes and optionally mute.
const struct spa_pod*
MixerBuildVolumeProps(void* buffer, size_t bufferSize,
	const float* volumes, uint32_t count, bool haveMute, bool mute);


} }


#endif
