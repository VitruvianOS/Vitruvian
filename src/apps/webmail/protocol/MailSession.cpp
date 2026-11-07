/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MailSession.h"
#include "../OAuth2.h"
#include "../WebMailMessages.h"
#include "MimeBuilder.h"
#include "SmtpClient.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <vector>


namespace {


const uint32 kCmdSync				= 'WcSy';
const uint32 kCmdFolders			= 'WcFd';
const uint32 kCmdHeaders			= 'WcHd';
const uint32 kCmdMessage			= 'WcMs';
const uint32 kCmdFlags				= 'WcFl';
const uint32 kCmdMove				= 'WcMv';
const uint32 kCmdDelete				= 'WcDe';
const uint32 kCmdSearch				= 'WcSe';
const uint32 kCmdSend				= 'WcSn';
const uint32 kCmdDraft				= 'WcDr';


MailCompose
CloneCompose(const MailCompose& compose)
{
	return compose;
}


} // namespace


MailSession::MailSession(const Account& account, const BMessenger& target)
	:
	fAccount(account),
	fTarget(target),
	fConnected(false),
	fQuit(false)
{
}


MailSession::~MailSession()
{
	Stop();
}


void
MailSession::Start()
{
	if (fThread.joinable())
		return;
	fThread = std::thread(&MailSession::_ThreadFunc, this);
}


void
MailSession::Stop()
{
	{
		std::lock_guard<std::mutex> lock(fMutex);
		fQuit = true;
	}
	fCondition.notify_all();
	if (fThread.joinable())
		fThread.join();
}


void
MailSession::SetAccessToken(const BString& token)
{
	std::lock_guard<std::mutex> lock(fMutex);
	fAccessToken = token;
}


void
MailSession::Sync()
{
	Command command = {};
	command.what = kCmdSync;
	std::lock_guard<std::mutex> lock(fMutex);
	fQueue.push_back(command);
	fCondition.notify_all();
}


void
MailSession::RequestFolders()
{
	Command command = {};
	command.what = kCmdFolders;
	std::lock_guard<std::mutex> lock(fMutex);
	fQueue.push_back(command);
	fCondition.notify_all();
}


void
MailSession::RequestHeaders(const BString& folder, uint32 offset,
	uint32 count)
{
	Command command = {};
	command.what = kCmdHeaders;
	command.folder = folder;
	command.offset = offset;
	command.count = count;
	std::lock_guard<std::mutex> lock(fMutex);
	fQueue.push_back(command);
	fCondition.notify_all();
}


void
MailSession::RequestMessage(const BString& folder, uint32 uid)
{
	Command command = {};
	command.what = kCmdMessage;
	command.folder = folder;
	command.uid = uid;
	std::lock_guard<std::mutex> lock(fMutex);
	fQueue.push_back(command);
	fCondition.notify_all();
}


void
MailSession::SetFlags(const BString& folder, const std::vector<uint32>& uids,
	bool seen)
{
	Command command = {};
	command.what = kCmdFlags;
	command.folder = folder;
	command.uids = uids;
	command.flag = seen;
	std::lock_guard<std::mutex> lock(fMutex);
	fQueue.push_back(command);
	fCondition.notify_all();
}


void
MailSession::MoveMessages(const BString& folder,
	const std::vector<uint32>& uids, const BString& destination)
{
	Command command = {};
	command.what = kCmdMove;
	command.folder = folder;
	command.uids = uids;
	command.destination = destination;
	std::lock_guard<std::mutex> lock(fMutex);
	fQueue.push_back(command);
	fCondition.notify_all();
}


void
MailSession::DeleteMessages(const BString& folder,
	const std::vector<uint32>& uids)
{
	Command command = {};
	command.what = kCmdDelete;
	command.folder = folder;
	command.uids = uids;
	std::lock_guard<std::mutex> lock(fMutex);
	fQueue.push_back(command);
	fCondition.notify_all();
}


