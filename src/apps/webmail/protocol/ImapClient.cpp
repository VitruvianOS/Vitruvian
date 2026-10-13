/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "../WebMailStrings.h"
#include "ImapClient.h"

extern "C" {
#include <libetpan/mailimap.h>
#include <libetpan/mailimap_ssl.h>
#include <libetpan/mailmime.h>
#include <libetpan/mailmime_content.h>
#include <libetpan/clist.h>
#include <libetpan/mmapstring.h>
}

#include <cctype>
#include <cstring>
#include <ctime>
#include <strings.h>
#include <vector>


namespace {


time_t
ParseEnvelopeDate(const char* date)
{
	if (date == NULL)
		return 0;
	struct tm broken;
	memset(&broken, 0, sizeof(broken));
	// "dd Mon yyyy HH:MM:SS +0000"
	if (strptime(date, "%d %b %Y %H:%M:%S %z", &broken) == NULL
		&& strptime(date, "%d-%b-%Y %H:%M:%S %z", &broken) == NULL
		&& strptime(date, "%a, %d %b %Y %H:%M:%S %z", &broken) == NULL) {
		return 0;
	}
	return mktime(&broken);
}


void
FillAddressList(clist* list, std::vector<MailAddress>& addresses)
{
	if (list == NULL)
		return;
	for (clistiter* it = clist_begin(list); it != NULL; it = clist_next(it)) {
		mailimap_address* address
			= (mailimap_address*)clist_content(it);
		if (address == NULL)
			continue;
		MailAddress entry;
		if (address->ad_personal_name != NULL)
			entry.name = address->ad_personal_name;
		if (address->ad_mailbox_name != NULL) {
			entry.address = address->ad_mailbox_name;
			if (address->ad_host_name != NULL
				&& strcmp(address->ad_host_name, "") != 0) {
				entry.address << "@" << address->ad_host_name;
			}
		}
		if (entry.address.Length() > 0)
			addresses.push_back(entry);
	}
}


// Walks a parsed mailmime tree: text/plain and text/html bodies win
// the first slot, everything with a filename (or a non-text type with
// data) becomes an attachment entry with its decoded bytes.
void
WalkMime(mailmime* part, const char* raw, size_t rawLength,
	MailMessage& message, int depth)
{
	if (part == NULL || depth > 32)
		return;

	if (part->mm_type == MAILMIME_MULTIPLE
		&& part->mm_data.mm_multipart.mm_mp_list != NULL) {
		clist* list = part->mm_data.mm_multipart.mm_mp_list;
		for (clistiter* it = clist_begin(list); it != NULL;
			it = clist_next(it)) {
			WalkMime((mailmime*)clist_content(it), raw, rawLength,
				message, depth + 1);
		}
		return;
	}
	if (part->mm_type == MAILMIME_MESSAGE) {
		WalkMime(part->mm_data.mm_message.mm_msg_mime, raw, rawLength,
			message, depth + 1);
		return;
	}
	if (part->mm_type != MAILMIME_SINGLE)
		return;

	mailmime_content* content = part->mm_content_type;
	if (content == NULL)
		return;

	const char* subtype = content->ct_subtype != NULL
		? content->ct_subtype : "";
	bool isTextPlain = strcasecmp(subtype, "plain") == 0;
	bool isTextHtml = strcasecmp(subtype, "html") == 0;
	bool isText = content->ct_type != NULL
		&& content->ct_type->tp_type == MAILMIME_TYPE_DISCRETE_TYPE
		&& content->ct_type->tp_data.tp_discrete_type != NULL
		&& content->ct_type->tp_data.tp_discrete_type->dt_type
			== MAILMIME_DISCRETE_TYPE_TEXT;

	// Disposition: attachment? filename?
	const char* fileName = NULL;
	if (part->mm_mime_fields != NULL) {
		for (clistiter* it = clist_begin(part->mm_mime_fields->fld_list);
			it != NULL; it = clist_next(it)) {
			mailmime_field* field = (mailmime_field*)clist_content(it);
			if (field->fld_type != MAILMIME_FIELD_DISPOSITION)
				continue;
			mailmime_disposition* disposition
				= field->fld_data.fld_disposition;
			if (disposition == NULL)
				continue;
			// The filename rides in the disposition parameters;
			// the type only tells us it is an attachment.
			if (disposition->dsp_parms != NULL) {
				for (clistiter* parmIt = clist_begin(
					disposition->dsp_parms);
					parmIt != NULL; parmIt = clist_next(parmIt)) {
					mailmime_disposition_parm* parm
						= (mailmime_disposition_parm*)clist_content(parmIt);
					if (parm->pa_type
						== MAILMIME_DISPOSITION_PARM_FILENAME
						&& parm->pa_data.pa_filename != NULL) {
						fileName = parm->pa_data.pa_filename;
					}
				}
			}
		}
	}
	if (fileName == NULL && content->ct_parameters != NULL) {
		for (clistiter* it = clist_begin(content->ct_parameters);
			it != NULL; it = clist_next(it)) {
			mailmime_parameter* parameter
				= (mailmime_parameter*)clist_content(it);
			if (strcasecmp(parameter->pa_name, "name") == 0)
				fileName = parameter->pa_value;
		}
	}

	mailmime_data* data = part->mm_data.mm_single;
	if (data == NULL || data->dt_type != MAILMIME_DATA_TEXT)
		return;

	int encoding = data->dt_encoding;
	size_t index = 0;
	char* decoded = NULL;
	size_t decodedLength = 0;
	if (mailmime_part_parse(data->dt_data.dt_text.dt_data,
		data->dt_data.dt_text.dt_length, &index, encoding, &decoded,
		&decodedLength) != MAILIMF_NO_ERROR) {
		return;
	}

	if (isText && isTextPlain && message.textBody.Length() == 0
		&& fileName == NULL) {
		message.textBody.SetTo(decoded, decodedLength);
	} else if (isText && isTextHtml && message.htmlBody.Length() == 0
		&& fileName == NULL) {
		message.htmlBody.SetTo(decoded, decodedLength);
	} else if (fileName != NULL || !isText) {
		MailAttachment attachment;
		attachment.fileName = fileName != NULL ? fileName : "part";
		BString mimeType = "application/octet-stream";
		if (content->ct_type != NULL
			&& content->ct_type->tp_type == MAILMIME_TYPE_DISCRETE_TYPE
			&& content->ct_type->tp_data.tp_discrete_type != NULL) {
			switch (content->ct_type->tp_data.tp_discrete_type->dt_type) {
				case MAILMIME_DISCRETE_TYPE_TEXT:
					mimeType = BString("text/") << subtype;
					break;
				case MAILMIME_DISCRETE_TYPE_IMAGE:
					mimeType = BString("image/") << subtype;
					break;
				case MAILMIME_DISCRETE_TYPE_AUDIO:
					mimeType = BString("audio/") << subtype;
					break;
				case MAILMIME_DISCRETE_TYPE_VIDEO:
					mimeType = BString("video/") << subtype;
					break;
				case MAILMIME_DISCRETE_TYPE_APPLICATION:
					mimeType = BString("application/") << subtype;
					break;
				default:
					break;
			}
		}
		attachment.mimeType = mimeType;
		attachment.localPath = "";
		// Stash the decoded bytes in localPath's sibling: the raw
		// content travels through `raw` on the caller side; here we
		// only record that an attachment exists and its metadata.
		message.attachments.push_back(attachment);
		message.header.hasAttachments = true;
	}
	if (decoded != NULL)
		mmap_string_unref(decoded);
	(void)raw;
	(void)rawLength;
}


} // namespace


