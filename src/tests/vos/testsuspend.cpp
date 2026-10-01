/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * testsuspend: checks that loopers, ports, semaphores and threads keep
 * working across system suspend.
 *
 *   testsuspend [seconds]
 *
 * Start it, then suspend and resume the machine a few times, e.g.
 *
 *   sudo rtcwake -m no -s 20 && systemctl suspend
 *
 * Each worker repeats one blocking pattern and checks every result:
 * nothing lost or duplicated, timeouts end in B_TIMED_OUT and never in
 * B_INTERRUPTED, no message runner burst or stall after resume. Signals
 * with a handler hit the process all along, as SIGCHLD would. Other
 * threads stay parked in every kind of wait (a semaphore, a port,
 * receive_data, a thread never resumed) and must still be there, then end
 * normally when released at exit. Suspends are detected here
 * (CLOCK_BOOTTIME runs on, CLOCK_MONOTONIC stops). Runs 600 seconds by
 * default; exits 1 if any check failed, 77 if no suspend happened.
 *
 * The parked threads also cover systemd's cgroup freezer, which must stop
 * every one of them without a suspend:
 *
 *   systemd-run --user --unit=testsuspend testsuspend 60
 *   systemctl --user freeze testsuspend   # must return within seconds
 *   systemctl --user thaw testsuspend
 */


#include <Application.h>
#include <Autolock.h>
#include <Locker.h>
#include <Looper.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <OS.h>

#include <signal.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


enum {
	CHECK_PORT_STREAM,
	CHECK_PORT_TIMEOUT,
	CHECK_PORT_ABSOLUTE,
	CHECK_SEM_PINGPONG,
	CHECK_SEM_TIMEOUT,
	CHECK_THREAD_DATA,
	CHECK_WAIT_FOR_THREAD,
	CHECK_SNOOZE,
	CHECK_LOOPER_REPLY,
	CHECK_MESSAGE_RUNNER,
	CHECK_PARKED,
	CHECK_COUNT
};

static const char* kCheckNames[CHECK_COUNT] = {
	"port stream",
	"port timeout",
	"port absolute",
	"sem ping-pong",
	"sem timeout",
	"send/receive_data",
	"wait_for_thread",
	"snooze",
	"looper reply",
	"message runner",
	"parked waits"
};

static const bigtime_t kRunnerInterval = 100000;
static const int32 kMaxLogged = 50;

static int64 sPassed[CHECK_COUNT];
static int64 sFailed[CHECK_COUNT];
static int32 sStop;
static int32 sSuspends;
static int32 sLogged;
static BLocker sLogLock("testsuspend log");


static bigtime_t
clock_us(clockid_t clock)
{
	struct timespec ts;
	clock_gettime(clock, &ts);
	return (bigtime_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}


static bool
stopping()
{
	return atomic_get(&sStop) != 0;
}


static void
pass(int check)
{
	atomic_add64(&sPassed[check], 1);
}


static void
fail(int check, const char* format, ...)
{
	atomic_add64(&sFailed[check], 1);
	if (atomic_add(&sLogged, 1) >= kMaxLogged)
		return;

	char text[256];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);

	BAutolock _(sLogLock);
	printf("FAIL %-18s %s (after %" B_PRId32 " suspends)\n",
		kCheckNames[check], text, atomic_get(&sSuspends));
	fflush(stdout);
}


// A timed wait must time out, not return early or be interrupted, and
// must not run far past its timeout (CLOCK_MONOTONIC excludes suspend).
static void
check_timed_wait(int check, status_t status, bigtime_t start,
	bigtime_t timeout)
{
	bigtime_t elapsed = system_time() - start;
	if (status != B_TIMED_OUT)
		fail(check, "status %s (0x%" B_PRIx32 ")", strerror(status), status);
	else if (elapsed < timeout)
		fail(check, "returned after %" B_PRId64 " of %" B_PRId64 " us",
			elapsed, timeout);
	else if (elapsed > timeout + 2000000)
		fail(check, "returned %" B_PRId64 " us late", elapsed - timeout);
	else
		pass(check);
}


// #pragma mark - suspend detection


