/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#ifndef _PHOTO_WRITER_H
#define _PHOTO_WRITER_H


#include <Bitmap.h>
#include <DataIO.h>
#include <Path.h>
#include <String.h>


class PhotoWriter {
public:
	// JPEG through the Translation Kit plus a minimal EXIF block (date,
	// camera name, orientation); sets the MIME attribute as well.
	static	status_t			Save(const BBitmap* bitmap,
									const char* directory,
									const char* cameraName,
									uint32 orientation, BPath* outPath);

	static	status_t			DecodeFile(const char* path,
									BBitmap** outBitmap);

	// Minimal APP1 (TIFF) block; exposed for tests.
	static	void				BuildExif(const char* cameraName,
									const char* dateTime, uint32 orientation,
									BMallocIO* outExif);
	static	status_t			ReadExif(const char* path, uint32* orientation,
									BString* cameraName, BString* dateTime);
};


#endif // _PHOTO_WRITER_H
