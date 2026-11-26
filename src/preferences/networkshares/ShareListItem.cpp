/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ShareListItem.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "NetworkShares"


ShareListItem::ShareListItem(const ShareListEntry& entry)
	:
	BStringItem(""),
	fEntry(entry)
{
	UpdateItemText();
}


void
ShareListItem::UpdateItemText()
{
	const VosNetworkShareSpec& spec = fEntry.spec;
	BString text;
	text << spec.server.c_str();
	if (spec.protocol == kShareProtocolNFS)
		text << ":" << spec.exportName.c_str();
	else
		text << "/" << spec.exportName.c_str();
	text << "  ->  " << spec.mountPoint.c_str();
	text << "  [" << VosNetworkSharesProtocolName(spec.protocol) << ", "
		<< VosNetworkSharesModeName(spec.mode) << "]";

	if (fEntry.mounted)
		text << "  mounted";
	else if (fEntry.unreachable)
		text << "  unreachable";
	else
		text << "  not mounted";

	SetText(text.String());
}
