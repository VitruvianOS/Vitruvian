/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/SoundPlayer.h>

#include <math.h>
#include <new>
#include <stdlib.h>
#include <string.h>

#include <Locker.h>
#include <OS.h>

#include <media2/MediaPlayer.h>
#include <media2/Sound.h>


static int32 sNextPlayID = 1;


class BSoundPlayer::Impl {
public:
	Impl(BSoundPlayer* owner, const BMediaFormat& format, const char* name,
		BSoundPlayer::BufferPlayerFunc playFunc,
		BSoundPlayer::EventNotifierFunc notifyFunc, void* cookie);
	~Impl();

	void* Cookie() const        { return fCookie; }
	void  SetCookie(void* c)    { fCookie = c; }

	status_t InitCheck() const { return fPlayer.InitCheck(); }

	const BMediaFormat& Format() const { return fPlayer.Format(); }
	status_t SetFormat(const BMediaFormat& fmt) { return fPlayer.SetFormat(fmt); }

	status_t Start();
	void     Stop(bool block, bool flush);
	bool     IsPlaying() const { return fPlayer.IsPlaying(); }

	float Volume() const { return fPlayer.Volume(); }
	void  SetVolume(float v) { fPlayer.SetVolume(v); }

	bool  HasData() const     { return fPlayer.HasData(); }
	void  SetHasData(bool v)  { fPlayer.SetHasData(v); }

	size_t BufferSize() const { return fPlayer.BufferSize(); }
	bigtime_t Latency() const { return 0; }

	void SetCallbacks(BSoundPlayer::BufferPlayerFunc playFunc,
		BSoundPlayer::EventNotifierFunc notifyFunc, void* cookie);
	void SetPlayFunc(BSoundPlayer::BufferPlayerFunc f);
	void SetNotifyFunc(BSoundPlayer::EventNotifierFunc n);

	bigtime_t CurrentTime() const;
	bigtime_t PerformanceTime() const { return system_time(); }

	status_t Preroll() const { return fPlayer.InitCheck(); }

	BSoundPlayer::play_id StartPlaying(BSound* sound, bigtime_t atTime,
		float volume);
	status_t SetSoundVolume(BSoundPlayer::play_id id, float volume);
	bool     IsSoundPlaying(BSoundPlayer::play_id id);
	status_t StopPlaying(BSoundPlayer::play_id id);
	status_t WaitForSound(BSoundPlayer::play_id id);
	status_t GetVolumeInfo(int32* _parameterID, float* _minDB, float* _maxDB);

	void DeliverSoundBuffer(void* buffer, size_t size,
		const media_raw_audio_format& format);
	void DispatchNotify(BSoundPlayer::sound_player_notification what);

private:
	static void _FillFunc(void* cookie, void* buffer, size_t size,
		const media_raw_audio_format& format);
	static void _NotifyFunc(void* cookie, BMediaPlayer::sound_player_notification what, ...);

	struct playing_sound {
		playing_sound*	next;
		off_t			current_offset;
		BSound*			sound;
		BSoundPlayer::play_id	id;
		float			volume;
		sem_id			wait_sem;
	};

	struct waiting_sound {
		waiting_sound*	next;
		bigtime_t		start_time;
		BSound*			sound;
		BSoundPlayer::play_id	id;
		float			volume;
	};

	void _PromoteWaitingSounds_Locked();
	status_t _StopPlayingLocked(playing_sound** link, bool notify);

	BSoundPlayer*		fOwner;
	BMediaPlayer		fPlayer;
	BSoundPlayer::BufferPlayerFunc		fPlayFunc;
	BSoundPlayer::EventNotifierFunc	fNotifyFunc;
	void*				fCookie;

	BLocker				fSoundLock;
	playing_sound*		fPlayingSounds;
	waiting_sound*		fWaitingSounds;

	bigtime_t			fPlayStartWall;
	bigtime_t			fFrozenCurrentTime;
};


BSoundPlayer::Impl::Impl(BSoundPlayer* owner, const BMediaFormat& format,
	const char*, BSoundPlayer::BufferPlayerFunc playFunc,
	BSoundPlayer::EventNotifierFunc notifyFunc, void* cookie)
	:
	fOwner(owner),
	fPlayer(),
	fPlayFunc(playFunc),
	fNotifyFunc(notifyFunc),
	fCookie(cookie),
	fSoundLock("BSoundPlayer sounds"),
	fPlayingSounds(NULL),
	fWaitingSounds(NULL),
	fPlayStartWall(-1),
	fFrozenCurrentTime(0)
{
	fPlayer.SetFormat(format);
	fPlayer.SetHooks(&Impl::_FillFunc, &Impl::_NotifyFunc, this);
}


