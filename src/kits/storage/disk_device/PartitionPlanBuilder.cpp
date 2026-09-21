/*
 * Copyright 2026, the Vitruvian project.
 * Distributed under the terms of the MIT License.
 */

#include "PartitionPlanBuilder.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <File.h>
#include <Path.h>

#include "PartitionReference.h"


// Wrapper so two .policy files don't annotate the same pkexec exec.path.
static const char* kInstallHelper = "/usr/libexec/vos-drivesetup-helper";


struct PartitionOpIdMap::Entry {
	PartitionReference*	ref;
	BString				opID;
};


PartitionOpIdMap::~PartitionOpIdMap()
{
}


void
PartitionOpIdMap::Set(PartitionReference* ref, const BString& opID)
{
	Entry* entry = new Entry;
	entry->ref = ref;
	entry->opID = opID;
	fEntries.AddItem(entry);
}


const BString*
PartitionOpIdMap::Get(PartitionReference* ref) const
{
	for (int32 i = 0; i < fEntries.CountItems(); i++) {
		if (fEntries.ItemAt(i)->ref == ref)
			return &fEntries.ItemAt(i)->opID;
	}
	return NULL;
}


PartitionPlanBuilder::PartitionPlanBuilder()
	:
	fOpCount(0),
	fValid(true)
{
}


void
PartitionPlanBuilder::SetDisk(const char* disk)
{
	fDisk = disk;
}


bool
PartitionPlanBuilder::_CheckField(const char* value)
{
	if (value != NULL && strchr(value, '\n') != NULL)
		fValid = false;
	return fValid;
}


void
PartitionPlanBuilder::AddCreateTable(const char* id, const char* type)
{
	if (!_CheckField(id) || !_CheckField(type))
		return;

	fOpCount++;
	fOps << "\n[op]\n";
	fOps << "kind=create_table\n";
	fOps << "id=" << id << "\n";
	fOps << "type=" << type << "\n";

	if (fTableType.IsEmpty())
		fTableType = type;
}


void
PartitionPlanBuilder::AddCreate(const char* id, off_t startMiB,
	off_t sizeMiB, const char* gptType, const char* role)
{
	const char* resolvedRole = (role != NULL && role[0] != '\0') ? role : "none";
	if (!_CheckField(id) || !_CheckField(gptType) || !_CheckField(resolvedRole))
		return;

	fOpCount++;
	fOps << "\n[op]\n";
	fOps << "kind=create\n";
	fOps << "id=" << id << "\n";
	fOps << "start_mib=" << (int64)startMiB << "\n";
	fOps << "size_mib=" << (int64)sizeMiB << "\n";
	fOps << "gpt_type=" << gptType << "\n";
	fOps << "role=" << resolvedRole << "\n";
}


void
PartitionPlanBuilder::AddDelete(const char* id, const char* targetRef)
{
	if (!_CheckField(id) || !_CheckField(targetRef))
		return;

	fOpCount++;
	fOps << "\n[op]\n";
	fOps << "kind=delete\n";
	fOps << "id=" << id << "\n";
	fOps << "target_ref=" << targetRef << "\n";
}


void
PartitionPlanBuilder::_AppendOptions(BString& out, const BMessage* options) const
{
	if (options == NULL)
		return;

	char* name;
	uint32 type;
	int32 count;
	for (int32 i = 0; options->GetInfo(B_STRING_TYPE, i, &name, &type,
			&count) == B_OK; i++) {
		for (int32 j = 0; j < count; j++) {
			BString value;
			options->FindString(name, j, &value);
			out << name << "=" << value << "\n";
		}
	}
}


