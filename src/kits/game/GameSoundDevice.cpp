/*
 * Vitruvian — BGameSoundDevice
 * Distributed under the terms of the MIT License.
 *
 * Reworked for media2: backed by a single BSoundPlayer. The PlayBuffer
 * callback zeros the output buffer and then walks fSounds, calling Play()
 * on each — every active SimpleSoundBuffer mixes its data in additively
 * (F32 fast path). Legacy GameProducer + Connection + media_node are gone.
 */

#include "GameSoundDevice.h"

#include <new>
#include <stdlib.h>
#include <string.h>

#include <mutex>

#include <media2/SoundPlayer.h>

#include "GameSoundBuffer.h"
#include "GSUtility.h"


static const gs_audio_format kDefaultFormat = {
	44100.0f,
	2,
	gs_audio_format::B_GS_F,
	B_MEDIA_LITTLE_ENDIAN,
	2048
};


class BGameSoundDevice::Mixer {
public:
	Mixer(BGameSoundDevice* device)
		:
		fDevice(device),
		fPlayer(NULL),
		fStarted(false)
	{
	}

	~Mixer()
	{
		Stop();
		delete fPlayer;
	}

	status_t Start(const gs_audio_format& fmt)
	{
		std::lock_guard<std::mutex> _(fLock);
		if (fStarted)
			return B_OK;

		BMediaFormat mfmt;
		mfmt.SetToDefault();
		media_raw_audio_format& raw = mfmt.format.u.raw_audio;
		raw.frame_rate    = fmt.frame_rate;
		raw.channel_count = fmt.channel_count;
		// Force F32LE for the mixer fast path.
		raw.format        = media_raw_audio_format::B_AUDIO_FLOAT;
		raw.byte_order    = B_MEDIA_LITTLE_ENDIAN;
		raw.buffer_size   = fmt.buffer_size;

		fPlayer = new(std::nothrow) BSoundPlayer(&mfmt, "game kit",
			&Mixer::_PlayCallback, NULL, this);
		if (fPlayer == NULL)
			return B_NO_MEMORY;
		if (fPlayer->InitCheck() != B_OK) {
			status_t err = fPlayer->InitCheck();
			delete fPlayer;
			fPlayer = NULL;
			return err;
		}
		fPlayer->SetHasData(true);
		fPlayer->Start();
		fStarted = true;
		return B_OK;
	}

	void Stop()
	{
		std::lock_guard<std::mutex> _(fLock);
		if (!fStarted || fPlayer == NULL)
			return;
		fPlayer->Stop(true, true);
		fStarted = false;
	}

	std::mutex& Lock() { return fLock; }

private:
	static void _PlayCallback(void* cookie, void* buffer, size_t size,
		const media_raw_audio_format& fmt)
	{
		Mixer* self = (Mixer*)cookie;
		memset(buffer, 0, size);
		const int64 frames = size / (sizeof(float) * fmt.channel_count);

		std::lock_guard<std::mutex> _(self->fLock);
		BGameSoundDevice* dev = self->fDevice;
		for (int32 i = 0; i < dev->fSoundCount; i++) {
			GameSoundBuffer* b = dev->fSounds[i];
			if (b != NULL && b->IsPlaying())
				b->Play(buffer, frames);
		}
	}

	BGameSoundDevice*	fDevice;
	std::mutex			fLock;
	BSoundPlayer*		fPlayer;
	bool				fStarted;
};


// #pragma mark - BGameSoundDevice


BGameSoundDevice::BGameSoundDevice()
	:
	fFormat(kDefaultFormat),
	fInitError(B_NO_INIT),
	fIsConnected(false),
	fSoundCount(0),
	fSounds(NULL),
	fMixer(NULL)
{
	fMixer = new(std::nothrow) Mixer(this);
	if (fMixer == NULL) {
		fInitError = B_NO_MEMORY;
		return;
	}
	fInitError = fMixer->Start(fFormat);
}


BGameSoundDevice::~BGameSoundDevice()
{
	if (fMixer != NULL)
		fMixer->Stop();

	for (int32 i = 0; i < fSoundCount; i++)
		delete fSounds[i];
	free(fSounds);

	delete fMixer;
}


status_t
BGameSoundDevice::InitCheck() const
{
	return fInitError;
}


const gs_audio_format&
BGameSoundDevice::Format() const
{
	return fFormat;
}


const gs_audio_format&
BGameSoundDevice::Format(gs_id sound) const
{
	std::lock_guard<std::mutex> _(fMixer->Lock());
	if (sound > 0 && sound <= fSoundCount && fSounds[sound - 1] != NULL)
		return fSounds[sound - 1]->Format();
	return fFormat;
}


int32
BGameSoundDevice::AllocateSound()
{
	for (int32 i = 0; i < fSoundCount; i++) {
		if (fSounds[i] == NULL)
			return i;
	}
	const int32 newCount = fSoundCount + 1;
	GameSoundBuffer** newArr = (GameSoundBuffer**)realloc(fSounds,
		newCount * sizeof(GameSoundBuffer*));
	if (newArr == NULL)
		return -1;
	newArr[fSoundCount] = NULL;
	fSounds = newArr;
	const int32 idx = fSoundCount;
	fSoundCount = newCount;
	return idx;
}


