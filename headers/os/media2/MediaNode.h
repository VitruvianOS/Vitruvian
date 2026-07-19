/*
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_NODE_H
#define _MEDIA2_MEDIA_NODE_H


#include <media2/MediaClient.h>
#include <media2/MediaFormat.h>


class BMediaNode : public BMediaClient {
public:
									BMediaNode(const char* name,
										media_type type = B_MEDIA_RAW_AUDIO,
										media_client_kinds kinds = B_MEDIA_PLAYER);
	virtual							~BMediaNode();

	virtual	status_t				GetPreferredFormat(BMediaFormat* format) const;
	virtual	status_t				SetFormat(const BMediaFormat& format);

protected:
	virtual	status_t				GetStreamFormat(BMediaFormat* outFormat) const override;

			BMediaFormat			fFormat;
};


#endif // _MEDIA2_MEDIA_NODE_H
