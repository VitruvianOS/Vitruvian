/*
 * Copyright 2026, the Vitruvian project.
 * Distributed under the terms of the MIT License.
 */

#include "ExpandVolumeWindow.h"

#include <Alert.h>
#include <Autolock.h>
#include <Button.h>
#include <Catalog.h>
#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Message.h>
#include <Messenger.h>
#include <Notification.h>
#include <Path.h>
#include <Partition.h>
#include <PartitioningInfo.h>
#include <Roster.h>
#include <SeparatorView.h>
#include <Slider.h>
#include <StatusBar.h>
#include <String.h>
#include <StringForSize.h>
#include <StringView.h>

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include <RootMedia.h>

#include "DiskDeviceJobQueue.h"
#include "PartitionReference.h"
#include "ResizeJob.h"

#include "ExpandVolumeApp.h"
#include "ExpandGeometry.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ExpandVolume"


static const char* kDiskUsageSignature = "application/x-vnd.vos-DiskUsage";

// Cooperative busy-timeout: the plan helper blocks on pkexec plus the
// filesystem resize tool; after this long the window declares defeat and
// re-enables Close even if the worker is still stuck in the helper.
static const bigtime_t kApplyTimeout = 10 * 60 * 1000000LL;

enum {
	kStateProbing,
	kStateNoSpace,
	kStateReady,
	kStateBusy,
	kStateDone,
	kStateError,
};


static const char*
_FormatSize(off_t bytes, char* buffer, size_t bufferSize)
{
	return string_for_size(bytes, buffer, bufferSize);
}


// #pragma mark - probe worker


struct ProbeResult {
	BString			volumeName;
	BString			devicePath;
	BString			fileSystem;
	partition_id	partitionID;
	partition_id	deviceID;
	off_t			currentSize;
	off_t			maxSize;
	off_t			blockSize;
	off_t			usedBytes;
	off_t			totalBytes;
	bool			mounted;
	bool			canResizeWhileMounted;
	BString			error;
	BString			gapExplanation;
};


static off_t
_RoundDownToBlock(off_t value, off_t blockSize)
{
	if (blockSize <= 1)
		return value;
	return value / blockSize * blockSize;
}


