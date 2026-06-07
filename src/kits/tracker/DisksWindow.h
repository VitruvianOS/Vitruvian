/*
 * Copyright 2024, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef DISKS_WINDOW_H
#define DISKS_WINDOW_H

#ifdef __VOS__

#include "ContainerWindow.h"
#include "PoseView.h"


namespace BPrivate {


class BDisksPoseView : public BPoseView {
public:
								BDisksPoseView(Model* model);

	virtual	bool				IsVolumesRoot() const { return true; }

private:
			typedef BPoseView	_inherited;
};


class BDisksWindow : public BContainerWindow {
public:
								BDisksWindow(LockingList<BWindow>* windowList,
									uint32 openFlags);

			BDisksPoseView*		PoseView() const;

protected:
	virtual	BPoseView*			NewPoseView(Model* model, uint32 viewMode);

private:
			typedef BContainerWindow _inherited;
};


} // namespace BPrivate

#endif // __VOS__
#endif // DISKS_WINDOW_H
