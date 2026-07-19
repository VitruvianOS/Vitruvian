/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaAutomation.h>

#include <math.h>
#include <new>
#include <vector>

#include <Message.h>
#include <OS.h>

#include <media2/Controllable.h>


struct AutoPoint {
	bigtime_t	time;
	float		value;
};


struct ParamTrack {
	int32								id;
	BMediaAutomation::interpolation_mode interp;
	std::vector<AutoPoint>				points;	// kept sorted by time
};


class BMediaAutomation::Impl {
public:
	Impl(BControllable* target);
	~Impl();

	status_t AddPoint(int32 paramId, bigtime_t time, float value);
	status_t RemovePoint(int32 paramId, bigtime_t time);
	status_t ClearParameter(int32 paramId);
	void     ClearAll();

	int32    CountParameters() const { return (int32)fTracks.size(); }
	int32    ParameterIdAt(int32 i) const;
	int32    CountPoints(int32 paramId) const;
	status_t GetPointAt(int32 paramId, int32 index, bigtime_t* time,
		float* value) const;
	float    ValueAt(int32 paramId, bigtime_t time) const;

	void     SetInterp(int32 paramId, BMediaAutomation::interpolation_mode mode);
	BMediaAutomation::interpolation_mode Interp(int32 paramId) const;

	status_t StartPlayback(bigtime_t startTime);
	status_t StopPlayback();
	bool     IsPlaying() const { return fPlayThread > 0; }

	status_t StartRecording();
	status_t StopRecording();
	bool     IsRecording() const { return fRecording; }

	void     PackToMessage(BMessage& out) const;
	status_t UnpackFromMessage(const BMessage& in);

private:
	ParamTrack* _FindOrAdd(int32 paramId);
	const ParamTrack* _Find(int32 paramId) const;
	static int32 _PlayThunk(void* arg);
	void _PlayLoop();

	BControllable*			fTarget;
	std::vector<ParamTrack>	fTracks;

	thread_id				fPlayThread;
	volatile bool			fPlayStop;
	bigtime_t				fPlayStart;

	bool					fRecording;
	bigtime_t				fRecordStart;

	static void _RecordHook(int32 id, bigtime_t when, const void* value,
		size_t size, void* cookie);
};


BMediaAutomation::Impl::Impl(BControllable* target)
	:
	fTarget(target),
	fPlayThread(-1),
	fPlayStop(false),
	fPlayStart(0),
	fRecording(false),
	fRecordStart(0)
{
}


BMediaAutomation::Impl::~Impl()
{
	StopPlayback();
}


ParamTrack*
BMediaAutomation::Impl::_FindOrAdd(int32 paramId)
{
	for (auto& t : fTracks) {
		if (t.id == paramId)
			return &t;
	}
	ParamTrack nt;
	nt.id     = paramId;
	nt.interp = BMediaAutomation::B_AUTOMATION_LINEAR;
	fTracks.push_back(nt);
	return &fTracks.back();
}


const ParamTrack*
BMediaAutomation::Impl::_Find(int32 paramId) const
{
	for (auto& t : fTracks) {
		if (t.id == paramId)
			return &t;
	}
	return NULL;
}


status_t
BMediaAutomation::Impl::AddPoint(int32 paramId, bigtime_t time, float value)
{
	ParamTrack* t = _FindOrAdd(paramId);
	if (t == NULL)
		return B_NO_MEMORY;
	// Insertion sort by time. Replace if a point already exists at `time`.
	auto it = t->points.begin();
	while (it != t->points.end() && it->time < time) ++it;
	if (it != t->points.end() && it->time == time) {
		it->value = value;
	} else {
		AutoPoint p; p.time = time; p.value = value;
		t->points.insert(it, p);
	}
	return B_OK;
}


