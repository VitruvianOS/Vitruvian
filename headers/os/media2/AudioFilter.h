/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_AUDIO_FILTER_H
#define _MEDIA2_AUDIO_FILTER_H


#include <media2/MediaClient.h>


class BAudioFilterUnit : public BMediaClient {
public:
								BAudioFilterUnit(const char* name,
									const char* gstDescription);
	virtual						~BAudioFilterUnit();

			status_t			InitCheck() const;

			BMediaInput*		Input() const;
			BMediaOutput*		Output() const;

	virtual	bool				IsInProcessRouted() const;

			int32				CountParameters() const;
			status_t			GetParameterInfo(int32 index,
									BString* outName, float* outMin,
									float* outMax, float* outValue);
			status_t			SetParameterValue(int32 index, float value);
			float				ParameterValue(int32 index) const;

private:
	class _Input;
	class _Output;

			void				_HandleBuffer(void* buffer, size_t size,
									const BMediaFormat& format);
	static	void				_ForwardThunk(void* cookie, void* buffer,
									size_t size, const BMediaFormat& format);

			void*				fCore;			// FilterUnitCore*
			_Input*				fInput;
			_Output*			fOutput;
};


class BEqualizerFilter : public BAudioFilterUnit {
public:
								BEqualizerFilter();
};


class BGainFilter : public BAudioFilterUnit {
public:
								BGainFilter();

			void				SetGain(float linear);
};


class BCompressorFilter : public BAudioFilterUnit {
public:
								BCompressorFilter();
};


class BReverbFilter : public BAudioFilterUnit {
public:
								BReverbFilter();
};


#endif // _MEDIA2_AUDIO_FILTER_H
