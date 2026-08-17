/*
 * Copyright 2000, Georges-Edouard Berenger. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "AutoIcon.h"
#include "Utilities.h"

#include <Bitmap.h>
#include <ControlLook.h>
#include <Entry.h>
#include <File.h>
#include <IconUtils.h>
#include <MimeType.h>
#include <NodeInfo.h>
#include <Resources.h>
#include <Roster.h>


// As a Deskbar replicant we live in Deskbar's team, so be_app->AppResources()
// would hand back Deskbar's resources rather than ours.
static BResources*
own_resources()
{
	static BResources sResources;
	static bool sTried = false;
	static bool sValid = false;

	if (!sTried) {
		sTried = true;

		entry_ref ref;
		find_self(ref);

		BFile file(&ref, B_READ_ONLY);
		sValid = file.InitCheck() == B_OK && sResources.SetTo(&file) == B_OK;
	}

	return sValid ? &sResources : NULL;
}


AutoIcon::~AutoIcon()
{
	delete fBitmap;
}


BBitmap*
AutoIcon::Bitmap()
{
	if (fBitmap != NULL)
		return fBitmap;

	if (fSignature) {
		fBitmap = new BBitmap(BRect(BPoint(0, 0),
			be_control_look->ComposeIconSize(B_MINI_ICON)), B_RGBA32);

		entry_ref ref;
		be_roster->FindApp (fSignature, &ref);
		if (BNodeInfo::GetTrackerIcon(&ref, fBitmap, (icon_size)-1) != B_OK) {
			BMimeType genericAppType(B_APP_MIME_TYPE);
			genericAppType.GetIcon(fBitmap, (icon_size)(fBitmap->Bounds().IntegerWidth() + 1));
		}
	} else if (fResourceName != NULL) {
		BResources* resources = own_resources();
		size_t size = 0;
		const void* data = resources == NULL ? NULL
			: resources->LoadResource(B_VECTOR_ICON_TYPE, fResourceName, &size);

		if (data != NULL) {
			fBitmap = new BBitmap(BRect(BPoint(0, 0),
				be_control_look->ComposeIconSize(B_MINI_ICON)), B_RGBA32);

			if (BIconUtils::GetVectorIcon((const uint8*)data, size, fBitmap)
					!= B_OK) {
				delete fBitmap;
				fBitmap = NULL;
			}
		}
	} else if (fbits) {
		fBitmap = new BBitmap(BRect(BPoint(0, 0),
			BSize(B_MINI_ICON - 1, B_MINI_ICON - 1)), B_RGBA32);

		fBitmap->SetBits(fbits, 256, 0, B_CMAP8);
	}
	return fBitmap;
}
