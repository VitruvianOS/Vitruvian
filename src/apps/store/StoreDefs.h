/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _STORE_DEFS_H
#define _STORE_DEFS_H


#define kStoreSignature "application/x-vnd.Vitruvian-Store"


// Page -> C++ via the "store" script message handler. The payload is a
// plain string "<action>|<argument>" (kept trivial so the C++ side
// needs no JSON parser):
//   ready              page loaded, wants the initial state
//   section|<kind>     Apps/Components/Data tab pressed
//   select|<category>  category button pressed (apps section)
//   install|<id>       Install pressed for a catalog entry
//   remove|<id>        Remove pressed for a catalog entry
enum {
	kStoreMsgReady		= 'stRd',
	kStoreMsgSelect		= 'stSe',
	kStoreMsgInstall	= 'stIn',
	kStoreMsgRemove		= 'stRm',
	kStoreMsgSection	= 'stKd'
};


// Window-private messages (worker replies, deferred page load, the
// EvaluateJavaScript reply, whose result string we never read, the
// fetched catalog.json).
enum {
	kStoreMsgLoadPage		= 'stLp',
	kStoreMsgRefreshState	= 'stRf',
	kStoreMsgScriptDone		= 'stJs',
	kStoreMsgCatalogFetched	= 'stCf'
};


#endif // _STORE_DEFS_H
