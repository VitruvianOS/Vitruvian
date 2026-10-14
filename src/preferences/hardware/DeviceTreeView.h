/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _DEVICE_TREE_VIEW_H
#define _DEVICE_TREE_VIEW_H


#include <Messenger.h>
#include <View.h>

#include <string>
#include <vector>

#include "HardwareModel.h"
#include "HwdataDatabase.h"


// One preflet tab: outline of the device categories with the devices
// underneath, and a read-only property pane on the right (details on
// top, the udev property dump below the splitter).


class BListItem;
class BOutlineListView;
class BStringView;
class BTextView;


class DeviceTreeView : public BView {
public:
							DeviceTreeView(Hardware::DeviceTab tab,
								const BMessenger& parent);

			void			SetRecords(
								const std::vector<
									Hardware::DeviceRecord>&
										records,
								const Hardware::HwdataDatabase* names);

			// Pointer into the last snapshot; owned by this view.
			const Hardware::DeviceRecord*
							SelectedRecord() const;

			// Property pane text for the current selection.
			std::string		SelectedDetail(bool includeProperties) const;

			void			SetPrivilegedText(const std::string& text);

			void			ShowEmptyState();

	virtual	void			MessageReceived(BMessage* message);

private:
			void			_Rebuild();
			void			_UpdateDetails();
			const Hardware::HwdataDatabase&
							_Names() const;
			const Hardware::DeviceRecord*
							_ItemRecord(BListItem* item) const;

private:
			Hardware::DeviceTab
							fTab;
			BMessenger		fParent;
			const Hardware::HwdataDatabase*
							fNames;
			Hardware::HwdataDatabase
							fEmptyNames;

			BOutlineListView*
							fList;
			BTextView*		fDetail;
			BTextView*		fAdvanced;
			BStringView*	fAdvancedCaption;

			std::vector<Hardware::DeviceRecord>
							fRecords;
			std::string		fPrivilegedText;
			std::string		fSelectedSyspath;
};


#endif	// _DEVICE_TREE_VIEW_H
