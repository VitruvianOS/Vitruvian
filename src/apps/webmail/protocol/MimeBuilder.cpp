/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "../WebMailStrings.h"
#include "MimeBuilder.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <sys/time.h>
#include <unistd.h>
#include <vector>


static const char kBase64Table[]
	= "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


BString
Base64Encode(const char* data, int32 length)
{
	BString out;
	if (data == NULL || length <= 0)
		return out;

	int32 i = 0;
	while (i + 2 < length) {
		uint32 triple = ((uint8)data[i] << 16) | ((uint8)data[i + 1] << 8)
			| (uint8)data[i + 2];
		out << kBase64Table[(triple >> 18) & 63]
			<< kBase64Table[(triple >> 12) & 63]
			<< kBase64Table[(triple >> 6) & 63]
			<< kBase64Table[triple & 63];
		if ((i / 3) % 19 == 18)
			out << "\r\n";
		i += 3;
	}
	if (i + 1 == length) {
		uint32 triple = (uint8)data[i] << 16;
		out << kBase64Table[(triple >> 18) & 63]
			<< kBase64Table[(triple >> 12) & 63] << "==";
	} else if (i + 2 == length) {
		uint32 triple = ((uint8)data[i] << 16) | ((uint8)data[i + 1] << 8);
		out << kBase64Table[(triple >> 18) & 63]
			<< kBase64Table[(triple >> 12) & 63]
			<< kBase64Table[(triple >> 6) & 63] << "=";
	}
	return out;
}


static bool
IsAscii(const char* text)
{
	if (text == NULL)
		return true;
	for (const unsigned char* c = (const unsigned char*)text;
		*c != '\0'; c++) {
		if (*c >= 0x80)
			return false;
	}
	return true;
}


BString
EncodeRFC2047(const char* text)
{
	BString out;
	if (text == NULL)
		return out;
	// RFC 2047 encoded words may not span whitespace runs; keep the
	// whole subject in one word, UTF-8 base64.
	if (IsAscii(text) && strchr(text, '=') == NULL
		&& strchr(text, '?') == NULL) {
		out = text;
		return out;
	}
	out << "=?UTF-8?B?" << Base64Encode(text, (int32)strlen(text))
		<< "?=";
	// The base64 helper may have wrapped lines: strip the breaks for
	// the encoded word.
	out.ReplaceAll("\r\n", "");
	return out;
}


BString
FormatAddress(const MailAddress& address)
{
	BString out;
	if (address.name.Length() > 0 && address.name != address.address) {
		out << "\"" << address.name << "\" <" << address.address << ">";
	} else {
		out << address.address;
	}
	return out;
}


BString
FormatAddressList(const std::vector<MailAddress>& addresses)
{
	BString out;
	for (size_t i = 0; i < addresses.size(); i++) {
		if (i > 0)
			out << ", ";
		out << FormatAddress(addresses[i]);
	}
	return out;
}


BString
FormatMessageDate(time_t when)
{
	struct tm broken;
	char buffer[64];
	localtime_r(&when, &broken);
	strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S %z", &broken);
	return BString(buffer);
}


BString
GenerateMessageId(const char* address)
{
	struct timeval now;
	gettimeofday(&now, NULL);
	char buffer[256];
	snprintf(buffer, sizeof(buffer), "<%ld.%06ld.%d@%s>",
		(long)now.tv_sec, (long)now.tv_usec, (int)getpid(),
		address != NULL && address[0] != '\0' ? address : "webmail");
	return BString(buffer);
}


static BString
WrapBase64Body(const BString& encoded)
{
	// Base64Encode already wraps every 76 characters; nothing else
	// to do, kept as a seam for future QP support.
	return encoded;
}


static BString
BuildAlternativePart(const MailCompose& compose)
{
	BString boundary = "----=_WebMailPart_"
		"B8F4A2C1";
	BString out;
	out << "MIME-Version: 1.0\r\n"
		<< "Content-Type: multipart/alternative; boundary=\""
		<< boundary << "\"\r\n\r\n";

	out << "--" << boundary << "\r\n"
		<< "Content-Type: text/plain; charset=UTF-8\r\n"
		<< "Content-Transfer-Encoding: base64\r\n\r\n";

	BString text = compose.bodyText;
	if (compose.signature.Length() > 0) {
		if (text.Length() > 0)
			text << "\r\n\r\n";
		text << "-- \r\n" << compose.signature;
	}
	out << WrapBase64Body(Base64Encode(text.String(),
		(int32)text.Length())) << "\r\n";

	if (compose.bodyHtml.Length() > 0) {
		out << "--" << boundary << "\r\n"
			<< "Content-Type: text/html; charset=UTF-8\r\n"
			<< "Content-Transfer-Encoding: base64\r\n\r\n";
		out << WrapBase64Body(Base64Encode(compose.bodyHtml.String(),
			(int32)compose.bodyHtml.Length())) << "\r\n";
	}

	out << "--" << boundary << "--\r\n";
	return out;
}


