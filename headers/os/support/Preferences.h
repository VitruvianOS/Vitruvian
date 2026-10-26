/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#if !defined(PREFERENCES_H)
#define PREFERENCES_H

#if !defined(LIBPREFS_H)
#include <libprefs.h>
#endif
#include <TypeConstants.h>


class
Preferences
{
	friend class PreferenceSet;
				/*	not implemented!	*/
								Preferences(
									const Preferences &		dummy);
		Preferences &			operator=(
									const Preferences &		dummy);
public:
								Preferences(
									const char *			application)
									{
										fHandle = NULL;
										fInit = PREFInit(application, &fHandle);
									}
								~Preferences()
									{
										if (fHandle != NULL)
											PREFShutdown(fHandle);
									}

		status_t				InitCheck()
									{
										return fInit;
									}

								operator PREFHandle()
									{
										return fHandle;
									}
protected:

		status_t				fInit;
		PREFHandle				fHandle;
};


class
PreferenceSet
{
					/*	not implemented on purpose!	*/
								PreferenceSet(
									const PreferenceSet &	dummy);
		PreferenceSet &			operator=(
									const PreferenceSet &	dummy);

public:
					/*	user-level means per-user settings	*/
								PreferenceSet(
									Preferences &			preferences,
									const char *			set_name,
									bool					user_level) :
									fPreferences(preferences)
									{
										fData = NULL;
										fInit = PREFLoadSet(preferences.fHandle, set_name, user_level, &fData);
									}
								~PreferenceSet()
									{
										if (fData != NULL)
											PREFDisposeSet(&fData);
									}

		status_t				InitCheck()
									{
										return fInit;
									}

					/*	save is NOT automatically done	*/
		status_t				Save()
									{
										return PREFSaveSet(fData);
									}
		status_t				Reload()
									{
										return PREFReloadSet(fData);
									}

		status_t				SetData(
									const char *			name,
									const void *			data,
									size_t					size,
									uint32					type = B_RAW_TYPE)
									{
										return PREFSetData(fData, name, data, size, type);
									}

		status_t				Delete(
									const char *			name)
									{
										return PREFDelete(fData, name);
									}

					/*	pointer is pointer to original	*/
		status_t				GetData(
									const char *			name,
									const void *&			out_data,
									ssize_t &				out_size,
									uint32 &				out_type)
									{
										return PREFGetData(fData, name, &out_data, &out_size, &out_type);
									}

								operator PREFData()
									{
										return fData;
									}

		BMessage &			GetMessage()
									{
										return *PREFGetMessage(fData);
									}
							operator BMessage * ()
									{
										return PREFGetMessage(fData);
									}
protected:

		Preferences &			fPreferences;
		status_t				fInit;
		PREFData				fData;
};


class
PrefTransaction
{
						/*	unimplemented on purpose!	*/
								PrefTransaction(
									const PrefTransaction &	trans);
		PrefTransaction &		operator=(
									const PrefTransaction &	trans);
public:

								PrefTransaction(
									PreferenceSet &			set)
									{
										fTransaction = NULL;
										fInit = PREFBeginTransaction(set, &fTransaction);
									}
								~PrefTransaction()
									{
										if (fInit >= B_OK)
											PREFEndTransaction(fTransaction);
									}

		status_t				InitCheck()
									{
										return fInit;
									}

protected:

		status_t				fInit;
		PREFTransaction			fTransaction;
};


#endif	/*	PREFERENCES_H		*/


