/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>. Distributed under the terms of the
 * MIT License.
 */
#ifndef COPY_ENGINE_H
#define COPY_ENGINE_H


#include <OS.h>
#include <SupportDefs.h>


class ProgressReporter;


class CopyEngine {
public:
								CopyEngine(ProgressReporter* reporter,
									sem_id cancelSem);
								~CopyEngine();

			status_t			Copy(const char* source, const char* target);

private:
			status_t			_RunRsync(const char* source,
									const char* target, int outFd);
			void				_HandleProgressLine(const char* line);

private:
			ProgressReporter*	fReporter;
			sem_id				fCancelSem;
			int					fLastReportedPercent;
};


#endif	// COPY_ENGINE_H
