/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "PhotoWriter.h"

#include <new>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Node.h>
#include <TranslationUtils.h>
#include <TranslatorRoster.h>
#include <TranslatorFormats.h>


namespace {


static void
_WriteBE32(BPositionIO* io, uint32 value)
{
	char bytes[4];
	bytes[0] = (char)((value >> 24) & 0xff);
	bytes[1] = (char)((value >> 16) & 0xff);
	bytes[2] = (char)((value >> 8) & 0xff);
	bytes[3] = (char)(value & 0xff);
	io->Write(bytes, 4);
}


static void
_WriteBEFloat(BPositionIO* io, float value)
{
	uint32 bits = 0;
	memcpy(&bits, &value, 4);
	_WriteBE32(io, bits);
}


static status_t
_FlattenBitmap(const BBitmap* bitmap, BMallocIO* out)
{
	const BRect bounds = bitmap->Bounds();
	const int32 width = (int32)bounds.Width() + 1;
	const int32 height = (int32)bounds.Height() + 1;
	const int32 bytesPerRow = bitmap->BytesPerRow();
	if (width <= 0 || height <= 0 || bytesPerRow <= 0)
		return B_BAD_VALUE;

	_WriteBE32(out, B_TRANSLATOR_BITMAP);
	_WriteBEFloat(out, bounds.left);
	_WriteBEFloat(out, bounds.top);
	_WriteBEFloat(out, bounds.right);
	_WriteBEFloat(out, bounds.bottom);
	_WriteBE32(out, (uint32)bytesPerRow);
	_WriteBE32(out, (uint32)bitmap->ColorSpace());
	_WriteBE32(out, (uint32)(bytesPerRow * height));
	out->Write(bitmap->Bits(), (size_t)bytesPerRow * height);
	return B_OK;
}


enum exif_type {
	kExifTypeAscii  = 2,
	kExifTypeShort  = 3
};


static void
_WriteUInt16(BPositionIO* io, uint16 value)
{
	char bytes[2];
	bytes[0] = (char)(value & 0xff);
	bytes[1] = (char)((value >> 8) & 0xff);
	io->Write(bytes, 2);
}


static void
_WriteUInt32(BPositionIO* io, uint32 value)
{
	char bytes[4];
	bytes[0] = (char)(value & 0xff);
	bytes[1] = (char)((value >> 8) & 0xff);
	bytes[2] = (char)((value >> 16) & 0xff);
	bytes[3] = (char)((value >> 24) & 0xff);
	io->Write(bytes, 4);
}


static uint16
_ReadUInt16(const uint8* bytes)
{
	return (uint16)(bytes[0] | (bytes[1] << 8));
}


static uint32
_ReadUInt32(const uint8* bytes)
{
	return (uint32)bytes[0] | ((uint32)bytes[1] << 8)
		| ((uint32)bytes[2] << 16) | ((uint32)bytes[3] << 24);
}


static status_t
_EnsureDirectory(const char* path, BDirectory* outDirectory)
{
	BEntry entry(path, NULL);
	if (entry.Exists()) {
		status_t status = outDirectory->SetTo(path);
		if (status == B_OK || status == B_FILE_EXISTS)
			return B_OK;
		return status;
	}

	BDirectory root("/");
	BString remainder(path);
	if (remainder.Length() > 0 && remainder[0] == '/')
		remainder.Remove(0, 1);

	while (remainder.Length() > 0) {
		int32 slash = remainder.FindFirst('/');
		BString component = slash >= 0
				? BString(remainder.String(), slash) : remainder;
		if (component.Length() > 0) {
			if (!root.Contains(component.String(), B_DIRECTORY_NODE)) {
				status_t status = root.CreateDirectory(component.String(),
					NULL);
				if (status != B_OK && status != B_FILE_EXISTS)
					return status;
			}
			status_t status = root.SetTo(&root, component.String());
			if (status != B_OK)
				return status;
		}
		if (slash < 0)
			break;
		remainder.Remove(0, slash + 1);
	}

	*outDirectory = root;
	return B_OK;
}


static translator_id
_JpegTranslator(BTranslatorRoster* roster)
{
	translator_id* ids = NULL;
	int32 count = 0;
	if (roster->GetAllTranslators(&ids, &count) != B_OK || count == 0)
		return 0;

	translator_id result = 0;
	for (int32 i = 0; i < count; i++) {
		const translation_format* formats = NULL;
		int32 numFormats = 0;
		if (roster->GetOutputFormats(ids[i], &formats, &numFormats) != B_OK)
			continue;
		for (int32 j = 0; j < numFormats; j++) {
			if (formats[j].type == B_JPEG_FORMAT) {
				result = ids[i];
				break;
			}
		}
		if (result != 0)
			break;
	}
	free(ids);
	return result;
}


} // anonymous namespace