status_t
BGameSoundDevice::CreateBuffer(gs_id* sound, const gs_audio_format* format,
	const void* data, int64 frames)
{
	if (sound == NULL || format == NULL || data == NULL)
		return B_BAD_VALUE;
	if (fInitError != B_OK)
		return fInitError;

	SimpleSoundBuffer* b = new(std::nothrow) SimpleSoundBuffer(format, data,
		frames);
	if (b == NULL)
		return B_NO_MEMORY;

	std::lock_guard<std::mutex> _(fMixer->Lock());
	const int32 idx = AllocateSound();
	if (idx < 0) {
		delete b;
		return B_NO_MEMORY;
	}
	fSounds[idx] = b;
	*sound = (gs_id)(idx + 1);
	return B_OK;
}


status_t
BGameSoundDevice::CreateBuffer(gs_id* sound, const void* object,
	const gs_audio_format* format, size_t /*inBufferFrameCount*/,
	size_t /*inBufferCount*/)
{
	if (sound == NULL || format == NULL || object == NULL)
		return B_BAD_VALUE;
	if (fInitError != B_OK)
		return fInitError;

	StreamingSoundBuffer* b = new(std::nothrow) StreamingSoundBuffer(format,
		const_cast<void*>(object));
	if (b == NULL)
		return B_NO_MEMORY;

	std::lock_guard<std::mutex> _(fMixer->Lock());
	const int32 idx = AllocateSound();
	if (idx < 0) {
		delete b;
		return B_NO_MEMORY;
	}
	fSounds[idx] = b;
	*sound = (gs_id)(idx + 1);
	return B_OK;
}


void
BGameSoundDevice::ReleaseBuffer(gs_id sound)
{
	if (fInitError != B_OK || sound <= 0)
		return;
	std::lock_guard<std::mutex> _(fMixer->Lock());
	if (sound > fSoundCount)
		return;
	const int32 idx = (int32)(sound - 1);
	delete fSounds[idx];
	fSounds[idx] = NULL;
}


status_t
BGameSoundDevice::Buffer(gs_id sound, gs_audio_format* format, void*& data)
{
	if (sound <= 0 || sound > fSoundCount || format == NULL)
		return B_BAD_VALUE;
	std::lock_guard<std::mutex> _(fMixer->Lock());
	GameSoundBuffer* b = fSounds[sound - 1];
	if (b == NULL)
		return B_ENTRY_NOT_FOUND;
	*format = b->Format();
	data = b->Data();
	return B_OK;
}


bool
BGameSoundDevice::IsPlaying(gs_id sound)
{
	if (sound <= 0 || sound > fSoundCount)
		return false;
	std::lock_guard<std::mutex> _(fMixer->Lock());
	GameSoundBuffer* b = fSounds[sound - 1];
	return b != NULL && b->IsPlaying();
}


status_t
BGameSoundDevice::StartPlaying(gs_id sound)
{
	if (sound <= 0 || sound > fSoundCount)
		return B_BAD_VALUE;
	std::lock_guard<std::mutex> _(fMixer->Lock());
	GameSoundBuffer* b = fSounds[sound - 1];
	if (b == NULL)
		return B_ENTRY_NOT_FOUND;
	return b->StartPlaying();
}


status_t
BGameSoundDevice::StopPlaying(gs_id sound)
{
	if (sound <= 0 || sound > fSoundCount)
		return B_BAD_VALUE;
	std::lock_guard<std::mutex> _(fMixer->Lock());
	GameSoundBuffer* b = fSounds[sound - 1];
	if (b == NULL)
		return B_ENTRY_NOT_FOUND;
	return b->StopPlaying();
}


status_t
BGameSoundDevice::GetAttributes(gs_id sound, gs_attribute* attributes,
	size_t count)
{
	if (sound <= 0 || sound > fSoundCount)
		return B_BAD_VALUE;
	std::lock_guard<std::mutex> _(fMixer->Lock());
	GameSoundBuffer* b = fSounds[sound - 1];
	if (b == NULL)
		return B_ENTRY_NOT_FOUND;
	return b->GetAttributes(attributes, count);
}


status_t
BGameSoundDevice::SetAttributes(gs_id sound, gs_attribute* attributes,
	size_t count)
{
	if (sound <= 0 || sound > fSoundCount)
		return B_BAD_VALUE;
	std::lock_guard<std::mutex> _(fMixer->Lock());
	GameSoundBuffer* b = fSounds[sound - 1];
	if (b == NULL)
		return B_ENTRY_NOT_FOUND;
	return b->SetAttributes(attributes, count);
}


void
BGameSoundDevice::SetInitError(status_t error)
{
	fInitError = error;
}


// #pragma mark - default device singleton


static std::mutex       sDefaultDeviceLock;
static BGameSoundDevice*	sDefaultDevice = NULL;
static int32            sDefaultDeviceRefs = 0;


BGameSoundDevice*
GetDefaultDevice()
{
	std::lock_guard<std::mutex> _(sDefaultDeviceLock);
	if (sDefaultDevice == NULL) {
		sDefaultDevice = new(std::nothrow) BGameSoundDevice();
		if (sDefaultDevice == NULL)
			return NULL;
	}
	sDefaultDeviceRefs++;
	return sDefaultDevice;
}


void
ReleaseDevice()
{
	std::lock_guard<std::mutex> _(sDefaultDeviceLock);
	if (sDefaultDeviceRefs > 0)
		sDefaultDeviceRefs--;
	if (sDefaultDeviceRefs == 0) {
		delete sDefaultDevice;
		sDefaultDevice = NULL;
	}
}