void
MailSession::Search(const BString& folder, const BString& criteria)
{
	Command command = {};
	command.what = kCmdSearch;
	command.folder = folder;
	command.criteria = criteria;
	std::lock_guard<std::mutex> lock(fMutex);
	fQueue.push_back(command);
	fCondition.notify_all();
}


void
MailSession::SendMessage(const MailCompose& compose)
{
	Command command = {};
	command.what = kCmdSend;
	command.compose = CloneCompose(compose);
	std::lock_guard<std::mutex> lock(fMutex);
	fQueue.push_back(command);
	fCondition.notify_all();
}


void
MailSession::SaveDraft(const MailCompose& compose)
{
	Command command = {};
	command.what = kCmdDraft;
	command.compose = CloneCompose(compose);
	command.draft = true;
	std::lock_guard<std::mutex> lock(fMutex);
	fQueue.push_back(command);
	fCondition.notify_all();
}


void
MailSession::_PostEvent(uint32 what, BMessage* extra)
{
	BMessage message;
	if (extra != NULL)
		message = *extra;
	message.what = MSG_NATIVE_EVENT;
	message.AddString(WEBMAIL_ACCOUNT_ID_FIELD, fAccount.id.String());
	message.AddInt32("webmail:what", (int32)what);
	fTarget.SendMessage(&message);
}


bool
MailSession::_Connect(BString& error)
{
	// A new connection has no folder selected.
	fSelectedFolder = "";
	if (fAccount.kind == IMAP_ACCOUNT) {
		ImapClient::Settings settings;
		settings.host = fAccount.imapHost;
		settings.port = fAccount.imapPort;
		settings.tls = fAccount.imapTLS;
		settings.user = fAccount.address;
		if (fAccessToken.Length() > 0
			&& fAccount.authMethod != "password") {
			settings.oauthToken = fAccessToken;
		} else {
			AccountsService* accounts = DefaultAccountsService();
			BString secretName = fAccount.imapHost.Length() > 0
				? "imap-password" : "password";
			status_t secret = accounts->GetSecret(fAccount.id.String(),
				secretName.String(), settings.password);
			if (settings.password.Length() == 0)
				secret = accounts->GetSecret(fAccount.id.String(),
					"password", settings.password);
			if (settings.password.Length() == 0) {
				error = secret == B_ENTRY_NOT_FOUND
					? "No password stored for this account"
					: "The key store refused to release the password";
				return false;
			}
		}
		return fImap.Connect(settings, error);
	}

	Pop3Client::Settings settings;
	settings.host = fAccount.pop3Host;
	settings.port = fAccount.pop3Port;
	settings.tls = fAccount.pop3TLS;
	settings.user = fAccount.address;
	AccountsService* accounts = DefaultAccountsService();
	status_t secret = accounts->GetSecret(fAccount.id.String(), "password",
		settings.password);
	if (settings.password.Length() == 0) {
		error = secret == B_ENTRY_NOT_FOUND
			? "No password stored for this account"
			: "The key store refused to release the password";
		return false;
	}
	return fPop3.Connect(settings, error);
}


void
MailSession::_Disconnect()
{
	fImap.Disconnect();
	fPop3.Disconnect();
	fConnected = false;
	fSelectedFolder = "";
}


void
MailSession::_SyncFolders()
{
	BString error;
	if (!fConnected) {
		if (!_Connect(error)) {
			BMessage extra;
			extra.AddString(WEBMAIL_ERROR_FIELD, error.String());
			_PostEvent(kNativeEventError, &extra);
			return;
		}
		fConnected = true;
	}

	BMessage extra;
	if (fAccount.kind == IMAP_ACCOUNT) {
		std::vector<MailFolder> folders;
		if (!fImap.ListFolders(folders, error)) {
			extra.AddString(WEBMAIL_ERROR_FIELD, error.String());
			_PostEvent(kNativeEventError, &extra);
			return;
		}
		extra.AddInt32("webmail:count", folders.size());
		for (int32 i = 0; i < (int32)folders.size(); i++) {
			extra.AddString("webmail:name", folders[i].name.String());
			extra.AddString("webmail:display",
				folders[i].displayName.String());
		}
		_PostEvent(kNativeEventFolders, &extra);
		return;
	}

	// POP3 has one mailbox; report it as INBOX and count the cache.
	extra.AddInt32("webmail:count", 1);
	extra.AddString("webmail:name", "INBOX");
	extra.AddString("webmail:display", "Inbox");
	_PostEvent(kNativeEventFolders, &extra);
}


