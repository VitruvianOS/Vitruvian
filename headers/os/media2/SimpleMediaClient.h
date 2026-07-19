/*
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_SIMPLE_MEDIA_CLIENT_H
#define _MEDIA2_SIMPLE_MEDIA_CLIENT_H


#include <media2/MediaClient.h>


class BSimpleMediaInput;
class BSimpleMediaOutput;


class BSimpleMediaClient : public BMediaClient {
public:
	enum notification {
		B_WILL_START = 1,
		B_WILL_STOP,
		B_WILL_SEEK,
		B_FORMAT_SUGGESTION
	};

	typedef void (*notify_hook)(void* cookie, notification what, ...);

	typedef void (*process_hook)(void* cookie,
								 BMediaConnection* connection,
								 void* buffer, size_t bufferSize,
								 uint32 frameCount);

									BSimpleMediaClient(const char* name,
										media_type type = B_MEDIA_UNKNOWN_TYPE,
										media_client_kinds kinds = B_MEDIA_PLAYER);
	virtual							~BSimpleMediaClient();

			BSimpleMediaInput*		BeginInput();
			BSimpleMediaOutput*		BeginOutput();

			void					SetNotifyHook(notify_hook hook, void* cookie);
			void					SetProcessHook(process_hook hook, void* cookie);

protected:
	virtual	void					HandleStart(bigtime_t performanceTime) override;
	virtual	void					HandleStop(bigtime_t performanceTime) override;
	virtual	void					HandleSeek(bigtime_t mediaTime,
										bigtime_t performanceTime) override;
	virtual	void					ProcessCallback(BMediaConnection* connection,
										void* buffer, size_t bufferSize,
										uint32 frameCount) override;

private:
			notify_hook				fNotifyHook;
			process_hook			fProcessHook;
			void*					fNotifyCookie;
			void*					fProcessCookie;
};


#endif // _MEDIA2_SIMPLE_MEDIA_CLIENT_H