BString
BuildMimeMessage(const MailCompose& compose,
	const std::vector<MailAttachmentData>& attachments)
{
	BString message;
	message << "Message-ID: "
		<< GenerateMessageId(compose.fromAddress.String()) << "\r\n"
		<< "Date: " << FormatMessageDate(time(NULL)) << "\r\n"
		<< "MIME-Version: 1.0\r\n";

	if (compose.fromName.Length() > 0)
		message << "From: \"" << compose.fromName << "\" <"
			<< compose.fromAddress << ">\r\n";
	else
		message << "From: " << compose.fromAddress << "\r\n";
	if (compose.replyTo.Length() > 0)
		message << "Reply-To: " << compose.replyTo << "\r\n";
	message << "To: " << FormatAddressList(compose.to) << "\r\n";
	if (compose.cc.size() > 0)
		message << "Cc: " << FormatAddressList(compose.cc) << "\r\n";
	// Bcc stays out of the transmitted headers per RFC 5322; the SMTP
	// envelope carries those recipients.
	message << "Subject: " << EncodeRFC2047(compose.subject.String())
		<< "\r\n";
	if (compose.inReplyTo.Length() > 0)
		message << "In-Reply-To: " << compose.inReplyTo << "\r\n";
	for (size_t i = 0; i < compose.references.size(); i++)
		message << "References: " << compose.references[i] << "\r\n";
	if (compose.signature.Length() > 0)
		message << "X-Signature: present\r\n";

	if (attachments.size() == 0) {
		if (compose.bodyHtml.Length() == 0) {
			// Plain text only.
			message << "Content-Type: text/plain; charset=UTF-8\r\n"
				<< "Content-Transfer-Encoding: base64\r\n\r\n";
			BString text = compose.bodyText;
			if (compose.signature.Length() > 0) {
				if (text.Length() > 0)
					text << "\r\n\r\n";
				text << "-- \r\n" << compose.signature;
			}
			message << WrapBase64Body(Base64Encode(text.String(),
				(int32)text.Length())) << "\r\n";
			return message;
		}
		message << BuildAlternativePart(compose);
		return message;
	}

	const char* mixedBoundary = "----=_WebMailMixed_5D21";
	message << "Content-Type: multipart/mixed; boundary=\""
		<< mixedBoundary << "\"\r\n\r\n";

	// The body part rides inside the mixed container.
	BString bodyPart;
	if (compose.bodyHtml.Length() > 0) {
		bodyPart = BuildAlternativePart(compose);
		// Strip the duplicated MIME-Version the inner builder adds.
		bodyPart.RemoveFirst("MIME-Version: 1.0\r\n");
	} else {
		bodyPart << "Content-Type: text/plain; charset=UTF-8\r\n"
			<< "Content-Transfer-Encoding: base64\r\n\r\n";
		BString text = compose.bodyText;
		if (compose.signature.Length() > 0) {
			if (text.Length() > 0)
				text << "\r\n\r\n";
			text << "-- \r\n" << compose.signature;
		}
		bodyPart << WrapBase64Body(Base64Encode(text.String(),
			(int32)text.Length())) << "\r\n";
	}
	message << "--" << mixedBoundary << "\r\n" << bodyPart;

	for (size_t i = 0; i < attachments.size(); i++) {
		const MailAttachmentData& attachment = attachments[i];
		message << "--" << mixedBoundary << "\r\n"
			<< "Content-Type: "
			<< (attachment.mimeType.Length() > 0
				? attachment.mimeType.String() : "application/octet-stream")
			<< "; name=\"" << attachment.fileName << "\"\r\n"
			<< "Content-Transfer-Encoding: base64\r\n"
			<< "Content-Disposition: attachment; filename=\""
			<< attachment.fileName << "\"\r\n\r\n";
		message << WrapBase64Body(Base64Encode(attachment.data.String(),
			(int32)attachment.data.Length())) << "\r\n";
	}
	message << "--" << mixedBoundary << "--\r\n";
	return message;
}


BString
BuildMimeMessageFromPaths(const MailCompose& compose, BString& error)
{
	std::vector<MailAttachmentData> attachments;
	for (size_t i = 0; i < compose.attachmentPaths.size(); i++) {
		const BString& path = compose.attachmentPaths[i];
		std::ifstream file(path.String(), std::ios::binary);
		if (!file.is_open()) {
			error = BString("cannot read attachment: ") << path;
			return BString();
		}
		std::ostringstream buffer;
		buffer << file.rdbuf();

		MailAttachmentData data;
		int32 slash = path.FindLast('/');
		data.fileName = slash != B_ERROR ? StringMid(path, slash + 1) : path;
		data.mimeType = "application/octet-stream";
		data.data = buffer.str().c_str();
		attachments.push_back(data);
	}
	error = "";
	return BuildMimeMessage(compose, attachments);
}
