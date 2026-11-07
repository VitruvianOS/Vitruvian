/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "KdbxCrypto.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include <argon2.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>


#define KDBX_BLOCK_SIZE			(1024 * 1024)
/* Conservative cost: the packaged libargon2 is unoptimized on this host. */
#define KDBX_ARGON2_MEMORY		(64u * 1024u)
#define KDBX_ARGON2_ITERATIONS	3u
#define KDBX_ARGON2_PARALLELISM	1u

/* Caps for attacker-controlled header and payload values. */
#define KDBX_MAX_FILE_SIZE		(64u * 1024u * 1024u)
#define KDBX_MAX_FIELD_SIZE		(16u * 1024u * 1024u)
#define KDBX_MAX_INFLATE_OUTPUT	(64u * 1024u * 1024u)
#define KDBX_MAX_ARGON2_MEMORY	(256u * 1024u * 1024u)
#define KDBX_MAX_ARGON2_ITERATIONS	16u
#define KDBX_MAX_ARGON2_PARALLELISM	4u
#define KDBX_MAX_ENTRIES		4096


static const uint8_t kAesCbcUuid[16] = {
	0x31, 0xc1, 0xf2, 0xe6, 0xbf, 0x71, 0x43, 0x50,
	0xbe, 0x58, 0x05, 0x21, 0x6a, 0xfc, 0x5a, 0xff
};
static const uint8_t kChaCha20Uuid[16] = {
	0xd6, 0x03, 0x8a, 0x2b, 0x8b, 0x6f, 0x4c, 0xb5,
	0xa5, 0x24, 0x33, 0x9a, 0x31, 0xdb, 0xb5, 0x9a
};
static const uint8_t kArgon2idUuid[16] = {
	0x9e, 0x29, 0x8b, 0x19, 0x56, 0xdb, 0x47, 0x73,
	0xb2, 0x3d, 0xfc, 0x3e, 0xc6, 0xf0, 0xa1, 0xe6
};


struct KdbxBuffer {
	uint8_t* data;
	size_t length;
	size_t capacity;
};


static int
_buf_Reserve(struct KdbxBuffer* buffer, size_t extra)
{
	if (buffer->length + extra <= buffer->capacity)
		return 0;
	size_t capacity = buffer->capacity == 0 ? 256 : buffer->capacity;
	while (capacity < buffer->length + extra)
		capacity *= 2;
	uint8_t* data = (uint8_t*)realloc(buffer->data, capacity);
	if (data == NULL)
		return -1;
	buffer->data = data;
	buffer->capacity = capacity;
	return 0;
}


static int
_buf_Append(struct KdbxBuffer* buffer, const void* data, size_t length)
{
	if (length == 0)
		return 0;
	if (_buf_Reserve(buffer, length) != 0)
		return -1;
	memcpy(buffer->data + buffer->length, data, length);
	buffer->length += length;
	return 0;
}


static int
_buf_AppendU8(struct KdbxBuffer* buffer, uint8_t value)
{
	return _buf_Append(buffer, &value, 1);
}


static int
_buf_AppendU16(struct KdbxBuffer* buffer, uint16_t value)
{
	uint8_t bytes[2];
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	return _buf_Append(buffer, bytes, sizeof(bytes));
}


static int
_buf_AppendU32(struct KdbxBuffer* buffer, uint32_t value)
{
	uint8_t bytes[4];
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
	return _buf_Append(buffer, bytes, sizeof(bytes));
}


static int
_buf_AppendU64(struct KdbxBuffer* buffer, uint64_t value)
{
	uint8_t bytes[8];
	for (int i = 0; i < 8; i++)
		bytes[i] = (uint8_t)(value >> (8 * i));
	return _buf_Append(buffer, bytes, sizeof(bytes));
}


static uint32_t
_readU32(const uint8_t* data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8)
		| ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}


static uint64_t
_readU64(const uint8_t* data)
{
	uint64_t value = 0;
	for (int i = 7; i >= 0; i--)
		value = (value << 8) | data[i];
	return value;
}


static int
_appendField(struct KdbxBuffer* buffer, uint8_t type, const void* value,
	size_t length)
{
	if (_buf_AppendU8(buffer, type) != 0
		|| _buf_AppendU32(buffer, (uint32_t)length) != 0
		|| _buf_Append(buffer, value, length) != 0)
		return -1;
	return 0;
}


static int
_appendVariant(struct KdbxBuffer* buffer, uint8_t type, const char* key,
	const void* value, size_t valueLength)
{
	size_t keyLength = strlen(key);
	if (_buf_AppendU8(buffer, type) != 0
		|| _buf_AppendU32(buffer, (uint32_t)keyLength) != 0
		|| _buf_Append(buffer, key, keyLength) != 0
		|| _buf_AppendU32(buffer, (uint32_t)valueLength) != 0
		|| _buf_Append(buffer, value, valueLength) != 0)
		return -1;
	return 0;
}


