/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_NODE_H
#define _MEDIA2_MEDIA_NODE_H


#include <media2/MediaClient.h>
#include <media2/MediaFormat.h>
#include <pipewire/core.h>

#include <Locker.h>

#include <map>
#include <vector>

#include <pipewire/link.h>
#include <pipewire/proxy.h>


class BMediaNode : public BMediaClient {
public:
									BMediaNode(const char* name, media_type type,
										media_client_kinds kinds);
	virtual							~BMediaNode();

	virtual	status_t				GetPreferredFormat(BMediaFormat* format) const;
	virtual	status_t				SetFormat(const BMediaFormat& format);

	virtual	status_t				Start();
	virtual	status_t				Stop();

	virtual	status_t				Bind(BMediaInput* input, BMediaOutput* output) override;
	virtual	status_t				Unbind(BMediaInput* input, BMediaOutput* output) override;

private:
			status_t				_StartConnections(void* backend);
			void					_StopConnections();
			status_t				_CreateLink(BMediaOutput* output,
										BMediaInput* input, pw_core* core);
			void					_DestroyLinks();

	static	void					_OnLinkInfo(void* data,
										const struct pw_link_info* info);
	static	const struct pw_link_events	sLinkEvents;

			BMediaFormat			fFormat;

			struct NodeLinkInfo {
				pw_proxy*			proxy;
				enum pw_link_state	state;
				spa_hook			listener;
				BMediaNode*			owner;
			};

			BLocker					fLinksLock;
			std::map<uint32, NodeLinkInfo>			fLinkInfos;

			std::map<void*, std::vector<uint32> >	fLinks;
};


#endif
