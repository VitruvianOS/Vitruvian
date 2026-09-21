/*
 * Copyright 2002-2013 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Ithamar R. Adema <ithamar@unet.nl>
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Axel Dörfler, axeld@pinc-software.de.
 *		Erik Jaesler <ejakowatz@users.sourceforge.net>
 *		Ingo Weinhold <ingo_weinhold@gmx.de>
 */


#include "MainWindow.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <Alert.h>
#include <AppFileInfo.h>
#include <Application.h>
#include <Catalog.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <ControlLook.h>
#include <Debug.h>
#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <DiskDeviceVisitor.h>
#include <DiskDeviceTypes.h>
#include <DiskSystem.h>
#include <IconUtils.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MenuItem.h>
#include <MenuBar.h>
#include <Menu.h>
#include <Messenger.h>
#include <MountServer.h>
#include <Path.h>
#include <Partition.h>
#include <PartitioningInfo.h>
#include <Roster.h>
#include <Screen.h>
#include <ScrollBar.h>
#include <NodeMonitor.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <fs_volume.h>
#include <tracker_private.h>

#include "ChangeParametersPanel.h"
#include "ColumnListView.h"
#include "CreateParametersPanel.h"
#include "DiskView.h"
#include "FeaturesWindow.h"
#include "FlagsPanel.h"
#include "InitParametersPanel.h"
#include "PartitionList.h"
#include "ProgressWindow.h"
#include "RenamePanel.h"
#include "ResizeMoveWindow.h"
#include "Support.h"

#include "DiskDeviceJobQueue.h"
#include "MoveJob.h"
#include "PartitionCapabilities.h"
#include "PartitionReference.h"
#include "RepairJob.h"
#include "ResizeJob.h"
#include "SetFlagsJob.h"
#include "SetStringJob.h"
#include "UninitializeJob.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MainWindow"


enum {
	MSG_MOUNT_ALL				= 'mnta',
	MSG_MOUNT					= 'mnts',
	MSG_UNMOUNT					= 'unmt',
	MSG_FORMAT					= 'frmt',
	MSG_CREATE					= 'crtp',
	MSG_CHANGE					= 'chgp',
	MSG_RESIZE_MOVE				= 'rszm',
	MSG_SET_FLAGS				= 'sflg',
	MSG_CHECK					= 'chck',
	MSG_ERASE					= 'eras',
	MSG_RENAME					= 'renm',
	MSG_RENAME_GPT				= 'rngp',
	MSG_INITIALIZE				= 'init',
	MSG_DELETE					= 'delt',
	MSG_EJECT					= 'ejct',
	MSG_OPEN_DISKPROBE			= 'opdp',
	MSG_SURFACE_TEST			= 'sfct',
	MSG_RESCAN					= 'rscn',
	MSG_REGISTER				= 'regi',
	MSG_UNREGISTER				= 'unrg',
	MSG_SAVE					= 'save',
	MSG_WRITE					= 'writ',
	MSG_COMPLETE				= 'comp',
	MSG_SHOW_FEATURES			= 'sftr',

	MSG_PARTITION_ROW_SELECTED	= 'prsl',
};


class ListPopulatorVisitor : public BDiskDeviceVisitor {
public:
	ListPopulatorVisitor(PartitionListView* list, int32& diskCount,
			SpaceIDMap& spaceIDMap)
		:
		fPartitionList(list),
		fDiskCount(diskCount),
		fSpaceIDMap(spaceIDMap)
	{
		fDiskCount = 0;
		// Deliberately not cleared; see _ScanDrives().
		// start with an empty list
		int32 rows = fPartitionList->CountRows();
		for (int32 i = rows - 1; i >= 0; i--) {
			BRow* row = fPartitionList->RowAt(i);
			fPartitionList->RemoveRow(row);
			delete row;
		}
	}

	virtual bool Visit(BDiskDevice* device)
	{
		fDiskCount++;
		// if we don't prepare the device for modifications,
		// we cannot get information about available empty
		// regions on the device or child partitions
		device->PrepareModifications();
		_AddPartition(device);
		return false; // Don't stop yet!
	}

	virtual bool Visit(BPartition* partition, int32 level)
	{
		_AddPartition(partition);
		return false; // Don't stop yet!
	}

private:
	void _AddPartition(BPartition* partition) const
	{
		// add the partition itself
		fPartitionList->AddPartition(partition);

		if (getenv("VOS_DEBUG_MENUS") != NULL) {
			BPath path;
			partition->GetPath(&path);
			fprintf(stderr, "[DriveSetup rows] %-12s id=%" B_PRId32
				" parentID=%" B_PRId32 " contentType=%s isDevice=%d "
				"containsPS=%d containsFS=%d\n",
				path.Path() ? path.Path() : "(no path)",
				partition->ID(),
				partition->Parent() ? partition->Parent()->ID() : -1,
				partition->ContentType() ? partition->ContentType() : "(null)",
				partition->IsDevice(),
				partition->ContainsPartitioningSystem(),
				partition->ContainsFileSystem());
		}

		// add any available space on it
		BPartitioningInfo info;
		status_t status = partition->GetPartitioningInfo(&info);
		if (status >= B_OK) {
			partition_id parentID = partition->ID();
			off_t offset;
			off_t size;
			for (int32 i = 0;
					info.GetPartitionableSpaceAt(i, &offset, &size) >= B_OK;
					i++) {
				// TODO: remove again once Disk Device API is fixed
				if (!is_valid_partitionable_space(size))
					continue;
				//
				partition_id id = fSpaceIDMap.SpaceIDFor(parentID, offset);
				if (getenv("VOS_DEBUG_MENUS") != NULL) {
					fprintf(stderr, "[DriveSetup rows]   space id=%" B_PRId32
						" parentID=%" B_PRId32 " offset=%" B_PRIdOFF
						" size=%" B_PRIdOFF "\n", id, parentID, offset, size);
				}
				fPartitionList->AddSpace(parentID, id, offset, size);
			}
		}
	}

	PartitionListView*	fPartitionList;
	int32&				fDiskCount;
	SpaceIDMap&			fSpaceIDMap;
	BDiskDevice*		fLastPreparedDevice;
};


class ModificationPreparer {
public:
	ModificationPreparer(BDiskDevice* disk)
		:
		fDisk(disk),
		fModificationStatus(fDisk->PrepareModifications())
	{
	}
	~ModificationPreparer()
	{
		if (fModificationStatus == B_OK)
			fDisk->CancelModifications();
	}
	status_t ModificationStatus() const
	{
		return fModificationStatus;
	}
	status_t CommitModifications(BMessage* outResult = NULL)
	{
		status_t status = fDisk->CommitModifications(true, BMessenger(),
			true, outResult);
		if (status == B_OK)
			fModificationStatus = B_ERROR;

		return status;
	}

private:
	BDiskDevice*	fDisk;
	status_t		fModificationStatus;
};


// #pragma mark -