static int
_appendVariantU32(struct KdbxBuffer* buffer, const char* key, uint32_t value)
{
	uint8_t bytes[4];
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
	return _appendVariant(buffer, 0x04, key, bytes, sizeof(bytes));
}


static int
_appendVariantU64(struct KdbxBuffer* buffer, const char* key, uint64_t value)
{
	uint8_t bytes[8];
	for (int i = 0; i < 8; i++)
		bytes[i] = (uint8_t)(value >> (8 * i));
	return _appendVariant(buffer, 0x05, key, bytes, sizeof(bytes));
}


static int
_appendVariantBytes(struct KdbxBuffer* buffer, const char* key,
	const uint8_t* value, size_t valueLength)
{
	return _appendVariant(buffer, 0x42, key, value, valueLength);
}


KdbxDatabase*
kdbx_DatabaseNew(const char* name)
{
	KdbxDatabase* database = (KdbxDatabase*)calloc(1, sizeof(KdbxDatabase));
	if (database == NULL)
		return NULL;
	database->name = strdup(name != NULL ? name : "V\\OS");
	if (database->name == NULL) {
		free(database);
		return NULL;
	}
	return database;
}


void
kdbx_DatabaseFree(KdbxDatabase* database)
{
	if (database == NULL)
		return;
	KdbxEntry* entry = database->entries;
	while (entry != NULL) {
		KdbxEntry* next = entry->next;
		free(entry->title);
		free(entry->username);
		free(entry->password);
		free(entry->url);
		free(entry->notes);
		free(entry->totp);
		free(entry);
		entry = next;
	}
	free(database->name);
	free(database);
}


int
kdbx_DatabaseAddEntry(KdbxDatabase* database, const char* title,
	const char* username, const char* password, const char* url,
	const char* notes, const char* totp)
{
	if (database == NULL || title == NULL)
		return -1;

	KdbxEntry* entry = (KdbxEntry*)calloc(1, sizeof(KdbxEntry));
	if (entry == NULL)
		return -1;
	entry->title = strdup(title);
	entry->username = strdup(username != NULL ? username : "");
	entry->password = strdup(password != NULL ? password : "");
	entry->url = strdup(url != NULL ? url : "");
	entry->notes = strdup(notes != NULL ? notes : "");
	entry->totp = totp != NULL ? strdup(totp) : NULL;
	if (entry->title == NULL || entry->username == NULL
		|| entry->password == NULL || entry->url == NULL
		|| entry->notes == NULL) {
		free(entry->title);
		free(entry->username);
		free(entry->password);
		free(entry->url);
		free(entry->notes);
		free(entry->totp);
		free(entry);
		return -1;
	}

	entry->next = database->entries;
	database->entries = entry;
	return 0;
}


static int
_buildXml(const KdbxDatabase* database, struct KdbxBuffer* xml)
{
	const char* prefix =
		"<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\"?>\r\n"
		"<KeePassFile>\r\n"
		"<Meta>\r\n"
		"<Generator>VOS-Passwords</Generator>\r\n"
		"<DatabaseName>";
	if (_buf_Append(xml, prefix, strlen(prefix)) != 0
		|| _buf_Append(xml, database->name, strlen(database->name)) != 0)
		return -1;

	const char* middle =
		"</DatabaseName>\r\n"
		"</Meta>\r\n"
		"<Root>\r\n"
		"<Group>\r\n"
		"<Name>Root</Name>\r\n";
	if (_buf_Append(xml, middle, strlen(middle)) != 0)
		return -1;

	for (KdbxEntry* entry = database->entries; entry != NULL;
			entry = entry->next) {
		const char* entryHead =
			"<Entry>\r\n"
			"<IconID>0</IconID>\r\n";
		if (_buf_Append(xml, entryHead, strlen(entryHead)) != 0)
			return -1;

		const char* keys[] = { "Title", "UserName", "Password", "URL",
			"Notes" };
		const char* values[] = { entry->title, entry->username,
			entry->password, entry->url, entry->notes };
		for (int i = 0; i < 5; i++) {
			if (_buf_Append(xml, "<String><Key>", 13) != 0
				|| _buf_Append(xml, keys[i], strlen(keys[i])) != 0
				|| _buf_Append(xml, "</Key><Value>", 13) != 0
				|| _buf_Append(xml, values[i], strlen(values[i])) != 0
				|| _buf_Append(xml, "</Value></String>\r\n", 19) != 0)
				return -1;
		}

		if (entry->totp != NULL && entry->totp[0] != '\0') {
			const char* otpKey = "otp";
			if (_buf_Append(xml, "<String><Key>", 13) != 0
				|| _buf_Append(xml, otpKey, strlen(otpKey)) != 0
				|| _buf_Append(xml, "</Key><Value>", 13) != 0
				|| _buf_Append(xml, entry->totp, strlen(entry->totp)) != 0
				|| _buf_Append(xml, "</Value></String>\r\n", 19) != 0)
				return -1;
		}

		if (_buf_Append(xml, "</Entry>\r\n", 10) != 0)
			return -1;
	}

	const char* tail = "</Group>\r\n</Root>\r\n</KeePassFile>\r\n";
	return _buf_Append(xml, tail, strlen(tail));
}


