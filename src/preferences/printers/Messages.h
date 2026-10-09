/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _MESSAGES_H
#define _MESSAGES_H


#include <SupportDefs.h>


const uint32 kMsgAddPrinter         = 'AddP';
const uint32 kMsgAddPrinterClosed   = 'APCl';
const uint32 kMsgRemovePrinter      = 'RemP';
const uint32 kMsgMakeDefaultPrinter = 'MDfP';
const uint32 kMsgPrinterSelected    = 'PSel';
const uint32 kMsgCancelJob          = 'CncJ';
const uint32 kMsgRestartJob         = 'RstJ';
const uint32 kMsgJobSelected        = 'JSel';
const uint32 kMsgPrintTestPage      = 'PtPg';
const uint32 kMsgRenamePrinter      = 'RnP0';
const uint32 kMsgEnablePrinter      = 'EnP0';
const uint32 kMsgDisablePrinter     = 'DsP0';
const uint32 kMsgPrinterOptions     = 'PtOp';
const uint32 kMsgHoldJob            = 'HldJ';
const uint32 kMsgReleaseJob         = 'RlsJ';
const uint32 kMsgPurgeJobs          = 'PrgJ';
const uint32 kMsgRefresh            = 'RfsH';

// AddPrinterDialog
const uint32 kMsgDiscover           = 'Disc';
const uint32 kMsgDiscoverDone       = 'DscD';
const uint32 kMsgDeviceSelected     = 'DvSl';
const uint32 kMsgPpdsLoaded         = 'PpdL';
const uint32 kMsgAddConfirm         = 'AdCf';
const uint32 kMsgAddDone            = 'AdDn';
const uint32 kMsgToggleMode         = 'TgMd';

// PrinterOptionsDialog
const uint32 kMsgOptionsChoices     = 'OpCh';
const uint32 kMsgOptionsSave        = 'OpSv';
const uint32 kMsgOptionsDone        = 'OpDn';
const uint32 kMsgRenameSave         = 'RnSv';
const uint32 kMsgRenameDone         = 'RnDn';


#endif // _MESSAGES_H