/* static */ void
PhotoWriter::BuildExif(const char* cameraName, const char* dateTime,
	uint32 orientation, BMallocIO* outExif)
{
	if (outExif == NULL)
		return;

	const char* make = "VitruvianOS";
	if (cameraName == NULL)
		cameraName = "Camera";
	if (dateTime == NULL)
		dateTime = "2026:01:01 00:00:00";

	const uint32 makeLength = (uint32)strlen(make) + 1;
	const uint32 modelLength = (uint32)strlen(cameraName) + 1;
	const uint32 dateLength = 20;

	const uint32 ifdOffset = 8;
	const uint32 entryCount = 4;
	const uint32 dataOffset = ifdOffset + 2 + entryCount * 12 + 4;

	BMallocIO io;
	io.Write("Exif\0\0", 6);
	_WriteUInt16(&io, 0x4949);			// little-endian
	_WriteUInt16(&io, 0x002A);
	_WriteUInt32(&io, ifdOffset);

	_WriteUInt16(&io, (uint16)entryCount);

	// Make (0x010F)
	_WriteUInt16(&io, 0x010F);
	_WriteUInt16(&io, kExifTypeAscii);
	_WriteUInt32(&io, makeLength);
	_WriteUInt32(&io, dataOffset);

	// Model (0x0110)
	_WriteUInt16(&io, 0x0110);
	_WriteUInt16(&io, kExifTypeAscii);
	_WriteUInt32(&io, modelLength);
	_WriteUInt32(&io, dataOffset + makeLength);

	// Orientation (0x0112)
	_WriteUInt16(&io, 0x0112);
	_WriteUInt16(&io, kExifTypeShort);
	_WriteUInt32(&io, 1);
	_WriteUInt16(&io, (uint16)orientation);
	_WriteUInt16(&io, 0);

	// DateTime (0x0132)
	_WriteUInt16(&io, 0x0132);
	_WriteUInt16(&io, kExifTypeAscii);
	_WriteUInt32(&io, dateLength);
	_WriteUInt32(&io, dataOffset + makeLength + modelLength);

	_WriteUInt32(&io, 0);				// next IFD

	io.Write(make, makeLength);
	io.Write(cameraName, modelLength);
	char dateBuffer[20];
	memset(dateBuffer, 0, sizeof(dateBuffer));
	strncpy(dateBuffer, dateTime, 19);
	io.Write(dateBuffer, dateLength);

	outExif->Write(io.Buffer(), io.BufferLength());
}


