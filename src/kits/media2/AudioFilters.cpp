/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <media2/MediaFilter.h>

#include <String.h>


BEqualizerFilter::BEqualizerFilter()
	:
	BMediaFilter("Equalizer", "equalizer-10bands", B_MEDIA_RAW_AUDIO)
{
}


BGainFilter::BGainFilter()
	:
	BMediaFilter("Gain", "volume", B_MEDIA_RAW_AUDIO)
{
}


void
BGainFilter::SetGain(float linear)
{
	for (int32 i = 0; i < CountParameters(); i++) {
		BString name;
		if (GetParameterInfo(i, &name, NULL, NULL, NULL) != B_OK)
			continue;
		if (name == "volume") {
			SetParameterValue(i, linear);
			break;
		}
	}
}


BCompressorFilter::BCompressorFilter()
	:
	BMediaFilter("Compressor", "audiodynamic", B_MEDIA_RAW_AUDIO)
{
}


BReverbFilter::BReverbFilter()
	:
	BMediaFilter("Reverb", "freeverb", B_MEDIA_RAW_AUDIO)
{
}
