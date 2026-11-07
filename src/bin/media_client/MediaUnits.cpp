/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MediaUnits.h"

#include <stdio.h>

#include "CortexMediaUnits.h"


void
media_units()
{
	for (int32 i = 0; i < CortexUnitCatalog::CountUnits(); i++) {
		const CortexUnitInfo* info = CortexUnitCatalog::UnitInfoAt(i);
		if (info != NULL)
			printf("%s\t%s\t%s\n", info->id, info->name, info->kind);
	}
}
