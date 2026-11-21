/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef PACKAGE_ERRORS_H
#define PACKAGE_ERRORS_H

#include <String.h>
#include <SupportDefs.h>


// User-facing text for a kAptError* code.
BString PackageErrorText(int32 error);

#endif	// PACKAGE_ERRORS_H
