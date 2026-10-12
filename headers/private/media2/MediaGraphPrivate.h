/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _MEDIA2_MEDIA_GRAPH_PRIVATE_H
#define _MEDIA2_MEDIA_GRAPH_PRIVATE_H


// Private BMediaGraph watcher notifications. No public B_MEDIA_* mask bit
// exists for these, so a watcher receives them whatever mask it asked for.
enum {
	kMsgDevicePortChanged		= 'PWPC',
	kMsgDeviceProfileChanged	= 'PWPF'
};


#endif // _MEDIA2_MEDIA_GRAPH_PRIVATE_H