static status_t
_WriteJpegWithExif(const char* path, const BMallocIO& jpegData,
	const BMallocIO& exif)
{
	const uint8* bytes = (const uint8*)jpegData.Buffer();
	const size_t length = jpegData.BufferLength();
	if (length < 2 || bytes[0] != 0xff || bytes[1] != 0xd8)
		return B_ERROR;

	// Skip a pre-existing APP1 so the block stays unique.
	size_t insertOffset = 2;
	while (insertOffset + 4 <= length && bytes[insertOffset] == 0xff
		&& bytes[insertOffset + 1] == 0xe1) {
		const uint16 segmentLength = (uint16)((bytes[insertOffset + 2] << 8)
			| bytes[insertOffset + 3]);
		insertOffset += 2 + segmentLength;
	}

	BFile file(path, B_CREATE_FILE | B_WRITE_ONLY | O_TRUNC);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();

	file.Write(bytes, insertOffset);

	uint8 app1Header[4];
	const uint16 payloadLength = (uint16)(exif.BufferLength());
	app1Header[0] = 0xff;
	app1Header[1] = 0xe1;
	app1Header[2] = (uint8)((payloadLength + 2) >> 8);
	app1Header[3] = (uint8)((payloadLength + 2) & 0xff);
	file.Write(app1Header, 4);
	file.Write(exif.Buffer(), payloadLength);

	file.Write(bytes + insertOffset, length - insertOffset);
	return B_OK;
}


/* static */ status_t
PhotoWriter::Save(const BBitmap* bitmap, const char* directory,
	const char* cameraName, uint32 orientation, BPath* outPath)
{
	if (bitmap == NULL || directory == NULL)
		return B_BAD_VALUE;

	BDirectory dir;
	status_t status = _EnsureDirectory(directory, &dir);
	if (status != B_OK)
		return status;

	time_t now = time(NULL);
	struct tm parts;
	localtime_r(&now, &parts);
	char name[64];
	snprintf(name, sizeof(name), "Camera-%04d%02d%02d-%02d%02d%02d.jpg",
		parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
		parts.tm_hour, parts.tm_min, parts.tm_sec);

	BPath path(directory, name, NULL);

	// Burst shots land within one second; never overwrite an earlier one.
	for (int32 suffix = 2; BEntry(path.Path()).Exists() && suffix < 1000;
			suffix++) {
		char unique[80];
		snprintf(unique, sizeof(unique),
			"Camera-%04d%02d%02d-%02d%02d%02d-%d.jpg", parts.tm_year + 1900,
			parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min,
			parts.tm_sec, (int)suffix);
		path.SetTo(directory, unique);
	}

	BFile file(path.Path(), B_CREATE_FILE | B_WRITE_ONLY | O_TRUNC);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();

	BMallocIO flattened;
	status = _FlattenBitmap(bitmap, &flattened);
	if (status != B_OK)
		return status;
	flattened.Seek(0, SEEK_SET);

	BTranslatorRoster* roster = BTranslatorRoster::Default();
	if (roster == NULL)
		return B_NO_INIT;
	translator_id translator = _JpegTranslator(roster);
	if (translator == 0)
		return B_UNSUPPORTED;

	BMallocIO jpegData;
	status = roster->Translate(translator, &flattened, NULL, &jpegData,
		B_JPEG_FORMAT);
	if (status != B_OK)
		return status;

	BMallocIO exif;
	char dateTime[20];
	strftime(dateTime, sizeof(dateTime), "%Y:%m:%d %H:%M:%S", &parts);
	BuildExif(cameraName, dateTime, orientation, &exif);

	status = _WriteJpegWithExif(path.Path(), jpegData, exif);
	if (status != B_OK)
		return status;

	BNode node(path.Path());
	if (node.InitCheck() != B_OK)
		return node.InitCheck();

	const char* mime = "image/jpeg";
	node.WriteAttr("BEOS:TYPE", B_STRING_TYPE, 0, mime, strlen(mime) + 1);
	node.WriteAttr("Media:Orientation", B_INT32_TYPE, 0, &orientation,
		sizeof(orientation));
	if (cameraName != NULL) {
		node.WriteAttr("Media:Camera", B_STRING_TYPE, 0, cameraName,
			strlen(cameraName) + 1);
	}

	if (outPath != NULL)
		*outPath = path;
	return B_OK;
}