static status_t
suspend_watcher(void*)
{
	bigtime_t offset = clock_us(CLOCK_BOOTTIME) - clock_us(CLOCK_MONOTONIC);
	while (!stopping()) {
		snooze(100000);
		bigtime_t now = clock_us(CLOCK_BOOTTIME) - clock_us(CLOCK_MONOTONIC);
		if (now - offset > 500000) {
			int32 count = atomic_add(&sSuspends, 1) + 1;
			BAutolock _(sLogLock);
			printf("suspend #%" B_PRId32 " detected, slept %.1f s\n", count,
				(now - offset) / 1000000.0);
			fflush(stdout);
		}
		offset = now;
	}
	return B_OK;
}


// #pragma mark - ports


static port_id sStreamPort;


static status_t
port_writer(void*)
{
	for (int32 seq = 0; !stopping(); seq++) {
		status_t status = write_port(sStreamPort, seq, &seq, sizeof(seq));
		if (status != B_OK)
			fail(CHECK_PORT_STREAM, "write_port: %s", strerror(status));
	}
	int32 end = -1;
	write_port(sStreamPort, -1, &end, sizeof(end));
	return B_OK;
}


static status_t
port_reader(void*)
{
	for (int32 expected = 0;; expected++) {
		int32 code;
		int32 data = 0;
		ssize_t size = read_port(sStreamPort, &code, &data, sizeof(data));
		if (size < 0) {
			fail(CHECK_PORT_STREAM, "read_port: %s", strerror(size));
			expected--;
			snooze(100000);
			continue;
		}
		if (code == -1)
			return B_OK;
		if (code != expected || data != expected || size != sizeof(data)) {
			fail(CHECK_PORT_STREAM, "got %" B_PRId32 "/%" B_PRId32
				", expected %" B_PRId32, code, data, expected);
			expected = code;
		} else
			pass(CHECK_PORT_STREAM);
	}
}


static status_t
port_timeout(void*)
{
	port_id port = create_port(1, "testsuspend empty");
	while (!stopping()) {
		int32 code;
		bigtime_t start = system_time();
		ssize_t status = read_port_etc(port, &code, NULL, 0,
			B_RELATIVE_TIMEOUT, 200000);
		check_timed_wait(CHECK_PORT_TIMEOUT, status, start, 200000);
	}
	delete_port(port);
	return B_OK;
}


static status_t
port_absolute(void*)
{
	port_id port = create_port(1, "testsuspend absolute");
	while (!stopping()) {
		int32 code;
		bigtime_t start = system_time();
		ssize_t status = read_port_etc(port, &code, NULL, 0,
			B_ABSOLUTE_TIMEOUT, start + 150000);
		check_timed_wait(CHECK_PORT_ABSOLUTE, status, start, 150000);
	}
	delete_port(port);
	return B_OK;
}


// #pragma mark - signals


static int32 sSignals;


static void
count_signal(int)
{
	atomic_add(&sSignals, 1);
}


// Process-directed, so the kernel picks any thread: it lands in whatever
// wait that thread is in. No SA_RESTART, like most handlers.
static status_t
signaler(void*)
{
	struct sigaction action = {};
	action.sa_handler = count_signal;
	sigemptyset(&action.sa_mask);
	sigaction(SIGUSR1, &action, NULL);

	while (!stopping()) {
		kill(getpid(), SIGUSR1);
		snooze(20000);
	}
	return B_OK;
}


// #pragma mark - parked waits
// wait_for_thread() resumes a never-resumed thread (legacy semantics), so the waiter waits on the semaphore thread.


static sem_id sParkedSem;
static port_id sParkedPort;
static thread_id sParkedReceiver;
static thread_id sParkedSemThread;
static thread_id sNeverResumed;


static void
check_parked(const char* what, status_t status, status_t expected)
{
	if (!stopping())
		fail(CHECK_PARKED, "%s ended early: %s", what, strerror(status));
	else if (status != expected)
		fail(CHECK_PARKED, "%s: %s", what, strerror(status));
	else
		pass(CHECK_PARKED);
}


