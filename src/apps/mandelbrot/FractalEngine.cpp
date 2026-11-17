/*
 * Copyright 2016, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Augustin Cavalier <waddlesplash>
 *		kerwizzy
 */
#include "FractalEngine.h"

#include <algorithm>
#include <math.h>
#include <string.h>

#include <Bitmap.h>
#include <OS.h>
#include <Region.h>
#include <String.h>

#include "Colorsets.h"


//#define TRACE_MANDELBROT_ENGINE
#ifdef TRACE_MANDELBROT_ENGINE
#	include <stdio.h>
#	define TRACE(x...) printf(x)
#else
#	define TRACE(x...)
#endif


FractalEngine::FractalEngine(BHandler* parent, BLooper* looper)
	:
	BLooper("FractalEngine"),
	fWidth(0), fHeight(0),
	fRenderBuffer(NULL),
	fRenderBufferLen(0),
	fReadyBuffer(NULL),
	fReadyLock("MandelbrotReady"),
	fReadyPending(0),
	fRenderStart(0),
	fRemapAt(0),
	fHaveImage(false),
	fTileCount(0),
	fPublished(0),
	fSubsampling(2),
	fMessenger(parent, looper),
	fRenderStopping(false),
	fRenderStopped(true),
	fResizing(false),
	fLocationX(0), fLocationY(0), fSize(0.005),
	fIterations(1024),
	fColorset(Colorset_Royal)
{
	fDoSet = &FractalEngine::DoSet_Mandelbrot;

	fRenderSem = create_sem(0, "RenderSem");
	fRenderStoppedSem = create_sem(0, "RenderStopped");

	system_info info;
	get_system_info(&info);
	fThreadCount = info.cpu_count;

	if (fThreadCount > MAX_RENDER_THREADS)
		fThreadCount = MAX_RENDER_THREADS;

	for (uint8 i = 0; i < fThreadCount; i++) {
		fRenderThreads[i] = spawn_thread(&FractalEngine::RenderThread,
			BString().SetToFormat("RenderThread%d", i).String(),
			B_NORMAL_PRIORITY, this);
		resume_thread(fRenderThreads[i]);
	}
}


FractalEngine::~FractalEngine()
{
	delete_sem(fRenderSem);
	delete_sem(fRenderStoppedSem);
	delete[] fRenderBuffer;
	delete[] fReadyBuffer;
}