void
PartitionPlanBuilder::AddFormat(const char* id, const char* targetRef,
	const char* filesystem, const char* label, const char* role,
	const BMessage* options)
{
	const char* resolvedLabel = label != NULL ? label : "";
	const char* resolvedRole = (role != NULL && role[0] != '\0') ? role : "none";
	if (!_CheckField(id) || !_CheckField(targetRef) || !_CheckField(filesystem)
		|| !_CheckField(resolvedLabel) || !_CheckField(resolvedRole)) {
		return;
	}
	if (options != NULL) {
		char* name;
		uint32 type;
		int32 count;
		for (int32 i = 0; options->GetInfo(B_STRING_TYPE, i, &name, &type,
				&count) == B_OK; i++) {
			for (int32 j = 0; j < count; j++) {
				BString value;
				options->FindString(name, j, &value);
				if (!_CheckField(name) || !_CheckField(value.String()))
					return;
			}
		}
	}

	fOpCount++;
	fOps << "\n[op]\n";
	fOps << "kind=format\n";
	fOps << "id=" << id << "\n";
	fOps << "target_ref=" << targetRef << "\n";
	fOps << "filesystem=" << filesystem << "\n";
	fOps << "label=" << resolvedLabel << "\n";
	fOps << "role=" << resolvedRole << "\n";
	_AppendOptions(fOps, options);
}


void
PartitionPlanBuilder::AddSetFlags(const char* id, const char* targetRef,
	const BMessage& flags)
{
	if (!_CheckField(id) || !_CheckField(targetRef))
		return;

	BString flag;
	for (int32 i = 0; flags.FindString("flag", i, &flag) == B_OK; i++) {
		if (!_CheckField(flag.String()))
			return;
	}

	fOpCount++;
	fOps << "\n[op]\n";
	fOps << "kind=set_flags\n";
	fOps << "id=" << id << "\n";
	fOps << "target_ref=" << targetRef << "\n";
	for (int32 i = 0; flags.FindString("flag", i, &flag) == B_OK; i++)
		fOps << "flag=" << flag << "\n";
}


void
PartitionPlanBuilder::AddResize(const char* id, const char* targetRef,
	const char* filesystem, off_t newSizeMiB)
{
	if (!_CheckField(id) || !_CheckField(targetRef) || !_CheckField(filesystem))
		return;

	fOpCount++;
	fOps << "\n[op]\n";
	fOps << "kind=resize\n";
	fOps << "id=" << id << "\n";
	fOps << "target_ref=" << targetRef << "\n";
	fOps << "filesystem=" << filesystem << "\n";
	fOps << "new_size_mib=" << (int64)newSizeMiB << "\n";
}


void
PartitionPlanBuilder::AddMove(const char* id, const char* targetRef,
	off_t newStartMiB)
{
	if (!_CheckField(id) || !_CheckField(targetRef))
		return;

	fOpCount++;
	fOps << "\n[op]\n";
	fOps << "kind=move\n";
	fOps << "id=" << id << "\n";
	fOps << "target_ref=" << targetRef << "\n";
	fOps << "new_start_mib=" << (int64)newStartMiB << "\n";
}


void
PartitionPlanBuilder::AddErase(const char* id, const char* targetRef)
{
	if (!_CheckField(id) || !_CheckField(targetRef))
		return;

	fOpCount++;
	fOps << "\n[op]\n";
	fOps << "kind=erase\n";
	fOps << "id=" << id << "\n";
	fOps << "target_ref=" << targetRef << "\n";
}


void
PartitionPlanBuilder::AddRepair(const char* id, const char* targetRef,
	const char* filesystem, bool checkOnly)
{
	if (!_CheckField(id) || !_CheckField(targetRef) || !_CheckField(filesystem))
		return;

	fOpCount++;
	fOps << "\n[op]\n";
	fOps << "kind=repair\n";
	fOps << "id=" << id << "\n";
	fOps << "target_ref=" << targetRef << "\n";
	fOps << "filesystem=" << filesystem << "\n";
	fOps << "check_only=" << (checkOnly ? "1" : "0") << "\n";
}


// "string_kind", not "kind": kind is already this record's op-type key.
void
PartitionPlanBuilder::AddSetLabel(const char* id, const char* targetRef,
	const char* filesystem, const char* value)
{
	if (!_CheckField(id) || !_CheckField(targetRef) || !_CheckField(filesystem)
		|| !_CheckField(value)) {
		return;
	}

	fOpCount++;
	fOps << "\n[op]\n";
	fOps << "kind=set_string\n";
	fOps << "id=" << id << "\n";
	fOps << "target_ref=" << targetRef << "\n";
	fOps << "string_kind=label\n";
	fOps << "filesystem=" << filesystem << "\n";
	fOps << "value=" << value << "\n";
}


