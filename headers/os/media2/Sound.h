/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_SOUND_H
#define _MEDIA2_SOUND_H


#include <Entry.h>

#include <media2/MediaFormat.h>


class BSound {
public:
							BSound(const entry_ref* soundFile,
								bool loadIntoMemory = false);
							BSound(const void* data, size_t size,
								const BMediaFormat& format);
	virtual					~BSound();

			status_t		InitCheck() const;

			const BMediaFormat&	Format() const;
			bigtime_t			Duration() const;
			const entry_ref*	GetEntry() const;

			const void*		Data() const;
			size_t			Size() const;

			bool			GetDataAt(off_t offset, void* buffer,
								size_t bufferSize, size_t* outUsed);

			BSound*			Acquire();
			BSound*			AcquireRef() { return Acquire(); }

			bool			Release();
			int32			RefCount() const { return fRefCount; }


private:
			entry_ref		fEntry;
			void*			fData;
			size_t			fSize;
			BMediaFormat	fFormat;
			bigtime_t		fDuration;
			int32			fRefCount;
			status_t		fInitErr;
			bool			fOwnsData;
};


#endif