void FractalEngine::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
	case MSG_CHANGE_SET:
		switch (msg->GetUInt8("set", 0)) {
		case 0: fDoSet = &FractalEngine::DoSet_Mandelbrot; break;
		case 1: fDoSet = &FractalEngine::DoSet_BurningShip; break;
		case 2: fDoSet = &FractalEngine::DoSet_Tricorn; break;
		case 3: fDoSet = &FractalEngine::DoSet_Julia; break;
		case 4: fDoSet = &FractalEngine::DoSet_OrbitTrap; break;
		case 5: fDoSet = &FractalEngine::DoSet_Multibrot; break;
		}
		break;
	case MSG_SET_PALETTE:
		switch (msg->GetUInt8("palette", 0)) {
		case 0: fColorset = Colorset_Royal; break;
		case 1: fColorset = Colorset_DeepFrost; break;
		case 2: fColorset = Colorset_Frost; break;
		case 3: fColorset = Colorset_Fire; break;
		case 4: fColorset = Colorset_Midnight; break;
		case 5: fColorset = Colorset_Grassland; break;
		case 6: fColorset = Colorset_Lightning; break;
		case 7: fColorset = Colorset_Spring; break;
		case 8: fColorset = Colorset_HighContrast; break;
		}
		break;
	case MSG_SET_ITERATIONS:
		fIterations = msg->GetUInt16("iterations", 0);
		break;
	case MSG_SET_SUBSAMPLING:
		fSubsampling = msg->GetUInt8("subsampling", 1);
		break;

	case MSG_RESIZE: {
		TRACE("Got MSG_RESIZE\n");
		if (fResizing) {
			// Will be true throughout this whole handler. Set false at the end
			TRACE("Breaking out of MSG_RESIZE handler\n");
			break;
		}
		fResizing = true;
		StopRender();

		uint16 oldWidth = fWidth;
		uint16 oldHeight = fHeight;
		uint8* oldReady = fReadyBuffer;

		fWidth = msg->GetUInt16("width", 320);
		fHeight = msg->GetUInt16("height", 240);

		delete[] fRenderBuffer;
		fRenderBufferLen = fWidth * fHeight * 4;
		fRenderBuffer = new uint8[fRenderBufferLen];
		TRACE("New buffer width %u height %u ptr = %p\n",
			  fWidth, fHeight, fRenderBuffer);

		memset(fRenderBuffer, 0, fRenderBufferLen);

		fReadyLock.Lock();
		fReadyBuffer = new uint8[fRenderBufferLen];
		if (fHaveImage && oldReady != NULL) {
			// keep the old image centered until the new render lands
			Remap(oldReady, oldWidth, oldHeight, fLocationX, fLocationY, fSize,
				fReadyBuffer, fWidth, fHeight, fLocationX, fLocationY, fSize);
		} else
			memset(fReadyBuffer, 0, fRenderBufferLen);
		memcpy(fRenderBuffer, fReadyBuffer, fRenderBufferLen);
		atomic_set(&fReadyPending, 0);
		fReadyLock.Unlock();
		delete[] oldReady;

		BMessage message(MSG_BUFFER_CREATED);
		fMessenger.SendMessage(&message);

		fResizing = false;
		break;
	}
	case MSG_RENDER: {
		TRACE("Got MSG_RENDER.\n");
		if (fResizing)
			break;

		// Stop the render if one is already running
		StopRender();
		Render(msg->GetDouble("locationX", 0), msg->GetDouble("locationY", 0),
			msg->GetDouble("size", 0.005));
		break;
	}
	case MSG_THREAD_RENDER_COMPLETE: {
		TRACE("Got MSG_THREAD_RENDER_COMPLETE for thread %d\n",
			msg->GetUInt8("thread", 0));

		int32 threadsStopped;
		get_sem_count(fRenderStoppedSem, &threadsStopped);
		TRACE("threadsStopped = %d\n",threadsStopped);
		if (threadsStopped == fThreadCount) {
			TRACE("Done rendering!\n");
			BMessage message(MSG_RENDER_COMPLETE);
			fMessenger.SendMessage(&message);
		}
		break;
	}

	default:
		BLooper::MessageReceived(msg);
		break;
	}
}


void FractalEngine::WriteToBitmap(BBitmap* bitmap, BRegion* dirty)
{
	Lock();
	BSize size = bitmap->Bounds().Size();
	if (size.IntegerWidth() != fWidth || size.IntegerHeight() != fHeight) {
		// some resize happened and now this won't work.
		Unlock();
		return;
	}
	if (dirty != NULL) {
		if (!fRenderStopped && fRemapAt != 0 && system_time() >= fRemapAt) {
			// show the rescaled previous image once the render is not instant
			fRemapAt = 0;
			atomic_set(&fReadyPending, 1);
		}
		if (atomic_get(&fReadyPending) == 0) {
			Unlock();
			return;
		}
	}
	fReadyLock.Lock();
	atomic_set(&fReadyPending, 0);
	fRemapAt = 0;
	bitmap->ImportBits(fReadyBuffer, fRenderBufferLen, fWidth * 4, 0,
		B_RGB32);
	fReadyLock.Unlock();
	if (dirty != NULL)
		dirty->Include(BRect(0, 0, fWidth - 1, fHeight - 1));
	Unlock();
}


void FractalEngine::Remap(const uint8* src, int32 srcW, int32 srcH, double srcX,
	double srcY, double srcSize, uint8* dst, int32 dstW, int32 dstH,
	double dstX, double dstY, double dstSize)
{
	int32* mapX = new int32[dstW];
	int32* mapY = new int32[dstH];
	for (int32 x = 0; x < dstW; x++) {
		double real = (x - dstW / 2) * dstSize + dstX;
		double px = floor((real - srcX) / srcSize + srcW / 2 + 0.5);
		mapX[x] = (px >= 0 && px < srcW) ? (int32)px : -1;
	}
	for (int32 y = 0; y < dstH; y++) {
		double imag = dstY - (y - dstH / 2) * dstSize;
		double py = floor(srcH / 2 - (imag - srcY) / srcSize + 0.5);
		mapY[y] = (py >= 0 && py < srcH) ? (int32)py : -1;
	}
	for (int32 y = 0; y < dstH; y++) {
		uint32* to = (uint32*)(dst + (size_t)y * dstW * 4);
		if (mapY[y] < 0) {
			memset(to, 0, (size_t)dstW * 4);
			continue;
		}
		const uint32* from = (const uint32*)(src + (size_t)mapY[y] * srcW * 4);
		for (int32 x = 0; x < dstW; x++)
			to[x] = mapX[x] < 0 ? 0 : from[mapX[x]];
	}
	delete[] mapX;
	delete[] mapY;
}


