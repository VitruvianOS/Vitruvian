/*
 * Copyright 2026, Vitruvian Project.
 * Level meter bar with peak/decay, for Media preferences & mixer views.
 * Distributed under the terms of the MIT License.
 */

#ifndef LEVEL_METER_VIEW_H
#define LEVEL_METER_VIEW_H


#include <View.h>


class LevelMeterView : public BView {
public:
							LevelMeterView(const char* name,
								float minDB = -60.0f,
								float maxDB = 0.0f,
								bool vertical = true);
	virtual					~LevelMeterView();

	virtual	void			Draw(BRect updateRect);
	virtual	void			FrameResized(float width, float height);

			void			SetPeak(float linear);
			float			Peak() const
									{ return fPeak; }

			void			SetDecayRate(float perSecond);
			void			Pulse();

private:
			void			_RecomputeBar();

			float			fMinDB;
			float			fMaxDB;
			bool			fVertical;
			float			fPeak;
			float			fDecayRate;
			BRect			fBar;
};


#endif
