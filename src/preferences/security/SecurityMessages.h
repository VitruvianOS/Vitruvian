/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SECURITY_MESSAGES_H
#define _SECURITY_MESSAGES_H


enum {
	kMsgFirewallEnabled = 'fwen',
	kMsgOutPolicy = 'fwou',
	kMsgLogging = 'fwlg',
	kMsgRuleRemove = 'fwrm',
	kMsgDeskbar = 'fwdk',
	kMsgReplicantState = 'fwst',
	kMsgReplicantPoll = 'fwpl',
	kMsgReplicantToggle = 'fwtg',
	kMsgOpenSecurity = 'fwop',
	kMsgAddRule = 'fwar',
	kMsgRulesChosen = 'fwrc',
	kMsgAdvanced = 'fwav',
	kMsgProfile = 'fwpr',
	kMsgReplicantProfile = 'fwrp',
	kMsgRestoreDefaults = 'fwrd',
};


#endif // _SECURITY_MESSAGES_H