void FractalEngine::StopRender()
{
	if (fRenderStopped || fRenderStopping) {
		// if fRenderStopped is true, then render is already stopped,
		// so we can't stop it again!
		// if fStopRender is true, then the fRenderStoppedSem are already
		// trying to be acquired, so stuff would break if we tried to acquire
		// them again.
		return;
	}
	fRenderStopping = true;
	TRACE("Stopping render...\n");
	for (uint i = 0; i < fThreadCount; i++) {
		TRACE("Stopping thread %d\n",i);
		acquire_sem(fRenderStoppedSem);
			// wait till all the threads are stopped...
	}
	int32 threadsStopped;
	get_sem_count(fRenderStoppedSem, &threadsStopped);
	TRACE("stopped sem count after stop = %d\n",threadsStopped);
	fRenderStopping = false;
	fRenderStopped = true;

	TRACE("Render stopped.\n");
}


void FractalEngine::Render(double locationX, double locationY, double size)
{
	if (fRenderStopping)
		debugger("Error: Render shouldn't be called while fRenderStopping = true\n");
	if (!fRenderStopped)
		debugger("Error: Render already running\n");

	fTileCount = (fHeight + RENDER_TILE_ROWS - 1) / RENDER_TILE_ROWS;
	for (int32 p = 0; p < RENDER_PASSES; p++) {
		fPassNext[p] = 0;
		fPassDone[p] = 0;
	}
	fPublished = 0;

	if (fReadyBuffer != NULL) {
		// carry the previous image over to the new view
		fReadyLock.Lock();
		Remap(fReadyBuffer, fWidth, fHeight, fLocationX, fLocationY, fSize,
			fRenderBuffer, fWidth, fHeight, locationX, locationY, size);
		memcpy(fReadyBuffer, fRenderBuffer, fRenderBufferLen);
		fReadyLock.Unlock();
		fHaveImage = true;
	}
	fRenderStart = system_time();
	fRemapAt = fRenderStart + RENDER_PUBLISH_DELAY;

	fRenderStopped = false;
		// This means that future Render calls will need to call stop render

	fLocationX = locationX;
	fLocationY = locationY;
	fSize = size;

	TRACE("Location (%%.100g): %.100g;%.100g;%.100g\n", fLocationX, fLocationY, fSize);

	for (uint8 i = 0; i < fThreadCount; i++) {
		release_sem(fRenderSem);
	}
}

status_t FractalEngine::RenderThread(void* data)
{
	FractalEngine* engine = static_cast<FractalEngine*>(data);
	thread_id self = find_thread(NULL);
	uint8 threadNum = 0;
	for (uint8 i = 0; i < engine->fThreadCount; i++) {
		if (engine->fRenderThreads[i] == self) {
			threadNum = i;
			break;
		}
	}

	while (true) {
		TRACE("Thread %d awaiting semaphore...\n", threadNum);
		acquire_sem(engine->fRenderSem);
		TRACE("Thread %d got semaphore!\n", threadNum);

		uint16 width = engine->fWidth;
		uint16 height = engine->fHeight;

		for (int32 pass = 0; pass < RENDER_PASSES; pass++) {
			uint32 step = 8 >> pass;
			while (!engine->fRenderStopping) {
				int32 tile = atomic_add(&engine->fPassNext[pass], 1);
				if (tile >= engine->fTileCount)
					break;

				uint16 top = tile * RENDER_TILE_ROWS;
				uint16 bottom = min_c(top + RENDER_TILE_ROWS, height);
				for (uint16 y = top; y < bottom; y += step) {
					for (uint16 x = 0; x < width; x += step) {
						if (pass > 0 && (x & (step * 2 - 1)) == 0
								&& (y & (step * 2 - 1)) == 0)
							continue;
						engine->RenderPixel(x, y, step);
					}
				}
				if (atomic_add(&engine->fPassDone[pass], 1) + 1
						== engine->fTileCount) {
					engine->PublishPass(pass);
					atomic_set(&engine->fPublished, pass + 1);
				}
			}
			// all threads move to the next pass together
			while (!engine->fRenderStopping
					&& atomic_get(&engine->fPublished) <= pass)
				snooze(100);
			if (engine->fRenderStopping)
				break;
		}

		if (engine->fRenderStopping)
			TRACE("Thread %d stopping\n", threadNum);

		if (!engine->fRenderStopping) {
			// if we got here, then this thread has finished rendering.
			BMessage message(FractalEngine::MSG_THREAD_RENDER_COMPLETE);
			message.AddUInt8("thread", threadNum);
			engine->PostMessage(&message);
		}

		release_sem(engine->fRenderStoppedSem);
	}
	return B_OK;
}