status_t
BMediaAutomation::Impl::RemovePoint(int32 paramId, bigtime_t time)
{
	for (auto& t : fTracks) {
		if (t.id != paramId)
			continue;
		for (auto it = t.points.begin(); it != t.points.end(); ++it) {
			if (it->time == time) {
				t.points.erase(it);
				return B_OK;
			}
		}
		return B_ENTRY_NOT_FOUND;
	}
	return B_ENTRY_NOT_FOUND;
}


status_t
BMediaAutomation::Impl::ClearParameter(int32 paramId)
{
	for (auto it = fTracks.begin(); it != fTracks.end(); ++it) {
		if (it->id == paramId) {
			fTracks.erase(it);
			return B_OK;
		}
	}
	return B_ENTRY_NOT_FOUND;
}


void
BMediaAutomation::Impl::ClearAll()
{
	fTracks.clear();
}


int32
BMediaAutomation::Impl::ParameterIdAt(int32 i) const
{
	if (i < 0 || (size_t)i >= fTracks.size())
		return 0;
	return fTracks[i].id;
}


int32
BMediaAutomation::Impl::CountPoints(int32 paramId) const
{
	const ParamTrack* t = _Find(paramId);
	return t != NULL ? (int32)t->points.size() : 0;
}


status_t
BMediaAutomation::Impl::GetPointAt(int32 paramId, int32 index, bigtime_t* time,
	float* value) const
{
	const ParamTrack* t = _Find(paramId);
	if (t == NULL || index < 0 || (size_t)index >= t->points.size())
		return B_BAD_INDEX;
	if (time  != NULL) *time  = t->points[index].time;
	if (value != NULL) *value = t->points[index].value;
	return B_OK;
}


float
BMediaAutomation::Impl::ValueAt(int32 paramId, bigtime_t time) const
{
	const ParamTrack* t = _Find(paramId);
	if (t == NULL || t->points.empty())
		return 0.0f;

	// Before first or after last point: clamp.
	if (time <= t->points.front().time)
		return t->points.front().value;
	if (time >= t->points.back().time)
		return t->points.back().value;

	// Find the bracketing pair (b earlier or equal, c later).
	const AutoPoint* b = NULL;
	const AutoPoint* c = NULL;
	for (size_t i = 0; i < t->points.size(); i++) {
		if (t->points[i].time <= time)
			b = &t->points[i];
		if (t->points[i].time > time && c == NULL)
			c = &t->points[i];
	}
	if (b == NULL || c == NULL || c == b)
		return b != NULL ? b->value : 0.0f;

	switch (t->interp) {
		case BMediaAutomation::B_AUTOMATION_STEP:
			return b->value;
		case BMediaAutomation::B_AUTOMATION_SMOOTH: {
			// Cubic ease (smoothstep).
			const float u = (float)(time - b->time) / (float)(c->time - b->time);
			const float s = u * u * (3.0f - 2.0f * u);
			return b->value + (c->value - b->value) * s;
		}
		case BMediaAutomation::B_AUTOMATION_LINEAR:
		default: {
			const float u = (float)(time - b->time) / (float)(c->time - b->time);
			return b->value + (c->value - b->value) * u;
		}
	}
}


void
BMediaAutomation::Impl::SetInterp(int32 paramId,
	BMediaAutomation::interpolation_mode mode)
{
	ParamTrack* t = _FindOrAdd(paramId);
	if (t != NULL)
		t->interp = mode;
}


BMediaAutomation::interpolation_mode
BMediaAutomation::Impl::Interp(int32 paramId) const
{
	const ParamTrack* t = _Find(paramId);
	return t != NULL ? t->interp : BMediaAutomation::B_AUTOMATION_LINEAR;
}


// #pragma mark - playback


int32
BMediaAutomation::Impl::_PlayThunk(void* arg)
{
	((Impl*)arg)->_PlayLoop();
	return B_OK;
}