static int
_gzip(const uint8_t* input, size_t inputLength, uint8_t** _output,
	size_t* _outputLength)
{
	uLongf bound = compressBound((uLong)inputLength);
	uint8_t* output = (uint8_t*)malloc(bound);
	if (output == NULL)
		return -1;

	z_stream stream;
	memset(&stream, 0, sizeof(stream));
	if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
			Z_DEFAULT_STRATEGY) != Z_OK) {
		free(output);
		return -1;
	}

	stream.next_in = (Bytef*)input;
	stream.avail_in = (uInt)inputLength;
	stream.next_out = output;
	stream.avail_out = (uInt)bound;
	int result = deflate(&stream, Z_FINISH);
	uLongf outLength = stream.total_out;
	deflateEnd(&stream);
	if (result != Z_STREAM_END) {
		free(output);
		return -1;
	}

	*_output = output;
	*_outputLength = (size_t)outLength;
	return 0;
}


static int
_gunzip(const uint8_t* input, size_t inputLength, uint8_t** _output,
	size_t* _outputLength)
{
	z_stream stream;
	memset(&stream, 0, sizeof(stream));
	if (inflateInit2(&stream, 15 + 16) != Z_OK)
		return -1;

	size_t capacity = inputLength * 4 + 4096;
	if (capacity > KDBX_MAX_INFLATE_OUTPUT)
		capacity = KDBX_MAX_INFLATE_OUTPUT;
	uint8_t* output = (uint8_t*)malloc(capacity + 1);
	if (output == NULL) {
		inflateEnd(&stream);
		return -1;
	}

	stream.next_in = (Bytef*)input;
	stream.avail_in = (uInt)inputLength;
	stream.next_out = output;
	stream.avail_out = (uInt)capacity;
	int result = Z_OK;
	size_t total = 0;
	while (result == Z_OK) {
		if (stream.avail_in == 0 && stream.avail_out > 0) {
			result = Z_DATA_ERROR;
			break;
		}
		if (stream.avail_out == 0) {
			if (capacity >= KDBX_MAX_INFLATE_OUTPUT) {
				free(output);
				inflateEnd(&stream);
				return -1;
			}
			size_t next = capacity * 2;
			if (next > KDBX_MAX_INFLATE_OUTPUT)
				next = KDBX_MAX_INFLATE_OUTPUT;
			uint8_t* grown = (uint8_t*)realloc(output, next + 1);
			if (grown == NULL) {
				free(output);
				inflateEnd(&stream);
				return -1;
			}
			output = grown;
			stream.next_out = output + total;
			stream.avail_out = (uInt)(next - total);
			capacity = next;
		}
		result = inflate(&stream, Z_NO_FLUSH);
		total = capacity - stream.avail_out;
	}
	inflateEnd(&stream);
	if (result != Z_STREAM_END) {
		free(output);
		return -1;
	}

	output[total] = '\0';
	*_output = output;
	*_outputLength = total;
	return 0;
}


static int
_hmacSha256(const uint8_t* key, size_t keyLength, const uint8_t* data,
	size_t dataLength, uint8_t out[32])
{
	unsigned int length = 0;
	if (HMAC(EVP_sha256(), key, (int)keyLength, data, (int)dataLength, out,
			&length) == NULL)
		return -1;
	return 0;
}


static void
_hmacBlockKey(const uint8_t hmacSeed[64], uint64_t index, uint8_t out[64])
{
	uint8_t joined[72];
	for (int i = 0; i < 8; i++)
		joined[i] = (uint8_t)(index >> (8 * i));
	memcpy(joined + 8, hmacSeed, 64);
	SHA512(joined, sizeof(joined), out);
}


static int
_aesCbcEncrypt(const uint8_t key[32], const uint8_t iv[16], const uint8_t* plain,
	size_t plainLength, uint8_t** _output, size_t* _outputLength)
{
	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL)
		return -1;

	uint8_t* output = (uint8_t*)malloc(plainLength + 16);
	if (output == NULL) {
		EVP_CIPHER_CTX_free(ctx);
		return -1;
	}

	int length = 0;
	int total = 0;
	int ok = 0;
	if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv) == 1
		&& EVP_EncryptUpdate(ctx, output, &length, (const uint8_t*)plain,
			(int)plainLength) == 1) {
		total = length;
		if (EVP_EncryptFinal_ex(ctx, output + total, &length) == 1) {
			total += length;
			ok = 1;
		}
	}

	EVP_CIPHER_CTX_free(ctx);
	if (!ok) {
		free(output);
		return -1;
	}

	*_output = output;
	*_outputLength = (size_t)total;
	return 0;
}