void
PartitionPlanBuilder::AddSetGptName(const char* id, const char* targetRef,
	const char* value)
{
	if (!_CheckField(id) || !_CheckField(targetRef) || !_CheckField(value))
		return;

	fOpCount++;
	fOps << "\n[op]\n";
	fOps << "kind=set_string\n";
	fOps << "id=" << id << "\n";
	fOps << "target_ref=" << targetRef << "\n";
	fOps << "string_kind=gpt_name\n";
	fOps << "value=" << value << "\n";
}


BString
PartitionPlanBuilder::ToPlan() const
{
	BString plan;
	plan << "plan_version=1\n";
	plan << "disk=" << fDisk << "\n";
	plan << "table_type=" << (fTableType.IsEmpty() ? "gpt" : fTableType.String())
		<< "\n";
	plan << fOps;
	return plan;
}


// Must mirror _partlib_kv_split() in vos-partition-lib.sh; same encoding.
static bool
split_kv(const BString& line, BString& key, BString& value)
{
	int32 eq = line.FindFirst('=');
	if (eq < 0)
		return false;
	line.CopyInto(key, 0, eq);
	value.SetTo(line.String() + eq + 1);
	return true;
}


// "kind=" is stored under field "op": the name ProgressWindow already reads.
static void
flush_record(const BString& section, BMessage& record, bool& haveRecord,
	BMessage& volumes, int32& volumeCount, BMessage& ops, int32& opCount)
{
	if (!haveRecord)
		return;
	if (section == "volume")
		volumes.AddMessage(BString() << volumeCount++, &record);
	else if (section == "op")
		ops.AddMessage(BString() << opCount++, &record);
	record.MakeEmpty();
	haveRecord = false;
}


static void
parse_result(const BString& text, BMessage& result)
{
	BString status, failedOp;
	BMessage volumes, ops;
	int32 volumeCount = 0, opCount = 0;

	BString section;
	BMessage record;
	bool haveRecord = false;

	int32 start = 0;
	int32 length = text.Length();
	while (start <= length) {
		int32 nl = text.FindFirst('\n', start);
		BString line;
		if (nl < 0) {
			if (start == length)
				break;
			text.CopyInto(line, start, length - start);
			start = length + 1;
		} else {
			text.CopyInto(line, start, nl - start);
			start = nl + 1;
		}

		if (line.IsEmpty() || line[0] == '#')
			continue;

		if (line[0] == '[' && line[line.Length() - 1] == ']') {
			flush_record(section, record, haveRecord, volumes, volumeCount,
				ops, opCount);
			line.CopyInto(section, 1, line.Length() - 2);
			haveRecord = true;
			continue;
		}

		BString key, value;
		if (!split_kv(line, key, value))
			continue;

		if (!haveRecord) {
			if (key == "status")
				status = value;
			else if (key == "failed_op")
				failedOp = value;
			continue;
		}

		if (section == "op" && key == "kind")
			record.AddString("op", value);
		else
			record.AddString(key.String(), value);
	}
	flush_record(section, record, haveRecord, volumes, volumeCount, ops,
		opCount);

	result.AddString("status", status);
	result.AddString("failed_op", failedOp);
	if (volumeCount > 0)
		result.AddMessage("volumes", &volumes);
	if (opCount > 0)
		result.AddMessage("ops", &ops);

	if (!failedOp.IsEmpty()) {
		for (int32 i = 0; i < opCount; i++) {
			BMessage op;
			if (ops.FindMessage(BString() << i, &op) != B_OK)
				continue;
			BString id, detail;
			op.FindString("id", &id);
			if (id != failedOp)
				continue;
			op.FindString("detail", &detail);
			if (!detail.IsEmpty())
				result.AddString("detail", detail);
			break;
		}
	}
}