static status_t
parked_sem(void*)
{
	check_parked("acquire_sem", acquire_sem(sParkedSem), B_OK);
	return B_OK;
}


static status_t
parked_port(void*)
{
	int32 code;
	ssize_t size = read_port(sParkedPort, &code, NULL, 0);
	check_parked("read_port", size < 0 ? size : B_OK, B_OK);
	return B_OK;
}


static status_t
parked_receiver(void*)
{
	thread_id sender;
	int32 code = receive_data(&sender, NULL, 0);
	check_parked("receive_data", code == 7 ? B_OK : B_ERROR, B_OK);
	return B_OK;
}


static status_t
never_resumed(void*)
{
	if (!stopping())
		fail(CHECK_PARKED, "thread ran before resume_thread()");
	return 42;
}


static status_t
parked_waiter(void*)
{
	status_t exitValue = -1;
	status_t status = wait_for_thread(sParkedSemThread, &exitValue);
	check_parked("wait_for_thread", status == B_OK && exitValue == B_OK
		? B_OK : B_ERROR, B_OK);
	return B_OK;
}


static void
release_parked()
{
	release_sem(sParkedSem);
	write_port(sParkedPort, 0, NULL, 0);
	send_data(sParkedReceiver, 7, NULL, 0);
	resume_thread(sNeverResumed);
}


// #pragma mark - semaphores


static sem_id sPing;
static sem_id sPong;


static status_t
sem_ponger(void*)
{
	while (true) {
		status_t status = acquire_sem(sPing);
		if (status == B_BAD_SEM_ID && stopping())
			return B_OK;
		if (status != B_OK) {
			fail(CHECK_SEM_PINGPONG, "pong acquire: %s", strerror(status));
			continue;
		}
		release_sem(sPong);
	}
}


static status_t
sem_pinger(void*)
{
	while (!stopping()) {
		release_sem(sPing);
		status_t status = acquire_sem(sPong);
		if (status != B_OK)
			fail(CHECK_SEM_PINGPONG, "ping acquire: %s", strerror(status));
		else
			pass(CHECK_SEM_PINGPONG);
	}
	return B_OK;
}


static status_t
sem_timeout(void*)
{
	sem_id sem = create_sem(0, "testsuspend never");
	while (!stopping()) {
		bigtime_t start = system_time();
		status_t status = acquire_sem_etc(sem, 1, B_RELATIVE_TIMEOUT, 100000);
		check_timed_wait(CHECK_SEM_TIMEOUT, status, start, 100000);
	}
	delete_sem(sem);
	return B_OK;
}


// #pragma mark - threads


static status_t
data_receiver(void*)
{
	for (int32 expected = 0;; expected++) {
		thread_id sender;
		int32 data = 0;
		int32 code = receive_data(&sender, &data, sizeof(data));
		if (code == -1)
			return B_OK;
		if (code != expected || data != expected) {
			fail(CHECK_THREAD_DATA, "got %" B_PRId32 "/%" B_PRId32
				", expected %" B_PRId32, code, data, expected);
			expected = code;
		} else
			pass(CHECK_THREAD_DATA);
	}
}


static status_t
data_sender(void* cookie)
{
	thread_id receiver = (thread_id)(addr_t)cookie;
	for (int32 seq = 0; !stopping(); seq++) {
		status_t status = send_data(receiver, seq, &seq, sizeof(seq));
		if (status != B_OK)
			fail(CHECK_THREAD_DATA, "send_data: %s", strerror(status));
	}
	int32 end = -1;
	send_data(receiver, -1, &end, sizeof(end));
	return B_OK;
}


static status_t
short_lived(void* cookie)
{
	snooze(10000);
	return (status_t)(addr_t)cookie;
}


static status_t
thread_spawner(void*)
{
	for (int32 seq = 1; !stopping(); seq++) {
		thread_id thread = spawn_thread(short_lived, "testsuspend child",
			B_NORMAL_PRIORITY, (void*)(addr_t)seq);
		if (thread < 0) {
			fail(CHECK_WAIT_FOR_THREAD, "spawn_thread: %s", strerror(thread));
			snooze(100000);
			continue;
		}
		resume_thread(thread);
		status_t exitValue = -1;
		status_t status = wait_for_thread(thread, &exitValue);
		if (status != B_OK || exitValue != seq) {
			fail(CHECK_WAIT_FOR_THREAD, "status %s, exit %" B_PRId32
				" expected %" B_PRId32, strerror(status), exitValue, seq);
		} else
			pass(CHECK_WAIT_FOR_THREAD);
	}
	return B_OK;
}


