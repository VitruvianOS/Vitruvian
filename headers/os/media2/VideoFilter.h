/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_VIDEO_FILTER_H
#define _MEDIA2_VIDEO_FILTER_H


#include <media2/MediaClient.h>


class BVideoFilterUnit : public BMediaClient {
public:
								// NULL description = CPU-only unit
								BVideoFilterUnit(const char* name,
									const char* description);
	virtual						~BVideoFilterUnit();

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

protected:
	// CPU units override this; data is a private writable copy.
	virtual	void				ProcessFrame(void* buffer, size_t size,
									const BMediaFormat& format);

private:
	class _Input;
	class _Output;

			void				_HandleBuffer(void* buffer, size_t size,
									const BMediaFormat& format);
	static	void				_ForwardThunk(void* cookie, void* buffer,
									size_t size, const BMediaFormat& format);
	static	void				_CpuThunk(void* buffer, size_t size,
									void* cookie);

			void*				fCore;			// FilterUnitCore*
			_Input*				fInput;
			_Output*			fOutput;
			BMediaFormat		fLastFormat;
};


class BVideoBalanceFilter : public BVideoFilterUnit {
public:
								BVideoBalanceFilter();

			void				SetBrightness(float value);
			void				SetContrast(float value);
			void				SetSaturation(float value);
};


class BGrayscaleFilter : public BVideoFilterUnit {
public:
								BGrayscaleFilter();
};


class BSepiaFilter : public BVideoFilterUnit {
public:
								BSepiaFilter();
};


class BInvertFilter : public BVideoFilterUnit {
public:
								BInvertFilter();

protected:
	virtual	void				ProcessFrame(void* buffer, size_t size,
									const BMediaFormat& format);
};


class BWarmFilter : public BVideoFilterUnit {
public:
								BWarmFilter();
};


class BCoolFilter : public BVideoFilterUnit {
public:
								BCoolFilter();
};


class BVintageFilter : public BVideoFilterUnit {
public:
								BVintageFilter();
};


class BVignetteFilter : public BVideoFilterUnit {
public:
								BVignetteFilter();

protected:
	virtual	void				ProcessFrame(void* buffer, size_t size,
									const BMediaFormat& format);
};


class BBlurFilter : public BVideoFilterUnit {
public:
								BBlurFilter();

			void				SetSigma(float sigma);
};


class BSharpenFilter : public BVideoFilterUnit {
public:
								BSharpenFilter();
};


class BVideoFlipFilter : public BVideoFilterUnit {
public:
								// horizontal mirrors; used for front cameras
								BVideoFlipFilter(bool horizontal = true);
};


#endif // _MEDIA2_VIDEO_FILTER_H
