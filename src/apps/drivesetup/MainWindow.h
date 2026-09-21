/*
 * Copyright 2002-2013 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Ithamar R. Adema <ithamar@unet.nl>
 *		Stephan Aßmus <superstippi@gmx.de>
 */
#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H


#include <DiskDeviceRoster.h>
#include <FilePanel.h>
#include <Notification.h>
#include <PopUpMenu.h>
#include <Window.h>

#include "Support.h"


class BColumnListView;
class BDiskDevice;
class BMenu;
class BMenuBar;
class BMenuItem;
class BPartition;
class BRow;
class DiskView;
class PartitionListView;


enum {
	MSG_SELECTED_PARTITION_ID	= 'spid',
	MSG_UPDATE_ZOOM_LIMITS		= 'uzls'
};


class MainWindow : public BWindow {
public:
								MainWindow();
	virtual						~MainWindow();

	// BWindow interface
	virtual	bool				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				MenusBeginning();

	// MainWindow
			status_t			StoreSettings(BMessage* archive) const;
			status_t			RestoreSettings(BMessage* archive);
			void				ApplyDefaultSettings();

			status_t			RegisterFileDiskDevice(const char* fileName);
			status_t			UnRegisterFileDiskDevice(const char* fileName);
	static	int32				SaveDiskImage(void* data);
	static	int32				WriteDiskImage(void* data);

private:
	static	void				_WriteDiskImage(BMessenger messenger,
									BFile source, BFile target,
									const char* targetpath,
									BString title, BString status);

			void				_ScanDrives();

			void				_AdaptToSelectedPartition();
			void				_SetToDiskAndPartition(partition_id diskID,
									partition_id partitionID,
									partition_id parentID);
			void				_UpdateMenus(BDiskDevice* disk,
									partition_id selectedPartition,
									partition_id parentID);

			void				_DisplayPartitionError(BString message,
									const BPartition* partition = NULL,
									status_t error = B_OK,
									const BMessage* result = NULL) const;

			void				_Mount(BDiskDevice* disk,
									partition_id selectedPartition);
			void				_Unmount(BDiskDevice* disk,
									partition_id selectedPartition);
			void				_MountAll();

			void				_Initialize(BDiskDevice* disk,
									partition_id selectedPartition,
									const BString& diskSystemName);
			void				_Create(BDiskDevice* disk,
									partition_id selectedPartition);
			void				_Delete(BDiskDevice* disk,
									partition_id selectedPartition);
			void				_ChangeParameters(BDiskDevice* disk,
									partition_id selectedPartition);
			void				_SetFlags(BDiskDevice* disk,
									partition_id selectedPartition);
			void				_ResizeMove(BDiskDevice* disk,
									partition_id selectedPartition);
			void				_CheckFilesystem(BDiskDevice* disk,
									partition_id selectedPartition);
			void				_Erase(BDiskDevice* disk,
									partition_id selectedPartition);
			void				_Rename(BDiskDevice* disk,
									partition_id selectedPartition);
			void				_RenameGpt(BDiskDevice* disk,
									partition_id selectedPartition);
			void				_ShowFeatures();
			float				_ColumnListViewHeight(BColumnListView* list,
									BRow* currentRow);
			void				_UpdateWindowZoomLimits();

private:
			BDiskDeviceRoster	fDiskDeviceRoster;
			BDiskDevice*		fCurrentDisk;
			partition_id		fCurrentPartitionID;

			PartitionListView*	fListView;
			DiskView*			fDiskView;

			SpaceIDMap			fSpaceIDMap;

			BMenu*				fDiskMenu;
			BMenu*				fDiskInitMenu;

			BMenu*				fPartitionMenu;
			BMenu*				fFormatMenu;
			BMenu*				fDiskImageMenu;
			BMenu*				fToolsMenu;

			BMenuBar* 			fMenuBar;

			BMenuItem*			fWipeMenuItem;
			BMenuItem*			fEjectMenuItem;
			BMenuItem*			fSurfaceTestMenuItem;
			BMenuItem*			fRescanMenuItem;

			BMenuItem*			fCreateMenuItem;
			BMenuItem*			fChangeMenuItem;
			BMenuItem*			fResizeMenuItem;
			BMenuItem*			fFlagsMenuItem;
			BMenuItem*			fCheckMenuItem;
			BMenuItem*			fEraseMenuItem;
			BMenuItem*			fRenameMenuItem;
			BMenuItem*			fRenameGptMenuItem;
			BMenuItem*			fDeleteMenuItem;
			BMenuItem*			fMountMenuItem;
			BMenuItem*			fUnmountMenuItem;
			BMenuItem*			fMountAllMenuItem;
			BMenuItem*			fOpenDiskProbeMenuItem;
			BMenuItem*			fFeaturesMenuItem;

			BMenu*				fFormatContextMenuItem;
			BMenuItem*			fCreateContextMenuItem;
			BMenuItem*			fChangeContextMenuItem;
			BMenuItem*			fDeleteContextMenuItem;
			BMenuItem*			fMountContextMenuItem;
			BMenuItem*			fUnmountContextMenuItem;
			BMenuItem*			fOpenDiskProbeContextMenuItem;
			BPopUpMenu*			fContextMenu;

			BMenuItem*			fRegisterMenuItem;
			BMenuItem*			fUnRegisterMenuItem;
			BMenuItem*			fSaveMenuItem;
			BMenuItem*			fWriteMenuItem;

			BFilePanel*			fRegisterFilePanel;
			BFilePanel*			fWriteImageFilePanel;
			BFilePanel*			fReadImageFilePanel;

			BNotification*		fNotification;
};


#endif // MAIN_WINDOW_H