static status_t
snoozer(void*)
{
	while (!stopping()) {
		bigtime_t start = system_time();
		status_t status = snooze(50000);
		bigtime_t elapsed = system_time() - start;
		if (status == B_INTERRUPTED)
			pass(CHECK_SNOOZE);	// a signal handler ran: Haiku semantics
		else if (status != B_OK)
			fail(CHECK_SNOOZE, "status %s", strerror(status));
		else if (elapsed < 50000)
			fail(CHECK_SNOOZE, "woke after %" B_PRId64 " us", elapsed);
		else
			pass(CHECK_SNOOZE);
	}
	return B_OK;
}


// #pragma mark - loopers


class EchoLooper : public BLooper {
public:
	EchoLooper()
		:
		BLooper("testsuspend echo")
	{
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != 'echo') {
			BLooper::MessageReceived(message);
			return;
		}
		BMessage reply('repl');
		reply.AddInt32("seq", message->GetInt32("seq", -1));
		message->SendReply(&reply);
	}
};


static status_t
looper_driver(void* cookie)
{
	BMessenger echo((BLooper*)cookie);
	for (int32 seq = 0; !stopping(); seq++) {
		BMessage message('echo');
		message.AddInt32("seq", seq);
		BMessage reply;
		status_t status = echo.SendMessage(&message, &reply);
		if (status != B_OK)
			fail(CHECK_LOOPER_REPLY, "SendMessage: %s", strerror(status));
		else if (reply.what != 'repl' || reply.GetInt32("seq", -1) != seq) {
			fail(CHECK_LOOPER_REPLY, "reply 0x%" B_PRIx32 " seq %" B_PRId32
				", expected %" B_PRId32, reply.what,
				reply.GetInt32("seq", -1), seq);
		} else
			pass(CHECK_LOOPER_REPLY);
	}
	return B_OK;
}


// #pragma mark - application


class TestApp : public BApplication {
public:
	TestApp(bigtime_t duration)
		:
		BApplication("application/x-vnd.vos-testsuspend"),
		fDuration(duration),
		fRunner(NULL),
		fLastTick(0),
		fLastTickSuspends(0)
	{
	}

	virtual void ReadyToRun()
	{
		fRunner = new BMessageRunner(be_app_messenger, BMessage('tick'),
			kRunnerInterval);
		if (fRunner->InitCheck() != B_OK)
			fail(CHECK_MESSAGE_RUNNER, "BMessageRunner: %s",
				strerror(fRunner->InitCheck()));
		BMessageRunner::StartSending(be_app_messenger, new BMessage('stop'),
			fDuration, 1);
		BMessageRunner::StartSending(be_app_messenger, new BMessage('stat'),
			10000000, -1);
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case 'tick':
				_Tick();
				break;
			case 'stat':
				_PrintTotals("status");
				break;
			case 'stop':
				delete fRunner;
				fRunner = NULL;
				Quit();
				break;
			default:
				BApplication::MessageReceived(message);
		}
	}

private:
	// After resume a runner must neither fire its missed ticks in a burst
	// nor stall; a gap that spans a suspend is expected.
	void _Tick()
	{
		bigtime_t now = system_time();
		int32 suspends = atomic_get(&sSuspends);
		if (fLastTick != 0) {
			bigtime_t gap = now - fLastTick;
			if (gap < kRunnerInterval / 4)
				fail(CHECK_MESSAGE_RUNNER, "burst: tick %" B_PRId64
					" us after the previous", gap);
			else if (gap > kRunnerInterval * 10
				&& suspends == fLastTickSuspends)
				fail(CHECK_MESSAGE_RUNNER, "stalled %" B_PRId64 " us", gap);
			else
				pass(CHECK_MESSAGE_RUNNER);
		}
		fLastTick = now;
		fLastTickSuspends = suspends;
	}

