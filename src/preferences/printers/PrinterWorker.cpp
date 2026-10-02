/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PrinterWorker.h"

#include <string.h>

#include <Message.h>
#include <OS.h>
#include <String.h>


struct WorkerRequest {
	uint32		op;
	BMessenger	reply;
	BString		name;
	BString		name2;
	BString		name3;
	int32		jobId;
	bool		flag;
	printcups_options options;
};


static int32
WorkerEntry(void* data)
{
	WorkerRequest* request = static_cast<WorkerRequest*>(data);
	BMessage result(kMsgWorkerResult);
	result.AddInt32("op", (int32)request->op);

	status_t status = B_OK;
	const char* error = "";

	switch (request->op) {
		case kWorkerOpListQueues:
		{
			static const int32 kMax = 64;
			printcups_queue_info* queues
				= new printcups_queue_info[kMax];
			int count = printcups_list_queues(queues, kMax);
			if (count < 0) {
				status = B_ERROR;
				error = printcups_last_error();
			} else {
				result.AddData("queues", B_RAW_TYPE, queues,
					count * (int32)sizeof(printcups_queue_info));
				result.AddInt32("count", count);
			}
			delete[] queues;
			break;
		}

		case kWorkerOpListJobs:
		{
			static const int32 kMax = 128;
			printcups_job* jobs = new printcups_job[kMax];
			int count = printcups_list_jobs(
				request->name.IsEmpty() ? NULL : request->name.String(),
				jobs, kMax);
			if (count < 0) {
				status = B_ERROR;
				error = printcups_last_error();
			} else {
				result.AddData("jobs", B_RAW_TYPE, jobs,
					count * (int32)sizeof(printcups_job));
				result.AddInt32("count", count);
			}
			delete[] jobs;
			break;
		}

		case kWorkerOpSetDefault:
			status = printcups_set_default(request->name.String());
			error = printcups_last_error();
			break;

		case kWorkerOpCancelJob:
			status = printcups_cancel_job(request->name.String(),
				request->jobId);
			error = printcups_last_error();
			break;

		case kWorkerOpHoldJob:
			status = printcups_hold_job(request->name.String(),
				request->jobId);
			error = printcups_last_error();
			break;

		case kWorkerOpReleaseJob:
			status = printcups_release_job(request->name.String(),
				request->jobId);
			error = printcups_last_error();
			break;

		case kWorkerOpPurgeJobs:
			status = printcups_purge_jobs(request->name.String());
			error = printcups_last_error();
			break;

		case kWorkerOpRemovePrinter:
			status = printcups_remove_printer(request->name.String());
			error = printcups_last_error();
			break;

		case kWorkerOpRenamePrinter:
			status = printcups_rename_printer(request->name.String(),
				request->name2.String(), request->name3.String());
			error = printcups_last_error();
			break;

		case kWorkerOpSetAccepting:
			status = printcups_set_accepting(request->name.String(),
				request->flag);
			error = printcups_last_error();
			break;

		case kWorkerOpSetOptions:
			status = printcups_set_options(request->name.String(),
				&request->options);
			error = printcups_last_error();
			break;

		case kWorkerOpTestPage:
			status = printcups_print_test_page(request->name.String());
			error = printcups_last_error();
			break;

		case kWorkerOpAddPrinter:
			status = printcups_add_printer(request->name.String(),
				request->name2.String(), request->name3.String());
			error = printcups_last_error();
			break;

		case kWorkerOpDiscover:
		{
			static const int32 kMax = 64;
			printcups_device* devices = new printcups_device[kMax];
			int count = printcups_discover_devices(devices, kMax);
			if (count < 0) {
				status = B_ERROR;
				error = printcups_last_error();
			} else {
				result.AddData("devices", B_RAW_TYPE, devices,
					count * (int32)sizeof(printcups_device));
				result.AddInt32("count", count);
			}
			delete[] devices;
			break;
		}

		case kWorkerOpListPPDs:
		{
			static const int32 kMax = 256;
			printcups_ppd* ppds = new printcups_ppd[kMax];
			int count = printcups_list_ppds(
				request->name.IsEmpty() ? NULL : request->name.String(),
				ppds, kMax);
			if (count < 0) {
				status = B_ERROR;
				error = printcups_last_error();
			} else {
				result.AddData("ppds", B_RAW_TYPE, ppds,
					count * (int32)sizeof(printcups_ppd));
				result.AddInt32("count", count);
			}
			delete[] ppds;
			break;
		}

		case kWorkerOpListChoices:
		{
			static const int32 kMax = 64;
			printcups_option_choice* choices
				= new printcups_option_choice[kMax];
			int count = printcups_list_choices(request->name.String(),
				request->name2.String(), choices, kMax);
			if (count < 0) {
				status = B_ERROR;
				error = printcups_last_error();
			} else {
				result.AddData("choices", B_RAW_TYPE, choices,
					count * (int32)sizeof(printcups_option_choice));
				result.AddInt32("count", count);
			}
			delete[] choices;
			break;
		}

		default:
			status = B_BAD_VALUE;
			break;
	}

	result.AddInt32("status", status);
	result.AddString("error", error != NULL ? error : "");
	request->reply.SendMessage(&result);
	delete request;
	return 0;
}


bool
PrinterWorker::Post(uint32 op, const BMessenger& reply, const BString& name,
	const BString& name2, const BString& name3, int32 jobId, bool flag,
	const printcups_options* options)
{
	WorkerRequest* request = new WorkerRequest;
	request->op = op;
	request->reply = reply;
	request->name = name;
	request->name2 = name2;
	request->name3 = name3;
	request->jobId = jobId;
	request->flag = flag;
	if (options != NULL)
		request->options = *options;
	else
		memset(&request->options, 0, sizeof(request->options));

	thread_id thread = spawn_thread(WorkerEntry, "printers:worker",
		B_NORMAL_PRIORITY, request);
	if (thread < B_OK) {
		delete request;
		return false;
	}
	resume_thread(thread);
	return true;
}
