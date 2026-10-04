/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SECRET_SERVICE_MAP_H
#define _SECRET_SERVICE_MAP_H


#include <stddef.h>


#ifdef __cplusplus
extern "C" {
#endif


#define SS_MAX_ATTRIBUTES	16
#define SS_MAX_NAME			128


typedef struct SsAttribute {
	char key[SS_MAX_NAME];
	char value[SS_MAX_NAME];
} SsAttribute;


typedef struct SsAttributeSet {
	int count;
	SsAttribute items[SS_MAX_ATTRIBUTES];
} SsAttributeSet;


/* Builds /org/freedesktop/secrets/collection/<sanitizedName>. */
int
ss_CollectionPath(const char* keyring, char* _path, size_t pathCapacity);


/* Copies the sanitised form of a keyring name, as used in collection paths. */
int
ss_SanitizeName(const char* keyring, char* _out, size_t capacity);


/* Builds an item path under a collection. */
int
ss_ItemPath(const char* keyring, const char* identifier,
	const char* username, char* _path, size_t pathCapacity);


/* True when every query attribute is present with the same value.
   A query containing the attribute "any" matches every item. */
int
ss_AttributesMatch(const SsAttributeSet* query, const SsAttributeSet* stored);


int
ss_AttributeSetAdd(SsAttributeSet* set, const char* key, const char* value);


#ifdef __cplusplus
}
#endif


#endif	/* _SECRET_SERVICE_MAP_H */
