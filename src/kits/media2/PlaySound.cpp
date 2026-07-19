/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <PlaySound.h>

#include <map>
#include <mutex>
#include <new>

#include <OS.h>

#include <media2/MediaPlayer.h>


struct SoundJob {
	thread_id		thread;
	sem_id			doneSem;
	BMediaPlayer*	player;
};


static std::mutex                       sJobsLock;
static std::map<sound_handle, SoundJob> sJobs;


static int32
PlaySoundThread(void* arg)
{
	sound_handle handle = (sound_handle)(addr_t)arg;

	BMediaPlayer* player = NULL;
	sem_id done = -1;
	{
		std::lock_guard<std::mutex> _(sJobsLock);
		auto it = sJobs.find(handle);
		if (it == sJobs.end())
			return B_ERROR;
		player = it->second.player;
		done   = it->second.doneSem;
	}

	if (player != NULL) {
		player->Play();
		while (player->IsPlaying())
			snooze(50000);	// poll until EOS (BMediaPlayer flips State() to STOPPED)
	}

	release_sem(done);

	// Self-cleanup
	std::lock_guard<std::mutex> _(sJobsLock);
	auto it = sJobs.find(handle);
	if (it != sJobs.end()) {
		delete it->second.player;
		// doneSem is consumed by wait_for_sound; left for the waiter to delete.
		sJobs.erase(it);
	}
	return B_OK;
}


sound_handle
play_sound(const entry_ref* soundRef, bool /*mix*/, bool /*queue*/,
	bool /*background*/)
{
	if (soundRef == NULL)
		return B_BAD_VALUE;

	BMediaPlayer* player = new(std::nothrow) BMediaPlayer(soundRef);
	if (player == NULL)
		return B_NO_MEMORY;
	if (player->InitCheck() != B_OK) {
		delete player;
		return B_ERROR;
	}

	sem_id done = create_sem(0, "play_sound done");
	if (done < B_OK) {
		delete player;
		return done;
	}

	SoundJob job;
	job.doneSem = done;
	job.player  = player;
	job.thread  = -1;

	{
		std::lock_guard<std::mutex> _(sJobsLock);
		sJobs[done] = job;
	}

	thread_id th = spawn_thread(&PlaySoundThread, "play_sound",
		B_NORMAL_PRIORITY, (void*)(addr_t)done);
	if (th < B_OK) {
		std::lock_guard<std::mutex> _(sJobsLock);
		sJobs.erase(done);
		delete_sem(done);
		delete player;
		return th;
	}
	{
		std::lock_guard<std::mutex> _(sJobsLock);
		sJobs[done].thread = th;
	}
	resume_thread(th);
	return done;
}


status_t
stop_sound(sound_handle handle)
{
	std::lock_guard<std::mutex> _(sJobsLock);
	auto it = sJobs.find(handle);
	if (it == sJobs.end())
		return B_BAD_VALUE;
	if (it->second.player != NULL)
		it->second.player->Stop();
	// Worker thread will release the sem and clean up.
	return B_OK;
}


status_t
wait_for_sound(sound_handle handle)
{
	status_t err = acquire_sem(handle);
	delete_sem(handle);
	return err;
}
