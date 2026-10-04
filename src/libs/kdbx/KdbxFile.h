/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KDBX_FILE_H
#define _KDBX_FILE_H


#include <String.h>

#include <KeyStore.h>


class KdbxFile {
public:
								KdbxFile();
								~KdbxFile();

			status_t			ExportToPath(const char* path,
									const char* password,
									const char* keyring);
			status_t			ImportFromPath(const char* path,
									const char* password,
									const char* keyring,
									bool overwriteExisting);

private:
			status_t			_BuildFromKeyStore(const char* keyring,
									const char* password,
									uint8** _data, size_t* _length);
			status_t			_WriteToKeyStore(const uint8* data,
									size_t length, const char* password,
									const char* keyring,
									bool overwriteExisting);
};


#endif	/* _KDBX_FILE_H */
