/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _HOSTSHIMS_MESSAGE_H
#define _HOSTSHIMS_MESSAGE_H

#include <SupportDefs.h>
#include <String.h>

#include <map>
#include <string>
#include <vector>

class BMessage {
public:
	uint32 what;

	BMessage() : what(0) {}
	BMessage(uint32 what) : what(what) {}
	BMessage(const BMessage& other) : what(other.what), fFields(other.fFields) {}

	BMessage& operator=(const BMessage& other)
	{
		if (this != &other) {
			what = other.what;
			fFields = other.fFields;
		}
		return *this;
	}

	void MakeEmpty() { fFields.clear(); }

	status_t AddString(const char* name, const char* value)
	{
		if (name == NULL)
			return B_BAD_VALUE;
		Field field;
		field.type = 'STRG';
		field.strings.push_back(value != NULL ? value : "");
		_Replace(name, field);
		return B_OK;
	}

	status_t AddUInt32(const char* name, uint32 value)
	{
		if (name == NULL)
			return B_BAD_VALUE;
		Field field;
		field.type = 'UINT';
		field.uints.push_back(value);
		_Replace(name, field);
		return B_OK;
	}

	status_t AddInt32(const char* name, int32 value)
	{
		if (name == NULL)
			return B_BAD_VALUE;
		Field field;
		field.type = 'INTG';
		field.ints.push_back(value);
		_Replace(name, field);
		return B_OK;
	}

	status_t AddBool(const char* name, bool value)
	{
		if (name == NULL)
			return B_BAD_VALUE;
		Field field;
		field.type = 'BOOL';
		field.bools.push_back(value);
		_Replace(name, field);
		return B_OK;
	}

	status_t AddMessage(const char* name, const BMessage* message)
	{
		if (name == NULL || message == NULL)
			return B_BAD_VALUE;
		Field field;
		field.type = 'MSGE';
		field.messages.push_back(*message);
		_Replace(name, field);
		return B_OK;
	}

	status_t FindString(const char* name, const char** value) const
	{
		const Field* field = _Find(name);
		if (field == NULL || field->type != 'STRG' || field->strings.empty())
			return B_NAME_NOT_FOUND;
		if (value != NULL)
			*value = field->strings[0].c_str();
		return B_OK;
	}

	status_t FindString(const char* name, BString* value) const
	{
		const char* string = NULL;
		status_t status = FindString(name, &string);
		if (status == B_OK && value != NULL)
			*value = string;
		return status;
	}

	status_t FindUInt32(const char* name, uint32* value) const
	{
		const Field* field = _Find(name);
		if (field == NULL || field->type != 'UINT' || field->uints.empty())
			return B_NAME_NOT_FOUND;
		if (value != NULL)
			*value = field->uints[0];
		return B_OK;
	}

	status_t FindInt32(const char* name, int32* value) const
	{
		const Field* field = _Find(name);
		if (field == NULL || field->type != 'INTG' || field->ints.empty())
			return B_NAME_NOT_FOUND;
		if (value != NULL)
			*value = field->ints[0];
		return B_OK;
	}

	int32 FindInt32(const char* name, int32 n = 0) const
	{
		(void)n;
		int32 value = 0;
		if (FindInt32(name, &value) != B_OK)
			return 0;
		return value;
	}

	status_t FindBool(const char* name, bool* value) const
	{
		const Field* field = _Find(name);
		if (field == NULL || field->type != 'BOOL' || field->bools.empty())
			return B_NAME_NOT_FOUND;
		if (value != NULL)
			*value = field->bools[0];
		return B_OK;
	}

	status_t FindMessage(const char* name, BMessage* message) const
	{
		const Field* field = _Find(name);
		if (field == NULL || field->type != 'MSGE' || field->messages.empty())
			return B_NAME_NOT_FOUND;
		if (message != NULL)
			*message = field->messages[0];
		return B_OK;
	}

	bool HasUInt32(const char* name) const
	{
		return FindUInt32(name, NULL) == B_OK;
	}

private:
	struct Field {
		uint32 type;
		std::vector<std::string> strings;
		std::vector<uint32> uints;
		std::vector<int32> ints;
		std::vector<bool> bools;
		std::vector<BMessage> messages;
	};

	const Field* _Find(const char* name) const
	{
		if (name == NULL)
			return NULL;
		std::map<std::string, Field>::const_iterator it
			= fFields.find(name);
		if (it == fFields.end())
			return NULL;
		return &it->second;
	}

	void _Replace(const char* name, const Field& field)
	{
		fFields[name] = field;
	}

	std::map<std::string, Field> fFields;
};

#endif
