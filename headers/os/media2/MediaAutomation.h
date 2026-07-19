/*
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_AUTOMATION_H
#define _MEDIA2_MEDIA_AUTOMATION_H


#include <Flattenable.h>
#include <OS.h>
#include <SupportDefs.h>


class BControllable;


class BMediaAutomation : public BFlattenable {
public:
	enum interpolation_mode {
		B_AUTOMATION_STEP,
		B_AUTOMATION_LINEAR,
		B_AUTOMATION_SMOOTH
	};

								BMediaAutomation(BControllable* target);
	virtual						~BMediaAutomation();

	// Recording
			status_t			StartRecording();
			status_t			StopRecording();
			bool				IsRecording() const;

	// Playback
			status_t			StartPlayback(bigtime_t startTime = 0);
			status_t			StopPlayback();
			bool				IsPlaying() const;

	// Edit
			status_t			AddPoint(int32 paramId, bigtime_t time,
									float value);
			status_t			AddPoint(int32 paramId, bigtime_t time,
									int32 value);	// discrete
			status_t			RemovePoint(int32 paramId, bigtime_t time);
			status_t			ClearParameter(int32 paramId);
			status_t			ClearAll();

	// Query
			int32				CountParameters() const;
			int32				ParameterIdAt(int32 index) const;
			int32				CountPoints(int32 paramId) const;
			status_t			GetPointAt(int32 paramId, int32 index,
									bigtime_t* time, float* value) const;
			float				ValueAt(int32 paramId, bigtime_t time) const;

			void				SetInterpolation(int32 paramId,
									interpolation_mode mode);
			interpolation_mode	Interpolation(int32 paramId) const;

	// BFlattenable
	virtual	bool				IsFixedSize() const override;
	virtual	type_code			TypeCode() const override;
	virtual	ssize_t				FlattenedSize() const override;
	virtual	status_t			Flatten(void* buffer, ssize_t size) const override;
	virtual	bool				AllowsTypeCode(type_code code) const override;
	virtual	status_t			Unflatten(type_code code, const void* buffer,
									ssize_t size) override;

private:
			class Impl;
			Impl*				fImpl;
};


#endif // _MEDIA2_MEDIA_AUTOMATION_H
