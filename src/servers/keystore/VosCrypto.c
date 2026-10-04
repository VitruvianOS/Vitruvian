/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "VosCrypto.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>


static int
_vos_Random(uint8_t* buffer, size_t length)
{
	return RAND_bytes(buffer, (int)length) == 1 ? 0 : -1;
}


static int
_vos_Pbkdf2(const uint8_t* password, size_t passwordLength,
	const uint8_t* salt, size_t saltLength, uint8_t* key, size_t keyLength)
{
	return PKCS5_PBKDF2_HMAC((const char*)password, (int)passwordLength,
		salt, (int)saltLength, VOS_KEYRING_ITERATIONS, EVP_sha256(),
		(int)keyLength, key) == 1 ? 0 : -1;
}


static int
_vos_WriteU32(uint8_t* out, uint32_t value)
{
	out[0] = (uint8_t)(value);
	out[1] = (uint8_t)(value >> 8);
	out[2] = (uint8_t)(value >> 16);
	out[3] = (uint8_t)(value >> 24);
	return 0;
}


static uint32_t
_vos_ReadU32(const uint8_t* in)
{
	return (uint32_t)in[0] | ((uint32_t)in[1] << 8)
		| ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}


int
vos_KeyringEncrypt(const uint8_t* key, size_t keyLength,
	const uint8_t* plain, size_t plainLength, uint8_t** _out, size_t* _outLength)
{
	if (key == NULL || keyLength == 0 || plain == NULL || _out == NULL
		|| _outLength == NULL)
		return -1;

	uint8_t salt[VOS_KEYRING_SALT_SIZE];
	uint8_t iv[VOS_KEYRING_IV_SIZE];
	uint8_t derived[32];
	if (_vos_Random(salt, sizeof(salt)) != 0
		|| _vos_Random(iv, sizeof(iv)) != 0
		|| _vos_Pbkdf2(key, keyLength, salt, sizeof(salt), derived,
			sizeof(derived)) != 0)
		return -1;

	size_t headerSize = 8 + sizeof(salt) + sizeof(iv);
	size_t tagSize = VOS_KEYRING_TAG_SIZE;
	uint8_t* out = (uint8_t*)malloc(headerSize + plainLength + tagSize);
	if (out == NULL)
		return -1;

	_vos_WriteU32(out, VOS_KEYRING_MAGIC);
	_vos_WriteU32(out + 4, VOS_KEYRING_VERSION);
	memcpy(out + 8, salt, sizeof(salt));
	memcpy(out + 8 + sizeof(salt), iv, sizeof(iv));

	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL) {
		free(out);
		return -1;
	}

	int ok = 0;
	int length = 0;
	int total = 0;
	uint8_t* cipher = out + headerSize;
	if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1
		&& EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)sizeof(iv),
			NULL) == 1
		&& EVP_EncryptInit_ex(ctx, NULL, NULL, derived, iv) == 1
		&& EVP_EncryptUpdate(ctx, cipher, &length, (const uint8_t*)plain,
			(int)plainLength) == 1) {
		total = length;
		if (EVP_EncryptFinal_ex(ctx, cipher + total, &length) == 1) {
			total += length;
			if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG,
					(int)tagSize, cipher + total) == 1
				&& (size_t)total == plainLength)
				ok = 1;
		}
	}

	EVP_CIPHER_CTX_free(ctx);
	memset(derived, 0, sizeof(derived));
	if (!ok) {
		free(out);
		return -1;
	}

	*_out = out;
	*_outLength = headerSize + plainLength + tagSize;
	return 0;
}


int
vos_KeyringDecrypt(const uint8_t* key, size_t keyLength,
	const uint8_t* sealed, size_t sealedLength, uint8_t** _out,
	size_t* _outLength)
{
	if (key == NULL || keyLength == 0 || sealed == NULL || _out == NULL
		|| _outLength == NULL || sealedLength < 8 + VOS_KEYRING_SALT_SIZE
			+ VOS_KEYRING_IV_SIZE + VOS_KEYRING_TAG_SIZE)
		return -1;

	if (_vos_ReadU32(sealed) != VOS_KEYRING_MAGIC
		|| _vos_ReadU32(sealed + 4) != VOS_KEYRING_VERSION)
		return -1;

	const uint8_t* salt = sealed + 8;
	const uint8_t* iv = sealed + 8 + VOS_KEYRING_SALT_SIZE;
	size_t cipherOffset = 8 + VOS_KEYRING_SALT_SIZE + VOS_KEYRING_IV_SIZE;
	size_t cipherLength = sealedLength - cipherOffset - VOS_KEYRING_TAG_SIZE;
	const uint8_t* cipher = sealed + cipherOffset;
	const uint8_t* tag = sealed + cipherOffset + cipherLength;

	uint8_t derived[32];
	if (_vos_Pbkdf2(key, keyLength, salt, VOS_KEYRING_SALT_SIZE, derived,
			sizeof(derived)) != 0)
		return -1;

	uint8_t* out = (uint8_t*)malloc(cipherLength + 1);
	if (out == NULL) {
		memset(derived, 0, sizeof(derived));
		return -1;
	}

	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL) {
		free(out);
		memset(derived, 0, sizeof(derived));
		return -1;
	}

	int ok = 0;
	int length = 0;
	int total = 0;
	if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1
		&& EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
			(int)VOS_KEYRING_IV_SIZE, NULL) == 1
		&& EVP_DecryptInit_ex(ctx, NULL, NULL, derived, iv) == 1
		&& EVP_DecryptUpdate(ctx, out, &length, cipher, (int)cipherLength)
			== 1) {
		total = length;
		if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG,
				(int)VOS_KEYRING_TAG_SIZE, (void*)tag) == 1
			&& EVP_DecryptFinal_ex(ctx, out + total, &length) == 1) {
			total += length;
			ok = 1;
		}
	}

	EVP_CIPHER_CTX_free(ctx);
	memset(derived, 0, sizeof(derived));
	if (!ok) {
		free(out);
		return -1;
	}

	out[total] = '\0';
	*_out = out;
	*_outLength = (size_t)total;
	return 0;
}


