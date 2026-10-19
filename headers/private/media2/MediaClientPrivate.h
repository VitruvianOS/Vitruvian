/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _MEDIA2_MEDIA_CLIENT_PRIVATE_H
#define _MEDIA2_MEDIA_CLIENT_PRIVATE_H


struct pw_stream_events;


namespace BPrivate {
namespace media {


// Backend access to BMediaClient internals.
struct ClientAccess {
	// Stream callbacks shared by every client that owns per-connection streams.
	static	const pw_stream_events*	StreamEvents();
};


}	// namespace media
}	// namespace BPrivate


#endif	// _MEDIA2_MEDIA_CLIENT_PRIVATE_H