status_t
PartitionPlanBuilder::RunPlan(const BString& plan, BMessage& result,
	bool allowReformatEsp)
{
	char planPath[] = "/tmp/vos-partition-plan-XXXXXX";
	int fd = mkstemp(planPath);
	if (fd < 0)
		return B_ERROR;

	ssize_t written = write(fd, plan.String(), plan.Length());
	close(fd);
	if (written < 0 || (size_t)written != (size_t)plan.Length()) {
		unlink(planPath);
		return B_ERROR;
	}

	int errPipe[2];
	if (pipe(errPipe) < 0) {
		unlink(planPath);
		return B_ERROR;
	}

	pid_t pid = fork();
	if (pid < 0) {
		close(errPipe[0]);
		close(errPipe[1]);
		unlink(planPath);
		return B_ERROR;
	}
	if (pid == 0) {
		close(errPipe[0]);
		dup2(errPipe[1], STDERR_FILENO);
		close(errPipe[1]);
		if (allowReformatEsp) {
			execl("/usr/bin/pkexec", "pkexec", kInstallHelper, "partition",
				"apply", planPath, "--allow-reformat-esp", (char*)NULL);
		} else {
			execl("/usr/bin/pkexec", "pkexec", kInstallHelper, "partition",
				"apply", planPath, (char*)NULL);
		}
		_exit(127);
	}

	close(errPipe[1]);

	BString helperLog;
	char readBuffer[512];
	ssize_t bytesRead;
	while ((bytesRead = read(errPipe[0], readBuffer, sizeof(readBuffer) - 1)) > 0) {
		readBuffer[bytesRead] = '\0';
		helperLog << readBuffer;
	}
	close(errPipe[0]);

	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;

	BString resultPath(planPath);
	resultPath << ".result";

	BFile resultFile(resultPath.String(), B_READ_ONLY);
	status_t error = resultFile.InitCheck();
	if (error == B_OK) {
		off_t size = 0;
		resultFile.GetSize(&size);
		char* buffer = (char*)malloc(size + 1);
		if (buffer != NULL) {
			ssize_t got = resultFile.Read(buffer, size);
			if (got < 0)
				got = 0;
			buffer[got] = '\0';
			parse_result(BString(buffer), result);
			free(buffer);
		} else {
			error = B_NO_MEMORY;
		}
	}

	if (getenv("VOS_KEEP_PLAN") == NULL) {
		unlink(planPath);
		unlink(resultPath.String());
	} else {
		fprintf(stderr, "[DriveSetup] plan kept at %s (result: %s)\n",
			planPath, resultPath.String());
	}

	if (error != B_OK) {
		int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
		BString detail;
		status_t reason = B_ERROR;

		if (!WIFEXITED(status)) {
			detail = "The helper was killed before it finished.";
		} else switch (exitCode) {
			case 0:
				detail = "The helper reported success but wrote no result "
					"file.";
				break;
			case 1:
				detail = "The helper failed while applying the plan.";
				break;
			case 2:
				detail = "The plan was rejected as invalid.";
				result.AddString("failed_op", "validate");
				reason = B_BAD_VALUE;
				break;
			case 126:
				detail = "Not authorized: the request was denied or "
					"cancelled.";
				reason = B_PERMISSION_DENIED;
				break;
			case 127:
				detail.SetToFormat("Could not run %s -- is it installed?",
					kInstallHelper);
				reason = B_ENTRY_NOT_FOUND;
				break;
			default:
				detail.SetToFormat("The helper exited with status %d.",
					exitCode);
				break;
		}

		if (helperLog.Length() > 0)
			detail << "\n\n" << helperLog;

		result.AddString("status", "failed");
		result.AddString("detail", detail);
		result.AddInt32("exit_code", exitCode);
		if (helperLog.Length() > 0)
			fprintf(stderr, "%s", helperLog.String());
		return reason;
	}

	const char* resultStatus = NULL;
	if (result.FindString("status", &resultStatus) == B_OK
		&& resultStatus != NULL && strcmp(resultStatus, "ok") != 0) {
		if (helperLog.Length() > 0) {
			fprintf(stderr, "%s", helperLog.String());
			BString detail;
			if (result.FindString("detail", &detail) == B_OK)
				detail << "\n\n" << helperLog;
			else
				detail = helperLog;
			result.RemoveName("detail");
			result.AddString("detail", detail);
		}
	}

	return B_OK;
}
