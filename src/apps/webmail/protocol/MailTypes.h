/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEB_MAIL_TYPES_H
#define WEB_MAIL_TYPES_H


#include <String.h>
#include <vector>


struct MailFolder {
	BString		name;			// mailbox name as the server knows it
	BString		displayName;	// last path component
	int32		messages;
	bool		hasUnseen;
	bool		selectable;
};


struct MailAddress {
	BString		name;
	BString		address;
};


struct MailMessageHeader {
	uint32		uid;
	int32		seq;			// sequence number on the server
	BString		subject;
	MailAddress	from;
	MailAddress	replyTo;
	std::vector<MailAddress> to;
	time_t		date;
	bool		seen;
	bool		flagged;
	bool		answered;
	bool		draft;
	bool		hasAttachments;
};


struct MailAttachment {
	BString		fileName;
	BString		mimeType;
	BString		contentId;
	BString		localPath;		// filled when saved to disk
};


struct MailMessage {
	MailMessageHeader	header;
	BString		textBody;
	BString		htmlBody;
	std::vector<MailAttachment> attachments;
	// Raw RFC822, kept for forwarding and sent copies.
	BString		raw;
};


// A composed message, input to the MIME builder.
struct MailCompose {
	BString		fromAddress;
	BString		fromName;
	std::vector<MailAddress> to;
	std::vector<MailAddress> cc;
	std::vector<MailAddress> bcc;
	BString		replyTo;
	BString		subject;
	BString		bodyText;
	BString		bodyHtml;
	BString		inReplyTo;
	std::vector<BString> references;
	std::vector<BString> attachmentPaths;
	BString		signature;
};


#endif // WEB_MAIL_TYPES_H
