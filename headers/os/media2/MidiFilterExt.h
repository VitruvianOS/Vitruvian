/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MIDI_FILTER_EXT_H
#define _MEDIA2_MIDI_FILTER_EXT_H


#include <atomic>

#include <DataIO.h>
#include <Locker.h>
#include <SupportDefs.h>

#include <media2/MediaConnection.h>
#include <media2/MidiUnit.h>


class BMidiChannelFilter;


// Drops or rewrites channel-numbered MIDI events. Built as a BMidiUnit with
// one BMidiInput and one BMidiOutput so it is a real graph node: per-channel
// routing is several filters with different masks, each linked to its sink.
//
// Channel filter masks and remaps are app-thread state. The filter input's
// event virtuals run on the PipeWire data thread and only read the mask as an
// atomic; they never lock or allocate (handoff 12.L L7).
class BMidiChannelFilter : public BMidiUnit {
public:
								BMidiChannelFilter(const char* name,
									uint32 channelMask);
	virtual						~BMidiChannelFilter();

			void				SetChannelMask(uint32 mask);
			uint32				ChannelMask() const;

			// from/to are 0..15; either -1 disables the rewrite.
			void				SetChannelRemap(int32 from, int32 to);
			void				ChannelRemap(int32* outFrom,
									int32* outTo) const;

private:
	class FilterInput;

			bool				_Accept(uchar channel) const;
			uchar				_RemapChannel(uchar channel) const;
			void				_NoteOff(uchar channel, uchar note,
									uchar velocity, bigtime_t time);
			void				_NoteOn(uchar channel, uchar note,
									uchar velocity, bigtime_t time);
			void				_KeyPressure(uchar channel, uchar note,
									uchar pressure, bigtime_t time);
			void				_ControlChange(uchar channel,
									uchar controlNumber, uchar controlValue,
									bigtime_t time);
			void				_ProgramChange(uchar channel,
									uchar programNumber, bigtime_t time);
			void				_ChannelPressure(uchar channel,
									uchar pressure, bigtime_t time);
			void				_PitchBend(uchar channel, uchar lsb,
									uchar msb, bigtime_t time);
			void				_SystemExclusive(void* data, size_t length,
									bigtime_t time);

			FilterInput*		fInput;
			BMidiOutput*		fOutput;

			std::atomic<uint32>	fChannelMask;
			std::atomic<int32>	fRemapFrom;
			std::atomic<int32>	fRemapTo;

			friend class FilterInput;
};


// Bounded SysEx dump collector. Subclasses BMidiInput; SystemExclusive is
// filled on the PipeWire data thread into a preallocated buffer. Dump() is
// app-thread only and returns what has been collected since the last Reset().
class BMidiSysExCollector : public BMidiInput {
public:
								BMidiSysExCollector(const char* name = NULL);
	virtual						~BMidiSysExCollector();

			status_t			Dump(BMemoryIO* out) const;
			void				Reset();
			uint32				DroppedBytes() const;

protected:
	virtual	void				SystemExclusive(void* data, size_t length,
										bigtime_t time);

private:
	static const size_t			kCapacity = 65536;

			uint8				fBuffer[kCapacity];
			std::atomic<size_t>	fUsed;
			std::atomic<uint64>	fDropped;
};


#endif