void
BMediaAutomation::Impl::_PlayLoop()
{
	if (fTarget == NULL || fTracks.empty())
		return;

	const bigtime_t startWall = system_time();
	const bigtime_t firstMedia = fPlayStart;

	// Tick at 100Hz, push each param's current ValueAt to the target.
	while (!fPlayStop) {
		const bigtime_t now = firstMedia + (system_time() - startWall);
		for (auto& t : fTracks) {
			const float v = ValueAt(t.id, now);
			fTarget->SetParameterValue(t.id, now, &v, sizeof(v));
			fTarget->BroadcastChangedParameter(t.id);
		}
		// Check if past last point on every track → stop.
		bool anyPending = false;
		for (auto& t : fTracks) {
			if (!t.points.empty() && now < t.points.back().time) {
				anyPending = true;
				break;
			}
		}
		if (!anyPending)
			break;
		snooze(10000);	// 10ms
	}
	fPlayThread = -1;
}


status_t
BMediaAutomation::Impl::StartPlayback(bigtime_t startTime)
{
	if (fPlayThread > 0)
		return B_OK;
	fPlayStop  = false;
	fPlayStart = startTime;
	fPlayThread = spawn_thread(&Impl::_PlayThunk, "automation play",
		B_NORMAL_PRIORITY, this);
	if (fPlayThread < B_OK) {
		thread_id err = fPlayThread;
		fPlayThread = -1;
		return err;
	}
	resume_thread(fPlayThread);
	return B_OK;
}


status_t
BMediaAutomation::Impl::StopPlayback()
{
	if (fPlayThread <= 0)
		return B_OK;
	fPlayStop = true;
	status_t exitVal;
	wait_for_thread(fPlayThread, &exitVal);
	fPlayThread = -1;
	return B_OK;
}


// #pragma mark - Flatten/Unflatten


void
BMediaAutomation::Impl::PackToMessage(BMessage& out) const
{
	for (auto& t : fTracks) {
		BMessage tm;
		tm.AddInt32("id",     t.id);
		tm.AddInt32("interp", (int32)t.interp);
		for (auto& p : t.points) {
			tm.AddInt64("time",  p.time);
			tm.AddFloat("value", p.value);
		}
		out.AddMessage("track", &tm);
	}
}


status_t
BMediaAutomation::Impl::UnpackFromMessage(const BMessage& in)
{
	fTracks.clear();
	BMessage tm;
	for (int32 i = 0; in.FindMessage("track", i, &tm) == B_OK; i++) {
		int32 id = 0, interp = (int32)BMediaAutomation::B_AUTOMATION_LINEAR;
		tm.FindInt32("id",     &id);
		tm.FindInt32("interp", &interp);
		ParamTrack* t = _FindOrAdd(id);
		if (t == NULL)
			continue;
		t->interp = (BMediaAutomation::interpolation_mode)interp;
		bigtime_t time;
		float value;
		for (int32 j = 0;
				tm.FindInt64("time",  j, &time)  == B_OK
				&& tm.FindFloat("value", j, &value) == B_OK; j++) {
			AutoPoint p; p.time = time; p.value = value;
			t->points.push_back(p);
		}
	}
	return B_OK;
}


// #pragma mark - public surface


BMediaAutomation::BMediaAutomation(BControllable* target)
	:
	fImpl(new(std::nothrow) Impl(target))
{
}


BMediaAutomation::~BMediaAutomation()
{
	delete fImpl;
}


status_t  BMediaAutomation::StartRecording()  { return fImpl ? fImpl->StartRecording() : B_NO_INIT; }
status_t  BMediaAutomation::StopRecording()   { return fImpl ? fImpl->StopRecording()  : B_NO_INIT; }
bool      BMediaAutomation::IsRecording() const { return fImpl && fImpl->IsRecording(); }


void
BMediaAutomation::Impl::_RecordHook(int32 id, bigtime_t when, const void* value,
	size_t size, void* cookie)
{
	Impl* self = (Impl*)cookie;
	if (self == NULL || !self->fRecording || value == NULL || size < sizeof(float))
		return;
	const float v = *(const float*)value;
	const bigtime_t rel = when - self->fRecordStart;
	self->AddPoint(id, rel, v);
}