MainWindow::MainWindow()
	:
	BWindow(BRect(50, 50, 600, 500), B_TRANSLATE_SYSTEM_NAME("DriveSetup"),
		B_DOCUMENT_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fCurrentDisk(NULL),
	fCurrentPartitionID(-1),
	fSpaceIDMap()
{
	fMenuBar = new BMenuBar("root menu");

	// create all the menu items
	fWipeMenuItem = new BMenuItem(B_TRANSLATE("Wipe (not implemented)"),
		new BMessage(MSG_FORMAT));
	fEjectMenuItem = new BMenuItem(B_TRANSLATE("Eject"),
		new BMessage(MSG_EJECT), 'E');
	fOpenDiskProbeMenuItem = new BMenuItem(B_TRANSLATE("Open with DiskProbe"),
		new BMessage(MSG_OPEN_DISKPROBE));

	fSurfaceTestMenuItem = new BMenuItem(
		B_TRANSLATE("Surface test (not implemented)"),
		new BMessage(MSG_SURFACE_TEST));
	fRescanMenuItem = new BMenuItem(B_TRANSLATE("Rescan"),
		new BMessage(MSG_RESCAN));

	fCreateMenuItem = new BMenuItem(B_TRANSLATE("Create" B_UTF8_ELLIPSIS),
		new BMessage(MSG_CREATE), 'C');
	fChangeMenuItem = new BMenuItem(
		B_TRANSLATE("Change parameters" B_UTF8_ELLIPSIS),
		new BMessage(MSG_CHANGE));
	fResizeMenuItem = new BMenuItem(
		B_TRANSLATE("Resize/Move" B_UTF8_ELLIPSIS),
		new BMessage(MSG_RESIZE_MOVE));
	fFlagsMenuItem = new BMenuItem(
		B_TRANSLATE("Flags" B_UTF8_ELLIPSIS),
		new BMessage(MSG_SET_FLAGS));
	fCheckMenuItem = new BMenuItem(
		B_TRANSLATE("Check filesystem"),
		new BMessage(MSG_CHECK));
	fEraseMenuItem = new BMenuItem(
		B_TRANSLATE("Erase filesystem"),
		new BMessage(MSG_ERASE));
	fRenameMenuItem = new BMenuItem(
		B_TRANSLATE("Rename" B_UTF8_ELLIPSIS),
		new BMessage(MSG_RENAME));
	fRenameGptMenuItem = new BMenuItem(
		B_TRANSLATE("Rename partition" B_UTF8_ELLIPSIS),
		new BMessage(MSG_RENAME_GPT));
	fDeleteMenuItem = new BMenuItem(B_TRANSLATE("Delete"),
		new BMessage(MSG_DELETE), 'D');

	fMountMenuItem = new BMenuItem(B_TRANSLATE("Mount"),
		new BMessage(MSG_MOUNT), 'M');
	fUnmountMenuItem = new BMenuItem(B_TRANSLATE("Unmount"),
		new BMessage(MSG_UNMOUNT), 'U');
	fMountAllMenuItem = new BMenuItem(B_TRANSLATE("Mount all"),
		new BMessage(MSG_MOUNT_ALL), 'M', B_SHIFT_KEY);

	fRegisterMenuItem = new BMenuItem(
		B_TRANSLATE("Register disk image" B_UTF8_ELLIPSIS),
		new BMessage(MSG_REGISTER));
	fUnRegisterMenuItem = new BMenuItem(
		B_TRANSLATE("Unregister disk image"),
		new BMessage(MSG_UNREGISTER));
	fSaveMenuItem = new BMenuItem(
		B_TRANSLATE("Save image" B_UTF8_ELLIPSIS),
		new BMessage(MSG_SAVE));
	fWriteMenuItem = new BMenuItem(
		B_TRANSLATE("Write image" B_UTF8_ELLIPSIS),
		new BMessage(MSG_WRITE));

	fFeaturesMenuItem = new BMenuItem(
		B_TRANSLATE("Filesystem features" B_UTF8_ELLIPSIS),
		new BMessage(MSG_SHOW_FEATURES));

	// Disk menu
	fDiskMenu = new BMenu(B_TRANSLATE("Disk"));

	// fDiskMenu->AddItem(fWipeMenuItem);
	fDiskInitMenu = new BMenu(B_TRANSLATE("Initialize"));
	fDiskMenu->AddItem(fDiskInitMenu);

	fDiskMenu->AddSeparatorItem();

	fDiskMenu->AddItem(fEjectMenuItem);
	// fDiskMenu->AddItem(fSurfaceTestMenuItem);
	fDiskMenu->AddItem(fRescanMenuItem);

	fMenuBar->AddItem(fDiskMenu);

	// Parition menu
	fPartitionMenu = new BMenu(B_TRANSLATE("Partition"));
	fPartitionMenu->AddItem(fCreateMenuItem);

	fFormatMenu = new BMenu(B_TRANSLATE("Format"));
	fPartitionMenu->AddItem(fFormatMenu);

	fPartitionMenu->AddItem(fChangeMenuItem);
	fPartitionMenu->AddItem(fResizeMenuItem);
	fPartitionMenu->AddItem(fRenameMenuItem);
	fPartitionMenu->AddItem(fCheckMenuItem);
	fPartitionMenu->AddItem(fEraseMenuItem);
	fPartitionMenu->AddItem(fDeleteMenuItem);

	fPartitionMenu->AddSeparatorItem();

	fPartitionMenu->AddItem(fMountMenuItem);
	fPartitionMenu->AddItem(fUnmountMenuItem);

	fPartitionMenu->AddSeparatorItem();

	fPartitionMenu->AddItem(fMountAllMenuItem);

	fPartitionMenu->AddSeparatorItem();

	fPartitionMenu->AddItem(fOpenDiskProbeMenuItem);
	fPartitionMenu->AddItem(fFlagsMenuItem);
	fMenuBar->AddItem(fPartitionMenu);

	// Disk image menu
	fDiskImageMenu = new BMenu(B_TRANSLATE("Disk image"));

	fDiskImageMenu->AddItem(fRegisterMenuItem);
	fDiskImageMenu->AddItem(fUnRegisterMenuItem);

	fDiskImageMenu->AddSeparatorItem();

	fDiskImageMenu->AddItem(fSaveMenuItem);
	fDiskImageMenu->AddItem(fWriteMenuItem);

	fMenuBar->AddItem(fDiskImageMenu);

	fToolsMenu = new BMenu(B_TRANSLATE("Tools"));
	fToolsMenu->AddItem(fFeaturesMenuItem);
	fMenuBar->AddItem(fToolsMenu);

	BGroupLayout* layout = new BGroupLayout(B_VERTICAL, 0);
	layout->SetInsets(-1, 0, -1, -1);
	SetLayout(layout);
	AddChild(fMenuBar);

	// Partition / Drives context menu
	fContextMenu = new BPopUpMenu("Partition", false, false);
	fCreateContextMenuItem = new BMenuItem(B_TRANSLATE("Create" B_UTF8_ELLIPSIS),
		new BMessage(MSG_CREATE), 'C');
	fChangeContextMenuItem = new BMenuItem(
		B_TRANSLATE("Change parameters" B_UTF8_ELLIPSIS),
		new BMessage(MSG_CHANGE));
	fDeleteContextMenuItem = new BMenuItem(B_TRANSLATE("Delete"),
		new BMessage(MSG_DELETE), 'D');
	fMountContextMenuItem = new BMenuItem(B_TRANSLATE("Mount"),
		new BMessage(MSG_MOUNT), 'M');
	fUnmountContextMenuItem = new BMenuItem(B_TRANSLATE("Unmount"),
		new BMessage(MSG_UNMOUNT), 'U');
	fOpenDiskProbeContextMenuItem = new BMenuItem(B_TRANSLATE("Open with DiskProbe"),
		new BMessage(MSG_OPEN_DISKPROBE));
	fFormatContextMenuItem = new BMenu(B_TRANSLATE("Format"));

	fContextMenu->AddItem(fCreateContextMenuItem);
	fContextMenu->AddItem(fFormatContextMenuItem);
	fContextMenu->AddItem(fChangeContextMenuItem);
	fContextMenu->AddItem(fDeleteContextMenuItem);
	fContextMenu->AddSeparatorItem();
	fContextMenu->AddItem(fMountContextMenuItem);
	fContextMenu->AddItem(fUnmountContextMenuItem);
	fContextMenu->AddSeparatorItem();
	fContextMenu->AddItem(fOpenDiskProbeContextMenuItem);
	fContextMenu->SetTargetForItems(this);

	// add DiskView
	fDiskView = new DiskView(fSpaceIDMap);
	AddChild(fDiskView);

	// add PartitionListView
	fListView = new PartitionListView();
	AddChild(fListView);

	// configure PartitionListView
	fListView->SetSelectionMode(B_SINGLE_SELECTION_LIST);
	fListView->SetSelectionMessage(new BMessage(MSG_PARTITION_ROW_SELECTED));
	fListView->SetTarget(this);
	fListView->MakeFocus(true);

	fRegisterFilePanel = NULL;
	fWriteImageFilePanel = NULL;
	fReadImageFilePanel = NULL;

	fNotification = new BNotification(B_INFORMATION_NOTIFICATION);
	fNotification->SetGroup(BString(B_TRANSLATE_SYSTEM_NAME("DriveSetup")));

	app_info info;
	be_roster->GetAppInfo("application/x-vnd.Haiku-DriveSetup", &info);
	BBitmap icon(BRect(0, 0, 32, 32), B_RGBA32);
	BNode node(&info.ref);
	if (BIconUtils::GetVectorIcon(&node, "BEOS:ICON", &icon) == B_OK)
		fNotification->SetIcon(&icon);

	status_t status = fDiskDeviceRoster.StartWatching(BMessenger(this));
	if (status != B_OK) {
		fprintf(stderr, "Failed to start watching for device changes: %s\n",
			strerror(status));
	}

	// Mounts are not device events; B_WATCH_MOUNT is a separate registration.
	status = watch_node(NULL, B_WATCH_MOUNT, BMessenger(this));
	if (status != B_OK) {
		fprintf(stderr, "Failed to start watching for mount changes: %s\n",
			strerror(status));
	}

	// visit all disks in the system and show their contents
	_ScanDrives();

	// If DeskBar isn't running, DriveSetup was probably started from Installer.
	// Make sure to show on all workspaces, so the window doesn't disappear when using the
	// workspace switch shortcut.
	// Also make it not minimizable, because without DeskBar, there is no way to restore a
	// minimized window.
	if (!be_roster->IsRunning(kDeskbarSignature)) {
		SetFlags(Flags() | B_NOT_MINIMIZABLE);
		SetWorkspaces(B_ALL_WORKSPACES);
	}
}


MainWindow::~MainWindow()
{
	BDiskDeviceRoster().StopWatching(this);
	delete fCurrentDisk;
	delete fContextMenu;

	delete fRegisterFilePanel;
	delete fReadImageFilePanel;
	delete fWriteImageFilePanel;

	delete fNotification;
}


void
MainWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_MOUNT_ALL:
			_MountAll();
			break;
		case MSG_MOUNT:
			_Mount(fCurrentDisk, fCurrentPartitionID);
			break;
		case MSG_UNMOUNT:
			_Unmount(fCurrentDisk, fCurrentPartitionID);
			break;

		case MSG_FORMAT:
			printf("MSG_FORMAT\n");
			break;

		case MSG_CREATE:
			_Create(fCurrentDisk, fCurrentPartitionID);
			break;

		case MSG_SHOW_FEATURES:
			_ShowFeatures();
			break;

		case MSG_INITIALIZE: {
			BString diskSystemName;
			if (message->FindString("disk system", &diskSystemName) != B_OK)
				break;
			_Initialize(fCurrentDisk, fCurrentPartitionID, diskSystemName);
			break;
		}

		case MSG_CHANGE:
			_ChangeParameters(fCurrentDisk, fCurrentPartitionID);
			break;

		case MSG_RESIZE_MOVE:
			_ResizeMove(fCurrentDisk, fCurrentPartitionID);
			break;

		case MSG_SET_FLAGS:
			_SetFlags(fCurrentDisk, fCurrentPartitionID);
			break;

		case MSG_CHECK:
			_CheckFilesystem(fCurrentDisk, fCurrentPartitionID);
			break;

		case MSG_ERASE:
			_Erase(fCurrentDisk, fCurrentPartitionID);
			break;

		case MSG_RENAME:
			_Rename(fCurrentDisk, fCurrentPartitionID);
			break;

		case MSG_RENAME_GPT:
			_RenameGpt(fCurrentDisk, fCurrentPartitionID);
			break;

		case MSG_DELETE:
			_Delete(fCurrentDisk, fCurrentPartitionID);
			break;

		case MSG_EJECT:
			// TODO: completely untested, especially interesting
			// if partition list behaves when partitions disappear
			if (fCurrentDisk) {
				// TODO: only if no partitions are mounted anymore?
				fCurrentDisk->Eject(true);
				_ScanDrives();
			}
			break;
		case MSG_OPEN_DISKPROBE:
		{
			PartitionListRow* row = dynamic_cast<PartitionListRow*>(
				fListView->CurrentSelection());
			const char* args[] = { row->DevicePath(), NULL };

			be_roster->Launch("application/x-vnd.Haiku-DiskProbe", 1,
				(char**)args);

			break;
		}
		case MSG_SURFACE_TEST:
			printf("MSG_SURFACE_TEST\n");
			break;

		case B_NODE_MONITOR:
		{
			int32 opcode;
			if (message->FindInt32("opcode", &opcode) == B_OK
				&& (opcode == B_DEVICE_MOUNTED
					|| opcode == B_DEVICE_UNMOUNTED)) {
				_ScanDrives();
			}
			break;
		}

		// TODO: this could probably be done better!
		case B_DEVICE_UPDATE:
			printf("B_DEVICE_UPDATE\n");
		case MSG_RESCAN:
			_ScanDrives();
			break;

		case MSG_PARTITION_ROW_SELECTED: {
			// selection of partitions via list view
			_AdaptToSelectedPartition();

			BPoint where;
			uint32 buttons;
			fListView->GetMouse(&where, &buttons);
			where.x += 2; // to prevent occasional select
			if (buttons & B_SECONDARY_MOUSE_BUTTON)
				fContextMenu->Go(fListView->ConvertToScreen(where),
					true, false, true);
			break;
		}
		case MSG_SELECTED_PARTITION_ID: {
			// selection of partitions via disk view
			partition_id id;
			if (message->FindInt32("partition_id", &id) == B_OK) {
				if (BRow* row = fListView->FindRow(id)) {
					fListView->DeselectAll();
					fListView->AddToSelection(row);
					_AdaptToSelectedPartition();
				}
			}
			BPoint where;
			uint32 buttons;
			fListView->GetMouse(&where, &buttons);
			where.x += 2; // to prevent occasional select
			if (buttons & B_SECONDARY_MOUSE_BUTTON)
				fContextMenu->Go(fListView->ConvertToScreen(where),
					true, false, true);
			break;
		}

		case MSG_REGISTER:
		{
			entry_ref entryRef;
			message->FindRef("refs", &entryRef);
			BEntry entry(&entryRef);
			BPath path;
			if (entry.GetPath(&path) == B_OK) {
				RegisterFileDiskDevice(path.Path());
				break;
			}

			if (fRegisterFilePanel == NULL) {
				fRegisterFilePanel = new(std::nothrow) BFilePanel(B_OPEN_PANEL,
					new BMessenger(this), NULL, B_FILE_NODE, false,
					new BMessage(MSG_REGISTER), NULL, true);
				BString title(B_TRANSLATE("%appname%: Register disk image"));
				title.ReplaceFirst("%appname%", B_TRANSLATE_SYSTEM_NAME("DriveSetup"));
				fRegisterFilePanel->Window()->SetTitle(title);
				fRegisterFilePanel->SetButtonLabel(B_DEFAULT_BUTTON, B_TRANSLATE("Register"));
			}

			if (fRegisterFilePanel != NULL)
				fRegisterFilePanel->Show();
			break;
		}
		case MSG_UNREGISTER:
		{
			BPath path;
			fCurrentDisk->GetPath(&path);
			UnRegisterFileDiskDevice(path.Path());
			break;
		}
		case MSG_WRITE:
		{
			entry_ref entryRef;
			message->FindRef("refs", &entryRef);
			BEntry entry(&entryRef);
			BPath path;
			if (entry.GetPath(&path) == B_OK) {
				BMessage* msg = new BMessage();

				PartitionListRow* row = dynamic_cast<PartitionListRow*>(
					fListView->CurrentSelection());

				msg->AddString("source", path.Path());
				msg->AddString("target", row->DevicePath());
				msg->AddMessenger("messenger", BMessenger(this));

				thread_id write_thread = spawn_thread(WriteDiskImage,
					"WriteDiskImage", B_LOW_PRIORITY, (void*)msg);
				resume_thread(write_thread);
			} else {
				if (fWriteImageFilePanel == NULL) {
					fWriteImageFilePanel = new(std::nothrow) BFilePanel(
						B_OPEN_PANEL, new BMessenger(this), NULL, B_FILE_NODE,
						false, new BMessage(MSG_WRITE), NULL, true);
					BString title(B_TRANSLATE("%appname%: Select disk image"));
					title.ReplaceFirst("%appname%", B_TRANSLATE_SYSTEM_NAME("DriveSetup"));
					fWriteImageFilePanel->Window()->SetTitle(title);
					fWriteImageFilePanel->SetButtonLabel(B_DEFAULT_BUTTON, B_TRANSLATE("Write"));
				}
				if (fWriteImageFilePanel != NULL)
					fWriteImageFilePanel->Show();
			}
			break;
		}
		case MSG_SAVE:
		{
			PartitionListRow* row = dynamic_cast<PartitionListRow*>(
				fListView->CurrentSelection());

			if (fReadImageFilePanel == NULL) {
				fReadImageFilePanel = new(std::nothrow) BFilePanel(B_SAVE_PANEL,
					new BMessenger(this), NULL, B_FILE_NODE, false, NULL, NULL,
					true);
				BString title(B_TRANSLATE("%appname%: Save disk image"));
				title.ReplaceFirst("%appname%", B_TRANSLATE_SYSTEM_NAME("DriveSetup"));
				fReadImageFilePanel->Window()->SetTitle(title);
			}

			if (fReadImageFilePanel != NULL) {
				BStringField* field = (BStringField*)row->GetField(2);
				fReadImageFilePanel->SetSaveText(field->String());
				fReadImageFilePanel->Show();
			}
			break;
		}
		case B_SAVE_REQUESTED:
		{
			BMessage* msg = new BMessage();

			entry_ref entryRef;
			message->FindRef("directory", &entryRef);
			BEntry entry(&entryRef);
			BPath path;
			entry.GetPath(&path);
			msg->AddString("targetfolder", path.Path());
			const char* name;
			message->FindString("name", &name);
			path.Append(name);

			PartitionListRow* row = dynamic_cast<PartitionListRow*>(
				fListView->CurrentSelection());

			msg->AddString("source", row->DevicePath());
			msg->AddString("target", path.Path());
			msg->AddInt32("partitionID", row->ID());
			msg->AddMessenger("messenger", BMessenger(this));

			thread_id save_thread = spawn_thread(SaveDiskImage, "SaveDiskImage",
				B_LOW_PRIORITY, (void*)msg);
			resume_thread(save_thread);

			break;
		}
		case MSG_COMPLETE:
		{
			const char* status = "";
			const char* jobid = "";

			message->FindString("status", &status);
			message->FindString("jobid", &jobid);

			fNotification->SetTitle(BString(B_TRANSLATE("Disk image")));
			fNotification->SetContent(BString(status));
			fNotification->SetMessageID(BString(jobid));
			fNotification->Send();

			break;
		}

		case MSG_UPDATE_ZOOM_LIMITS:
			_UpdateWindowZoomLimits();
			break;

		case B_REFS_RECEIVED:
		{
			entry_ref ref;
			int32 i = 0;

			while (message->FindRef("refs", i++, &ref) == B_OK) {
				BEntry entry(&ref, true);
				BPath path(&entry);
				BNode node(&entry);

				if (node.IsFile()) {
					if (entry.GetPath(&path) == B_OK)
						RegisterFileDiskDevice(path.Path());
				}
			}
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
MainWindow::QuitRequested()
{
	// TODO: ask about any unsaved changes
	be_app->PostMessage(B_QUIT_REQUESTED);
	Hide();
	return false;
}


// #pragma mark -


status_t
MainWindow::StoreSettings(BMessage* archive) const
{
	if (archive->ReplaceRect("window frame", Frame()) < B_OK)
		archive->AddRect("window frame", Frame());

	BMessage columnSettings;
	fListView->SaveState(&columnSettings);
	if (archive->ReplaceMessage("column settings", &columnSettings) < B_OK)
		archive->AddMessage("column settings", &columnSettings);

	return B_OK;
}


status_t
MainWindow::RestoreSettings(BMessage* archive)
{
	BRect frame;
	if (archive->FindRect("window frame", &frame) == B_OK) {
		BScreen screen(this);
		if (frame.Intersects(screen.Frame())) {
			MoveTo(frame.LeftTop());
			ResizeTo(frame.Width(), frame.Height());
		}
	}

	BMessage columnSettings;
	if (archive->FindMessage("column settings", &columnSettings) == B_OK)
		fListView->LoadState(&columnSettings);

	return B_OK;
}


void
MainWindow::ApplyDefaultSettings()
{
	if (!Lock())
		return;

	fListView->ResizeAllColumnsToPreferred();

	// Adjust window size for convenience
	BScreen screen(this);
	float windowWidth = Frame().Width();
	float windowHeight = Frame().Height();

	float enlargeWidthBy = fListView->PreferredSize().width
		- fListView->Bounds().Width();
	float enlargeHeightBy = fListView->PreferredSize().height
		- fListView->Bounds().Height();

	if (enlargeWidthBy > 0.0f)
		windowWidth += enlargeWidthBy;
	if (enlargeHeightBy > 0.0f)
		windowHeight += enlargeHeightBy;

	if (windowWidth > screen.Frame().Width() - 20.0f)
		windowWidth = screen.Frame().Width() - 20.0f;
	if (windowHeight > screen.Frame().Height() - 20.0f)
		windowHeight = screen.Frame().Height() - 20.0f;

	ResizeTo(windowWidth, windowHeight);
	CenterOnScreen();

	Unlock();
}


static void
FileErrorAlert(const char* text, const char* fileName, alert_type type)
{
	BString message;
	message.SetToFormat(text, fileName);
	BString title(B_TRANSLATE("%appname% error"));
	title.ReplaceFirst("%appname%", B_TRANSLATE_SYSTEM_NAME("DriveSetup"));
	BAlert* alert = new BAlert(title, message.String(),
		B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_FROM_WIDEST, type);
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go(NULL);
}


status_t
MainWindow::RegisterFileDiskDevice(const char* fileName)
{
	BString message;

	struct stat st;
	if (lstat(fileName, &st) != 0) {
		status_t error = errno;

		FileErrorAlert(B_TRANSLATE("Failed to get information about '%s'."),
			fileName, B_STOP_ALERT);
		return error;
	}

	if (!S_ISREG(st.st_mode)) {
		FileErrorAlert(B_TRANSLATE("'%s' is not a regular file."), fileName,
			B_STOP_ALERT);
		return B_BAD_VALUE;
	}

	// register the file
	BDiskDeviceRoster roster;
	partition_id id = roster.RegisterFileDevice(fileName);
	if (id < 0) {
		FileErrorAlert(B_TRANSLATE("Failed to register file disk device: %s"),
			fileName, B_STOP_ALERT);
		return id;
	}

	fNotification->SetTitle(BString(B_TRANSLATE("Disk image")));
	fNotification->SetContent(BString(
		B_TRANSLATE("Disk image has been added.")));
	fNotification->Send();

	return B_OK;
}


status_t
MainWindow::UnRegisterFileDiskDevice(const char* fileNameOrID)
{
	BString message;

	// try to parse the parameter as ID
	char* numberEnd;
	partition_id id = strtol(fileNameOrID, &numberEnd, 0);
	BDiskDeviceRoster roster;
	if (id >= 0 && numberEnd != fileNameOrID && *numberEnd == '\0') {
		BDiskDevice device;
		if (roster.GetDeviceWithID(id, &device) == B_OK && device.IsFile()) {
			status_t error = roster.UnregisterFileDevice(id);
			if (error != B_OK) {
				FileErrorAlert(B_TRANSLATE(
					"Failed to unregister file disk device."), NULL,
					B_STOP_ALERT);
				return error;
			}

			fNotification->SetTitle(BString(B_TRANSLATE("Disk image")));
			fNotification->SetContent(BString(
				B_TRANSLATE("Disk image has been removed.")));
			fNotification->Send();

			return B_OK;
		} else {
			FileErrorAlert(B_TRANSLATE(
				"No file disk device found with the given ID, trying file '%s'."),
				fileNameOrID, B_WARNING_ALERT);
		}
	}

	// the parameter must be a file name -- stat() it
	struct stat st;
	if (lstat(fileNameOrID, &st) != 0) {
		status_t error = errno;
		FileErrorAlert(B_TRANSLATE("Failed to get information about '%s'."),
			fileNameOrID, B_STOP_ALERT);
		return error;
	}

	// remember the volume and node ID, so we can identify the file
	// NOTE: There's a race condition -- we would need to open the file and
	// keep it open to avoid it.
	dev_t volumeID = st.st_dev;
	ino_t nodeID = st.st_ino;

	// iterate through all file disk devices and try to find a match
	BDiskDevice device;
	while (roster.GetNextDevice(&device) == B_OK) {
		if (!device.IsFile())
			continue;

		// get file path and stat it, same for the device path
		BPath path;
		if ((device.GetFilePath(&path) == B_OK && lstat(path.Path(), &st) == 0
				&& volumeID == st.st_dev && nodeID == st.st_ino)
			|| (device.GetPath(&path) == B_OK && lstat(path.Path(), &st) == 0
				&& volumeID == st.st_dev && nodeID == st.st_ino)) {
			status_t error = roster.UnregisterFileDevice(device.ID());
			if (error != B_OK) {
				FileErrorAlert(B_TRANSLATE(
					"Failed to unregister file disk device %s."), fileNameOrID,
					B_STOP_ALERT);
				return error;
			}

			fNotification->SetTitle(BString(B_TRANSLATE("Disk image")));
			fNotification->SetContent(BString(
				B_TRANSLATE("Disk image has been removed.")));
			fNotification->Send();

			return B_OK;
		}
	}

	FileErrorAlert(B_TRANSLATE("%s does not refer to a file disk device."),
		fileNameOrID, B_WARNING_ALERT);
	return B_BAD_VALUE;
}


int32
MainWindow::SaveDiskImage(void* data)
{
	BMessage* msg = (BMessage*)data;
	const char* sourcepath;
	const char* targetpath;
	const char* targetfolder;
	int32 partitionID;
	BMessenger messenger;

	msg->FindString("source", &sourcepath);
	msg->FindString("target", &targetpath);
	msg->FindString("targetfolder", &targetfolder);
	msg->FindInt32("partitionID", &partitionID);
	msg->FindMessenger("messenger", &messenger);

	BFile source(sourcepath, B_READ_ONLY);
	BFile target(targetpath, B_READ_WRITE | B_CREATE_FILE);

	if (source.InitCheck() != B_OK) {
		FileErrorAlert(B_TRANSLATE("Cannot init source volume."), NULL,
			B_STOP_ALERT);
		return -1;
	}

	if (target.InitCheck() != B_OK) {
		FileErrorAlert(B_TRANSLATE("Cannot init target file."), NULL,
			B_STOP_ALERT);
		return -1;
	}

	BDirectory* targetdir = new BDirectory(targetfolder);
	node_ref node;
	targetdir->GetNodeRef(&node);
	BVolume volume(node.device());

	if (volume.InitCheck()) {
		FileErrorAlert(B_TRANSLATE("Cannot init target volume."), NULL,
			B_STOP_ALERT);
		return -1;
	}

	if (volume.IsReadOnly()) {
		FileErrorAlert(B_TRANSLATE("Target volume is read only."), NULL,
			B_STOP_ALERT);
		return -1;
	}

	BPartition *partition;
	BDiskDevice device;

	BDiskDeviceRoster().GetPartitionWithID(partitionID, &device, &partition);

	if (volume.FreeBytes() < partition->Size()) {
		FileErrorAlert(B_TRANSLATE("There is not enough free space on target "
			"device."), NULL, B_STOP_ALERT);
		return -1;
	}

	_WriteDiskImage(messenger, source, target, targetpath,
		BString(B_TRANSLATE("Creating disk image")),
		BString(B_TRANSLATE("Disk image successfully created.")));

	return 0;
}


int32
MainWindow::WriteDiskImage(void* data)
{
	BMessage* msg = (BMessage*)data;
	const char* sourcepath;
	const char* targetpath;
	BMessenger messenger;

	msg->FindString("source", &sourcepath);
	msg->FindString("target", &targetpath);
	msg->FindMessenger("messenger", &messenger);

	BFile source(sourcepath, B_READ_ONLY);
	BFile target(targetpath, B_READ_WRITE);

	if (target.InitCheck() != B_OK) {
		FileErrorAlert(B_TRANSLATE("Cannot init target partition."), NULL,
			B_STOP_ALERT);
		return -1;
	}

	BPartition *partition;
	BDiskDevice device;

	BDiskDeviceRoster().GetPartitionForPath(targetpath, &device, &partition);

	if (partition->IsReadOnly()) {
		FileErrorAlert(B_TRANSLATE("Target partition is read only."), NULL,
			B_STOP_ALERT);
		return -1;
	}

	off_t size;
	source.GetSize(&size);

	if (partition->Size() < size) {
		FileErrorAlert(B_TRANSLATE("The target partition is smaller than the "
			"image file."), NULL, B_STOP_ALERT);
		return -1;
	}

	_WriteDiskImage(messenger, source, target, targetpath,
		BString(B_TRANSLATE("Writing image to disk")),
		BString(B_TRANSLATE("Disk image successfully written to the target.")));

	return 0;
}


void
MainWindow::_WriteDiskImage(BMessenger messenger, BFile source, BFile target,
	const char* targetpath, BString title, BString status)
{
	size_t bufsize = 1024 * 1024;
	off_t sourcesize;
	ssize_t targetbytes = 0;

	source.GetSize(&sourcesize);

	char maximumBuffer[16], currentBuffer[16];
	BString statusprogress;

	BNotification progress(B_PROGRESS_NOTIFICATION);
	progress.SetGroup(BString(B_TRANSLATE_SYSTEM_NAME("DriveSetup")));
	progress.SetMessageID(BString(targetpath));
	progress.SetTitle(title);

	char* buffer = (char*)malloc(bufsize);
	while (true) {
		ssize_t bytes = source.Read(buffer, bufsize);

		if (bytes > 0) {
			target.Write(buffer, (size_t)bytes);

			targetbytes += bytes;

			statusprogress.SetToFormat("%s: %s / %s", targetpath,
				string_for_size(targetbytes, currentBuffer, 16),
				string_for_size(sourcesize, maximumBuffer, 16));

			progress.SetContent(statusprogress);
			progress.SetProgress((float)targetbytes / sourcesize);
			progress.Send();
		} else
			break;
	}
	free(buffer);

	BMessage message(MSG_COMPLETE);
	message.AddString("status", status);
	message.AddString("jobid", targetpath);
	messenger.SendMessage(&message);
}


// #pragma mark -


void
MainWindow::MenusBeginning()
{
	PartitionListRow* selected
		= dynamic_cast<PartitionListRow*>(fListView->CurrentSelection());
	_SetToDiskAndPartition(fCurrentDisk != NULL ? fCurrentDisk->ID() : -1,
		fCurrentPartitionID, selected != NULL ? selected->ParentID() : -1);
}


void
MainWindow::_ScanDrives()
{
	// Do not clear SpaceIDMap: ids must stay stable across rescans.
	int32 diskCount = 0;
	ListPopulatorVisitor driveVisitor(fListView, diskCount, fSpaceIDMap);
	fDiskDeviceRoster.VisitEachPartition(&driveVisitor);
	fDiskView->SetDiskCount(diskCount);

	// restore selection
	PartitionListRow* previousSelection
		= fListView->FindRow(fCurrentPartitionID);
	if (previousSelection) {
		if (fCurrentDisk != NULL)
			fCurrentDisk->Update();
		fListView->SetFocusRow(previousSelection, true);
		_UpdateMenus(fCurrentDisk, fCurrentPartitionID,
			previousSelection->ParentID());
		fDiskView->ForceUpdate();
	} else {
		_UpdateMenus(NULL, -1, -1);
	}

	PostMessage(MSG_UPDATE_ZOOM_LIMITS);
}


// #pragma mark -


void
MainWindow::_AdaptToSelectedPartition()
{
	partition_id diskID = -1;
	partition_id partitionID = -1;
	partition_id parentID = -1;

	BRow* _selectedRow = fListView->CurrentSelection();
	if (_selectedRow) {
		// go up to top level row
		BRow* _topLevelRow = _selectedRow;
		BRow* parent = NULL;
		while (fListView->FindParent(_topLevelRow, &parent, NULL))
			_topLevelRow = parent;

		PartitionListRow* topLevelRow
			= dynamic_cast<PartitionListRow*>(_topLevelRow);
		PartitionListRow* selectedRow
			= dynamic_cast<PartitionListRow*>(_selectedRow);

		if (topLevelRow)
			diskID = topLevelRow->ID();
		if (selectedRow) {
			partitionID = selectedRow->ID();
			parentID = selectedRow->ParentID();
		}
	}

	if (getenv("VOS_DEBUG_MENUS") != NULL) {
		fprintf(stderr, "[DriveSetup select] row=%p disk=%" B_PRId32
			" partition=%" B_PRId32 " parent=%" B_PRId32
			" (fCurrentPartitionID was %" B_PRId32 ")\n",
			_selectedRow, diskID, partitionID, parentID, fCurrentPartitionID);
	}

	_SetToDiskAndPartition(diskID, partitionID, parentID);
}


void
MainWindow::_SetToDiskAndPartition(partition_id disk, partition_id partition,
	partition_id parent)
{
//printf("MainWindow::_SetToDiskAndPartition(disk: %ld, partition: %ld, "
//	"parent: %ld)\n", disk, partition, parent);

	BDiskDevice* oldDisk = NULL;
	if (!fCurrentDisk || fCurrentDisk->ID() != disk) {
		oldDisk = fCurrentDisk;
		fCurrentDisk = NULL;
		if (disk >= 0) {
			BDiskDevice* newDisk = new BDiskDevice();
			status_t status = newDisk->SetTo(disk);
			if (status != B_OK) {
				printf("error switching disks: %s\n", strerror(status));
				delete newDisk;
			} else
				fCurrentDisk = newDisk;
		}
	} else {
		fCurrentDisk->Update();
	}

	fCurrentPartitionID = partition;

	fDiskView->SetDisk(fCurrentDisk, fCurrentPartitionID);
	_UpdateMenus(fCurrentDisk, fCurrentPartitionID, parent);

	delete oldDisk;
}


void
MainWindow::_UpdateMenus(BDiskDevice* disk,
	partition_id selectedPartition, partition_id parentID)
{
	while (BMenuItem* item = fFormatMenu->RemoveItem((int32)0))
		delete item;
	while (BMenuItem* item = fDiskInitMenu->RemoveItem((int32)0))
		delete item;
	while (BMenuItem* item = fFormatContextMenuItem->RemoveItem((int32)0))
		delete item;

	fCreateMenuItem->SetEnabled(false);
	fUnmountMenuItem->SetEnabled(false);
	fDiskInitMenu->SetEnabled(false);
	fFormatMenu->SetEnabled(false);

	fCreateContextMenuItem->SetEnabled(false);
	fUnmountContextMenuItem->SetEnabled(false);
	fFormatContextMenuItem->SetEnabled(false);

	if (disk == NULL) {
		fWipeMenuItem->SetEnabled(false);
		fEjectMenuItem->SetEnabled(false);
		fSurfaceTestMenuItem->SetEnabled(false);
		fUnRegisterMenuItem->SetEnabled(false);
		fSaveMenuItem->SetEnabled(false);
		fWriteMenuItem->SetEnabled(false);
		fOpenDiskProbeMenuItem->SetEnabled(false);
		fOpenDiskProbeContextMenuItem->SetEnabled(false);
	} else {
//		fWipeMenuItem->SetEnabled(true);
		fWipeMenuItem->SetEnabled(false);
		fEjectMenuItem->SetEnabled(disk->IsRemovableMedia());
//		fSurfaceTestMenuItem->SetEnabled(true);
		fSurfaceTestMenuItem->SetEnabled(false);

		fUnRegisterMenuItem->SetEnabled(disk->IsFile());

		fSaveMenuItem->SetEnabled(true);
		fWriteMenuItem->SetEnabled(!disk->IsReadOnly());

		// Create menu and items
		BPartition* parentPartition = NULL;
		if (selectedPartition <= -2) {
			// a partitionable space item is selected
			parentPartition = disk->FindDescendant(parentID);
		}

		if (parentPartition && parentPartition->ContainsPartitioningSystem()) {
			fCreateMenuItem->SetEnabled(true);
			fCreateContextMenuItem->SetEnabled(true);
		}
		bool prepared = disk->PrepareModifications() == B_OK;

		fFormatMenu->SetEnabled(prepared);
		fDeleteMenuItem->SetEnabled(prepared);
		fChangeMenuItem->SetEnabled(prepared);
		fFlagsMenuItem->SetEnabled(prepared);

		fFormatContextMenuItem->SetEnabled(prepared);
		fDeleteContextMenuItem->SetEnabled(prepared);
		fChangeContextMenuItem->SetEnabled(prepared);

		BPartition* partition = disk->FindDescendant(selectedPartition);

		BDiskSystem diskSystem;
		fDiskDeviceRoster.RewindDiskSystems();
		while (fDiskDeviceRoster.GetNextDiskSystem(&diskSystem) == B_OK) {
			if (!diskSystem.SupportsInitializing())
				continue;

			BMessage* message = new BMessage(MSG_INITIALIZE);
			message->AddInt32("parent id", parentID);
			message->AddString("disk system", diskSystem.PrettyName());

			BString label = diskSystem.PrettyName();
			label << B_UTF8_ELLIPSIS;
			BMenuItem* item = new BMenuItem(label.String(), message);

			// TODO: Very unintuitive that we have to use PrettyName (vs Name)
			item->SetEnabled(partition != NULL
				&& partition->CanInitialize(diskSystem.PrettyName()));

			if (disk->ID() == selectedPartition
				&& !diskSystem.IsFileSystem()) {
				// Disk is selected, and DiskSystem is a partition map
				fDiskInitMenu->AddItem(item);
			} else if (diskSystem.IsFileSystem()) {
				// Otherwise a filesystem
				fFormatMenu->AddItem(item);

				// Context menu
				BMessage* message = new BMessage(MSG_INITIALIZE);
				message->AddInt32("parent id", parentID);
				message->AddString("disk system", diskSystem.PrettyName());
				BMenuItem* popUpItem = new BMenuItem(label.String(), message);
				popUpItem->SetEnabled(partition != NULL
					&& partition->CanInitialize(diskSystem.PrettyName()));
				fFormatContextMenuItem->AddItem(popUpItem);
				fFormatContextMenuItem->SetTargetForItems(this);
			}
		}

		// Mount items
		if (partition != NULL) {
			bool writable = !partition->IsReadOnly()
				&& partition->Device()->HasMedia();
			bool notMountedAndWritable = !partition->IsMounted() && writable;

			fFormatMenu->SetEnabled(writable && fFormatMenu->CountItems() > 0);

			fDiskInitMenu->SetEnabled(notMountedAndWritable
				&& partition->IsDevice()
				&& fDiskInitMenu->CountItems() > 0);

			fChangeMenuItem->SetEnabled(writable
				&& partition->CanEditParameters());
			fChangeContextMenuItem->SetEnabled(writable
				&& partition->CanEditParameters());
			fFlagsMenuItem->SetEnabled(writable
				&& partition->Parent() != NULL);

			// ContentType() is set for bare maps too; require a filesystem.
			BDiskSystem contentDiskSystem;
			bool hasContent = partition->ContentType() != NULL
				&& partition->GetDiskSystem(&contentDiskSystem) == B_OK
				&& contentDiskSystem.IsFileSystem();
			bool contentOpsPossible = notMountedAndWritable && hasContent;

			bool canCheck = false;
			bool canRenameLabel = false;
			bool canResizeMove = false;
			if (contentOpsPossible) {
				PartitionCapabilities caps;
				if (PartitionCapabilities::Get(partition->ContentType(), caps)
						== B_OK) {
					canCheck = caps.check == "external"
						|| caps.check == "internal";
					canRenameLabel = caps.writeLabel == "external"
						|| caps.writeLabel == "internal";
					canResizeMove = caps.grow != "none"
						|| caps.shrink != "none" || caps.move != "none";
				}
			}
			fCheckMenuItem->SetEnabled(canCheck);
			fEraseMenuItem->SetEnabled(contentOpsPossible);
			fResizeMenuItem->SetEnabled(canResizeMove);
			fRenameMenuItem->SetEnabled(canRenameLabel);

			BPartition* parentPartition = partition->Parent();
			bool isGptTable = parentPartition != NULL
				&& BString(parentPartition->ContentType()) == "gpt";
			if (isGptTable) {
				if (fPartitionMenu->IndexOf(fRenameGptMenuItem) < 0) {
					fPartitionMenu->AddItem(fRenameGptMenuItem,
						fPartitionMenu->IndexOf(fRenameMenuItem) + 1);
				}
				fRenameGptMenuItem->SetEnabled(writable
					&& !partition->IsDevice());
			} else if (fPartitionMenu->IndexOf(fRenameGptMenuItem) >= 0) {
				fPartitionMenu->RemoveItem(fRenameGptMenuItem);
			}

			fDeleteMenuItem->SetEnabled(notMountedAndWritable
				&& !partition->IsDevice());
			fDeleteContextMenuItem->SetEnabled(notMountedAndWritable
				&& !partition->IsDevice());

			bool mountable = !partition->IsMounted()
				&& partition->ContentType() != NULL;
			fMountMenuItem->SetEnabled(mountable);
			fMountContextMenuItem->SetEnabled(mountable);

			fFormatContextMenuItem->SetEnabled(notMountedAndWritable
				&& fFormatContextMenuItem->CountItems() > 0);

			bool unMountable = false;
			if (partition->IsMounted()) {
				// see if this partition is the boot volume
				BVolume volume;
				BVolume bootVolume;
				if (BVolumeRoster().GetBootVolume(&bootVolume) == B_OK
					&& partition->GetVolume(&volume) == B_OK) {
					unMountable = volume != bootVolume;
				} else
					unMountable = true;
			}
			fUnmountMenuItem->SetEnabled(unMountable);
			fUnmountContextMenuItem->SetEnabled(unMountable);
		} else {
			fDeleteMenuItem->SetEnabled(false);
			fChangeMenuItem->SetEnabled(false);
			fResizeMenuItem->SetEnabled(false);
			fFlagsMenuItem->SetEnabled(false);
			fCheckMenuItem->SetEnabled(false);
			fEraseMenuItem->SetEnabled(false);
			fRenameMenuItem->SetEnabled(false);
			if (fPartitionMenu->IndexOf(fRenameGptMenuItem) >= 0)
				fPartitionMenu->RemoveItem(fRenameGptMenuItem);
			fMountMenuItem->SetEnabled(false);
			fFormatMenu->SetEnabled(false);
			fDiskInitMenu->SetEnabled(false);

			fDeleteContextMenuItem->SetEnabled(false);
			fChangeContextMenuItem->SetEnabled(false);
			fMountContextMenuItem->SetEnabled(false);
			fFormatContextMenuItem->SetEnabled(false);
		}

		if (getenv("VOS_DEBUG_MENUS") != NULL) {
			BPartition* p = disk->FindDescendant(selectedPartition);
			fprintf(stderr, "[DriveSetup menus] selected=%" B_PRId32
				" parent=%" B_PRId32 " found=%s prepared=%s\n",
				selectedPartition, parentID, p ? "yes" : "NO", 
				prepared ? "yes" : "no");
			if (p != NULL) {
				BDiskSystem ds;
				bool gotDS = p->GetDiskSystem(&ds) == B_OK;
				fprintf(stderr, "[DriveSetup menus]   contentType=%s "
					"diskSystem=%s isFS=%s isDevice=%s mounted=%s "
					"readOnly=%s hasMedia=%s\n",
					p->ContentType() ? p->ContentType() : "(null)",
					gotDS ? ds.Name() : "(lookup FAILED)",
					gotDS && ds.IsFileSystem() ? "yes" : "no",
					p->IsDevice() ? "yes" : "no",
					p->IsMounted() ? "yes" : "no",
					p->IsReadOnly() ? "yes" : "no",
					p->Device()->HasMedia() ? "yes" : "no");
			} else {
				BPartition* pp = parentID >= 0
					? disk->FindDescendant(parentID) : NULL;
				fprintf(stderr, "[DriveSetup menus]   (space row) parent=%s "
					"parentIsContainer=%s\n",
					pp ? "found" : "NOT FOUND",
					pp && pp->ContainsPartitioningSystem() ? "yes" : "no");
			}
			fprintf(stderr, "[DriveSetup menus]   enabled: create=%d "
				"format=%d delete=%d change=%d mount=%d unmount=%d "
				"erase=%d check=%d rename=%d resize=%d\n",
				fCreateContextMenuItem->IsEnabled(),
				fFormatContextMenuItem->IsEnabled(),
				fDeleteContextMenuItem->IsEnabled(),
				fChangeContextMenuItem->IsEnabled(),
				fMountContextMenuItem->IsEnabled(),
				fUnmountContextMenuItem->IsEnabled(),
				fEraseMenuItem->IsEnabled(), fCheckMenuItem->IsEnabled(),
				fRenameMenuItem->IsEnabled(), fResizeMenuItem->IsEnabled());
		}

		if (prepared)
			disk->CancelModifications();

		fOpenDiskProbeMenuItem->SetEnabled(true);
		fOpenDiskProbeContextMenuItem->SetEnabled(true);

		fMountAllMenuItem->SetEnabled(true);
	}
	if (selectedPartition < 0) {
		fDeleteMenuItem->SetEnabled(false);
		fChangeMenuItem->SetEnabled(false);
		fResizeMenuItem->SetEnabled(false);
		fFlagsMenuItem->SetEnabled(false);
		fCheckMenuItem->SetEnabled(false);
		fEraseMenuItem->SetEnabled(false);
		fRenameMenuItem->SetEnabled(false);
		if (fPartitionMenu->IndexOf(fRenameGptMenuItem) >= 0)
			fPartitionMenu->RemoveItem(fRenameGptMenuItem);
		fMountMenuItem->SetEnabled(false);

		fDeleteContextMenuItem->SetEnabled(false);
		fChangeContextMenuItem->SetEnabled(false);
		fMountContextMenuItem->SetEnabled(false);
	}
}


void
MainWindow::_DisplayPartitionError(BString _message,
	const BPartition* partition, status_t error, const BMessage* result) const
{
	char message[1024];

	if (partition && _message.FindFirst("%s") >= 0) {
		BString name;
		name << "\"" << partition->ContentName() << "\"";
		snprintf(message, sizeof(message), _message.String(), name.String());
	} else {
		_message.ReplaceAll("%s", "");
		strlcpy(message, _message.String(), sizeof(message));
	}

	BString detail;
	if (result != NULL)
		result->FindString("detail", &detail);

	if (!detail.IsEmpty()) {
		BString helper = message;
		snprintf(message, sizeof(message), "%s\n\n%s", helper.String(),
			detail.String());
	} else if (error < B_OK) {
		BString helper = message;
		const char* errorString
			= B_TRANSLATE_COMMENT("Error:", "in any error alert");
		snprintf(message, sizeof(message), "%s\n\n%s %s", helper.String(),
			errorString, strerror(error));
	}

	BAlert* alert = new BAlert("error", message, B_TRANSLATE("OK"), NULL, NULL,
		B_WIDTH_FROM_WIDEST, error < B_OK ? B_STOP_ALERT : B_INFO_ALERT);
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go(NULL);
}


// #pragma mark -


void
MainWindow::_Mount(BDiskDevice* disk, partition_id selectedPartition)
{
	if (!disk || selectedPartition < 0) {
		_DisplayPartitionError(B_TRANSLATE("You need to select a partition "
			"entry from the list."));
		return;
	}

	BPartition* partition = disk->FindDescendant(selectedPartition);
	if (!partition) {
		_DisplayPartitionError(B_TRANSLATE("Unable to find the selected "
			"partition by ID."));
		return;
	}

	if (partition->IsMounted()) {
		_DisplayPartitionError(
			B_TRANSLATE("The partition %s is already mounted."), partition);
		return;
	}

	// Unprivileged app: mount(2) needs CAP_SYS_ADMIN, so ask mount_server.
	BMessage message(kMountVolume);
	message.AddInt64("id", partition->ID());
	BMessenger(kMountServerSignature).SendMessage(&message);
}


void
MainWindow::_Unmount(BDiskDevice* disk, partition_id selectedPartition)
{
	if (!disk || selectedPartition < 0) {
		_DisplayPartitionError(B_TRANSLATE("You need to select a partition "
			"entry from the list."));
		return;
	}

	BPartition* partition = disk->FindDescendant(selectedPartition);
	if (!partition) {
		_DisplayPartitionError(B_TRANSLATE("Unable to find the selected "
			"partition by ID."));
		return;
	}

	if (!partition->IsMounted()) {
		_DisplayPartitionError(
			B_TRANSLATE("The partition %s is already unmounted."),
			partition);
		return;
	}

	BMessage message(kUnmountVolume);
	message.AddInt64("id", partition->ID());
	BMessenger(kMountServerSignature).SendMessage(&message);
}


void
MainWindow::_MountAll()
{
	BMessage message(kMountAllNow);
	BMessenger(kMountServerSignature).SendMessage(&message);
}


// #pragma mark -


void
MainWindow::_Initialize(BDiskDevice* disk, partition_id selectedPartition,
	const BString& diskSystemName)
{
	if (!disk || selectedPartition < 0) {
		_DisplayPartitionError(B_TRANSLATE("You need to select a partition "
			"entry from the list."));
		return;
	}

	if (disk->IsReadOnly()) {
		_DisplayPartitionError(B_TRANSLATE("The selected disk is read-only."));
		return;
	}

	BPartition* partition = disk->FindDescendant(selectedPartition);
	if (!partition) {
		_DisplayPartitionError(B_TRANSLATE("Unable to find the selected "
			"partition by ID."));
		return;
	}

	if (partition->IsMounted()) {
		if (partition->Unmount() != B_OK) {
			// Probably it's the system partition
			_DisplayPartitionError(
				B_TRANSLATE("The partition cannot be unmounted."));

			return;
		}
	}

	BDiskSystem diskSystem;
	fDiskDeviceRoster.RewindDiskSystems();
	bool found = false;
	while (fDiskDeviceRoster.GetNextDiskSystem(&diskSystem) == B_OK) {
		if (diskSystem.SupportsInitializing()) {
			if (diskSystemName == diskSystem.PrettyName()) {
				found = true;
				break;
			}
		}
	}

	if (!found) {
		_DisplayPartitionError(B_TRANSLATE("Disk system %s not found!"));
		return;
	}

	BString message;

	if (diskSystem.IsFileSystem()) {
		BString intelExtendedPartition = "Intel Extended Partition";
		if (disk->ID() == selectedPartition) {
			message = B_TRANSLATE("Are you sure you "
				"want to format a raw disk? (Most people initialize the disk "
				"with a partitioning system first) You will be asked "
				"again before changes are written to the disk.");
		} else if (partition->ContentName()
			&& strlen(partition->ContentName()) > 0) {
			message = B_TRANSLATE("Are you sure you "
				"want to format the partition \"%s\"? You will be asked "
				"again before changes are written to the disk.");
			message.ReplaceFirst("%s", partition->ContentName());
		} else if (partition->Type() == intelExtendedPartition) {
			message = B_TRANSLATE("Are you sure you "
				"want to format the Intel Extended Partition? Any "
				"subpartitions it contains will be overwritten if you "
				"continue. You will be asked again before changes are "
				"written to the disk.");
		} else {
			message = B_TRANSLATE("Are you sure you "
				"want to format the partition? You will be asked again "
				"before changes are written to the disk.");
		}
	} else {
		message = B_TRANSLATE("Are you sure you "
			"want to initialize the selected disk? All data will be lost. "
			"You will be asked again before changes are written to the "
			"disk.\n");
	}
	BAlert* alert = new BAlert("first notice", message.String(),
		B_TRANSLATE("Continue"), B_TRANSLATE("Cancel"), NULL,
		B_WIDTH_FROM_WIDEST, B_WARNING_ALERT);
	alert->SetShortcut(1, B_ESCAPE);
	int32 choice = alert->Go();

	if (choice == 1)
		return;

	ModificationPreparer modificationPreparer(disk);
	status_t status = modificationPreparer.ModificationStatus();
	if (status != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("There was an error preparing the "
			"disk for modifications."), NULL, status);
		return;
	}

	BString name;
	BString parameters;
	InitParametersPanel* panel = new InitParametersPanel(this, diskSystemName,
		partition);
	if (panel->Go(name, parameters) != B_OK)
		return;

	bool supportsName = diskSystem.SupportsContentName();
	BString validatedName(name);
	status = partition->ValidateInitialize(diskSystem.PrettyName(),
		supportsName ? &validatedName : NULL, parameters.String());
	if (status != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("Validation of the given "
			"initialization parameters failed."), partition, status);
		return;
	}

	BString previousName = partition->ContentName();

	status = partition->Initialize(diskSystem.PrettyName(),
		supportsName ? validatedName.String() : NULL, parameters.String());
	if (status != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("Initialization of the partition "
			"%s failed. (Nothing has been written to disk.)"), partition,
			status);
		return;
	}

	// Also set the partition name in the partition table if supported
	if (partition->CanSetName()
		&& partition->ValidateSetName(&validatedName) == B_OK) {
		partition->SetName(validatedName.String());
	}

	// everything looks fine, we are ready to actually write the changes
	// to disk

	// Warn the user one more time...
	if (previousName.Length() > 0) {
		if (partition->IsDevice()) {
			message = B_TRANSLATE("Are you sure you "
				"want to write the changes back to disk now?\n\n"
				"All data on the disk \"%s\" will be irretrievably lost if you "
				"do so!");
			message.ReplaceFirst("%s", previousName.String());
		} else {
			message = B_TRANSLATE("Are you sure you "
				"want to write the changes back to disk now?\n\n"
				"All data on the partition \"%s\" will be irretrievably lost if you "
				"do so!");
			message.ReplaceFirst("%s", previousName.String());
		}
	} else {
		if (partition->IsDevice()) {
			message = B_TRANSLATE("Are you sure you "
				"want to write the changes back to disk now?\n\n"
				"All data on the selected disk will be irretrievably lost if "
				"you do so!");
		} else {
			message = B_TRANSLATE("Are you sure you "
				"want to write the changes back to disk now?\n\n"
				"All data on the selected partition will be irretrievably lost "
				"if you do so!");
		}
	}
	alert = new BAlert("final notice", message.String(),
		B_TRANSLATE("Write changes"), B_TRANSLATE("Cancel"), NULL,
		B_WIDTH_FROM_WIDEST, B_WARNING_ALERT);
	alert->SetShortcut(1, B_ESCAPE);
	choice = alert->Go();

	if (choice == 1)
		return;

	// commit
	BMessage result;
	status = modificationPreparer.CommitModifications(&result);

	ProgressWindow* progress = new ProgressWindow(this, result);
	progress->Go();

	// The partition pointer is toast now! Use the partition ID to
	// retrieve it again.
	partition = disk->FindDescendant(selectedPartition);

	if (status == B_OK) {
		if (diskSystem.IsFileSystem()) {
			_DisplayPartitionError(B_TRANSLATE("The partition %s has been "
				"successfully formatted.\n"), partition);
		} else {
			_DisplayPartitionError(B_TRANSLATE("The disk has been "
				"successfully initialized.\n"), partition);
		}
	} else {
		if (diskSystem.IsFileSystem()) {
			_DisplayPartitionError(B_TRANSLATE("Failed to format the "
				"partition %s!\n"), partition, status, &result);
		} else {
			_DisplayPartitionError(B_TRANSLATE("Failed to initialize the "
				"disk %s!\n"), partition, status, &result);
		}
	}

	_ScanDrives();
}


