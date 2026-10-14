/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _VITRUVIAN_MEDIA2_FILTER_UNIT_CORE_H
#define _VITRUVIAN_MEDIA2_FILTER_UNIT_CORE_H


#include <String.h>

#include <media2/MediaClientDefs.h>
#include <media2/MediaFormat.h>


namespace BPrivate { namespace media {


// Shared push/pull engine for in-process GStreamer filter units.
class FilterUnitCore {
public:
	typedef void (*ForwardFunc)(void* cookie, void* buffer, size_t size,
		const BMediaFormat& format);
	typedef void (*CpuFilterFunc)(void* buffer, size_t size, void* cookie);

								FilterUnitCore(const char* name,
									const char* description,
									media_type_mask acceptedTypes,
									ForwardFunc forward, void* forwardCookie);
								~FilterUnitCore();

			status_t			InitCheck() const;

			// cpuFilter runs on a private copy when set and no GStreamer
			// chain was built (or the chain produced no sample in time).
			status_t			ProcessBuffer(void* buffer, size_t size,
									const BMediaFormat& format,
									CpuFilterFunc cpuFilter,
									void* cpuCookie);

			int32				CountParameters() const;
			status_t			GetParameterInfo(int32 index,
									BString* outName, float* outMin,
									float* outMax, float* outValue);
			status_t			SetParameterValue(int32 index, float value);
			float				ParameterValue(int32 index) const;

private:
			struct Parameter;
			struct Impl;

			Impl*				fImpl;
};


} }


#endif // _VITRUVIAN_MEDIA2_FILTER_UNIT_CORE_H