static int
_aesCbcDecrypt(const uint8_t key[32], const uint8_t iv[16], const uint8_t* cipher,
	size_t cipherLength, uint8_t** _output, size_t* _outputLength)
{
	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL)
		return -1;

	uint8_t* output = (uint8_t*)malloc(cipherLength + 16);
	if (output == NULL) {
		EVP_CIPHER_CTX_free(ctx);
		return -1;
	}

	int length = 0;
	int total = 0;
	int ok = 0;
	if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv) == 1
		&& EVP_DecryptUpdate(ctx, output, &length, (const uint8_t*)cipher,
			(int)cipherLength) == 1) {
		total = length;
		if (EVP_DecryptFinal_ex(ctx, output + total, &length) == 1) {
			total += length;
			ok = 1;
		}
	}

	EVP_CIPHER_CTX_free(ctx);
	if (!ok) {
		free(output);
		return -1;
	}

	*_output = output;
	*_outputLength = (size_t)total;
	return 0;
}


static int
_chacha20Decrypt(const uint8_t key[32], const uint8_t nonce[12],
	const uint8_t* cipher, size_t cipherLength, uint8_t** _output,
	size_t* _outputLength)
{
	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL)
		return -1;

	uint8_t* output = (uint8_t*)malloc(cipherLength);
	if (output == NULL) {
		EVP_CIPHER_CTX_free(ctx);
		return -1;
	}

	int length = 0;
	int total = 0;
	int ok = 0;
	if (EVP_DecryptInit_ex(ctx, EVP_chacha20(), NULL, key, nonce) == 1
		&& EVP_DecryptUpdate(ctx, output, &length, (const uint8_t*)cipher,
			(int)cipherLength) == 1) {
		total = length;
		ok = 1;
	}

	EVP_CIPHER_CTX_free(ctx);
	if (!ok) {
		free(output);
		return -1;
	}

	*_output = output;
	*_outputLength = (size_t)total;
	return 0;
}


