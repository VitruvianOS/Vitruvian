/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _MEDIA2_MEDIA_FILTER_H
#define _MEDIA2_MEDIA_FILTER_H


// Filters are BMediaClients because they run as an in-process GStreamer
// chain, routed buffer-to-buffer by BMediaGraph::_ConnectInProcess: not a
// pw_filter (BMediaUnit) nor a per-connection pw_stream (BMediaNode).

#include <Locker.h>
#include <ObjectList.h>

#include <media2/MediaClient.h>


class BMediaFilter : public BMediaClient {
public:
								// type: B_MEDIA_RAW_AUDIO or
								// B_MEDIA_RAW_VIDEO; NULL description =
								// CPU-only filter
								BMediaFilter(const char* name,
									const char* description,
									media_type type);
	virtual						~BMediaFilter();

			status_t			InitCheck() const;
			media_type			Type() const;

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
	// CPU filters override this; data is a private writable copy.
	virtual	void				ProcessBuffer(void* buffer, size_t size,
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

			media_type			fType;
			void*				fCore;			// FilterUnitCore*
			_Input*				fInput;
			_Output*			fOutput;
			BMediaFormat		fLastFormat;
};


class BEqualizerFilter : public BMediaFilter {
public:
								BEqualizerFilter();
};


class BGainFilter : public BMediaFilter {
public:
								BGainFilter();

			void				SetGain(float linear);
};


class BCompressorFilter : public BMediaFilter {
public:
								BCompressorFilter();
};


class BReverbFilter : public BMediaFilter {
public:
								BReverbFilter();
};


class BVideoBalanceFilter : public BMediaFilter {
public:
								BVideoBalanceFilter();

			void				SetBrightness(float value);
			void				SetContrast(float value);
			void				SetSaturation(float value);
};


class BGrayscaleFilter : public BMediaFilter {
public:
								BGrayscaleFilter();
};


class BSepiaFilter : public BMediaFilter {
public:
								BSepiaFilter();
};


class BInvertFilter : public BMediaFilter {
public:
								BInvertFilter();

protected:
	virtual	void				ProcessBuffer(void* buffer, size_t size,
									const BMediaFormat& format);
};


class BWarmFilter : public BMediaFilter {
public:
								BWarmFilter();
};


class BCoolFilter : public BMediaFilter {
public:
								BCoolFilter();
};


class BVintageFilter : public BMediaFilter {
public:
								BVintageFilter();
};


class BVignetteFilter : public BMediaFilter {
public:
								BVignetteFilter();

protected:
	virtual	void				ProcessBuffer(void* buffer, size_t size,
									const BMediaFormat& format);
};


class BBlurFilter : public BMediaFilter {
public:
								BBlurFilter();

			void				SetSigma(float sigma);
};


class BSharpenFilter : public BMediaFilter {
public:
								BSharpenFilter();
};


class BVideoFlipFilter : public BMediaFilter {
public:
								// horizontal mirrors; used for front cameras
								BVideoFlipFilter(bool horizontal = true);
};


// Pass-through with one input and N outputs; every frame goes to all of
// them (preview + recorder sharing one camera feed).
class BVideoSplitter : public BMediaClient {
public:
								BVideoSplitter();
	virtual						~BVideoSplitter();

	virtual	bool				IsInProcessRouted() const;

			BMediaInput*		Input() const;
			BMediaOutput*		AddOutput();
			int32				CountOutputs() const;
			BMediaOutput*		OutputAt(int32 index) const;
			status_t			RemoveOutput(BMediaOutput* output);

private:
	class _Input;
	class _Output;

			_Input*				fInput;
			BObjectList<BMediaOutput, false>* fOutputs;
			mutable BLocker			fLock;
};


#endif // _MEDIA2_MEDIA_FILTER_H