void FractalEngine::PublishPass(int32 pass)
{
	// intermediate passes of a quick render are never shown
	if (pass < RENDER_PASSES - 1
			&& system_time() - fRenderStart < RENDER_PUBLISH_DELAY)
		return;
	fReadyLock.Lock();
	memcpy(fReadyBuffer, fRenderBuffer, fRenderBufferLen);
	atomic_set(&fReadyPending, 1);
	fReadyLock.Unlock();
}


void FractalEngine::RenderPixel(uint32 x, uint32 y, uint32 step)
{
	double real = (x * fSize + fLocationX) - (fWidth / 2 * fSize);
	double imaginary = (y * -(fSize) + fLocationY) - (fHeight / 2 * -(fSize));

	double subsampleDelta = fSize / fSubsampling;
	uint16 nSamples = fSubsampling * fSubsampling;

	int16 totalR = 0;
	int16 totalG = 0;
	int16 totalB = 0;
	for (uint8 subX = 0; subX < fSubsampling; subX++) {
		for (uint8 subY = 0; subY < fSubsampling; subY++) {
			double sampleReal = real + subX * subsampleDelta;
			double sampleImaginary = imaginary + subY * subsampleDelta;

			int32 sampleIterToEscape = (this->*fDoSet)(sampleReal, sampleImaginary);

			uint16 sampleLoc = 0;
			if (sampleIterToEscape == -1)
				// Didn't escape.
				sampleLoc = 999;
			else
				sampleLoc = 998 - (sampleIterToEscape % 999);
			sampleLoc *= 3;

			totalR += fColorset[sampleLoc + 0];
			totalG += fColorset[sampleLoc + 1];
			totalB += fColorset[sampleLoc + 2];
		}
	}


	uint32 pixel = 0xff000000 | ((totalR / nSamples) << 16)
		| ((totalG / nSamples) << 8) | (totalB / nSamples);
		// fRenderBuffer is BGR
	uint32 xEnd = min_c(x + step, (uint32)fWidth);
	uint32 yEnd = min_c(y + step, (uint32)fHeight);
	for (uint32 by = y; by < yEnd; by++) {
		uint32* row = (uint32*)(fRenderBuffer + (size_t)fWidth * by * 4);
		for (uint32 bx = x; bx < xEnd; bx++)
			row[bx] = pixel;
	}
}


// Magic numbers & other general constants
const double gJuliaA = 0;
const double gJuliaB = 1;

const uint8 gEscapeHorizon = 4;


int32 FractalEngine::DoSet_Mandelbrot(double real, double imaginary)
{
	double zReal = 0;
	double zImaginary = 0;

	for (int32 i = 0; i < fIterations; i++) {
		double zRealSq = zReal * zReal;
		double zImaginarySq = zImaginary * zImaginary;
		double nzReal = (zRealSq + (-1 * zImaginarySq));

		zImaginary = 2 * (zReal * zImaginary);
		zReal = nzReal;

		zReal += real;
		zImaginary += imaginary;

		// If it is outside the 2 unit circle...
		if ((zRealSq + zImaginarySq) > gEscapeHorizon)
			return i; // stop it from running longer
	}
	return -1;
}


