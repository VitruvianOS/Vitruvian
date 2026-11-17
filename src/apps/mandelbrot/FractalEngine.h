/*
 * Copyright 2016, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Augustin Cavalier <waddlesplash>
 *		kerwizzy
 */
#ifndef FRACTALENGINE_H
#define FRACTALENGINE_H

#include <SupportDefs.h>
#include <Looper.h>
#include <Locker.h>
#include <Messenger.h>

class BBitmap;
class BRegion;


#define MAX_RENDER_THREADS 16
#define RENDER_PASSES 4
#define RENDER_TILE_ROWS 16
#define RENDER_PUBLISH_DELAY 16000


class FractalEngine : public BLooper {
public:
	enum {
		MSG_CHANGE_SET = 'Frct',
		MSG_SET_PALETTE,
		MSG_SET_ITERATIONS,
		MSG_SET_SUBSAMPLING,
		MSG_RESIZE,
		MSG_BUFFER_CREATED,
		MSG_RENDER,
		MSG_RENDER_COMPLETE,
		MSG_THREAD_RENDER_COMPLETE,
	};

	FractalEngine(BHandler* parent, BLooper* looper);
	~FractalEngine();

	virtual void MessageReceived(BMessage* msg);
	void WriteToBitmap(BBitmap*, BRegion* dirty = NULL);
		// imports the last finished pass (all if dirty is NULL)

private:
	uint16 fWidth;
	uint16 fHeight;

	uint8* fRenderBuffer;
	uint32 fRenderBufferLen;

	uint8* fReadyBuffer;
		// last published image, guarded by fReadyLock
	BLocker fReadyLock;
	int32 fReadyPending;
		// 1 when fReadyBuffer has not been imported yet
	bigtime_t fRenderStart;
	bigtime_t fRemapAt;
	bool fHaveImage;

	int32 fTileCount;
	int32 fPassNext[RENDER_PASSES];
	int32 fPassDone[RENDER_PASSES];
	int32 fPublished;

	uint8 fSubsampling;
		// 1 disables subsampling.

	BMessenger fMessenger;

	uint8 fThreadCount;
	thread_id fRenderThreads[MAX_RENDER_THREADS];
	sem_id fRenderSem;
		// released to tell threads to start running
	sem_id fRenderStoppedSem;
		// released by threads when done rendering or otherwise stopped

	bool fRenderStopping;
		// true when the render is trying to be stopped
	bool fRenderStopped;
	bool fResizing;

	double fLocationX, fLocationY;
	double fSize;
		// the width on the complex plane of a single pixel

	uint16 fIterations;

	const uint8* fColorset;

	int32 (FractalEngine::*fDoSet)(double real, double imaginary);

	void Render(double locationX, double locationY, double size);
	void StopRender();
	static status_t RenderThread(void* data);
	void RenderPixel(uint32 x, uint32 y, uint32 step);
	void PublishPass(int32 pass);
	static void Remap(const uint8* src, int32 srcW, int32 srcH,
		double srcX, double srcY, double srcSize, uint8* dst, int32 dstW,
		int32 dstH, double dstX, double dstY, double dstSize);

	int32 DoSet_Mandelbrot(double real, double imaginary);
	int32 DoSet_BurningShip(double real, double imaginary);
	int32 DoSet_Tricorn(double real, double imaginary);
	int32 DoSet_Julia(double real, double imaginary);
	int32 DoSet_OrbitTrap(double real, double imaginary);
	int32 DoSet_Multibrot(double real, double imaginary);
};


#endif	/* FRACTALENGINE_H */