void
MainWindow::_Create(BDiskDevice* disk, partition_id selectedPartition)
{
	if (!disk || selectedPartition > -2) {
		_DisplayPartitionError(B_TRANSLATE("The currently selected partition "
			"is not empty."));
		return;
	}

	if (disk->IsReadOnly()) {
		_DisplayPartitionError(B_TRANSLATE("The selected disk is read-only."));
		return;
	}

	PartitionListRow* currentSelection = dynamic_cast<PartitionListRow*>(
		fListView->CurrentSelection());
	if (!currentSelection) {
		_DisplayPartitionError(B_TRANSLATE("There was an error acquiring the "
			"partition row."));
		return;
	}

	BPartition* parent = disk->FindDescendant(currentSelection->ParentID());
	if (!parent) {
		_DisplayPartitionError(B_TRANSLATE("The currently selected partition "
			"does not have a parent partition."));
		return;
	}

	if (!parent->ContainsPartitioningSystem()) {
		_DisplayPartitionError(B_TRANSLATE("The selected partition does not "
			"contain a partitioning system."));
		return;
	}

	ModificationPreparer modificationPreparer(disk);
	status_t status = modificationPreparer.ModificationStatus();
	if (status != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("There was an error preparing the "
			"disk for modifications."), NULL, status);
		return;
	}

	// get partitioning info
	BPartitioningInfo partitioningInfo;
	status_t error = parent->GetPartitioningInfo(&partitioningInfo);
	if (error != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("Could not acquire partitioning "
			"information."));
		return;
	}

	int32 spacesCount = partitioningInfo.CountPartitionableSpaces();
	if (spacesCount == 0) {
		_DisplayPartitionError(B_TRANSLATE("There's no space on the partition "
			"where a child partition could be created."));
		return;
	}

	BString name, type, parameters;
	off_t offset = currentSelection->Offset();
	off_t size = currentSelection->Size();

	CreateParametersPanel* panel = new CreateParametersPanel(this, parent,
		offset, size);
	status = panel->Go(offset, size, name, type, parameters);
	if (status != B_OK) {
		if (status != B_CANCELED) {
			_DisplayPartitionError(B_TRANSLATE("The panel could not return "
				"successfully."), NULL, status);
		}
		return;
	}

	status = parent->ValidateCreateChild(&offset, &size, type.String(),
		&name, parameters.String());

	if (status != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("Validation of the given creation "
			"parameters failed."), NULL, status);
		return;
	}

	// Warn the user one more time...
	BAlert* alert = new BAlert("final notice", B_TRANSLATE("Are you sure you "
		"want to write the changes back to disk now?\n\n"
		"All data on the partition will be irretrievably lost if you do "
		"so!"), B_TRANSLATE("Write changes"), B_TRANSLATE("Cancel"), NULL,
		B_WIDTH_FROM_WIDEST, B_WARNING_ALERT);
	alert->SetShortcut(1, B_ESCAPE);
	int32 choice = alert->Go();

	if (choice == 1)
		return;

	status = parent->CreateChild(offset, size, type.String(), name.String(),
		parameters.String());

	if (status != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("Creation of the partition has "
			"failed."), NULL, status);
		return;
	}

	// commit
	BMessage result;
	status = modificationPreparer.CommitModifications(&result);

	ProgressWindow* progress = new ProgressWindow(this, result);
	progress->Go();

	if (status != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("Failed to create the "
			"partition. No changes have been written to disk."), NULL,
			status, &result);
		return;
	}

	// The disk layout has changed, update disk information
	bool updated;
	status = disk->Update(&updated);

	_ScanDrives();
	fDiskView->ForceUpdate();
}