int
kdbx_Write(const KdbxDatabase* database, const char* password,
	uint8_t** _output, size_t* _outputLength)
{
	if (database == NULL || password == NULL || _output == NULL
		|| _outputLength == NULL)
		return -1;

	uint8_t passwordHash[32];
	SHA256((const uint8_t*)password, strlen(password), passwordHash);
	uint8_t composite[32];
	SHA256(passwordHash, sizeof(passwordHash), composite);

	uint8_t masterSeed[32];
	uint8_t salt[32];
	uint8_t iv[16];
	if (RAND_bytes(masterSeed, (int)sizeof(masterSeed)) != 1
		|| RAND_bytes(salt, (int)sizeof(salt)) != 1
		|| RAND_bytes(iv, (int)sizeof(iv)) != 1)
		return -1;

	uint8_t transformed[32];
	if (argon2id_hash_raw(KDBX_ARGON2_ITERATIONS, KDBX_ARGON2_MEMORY,
			KDBX_ARGON2_PARALLELISM, composite, sizeof(composite), salt,
			sizeof(salt), transformed, sizeof(transformed)) != ARGON2_OK)
		return -1;

	uint8_t encKeyInput[64];
	memcpy(encKeyInput, masterSeed, 32);
	memcpy(encKeyInput + 32, transformed, 32);
	uint8_t encKey[32];
	SHA256(encKeyInput, sizeof(encKeyInput), encKey);

	uint8_t hmacSeedInput[65];
	memcpy(hmacSeedInput, masterSeed, 32);
	memcpy(hmacSeedInput + 32, transformed, 32);
	hmacSeedInput[64] = 0x01;
	uint8_t hmacSeed[64];
	SHA512(hmacSeedInput, sizeof(hmacSeedInput), hmacSeed);

	struct KdbxBuffer header;
	memset(&header, 0, sizeof(header));
	uint8_t endOfHeader[4] = { 0x0d, 0x0a, 0x0d, 0x0a };
	struct KdbxBuffer kdf;
	memset(&kdf, 0, sizeof(kdf));
	uint32_t compression = 1;
	uint32_t argonMemory = KDBX_ARGON2_MEMORY;
	uint32_t argonVersion = 0x13;
	uint32_t argonParallel = KDBX_ARGON2_PARALLELISM;
	uint64_t argonIterations = KDBX_ARGON2_ITERATIONS;

	if (_buf_AppendU32(&header, 0x9AA2D903) != 0
		|| _buf_AppendU32(&header, 0xB54BFB67) != 0
		|| _buf_AppendU16(&header, 0) != 0
		|| _buf_AppendU16(&header, 4) != 0
		|| _appendField(&header, 2, kAesCbcUuid, sizeof(kAesCbcUuid)) != 0
		|| _appendField(&header, 3, &compression, sizeof(compression)) != 0
		|| _appendField(&header, 4, masterSeed, sizeof(masterSeed)) != 0
		|| _appendField(&header, 7, iv, sizeof(iv)) != 0)
		goto err;

	if (_buf_AppendU16(&kdf, 0x0100) != 0
		|| _appendVariantBytes(&kdf, "$UUID", kArgon2idUuid,
			sizeof(kArgon2idUuid)) != 0
		|| _appendVariantBytes(&kdf, "S", salt, sizeof(salt)) != 0
		|| _appendVariantU32(&kdf, "P", argonParallel) != 0
		|| _appendVariantU64(&kdf, "M", argonMemory) != 0
		|| _appendVariantU64(&kdf, "I", argonIterations) != 0
		|| _appendVariantU32(&kdf, "V", argonVersion) != 0
		|| _buf_AppendU8(&kdf, 0) != 0)
		goto err;

	if (_appendField(&header, 11, kdf.data, kdf.length) != 0
		|| _appendField(&header, 0, endOfHeader, sizeof(endOfHeader)) != 0)
		goto err;

	uint8_t headerHash[32];
	uint8_t headerHmacKeyInput[8 + 64];
	uint8_t headerHmacKey[64];
	uint8_t headerHmac[32];
	SHA256(header.data, header.length, headerHash);
	for (int i = 0; i < 8; i++)
		headerHmacKeyInput[i] = 0xff;
	memcpy(headerHmacKeyInput + 8, hmacSeed, 64);
	SHA512(headerHmacKeyInput, sizeof(headerHmacKeyInput), headerHmacKey);
	_hmacSha256(headerHmacKey, sizeof(headerHmacKey), header.data,
		header.length, headerHmac);

	struct KdbxBuffer xml;
	memset(&xml, 0, sizeof(xml));
	if (_buildXml(database, &xml) != 0)
		goto err;

	/* Inner header is mandatory in KDBX4; we ship EndOfHeader only. */
	struct KdbxBuffer innerHeader;
	memset(&innerHeader, 0, sizeof(innerHeader));
	if (_buf_AppendU8(&innerHeader, 0) != 0
		|| _buf_AppendU32(&innerHeader, 0) != 0
		|| _buf_Append(&innerHeader, xml.data, xml.length) != 0)
		goto err;

	uint8_t* compressed = NULL;
	size_t compressedLength = 0;
	if (_gzip(innerHeader.data, innerHeader.length, &compressed,
			&compressedLength) != 0) {
		free(innerHeader.data);
		goto err;
	}
	free(innerHeader.data);

	uint8_t* encrypted = NULL;
	size_t encryptedLength = 0;
	if (_aesCbcEncrypt(encKey, iv, compressed, compressedLength, &encrypted,
			&encryptedLength) != 0) {
		free(compressed);
		goto err;
	}
	free(compressed);

	struct KdbxBuffer file;
	memset(&file, 0, sizeof(file));
	if (_buf_Append(&file, header.data, header.length) != 0
		|| _buf_Append(&file, headerHash, sizeof(headerHash)) != 0
		|| _buf_Append(&file, headerHmac, sizeof(headerHmac)) != 0)
		goto err_free_encrypted;

	size_t offset = 0;
	uint64_t blockIndex = 0;
	while (offset < encryptedLength) {
		size_t chunk = encryptedLength - offset;
		if (chunk > KDBX_BLOCK_SIZE)
			chunk = KDBX_BLOCK_SIZE;
		uint8_t blockKey[64];
		uint8_t blockHmac[32];
		struct KdbxBuffer blockMacInput;
		memset(&blockMacInput, 0, sizeof(blockMacInput));
		_hmacBlockKey(hmacSeed, blockIndex, blockKey);
		_buf_AppendU64(&blockMacInput, blockIndex);
		_buf_AppendU32(&blockMacInput, (uint32_t)chunk);
		_buf_Append(&blockMacInput, encrypted + offset, chunk);
		_hmacSha256(blockKey, sizeof(blockKey), blockMacInput.data,
			blockMacInput.length, blockHmac);
		if (_buf_Append(&file, blockHmac, sizeof(blockHmac)) != 0
			|| _buf_AppendU32(&file, (uint32_t)chunk) != 0
			|| _buf_Append(&file, encrypted + offset, chunk) != 0) {
			free(blockMacInput.data);
			goto err_free_encrypted;
		}
		free(blockMacInput.data);
		offset += chunk;
		blockIndex++;
	}

{
		uint8_t blockKey[64];
		uint8_t blockHmac[32];
		uint8_t emptyMac[12];
		_hmacBlockKey(hmacSeed, blockIndex, blockKey);
		memset(emptyMac, 0, sizeof(emptyMac));
		for (int i = 0; i < 8; i++)
			emptyMac[i] = (uint8_t)(blockIndex >> (8 * i));
		_hmacSha256(blockKey, sizeof(blockKey), emptyMac, sizeof(emptyMac),
			blockHmac);
		if (_buf_Append(&file, blockHmac, sizeof(blockHmac)) != 0
			|| _buf_AppendU32(&file, 0) != 0) {
			goto err_free_encrypted;
		}
	}

	free(encrypted);
	free(xml.data);
	free(kdf.data);
	free(header.data);
	memset(encKey, 0, sizeof(encKey));
	memset(hmacSeed, 0, sizeof(hmacSeed));
	*_output = file.data;
	*_outputLength = file.length;
	return 0;

err_free_encrypted:
	free(encrypted);
err:
	free(xml.data);
	free(kdf.data);
	free(header.data);
	memset(encKey, 0, sizeof(encKey));
	memset(hmacSeed, 0, sizeof(hmacSeed));
	return -1;
}