static const char kUpper[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char kLower[] = "abcdefghijklmnopqrstuvwxyz";
static const char kDigits[] = "0123456789";
static const char kSymbols[] = "!@#$%^&*()-_=+[]{};:,.?/";


int
vos_GeneratePassword(char* buffer, size_t bufferLength, size_t length,
	uint32_t charsetFlags)
{
	if (buffer == NULL || bufferLength < length + 1 || length == 0)
		return -1;

	if (charsetFlags == 0)
		charsetFlags = 0x0f;

	char pool[128];
	size_t poolLength = 0;
	if ((charsetFlags & 0x01) != 0) {
		memcpy(pool + poolLength, kUpper, sizeof(kUpper) - 1);
		poolLength += sizeof(kUpper) - 1;
	}
	if ((charsetFlags & 0x02) != 0) {
		memcpy(pool + poolLength, kLower, sizeof(kLower) - 1);
		poolLength += sizeof(kLower) - 1;
	}
	if ((charsetFlags & 0x04) != 0) {
		memcpy(pool + poolLength, kDigits, sizeof(kDigits) - 1);
		poolLength += sizeof(kDigits) - 1;
	}
	if ((charsetFlags & 0x08) != 0) {
		memcpy(pool + poolLength, kSymbols, sizeof(kSymbols) - 1);
		poolLength += sizeof(kSymbols) - 1;
	}
	if (poolLength == 0)
		return -1;

	uint8_t randomBytes[256];
	size_t needed = length;
	if (needed > sizeof(randomBytes))
		needed = sizeof(randomBytes);
	if (_vos_Random(randomBytes, needed) != 0)
		return -1;

	for (size_t i = 0; i < length; i++)
		buffer[i] = pool[randomBytes[i % needed] % poolLength];
	buffer[length] = '\0';
	return 0;
}


static int
_vos_Base32Decode(const char* text, uint8_t* out, size_t outCapacity,
	size_t* _outLength)
{
	static const char* kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
	size_t textLength = strlen(text);
	size_t outIndex = 0;
	uint32_t accumulator = 0;
	int bits = 0;

	for (size_t i = 0; i < textLength; i++) {
		char c = text[i];
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '=')
			continue;
		if (c >= 'a' && c <= 'z')
			c = (char)(c - 'a' + 'A');
		const char* found = strchr(kAlphabet, c);
		if (found == NULL)
			return -1;
		accumulator = (accumulator << 5) | (uint32_t)(found - kAlphabet);
		bits += 5;
		if (bits >= 8) {
			bits -= 8;
			if (outIndex >= outCapacity)
				return -1;
			out[outIndex++] = (uint8_t)((accumulator >> bits) & 0xff);
		}
	}

	*_outLength = outIndex;
	return 0;
}


int
vos_TotpCode(const char* base32Secret, uint64_t timeStep, int digits,
	char* _code)
{
	if (base32Secret == NULL || _code == NULL || (digits != 6 && digits != 8))
		return -1;

	uint8_t secret[128];
	size_t secretLength = 0;
	if (_vos_Base32Decode(base32Secret, secret, sizeof(secret),
			&secretLength) != 0 || secretLength == 0)
		return -1;

	uint8_t counter[8];
	for (int i = 7; i >= 0; i--) {
		counter[i] = (uint8_t)(timeStep & 0xff);
		timeStep >>= 8;
	}

	unsigned int macLength = 0;
	uint8_t mac[EVP_MAX_MD_SIZE];
	if (HMAC(EVP_sha1(), secret, (int)secretLength, counter, sizeof(counter),
			mac, &macLength) == NULL)
		return -1;

	int offset = mac[macLength - 1] & 0x0f;
	uint32_t binary = ((uint32_t)(mac[offset] & 0x7f) << 24)
		| ((uint32_t)mac[offset + 1] << 16)
		| ((uint32_t)mac[offset + 2] << 8) | (uint32_t)mac[offset + 3];
	uint32_t modulus = 1;
	for (int i = 0; i < digits; i++)
		modulus *= 10;

	char text[16];
	snprintf(text, sizeof(text), "%0*u", digits, binary % modulus);
	memcpy(_code, text, (size_t)digits + 1);
	memset(secret, 0, sizeof(secret));
	return 0;
}