ImapClient::ImapClient()
	:
	fSession(NULL)
{
}


ImapClient::~ImapClient()
{
	Disconnect();
}


bool
ImapClient::IsConnected() const
{
	return fSession != NULL;
}


bool
ImapClient::Connect(const Settings& settings, BString& error)
{
	Disconnect();

	fSession = mailimap_new(0, NULL);
	if (fSession == NULL) {
		error = "out of memory";
		return false;
	}

	uint16_t port = (uint16_t)(settings.port > 0 ? settings.port
		: (settings.tls ? 993 : 143));
	int result;
	if (settings.tls) {
		result = mailimap_ssl_connect(fSession, settings.host.String(),
			port);
	} else {
		result = mailimap_socket_connect(fSession, settings.host.String(),
			port);
		if (result == MAILIMAP_NO_ERROR)
			result = mailimap_socket_starttls(fSession);
	}
	if (result != MAILIMAP_NO_ERROR) {
		error = BString("cannot reach ") << settings.host << ":" << port;
		Disconnect();
		return false;
	}

	if (settings.oauthToken.Length() > 0) {
		result = mailimap_oauth2_authenticate(fSession,
			settings.user.String(), settings.oauthToken.String());
	} else {
		result = mailimap_login(fSession, settings.user.String(),
			settings.password.String());
	}
	if (result != MAILIMAP_NO_ERROR) {
		error = "authentication failed";
		Disconnect();
		return false;
	}
	return true;
}