static char*
_xmlExtractValue(const char* xml, const char* key, const char** _after)
{
	char pattern[128];
	snprintf(pattern, sizeof(pattern), "<Key>%s</Key><Value>", key);
	const char* start = strstr(xml, pattern);
	if (start == NULL)
		return NULL;
	start += strlen(pattern);
	const char* end = strstr(start, "</Value>");
	if (end == NULL)
		return NULL;
	size_t length = (size_t)(end - start);
	if (length > KDBX_MAX_FIELD_SIZE)
		return NULL;
	char* value = (char*)malloc(length + 1);
	if (value == NULL)
		return NULL;
	memcpy(value, start, length);
	value[length] = '\0';
	*_after = end + strlen("</Value>");
	return value;
}


static int
_parseEntries(const char* xml, KdbxDatabase* database)
{
	const char* cursor = xml;
	int count = 0;
	while ((cursor = strstr(cursor, "<Entry>")) != NULL) {
		if (++count > KDBX_MAX_ENTRIES)
			return -1;
		const char* entryEnd = strstr(cursor, "</Entry>");
		if (entryEnd == NULL)
			break;
		size_t entryLength = (size_t)(entryEnd - cursor);
		if (entryLength > KDBX_MAX_FIELD_SIZE)
			return -1;
		char* entryXml = (char*)malloc(entryLength + 1);
		if (entryXml == NULL)
			return -1;
		memcpy(entryXml, cursor, entryLength);
		entryXml[entryLength] = '\0';

		const char* after = NULL;
		char* title = _xmlExtractValue(entryXml, "Title", &after);
		char* username = _xmlExtractValue(entryXml, "UserName", &after);
		char* password = _xmlExtractValue(entryXml, "Password", &after);
		char* url = _xmlExtractValue(entryXml, "URL", &after);
		char* notes = _xmlExtractValue(entryXml, "Notes", &after);
		char* totp = _xmlExtractValue(entryXml, "otp", &after);
		if (title == NULL)
			title = strdup("");
		int status = 0;
		if (title == NULL)
			status = -1;
		else if (kdbx_DatabaseAddEntry(database, title,
				username != NULL ? username : "",
				password != NULL ? password : "",
				url != NULL ? url : "",
				notes != NULL ? notes : "", totp) != 0)
			status = -1;

		free(title);
		free(username);
		free(password);
		free(url);
		free(notes);
		free(totp);
		free(entryXml);
		if (status != 0)
			return -1;
		cursor = entryEnd + 1;
	}
	return 0;
}