BSoundPlayer::Impl::~Impl()
{
	while (fPlayingSounds != NULL)
		_StopPlayingLocked(&fPlayingSounds, false);
	while (fWaitingSounds != NULL) {
		waiting_sound* w = fWaitingSounds;
		fWaitingSounds = w->next;
		w->sound->Release();
		free(w);
	}
}


status_t
BSoundPlayer::Impl::Start()
{
	if (!IsPlaying())
		fPlayStartWall = system_time();
	return fPlayer.Play();
}


void
BSoundPlayer::Impl::Stop(bool /*block*/, bool /*flush*/)
{
	if (fPlayStartWall >= 0) {
		fFrozenCurrentTime = system_time() - fPlayStartWall;
		fPlayStartWall = -1;
	}
	fPlayer.Stop();
}


bigtime_t
BSoundPlayer::Impl::CurrentTime() const
{
	return fPlayStartWall >= 0 ? system_time() - fPlayStartWall
		: fFrozenCurrentTime;
}


void
BSoundPlayer::Impl::SetCallbacks(BSoundPlayer::BufferPlayerFunc playFunc,
	BSoundPlayer::EventNotifierFunc notifyFunc, void* cookie)
{
	fPlayFunc   = playFunc;
	fNotifyFunc = notifyFunc;
	fCookie     = cookie;
}


void
BSoundPlayer::Impl::SetPlayFunc(BSoundPlayer::BufferPlayerFunc f)
{
	fPlayFunc = f;
}


void
BSoundPlayer::Impl::SetNotifyFunc(BSoundPlayer::EventNotifierFunc n)
{
	fNotifyFunc = n;
}


void
BSoundPlayer::Impl::_FillFunc(void* cookie, void* buffer, size_t size,
	const media_raw_audio_format& format)
{
	Impl* self = (Impl*)cookie;
	if (self->fPlayFunc != NULL) {
		self->fPlayFunc(self->fCookie, buffer, size, format);
		return;
	}

	if (self->fOwner != NULL)
		self->fOwner->PlayBuffer(buffer, size, format);
}


void
BSoundPlayer::Impl::_NotifyFunc(void* cookie,
	BMediaPlayer::sound_player_notification what, ...)
{
	Impl* self = (Impl*)cookie;
	const BSoundPlayer::sound_player_notification mapped =
		(BSoundPlayer::sound_player_notification)what;
	if (self->fNotifyFunc != NULL)
		self->fNotifyFunc(self->fCookie, mapped);
	else if (self->fOwner != NULL)
		self->fOwner->Notify(mapped);
}


void
BSoundPlayer::Impl::_PromoteWaitingSounds_Locked()
{
	const bigtime_t now = PerformanceTime();
	waiting_sound** link = &fWaitingSounds;
	while (*link != NULL) {
		waiting_sound* w = *link;
		if (w->start_time > now) {
			link = &w->next;
			continue;
		}
		*link = w->next;

		playing_sound* p = (playing_sound*)malloc(sizeof(playing_sound));
		if (p == NULL) {
			w->sound->Release();
			free(w);
			continue;
		}
		p->current_offset = 0;
		p->sound = w->sound;
		p->id = w->id;
		p->volume = w->volume;
		p->wait_sem = -1;
		p->next = fPlayingSounds;
		fPlayingSounds = p;
		free(w);
	}
}


BSoundPlayer::play_id
BSoundPlayer::Impl::StartPlaying(BSound* sound, bigtime_t atTime, float volume)
{
	if (sound == NULL)
		return B_BAD_VALUE;

	const BSoundPlayer::play_id id = atomic_add(&sNextPlayID, 1);
	sound->Acquire();

	if (!fSoundLock.Lock()) {
		sound->Release();
		return B_ERROR;
	}

	if (atTime <= PerformanceTime()) {
		playing_sound* p = (playing_sound*)malloc(sizeof(playing_sound));
		if (p == NULL) {
			fSoundLock.Unlock();
			sound->Release();
			return B_NO_MEMORY;
		}
		p->current_offset = 0;
		p->sound = sound;
		p->id = id;
		p->volume = volume;
		p->wait_sem = -1;
		p->next = fPlayingSounds;
		fPlayingSounds = p;
	} else {
		waiting_sound* w = (waiting_sound*)malloc(sizeof(waiting_sound));
		if (w == NULL) {
			fSoundLock.Unlock();
			sound->Release();
			return B_NO_MEMORY;
		}
		w->start_time = atTime;
		w->sound = sound;
		w->id = id;
		w->volume = volume;
		w->next = fWaitingSounds;
		fWaitingSounds = w;
	}
	fSoundLock.Unlock();

	fPlayer.SetHasData(true);
	return id;
}


