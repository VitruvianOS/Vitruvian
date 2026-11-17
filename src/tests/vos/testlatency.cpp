/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * testlatency: round-trip latency of ports, semaphores and BMessage replies.
 *
 *   testlatency [iterations]
 */


#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

#include <Looper.h>
#include <Handler.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>


static int32 sIterations = 20000;
static const int32 kWarmup = 500;
static const int32 kQuit = 'quit';


static void
Report(const char* name, std::vector<bigtime_t>& samples)
{
	std::sort(samples.begin(), samples.end());
	size_t n = samples.size();
	printf("%-28s median %6lld us  p99 %6lld us  max %6lld us  (n=%zu)\n",
		name, (long long)samples[n / 2], (long long)samples[n * 99 / 100],
		(long long)samples[n - 1], n);
	fflush(stdout);
}


// Echoes every message on `in` back to `out` until kQuit.
static void
EchoLoop(port_id in, port_id out)
{
	char buffer[16];
	int32 code;
	for (;;) {
		ssize_t size = read_port(in, &code, buffer, sizeof(buffer));
		if (size < 0 || code == kQuit)
			return;
		write_port(out, code, buffer, size);
	}
}


static int32
EchoThread(void* data)
{
	port_id* ports = (port_id*)data;
	EchoLoop(ports[0], ports[1]);
	return 0;
}


static void
PingPort(port_id out, port_id in, std::vector<bigtime_t>& samples)
{
	char buffer[16] = {};
	int32 code;
	for (int32 i = 0; i < sIterations + kWarmup; i++) {
		bigtime_t start = system_time();
		write_port(out, 'ping', buffer, sizeof(buffer));
		read_port(in, &code, buffer, sizeof(buffer));
		bigtime_t took = system_time() - start;
		if (i >= kWarmup)
			samples.push_back(took);
	}
	write_port(out, kQuit, NULL, 0);
}


static void
PortThreads()
{
	port_id ports[2] = { create_port(4, "lat a"), create_port(4, "lat b") };
	thread_id echo = spawn_thread(EchoThread, "lat echo", B_NORMAL_PRIORITY,
		ports);
	resume_thread(echo);

	std::vector<bigtime_t> samples;
	PingPort(ports[0], ports[1], samples);
	status_t status;
	wait_for_thread(echo, &status);
	delete_port(ports[0]);
	delete_port(ports[1]);
	Report("port threads", samples);
}


static void
PortProcesses()
{
	port_id a = create_port(4, "lat pa");
	port_id b = create_port(4, "lat pb");

	pid_t child = fork();
	if (child == 0) {
		EchoLoop(a, b);
		_exit(0);
	}

	std::vector<bigtime_t> samples;
	PingPort(a, b, samples);
	int status;
	waitpid(child, &status, 0);
	delete_port(a);
	delete_port(b);
	Report("port processes", samples);
}


struct SemPair {
	sem_id	ping;
	sem_id	pong;
	bool	quit;
};


static int32
SemEchoThread(void* data)
{
	SemPair* pair = (SemPair*)data;
	for (;;) {
		acquire_sem(pair->ping);
		if (pair->quit)
			return 0;
		release_sem(pair->pong);
	}
}


static void
SemThreads()
{
	SemPair pair = { create_sem(0, "lat ping"), create_sem(0, "lat pong"),
		false };
	thread_id echo = spawn_thread(SemEchoThread, "lat sem", B_NORMAL_PRIORITY,
		&pair);
	resume_thread(echo);

	std::vector<bigtime_t> samples;
	for (int32 i = 0; i < sIterations + kWarmup; i++) {
		bigtime_t start = system_time();
		release_sem(pair.ping);
		acquire_sem(pair.pong);
		bigtime_t took = system_time() - start;
		if (i >= kWarmup)
			samples.push_back(took);
	}
	pair.quit = true;
	release_sem(pair.ping);
	status_t status;
	wait_for_thread(echo, &status);
	delete_sem(pair.ping);
	delete_sem(pair.pong);
	Report("semaphore threads", samples);
}


class EchoHandler : public BHandler {
public:
	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == 'ping') {
			BMessage reply('pong');
			message->SendReply(&reply);
		} else
			BHandler::MessageReceived(message);
	}
};


static void
MessageReply()
{
	BLooper* server = new BLooper("lat server");
	EchoHandler* handler = new EchoHandler;
	server->Lock();
	server->AddHandler(handler);
	server->Unlock();
	server->Run();

	BLooper* client = new BLooper("lat client");
	client->Run();
	BMessenger messenger(handler, server);

	// Sent from a plain thread, like a client of a looper.
	std::vector<bigtime_t> samples;
	for (int32 i = 0; i < sIterations + kWarmup; i++) {
		BMessage message('ping');
		BMessage reply;
		bigtime_t start = system_time();
		messenger.SendMessage(&message, &reply);
		bigtime_t took = system_time() - start;
		if (i >= kWarmup)
			samples.push_back(took);
	}

	client->Lock();
	client->Quit();
	server->Lock();
	server->Quit();
	Report("BMessage reply loopers", samples);
}


int
main(int argc, char** argv)
{
	if (argc > 1)
		sIterations = atoi(argv[1]);
	if (sIterations < 100)
		sIterations = 100;

	// Fork first, before any looper threads exist.
	PortProcesses();
	PortThreads();
	SemThreads();
	MessageReply();
	return 0;
}
