/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaNode.h>


BMediaNode::BMediaNode(const char* name, media_type type,
	media_client_kinds kinds)
	:
	BMediaClient(name, type, kinds),
	fFormat()
{
	fFormat.SetToDefault();
}


BMediaNode::~BMediaNode()
{
}


status_t
BMediaNode::GetPreferredFormat(BMediaFormat* format) const
{
	if (format == NULL)
		return B_BAD_VALUE;
	*format = fFormat;
	return B_OK;
}


status_t
BMediaNode::SetFormat(const BMediaFormat& format)
{
	if (IsStarted())
		return B_NOT_ALLOWED;
	fFormat = format;
	return B_OK;
}


status_t
BMediaNode::GetStreamFormat(BMediaFormat* outFormat) const
{
	if (outFormat == NULL)
		return B_BAD_VALUE;
	*outFormat = fFormat;
	return B_OK;
}