void
MailSession::_SyncHeaders(const BString& folder, uint32 offset, uint32 count)
{
	BString error;
	if (fAccount.kind != IMAP_ACCOUNT)
		return;
	if (!fConnected) {
		if (!_Connect(error)) {
			BMessage extra;
			extra.AddString(WEBMAIL_ERROR_FIELD, error.String());
			_PostEvent(kNativeEventError, &extra);
			return;
		}
		fConnected = true;
	}
	if (fSelectedFolder != folder) {
		uint32 exists = 0;
		if (!fImap.Select(folder.String(), exists, error)) {
			BMessage extra;
			extra.AddString(WEBMAIL_ERROR_FIELD, error.String());
			_PostEvent(kNativeEventError, &extra);
			return;
		}
		fSelectedFolder = folder;
	}

	std::vector<MailMessageHeader> headers;
	// The pager asks for `count` messages starting at `offset`; the
	// UID walk uses the folder UIDNEXT-free path: fetch the last N by
	// asking from UID 1 and skipping (kept simple and correct for
	// the first pages; a UID map lands with the cache work).
	uint32 firstUid = offset > 0 ? offset : 1;
	if (!fImap.FetchHeaders(firstUid, count, headers, error)) {
		BMessage extra;
		extra.AddString(WEBMAIL_ERROR_FIELD, error.String());
		_PostEvent(kNativeEventError, &extra);
		return;
	}

	BMessage extra;
	extra.AddString("webmail:folder", folder.String());
	extra.AddInt32("webmail:count", headers.size());
	for (int32 i = 0; i < (int32)headers.size(); i++) {
		const MailMessageHeader& header = headers[i];
		extra.AddInt32("webmail:uid", (int32)header.uid);
		extra.AddString("webmail:subject", header.subject.String());
		extra.AddString("webmail:from_name", header.from.name.String());
		extra.AddString("webmail:from_addr", header.from.address.String());
		extra.AddInt64("webmail:date", (int64)header.date);
		extra.AddBool("webmail:seen", header.seen);
		extra.AddBool("webmail:flagged", header.flagged);
		extra.AddBool("webmail:answered", header.answered);
	}
	_PostEvent(kNativeEventHeaders, &extra);
}


void
MailSession::_FetchMessage(const BString& folder, uint32 uid)
{
	BString error;
	if (fAccount.kind != IMAP_ACCOUNT)
		return;
	if (!fConnected) {
		if (!_Connect(error)) {
			BMessage extra;
			extra.AddString(WEBMAIL_ERROR_FIELD, error.String());
			_PostEvent(kNativeEventError, &extra);
			return;
		}
		fConnected = true;
	}
	if (fSelectedFolder != folder) {
		uint32 exists = 0;
		if (!fImap.Select(folder.String(), exists, error)) {
			BMessage extra;
			extra.AddString(WEBMAIL_ERROR_FIELD, error.String());
			_PostEvent(kNativeEventError, &extra);
			return;
		}
		fSelectedFolder = folder;
	}

	MailMessage message;
	if (!fImap.FetchMessage(uid, message, error)) {
		BMessage extra;
		extra.AddString(WEBMAIL_ERROR_FIELD, error.String());
		_PostEvent(kNativeEventError, &extra);
		return;
	}

	BMessage extra;
	extra.AddInt32("webmail:uid", (int32)uid);
	extra.AddString("webmail:folder", folder.String());
	extra.AddString("webmail:subject", message.header.subject.String());
	extra.AddString("webmail:from_name", message.header.from.name.String());
	extra.AddString("webmail:from_addr",
		message.header.from.address.String());
	extra.AddString("webmail:text", message.textBody.String());
	extra.AddString("webmail:html", message.htmlBody.String());
	extra.AddInt64("webmail:date", (int64)message.header.date);
	for (int32 i = 0; i < (int32)message.attachments.size(); i++) {
		extra.AddString("webmail:attachment",
			message.attachments[i].fileName.String());
	}
	_PostEvent(kNativeEventMessage, &extra);

	// Reading marks the message seen.
	std::vector<uint32> uids;
	uids.push_back(uid);
	fImap.SetSeen(uids, true, error);
}


