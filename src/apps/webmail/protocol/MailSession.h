/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEB_MAIL_SESSION_H
#define WEB_MAIL_SESSION_H


#include "../Accounts.h"
#include "../protocol/MailTypes.h"
#include "ImapClient.h"
#include "Pop3Client.h"

#include <Messenger.h>
#include <String.h>

#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>


// One native (IMAP or POP3) account driven from a worker thread.
// Results land back on the window messenger as MSG_NATIVE_EVENT with
// WEBMAIL_ACCOUNT_ID_FIELD plus a sub-what in "webmail:what":
// folders, headers, message, sent, error, new-mail.
enum {
	kNativeEventFolders		= 'WnFd',
	kNativeEventHeaders		= 'WnHd',
	kNativeEventMessage		= 'WnMs',
	kNativeEventSent		= 'WnSn',
	kNativeEventError		= 'WnEr',
	kNativeEventNewMail		= 'WnNm',
	kNativeEventSearch		= 'WnSh'
};


class MailSession {
public:
						MailSession(const Account& account,
							const BMessenger& target);
						~MailSession();

			void		Start();
			void		Stop();

			// All of these queue work onto the session thread.
			void		Sync();
			void		RequestFolders();
			void		RequestHeaders(const BString& folder, uint32 offset,
								uint32 count);
			void		RequestMessage(const BString& folder, uint32 uid);
			void		SetFlags(const BString& folder,
								const std::vector<uint32>& uids, bool seen);
			void		MoveMessages(const BString& folder,
								const std::vector<uint32>& uids,
								const BString& destination);
			void		DeleteMessages(const BString& folder,
								const std::vector<uint32>& uids);
			void		Search(const BString& folder,
								const BString& criteria);
			void		SendMessage(const MailCompose& compose);
			void		SaveDraft(const MailCompose& compose);

			// OAuth access token for XOAUTH2; empty until set.
			void		SetAccessToken(const BString& token);

private:
			void		_ThreadFunc();
			void		_ProcessQueue();
			bool		_Connect(BString& error);
			void		_Disconnect();
			void		_SyncFolders();
			void		_SyncHeaders(const BString& folder, uint32 offset,
								uint32 count);
			void		_FetchMessage(const BString& folder, uint32 uid);
			void		_SendOrDraft(const MailCompose& compose,
								bool draft);
			void		_PostEvent(uint32 what, BMessage* extra = NULL);

			struct Command {
				uint32						what;
				BString						folder;
				BString					destination;
				BString						criteria;
				uint32						uid;
				uint32						offset;
				uint32						count;
				std::vector<uint32>				uids;
				bool						flag;
				MailCompose					compose;
				bool						draft;
			};

		Account			fAccount;
		BMessenger		fTarget;
		ImapClient		fImap;
		Pop3Client		fPop3;
		BString			fAccessToken;
		BString			fSelectedFolder;
		bool			fConnected;

		std::thread		fThread;
		std::mutex		fMutex;
		std::condition_variable	fCondition;
		bool			fQuit;
		std::vector<Command>		fQueue;
};


#endif // WEB_MAIL_SESSION_H
