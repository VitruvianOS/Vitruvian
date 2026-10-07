/*
 * Copyright 2005, Jérôme Duval. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Inspired by SoundCapture from Be newsletter (Media Kit Basics: Consumers and Producers)
 */
#ifndef VOLUMESLIDER_H
#define VOLUMESLIDER_H

#include <Control.h>
#include <Bitmap.h>
#include <Box.h>
#include <media2/MediaPlayer.h>

class VolumeSlider : public BControl
{
public:
	VolumeSlider(BRect rect, const char* title, uint32 resizeFlags);
	~VolumeSlider();
	virtual void Draw(BRect);
	virtual void MouseMoved(BPoint point, uint32 transit, const BMessage *message);
	virtual void MouseUp(BPoint point);
	virtual void MouseDown(BPoint point);
	void SetMediaPlayer(BMediaPlayer *player);
private:
	void _UpdateVolume(BPoint point);

	BBitmap fLeftBitmap, fRightBitmap, fButtonBitmap;
	float fRight;
	float fVolume;
	BMediaPlayer *fMediaPlayer;
};

class SpeakerView : public BBox
{
public:
	SpeakerView(BRect rect, uint32 resizeFlags);
	~SpeakerView();
	void Draw(BRect updateRect);
		
private:
	BBitmap fSpeakerBitmap;	
};

#endif	