int32 FractalEngine::DoSet_BurningShip(double real, double imaginary)
{
	double zReal = 0;
	double zImaginary = 0;

	// It looks "upside down" otherwise.
	imaginary = -imaginary;

	for (int32 i = 0; i < fIterations; i++) {
		zReal = fabs(zReal);
		zImaginary = fabs(zImaginary);

		double zRealSq = zReal * zReal;
		double zImaginarySq = zImaginary * zImaginary;
		double nzReal = (zRealSq + (-1 * zImaginarySq));

		zImaginary = 2 * (zReal * zImaginary);
		zReal = nzReal;

		zReal += real;
		zImaginary += imaginary;

		// If it is outside the 2 unit circle...
		if ((zRealSq + zImaginarySq) > gEscapeHorizon)
			return i; // stop it from running longer
	}
	return -1;
}


int32 FractalEngine::DoSet_Tricorn(double real, double imaginary)
{
	double zReal = 0;
	double zImaginary = 0;

	real = -real;

	for (int32 i = 0; i < fIterations; i++) {
		double znRe = zImaginary * -1;
		zImaginary = zReal * -1;
		zReal = znRe; // Swap the real and complex parts each time.

		double zRealSq = zReal * zReal;
		double zImaginarySq = zImaginary * zImaginary;
		double nzReal = (zRealSq + (-1 * zImaginarySq));

		zImaginary = 2 * (zReal * zImaginary);
		zReal = nzReal;

		zReal += real;
		zImaginary += imaginary;

		// If it is outside the 2 unit circle...
		if ((zRealSq + zImaginarySq) > gEscapeHorizon)
			return i; // stop it from running longer
	}
	return -1;
}


int32 FractalEngine::DoSet_Julia(double real, double imaginary)
{
	double zReal = real;
	double zImaginary = imaginary;

	double muRe = gJuliaA;
	double muIm = gJuliaB;

	for (int32 i = 0; i < fIterations; i++) {
		double zRealSq = zReal * zReal;
		double zImaginarySq = zImaginary * zImaginary;
		double nzReal = (zRealSq + (-1 * (zImaginarySq)));

		zImaginary = 2 * (zReal * zImaginary);
		zReal = nzReal;

		zReal += muRe;
		zImaginary += muIm;

		// If it is outside the 2 unit circle...
		if ((zRealSq + zImaginarySq) > gEscapeHorizon)
			return i; // stop it from running longer
	}
	return -1;
}


int32 FractalEngine::DoSet_OrbitTrap(double real, double imaginary)
{
	double zReal = 0;
	double zImaginary = 0;

	double closest = 10000000;
	double distance = 0;
	double lineDist = 0;

	for (int32 i = 0; i < fIterations; i++) {
		double zRealSq = zReal * zReal;
		double zImaginarySq = zImaginary * zImaginary;
		double nzReal = (zRealSq + (-1 * zImaginarySq));

		zImaginary = 2 * (zReal * zImaginary);
		zReal = nzReal;

		zReal += real;
		zImaginary += imaginary;

		distance = sqrt(zRealSq + zImaginarySq);
		lineDist = fabs(zReal + zImaginary);

		// If it is closer than ever before...
		if (lineDist < closest)
			closest = lineDist;

		if (distance > gEscapeHorizon)
			return static_cast<int32>(floor(4 * log(4 / closest)));
	}
	return static_cast<int32>(floor(4 * log(4 / closest)));
}


int32 FractalEngine::DoSet_Multibrot(double real, double imaginary)
{
	double zReal = 0;
	double zImaginary = 0;

	for (int32 i = 0; i < fIterations; i++) {
		double zRealSq = zReal * zReal;
		double zImaginarySq = zImaginary * zImaginary;
		double nzReal = (zRealSq * zReal - 3 * zReal * zImaginarySq);

		zImaginary = 3 * (zRealSq * zImaginary) - (zImaginarySq * zImaginary);

		zReal = nzReal;
		zReal += real;
		zImaginary += imaginary;

		// If it is outside the 2 unit circle...
		if ((zRealSq + zImaginarySq) > gEscapeHorizon)
			return i; // stop it from running longer
	}
	return -1;
}
