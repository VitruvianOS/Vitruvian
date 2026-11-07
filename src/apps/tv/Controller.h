/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef TV_CONTROLLER_H
#define TV_CONTROLLER_H


#include <ObjectList.h>
#include <String.h>

#include <media2/MediaGraph.h>


class VideoView;


// Capture-source picker over BMediaGraph. Connect/Start stop at the
// media2 video seam; see CaptureSession.cpp.
class Controller {
public:
								Controller();
								~Controller();

			void				SetVideoView(VideoView* view);

			int32				CountSources() const;
			const char*			SourceName(int32 index) const;
			int32				CurrentSource() const;

			status_t			SelectSource(int32 index);
			status_t			StartCapture();
			void				StopCapture();

			status_t			InitCheck() const { return fInitCheck; }

private:
			status_t			_RefreshSources();
			status_t			_ConnectSource(uint32 nodeId);

			VideoView*			fVideoView;
			BObjectList<BMediaGraph::ForeignNode, true> fSources;
			int32				fCurrentSource;
			uint32				fConnectedNodeId;
			bool				fCapturing;
			status_t			fInitCheck;
};


#endif
