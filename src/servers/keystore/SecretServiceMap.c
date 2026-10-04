/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "SecretServiceMap.h"


#include <stdio.h>
#include <string.h>


static void
_sanitize(char* text)
{
	for (size_t i = 0; text[i] != '\0'; i++) {
		char c = text[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '_')
			continue;
		text[i] = '_';
	}
}


int
ss_CollectionPath(const char* keyring, char* _path, size_t pathCapacity)
{
	if (keyring == NULL || _path == NULL || pathCapacity < 32)
		return -1;

	char name[SS_MAX_NAME];
	snprintf(name, sizeof(name), "%s", keyring);
	_sanitize(name);
	snprintf(_path, pathCapacity,
		"/org/freedesktop/secrets/collection/%s", name);
	return 0;
}


int
ss_ItemPath(const char* keyring, const char* identifier,
	const char* username, char* _path, size_t pathCapacity)
{
	if (identifier == NULL || _path == NULL)
		return -1;

	char collection[SS_MAX_NAME * 2];
	if (ss_CollectionPath(keyring != NULL ? keyring : "default", collection,
			sizeof(collection)) != 0)
		return -1;

	char id[SS_MAX_NAME];
	char user[SS_MAX_NAME];
	snprintf(id, sizeof(id), "%s", identifier);
	_sanitize(id);
	if (username != NULL && username[0] != '\0') {
		snprintf(user, sizeof(user), "%s", username);
		_sanitize(user);
		snprintf(_path, pathCapacity, "%s/%s_%s", collection, id, user);
	} else {
		snprintf(_path, pathCapacity, "%s/%s", collection, id);
	}
	return 0;
}


int
ss_AttributeSetAdd(SsAttributeSet* set, const char* key, const char* value)
{
	if (set == NULL || key == NULL || value == NULL
		|| set->count >= SS_MAX_ATTRIBUTES)
		return -1;
	snprintf(set->items[set->count].key, SS_MAX_NAME, "%s", key);
	snprintf(set->items[set->count].value, SS_MAX_NAME, "%s", value);
	set->count++;
	return 0;
}


int
ss_AttributesMatch(const SsAttributeSet* query, const SsAttributeSet* stored)
{
	if (query == NULL || stored == NULL)
		return 0;
	if (query->count == 0)
		return 0;

	for (int i = 0; i < query->count; i++) {
		int found = 0;
		for (int j = 0; j < stored->count; j++) {
			if (strcmp(query->items[i].key, stored->items[j].key) == 0
				&& strcmp(query->items[i].value, stored->items[j].value)
					== 0) {
				found = 1;
				break;
			}
		}
		if (!found)
			return 0;
	}
	return 1;
}
