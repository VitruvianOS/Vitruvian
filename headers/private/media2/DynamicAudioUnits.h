/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _MEDIA2_DYNAMIC_AUDIO_UNITS_H
#define _MEDIA2_DYNAMIC_AUDIO_UNITS_H


// Audio units whose pin count follows the links. The mixer always keeps
// one free input; the splitter grows one output per link. All of them
// are native F32 units on a single pw_filter: conversion and resampling
// happen on the links, and the realtime path only multiplies and adds.

#include <atomic>

#include <Locker.h>

#include <media2/MediaUnit.h>
#include <media2/ParameterWeb.h>


class BControllable;


class BMixerUnit : public BMediaUnit {
public:
								BMixerUnit();
	virtual						~BMixerUnit();

			status_t			InitCheck() const;

protected:
	virtual	void				ProcessCycle(uint32 frameCount) override;
	virtual	void				HandleLink(BMediaConnection* local,
									BMediaConnection* peer) override;
	virtual	void				HandleUnlink(BMediaConnection* local,
									BMediaConnection* peer) override;

private:
	class _Input;

	struct Channel {
		std::atomic<bool>		used;
		_Input*					connection;
		std::atomic<float>		gain;
		std::atomic<float>		pan;
		std::atomic<float>		level;
		std::atomic<bool>		mute;
		std::atomic<bool>		solo;
	};

	static	const int32			kMaxChannels = 32;
	enum {
		kMasterGain		= B_MEDIA_PROP_CUSTOM_BASE,
		kMasterMute,
		kChannelBase,
		kChannelStride	= 5
	};

	static	void				_ChangeHook(int32 id, bigtime_t when,
									const void* value, size_t size,
									void* cookie);
			void				_BuildWeb();
			void				_AddChannelInput();
			void				_RemoveChannelInput(_Input* input);
			int32				_IndexOf(BMediaInput* input) const;
			BParameterGroup*	_ChannelGroup(int32 slot);

			BMediaOutput*		fOutputLeft;
			BMediaOutput*		fOutputRight;
			Channel				fChannels[kMaxChannels];
			_Input*				fFreeInput;
			BControllable*		fControllable;
			std::atomic<float>	fMasterGain;
			std::atomic<bool>	fMasterMute;
			mutable BLocker		fLock;
			status_t			fInitStatus;
};


class BSplitterUnit : public BMediaUnit {
public:
								BSplitterUnit();
	virtual						~BSplitterUnit();

			status_t			InitCheck() const;

protected:
	virtual	void				ProcessCycle(uint32 frameCount) override;
	virtual	void				HandleLink(BMediaConnection* local,
									BMediaConnection* peer) override;
	virtual	void				HandleUnlink(BMediaConnection* local,
									BMediaConnection* peer) override;

private:
	class _Output;

	struct Channel {
		std::atomic<bool>		used;
		_Output*				connection;
		std::atomic<float>		gain;
		std::atomic<bool>		mute;
	};

	static	const int32			kMaxOutputs = 16;
	enum {
		kChannelBase	= B_MEDIA_PROP_CUSTOM_BASE,
		kChannelStride	= 2
	};

	static	void				_ChangeHook(int32 id, bigtime_t when,
									const void* value, size_t size,
									void* cookie);
			void				_BuildWeb();
			void				_AddChannelOutput();
			void				_RemoveChannelOutput(_Output* output);
			int32				_IndexOf(BMediaOutput* output) const;
			BParameterGroup*	_ChannelGroup(int32 slot);

			BMediaInput*		fInput;
			Channel				fChannels[kMaxOutputs];
			_Output*			fFreeOutput;
			BControllable*		fControllable;
			mutable BLocker		fLock;
			status_t			fInitStatus;
};


class BToneUnit : public BMediaUnit {
public:
								BToneUnit(const char* name = "Tone");
	virtual						~BToneUnit();

			status_t			InitCheck() const;

protected:
	virtual	void				ProcessCycle(uint32 frameCount) override;

private:
	enum {
		kFrequency		= B_MEDIA_PROP_CUSTOM_BASE,
		kWaveform,
		kGain
	};

	enum waveform {
		kSine = 0,
		kSquare,
		kSaw,
		kNoise
	};

	static	void				_ChangeHook(int32 id, bigtime_t when,
									const void* value, size_t size,
									void* cookie);
			void				_BuildWeb();

			BMediaOutput*		fOutput;
			BControllable*		fControllable;
			std::atomic<float>	fFrequency;
			std::atomic<float>	fGain;
			std::atomic<int32>	fWaveform;
			float				fPhase;
			uint32				fNoiseState;
			status_t			fInitStatus;
};


#endif // _MEDIA2_DYNAMIC_AUDIO_UNITS_H