void
MailSession::_SendOrDraft(const MailCompose& compose, bool draft)
{
	BString error;
	BString rfc822;
	if (draft) {
		rfc822 = BuildMimeMessageFromPaths(compose, error);
		if (error.Length() > 0) {
			BMessage extra;
			extra.AddString(WEBMAIL_ERROR_FIELD, error.String());
			_PostEvent(kNativeEventError, &extra);
			return;
		}
	} else {
		rfc822 = BuildMimeMessageFromPaths(compose, error);
		if (error.Length() > 0) {
			BMessage extra;
			extra.AddString(WEBMAIL_ERROR_FIELD, error.String());
			_PostEvent(kNativeEventError, &extra);
			return;
		}
	}

	if (fAccount.kind == IMAP_ACCOUNT) {
		if (!fConnected) {
			if (!_Connect(error)) {
				BMessage extra;
				extra.AddString(WEBMAIL_ERROR_FIELD, error.String());
				_PostEvent(kNativeEventError, &extra);
				return;
			}
			fConnected = true;
		}
		if (draft) {
			if (!fImap.AppendMessage("Drafts", rfc822, false, error)) {
				BMessage extra;
				extra.AddString(WEBMAIL_ERROR_FIELD, error.String());
				_PostEvent(kNativeEventError, &extra);
				return;
			}
		} else {
			SmtpClient::Settings settings;
			settings.host = fAccount.smtpHost;
			settings.port = fAccount.smtpPort;
			settings.tls = fAccount.smtpTLS;
			settings.user = fAccount.address;
			if (fAccessToken.Length() > 0
				&& fAccount.authMethod != "password") {
				settings.oauthToken = fAccessToken;
			} else {
				AccountsService* accounts = DefaultAccountsService();
				status_t secret = accounts->GetSecret(
					fAccount.id.String(), "smtp-password",
					settings.password);
				if (settings.password.Length() == 0)
					secret = accounts->GetSecret(fAccount.id.String(),
						"password", settings.password);
				if (settings.password.Length() == 0) {
					BMessage extra;
					extra.AddString(WEBMAIL_ERROR_FIELD,
						secret == B_ENTRY_NOT_FOUND
							? "No password stored for this account"
							: "The key store refused to release the "
								"password");
					_PostEvent(kNativeEventError, &extra);
					return;
				}
			}

			SmtpClient smtp;
			if (!smtp.Connect(settings, error)) {
				BMessage extra;
				extra.AddString(WEBMAIL_ERROR_FIELD, error.String());
				_PostEvent(kNativeEventError, &extra);
				return;
			}
			MailAddress from;
			from.address = fAccount.address;
			from.name = fAccount.identityName;
			std::vector<MailAddress> recipients;
			for (int32 i = 0; i < (int32)compose.to.size(); i++)
				recipients.push_back(compose.to[i]);
			for (int32 i = 0; i < (int32)compose.cc.size(); i++)
				recipients.push_back(compose.cc[i]);
			for (int32 i = 0; i < (int32)compose.bcc.size(); i++)
				recipients.push_back(compose.bcc[i]);
			if (!smtp.Send(from, recipients, rfc822, error)) {
				BMessage extra;
				extra.AddString(WEBMAIL_ERROR_FIELD, error.String());
				_PostEvent(kNativeEventError, &extra);
				return;
			}
			fImap.AppendMessage("Sent", rfc822, true, error);
		}
		_PostEvent(kNativeEventSent, NULL);
		return;
	}

	// POP3 accounts send through SMTP only; they have no Drafts or
	// Sent mailbox on the server.
	if (draft) {
		BMessage extra;
		extra.AddString(WEBMAIL_ERROR_FIELD,
			"POP3 accounts keep drafts locally only");
		_PostEvent(kNativeEventError, &extra);
		return;
	}
	SmtpClient::Settings settings;
	settings.host = fAccount.smtpHost;
	settings.port = fAccount.smtpPort;
	settings.tls = fAccount.smtpTLS;
	settings.user = fAccount.address;
	AccountsService* accounts = DefaultAccountsService();
	status_t secret = accounts->GetSecret(fAccount.id.String(),
		"smtp-password", settings.password);
	if (settings.password.Length() == 0)
		secret = accounts->GetSecret(fAccount.id.String(), "password",
			settings.password);
	if (settings.password.Length() == 0) {
		BMessage extra;
		extra.AddString(WEBMAIL_ERROR_FIELD, secret == B_ENTRY_NOT_FOUND
			? "No password stored for this account"
			: "The key store refused to release the password");
		_PostEvent(kNativeEventError, &extra);
		return;
	}
	SmtpClient smtp;
	if (!smtp.Connect(settings, error)) {
		BMessage extra;
		extra.AddString(WEBMAIL_ERROR_FIELD, error.String());
		_PostEvent(kNativeEventError, &extra);
		return;
	}
	MailAddress from;
	from.address = fAccount.address;
	from.name = fAccount.identityName;
	std::vector<MailAddress> recipients;
	for (int32 i = 0; i < (int32)compose.to.size(); i++)
		recipients.push_back(compose.to[i]);
	for (int32 i = 0; i < (int32)compose.cc.size(); i++)
		recipients.push_back(compose.cc[i]);
	for (int32 i = 0; i < (int32)compose.bcc.size(); i++)
		recipients.push_back(compose.bcc[i]);
	if (!smtp.Send(from, recipients, rfc822, error)) {
		BMessage extra;
		extra.AddString(WEBMAIL_ERROR_FIELD, error.String());
		_PostEvent(kNativeEventError, &extra);
		return;
	}
	_PostEvent(kNativeEventSent, NULL);
}