status_t
BSoundPlayer::Impl::SetSoundVolume(BSoundPlayer::play_id id, float volume)
{
	if (!fSoundLock.Lock())
		return B_ERROR;
	for (playing_sound* p = fPlayingSounds; p != NULL; p = p->next) {
		if (p->id == id) {
			p->volume = volume;
			fSoundLock.Unlock();
			return B_OK;
		}
	}
	for (waiting_sound* w = fWaitingSounds; w != NULL; w = w->next) {
		if (w->id == id) {
			w->volume = volume;
			fSoundLock.Unlock();
			return B_OK;
		}
	}
	fSoundLock.Unlock();
	return B_ENTRY_NOT_FOUND;
}


bool
BSoundPlayer::Impl::IsSoundPlaying(BSoundPlayer::play_id id)
{
	if (!fSoundLock.Lock())
		return false;
	for (playing_sound* p = fPlayingSounds; p != NULL; p = p->next) {
		if (p->id == id) {
			fSoundLock.Unlock();
			return true;
		}
	}
	fSoundLock.Unlock();
	return false;
}


status_t
BSoundPlayer::Impl::_StopPlayingLocked(playing_sound** link, bool notify)
{
	playing_sound* item = *link;
	*link = item->next;
	const BSoundPlayer::play_id id = item->id;
	const sem_id waitSem = item->wait_sem;
	item->sound->Release();
	free(item);
	fSoundLock.Unlock();

	if (notify && fOwner != NULL)
		fOwner->Notify(BSoundPlayer::B_SOUND_DONE, id, true);
	if (waitSem >= 0)
		release_sem(waitSem);
	return B_OK;
}


status_t
BSoundPlayer::Impl::StopPlaying(BSoundPlayer::play_id id)
{
	if (!fSoundLock.Lock())
		return B_ERROR;
	for (playing_sound** link = &fPlayingSounds; *link != NULL;
			link = &(*link)->next) {
		if ((*link)->id == id)
			return _StopPlayingLocked(link, true);
	}
	fSoundLock.Unlock();
	return B_ENTRY_NOT_FOUND;
}


status_t
BSoundPlayer::Impl::WaitForSound(BSoundPlayer::play_id id)
{
	if (!fSoundLock.Lock())
		return B_ERROR;
	for (playing_sound* p = fPlayingSounds; p != NULL; p = p->next) {
		if (p->id == id) {
			if (p->wait_sem < 0)
				p->wait_sem = create_sem(0, "wait for sound");
			sem_id waitSem = p->wait_sem;
			fSoundLock.Unlock();
			return acquire_sem(waitSem);
		}
	}
	fSoundLock.Unlock();
	return B_ENTRY_NOT_FOUND;
}


status_t
BSoundPlayer::Impl::GetVolumeInfo(int32*, float*, float*)
{
	return B_NO_INIT;
}


void
BSoundPlayer::Impl::DeliverSoundBuffer(void* buffer, size_t size,
	const media_raw_audio_format&)
{
	if (!fSoundLock.Lock()) {
		memset(buffer, 0, size);
		return;
	}

	_PromoteWaitingSounds_Locked();

	playing_sound* item = fPlayingSounds;
	if (item == NULL) {
		fSoundLock.Unlock();
		fPlayer.SetHasData(false);
		memset(buffer, 0, size);
		return;
	}

	size_t used = 0;
	if (!item->sound->GetDataAt(item->current_offset, buffer, size, &used)) {
		playing_sound** link = &fPlayingSounds;
		_StopPlayingLocked(link, true);
		memset(buffer, 0, size);
		return;
	}
	item->current_offset += used;
	fSoundLock.Unlock();

	if (used < size)
		memset((uint8_t*)buffer + used, 0, size - used);
}


void
BSoundPlayer::Impl::DispatchNotify(BSoundPlayer::sound_player_notification what)
{
	if (fNotifyFunc != NULL)
		fNotifyFunc(fCookie, what);
}


BSoundPlayer::BSoundPlayer(const BMediaFormat* format, const char* name,
	BufferPlayerFunc playFunc, EventNotifierFunc notifyFunc, void* cookie)
	:
	fImpl(NULL)
{
	BMediaFormat fmt;
	if (format != NULL)
		fmt = *format;
	else
		fmt.SetToDefault();
	fImpl = new(std::nothrow) Impl(this, fmt, name, playFunc, notifyFunc, cookie);
}


BSoundPlayer::BSoundPlayer(const char* name, BufferPlayerFunc playFunc,
	EventNotifierFunc notifyFunc, void* cookie)
	:
	fImpl(NULL)
{
	BMediaFormat fmt;
	fmt.SetToDefault();
	fImpl = new(std::nothrow) Impl(this, fmt, name, playFunc, notifyFunc, cookie);
}


void
BSoundPlayer::PlayBuffer(void* buffer, size_t size,
	const media_raw_audio_format& format)
{
	if (fImpl != NULL)
		fImpl->DeliverSoundBuffer(buffer, size, format);
}


