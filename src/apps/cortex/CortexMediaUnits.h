/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _CORTEX_MEDIA_UNITS_H
#define _CORTEX_MEDIA_UNITS_H


#include <Entry.h>
#include <Locker.h>

#include <media2/MediaClient.h>
#include <media2/MediaDefs.h>
#include <media2/MediaUnit.h>


class BMediaFile;
class BMediaInput;
class BMediaOutput;
class BMediaTrack;
struct entry_ref;


struct CortexUnitInfo {
	const char*		id;
	const char*		name;
	const char*		kind;
};


class CortexUnitCatalog {
public:
	static	int32				CountUnits();
	static	const CortexUnitInfo*	UnitInfoAt(int32 index);
	static	BMediaClient*		CreateUnit(int32 index);
};


class CortexDecoderUnit : public BMediaUnit {
public:
								CortexDecoderUnit();
	virtual						~CortexDecoderUnit();

protected:
	virtual	void				ProcessCycle(uint32 frameCount);

private:
			void				_ConnectPipeline();
			void				_DisconnectPipeline();

			BMediaInput*		fInput;
			BMediaOutput*		fOutput;
			void*				fPipeline;
			void*				fAppSrc;
			void*				fAppSink;
};


class CortexEncoderUnit : public BMediaUnit {
public:
								CortexEncoderUnit();
	virtual						~CortexEncoderUnit();

protected:
	virtual	void				ProcessCycle(uint32 frameCount);

private:
			status_t			_ConnectPipeline(const BMediaFormat& rawFormat);
			void				_DisconnectPipeline();

			BMediaInput*		fInput;
			BMediaOutput*		fOutput;
			void*				fPipeline;
			void*				fAppSrc;
			void*				fAppSink;
};


class CortexFileReaderUnit : public BMediaUnit {
public:
								CortexFileReaderUnit();
	virtual						~CortexFileReaderUnit();

			status_t			SetFile(const entry_ref& ref);
			status_t			ProposeOutputFormat(BMediaFormat* format);

protected:
	virtual	void				ProcessCycle(uint32 frameCount);

private:
			void				_CloseFile();

			BMediaOutput*		fOutput;
			BLocker				fFileLock;
			BMediaFile*			fFile;
			BMediaTrack*		fTrack;
			media_raw_audio_format	fTrackFormat;
};


class CortexFileWriterUnit : public BMediaUnit {
public:
								CortexFileWriterUnit();
	virtual						~CortexFileWriterUnit();

			status_t			SetFile(const entry_ref& ref,
									const media_file_format& fileFormat);

protected:
	virtual	void				ProcessCycle(uint32 frameCount);
	virtual	void				HandleStop(bigtime_t performanceTime);

private:
			status_t			_OpenOutput(const media_raw_audio_format& raw);
			void				_CloseOutput();

			BMediaInput*		fInput;
			BLocker				fLock;
			bool				fPendingOpen;
			entry_ref			fOutputRef;
			media_file_format	fFileFormat;
			BMediaFile*			fFile;
			BMediaTrack*		fTrack;
			media_raw_audio_format	fTrackFormat;
};


#endif // _CORTEX_MEDIA_UNITS_H