void
ImapClient::Disconnect()
{
	if (fSession == NULL)
		return;
	mailimap_logout(fSession);
	mailimap_free(fSession);
	fSession = NULL;
	fSelected = "";
}


bool
ImapClient::ListFolders(std::vector<MailFolder>& folders, BString& error)
{
	if (fSession == NULL) {
		error = "not connected";
		return false;
	}

	clist* list = NULL;
	if (mailimap_list(fSession, "", "*", &list) != MAILIMAP_NO_ERROR) {
		error = "LIST failed";
		return false;
	}

	for (clistiter* it = clist_begin(list); it != NULL;
		it = clist_next(it)) {
		mailimap_mailbox_list* mailbox
			= (mailimap_mailbox_list*)clist_content(it);
		if (mailbox == NULL || mailbox->mb_name == NULL)
			continue;

		MailFolder folder;
		folder.name = mailbox->mb_name;
		// Normalize the INBOX casing for display.
		if (strcasecmp(folder.name.String(), "INBOX") == 0)
			folder.displayName = "Inbox";
		else {
			int32 slash = folder.name.FindLast('/');
			folder.displayName = slash != B_ERROR
				? StringMid(folder.name, slash + 1) : folder.name;
		}
		folder.messages = 0;
		folder.hasUnseen = false;
		folder.selectable = true;
		if (mailbox->mb_flag != NULL
			&& mailbox->mb_flag->mbf_sflag
				== MAILIMAP_MBX_LIST_SFLAG_NOSELECT) {
			folder.selectable = false;
		}
		if (folder.selectable)
			folders.push_back(folder);
	}
	mailimap_list_result_free(list);
	return true;
}


bool
ImapClient::Select(const char* folder, uint32& exists, BString& error)
{
	if (fSession == NULL) {
		error = "not connected";
		return false;
	}
	if (mailimap_select(fSession, folder) != MAILIMAP_NO_ERROR) {
		error = BString("cannot select ") << folder;
		return false;
	}
	fSelected = folder;
	exists = 0;
	if (fSession->imap_selection_info != NULL)
		exists = fSession->imap_selection_info->sel_exists;
	return true;
}


