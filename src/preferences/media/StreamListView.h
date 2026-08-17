/*
 * Copyright 2026, Vitruvian Project.
 * StreamListView — list of application streams (PipeWire "Stream" nodes).
 * Distributed under the terms of the MIT License.
 */

#ifndef STREAM_LIST_VIEW_H
#define STREAM_LIST_VIEW_H


#include <GroupView.h>
#include <ObjectList.h>


class BListView;
class BSlider;
class BCheckBox;
class BMenuField;
class BPopUpMenu;
class BStringView;


class StreamListView : public BGroupView {
public:
							StreamListView();
	virtual					~StreamListView();

	virtual	void			AttachedToWindow();
	virtual	void			MessageReceived(BMessage* message);

			void			Refresh();

private:
			void			_ShowDetailFor(int32 index);
			void			_PopulateRouteMenu(bool isOutput,
								uint32 currentDeviceId);

			BListView*		fList;
			BSlider*		fVolume;
			BCheckBox*		fMute;
			BPopUpMenu*		fRouteMenu;
			BMenuField*		fRouteField;
			BStringView*	fEmptyLabel;

			uint32			fCurrentStreamId;
			bool			fSuppressVolumeMsg;
};


#endif
