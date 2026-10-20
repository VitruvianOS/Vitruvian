/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _MEDIA2_ROUTING_STORE_H
#define _MEDIA2_ROUTING_STORE_H


#include <Path.h>
#include <String.h>


// One user link, named by stable node and port names.
struct BMediaRoute {
	BString		fromNode;
	BString		fromPort;
	BString		toNode;
	BString		toPort;
};


// User links kept in ~/config/settings/Media/routing.
class BRoutingStore {
public:
	static	BRoutingStore*		Instance();

			status_t			Load();
			status_t			Save() const;

			// One route per (from, to) pair; a later add replaces it.
			status_t			AddRoute(const BMediaRoute& route);
			status_t			RemoveRoute(const BMediaRoute& route);

			int32				CountRoutes() const;
			status_t			GetRoute(int32 index,
									BMediaRoute* outRoute) const;

private:
								BRoutingStore();
								~BRoutingStore();

			struct Impl;
			Impl*				fImpl;
};


#endif // _MEDIA2_ROUTING_STORE_H
