/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SECURITY_REPLICANT_H
#define _SECURITY_REPLICANT_H


#include "FirewallModel.h"

#include <Messenger.h>
#include <OS.h>
#include <String.h>
#include <View.h>


class BMessageRunner;
class BPopUpMenu;
class BShape;


extern const char* kSecurityReplicantName;
extern const char* kSecuritySignature;


// Everything the shield shows.
struct SecurityStatus {
	bool			firewallValid;
	bool			firewallEnabled;
	int32			profile;
};


// Deskbar shield: polls the read-only firewall helper and offers a
// firewall toggle plus "open Security preflet".
class SecurityReplicant : public BView {
public:
								SecurityReplicant(BRect frame);
								SecurityReplicant(BMessage* archive);
	virtual						~SecurityReplicant();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MessageReceived(BMessage* message);

private:
	struct WorkerArgs {
		BMessenger	target;
		bool		toggle;
		int32		setProfile;
		BString		prefer;
	};

	static	int32				_WorkerEntry(void* data);

			void				_Init();
			void				_StartWorker(bool toggle, int32 setProfile = -1);
			void				_UpdateStatus(const SecurityStatus& status);
			BPopUpMenu*			_BuildMenu();

			SecurityStatus		fStatus;
			BMessageRunner*		fRunner;
			bool				fWorking;
			bool				fPending;
			BString				fBackend;
};


#endif // _SECURITY_REPLICANT_H
