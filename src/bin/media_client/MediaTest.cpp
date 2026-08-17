/*
 * Copyright 2017, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MediaTest.h"

#include <media2/MediaClient.h>
#include <media2/MediaConnection.h>
#include <media2/MediaGraph.h>
#include <SupportDefs.h>

#include <assert.h>
#include <stdio.h>
#include <vector>

#ifdef DEBUG
#define DELAYED_MODE 1
#define SNOOZE_FOR 10000000
#endif

#define MAX_MULTI_CLIENTS 3








class TestInput : public BMediaInput {
public:


	TestInput() : BMediaConnection(B_MEDIA_INPUT), BMediaInput() {}

protected:
	status_t AcceptFormat(BMediaFormat* format) override { return B_OK; }
};


class TestOutput : public BMediaOutput {
public:
	TestOutput() : BMediaConnection(B_MEDIA_OUTPUT), BMediaOutput() {}

protected:
	status_t PrepareToConnect(BMediaFormat* format) override { return B_OK; }
	status_t FormatProposal(BMediaFormat* format) override { return B_OK; }
};


static BMediaClient* sProducer = NULL;
static BMediaClient* sConsumer = NULL;
static BMediaClient* sFilter = NULL;

static BMediaClient* sProducers[MAX_MULTI_CLIENTS];
static BMediaClient* sConsumers[MAX_MULTI_CLIENTS];




static std::vector<BMediaConnection*> sConnections;


static void
_ReportStatus(const char* what, status_t status)
{




	printf("  %-40s %s\n", what, strerror(status));
}


static BMediaOutput*
BeginOutput(BMediaClient* client)
{
	TestOutput* output = new TestOutput();
	client->RegisterOutput(output);
	sConnections.push_back(output);
	return output;
}


static BMediaInput*
BeginInput(BMediaClient* client)
{
	TestInput* input = new TestInput();
	client->RegisterInput(input);
	sConnections.push_back(input);
	return input;
}


static void
_ReleaseConnections()
{
	for (size_t i = 0; i < sConnections.size(); i++)
		sConnections[i]->Release();
	sConnections.clear();
}


void
_InitClients(bool hasFilter)
{
	sProducer = new BMediaClient("MediaClientProducer", B_MEDIA_PLAYER);
	sConsumer = new BMediaClient("MediaClientConsumer", B_MEDIA_RECORDER);

	if (hasFilter)
		sFilter = new BMediaClient("MediaClientFilter", B_MEDIA_FILTER);
	else
		sFilter = NULL;
}


void
_InitClientsMulti(bool isMixer)
{
	if (!isMixer) {
		for (int i = 0; i < MAX_MULTI_CLIENTS; i++) {
			sConsumers[i] = new BMediaClient("Test Consumer", B_MEDIA_RECORDER);
		}
		sProducer = new BMediaClient("MediaClientProducer", B_MEDIA_PLAYER);
	} else {
		for (int i = 0; i < MAX_MULTI_CLIENTS; i++) {
			sProducers[i] = new BMediaClient("Test Producer", B_MEDIA_PLAYER);
		}
		sConsumer = new BMediaClient("MediaClientConsumer", B_MEDIA_RECORDER);
	}

	sFilter = new BMediaClient("MediaClientFilter", B_MEDIA_FILTER);
}


void
_DeleteClients()
{
	_ReleaseConnections();
	delete sProducer;
	delete sConsumer;
	delete sFilter;
}


void
_DeleteClientsMulti(bool isMixer)
{
	_ReleaseConnections();
	if (!isMixer) {
		for (int i = 0; i < MAX_MULTI_CLIENTS; i++) {
			delete sConsumers[i];
		}
		delete sProducer;
	} else {
		for (int i = 0; i < MAX_MULTI_CLIENTS; i++) {
			delete sProducers[i];
		}
		delete sConsumer;
	}
	delete sFilter;
}


BMediaFormat
_BuildRawAudioFormat()
{
	BMediaFormat format;
	format.format.type = B_MEDIA_RAW_AUDIO;
	format.format.u.raw_audio = media_raw_audio_format::wildcard;

	return format;
}


void
_ConsumerProducerTest()
{
	_InitClients(false);

	BMediaOutput* output = BeginOutput(sProducer);
	BMediaInput* input = BeginInput(sConsumer);

	output->SetAcceptedFormat(_BuildRawAudioFormat());
	input->SetAcceptedFormat(_BuildRawAudioFormat());




	_ReportStatus("Connect (producer -> consumer)",
		BMediaGraph::Instance()->Connect(output, input));

	#ifdef DELAYED_MODE
	snooze(SNOOZE_FOR);
	#endif

	_ReportStatus("Disconnect (input)", input->Disconnect());

	_DeleteClients();
}


void
_ProducerConsumerTest()
{
	_InitClients(false);

	BMediaOutput* output = BeginOutput(sProducer);
	BMediaInput* input = BeginInput(sConsumer);

	_ReportStatus("Connect (producer -> consumer)",
		BMediaGraph::Instance()->Connect(output, input));

	#ifdef DELAYED_MODE
	snooze(SNOOZE_FOR);
	#endif




	_ReportStatus("Disconnect (output)", output->Disconnect());

	_DeleteClients();
}


void
_ProducerFilterConsumerTest()
{
	_InitClients(true);

	BMediaOutput* output = BeginOutput(sProducer);
	BMediaInput* input = BeginInput(sConsumer);

	BMediaInput* filterInput = BeginInput(sFilter);
	BMediaOutput* filterOutput = BeginOutput(sFilter);

	_ReportStatus("Bind (filter passthrough)",
		sFilter->Bind(filterInput, filterOutput));

	_ReportStatus("Connect (producer -> filter)",
		BMediaGraph::Instance()->Connect(output, filterInput));
	_ReportStatus("Connect (filter -> consumer)",
		BMediaGraph::Instance()->Connect(filterOutput, input));

	#ifdef DELAYED_MODE
	snooze(SNOOZE_FOR);
	#endif

	_ReportStatus("Unbind (filter passthrough)",
		sFilter->Unbind(filterInput, filterOutput));

	_DeleteClients();
}


void
_SplitterConfigurationTest()
{
	_InitClientsMulti(false);

	for (int i = 0; i < MAX_MULTI_CLIENTS; i++) {
		BMediaOutput* output = BeginOutput(sFilter);
		_ReportStatus("Connect (filter -> consumer[i])",
			BMediaGraph::Instance()->Connect(output, BeginInput(sConsumers[i])));
	}

	_ReportStatus("Connect (producer -> filter)",
		BMediaGraph::Instance()->Connect(BeginOutput(sProducer),
			BeginInput(sFilter)));

	#ifdef DELAYED_MODE
	snooze(SNOOZE_FOR);
	#endif

	_DeleteClientsMulti(false);
}


void
_MixerConfigurationTest()
{
	_InitClientsMulti(true);

	for (int i = 0; i < MAX_MULTI_CLIENTS; i++) {
		BMediaInput* input = BeginInput(sFilter);
		_ReportStatus("Connect (producer[i] -> filter)",
			BMediaGraph::Instance()->Connect(BeginOutput(sProducers[i]), input));
	}

	_ReportStatus("Connect (filter -> consumer)",
		BMediaGraph::Instance()->Connect(BeginOutput(sFilter),
			BeginInput(sConsumer)));

	#ifdef DELAYED_MODE
	snooze(SNOOZE_FOR);
	#endif

	_DeleteClientsMulti(true);
}


void
media_test()
{
	printf("Testing Simple (1:1) Producer-Consumer configuration:\n");
	_ConsumerProducerTest();
	_ProducerConsumerTest();

	printf("Testing Simple (1:1:1) Producer-Filter-Consumer configuration:\n");
	_ProducerFilterConsumerTest();

	printf("Testing Splitter Configuration (N:1:1):\n");
	_SplitterConfigurationTest();

	printf("Testing Mixer Configuration (N:1:1):\n");
	_MixerConfigurationTest();
}
