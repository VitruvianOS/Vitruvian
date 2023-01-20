/*
 * Copyright 2019-2021, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _LIBROOT2_PORT
#define _LIBROOT2_PORT

#include <OS.h>
#include <String.h>

#include <mqueue.h>




#include <pthread.h>


class Port
{
public:
						Port(int32 queueLength, const char* name);
						~Port();

	status_t			InitCheck() const;

	port_id				ID() const;
	mqd_t				Queue() const;
	const char* 		Name() const;

	status_t			Close();

	ssize_t				Read(int32* _msgCode, void* msgBuffer,
							size_t bufferSize, uint32 flags,
							bigtime_t timeout, port_message_info* info);

	static ssize_t		ReadRemote(port_id id, int32* msgCode,
							void* msgBuffer, size_t bufferSize,
							uint32 flags, bigtime_t timeout, port_message_info* info);

	status_t			Write(int32 msgCode,
							const void* msgBuffer,
							size_t bufferSize,
							uint32 flags,
							bigtime_t timeout);

	static status_t		WriteRemote(port_id id,
							int32 msgCode,
							const void* msgBuffer,
							size_t bufferSize,
							uint32 flags,
							bigtime_t timeout);

	void				ReadLock() { pthread_mutex_lock(&fReadLock); };
	void				ReadUnlock() { pthread_mutex_unlock(&fReadLock); };

	void				WriteLock() { pthread_mutex_lock(&fWriteLock); };
	void				WriteUnlock() { pthread_mutex_unlock(&fWriteLock); };

protected:
	static ssize_t		ReadFd(port_id id, int fd, BString name,
							int32* _msgCode, void* msgBuffer,
							size_t bufferSize, uint32 flags,
							bigtime_t timeout, port_message_info* info);

	static status_t		WriteFd(int fd,
							BString name,
							int32 msgCode,
							const void* msgBuffer,
							size_t bufferSize,
							uint32 flags,
							bigtime_t timeout);

	static status_t		PollIn(int portFd, const char* path,
							uint32 flags, bigtime_t timeout);

	static status_t		PollOut(int portFd, const char* path,
							uint32 flags, bigtime_t timeout);

private:
	static bool			_IsOpen(const char* path);
	static void			_BuildTimeout(struct timeval* tv, uint32 flags, bigtime_t timeout);

	port_id				fID;
	mqd_t				fQueue;
	const char*			fName;
	BString				fPrivateName;

	status_t			fInitCheck;

	pthread_mutex_t		fReadLock;
	pthread_mutex_t		fWriteLock;
};


#endif