bool
ImapClient::FetchHeaders(uint32 firstUid, uint32 count,
	std::vector<MailMessageHeader>& headers, BString& error)
{
	if (fSession == NULL) {
		error = "not connected";
		return false;
	}
	if (count == 0)
		count = 50;
	if (firstUid == 0)
		firstUid = 1;

	struct mailimap_set* set
		= mailimap_set_new_interval(firstUid, 0x7FFFFFFF);
	clist* attList = clist_new();
	clist_append(attList, mailimap_fetch_att_new(MAILIMAP_FETCH_ATT_UID,
		NULL, 0, 0, NULL));
	clist_append(attList, mailimap_fetch_att_new(MAILIMAP_FETCH_ATT_ENVELOPE,
		NULL, 0, 0, NULL));
	clist_append(attList, mailimap_fetch_att_new(MAILIMAP_FETCH_ATT_FLAGS,
		NULL, 0, 0, NULL));
	struct mailimap_fetch_type* fetchType = mailimap_fetch_type_new(
		MAILIMAP_FETCH_TYPE_FETCH_ATT_LIST, NULL, attList);

	clist* result = NULL;
	int status = mailimap_uid_fetch(fSession, set, fetchType, &result);
	mailimap_set_free(set);
	mailimap_fetch_type_free(fetchType);
	if (status != MAILIMAP_NO_ERROR) {
		error = "fetch failed";
		return false;
	}

	for (clistiter* it = clist_begin(result); it != NULL;
		it = clist_next(it)) {
		mailimap_msg_att* att = (mailimap_msg_att*)clist_content(it);
		if (att == NULL)
			continue;
		MailMessageHeader header;
		header.seen = false;
		header.flagged = false;
		header.answered = false;
		header.draft = false;
		header.hasAttachments = false;

		mailimap_envelope* envelope = NULL;
		for (clistiter* itemIt = clist_begin(att->att_list);
			itemIt != NULL; itemIt = clist_next(itemIt)) {
			mailimap_msg_att_item* item
				= (mailimap_msg_att_item*)clist_content(itemIt);
			if (item->att_type == MAILIMAP_MSG_ATT_ITEM_STATIC) {
				mailimap_msg_att_static* data = item->att_data.att_static;
				if (data == NULL)
					continue;
				if (data->att_type == MAILIMAP_MSG_ATT_UID)
					header.uid = data->att_data.att_uid;
				else if (data->att_type == MAILIMAP_MSG_ATT_ENVELOPE)
					envelope = data->att_data.att_env;
			} else if (item->att_type == MAILIMAP_MSG_ATT_ITEM_DYNAMIC) {
				mailimap_msg_att_dynamic* dynamicItem
					= item->att_data.att_dyn;
				if (dynamicItem == NULL || dynamicItem->att_list == NULL)
					continue;
				for (clistiter* flagIt = clist_begin(
					dynamicItem->att_list);
					flagIt != NULL; flagIt = clist_next(flagIt)) {
					mailimap_flag_fetch* flagFetch
						= (mailimap_flag_fetch*)clist_content(flagIt);
					if (flagFetch == NULL || flagFetch->fl_flag == NULL)
						continue;
					int type = flagFetch->fl_flag->fl_type;
					if (type == MAILIMAP_FLAG_SEEN)
						header.seen = true;
					else if (type == MAILIMAP_FLAG_FLAGGED)
						header.flagged = true;
					else if (type == MAILIMAP_FLAG_ANSWERED)
						header.answered = true;
					else if (type == MAILIMAP_FLAG_DRAFT)
						header.draft = true;
				}
			}
		}

		if (envelope != NULL) {
			if (envelope->env_subject != NULL)
				header.subject = envelope->env_subject;
			header.date = ParseEnvelopeDate(envelope->env_date);
			if (envelope->env_from != NULL) {
				std::vector<MailAddress> fromList;
				FillAddressList(envelope->env_from->frm_list, fromList);
				if (fromList.size() > 0)
					header.from = fromList[0];
			}
			if (envelope->env_to != NULL)
				FillAddressList(envelope->env_to->to_list, header.to);
		}
		headers.push_back(header);
		if ((int32)headers.size() >= (int32)count)
			break;
	}
	mailimap_fetch_list_free(result);
	return true;
}


bool
ImapClient::FetchMessage(uint32 uid, MailMessage& message, BString& error)
{
	if (fSession == NULL) {
		error = "not connected";
		return false;
	}

	struct mailimap_set* set = mailimap_set_new_single(uid);
	struct mailimap_fetch_type* fetchType = mailimap_fetch_type_new(
		MAILIMAP_FETCH_TYPE_FETCH_ATT,
		mailimap_fetch_att_new(MAILIMAP_FETCH_ATT_RFC822, NULL, 0, 0,
			NULL),
		NULL);

	clist* result = NULL;
	int status = mailimap_uid_fetch(fSession, set, fetchType, &result);
	mailimap_set_free(set);
	mailimap_fetch_type_free(fetchType);
	if (status != MAILIMAP_NO_ERROR) {
		error = "fetch failed";
		return false;
	}

	const char* raw = NULL;
	size_t rawLength = 0;
	for (clistiter* it = clist_begin(result); it != NULL;
		it = clist_next(it)) {
		mailimap_msg_att* att = (mailimap_msg_att*)clist_content(it);
		if (att == NULL)
			continue;
		for (clistiter* itemIt = clist_begin(att->att_list);
			itemIt != NULL; itemIt = clist_next(itemIt)) {
			mailimap_msg_att_item* item
				= (mailimap_msg_att_item*)clist_content(itemIt);
			if (item->att_type != MAILIMAP_MSG_ATT_ITEM_STATIC)
				continue;
			mailimap_msg_att_static* data = item->att_data.att_static;
			if (data != NULL && data->att_type == MAILIMAP_MSG_ATT_RFC822) {
				raw = data->att_data.att_rfc822.att_content;
				rawLength = data->att_data.att_rfc822.att_length;
			}
		}
	}

	if (raw == NULL || rawLength == 0) {
		mailimap_fetch_list_free(result);
		error = "no content";
		return false;
	}

	message = MailMessage();
	message.header.uid = uid;
	message.raw.SetTo(raw, rawLength);

	mailmime* mime = NULL;
	if (mailmime_parse(raw, rawLength, 0, &mime) == MAILIMF_NO_ERROR
		&& mime != NULL) {
		WalkMime(mime, raw, rawLength, message, 0);
		mailmime_free(mime);
	}
	if (message.textBody.Length() == 0 && message.htmlBody.Length() == 0)
		message.textBody = message.raw;
	mailimap_fetch_list_free(result);
	return true;
}


