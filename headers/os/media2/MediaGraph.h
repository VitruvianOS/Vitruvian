/*
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_GRAPH_H
#define _MEDIA2_MEDIA_GRAPH_H


#include <media2/MediaClientDefs.h>


class BMediaConnection;
class BMediaInput;
class BMediaOutput;
class BMessage;

template<class T, bool O> class BObjectList;


class BMediaGraph {
public:
	static	BMediaGraph*			Instance();
									// Returns NULL if PipeWire is unavailable.

			status_t				Connect(BMediaOutput* output,
										BMediaInput* input);
			status_t				Disconnect(BMediaOutput* output,
										BMediaInput* input);

			status_t				GetClients(BObjectList<media_client_id, true>* clients);
			status_t				GetClientInfo(media_client_id id, BMessage* info);

			status_t				GetDefaultAudioOutput(media_client_id* id);
			status_t				GetDefaultAudioInput(media_client_id* id);
			status_t				SetDefaultAudioOutput(media_client_id id);
			status_t				SetDefaultAudioInput(media_client_id id);

private:
									BMediaGraph();
									~BMediaGraph();
									BMediaGraph(const BMediaGraph&) = delete;
			BMediaGraph&			operator=(const BMediaGraph&) = delete;
};


inline BMediaGraph*	MediaGraph() { return BMediaGraph::Instance(); }


#endif // _MEDIA2_MEDIA_GRAPH_H