void
MailSession::_ProcessQueue()
{
	std::vector<Command> batch;
	{
		std::lock_guard<std::mutex> lock(fMutex);
		batch.swap(fQueue);
	}

	for (const Command& command : batch) {
		switch (command.what) {
			case kCmdSync:
			case kCmdFolders:
				_SyncFolders();
				if (command.what == kCmdSync) {
					uint32 exists = 0;
					BString error;
					if (fConnected && fAccount.kind == IMAP_ACCOUNT
						&& fImap.Select("INBOX", exists, error)) {
						fSelectedFolder = "INBOX";
						std::vector<MailMessageHeader> headers;
						if (fImap.FetchHeaders(1, 25, headers, error)
							&& headers.size() > 0) {
							// The sidebar learns the inbox count from
							// the headers event; nothing else to do.
						}
					}
				}
				break;
			case kCmdHeaders:
				_SyncHeaders(command.folder, command.offset,
					command.count > 0 ? command.count : 50);
				break;
			case kCmdMessage:
				_FetchMessage(command.folder, command.uid);
				break;
			case kCmdFlags:
			{
				BString error;
				if (fConnected)
					fImap.SetSeen(command.uids, command.flag, error);
				break;
			}
			case kCmdMove:
			{
				BString error;
				if (fConnected)
					fImap.MoveMessages(command.destination.String(),
						command.uids, error);
				break;
			}
			case kCmdDelete:
			{
				BString error;
				if (fConnected)
					fImap.DeleteMessages(command.uids, error);
				break;
			}
			case kCmdSearch:
			{
				BString error;
				if (!fConnected && !_Connect(error))
					break;
				if (fSelectedFolder != command.folder) {
					uint32 exists = 0;
					fImap.Select(command.folder.String(), exists, error);
					fSelectedFolder = command.folder;
				}
				std::vector<uint32> uids;
				if (fImap.Search(command.criteria.String(), uids, error)) {
					BMessage extra;
					extra.AddString("webmail:folder",
						command.folder.String());
					extra.AddInt32("webmail:count", uids.size());
					for (int32 i = 0; i < (int32)uids.size(); i++)
						extra.AddInt32("webmail:uid", (int32)uids[i]);
					_PostEvent(kNativeEventSearch, &extra);
				}
				break;
			}
			case kCmdSend:
				_SendOrDraft(command.compose, false);
				break;
			case kCmdDraft:
				_SendOrDraft(command.compose, true);
				break;
			default:
				break;
		}
	}
}