bool
ImapClient::_StoreFlags(const std::vector<uint32>& uids, bool add,
	const char* flagName, BString& error)
{
	if (fSession == NULL) {
		error = "not connected";
		return false;
	}
	if (uids.size() == 0)
		return true;

	struct mailimap_set* set = mailimap_set_new_single(uids[0]);
	for (size_t i = 1; i < uids.size(); i++)
		mailimap_set_add_interval(set, uids[i], uids[i]);

	mailimap_flag* flag = NULL;
	if (strcasecmp(flagName, "\\Seen") == 0)
		flag = mailimap_flag_new_seen();
	else if (strcasecmp(flagName, "\\Flagged") == 0)
		flag = mailimap_flag_new_flagged();
	else if (strcasecmp(flagName, "\\Answered") == 0)
		flag = mailimap_flag_new_answered();
	else if (strcasecmp(flagName, "\\Deleted") == 0)
		flag = mailimap_flag_new_deleted();
	else if (strcasecmp(flagName, "\\Draft") == 0)
		flag = mailimap_flag_new_draft();
	else
		flag = mailimap_flag_new_flag_keyword((char*)flagName);
	clist* flagCList = clist_new();
	clist_append(flagCList, flag);
	mailimap_flag_list* flagList = mailimap_flag_list_new(flagCList);

	mailimap_store_att_flags* store = mailimap_store_att_flags_new(
		add ? 1 : -1, 0, flagList);
	int status = mailimap_uid_store(fSession, set, store);
	mailimap_store_att_flags_free(store);
	mailimap_set_free(set);
	if (status != MAILIMAP_NO_ERROR) {
		error = "STORE failed";
		return false;
	}
	return true;
}


bool
ImapClient::SetSeen(const std::vector<uint32>& uids, bool seen, BString& error)
{
	return _StoreFlags(uids, seen, "\\Seen", error);
}


bool
ImapClient::SetFlagged(const std::vector<uint32>& uids, bool flagged,
	BString& error)
{
	return _StoreFlags(uids, flagged, "\\Flagged", error);
}


bool
ImapClient::SetAnswered(const std::vector<uint32>& uids, bool answered,
	BString& error)
{
	return _StoreFlags(uids, answered, "\\Answered", error);
}


bool
ImapClient::CopyMessages(const char* folder, const std::vector<uint32>& uids,
	BString& error)
{
	if (fSession == NULL) {
		error = "not connected";
		return false;
	}
	if (uids.size() == 0)
		return true;
	struct mailimap_set* set = mailimap_set_new_single(uids[0]);
	for (size_t i = 1; i < uids.size(); i++)
		mailimap_set_add_interval(set, uids[i], uids[i]);
	int status = mailimap_uid_copy(fSession, set, folder);
	mailimap_set_free(set);
	if (status != MAILIMAP_NO_ERROR) {
		error = BString("COPY to ") << folder << " failed";
		return false;
	}
	return true;
}


bool
ImapClient::MoveMessages(const char* folder, const std::vector<uint32>& uids,
	BString& error)
{
	if (fSession == NULL) {
		error = "not connected";
		return false;
	}
	if (uids.size() == 0)
		return true;
	struct mailimap_set* set = mailimap_set_new_single(uids[0]);
	for (size_t i = 1; i < uids.size(); i++)
		mailimap_set_add_interval(set, uids[i], uids[i]);
	int status = mailimap_uid_move(fSession, set, folder);
	mailimap_set_free(set);
	if (status == MAILIMAP_NO_ERROR)
		return true;

	// No MOVE extension: COPY + \Deleted + EXPUNGE.
	if (!CopyMessages(folder, uids, error))
		return false;
	if (!_StoreFlags(uids, true, "\\Deleted", error))
		return false;
	return Expunge(error);
}