void
MainWindow::_Delete(BDiskDevice* disk, partition_id selectedPartition)
{
	if (!disk || selectedPartition < 0) {
		_DisplayPartitionError(B_TRANSLATE("You need to select a partition "
			"entry from the list."));
		return;
	}

	if (disk->IsReadOnly()) {
		_DisplayPartitionError(B_TRANSLATE("The selected disk is read-only."));
		return;
	}

	BPartition* partition = disk->FindDescendant(selectedPartition);
	if (!partition) {
		_DisplayPartitionError(B_TRANSLATE("Unable to find the selected "
			"partition by ID."));
		return;
	}

	BPartition* parent = partition->Parent();
	if (!parent) {
		_DisplayPartitionError(B_TRANSLATE("The currently selected partition "
			"does not have a parent partition."));
		return;
	}

	ModificationPreparer modificationPreparer(disk);
	status_t status = modificationPreparer.ModificationStatus();
	if (status != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("There was an error preparing the "
			"disk for modifications."), NULL, status);
		return;
	}

	if (!parent->CanDeleteChild(partition->Index())) {
		_DisplayPartitionError(
			B_TRANSLATE("Cannot delete the selected partition."));
		return;
	}

	// Warn the user one more time...
	BAlert* alert = new BAlert("final notice", B_TRANSLATE("Are you sure you "
		"want to delete the selected partition?\n\n"
		"All data on the partition will be irretrievably lost if you "
		"do so!"), B_TRANSLATE("Delete partition"), B_TRANSLATE("Cancel"), NULL,
		B_WIDTH_FROM_WIDEST, B_WARNING_ALERT);
	alert->SetShortcut(1, B_ESCAPE);
	int32 choice = alert->Go();

	if (choice == 1)
		return;

	status = parent->DeleteChild(partition->Index());
	if (status != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("Could not delete the selected "
			"partition."), NULL, status);
		return;
	}

	BMessage result;
	status = modificationPreparer.CommitModifications(&result);

	ProgressWindow* progress = new ProgressWindow(this, result);
	progress->Go();

	if (status != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("Failed to delete the partition. "
			"No changes have been written to disk."), NULL, status, &result);
		return;
	}

	_ScanDrives();
	fDiskView->ForceUpdate();
}


