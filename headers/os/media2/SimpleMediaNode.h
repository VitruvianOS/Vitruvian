/*
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_SIMPLE_MEDIA_NODE_H
#define _MEDIA2_SIMPLE_MEDIA_NODE_H


#include <media2/MediaNode.h>


class BSimpleMediaNode : public BMediaNode {
public:
	enum notification {
		B_WILL_START = 1,
		B_WILL_STOP,
		B_WILL_SEEK,
		B_FORMAT_SUGGESTION
	};

	typedef void (*notify_hook)(void* cookie, notification what, ...);
	typedef void (*process_hook)(void* cookie, void* buffer, size_t size,
		uint32 frameCount, const BMediaFormat& format);
	typedef status_t (*format_hook)(void* cookie, BMediaFormat* format);

									BSimpleMediaNode(const char* name,
										media_type type = B_MEDIA_RAW_AUDIO,
										media_client_kinds kinds = B_MEDIA_PLAYER);
	virtual							~BSimpleMediaNode();

			void					SetNotifyHook(notify_hook hook, void* cookie);
			void					SetProcessHook(process_hook hook, void* cookie);
			void					SetFormatHook(format_hook hook, void* cookie);

protected:
	virtual	void					HandleStart(bigtime_t performanceTime) override;
	virtual	void					HandleStop(bigtime_t performanceTime) override;
	virtual	void					HandleSeek(bigtime_t mediaTime,
										bigtime_t performanceTime) override;
	virtual	void					ProcessCallback(BMediaConnection* connection,
										void* buffer, size_t bufferSize,
										uint32 frameCount) override;
	virtual	status_t				GetStreamFormat(BMediaFormat* outFormat) const override;

private:
			notify_hook				fNotifyHook;
			process_hook			fProcessHook;
			format_hook				fFormatHook;
			void*					fNotifyCookie;
			void*					fProcessCookie;
			void*					fFormatCookie;
};


#endif // _MEDIA2_SIMPLE_MEDIA_NODE_H
