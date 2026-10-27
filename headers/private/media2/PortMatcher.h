/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _MEDIA2_PORT_MATCHER_H
#define _MEDIA2_PORT_MATCHER_H


#include <string.h>

#include <vector>

#include <ObjectList.h>
#include <media2/MediaGraph.h>
#include <media2/SoundFontSynth.h>


// Output N of a unit is the N-th speaker (out0 FL, out1 FR). Ports carry
// their channel as audio.channel or a name suffix ("playback_FL").
namespace BPrivate { namespace media {

static const char* const kMediaChannelTags[]
	= { "FL", "FR", "FC", "LFE", "RL", "RR", "SL", "SR" };

const int kMediaChannelCount
	= sizeof(kMediaChannelTags) / sizeof(kMediaChannelTags[0]);

const int kMediaChannelUnknown = -1;


static inline int
MediaChannelFromTag(const char* tag)
{
	if (tag == NULL)
		return kMediaChannelUnknown;
	for (int i = 0; i < kMediaChannelCount; i++) {
		if (strcmp(tag, kMediaChannelTags[i]) == 0)
			return i;
	}
	return kMediaChannelUnknown;
}


// "FL" or "playback_FL".
static inline int
MediaChannelFromPortName(const char* name)
{
	if (name == NULL)
		return kMediaChannelUnknown;
	size_t length = strlen(name);
	for (int i = 0; i < kMediaChannelCount; i++) {
		const char* tag = kMediaChannelTags[i];
		size_t tagLength = strlen(tag);
		if (length == tagLength && strncmp(name, tag, tagLength) == 0)
			return i;
		if (length > tagLength && name[length - tagLength - 1] == '_'
			&& strncmp(name + length - tagLength, tag, tagLength) == 0)
			return i;
	}
	return kMediaChannelUnknown;
}


static inline int
MediaChannelForOutputIndex(int index)
{
	if (index < 0 || index >= kMediaChannelCount)
		return kMediaChannelUnknown;
	return index;
}


// outMatch[i] is the port for output i, or -1. Outputs without a channel
// match take the free ports in order. Returns the matched count.
static inline int
MatchInputPorts(const int* portChannels, int portCount, int outputCount,
	int* outMatch)
{
	if (portChannels == NULL || outMatch == NULL || portCount < 0
		|| outputCount < 0)
		return 0;

	for (int i = 0; i < outputCount; i++)
		outMatch[i] = -1;

	int matched = 0;
	for (int i = 0; i < outputCount; i++) {
		int channel = MediaChannelForOutputIndex(i);
		int pick = kMediaChannelUnknown;
		if (channel != kMediaChannelUnknown) {
			for (int p = 0; p < portCount; p++) {
				if (portChannels[p] != channel)
					continue;
				int j = 0;
				while (j < i && outMatch[j] != p)
					j++;
				if (j == i) {
					pick = p;
					break;
				}
			}
		}
		if (pick == kMediaChannelUnknown) {
			for (int p = 0; p < portCount; p++) {
				int j = 0;
				while (j < i && outMatch[j] != p)
					j++;
				if (j == i) {
					pick = p;
					break;
				}
			}
		}
		outMatch[i] = pick;
		if (pick != kMediaChannelUnknown)
			matched++;
	}
	return matched;
}


// Links the synth's outputs to the device's input ports by channel; false
// when nothing linked, so the caller can retry.
static inline bool
LinkSynthOutputs(BSoundFontSynth* synth,
	const BObjectList<BMediaGraph::ForeignPort, true>& ports)
{
	if (synth == NULL)
		return false;

	int32 count = ports.CountItems();
	std::vector<int> inPorts;
	for (int32 p = 0; p < count; p++) {
		const BMediaGraph::ForeignPort* port = ports.ItemAt(p);
		if (port != NULL && port->direction == "in")
			inPorts.push_back(p);
	}

	int32 candidates = (int32)inPorts.size();
	std::vector<int> channels(candidates, kMediaChannelUnknown);
	for (int32 j = 0; j < candidates; j++) {
		const BMediaGraph::ForeignPort* port = ports.ItemAt(inPorts[j]);
		channels[j] = MediaChannelFromTag(port->channel.String());
		if (channels[j] == kMediaChannelUnknown)
			channels[j] = MediaChannelFromPortName(port->name.String());
	}

	int32 outputs = synth->CountOutputs();
	std::vector<int> match(outputs, kMediaChannelUnknown);
	if (candidates > 0 && outputs > 0)
		MatchInputPorts(&channels[0], candidates, outputs, &match[0]);

	bool linked = false;
	for (int32 i = 0; i < outputs; i++) {
		BMediaOutput* output = synth->OutputAt(i);
		if (output == NULL)
			continue;
		if (match[i] < 0 || match[i] >= candidates)
			continue;
		const BMediaGraph::ForeignPort* port = ports.ItemAt(inPorts[match[i]]);
		if (port == NULL)
			continue;
		if (MediaGraph()->Connect(output, port) == B_OK)
			linked = true;
	}
	return linked;
}

} } // namespace BPrivate::media


#endif // _MEDIA2_PORT_MATCHER_H