void
MailSession::_ThreadFunc()
{
	BString error;
	bool idling = false;
	if (_Connect(error)) {
		fConnected = true;
		if (fAccount.kind == IMAP_ACCOUNT && fAccount.useIDLE) {
			uint32 exists = 0;
			if (fImap.Select("INBOX", exists, error)) {
				fSelectedFolder = "INBOX";
				idling = fImap.IdleStart(error);
			}
		}
	} else {
		BMessage extra;
		extra.AddString(WEBMAIL_ERROR_FIELD, error.String());
		_PostEvent(kNativeEventError, &extra);
	}

	time_t lastPoll = time(NULL);
	while (true) {
		{
			std::unique_lock<std::mutex> lock(fMutex);
			if (fQueue.empty() && !fQuit) {
				// Slice the wait so queued work lands promptly even
				// while the connection idles.
				fCondition.wait_for(lock, std::chrono::seconds(2));
			}
			if (fQuit)
				break;
		}

		bool pending;
		{
			std::lock_guard<std::mutex> lock(fMutex);
			pending = !fQueue.empty();
		}
		if (pending) {
			// The server rejects commands while the connection idles.
			if (idling) {
				BString idleError;
				fImap.IdleDone(idleError);
				idling = false;
			}
			_ProcessQueue();
			if (fConnected && fAccount.kind == IMAP_ACCOUNT
				&& fAccount.useIDLE && fImap.IsConnected()
				&& fSelectedFolder.Length() > 0) {
				BString idleError;
				idling = fImap.IdleStart(idleError);
			}
		}

		if (idling && fImap.IsConnected()) {
			bool traffic = false;
			if (!fImap.IdleWait(5, traffic)) {
				// Connection died: reconnect on the next cycle.
				_Disconnect();
				idling = false;
				BString reconnectError;
				if (_Connect(reconnectError)) {
					fConnected = true;
					uint32 exists = 0;
					if (fImap.Select("INBOX", exists, reconnectError)) {
						fSelectedFolder = "INBOX";
						idling = fImap.IdleStart(reconnectError);
					}
				}
			} else if (traffic) {
				BString idleError;
				fImap.IdleDone(idleError);
				BMessage extra;
				_PostEvent(kNativeEventNewMail, &extra);
				// Re-enter IDLE after the UI pulls what it needs.
				idling = fImap.IdleStart(idleError);
				lastPoll = time(NULL);
			}
		} else if (fConnected && fAccount.kind == IMAP_ACCOUNT
			&& !fAccount.useIDLE && time(NULL) - lastPoll > 300) {
			// No IDLE: refresh the inbox view every five minutes and
			// let the UI decide what changed.
			lastPoll = time(NULL);
			BMessage extra;
			_PostEvent(kNativeEventNewMail, &extra);
		}
	}

	_Disconnect();
}