static void
_ProbeRootPartition(ProbeResult& result)
{
	result.partitionID = -1;
	result.deviceID = -1;
	result.currentSize = 0;
	result.maxSize = 0;
	result.blockSize = 512;
	result.usedBytes = -1;
	result.totalBytes = -1;
	result.mounted = false;
	result.canResizeWhileMounted = false;

	BString rootFs;
	if (!root_is_persistent_rw(&rootFs)) {
		result.error = B_TRANSLATE("The system is running from read-only "
			"media; there is nothing to expand.");
		return;
	}

	// The system volume: whatever "/" is mounted from.
	BDiskDeviceRoster roster;
	BDiskDevice* device = new(std::nothrow) BDiskDevice();
	if (device == NULL) {
		result.error = B_TRANSLATE("Out of memory.");
		return;
	}

	BPartition* partition = NULL;
	status_t status = roster.FindPartitionByMountPoint("/", device,
		&partition);
	if (status != B_OK || partition == NULL) {
		delete device;
		result.error = B_TRANSLATE("Could not locate the system partition "
			"on any disk.");
		return;
	}

	result.partitionID = partition->ID();
	result.deviceID = device->ID();
	result.mounted = partition->IsMounted();
	if (partition->BlockSize() > 0)
		result.blockSize = partition->BlockSize();

	BPath path;
	if (partition->GetPath(&path) == B_OK)
		result.devicePath = path.Path();

	BString contentName = partition->ContentName();
	if (!contentName.IsEmpty())
		result.volumeName = contentName;
	else if (partition->Name() != NULL)
		result.volumeName = partition->Name();
	else
		result.volumeName = result.devicePath;
	if (result.volumeName.IsEmpty())
		result.volumeName = B_TRANSLATE("System");

	if (partition->ContentType() != NULL)
		result.fileSystem = partition->ContentType();

	// Filesystem usage read straight off the mount: no privileged
	// helper at probe time, so the popup stays quiet on a fresh boot.
	struct statvfs vfs;
	if (statvfs("/", &vfs) == 0) {
		result.totalBytes = (off_t)vfs.f_blocks * vfs.f_frsize;
		result.usedBytes
			= (off_t)(vfs.f_blocks - vfs.f_bfree) * vfs.f_frsize;
	}

	result.currentSize = partition->Size();

	bool canResizeContents = false;
	if (!partition->CanResize(&canResizeContents,
			&result.canResizeWhileMounted)
		|| !result.canResizeWhileMounted) {
		BString text;
		text.SetToFormat(B_TRANSLATE("The system filesystem (%s) cannot be "
			"grown here."), rootFs.String());
		result.error = text;
		delete device;
		return;
	}

	// Claimable space: the partitionable range directly behind the
	// partition. Anything further away would need a move first and is
	// out of scope for a one-click grow.
	BPartition* parent = partition->Parent();
	if (parent == NULL) {
		result.error = B_TRANSLATE("The system partition has no parent "
			"disk; cannot determine free space.");
		delete device;
		return;
	}

	BPartitioningInfo info;
	status = parent->GetPartitioningInfo(&info);
	if (status != B_OK) {
		result.error = B_TRANSLATE("The disk did not report its free "
			"space layout.");
		delete device;
		return;
	}

	int32 count = info.CountPartitionableSpaces();
	if (count < 0)
		count = 0;

	off_t totalFree = 0;
	ExpandVolumeGeometry::SpaceRange* spaces
		= new(std::nothrow) ExpandVolumeGeometry::SpaceRange[count > 0
			? count : 1];
	if (spaces == NULL) {
		result.error = B_TRANSLATE("Out of memory.");
		delete device;
		return;
	}

	for (int32 i = 0; i < count; i++) {
		off_t spaceOffset = 0;
		off_t spaceSize = 0;
		if (info.GetPartitionableSpaceAt(i, &spaceOffset, &spaceSize)
				== B_OK) {
			spaces[i].offset = spaceOffset;
			spaces[i].size = spaceSize;
			totalFree += spaceSize;
		} else {
			spaces[i].offset = -1;
			spaces[i].size = 0;
		}
	}

	ExpandVolumeGeometry::SpaceRange gap;
	bool haveGap = ExpandVolumeGeometry::FindAdjacentTailGap(
		partition->Offset(), partition->Size(), spaces, count, &gap);
	delete[] spaces;

	if (!haveGap) {
		if (totalFree > 0) {
			result.gapExplanation = B_TRANSLATE("There is free space on "
				"this disk, but none of it sits directly behind the "
				"system partition. Growing in place would require moving "
				"partitions; use DriveSetup for that.");
		} else {
			result.gapExplanation = B_TRANSLATE("The disk has no free "
				"space next to the system partition. Nothing to expand "
				"into.");
		}
		delete device;
		return;
	}

	// Ask the kit for the real ceiling: ValidateResize clamps the request
	// to what the disk system and the filesystem will accept.
	off_t proposed = (off_t)(gap.offset + gap.size - partition->Offset());
	status = partition->ValidateResize(&proposed);
	if (status != B_OK) {
		result.gapExplanation = B_TRANSLATE("Free space sits behind the "
			"system partition, but the partition manager refused the "
			"resize.");
		delete device;
		return;
	}
	if (proposed <= partition->Size()) {
		result.gapExplanation = B_TRANSLATE("The free space behind the "
			"system partition is too small to be usable.");
		delete device;
		return;
	}

	result.maxSize = _RoundDownToBlock(proposed, result.blockSize);
	if (result.maxSize <= result.currentSize) {
		result.gapExplanation = B_TRANSLATE("The usable free space rounds "
			"down to nothing at this block size.");
		delete device;
		return;
	}

	delete device;
}


static void*
_ProbeThread(void* data)
{
	ExpandVolumeWindow* window = static_cast<ExpandVolumeWindow*>(data);

	ProbeResult result;
	_ProbeRootPartition(result);

	BMessage message(MSG_PROBE_DONE);
	message.AddString("volumeName", result.volumeName);
	message.AddString("devicePath", result.devicePath);
	message.AddString("fileSystem", result.fileSystem);
	message.AddInt64("partitionID", (int64)result.partitionID);
	message.AddInt64("deviceID", (int64)result.deviceID);
	message.AddInt64("currentSize", result.currentSize);
	message.AddInt64("maxSize", result.maxSize);
	message.AddInt64("blockSize", result.blockSize);
	message.AddInt64("usedBytes", result.usedBytes);
	message.AddInt64("totalBytes", result.totalBytes);
	message.AddBool("mounted", result.mounted);
	message.AddBool("canResizeWhileMounted", result.canResizeWhileMounted);
	message.AddString("error", result.error);
	message.AddString("gapExplanation", result.gapExplanation);

	window->PostMessage(&message);
	return NULL;
}


