/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MixerParams.h"

#include <spa/pod/builder.h>


namespace BPrivate { namespace media {


const struct spa_pod*
MixerBuildVolumeProps(void* buffer, size_t bufferSize,
	const float* volumes, uint32_t count, bool haveMute, bool mute)
{
	if (buffer == NULL || volumes == NULL || count == 0)
		return NULL;

	spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, (uint32_t)bufferSize);
	spa_pod_frame frame;
	spa_pod_builder_push_object(&b, &frame,
		SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
	spa_pod_builder_add(&b,
		SPA_PROP_channelVolumes, SPA_POD_Array(sizeof(float),
			SPA_TYPE_Float, count, volumes),
		0);
	if (haveMute)
		spa_pod_builder_add(&b, SPA_PROP_mute, SPA_POD_Bool(mute), 0);
	return (const struct spa_pod*)spa_pod_builder_pop(&b, &frame);
}


} }