status_t
BMediaAutomation::Impl::StartRecording()
{
	if (fTarget == NULL)
		return B_NO_INIT;
	if (fRecording)
		return B_OK;
	fRecording   = true;
	fRecordStart = system_time();
	fTarget->SetChangeHook(&Impl::_RecordHook, this);
	return B_OK;
}


status_t
BMediaAutomation::Impl::StopRecording()
{
	if (!fRecording)
		return B_OK;
	if (fTarget != NULL)
		fTarget->SetChangeHook(NULL, NULL);
	fRecording = false;
	return B_OK;
}

status_t  BMediaAutomation::StartPlayback(bigtime_t t) { return fImpl ? fImpl->StartPlayback(t) : B_NO_INIT; }
status_t  BMediaAutomation::StopPlayback()             { return fImpl ? fImpl->StopPlayback()  : B_OK; }
bool      BMediaAutomation::IsPlaying() const          { return fImpl && fImpl->IsPlaying(); }

status_t
BMediaAutomation::AddPoint(int32 paramId, bigtime_t time, float value)
{
	return fImpl ? fImpl->AddPoint(paramId, time, value) : B_NO_INIT;
}

status_t
BMediaAutomation::AddPoint(int32 paramId, bigtime_t time, int32 value)
{
	return AddPoint(paramId, time, (float)value);
}

status_t  BMediaAutomation::RemovePoint(int32 id, bigtime_t t) { return fImpl ? fImpl->RemovePoint(id, t) : B_NO_INIT; }
status_t  BMediaAutomation::ClearParameter(int32 id) { return fImpl ? fImpl->ClearParameter(id) : B_NO_INIT; }
status_t  BMediaAutomation::ClearAll() { if (fImpl) fImpl->ClearAll(); return B_OK; }

int32     BMediaAutomation::CountParameters() const { return fImpl ? fImpl->CountParameters() : 0; }
int32     BMediaAutomation::ParameterIdAt(int32 i) const { return fImpl ? fImpl->ParameterIdAt(i) : 0; }
int32     BMediaAutomation::CountPoints(int32 id) const  { return fImpl ? fImpl->CountPoints(id) : 0; }

status_t
BMediaAutomation::GetPointAt(int32 paramId, int32 index, bigtime_t* time,
	float* value) const
{
	return fImpl ? fImpl->GetPointAt(paramId, index, time, value) : B_NO_INIT;
}

float     BMediaAutomation::ValueAt(int32 id, bigtime_t t) const { return fImpl ? fImpl->ValueAt(id, t) : 0.0f; }

void
BMediaAutomation::SetInterpolation(int32 paramId, interpolation_mode mode)
{
	if (fImpl) fImpl->SetInterp(paramId, mode);
}

BMediaAutomation::interpolation_mode
BMediaAutomation::Interpolation(int32 paramId) const
{
	return fImpl ? fImpl->Interp(paramId) : B_AUTOMATION_LINEAR;
}


bool       BMediaAutomation::IsFixedSize() const { return false; }
type_code  BMediaAutomation::TypeCode() const    { return 'auto'; }

bool
BMediaAutomation::AllowsTypeCode(type_code code) const
{
	return code == TypeCode();
}

ssize_t
BMediaAutomation::FlattenedSize() const
{
	BMessage m;
	if (fImpl != NULL) fImpl->PackToMessage(m);
	return m.FlattenedSize();
}

status_t
BMediaAutomation::Flatten(void* buffer, ssize_t size) const
{
	BMessage m;
	if (fImpl != NULL) fImpl->PackToMessage(m);
	return m.Flatten((char*)buffer, size);
}

status_t
BMediaAutomation::Unflatten(type_code code, const void* buffer, ssize_t /*size*/)
{
	if (!AllowsTypeCode(code))
		return B_BAD_TYPE;
	if (fImpl == NULL)
		return B_NO_INIT;
	BMessage m;
	status_t err = m.Unflatten((const char*)buffer);
	if (err != B_OK)
		return err;
	return fImpl->UnpackFromMessage(m);
}
