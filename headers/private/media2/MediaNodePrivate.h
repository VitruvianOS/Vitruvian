/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _MEDIA2_MEDIA_NODE_PRIVATE_H
#define _MEDIA2_MEDIA_NODE_PRIVATE_H


#include <map>
#include <vector>

#include <Locker.h>
#include <SupportDefs.h>

#include <pipewire/link.h>
#include <pipewire/proxy.h>


namespace BPrivate {
namespace media {


// Link bookkeeping owned by a BMediaNode.
struct NodeLinkState {
	struct Info {
		pw_proxy*			proxy;
		enum pw_link_state	state;
		spa_hook			listener;
		NodeLinkState*		owner;
	};

	BLocker								lock;
	std::map<uint32, Info>				infos;
	std::map<void*, std::vector<uint32> >	links;
};


}	// namespace media
}	// namespace BPrivate


#endif	// _MEDIA2_MEDIA_NODE_PRIVATE_H