/* static */ status_t
PhotoWriter::DecodeFile(const char* path, BBitmap** outBitmap)
{
	if (path == NULL || outBitmap == NULL)
		return B_BAD_VALUE;
	*outBitmap = NULL;

	BFile file(path, B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();

	BTranslatorRoster* roster = BTranslatorRoster::Default();
	if (roster == NULL)
		return B_NO_INIT;

	translator_info info;
	memset(&info, 0, sizeof(info));
	info.type = B_JPEG_FORMAT;
	status_t status = roster->Identify(&file, NULL, &info);
	if (status != B_OK)
		return status;

	BMallocIO raw;
	status = roster->Translate(&file, &info, NULL, &raw,
		B_TRANSLATOR_BITMAP);
	if (status != B_OK)
		return status;

	raw.Seek(0, SEEK_SET);
	BBitmap* bitmap = BTranslationUtils::GetBitmap(&raw, roster);
	if (bitmap == NULL)
		return B_ERROR;
	*outBitmap = bitmap;
	return B_OK;
}


/* static */ status_t
PhotoWriter::ReadExif(const char* path, uint32* orientation,
	BString* cameraName, BString* dateTime)
{
	if (path == NULL)
		return B_BAD_VALUE;

	BFile file(path, B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();

	off_t size = 0;
	if (file.GetSize(&size) != B_OK || size < 8)
		return B_ERROR;

	const size_t readSize = (size_t)size;
	uint8* bytes = (uint8*)malloc(readSize);
	if (bytes == NULL)
		return B_NO_MEMORY;
	if (file.Read(bytes, readSize) != (ssize_t)readSize) {
		free(bytes);
		return B_ERROR;
	}

	status_t result = B_ENTRY_NOT_FOUND;
	size_t offset = 2;
	while (offset + 4 <= readSize && bytes[offset] == 0xff) {
		const uint8 marker = bytes[offset + 1];
		const uint16 segmentLength = (uint16)((bytes[offset + 2] << 8)
			| bytes[offset + 3]);
		if (marker == 0xe1 && offset + 10 <= readSize
			&& memcmp(bytes + offset + 4, "Exif\0\0", 6) == 0) {
			const uint8* tiff = bytes + offset + 10;
			const size_t tiffLength = readSize - offset - 10;
			if (tiffLength < 8 || tiff[0] != 'I' || tiff[1] != 'I') {
				break;
			}
			const uint32 ifdOffset = _ReadUInt32(tiff + 4);
			if (ifdOffset + 2 > tiffLength)
				break;
			const uint16 count = _ReadUInt16(tiff + ifdOffset);
			for (uint16 i = 0; i < count; i++) {
				const uint8* entry = tiff + ifdOffset + 2 + i * 12;
				if (entry + 12 > tiff + tiffLength)
					break;
				const uint16 tag = _ReadUInt16(entry);
				const uint16 type = _ReadUInt16(entry + 2);
				const uint32 valueCount = _ReadUInt32(entry + 4);
				if (tag == 0x0112 && type == kExifTypeShort
					&& orientation != NULL) {
					*orientation = _ReadUInt16(entry + 8);
					result = B_OK;
				} else if ((tag == 0x0110 || tag == 0x0132)
					&& type == kExifTypeAscii) {
					const uint32 valueOffset = _ReadUInt32(entry + 8);
					if (valueOffset + valueCount <= tiffLength) {
						BString value((const char*)tiff + valueOffset);
						if (tag == 0x0110 && cameraName != NULL) {
							*cameraName = value;
							result = B_OK;
						} else if (tag == 0x0132 && dateTime != NULL) {
							*dateTime = value;
							result = B_OK;
						}
					}
				}
			}
			break;
		}
		if (marker == 0xda || marker == 0xd9)
			break;
		offset += 2 + segmentLength;
	}

	free(bytes);
	return result;
}