void
MainWindow::_ChangeParameters(BDiskDevice* disk, partition_id selectedPartition)
{
	if (disk == NULL || selectedPartition < 0) {
		_DisplayPartitionError(B_TRANSLATE("You need to select a partition "
			"entry from the list."));
		return;
	}

	if (disk->IsReadOnly()) {
		_DisplayPartitionError(B_TRANSLATE("The selected disk is read-only."));
		return;
	}

	BPartition* partition = disk->FindDescendant(selectedPartition);
	if (partition == NULL) {
		_DisplayPartitionError(B_TRANSLATE("Unable to find the selected "
			"partition by ID."));
		return;
	}

	ModificationPreparer modificationPreparer(disk);
	status_t status = modificationPreparer.ModificationStatus();
	if (status != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("There was an error preparing the "
			"disk for modifications."), NULL, status);
		return;
	}

	ChangeParametersPanel* panel = new ChangeParametersPanel(this, partition);

	BString name, type, parameters;
	status = panel->Go(name, type, parameters);
	if (status != B_OK) {
		if (status != B_CANCELED) {
			_DisplayPartitionError(B_TRANSLATE("The panel experienced a "
				"problem!"), NULL, status);
		}
		return;
	}

	if (partition->CanSetType())
		status = partition->ValidateSetType(type.String());
	if (status == B_OK && partition->CanSetName())
		status = partition->ValidateSetName(&name);
	if (status != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("Validation of the given parameters "
			"failed."));
		return;
	}

	// Warn the user one more time...
	BAlert* alert = new BAlert("final notice", B_TRANSLATE("Are you sure you "
		"want to change parameters of the selected partition?\n\n"
		"The partition may no longer be recognized by other operating systems "
		"anymore!"), B_TRANSLATE("Change parameters"), B_TRANSLATE("Cancel"),
		NULL, B_WIDTH_FROM_WIDEST, B_WARNING_ALERT);
	alert->SetShortcut(1, B_ESCAPE);
	int32 choice = alert->Go();

	if (choice == 1)
		return;

	if (partition->CanSetType())
		status = partition->SetType(type.String());
	if (status == B_OK && partition->CanSetName())
		status = partition->SetName(name.String());
	if (status == B_OK && partition->CanEditParameters())
		status = partition->SetParameters(parameters.String());

	if (status != B_OK) {
		_DisplayPartitionError(
			B_TRANSLATE("Could not change the parameters of the selected "
				"partition."), NULL, status);
		return;
	}

	BMessage result;
	status = modificationPreparer.CommitModifications(&result);

	ProgressWindow* progress = new ProgressWindow(this, result);
	progress->Go();

	if (status != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("Failed to change the parameters "
			"of the partition. No changes have been written to disk."), NULL,
			status, &result);
		return;
	}

	_ScanDrives();
	fDiskView->ForceUpdate();
}


