/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEB_MAIL_MIME_BUILDER_H
#define WEB_MAIL_MIME_BUILDER_H


#include "../protocol/MailTypes.h"

#include <String.h>
#include <vector>


struct MailAttachmentData {
	BString		fileName;
	BString		mimeType;
	BString		data;			// raw bytes
};


// Pure pieces (host tested).
BString		Base64Encode(const char* data, int32 length);
BString		EncodeRFC2047(const char* text);
BString		FormatAddress(const MailAddress& address);
BString		FormatAddressList(const std::vector<MailAddress>& addresses);
BString		FormatMessageDate(time_t when);
BString		GenerateMessageId(const char* address);

// Builds the complete RFC822 message: multipart/alternative when both
// bodies exist, multipart/mixed when attachments exist.
BString		BuildMimeMessage(const MailCompose& compose,
				const std::vector<MailAttachmentData>& attachments);

// Reads the attachment files from disk (used before sending).
BString		BuildMimeMessageFromPaths(const MailCompose& compose,
				BString& error);


#endif // WEB_MAIL_MIME_BUILDER_H