public:
	static void _PrintTotals(const char* label)
	{
		BAutolock _(sLogLock);
		printf("%s, %" B_PRId32 " suspends:", label, atomic_get(&sSuspends));
		for (int i = 0; i < CHECK_COUNT; i++) {
			printf(" %s %" B_PRId64 "/%" B_PRId64 ";", kCheckNames[i],
				atomic_get64(&sPassed[i]),
				atomic_get64(&sPassed[i]) + atomic_get64(&sFailed[i]));
		}
		printf("\n");
		fflush(stdout);
	}

private:
	bigtime_t		fDuration;
	BMessageRunner*	fRunner;
	bigtime_t		fLastTick;
	int32			fLastTickSuspends;
};


static thread_id
start(thread_func function, const char* name, void* cookie = NULL)
{
	thread_id thread = spawn_thread(function, name, B_NORMAL_PRIORITY,
		cookie);
	if (thread < 0) {
		fprintf(stderr, "spawn_thread(%s): %s\n", name, strerror(thread));
		exit(2);
	}
	resume_thread(thread);
	return thread;
}


int
main(int argc, char** argv)
{
	bigtime_t duration = 600 * 1000000LL;
	if (argc > 1)
		duration = atoll(argv[1]) * 1000000LL;

	TestApp app(duration);

	sStreamPort = create_port(4, "testsuspend stream");
	sPing = create_sem(0, "testsuspend ping");
	sPong = create_sem(0, "testsuspend pong");

	EchoLooper* echo = new EchoLooper;
	echo->Run();

	sParkedSem = create_sem(0, "testsuspend parked");
	sParkedPort = create_port(1, "testsuspend parked");
	sParkedReceiver = start(parked_receiver, "parked receiver");
	sParkedSemThread = start(parked_sem, "parked sem");
	sNeverResumed = spawn_thread(never_resumed, "never resumed",
		B_NORMAL_PRIORITY, NULL);

	thread_id receiver = start(data_receiver, "data receiver");
	thread_id threads[] = {
		start(suspend_watcher, "suspend watcher"),
		start(port_reader, "port reader"),
		start(port_writer, "port writer"),
		start(port_timeout, "port timeout"),
		start(port_absolute, "port absolute"),
		start(signaler, "signaler"),
		sParkedSemThread,
		sNeverResumed,
		start(parked_port, "parked port"),
		start(parked_waiter, "parked waiter"),
		sParkedReceiver,
		start(sem_pinger, "sem pinger"),
		start(sem_timeout, "sem timeout"),
		start(data_sender, "data sender", (void*)(addr_t)receiver),
		start(thread_spawner, "thread spawner"),
		start(snoozer, "snoozer"),
		start(looper_driver, "looper driver", echo),
		receiver
	};
	thread_id ponger = start(sem_ponger, "sem ponger");

	printf("testsuspend: running %" B_PRId64 " s; suspend and resume now\n",
		duration / 1000000);
	fflush(stdout);

	app.Run();

	atomic_set(&sStop, 1);
	release_parked();
	for (size_t i = 0; i < sizeof(threads) / sizeof(threads[0]); i++) {
		status_t exitValue;
		wait_for_thread(threads[i], &exitValue);
	}
	delete_sem(sPing);
	delete_sem(sPong);
	status_t exitValue;
	wait_for_thread(ponger, &exitValue);

	echo->Lock();
	echo->Quit();
	delete_port(sStreamPort);

	TestApp::_PrintTotals("done");
	printf("%" B_PRId32 " signals delivered\n", atomic_get(&sSignals));
	int64 failed = 0;
	for (int i = 0; i < CHECK_COUNT; i++)
		failed += sFailed[i];
	if (failed != 0) {
		printf("FAIL\n");
		return 1;
	}
	if (atomic_get(&sSuspends) == 0) {
		// The automake "skipped" status: nothing was tested.
		printf("SKIP: no suspend detected\n");
		return 77;
	}
	printf("PASS\n");
	return 0;
}
