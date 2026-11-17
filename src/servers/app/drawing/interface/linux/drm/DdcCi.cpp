/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "DdcCi.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/i2c-dev.h>
#include <sys/ioctl.h>


static const int kAddress = 0x37;
static const int kBrightness = 0x10;
static const useconds_t kDelay = 50000;
static const int kRetries = 3;


static void
_Delay()
{
	usleep(kDelay);
}


static bool
_GetVcp(int fd, uint8_t code, int* current, int* max)
{
	uint8_t req[5] = { 0x51, 0x82, 0x01, code, 0 };
	uint8_t sum = 0x6e;
	for (int i = 0; i < 4; i++)
		sum ^= req[i];
	req[4] = sum;
	if (write(fd, req, sizeof(req)) != (ssize_t)sizeof(req))
		return false;
	_Delay();

	uint8_t rep[11];
	if (read(fd, rep, sizeof(rep)) != (ssize_t)sizeof(rep))
		return false;
	uint8_t check = 0x50;
	for (int i = 0; i < 10; i++)
		check ^= rep[i];
	// 6e len=0x88 opcode=0x02 result=0 code type maxH maxL curH curL chk
	if (check != rep[10] || rep[1] != 0x88 || rep[2] != 0x02 || rep[3] != 0
		|| rep[4] != code) {
		return false;
	}
	*max = (rep[6] << 8) | rep[7];
	*current = (rep[8] << 8) | rep[9];
	return true;
}


static bool
_SetVcp(int fd, uint8_t code, int value)
{
	uint8_t req[7] = { 0x51, 0x84, 0x03, code, (uint8_t)(value >> 8),
		(uint8_t)value, 0 };
	uint8_t sum = 0x6e;
	for (int i = 0; i < 6; i++)
		sum ^= req[i];
	req[6] = sum;
	const bool ok = write(fd, req, sizeof(req)) == (ssize_t)sizeof(req);
	_Delay();
	return ok;
}


DdcCi::DdcCi()
	:
	fQuit(false),
	fSupported(false),
	fMax(0),
	fCurrent(-1),
	fPending(-1),
	fDeferred(-1.0f),
	fState("idle")
{
}


DdcCi::~DdcCi()
{
	Stop();
}


void
DdcCi::Start(const char* connector)
{
	Stop();
	{
		std::lock_guard<std::mutex> guard(fLock);
		fQuit = false;
		fSupported = false;
		fMax = 0;
		fCurrent = -1;
		fPending = -1;
		fBus.clear();
		fState = "probing";
	}
	fThread = std::thread(&DdcCi::_Run, this, std::string(connector), 0);
}


void
DdcCi::Stop()
{
	{
		std::lock_guard<std::mutex> guard(fLock);
		fQuit = true;
	}
	fWake.notify_all();
	if (fThread.joinable())
		fThread.join();
}


bool
DdcCi::Supported() const
{
	std::lock_guard<std::mutex> guard(fLock);
	return fSupported;
}


int
DdcCi::Max() const
{
	std::lock_guard<std::mutex> guard(fLock);
	return fMax;
}


int
DdcCi::Current() const
{
	std::lock_guard<std::mutex> guard(fLock);
	return fCurrent;
}


void
DdcCi::RequestFraction(float fraction)
{
	{
		std::lock_guard<std::mutex> guard(fLock);
		if (!fSupported) {
			fDeferred = fraction;
			return;
		}
		fPending = (int)(fraction * fMax + 0.5f);
		fCurrent = fPending;
	}
	fWake.notify_all();
}


void
DdcCi::Request(int value)
{
	{
		std::lock_guard<std::mutex> guard(fLock);
		if (!fSupported)
			return;
		fDeferred = -1.0f;
		if (value < 0)
			value = 0;
		if (value > fMax)
			value = fMax;
		fPending = value;
		fCurrent = value;
	}
	fWake.notify_all();
}


std::string
DdcCi::Bus() const
{
	std::lock_guard<std::mutex> guard(fLock);
	return fBus;
}


std::string
DdcCi::State() const
{
	std::lock_guard<std::mutex> guard(fLock);
	return fState;
}


void
DdcCi::_Run(std::string connector, int)
{
	std::string link = "/sys/class/drm/" + connector + "/ddc";
	char target[PATH_MAX];
	ssize_t len = readlink(link.c_str(), target, sizeof(target) - 1);
	if (len <= 0) {
		std::lock_guard<std::mutex> guard(fLock);
		fState = "no-ddc";
		return;
	}
	target[len] = '\0';
	const char* base = strrchr(target, '/');
	base = base != NULL ? base + 1 : target;

	std::string node = std::string("/dev/") + base;
	int fd = open(node.c_str(), O_RDWR | O_CLOEXEC);
	if (fd < 0 || ioctl(fd, I2C_SLAVE, kAddress) < 0) {
		if (fd >= 0)
			close(fd);
		std::lock_guard<std::mutex> guard(fLock);
		fState = "no-bus";
		fBus = node;
		return;
	}

	int current = 0;
	int max = 0;
	bool ok = false;
	for (int i = 0; i < kRetries && !ok; i++) {
		_Delay();
		ok = _GetVcp(fd, kBrightness, &current, &max) && max > 0;
	}

	{
		std::lock_guard<std::mutex> guard(fLock);
		fBus = node;
		if (!ok) {
			fState = "no-reply";
			close(fd);
			return;
		}
		fSupported = true;
		fMax = max;
		fCurrent = current;
		fState = "ok";
		// Apply a value requested before the probe finished.
		if (fDeferred >= 0.0f) {
			fPending = (int)(fDeferred * max + 0.5f);
			fCurrent = fPending;
			fDeferred = -1.0f;
		}
	}

	std::unique_lock<std::mutex> lock(fLock);
	for (;;) {
		fWake.wait(lock, [this] { return fQuit || fPending >= 0; });
		if (fQuit)
			break;
		const int value = fPending;
		fPending = -1;
		lock.unlock();
		for (int i = 0; i < kRetries; i++) {
			if (_SetVcp(fd, kBrightness, value))
				break;
		}
		lock.lock();
	}
	lock.unlock();
	close(fd);
}