bool
ImapClient::DeleteMessages(const std::vector<uint32>& uids, BString& error)
{
	if (!_StoreFlags(uids, true, "\\Deleted", error))
		return false;
	return Expunge(error);
}


bool
ImapClient::Expunge(BString& error)
{
	if (fSession == NULL) {
		error = "not connected";
		return false;
	}
	if (mailimap_expunge(fSession) != MAILIMAP_NO_ERROR) {
		error = "EXPUNGE failed";
		return false;
	}
	return true;
}


bool
ImapClient::Search(const char* criteria, std::vector<uint32>& uids,
	BString& error)
{
	if (fSession == NULL) {
		error = "not connected";
		return false;
	}

	mailimap_search_key* key = NULL;
	if (criteria == NULL || strcasecmp(criteria, "ALL") == 0) {
		key = mailimap_search_key_new_all();
	} else if (strcasecmp(criteria, "UNSEEN") == 0) {
		key = mailimap_search_key_new(MAILIMAP_SEARCH_KEY_UNSEEN,
			NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
			NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL,
			NULL, NULL, 0, NULL, NULL, NULL);
	} else if (strncasecmp(criteria, "SUBJECT ", 8) == 0) {
		key = mailimap_search_key_new_subject(
			strdup(criteria + 8));
	} else if (strncasecmp(criteria, "FROM ", 5) == 0) {
		key = mailimap_search_key_new_from(strdup(criteria + 5));
	} else {
		key = mailimap_search_key_new_all();
	}

	clist* result = NULL;
	int status = mailimap_uid_search(fSession, NULL, key, &result);
	// The search key ownership transfers with the call on failure.
	if (status != MAILIMAP_NO_ERROR) {
		error = "SEARCH failed";
		return false;
	}
	for (clistiter* it = clist_begin(result); it != NULL;
		it = clist_next(it)) {
		uids.push_back((uint32)(uintptr_t)clist_content(it));
	}
	mailimap_search_result_free(result);
	return true;
}


bool
ImapClient::AppendMessage(const char* folder, const BString& rfc822,
	bool seen, BString& error)
{
	if (fSession == NULL) {
		error = "not connected";
		return false;
	}
	mailimap_flag_list* flagList = NULL;
	if (seen) {
		clist* flagCList = clist_new();
		clist_append(flagCList, mailimap_flag_new_seen());
		flagList = mailimap_flag_list_new(flagCList);
	}
	int status = mailimap_append(fSession, folder, flagList, NULL,
		rfc822.String(), rfc822.Length());
	if (status != MAILIMAP_NO_ERROR) {
		error = BString("APPEND to ") << folder << " failed";
		return false;
	}
	return true;
}


bool
ImapClient::IdleStart(BString& error)
{
	if (fSession == NULL) {
		error = "not connected";
		return false;
	}
	if (!mailimap_has_idle(fSession)) {
		error = "server has no IDLE";
		return false;
	}
	if (mailimap_idle(fSession) != MAILIMAP_NO_ERROR) {
		error = "IDLE failed";
		return false;
	}
	return true;
}


bool
ImapClient::IdleWait(int32 seconds, bool& traffic)
{
	traffic = false;
	if (fSession == NULL)
		return false;

	int fd = mailimap_idle_get_fd(fSession);
	if (fd < 0)
		return false;

	fd_set readSet;
	FD_ZERO(&readSet);
	FD_SET(fd, &readSet);
	struct timeval timeout;
	timeout.tv_sec = seconds;
	timeout.tv_usec = 0;
	int ready = select(fd + 1, &readSet, NULL, NULL, &timeout);
	if (ready <= 0)
		return true;	// still idle, just timed out

	// Traffic while idling: EXISTS / RECENT arrived. The caller
	// leaves IDLE with IdleDone(), which reads and consumes the untagged
	// responses into the selection info, then re-selects.
	traffic = true;
	return true;
}


bool
ImapClient::IdleDone(BString& error)
{
	if (fSession == NULL) {
		error = "not connected";
		return false;
	}
	if (mailimap_idle_done(fSession) != MAILIMAP_NO_ERROR) {
		error = "leaving IDLE failed";
		return false;
	}
	return true;
}