void
MainWindow::_SetFlags(BDiskDevice* disk, partition_id selectedPartition)
{
	if (disk == NULL || selectedPartition < 0) {
		_DisplayPartitionError(B_TRANSLATE("You need to select a partition "
			"entry from the list."));
		return;
	}

	if (disk->IsReadOnly()) {
		_DisplayPartitionError(B_TRANSLATE("The selected disk is read-only."));
		return;
	}

	BPartition* partition = disk->FindDescendant(selectedPartition);
	if (partition == NULL) {
		_DisplayPartitionError(B_TRANSLATE("Unable to find the selected "
			"partition by ID."));
		return;
	}

	BPartition* parent = partition->Parent();
	if (parent == NULL) {
		_DisplayPartitionError(B_TRANSLATE("The selected partition has no "
			"partitioning system to set flags on."));
		return;
	}

	FlagsPanel* panel = new FlagsPanel(this, partition);

	BMessage flags;
	status_t status = panel->Go(flags);
	if (status != B_OK) {
		if (status != B_CANCELED) {
			_DisplayPartitionError(B_TRANSLATE("The panel experienced a "
				"problem!"), NULL, status);
		}
		return;
	}

	PartitionReference* parentRef = new PartitionReference(parent->ID());
	PartitionReference* childRef = new PartitionReference(partition->ID());

	SetFlagsJob* job = new SetFlagsJob(parentRef, childRef);
	job->Init(flags);

	parentRef->ReleaseReference();
	childRef->ReleaseReference();

	BMessage result;
	DiskDeviceJobQueue jobQueue;
	jobQueue.AddJob(job);
	status = jobQueue.ExecuteViaHelper(disk, &result);

	ProgressWindow* progress = new ProgressWindow(this, result);
	progress->Go();

	if (status != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("Could not set the flags of the "
			"selected partition."), NULL, status, &result);
		return;
	}

	_ScanDrives();
}


