/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "AlsaControls.h"

#include <stdio.h>
#include <stdlib.h>

#include <alsa/asoundlib.h>


static bool
_selem_usable(snd_mixer_selem_id_t* id, snd_mixer_elem_t* elem)
{
	if (snd_mixer_selem_has_playback_volume(elem)
			|| snd_mixer_selem_has_capture_volume(elem)
			|| snd_mixer_selem_is_enumerated(elem)
			|| snd_mixer_selem_has_playback_switch(elem)
			|| snd_mixer_selem_has_capture_switch(elem))
		return true;
	return false;
}


status_t
AlsaControls::Fetch(std::vector<Control>& outControls)
{
	outControls.clear();

	int card = -1;
	while (snd_card_next(&card) == 0 && card >= 0) {
		char hw[32];
		snprintf(hw, sizeof(hw), "hw:%d", card);

		snd_ctl_t* ctl = NULL;
		if (snd_ctl_open(&ctl, hw, 0) < 0)
			continue;

		snd_mixer_t* mixer = NULL;
		if (snd_mixer_open(&mixer, 0) < 0) {
			snd_ctl_close(ctl);
			continue;
		}
		if (snd_mixer_attach(mixer, hw) < 0
				|| snd_mixer_selem_register(mixer, NULL, NULL) < 0
				|| snd_mixer_load(mixer) < 0) {
			snd_mixer_close(mixer);
			snd_ctl_close(ctl);
			continue;
		}

		char cardName[128] = { 0 };
		snd_ctl_card_info_t* cardInfo = NULL;
		if (snd_ctl_card_info_malloc(&cardInfo) == 0
				&& snd_ctl_card_info(ctl, cardInfo) == 0) {
			snprintf(cardName, sizeof(cardName), "%s",
				snd_ctl_card_info_get_name(cardInfo));
		}
		if (cardInfo != NULL)
			snd_ctl_card_info_free(cardInfo);

		snd_mixer_selem_id_t* sid = NULL;
		snd_mixer_selem_id_alloca(&sid);
		for (snd_mixer_elem_t* elem = snd_mixer_first_elem(mixer);
				elem != NULL; elem = snd_mixer_elem_next(elem)) {
			if (!snd_mixer_selem_is_active(elem))
				continue;
			snd_mixer_selem_get_id(elem, sid);
			if (!_selem_usable(sid, elem))
				continue;

			Control control;
			control.cardIndex = card;
			control.cardName = cardName;
			control.name = snd_mixer_selem_get_name(elem);
			control.isEnum = snd_mixer_selem_is_enumerated(elem) != 0;
			control.isBoolean = false;
			control.enumIndex = -1;
			control.value = 0.0f;
			control.muted = false;
			control.isPlayback = snd_mixer_selem_has_playback_volume(elem)
				|| snd_mixer_selem_has_playback_switch(elem);

			if (control.isEnum) {
				int items = snd_mixer_selem_get_enum_items(elem);
				unsigned int current = 0;
				snd_mixer_selem_get_enum_item(elem,
					(snd_mixer_selem_channel_id_t)0, &current);
				for (int i = 0; i < items; i++) {
					char item[128] = { 0 };
					if (snd_mixer_selem_get_enum_item_name(elem,
							(unsigned int)i, sizeof(item), item) >= 0)
						control.enumNames.push_back(item);
				}
				control.enumIndex = (int32)current;
			} else {
				long min = 0;
				long max = 0;
				long value = 0;
				int (*getRange)(snd_mixer_elem_t*, long*, long*)
					= control.isPlayback
					? snd_mixer_selem_get_playback_volume_range
					: snd_mixer_selem_get_capture_volume_range;
				int (*getValue)(snd_mixer_elem_t*, snd_mixer_selem_channel_id_t,
					long*) = control.isPlayback
					? snd_mixer_selem_get_playback_volume
					: snd_mixer_selem_get_capture_volume;
				if (getRange(elem, &min, &max) == 0
						&& getValue(elem, (snd_mixer_selem_channel_id_t)0, &value) == 0
						&& max > min) {
					control.value = (float)(value - min) / (float)(max - min);
				}

				int (*getSwitch)(snd_mixer_elem_t*,
					snd_mixer_selem_channel_id_t, int*) = control.isPlayback
					? snd_mixer_selem_get_playback_switch
					: snd_mixer_selem_get_capture_switch;
				int on = 1;
				if (getSwitch != NULL
						&& getSwitch(elem, (snd_mixer_selem_channel_id_t)0, &on) == 0) {
					control.isBoolean = true;
					control.muted = on == 0;
				}
			}

			outControls.push_back(control);
		}

		snd_mixer_close(mixer);
		snd_ctl_close(ctl);
	}

	return B_OK;
}


status_t
AlsaControls::Apply(const Control& control, float value, bool mute)
{
	char hw[32];
	snprintf(hw, sizeof(hw), "hw:%d", control.cardIndex);

	snd_mixer_t* mixer = NULL;
	if (snd_mixer_open(&mixer, 0) < 0)
		return B_ERROR;
	if (snd_mixer_attach(mixer, hw) < 0
			|| snd_mixer_selem_register(mixer, NULL, NULL) < 0
			|| snd_mixer_load(mixer) < 0) {
		snd_mixer_close(mixer);
		return B_ERROR;
	}

	status_t result = B_ENTRY_NOT_FOUND;
	for (snd_mixer_elem_t* elem = snd_mixer_first_elem(mixer);
			elem != NULL; elem = snd_mixer_elem_next(elem)) {
		if (!snd_mixer_selem_is_active(elem))
			continue;
		if (control.name != snd_mixer_selem_get_name(elem))
			continue;

		if (control.isEnum) {
			if (control.enumIndex >= 0
					&& snd_mixer_selem_set_enum_item(elem,
						(snd_mixer_selem_channel_id_t)0,
						(unsigned int)control.enumIndex) == 0)
				result = B_OK;
			break;
		}

		long min = 0;
		long max = 0;
		int (*getRange)(snd_mixer_elem_t*, long*, long*) = control.isPlayback
			? snd_mixer_selem_get_playback_volume_range
			: snd_mixer_selem_get_capture_volume_range;
		if (getRange(elem, &min, &max) == 0) {
			float clamped = value;
			if (clamped < 0.0f) clamped = 0.0f;
			if (clamped > 1.0f) clamped = 1.0f;
			long raw = min + (long)(clamped * (float)(max - min));
			int setRes = control.isPlayback
				? snd_mixer_selem_set_playback_volume_all(elem, raw)
				: snd_mixer_selem_set_capture_volume_all(elem, raw);
			if (setRes == 0)
				result = B_OK;
		}

		int (*setSwitch)(snd_mixer_elem_t*, snd_mixer_selem_channel_id_t,
			int) = control.isPlayback
			? snd_mixer_selem_set_playback_switch
			: snd_mixer_selem_set_capture_switch;
		if (setSwitch != NULL) {
			if (setSwitch(elem, (snd_mixer_selem_channel_id_t)0, mute ? 0 : 1) == 0)
				result = B_OK;
		}
		break;
	}

	snd_mixer_close(mixer);
	return result;
}
