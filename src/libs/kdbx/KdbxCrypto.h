/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KDBX_CRYPTO_H
#define _KDBX_CRYPTO_H


#include <stddef.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif


typedef struct KdbxEntry {
	char* title;
	char* username;
	char* password;
	char* url;
	char* notes;
	char* totp;
	struct KdbxEntry* next;
} KdbxEntry;


typedef struct KdbxDatabase {
	char* name;
	KdbxEntry* entries;
} KdbxDatabase;


KdbxDatabase*
kdbx_DatabaseNew(const char* name);


void
kdbx_DatabaseFree(KdbxDatabase* database);


int
kdbx_DatabaseAddEntry(KdbxDatabase* database, const char* title,
	const char* username, const char* password, const char* url,
	const char* notes, const char* totp);


/* Writes a KDBX 4 file (Argon2id + AES-256-CBC + gzip). Caller frees. */
int
kdbx_Write(const KdbxDatabase* database, const char* password,
	uint8_t** _output, size_t* _outputLength);


/* Reads a KDBX 4 file. Salsa20-protected strings are not supported. */
KdbxDatabase*
kdbx_Read(const uint8_t* file, size_t fileLength, const char* password);


#ifdef __cplusplus
}
#endif


#endif	/* _KDBX_CRYPTO_H */
