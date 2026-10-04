/*
 * Copyright 2005-2006, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <Application.h>
#include <Catalog.h>

#include "ICOTranslator.h"
#include "ICO.h"
#include "TranslatorWindow.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "main"

int
main(int /*argc*/, char ** /*argv*/)
{
	BApplication app("application/x-vnd.vos-ICOTranslator");

	status_t result;
	result = LaunchTranslatorWindow(new ICOTranslator, 
		B_TRANSLATE("ICO Settings"), BRect(0, 0, 225, 175));
	if (result != B_OK)
		return 1;

	app.Run();
	return 0;
}

