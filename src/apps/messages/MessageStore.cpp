/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MessageStore.h"

#include <Directory.h>
#include <File.h>
#include <Path.h>

#include <stdio.h>
#include <string.h>

#include <FindDirectory.h>


static const uint32 kStoreMagic = 'MSGS';


MessageStore::MessageStore()
{
	Load();
}


SmsThread*
MessageStore::ThreadAt(int32 index) const
{
	return fThreads.ItemAt(index);
}


SmsThread*
MessageStore::FindThread(const char* number) const
{
	if (number == NULL)
		return NULL;

	for (int32 i = 0; i < fThreads.CountItems(); i++) {
		SmsThread* thread = fThreads.ItemAt(i);
		if (thread->number == number)
			return thread;
	}
	return NULL;
}


SmsThread*
MessageStore::FindOrAddThread(const char* number)
{
	SmsThread* thread = FindThread(number);
	if (thread != NULL)
		return thread;

	thread = new SmsThread;
	thread->number = number != NULL ? number : "";
	fThreads.AddItem(thread, 0);
	return thread;
}


void
MessageStore::AddIncoming(const char* number, const char* text)
{
	SmsThread* thread = FindOrAddThread(number);

	StoredSms* sms = new StoredSms;
	sms->number = number != NULL ? number : "";
	sms->text = text != NULL ? text : "";
	sms->direction = "in";
	sms->state = "received";
	sms->timestamp = time(NULL);
	thread->messages.AddItem(sms);

	Save();
}


StoredSms*
MessageStore::AddOutgoing(const char* number, const char* text)
{
	SmsThread* thread = FindOrAddThread(number);

	StoredSms* sms = new StoredSms;
	sms->number = number != NULL ? number : "";
	sms->text = text != NULL ? text : "";
	sms->direction = "out";
	sms->state = "sending";
	sms->timestamp = time(NULL);
	thread->messages.AddItem(sms);

	Save();
	return sms;
}


void
MessageStore::UpdateLastOutgoingState(const char* number, const char* state)
{
	SmsThread* thread = FindThread(number);
	if (thread == NULL)
		return;

	for (int32 i = thread->messages.CountItems() - 1; i >= 0; i--) {
		StoredSms* sms = thread->messages.ItemAt(i);
		if (strcmp(sms->direction, "out") == 0) {
			sms->state = state;
			break;
		}
	}
	Save();
}


void
MessageStore::RemoveThread(const char* number)
{
	SmsThread* thread = FindThread(number);
	if (thread == NULL)
		return;

	fThreads.RemoveItem(thread);
	delete thread;
	Save();
}


void
MessageStore::Load()
{
	fThreads.MakeEmpty();

	BPath path;
	if (find_directory(B_USER_CONFIG_DIRECTORY, &path) != B_OK)
		return;
	BString fullPath(path.Path());
	fullPath << "/settings/Messages/store";

	BFile file(fullPath.String(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BMessage archive;
	if (archive.Unflatten(&file) != B_OK || archive.what != kStoreMagic)
		return;

	int32 threadCount = 0;
	archive.FindInt32("threads", &threadCount);
	for (int32 i = 0; i < threadCount; i++) {
		char threadName[32];
		snprintf(threadName, sizeof(threadName), "thread_%" B_PRId32, i);
		BMessage threadArchive;
		if (archive.FindMessage(threadName, &threadArchive) != B_OK)
			continue;

		SmsThread* thread = new SmsThread;
		threadArchive.FindString("number", &thread->number);

		int32 messageCount = 0;
		threadArchive.FindInt32("messages", &messageCount);
		for (int32 m = 0; m < messageCount; m++) {
			char messageName[32];
			snprintf(messageName, sizeof(messageName),
				"message_%" B_PRId32, m);
			BMessage entry;
			if (threadArchive.FindMessage(messageName, &entry)
					!= B_OK)
				continue;

			StoredSms* sms = new StoredSms;
			entry.FindString("number", &sms->number);
			entry.FindString("text", &sms->text);
			BString direction;
			entry.FindString("direction", &direction);
			sms->direction = strcmp(direction.String(), "in") == 0
				? "in" : "out";
			BString state;
			entry.FindString("state", &state);
			if (state == "sent")
				sms->state = "sent";
			else if (state == "failed")
				sms->state = "failed";
			else if (state == "sending")
				sms->state = "sending";
			else
				sms->state = "received";
			int64 stamp = 0;
			entry.FindInt64("timestamp", &stamp);
			sms->timestamp = (time_t)stamp;
			thread->messages.AddItem(sms);
		}
		fThreads.AddItem(thread);
	}
}


void
MessageStore::Save()
{
	BPath path;
	if (find_directory(B_USER_CONFIG_DIRECTORY, &path) != B_OK)
		return;

	BString dirPath(path.Path());
	dirPath << "/settings/Messages";
	create_directory(dirPath.String(), 0755);

	BString fullPath(dirPath.String());
	fullPath << "/store";

	BMessage archive(kStoreMagic);
	archive.AddInt32("threads", fThreads.CountItems());
	for (int32 i = 0; i < fThreads.CountItems(); i++) {
		SmsThread* thread = fThreads.ItemAt(i);

		BMessage threadArchive;
		threadArchive.AddString("number", thread->number);
		threadArchive.AddInt32("messages", thread->messages.CountItems());
		for (int32 m = 0; m < thread->messages.CountItems(); m++) {
			StoredSms* sms = thread->messages.ItemAt(m);
			BMessage entry;
			entry.AddString("number", sms->number);
			entry.AddString("text", sms->text);
			entry.AddString("direction", sms->direction);
			entry.AddString("state", sms->state);
			entry.AddInt64("timestamp", (int64)sms->timestamp);
			char messageName[32];
			snprintf(messageName, sizeof(messageName),
				"message_%" B_PRId32, m);
			threadArchive.AddMessage(messageName, &entry);
		}

		char threadName[32];
		snprintf(threadName, sizeof(threadName), "thread_%" B_PRId32, i);
		archive.AddMessage(threadName, &threadArchive);
	}

	BFile file(fullPath.String(), B_WRITE_ONLY | B_CREATE_FILE
		| B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return;
	archive.Flatten(&file);
}