void
MainWindow::_ResizeMove(BDiskDevice* disk, partition_id selectedPartition)
{
	if (disk == NULL || selectedPartition < 0) {
		_DisplayPartitionError(B_TRANSLATE("You need to select a partition "
			"entry from the list."));
		return;
	}

	if (disk->IsReadOnly()) {
		_DisplayPartitionError(B_TRANSLATE("The selected disk is read-only."));
		return;
	}

	BPartition* partition = disk->FindDescendant(selectedPartition);
	if (partition == NULL) {
		_DisplayPartitionError(B_TRANSLATE("Unable to find the selected "
			"partition by ID."));
		return;
	}

	BPartition* parent = partition->Parent();
	if (parent == NULL) {
		_DisplayPartitionError(B_TRANSLATE("The selected partition has no "
			"partitioning system to resize or move within."));
		return;
	}

	if (partition->IsMounted()) {
		_DisplayPartitionError(B_TRANSLATE("Unmount the partition before "
			"resizing or moving it."));
		return;
	}

	const char* filesystem = partition->ContentType();
	PartitionCapabilities caps;
	if (filesystem == NULL
		|| PartitionCapabilities::Get(filesystem, caps) != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("Could not determine what this "
			"partition's filesystem supports."));
		return;
	}

	bool canResize = caps.grow != "none" || caps.shrink != "none";
	bool canMoveType = caps.move != "none";
	if (!canResize && !canMoveType) {
		_DisplayPartitionError(B_TRANSLATE("This filesystem does not "
			"support resizing or moving."));
		return;
	}

	BPath path;
	if (partition->GetPath(&path) != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("Could not determine the "
			"partition's device path."));
		return;
	}

	off_t currentSizeMiB = partition->Size() / (1024 * 1024);
	off_t minSizeMiB = currentSizeMiB;
	off_t maxSizeMiB = currentSizeMiB;
	off_t usedMiB = -1;
	if (canResize) {
		status_t status = PartitionCapabilities::GetSizeLimits(path.Path(),
			filesystem, minSizeMiB, maxSizeMiB, &usedMiB);
		if (status != B_OK) {
			_DisplayPartitionError(B_TRANSLATE("Could not determine this "
				"partition's size limits; resizing is unavailable for now "
				"(move may still be)."), NULL, status);
			canResize = false;
			minSizeMiB = maxSizeMiB = currentSizeMiB;
			usedMiB = -1;
		}
	}

	off_t currentStartMiB = partition->Offset() / (1024 * 1024);
	off_t minStartMiB = currentStartMiB;
	off_t maxStartMiB = currentStartMiB;
	bool canMove = false;
	if (canMoveType) {
		ModificationPreparer modificationPreparer(disk);
		if (modificationPreparer.ModificationStatus() == B_OK) {
			BPartitioningInfo info;
			if (parent->GetPartitioningInfo(&info) == B_OK) {
				off_t partOffset = partition->Offset();
				off_t partSize = partition->Size();
				off_t beforeGap = 0;
				off_t afterGap = 0;
				off_t spaceOffset, spaceSize;
				for (int32 i = 0; info.GetPartitionableSpaceAt(i,
						&spaceOffset, &spaceSize) == B_OK; i++) {
					if (spaceOffset + spaceSize == partOffset)
						beforeGap = spaceSize;
					if (spaceOffset == partOffset + partSize)
						afterGap = spaceSize;
				}
				if (beforeGap > 0 || afterGap > 0) {
					minStartMiB = (partOffset - beforeGap) / (1024 * 1024);
					maxStartMiB = (partOffset + afterGap) / (1024 * 1024);
					canMove = true;
				}
			}
		}
	}

	if (!canResize && !canMove) {
		_DisplayPartitionError(B_TRANSLATE("There is no free space next to "
			"this partition to grow or move into, and its filesystem "
			"cannot be shrunk."));
		return;
	}

	ResizeMoveWindow* panel = new ResizeMoveWindow(this,
		B_TRANSLATE("Resize/move partition"), currentSizeMiB, minSizeMiB,
		maxSizeMiB, canResize, currentStartMiB, minStartMiB, maxStartMiB,
		canMove, usedMiB);

	off_t newSizeMiB = currentSizeMiB;
	off_t newStartMiB = currentStartMiB;
	status_t status = panel->Go(newSizeMiB, newStartMiB);
	if (status != B_OK) {
		if (status != B_CANCELED) {
			_DisplayPartitionError(B_TRANSLATE("The panel experienced a "
				"problem!"), NULL, status);
		}
		return;
	}

	bool sizeChanged = canResize && newSizeMiB != currentSizeMiB;
	bool startChanged = canMove && newStartMiB != currentStartMiB;
	if (!sizeChanged && !startChanged) {
		return;
	}

	PartitionReference* parentRef = new PartitionReference(parent->ID());
	PartitionReference* childRef = new PartitionReference(partition->ID());

	DiskDeviceJobQueue jobQueue;

	// Move must run before resize: refs resolve against the live devnode.
	if (startChanged) {
		MoveJob* moveJob = new MoveJob(parentRef, childRef);
		moveJob->Init(newStartMiB * 1024 * 1024, NULL, 0);
		jobQueue.AddJob(moveJob);
	}
	if (sizeChanged) {
		off_t newSizeBytes = newSizeMiB * 1024 * 1024;
		ResizeJob* resizeJob = new ResizeJob(parentRef, childRef,
			newSizeBytes, newSizeBytes);
		jobQueue.AddJob(resizeJob);
	}

	parentRef->ReleaseReference();
	childRef->ReleaseReference();

	BMessage result;
	status = jobQueue.ExecuteViaHelper(disk, &result);

	ProgressWindow* progress = new ProgressWindow(this, result);
	progress->Go();

	if (status != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("Could not resize or move the "
			"selected partition."), NULL, status, &result);
		return;
	}

	_ScanDrives();
}