// #pragma mark - apply worker


static void*
_ApplyThread(void* data)
{
	BMessage* request = static_cast<BMessage*>(data);

	partition_id partitionID
		= (partition_id)request->FindInt64("partitionID");
	off_t newSize = request->FindInt64("newSize");
	int32 generation = request->FindInt32("generation");
	BMessenger target;
	request->FindMessenger("target", &target);
	delete request;

	status_t applyStatus = B_ERROR;
	BString detail;
	BString failedOp;
	BString helperStatus;

	// Re-resolve by ID in the worker: the probe's BDiskDevice stays with
	// the window thread, and the plan helper only needs IDs and paths.
	BDiskDeviceRoster roster;
	BDiskDevice device;
	BPartition* partition = NULL;
	if (roster.GetPartitionWithID(partitionID, &device, &partition)
			!= B_OK || partition == NULL) {
		detail = B_TRANSLATE("The system partition disappeared while "
			"preparing the resize.");
	} else {
		BPartition* parent = partition->Parent();
		if (parent == NULL) {
			detail = B_TRANSLATE("The system partition no longer has a "
				"parent disk.");
		} else {
			// DriveSetup's resize idiom: a ResizeJob on a job queue,
			// executed through the privileged plan helper. The in-kernel
			// _kern_resize_partition is B_NOT_SUPPORTED on vos, so the
			// helper is the only working path.
			PartitionReference* parentRef
				= new PartitionReference(parent->ID());
			PartitionReference* childRef
				= new PartitionReference(partition->ID());

			DiskDeviceJobQueue jobQueue;
			ResizeJob* resizeJob = new ResizeJob(parentRef, childRef,
				newSize, newSize);
			jobQueue.AddJob(resizeJob);

			parentRef->ReleaseReference();
			childRef->ReleaseReference();

			BMessage result;
			applyStatus = jobQueue.ExecuteViaHelper(&device, &result);

			BString statusText;
			if (result.FindString("status", &statusText) == B_OK)
				helperStatus = statusText;
			result.FindString("detail", &detail);
			result.FindString("failed_op", &failedOp);
		}
	}

	BMessage message(MSG_APPLY_DONE);
	message.AddInt32("generation", generation);
	message.AddInt32("status", applyStatus);
	message.AddString("detail", detail);
	message.AddString("failedOp", failedOp);
	message.AddString("helperStatus", helperStatus);

	target.SendMessage(&message);
	return NULL;
}


// #pragma mark - window


ExpandVolumeWindow::ExpandVolumeWindow(bool onlyIfExpandable)
	:
	BWindow(BRect(0, 0, 460, 320), B_TRANSLATE("Expand Volume"),
		B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_QUIT_ON_WINDOW_CLOSE),
	fState(kStateProbing),
	fOnlyIfExpandable(onlyIfExpandable),
	fRootPartitionID(-1),
	fDeviceID(-1),
	fCurrentSize(0),
	fMaxSize(0),
	fBlockSize(512),
	fUsedBytes(-1),
	fTotalBytes(-1),
	fMounted(false),
	fCanResizeWhileMounted(false),
	fCurrentSizeMiB(0),
	fMaxSizeMiB(0),
	fPendingSizeMiB(0),
	fPendingApplySize(0),
	fWorkerLock("expandvolume worker"),
	fApplyGeneration(0),
	fWorkerRunning(false),
	fApplyStartedAt(0)
{
	_BuildControls();
	CenterOnScreen();
	// Stays hidden until the probe finds something to claim.
	if (fOnlyIfExpandable)
		Run();
	else
		Show();

	_StartProbe();
}


ExpandVolumeWindow::~ExpandVolumeWindow()
{
}