KdbxDatabase*
kdbx_Read(const uint8_t* file, size_t fileLength, const char* password)
{
	if (file == NULL || password == NULL || fileLength < 12 + 64
		|| fileLength > KDBX_MAX_FILE_SIZE)
		return NULL;
	if (_readU32(file) != 0x9AA2D903
		|| _readU32(file + 4) != 0xB54BFB67)
		return NULL;

	size_t offset = 12;
	uint8_t masterSeed[32];
	uint8_t iv[16];
	uint8_t kdfParams[512];
	size_t kdfLength = 0;
	int sawAes = 0;
	int sawChaCha = 0;
	int sawMasterSeed = 0;
	int sawIv = 0;

	while (offset + 5 <= fileLength) {
		uint8_t type = file[offset];
		uint32_t size = _readU32(file + offset + 1);
		const uint8_t* value = file + offset + 5;
		if (size > KDBX_MAX_FIELD_SIZE)
			return NULL;
		if (offset + 5 + (size_t)size > fileLength)
			return NULL;
		if (type == 0)
			break;
		if (type == 2 && size == 16) {
			if (memcmp(value, kAesCbcUuid, 16) == 0)
				sawAes = 1;
			else if (memcmp(value, kChaCha20Uuid, 16) == 0)
				sawChaCha = 1;
		} else if (type == 4 && size == 32) {
			memcpy(masterSeed, value, 32);
			sawMasterSeed = 1;
		} else if (type == 7) {
			if (size == 16) {
				memcpy(iv, value, 16);
				sawIv = 1;
			} else if (size == 12) {
				memcpy(iv, value, 12);
				sawIv = 1;
			} else
				return NULL;
		} else if (type == 11 && size <= sizeof(kdfParams)) {
			memcpy(kdfParams, value, size);
			kdfLength = size;
		}
		offset += 5 + (size_t)size;
	}

	if (offset + 5 > fileLength || file[offset] != 0
		|| _readU32(file + offset + 1) != 4
		|| memcmp(file + offset + 5, "\x0d\x0a\x0d\x0a", 4) != 0)
		return NULL;
	offset += 9;

	if (offset + 64 > fileLength)
		return NULL;
	const uint8_t* headerBytes = file;
	size_t headerLength = offset;
	uint8_t headerHash[32];
	SHA256(headerBytes, headerLength, headerHash);
	if (memcmp(file + offset, headerHash, 32) != 0)
		return NULL;
	offset += 32;

	if (!sawAes || !sawMasterSeed || !sawIv || kdfLength == 0)
		return NULL;

	uint8_t argonSalt[32];
	size_t argonSaltLength = 0;
	uint32_t argonMemory = 64u * 1024u * 1024u;
	uint32_t argonParallel = 1;
	uint64_t argonIterations = 2;
	int sawArgon2id = 0;

	{
		size_t pos = 2;
		if (kdfLength < 2)
			return NULL;
		while (pos + 1 < kdfLength) {
			if (kdfParams[pos] == 0)
				break;
			uint8_t vtype = kdfParams[pos];
			if (pos + 5 > kdfLength)
				return NULL;
			uint32_t keyLength = _readU32(kdfParams + pos + 1);
			if (pos + 5 + (size_t)keyLength + 4 > kdfLength)
				return NULL;
			const char* keyName = (const char*)(kdfParams + pos + 5);
			uint32_t valueLength = _readU32(kdfParams + pos + 5 + keyLength);
			const uint8_t* value = kdfParams + pos + 5 + keyLength + 4;
			if (pos + 5 + (size_t)keyLength + 4 + (size_t)valueLength
					> kdfLength)
				return NULL;
			if (keyLength == 5 && memcmp(keyName, "$UUID", 5) == 0
				&& valueLength == 16
				&& memcmp(value, kArgon2idUuid, 16) == 0)
				sawArgon2id = 1;
			else if (keyLength == 1 && keyName[0] == 'S'
				&& valueLength <= sizeof(argonSalt)) {
				memcpy(argonSalt, value, valueLength);
				argonSaltLength = valueLength;
			} else if (keyLength == 1 && keyName[0] == 'P'
				&& vtype == 0x04 && valueLength == 4)
				argonParallel = _readU32(value);
			else if (keyLength == 1 && keyName[0] == 'M'
				&& vtype == 0x05 && valueLength == 8)
				argonMemory = (uint32_t)_readU64(value);
			else if (keyLength == 1 && keyName[0] == 'I'
				&& vtype == 0x05 && valueLength == 8)
				argonIterations = _readU64(value);
			pos += 5 + (size_t)keyLength + 4 + (size_t)valueLength;
		}
	}

	if (!sawArgon2id || argonSaltLength == 0)
		return NULL;
	if (argonMemory == 0 || argonMemory > KDBX_MAX_ARGON2_MEMORY
		|| argonIterations == 0
		|| argonIterations > KDBX_MAX_ARGON2_ITERATIONS
		|| argonParallel == 0 || argonParallel > KDBX_MAX_ARGON2_PARALLELISM)
		return NULL;

	uint8_t passwordHash[32];
	SHA256((const uint8_t*)password, strlen(password), passwordHash);
	uint8_t composite[32];
	SHA256(passwordHash, sizeof(passwordHash), composite);
	uint8_t transformed[32];
	if (argon2id_hash_raw((uint32_t)argonIterations, argonMemory,
			argonParallel, composite, sizeof(composite), argonSalt,
			argonSaltLength, transformed, sizeof(transformed)) != ARGON2_OK)
		return NULL;

	uint8_t encKeyInput[64];
	memcpy(encKeyInput, masterSeed, 32);
	memcpy(encKeyInput + 32, transformed, 32);
	uint8_t encKey[32];
	SHA256(encKeyInput, sizeof(encKeyInput), encKey);
	uint8_t hmacSeedInput[65];
	memcpy(hmacSeedInput, masterSeed, 32);
	memcpy(hmacSeedInput + 32, transformed, 32);
	hmacSeedInput[64] = 0x01;
	uint8_t hmacSeed[64];
	SHA512(hmacSeedInput, sizeof(hmacSeedInput), hmacSeed);

	uint8_t headerHmacKeyInput[72];
	uint8_t headerHmacKey[64];
	for (int i = 0; i < 8; i++)
		headerHmacKeyInput[i] = 0xff;
	memcpy(headerHmacKeyInput + 8, hmacSeed, 64);
	SHA512(headerHmacKeyInput, sizeof(headerHmacKeyInput), headerHmacKey);
	uint8_t expectedHmac[32];
	_hmacSha256(headerHmacKey, sizeof(headerHmacKey), file, headerLength,
		expectedHmac);
	if (memcmp(file + offset, expectedHmac, 32) != 0)
		return NULL;
	offset += 32;

	struct KdbxBuffer cipher;
	memset(&cipher, 0, sizeof(cipher));
	uint64_t blockIndex = 0;
	int sawTerminator = 0;
	while (offset < fileLength) {
		if (offset + 36 > fileLength)
			goto err;
		const uint8_t* blockHmac = file + offset;
		uint32_t blockSize = _readU32(file + offset + 32);
		if (offset + 36 + blockSize > fileLength)
			goto err;
		const uint8_t* blockData = file + offset + 36;
		uint8_t blockKey[64];
		_hmacBlockKey(hmacSeed, blockIndex, blockKey);
		if (blockSize > KDBX_BLOCK_SIZE)
			goto err;
		uint8_t* macInput = (uint8_t*)malloc(12 + blockSize);
		if (macInput == NULL)
			goto err;
		for (int i = 0; i < 8; i++)
			macInput[i] = (uint8_t)(blockIndex >> (8 * i));
		macInput[8] = (uint8_t)blockSize;
		macInput[9] = (uint8_t)(blockSize >> 8);
		macInput[10] = (uint8_t)(blockSize >> 16);
		macInput[11] = (uint8_t)(blockSize >> 24);
		if (blockSize > 0)
			memcpy(macInput + 12, blockData, blockSize);
		uint8_t blockHmacExpected[32];
		_hmacSha256(blockKey, sizeof(blockKey), macInput, 12 + blockSize,
			blockHmacExpected);
		free(macInput);
		if (memcmp(blockHmac, blockHmacExpected, 32) != 0)
			goto err;
		if (blockSize == 0) {
			sawTerminator = 1;
			offset += 36;
			break;
		}
		if (_buf_Append(&cipher, blockData, blockSize) != 0)
			goto err;
		offset += 36 + blockSize;
		blockIndex++;
	}

	if (!sawTerminator)
		goto err;

	uint8_t* plain = NULL;
	size_t plainLength = 0;
	if (sawChaCha) {
		if (_chacha20Decrypt(encKey, iv, cipher.data, cipher.length, &plain,
				&plainLength) != 0)
			goto err;
	} else {
		if (_aesCbcDecrypt(encKey, iv, cipher.data, cipher.length, &plain,
				&plainLength) != 0)
			goto err;
	}
	free(cipher.data);

	uint8_t* inflated = NULL;
	size_t inflatedLength = 0;
	if (_gunzip(plain, plainLength, &inflated, &inflatedLength) != 0) {
		free(plain);
		return NULL;
	}
	free(plain);

	/* Skip the inner header; the XML follows its EndOfHeader field. */
	size_t innerOffset = 0;
	if (inflatedLength > 5 && inflated[0] == '<') {
		/* No inner header: raw XML payload. */
		innerOffset = 0;
	} else {
		while (innerOffset + 5 <= inflatedLength) {
			uint8_t type = inflated[innerOffset];
			uint32_t size = _readU32(inflated + innerOffset + 1);
			if (innerOffset + 5 + size > inflatedLength)
				break;
			if (type == 0) {
				innerOffset += 5 + size;
				break;
			}
			innerOffset += 5 + size;
		}
	}

	const char* xml = (const char*)(inflated + innerOffset);
	KdbxDatabase* database = kdbx_DatabaseNew("Imported");
	if (database == NULL) {
		free(inflated);
		return NULL;
	}
	if (_parseEntries(xml, database) != 0) {
		kdbx_DatabaseFree(database);
		free(inflated);
		return NULL;
	}

	free(inflated);
	memset(encKey, 0, sizeof(encKey));
	memset(hmacSeed, 0, sizeof(hmacSeed));
	return database;

err:
	free(cipher.data);
	memset(encKey, 0, sizeof(encKey));
	memset(hmacSeed, 0, sizeof(hmacSeed));
	return NULL;
}
