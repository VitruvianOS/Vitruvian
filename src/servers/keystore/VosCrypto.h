/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _VOS_CRYPTO_H
#define _VOS_CRYPTO_H


#include <stddef.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif


#define VOS_KEYRING_MAGIC		0x564B5247	/* "VKRG" */
#define VOS_KEYRING_VERSION		1
#define VOS_KEYRING_SALT_SIZE	16
#define VOS_KEYRING_IV_SIZE		12
#define VOS_KEYRING_TAG_SIZE	16
#define VOS_KEYRING_ITERATIONS	200000


/* AES-256-GCM; output is magic||version||salt||iv||ct||tag. */
int
vos_KeyringEncrypt(const uint8_t* key, size_t keyLength,
	const uint8_t* plain, size_t plainLength, uint8_t** _out, size_t* _outLength);


int
vos_KeyringDecrypt(const uint8_t* key, size_t keyLength,
	const uint8_t* sealed, size_t sealedLength, uint8_t** _out,
	size_t* _outLength);


/* charsetFlags: bit0 upper, bit1 lower, bit2 digits, bit3 symbols. */
int
vos_GeneratePassword(char* buffer, size_t bufferLength, size_t length,
	uint32_t charsetFlags);


/* RFC 6238; digits = 6 or 8. base32 = RFC 4648 alphabet without padding. */
int
vos_TotpCode(const char* base32Secret, uint64_t timeStep, int digits,
	char* _code /* digits+1 */);


#ifdef __cplusplus
}
#endif


#endif	/* _VOS_CRYPTO_H */