void
ExpandVolumeWindow::_BuildControls()
{
	fVolumeView = new BStringView("volume", B_TRANSLATE("Looking for the "
		"system volume..."));
	fVolumeView->SetFont(be_bold_font);

	fDeviceView = new BStringView("device", " ");

	fSizeView = new BStringView("size", " ");
	fUsedView = new BStringView("used", " ");
	fClaimableView = new BStringView("claimable", " ");

	fNoticeView = new BStringView("notice", " ");
	fNoticeView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fNoticeView->Hide();

	fSizeSlider = new BSlider("sizeSlider", B_TRANSLATE("New size"),
		new BMessage(MSG_SIZE_SLIDER), 0, 100, B_HORIZONTAL);
	fSizeSlider->SetExplicitMinSize(
		BSize(280, fSizeSlider->PreferredSize().height));
	fSizeSlider->SetEnabled(false);

	fProgress = new BStatusBar("progress", B_TRANSLATE("Working"));
	fProgress->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fProgress->Hide();

	fUseAllButton = new BButton("useAll", B_TRANSLATE("Use all free space"),
		new BMessage(MSG_USE_ALL));
	fUseAllButton->SetEnabled(false);

	fApplyButton = new BButton("apply", B_TRANSLATE("Apply"),
		new BMessage(MSG_APPLY));
	fApplyButton->SetEnabled(false);

	fCloseButton = new BButton("close", B_TRANSLATE("Close"),
		new BMessage(B_QUIT_REQUESTED));
	fCloseButton->MakeDefault(true);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fVolumeView)
		.Add(fDeviceView)
		.Add(new BSeparatorView(B_HORIZONTAL))
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.Add(fSizeView)
			.Add(fUsedView)
			.Add(fClaimableView)
		.End()
		.Add(fSizeSlider)
		.Add(fProgress)
		.Add(fNoticeView)
		.AddGlue()
		.AddGroup(B_HORIZONTAL)
			.Add(fUseAllButton)
			.AddGlue()
			.Add(fApplyButton)
			.Add(fCloseButton)
		.End();
}


void
ExpandVolumeWindow::_StartProbe()
{
	_SetStateProbing();

	pthread_t thread;
	if (pthread_create(&thread, NULL, _ProbeThread, this) == 0) {
		pthread_detach(thread);
	} else {
		_SetStateNoSpace(B_TRANSLATE("Could not start the probe thread."));
	}
}


void
ExpandVolumeWindow::_ApplyProbeResult(BMessage* message)
{
	const char* text;
	if (message->FindString("volumeName", &text) == B_OK)
		fVolumeName = text;
	if (message->FindString("devicePath", &text) == B_OK)
		fDevicePath = text;
	if (message->FindString("fileSystem", &text) == B_OK)
		fFileSystem = text;
	fRootPartitionID = (partition_id)message->FindInt64("partitionID");
	fDeviceID = (partition_id)message->FindInt64("deviceID");
	fCurrentSize = message->FindInt64("currentSize");
	fMaxSize = message->FindInt64("maxSize");
	fBlockSize = message->FindInt64("blockSize");
	fUsedBytes = message->FindInt64("usedBytes");
	fTotalBytes = message->FindInt64("totalBytes");
	message->FindBool("mounted", &fMounted);
	message->FindBool("canResizeWhileMounted", &fCanResizeWhileMounted);

	BString error;
	message->FindString("error", &error);
	BString gapExplanation;
	message->FindString("gapExplanation", &gapExplanation);

	if (fOnlyIfExpandable
		&& (!error.IsEmpty() || fMaxSize <= fCurrentSize)) {
		be_app->PostMessage(B_QUIT_REQUESTED);
		return;
	}
	if (fOnlyIfExpandable && IsHidden())
		Show();

	fVolumeView->SetText(fVolumeName.String());

	BString deviceLine;
	if (!fFileSystem.IsEmpty())
		deviceLine.SetToFormat("%s  %s", fDevicePath.String(),
			fFileSystem.String());
	else
		deviceLine = fDevicePath;
	fDeviceView->SetText(deviceLine.String());

	_UpdateSizeLabels();

	if (!error.IsEmpty()) {
		// Volume unreadable: degrade to the explanatory no-op pane.
		_SetStateNoSpace(error);
		return;
	}
	if (fMaxSize <= fCurrentSize) {
		if (gapExplanation.IsEmpty())
			_SetStateNoSpace(B_TRANSLATE("There is no free space to "
				"claim."));
		else
			_SetStateNoSpace(gapExplanation);
		return;
	}

	_SetStateReady();

	if (fMounted && !fCanResizeWhileMounted) {
		// Not fatal -- the helper decides -- but set expectations early.
		BString notice;
		notice.SetToFormat(B_TRANSLATE("%s is in use. Growing a mounted "
			"volume only works for filesystems that support it (for "
			"example btrfs); otherwise the partition manager will refuse "
			"and nothing will be changed."), fVolumeName.String());
		fNoticeView->SetText(notice.String());
		fNoticeView->Show();
	}
}