void
BSoundPlayer::Notify(sound_player_notification what, ...)
{
	if (fImpl != NULL)
		fImpl->DispatchNotify(what);
}


bigtime_t BSoundPlayer::CurrentTime() const     { return fImpl ? fImpl->CurrentTime() : 0; }
bigtime_t BSoundPlayer::PerformanceTime() const { return fImpl ? fImpl->PerformanceTime() : (bigtime_t)B_ERROR; }
status_t  BSoundPlayer::Preroll()               { return fImpl ? fImpl->Preroll() : B_NO_INIT; }


BSoundPlayer::play_id
BSoundPlayer::StartPlaying(BSound* sound, bigtime_t atTime)
{
	return StartPlaying(sound, atTime, 1.0f);
}


BSoundPlayer::play_id
BSoundPlayer::StartPlaying(BSound* sound, bigtime_t atTime, float withVolume)
{
	return fImpl ? fImpl->StartPlaying(sound, atTime, withVolume) : B_NO_INIT;
}


status_t
BSoundPlayer::SetSoundVolume(play_id id, float newVolume)
{
	return fImpl ? fImpl->SetSoundVolume(id, newVolume) : B_NO_INIT;
}


bool
BSoundPlayer::IsPlaying(play_id id)
{
	return fImpl != NULL && fImpl->IsSoundPlaying(id);
}


status_t
BSoundPlayer::StopPlaying(play_id id)
{
	return fImpl ? fImpl->StopPlaying(id) : B_NO_INIT;
}


status_t
BSoundPlayer::WaitForSound(play_id id)
{
	return fImpl ? fImpl->WaitForSound(id) : B_NO_INIT;
}


status_t
BSoundPlayer::GetVolumeInfo(int32* _parameterID, float* _minDB, float* _maxDB)
{
	return fImpl ? fImpl->GetVolumeInfo(_parameterID, _minDB, _maxDB) : B_NO_INIT;
}


void
BSoundPlayer::SetBufferPlayer(BufferPlayerFunc playFunc)
{
	if (fImpl != NULL)
		fImpl->SetPlayFunc(playFunc);
}


void
BSoundPlayer::SetNotifier(EventNotifierFunc notifyFunc)
{
	if (fImpl != NULL)
		fImpl->SetNotifyFunc(notifyFunc);
}


void*
BSoundPlayer::Cookie() const
{
	return fImpl != NULL ? fImpl->Cookie() : NULL;
}


void
BSoundPlayer::SetCookie(void* cookie)
{
	if (fImpl != NULL)
		fImpl->SetCookie(cookie);
}


BSoundPlayer::~BSoundPlayer()
{
	delete fImpl;
}


status_t
BSoundPlayer::InitCheck() const
{
	return fImpl != NULL ? fImpl->InitCheck() : B_NO_MEMORY;
}


const BMediaFormat&
BSoundPlayer::Format() const
{
	static BMediaFormat sEmpty;
	return fImpl != NULL ? fImpl->Format() : sEmpty;
}


status_t  BSoundPlayer::SetFormat(const BMediaFormat& f) { return fImpl ? fImpl->SetFormat(f) : B_NO_INIT; }
status_t  BSoundPlayer::Start()                          { return fImpl ? fImpl->Start() : B_NO_INIT; }
void      BSoundPlayer::Stop(bool b, bool f)             { if (fImpl) fImpl->Stop(b, f); }
bool      BSoundPlayer::IsPlaying() const                { return fImpl && fImpl->IsPlaying(); }
float     BSoundPlayer::Volume() const                   { return fImpl ? fImpl->Volume() : 0.0f; }
size_t    BSoundPlayer::BufferSize() const               { return fImpl ? fImpl->BufferSize() : 0; }
bool      BSoundPlayer::HasData() const                  { return fImpl && fImpl->HasData(); }
void      BSoundPlayer::SetHasData(bool v)               { if (fImpl) fImpl->SetHasData(v); }
bigtime_t BSoundPlayer::Latency() const                  { return fImpl ? fImpl->Latency() : 0; }


status_t
BSoundPlayer::SetVolume(float v)
{
	if (fImpl == NULL)
		return B_NO_INIT;
	fImpl->SetVolume(v);
	return B_OK;
}


status_t
BSoundPlayer::SetVolumeDB(float dB)
{
	return SetVolume(powf(10.0f, dB / 20.0f));
}


float
BSoundPlayer::VolumeDB() const
{
	const float v = Volume();
	if (v <= 0.0f)
		return -INFINITY;
	return 20.0f * log10f(v);
}


void
BSoundPlayer::SetCallbacks(BufferPlayerFunc playFunc,
	EventNotifierFunc notifyFunc, void* cookie)
{
	if (fImpl != NULL)
		fImpl->SetCallbacks(playFunc, notifyFunc, cookie);
}
