/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _MEDIA2_NATIVE_AUDIO_UNITS_H
#define _MEDIA2_NATIVE_AUDIO_UNITS_H


// CPU audio effects as plain BMediaUnits: one mono F32 input and output,
// detached when linked in-process and a pw_filter node in the graph.

#include <atomic>
#include <vector>

#include <String.h>

#include <media2/MediaUnit.h>


class BControllable;


class BNativeAudioUnit : public BMediaUnit {
public:
	virtual						~BNativeAudioUnit();

			status_t			InitCheck() const;

			int32				CountParameters() const;
			status_t			GetParameterInfo(int32 index,
									BString* outName, float* outMin,
									float* outMax, float* outValue) const;
			status_t			SetParameterValue(int32 index, float value);
			float				ParameterValue(int32 index) const;

	virtual	status_t			Start() override;

protected:
								BNativeAudioUnit(const char* name);

			void				AddParameter(const char* name, float minValue,
									float maxValue, float defaultValue);

	// Called per cycle; in and out may alias.
	virtual	void				Process(const float* in, float* out,
									uint32 frameCount, uint32 sampleRate) = 0;

	virtual	void				ProcessCycle(uint32 frameCount) override;

private:
	struct Parameter {
		BString					name;
		float					minValue;
		float					maxValue;
		float					defaultValue;
		std::atomic<float>		value;
	};

	static	void				_ChangeHook(int32 id, bigtime_t when,
									const void* value, size_t size,
									void* cookie);
			void				_PublishAndBind();

			BMediaInput*		fInput;
			BMediaOutput*		fOutput;
			BControllable*		fControllable;
			Parameter*			fParameters[16];
			int32				fParameterCount;
			status_t			fInitStatus;
};


class BGainUnit : public BNativeAudioUnit {
public:
								BGainUnit();

protected:
	virtual	void				Process(const float* in, float* out,
									uint32 frameCount, uint32 sampleRate)
									override;
};


class BCompressorUnit : public BNativeAudioUnit {
public:
								BCompressorUnit();

protected:
	virtual	void				Process(const float* in, float* out,
									uint32 frameCount, uint32 sampleRate)
									override;

private:
			float				fEnvelope;
};


class BReverbUnit : public BNativeAudioUnit {
public:
								BReverbUnit();

protected:
	virtual	void				Process(const float* in, float* out,
									uint32 frameCount, uint32 sampleRate)
									override;

private:
	struct Comb {
		std::vector<float>		buffer;
		uint32					length;
		uint32					position;
		float					filter;
	};

			void				_Configure(uint32 sampleRate);

			Comb				fCombs[4];
			Comb				fAllpass[2];
			uint32				fConfiguredRate;
};


#endif // _MEDIA2_NATIVE_AUDIO_UNITS_H
