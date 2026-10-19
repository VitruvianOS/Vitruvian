/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_NODE_H
#define _MEDIA2_MEDIA_NODE_H


#include <media2/MediaClient.h>
#include <media2/MediaFormat.h>

#include <Locker.h>


namespace BPrivate {
namespace media {
struct NodeLinkState;
}
}


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
			void					_StreamFault(BMediaConnection* connection) override;
			status_t				_CreateLink(BMediaOutput* output,
										BMediaInput* input, void* core);
			void					_DestroyLinks();

			BMediaFormat			fFormat;

			BPrivate::media::NodeLinkState*	fLinkState;
};


#endif
