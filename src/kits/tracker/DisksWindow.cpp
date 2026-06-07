/*
 * Copyright 2024, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifdef __VOS__

#include "DisksWindow.h"

#include "Model.h"


namespace BPrivate {


BDisksPoseView::BDisksPoseView(Model* model)
	:
	BPoseView(model, kIconMode)
{
}


BDisksWindow::BDisksWindow(LockingList<BWindow>* windowList, uint32 openFlags)
	:
	BContainerWindow(windowList, openFlags)
{
}


BPoseView*
BDisksWindow::NewPoseView(Model* model, uint32 viewMode)
{
	return new BDisksPoseView(model);
}


BDisksPoseView*
BDisksWindow::PoseView() const
{
	return static_cast<BDisksPoseView*>(fPoseView);
}


} // namespace BPrivate

#endif // __VOS__