void
ExpandVolumeWindow::_SetStateProbing()
{
	fState = kStateProbing;
	fSizeSlider->SetEnabled(false);
	fUseAllButton->SetEnabled(false);
	fApplyButton->SetEnabled(false);
	fCloseButton->SetEnabled(true);
	fNoticeView->Hide();
	fProgress->Hide();
}


void
ExpandVolumeWindow::_SetStateNoSpace(const BString& reason)
{
	fState = kStateNoSpace;
	fSizeSlider->SetEnabled(false);
	fUseAllButton->SetEnabled(false);
	fApplyButton->SetEnabled(false);
	fCloseButton->SetEnabled(true);
	fProgress->Hide();
	fNoticeView->SetText(reason.String());
	fNoticeView->Show();
}


void
ExpandVolumeWindow::_SetStateReady()
{
	fState = kStateReady;

	fCurrentSizeMiB = BytesToMiB(fCurrentSize);
	fMaxSizeMiB = BytesToMiB(fMaxSize);
	if (fMaxSizeMiB <= fCurrentSizeMiB)
		fMaxSizeMiB = fCurrentSizeMiB + 1;

	fSizeSlider->SetLimits((int32)fCurrentSizeMiB, (int32)fMaxSizeMiB);
	fSizeSlider->SetValue((int32)fMaxSizeMiB);
	fPendingSizeMiB = fMaxSizeMiB;
	fSizeSlider->SetEnabled(true);
	fUseAllButton->SetEnabled(true);
	fApplyButton->SetEnabled(true);
	fCloseButton->SetEnabled(true);
	fProgress->Hide();
}


void
ExpandVolumeWindow::_SetStateBusy()
{
	fState = kStateBusy;
	fSizeSlider->SetEnabled(false);
	fUseAllButton->SetEnabled(false);
	fApplyButton->SetEnabled(false);
	fCloseButton->SetEnabled(false);
	fProgress->Show();
	fProgress->Reset(B_TRANSLATE("Working"));
	fProgress->SetMaxValue(1);
	fProgress->SetTo(0);

	BMessage tick(MSG_TICK);
	PostMessage(&tick);
}


void
ExpandVolumeWindow::_SetStateDone(const BString& summary)
{
	fState = kStateDone;
	fSizeSlider->SetEnabled(false);
	fUseAllButton->SetEnabled(false);
	fApplyButton->SetEnabled(false);
	fCloseButton->SetEnabled(true);
	fProgress->Hide();
	fNoticeView->SetText(summary.String());
	fNoticeView->Show();

	BAlert* alert = new BAlert(B_TRANSLATE("Expand volume"), summary.String(),
		B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_INFO_ALERT);
	alert->Go(NULL);

	// The grow succeeded; keep the read-only lines honest.
	fCurrentSize = fPendingApplySize > 0 ? fPendingApplySize : fMaxSize;
	_UpdateSizeLabels();
}


