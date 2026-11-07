/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "Controller.h"

#include <new>
#include <stdio.h>
#include <string.h>

#include <media2/MediaDefs.h>

#include "VideoView.h"


Controller::Controller()
	:
	fVideoView(NULL),
	fCurrentSource(-1),
	fConnectedNodeId(0),
	fCapturing(false),
	fInitCheck(B_OK)
{
	fInitCheck = _RefreshSources();
}


Controller::~Controller()
{
	StopCapture();
}


void
Controller::SetVideoView(VideoView* view)
{
	fVideoView = view;
}


int32
Controller::CountSources() const
{
	return fSources.CountItems();
}


const char*
Controller::SourceName(int32 index) const
{
	BMediaGraph::ForeignNode* node = fSources.ItemAt(index);
	return node != NULL ? node->description.String() : NULL;
}


int32
Controller::CurrentSource() const
{
	return fCurrentSource;
}


status_t
Controller::SelectSource(int32 index)
{
	if (index < 0 || index >= fSources.CountItems())
		return B_BAD_VALUE;
	if (fCapturing)
		StopCapture();
	fCurrentSource = index;
	return B_OK;
}


status_t
Controller::StartCapture()
{
	if (fCurrentSource < 0 || fCurrentSource >= fSources.CountItems())
		return B_BAD_VALUE;

	BMediaGraph::ForeignNode* node = fSources.ItemAt(fCurrentSource);
	if (node == NULL)
		return B_BAD_VALUE;

	status_t err = _ConnectSource(node->id);
	if (err != B_OK)
		return err;

	fCapturing = true;
	if (fVideoView != NULL)
		fVideoView->SetCapturing(true);
	return B_OK;
}


void
Controller::StopCapture()
{
	if (!fCapturing)
		return;
	fCapturing = false;
	fConnectedNodeId = 0;
	if (fVideoView != NULL)
		fVideoView->SetCapturing(false);
}


status_t
Controller::_RefreshSources()
{
	fSources.MakeEmpty();
	fCurrentSource = -1;

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	BObjectList<BMediaGraph::ForeignNode, true> nodes;
	status_t err = graph->GetForeignNodes(&nodes);
	if (err != B_OK)
		return err;

	for (int32 i = 0; i < nodes.CountItems(); i++) {
		BMediaGraph::ForeignNode* node = nodes.ItemAt(i);
		if (node == NULL)
			continue;
		const char* mediaClass = node->mediaClass.String();
		if (mediaClass == NULL)
			continue;
		// PipeWire camera/v4l2/DVB nodes, when present, carry one of these.
		// Audio/Source devices are not TV inputs.
		if (strstr(mediaClass, "Video") != NULL
				|| strstr(mediaClass, "DVB") != NULL
				|| strstr(mediaClass, "Camera") != NULL) {
			BMediaGraph::ForeignNode* copy = new(std::nothrow)
				BMediaGraph::ForeignNode(*node);
			if (copy != NULL)
				fSources.AddItem(copy);
		}
	}

	if (fSources.CountItems() > 0)
		fCurrentSource = 0;
	return B_OK;
}


status_t
Controller::_ConnectSource(uint32 nodeId)
{
	// Seam: media2 enumerates foreign video-ish nodes, but BMediaNode
	// only starts raw-audio streams and nothing delivers raw video
	// frames to a BView. Connect is therefore not offered yet.
	fprintf(stderr, "TV: video capture path is not in media2 yet "
		"(node %u selected, not connected)\n", nodeId);
	return B_NOT_SUPPORTED;
}