void
MainWindow::_CheckFilesystem(BDiskDevice* disk, partition_id selectedPartition)
{
	if (disk == NULL || selectedPartition < 0) {
		_DisplayPartitionError(B_TRANSLATE("You need to select a partition "
			"entry from the list."));
		return;
	}

	BPartition* partition = disk->FindDescendant(selectedPartition);
	if (partition == NULL) {
		_DisplayPartitionError(B_TRANSLATE("Unable to find the selected "
			"partition by ID."));
		return;
	}

	PartitionReference* partitionRef
		= new PartitionReference(partition->ID());

	RepairJob* job = new RepairJob(partitionRef, true);

	partitionRef->ReleaseReference();

	BMessage result;
	DiskDeviceJobQueue jobQueue;
	jobQueue.AddJob(job);
	status_t status = jobQueue.ExecuteViaHelper(disk, &result);

	ProgressWindow* progress = new ProgressWindow(this, result);
	progress->Go();

	if (status != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("Checking the selected "
			"partition's filesystem failed, or found problems."), NULL,
			status, &result);
	}
}


void
MainWindow::_Erase(BDiskDevice* disk, partition_id selectedPartition)
{
	if (disk == NULL || selectedPartition < 0) {
		_DisplayPartitionError(B_TRANSLATE("You need to select a partition "
			"entry from the list."));
		return;
	}

	if (disk->IsReadOnly()) {
		_DisplayPartitionError(B_TRANSLATE("The selected disk is read-only."));
		return;
	}

	BPartition* partition = disk->FindDescendant(selectedPartition);
	if (partition == NULL) {
		_DisplayPartitionError(B_TRANSLATE("Unable to find the selected "
			"partition by ID."));
		return;
	}

	BPartition* parent = partition->Parent();

	BAlert* alert = new BAlert("final notice", B_TRANSLATE("Are you sure you "
		"want to erase the filesystem on the selected partition?\n\n"
		"All data on the partition will be irretrievably lost if you "
		"do so! The partition itself is not removed."),
		B_TRANSLATE("Erase filesystem"), B_TRANSLATE("Cancel"), NULL,
		B_WIDTH_FROM_WIDEST, B_WARNING_ALERT);
	alert->SetShortcut(1, B_ESCAPE);
	if (alert->Go() == 1)
		return;

	PartitionReference* parentRef = parent != NULL
		? new PartitionReference(parent->ID()) : NULL;
	PartitionReference* childRef = new PartitionReference(partition->ID());

	UninitializeJob* job = new UninitializeJob(childRef, parentRef);

	if (parentRef != NULL)
		parentRef->ReleaseReference();
	childRef->ReleaseReference();

	BMessage result;
	DiskDeviceJobQueue jobQueue;
	jobQueue.AddJob(job);
	status_t status = jobQueue.ExecuteViaHelper(disk, &result);

	ProgressWindow* progress = new ProgressWindow(this, result);
	progress->Go();

	if (status != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("Could not erase the filesystem "
			"on the selected partition."), NULL, status, &result);
		return;
	}

	_ScanDrives();
}


void
MainWindow::_Rename(BDiskDevice* disk, partition_id selectedPartition)
{
	if (disk == NULL || selectedPartition < 0) {
		_DisplayPartitionError(B_TRANSLATE("You need to select a partition "
			"entry from the list."));
		return;
	}

	if (disk->IsReadOnly()) {
		_DisplayPartitionError(B_TRANSLATE("The selected disk is read-only."));
		return;
	}

	BPartition* partition = disk->FindDescendant(selectedPartition);
	if (partition == NULL) {
		_DisplayPartitionError(B_TRANSLATE("Unable to find the selected "
			"partition by ID."));
		return;
	}

	RenamePanel* panel = new RenamePanel(this,
		B_TRANSLATE("Rename filesystem"), B_TRANSLATE("New label:"),
		partition->ContentName().String());

	BString newLabel;
	status_t status = panel->Go(newLabel);
	if (status != B_OK) {
		if (status != B_CANCELED) {
			_DisplayPartitionError(B_TRANSLATE("The panel experienced a "
				"problem!"), NULL, status);
		}
		return;
	}

	PartitionReference* partitionRef
		= new PartitionReference(partition->ID());

	SetStringJob* job = new SetStringJob(partitionRef);
	job->Init(newLabel.String(), B_DISK_DEVICE_JOB_SET_CONTENT_NAME);

	partitionRef->ReleaseReference();

	BMessage result;
	DiskDeviceJobQueue jobQueue;
	jobQueue.AddJob(job);
	status = jobQueue.ExecuteViaHelper(disk, &result);

	ProgressWindow* progress = new ProgressWindow(this, result);
	progress->Go();

	if (status != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("Could not rename the selected "
			"partition's filesystem."), NULL, status, &result);
		return;
	}

	_ScanDrives();
}


void
MainWindow::_RenameGpt(BDiskDevice* disk, partition_id selectedPartition)
{
	if (disk == NULL || selectedPartition < 0) {
		_DisplayPartitionError(B_TRANSLATE("You need to select a partition "
			"entry from the list."));
		return;
	}

	if (disk->IsReadOnly()) {
		_DisplayPartitionError(B_TRANSLATE("The selected disk is read-only."));
		return;
	}

	BPartition* partition = disk->FindDescendant(selectedPartition);
	if (partition == NULL) {
		_DisplayPartitionError(B_TRANSLATE("Unable to find the selected "
			"partition by ID."));
		return;
	}

	RenamePanel* panel = new RenamePanel(this,
		B_TRANSLATE("Rename partition"), B_TRANSLATE("New name:"),
		partition->Name());

	BString newName;
	status_t status = panel->Go(newName);
	if (status != B_OK) {
		if (status != B_CANCELED) {
			_DisplayPartitionError(B_TRANSLATE("The panel experienced a "
				"problem!"), NULL, status);
		}
		return;
	}

	PartitionReference* partitionRef
		= new PartitionReference(partition->ID());

	SetStringJob* job = new SetStringJob(partitionRef);
	job->Init(newName.String(), B_DISK_DEVICE_JOB_SET_NAME);

	partitionRef->ReleaseReference();

	BMessage result;
	DiskDeviceJobQueue jobQueue;
	jobQueue.AddJob(job);
	status = jobQueue.ExecuteViaHelper(disk, &result);

	ProgressWindow* progress = new ProgressWindow(this, result);
	progress->Go();

	if (status != B_OK) {
		_DisplayPartitionError(B_TRANSLATE("Could not rename the selected "
			"partition."), NULL, status, &result);
		return;
	}

	_ScanDrives();
}


void
MainWindow::_ShowFeatures()
{
	FeaturesWindow* window = new FeaturesWindow(this);
	window->Show();
}


float
MainWindow::_ColumnListViewHeight(BColumnListView* list, BRow* currentRow)
{
	float height = 0;
	int32 rows = list->CountRows(currentRow);
	for (int32 i = 0; i < rows; i++) {
		BRow* row = list->RowAt(i, currentRow);
		height += row->Height() + 1;
		if (row->IsExpanded() && list->CountRows(row) > 0)
			height += _ColumnListViewHeight(list, row);
	}
	return height;
}


void
MainWindow::_UpdateWindowZoomLimits()
{
	float maxHeight = 0;
	int32 numColumns = fListView->CountColumns();
	BRow* parentRow = fListView->RowAt(0, NULL);
	BColumn* column = NULL;

	maxHeight += _ColumnListViewHeight(fListView, NULL);

	float maxWidth = fListView->LatchWidth();
	for (int32 i = 0; i < numColumns; i++) {
		column = fListView->ColumnAt(i);
		maxWidth += column->Width();
	}

	maxHeight += B_H_SCROLL_BAR_HEIGHT;
	maxHeight += 1.5 * parentRow->Height();	// the label row
	maxHeight += fDiskView->Bounds().Height();
	maxHeight += fMenuBar->Bounds().Height();
	maxWidth += 1.5 * B_V_SCROLL_BAR_WIDTH;	// scroll bar & borders

	SetZoomLimits(maxWidth, maxHeight);
}