void
ExpandVolumeWindow::_SetStateError(const BString& detail)
{
	fState = kStateError;
	fSizeSlider->SetEnabled(false);
	fUseAllButton->SetEnabled(false);
	fApplyButton->SetEnabled(false);
	fCloseButton->SetEnabled(true);
	fProgress->Hide();
	fNoticeView->SetText(detail.String());
	fNoticeView->Show();

	BAlert* alert = new BAlert(B_TRANSLATE("Expand volume"), detail.String(),
		B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->Go(NULL);
}


void
ExpandVolumeWindow::_UpdateSizeLabels()
{
	char buffer[64];

	BString sizeLine;
	sizeLine.SetToFormat(B_TRANSLATE("Current size: %s"),
		_FormatSize(fCurrentSize, buffer, sizeof(buffer)));
	fSizeView->SetText(sizeLine.String());

	if (fUsedBytes >= 0 && fTotalBytes > 0) {
		char usedBuffer[64];
		char totalBuffer[64];
		BString usedLine;
		usedLine.SetToFormat(B_TRANSLATE("Used on this volume: %s of %s"),
			_FormatSize(fUsedBytes, usedBuffer, sizeof(usedBuffer)),
			_FormatSize(fTotalBytes, totalBuffer, sizeof(totalBuffer)));
		fUsedView->SetText(usedLine.String());
	} else {
		fUsedView->SetText(B_TRANSLATE("Used: not readable"));
	}

	if (fMaxSize > fCurrentSize) {
		char claimBuffer[64];
		BString claimLine;
		claimLine.SetToFormat(B_TRANSLATE("Available to claim: up to %s"),
			_FormatSize(fMaxSize - fCurrentSize, claimBuffer,
				sizeof(claimBuffer)));
		fClaimableView->SetText(claimLine.String());
	} else {
		fClaimableView->SetText(B_TRANSLATE("Available to claim: none"));
	}
}


void
ExpandVolumeWindow::_StartApply()
{
	if (fState != kStateReady && fState != kStateError)
		return;

	off_t requested = MiBToBytes(fPendingSizeMiB);
	if (requested <= fCurrentSize || requested > fMaxSize)
		requested = fMaxSize;

	char currentBuffer[64];
	char requestedBuffer[64];
	BString question;
	question.SetToFormat(B_TRANSLATE("Grow the system partition from %s "
		"to %s? A privileged helper will ask you to confirm."),
		_FormatSize(fCurrentSize, currentBuffer, sizeof(currentBuffer)),
		_FormatSize(requested, requestedBuffer, sizeof(requestedBuffer)));

	BAlert* alert = new(std::nothrow) BAlert(
		B_TRANSLATE("Expand Volume"), question.String(),
		B_TRANSLATE("Cancel"), B_TRANSLATE("Grow"), NULL,
		B_WIDTH_AS_USUAL, B_INFO_ALERT);
	if (alert == NULL)
		return;
	alert->SetShortcut(0, B_ESCAPE);
	if (alert->Go() != 1)
		return;

	fPendingApplySize = requested;

	int32 generation;
	{
		BAutolock lock(&fWorkerLock);
		fApplyGeneration++;
		generation = fApplyGeneration;
		fWorkerRunning = true;
	}

	fApplyStartedAt = system_time();
	_SetStateBusy();

	BMessage* request = new BMessage();
	request->AddInt64("deviceID", (int64)fDeviceID);
	request->AddInt64("partitionID", (int64)fRootPartitionID);
	request->AddInt64("newSize", requested);
	request->AddInt32("generation", generation);
	request->AddMessenger("target", BMessenger(this));

	pthread_t thread;
	if (pthread_create(&thread, NULL, _ApplyThread, request) == 0) {
		pthread_detach(thread);
	} else {
		delete request;
		BAutolock lock(&fWorkerLock);
		fWorkerRunning = false;
		_SetStateError(B_TRANSLATE("Could not start the resize worker."));
	}
}


void
ExpandVolumeWindow::_FinishApply(BMessage* message)
{
	int32 generation = message->FindInt32("generation");
	int32 status = message->FindInt32("status");
	BString detail;
	message->FindString("detail", &detail);
	BString failedOp;
	message->FindString("failedOp", &failedOp);
	BString helperStatus;
	message->FindString("helperStatus", &helperStatus);

	{
		BAutolock lock(&fWorkerLock);
		if (generation != fApplyGeneration)
			return;		// stale worker from a timed-out attempt
		fWorkerRunning = false;
	}

	if (status == B_OK && helperStatus == "ok") {
		char beforeBuffer[64];
		char afterBuffer[64];
		BString summary;
		summary.SetToFormat(B_TRANSLATE("Done: the partition is now %s "
			"(was %s). A reboot is optional."),
			_FormatSize(fPendingApplySize, afterBuffer, sizeof(afterBuffer)),
			_FormatSize(fCurrentSize, beforeBuffer, sizeof(beforeBuffer)));
		_SetStateDone(summary);
		_NotifySuccess();
		_WriteMarker();
		return;
	}

	// Graceful failure surface: the helper's own words when we have
	// them, otherwise the errno-style status from the queue.
	BString text;
	if (!detail.IsEmpty())
		text = detail;
	else if (status == B_PERMISSION_DENIED)
		text = B_TRANSLATE("The request was denied; nothing was changed.");
	else if (status == B_NOT_SUPPORTED)
		text = B_TRANSLATE("This volume cannot be resized right now (the "
			"filesystem may be in use, or resize support may be missing).");
	else
		text = B_TRANSLATE("The resize failed; nothing was changed.");

	if (!failedOp.IsEmpty() && failedOp != "validate") {
		BString withOp;
		withOp.SetToFormat("%s (%s)", text.String(), failedOp.String());
		text = withOp;
	}

	// The mounted-root case deserves a concrete remediation hint.
	if (fMounted && !fCanResizeWhileMounted) {
		text << " " << B_TRANSLATE("The volume is in use; unmount it "
			"(or grow a btrfs volume, which supports online growth) and "
			"try again.");
	}

	_SetStateError(text);
	fprintf(stderr, "ExpandVolume: resize failed (status=%d, "
		"helperStatus=%s, failed_op=%s): %s\n", (int)status,
		helperStatus.String(), failedOp.String(), text.String());
}


void
ExpandVolumeWindow::_NotifySuccess()
{
	BNotification* notification
		= new(std::nothrow) BNotification(B_INFORMATION_NOTIFICATION);
	if (notification == NULL)
		return;

	notification->SetGroup(BString(B_TRANSLATE_SYSTEM_NAME("ExpandVolume")));
	notification->SetTitle(B_TRANSLATE("Partition expanded"));
	char buffer[64];
	notification->SetContent(BString()
		<< B_TRANSLATE("The system partition now uses the free space. "
			"A reboot is optional.")
		<< " " << _FormatSize(fPendingApplySize, buffer, sizeof(buffer)));
	notification->Send();
	delete notification;

	// DiskUsage caches its scan; a relaunch refreshes it if it is open.
	be_roster->Launch(kDiskUsageSignature);
}


void
ExpandVolumeWindow::_WriteMarker()
{
	WriteExpandVolumeDoneMarker();
}


void
ExpandVolumeWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_PROBE_DONE:
			_ApplyProbeResult(message);
			break;

		case MSG_APPLY_DONE:
			_FinishApply(message);
			break;

		case MSG_APPLY_TIMEOUT:
		{
			int32 generation = message->FindInt32("generation");
			BAutolock lock(&fWorkerLock);
			if (generation == fApplyGeneration && fWorkerRunning) {
				// Leave the worker alone; just stop holding the window
				// hostage. A late MSG_APPLY_DONE still wins the UI.
				fCloseButton->SetEnabled(true);
				_SetStateError(B_TRANSLATE("The resize is taking too "
					"long. You can close this window; if the helper "
					"finishes later, the result will still be shown."));
			}
			break;
		}

		case MSG_TICK:
		{
			if (fState != kStateBusy)
				break;
			bigtime_t elapsed = system_time() - fApplyStartedAt;
			BString text;
			text.SetToFormat(B_TRANSLATE("Working -- %ld s (the helper "
				"may ask for confirmation)"), (long)(elapsed / 1000000));
			fProgress->SetText(text.String());

			if (elapsed < kApplyTimeout) {
				BMessage tick(MSG_TICK);
				PostMessage(&tick, this);
			} else {
				BMessage timeout(MSG_APPLY_TIMEOUT);
				int32 generation;
				{
					BAutolock lock(&fWorkerLock);
					generation = fApplyGeneration;
				}
				timeout.AddInt32("generation", generation);
				PostMessage(&timeout);
			}
			break;
		}

		case MSG_SIZE_SLIDER:
			fPendingSizeMiB = fSizeSlider->Value();
			break;

		case MSG_USE_ALL:
			fSizeSlider->SetValue((int32)fMaxSizeMiB);
			fPendingSizeMiB = fMaxSizeMiB;
			break;

		case MSG_APPLY:
			_StartApply();
			break;

		case MSG_SHOW_WINDOW:
			if (IsHidden())
				Show();
			Activate();
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


bool
ExpandVolumeWindow::QuitRequested()
{
	// Any completed look at the popup (including the no-space pane)
	// counts as "seen": do not raise it again on the next TryNow boot
	// when the live home persists.
	if (!fOnlyIfExpandable || !IsHidden())
		_WriteMarker();
	return true;
}
