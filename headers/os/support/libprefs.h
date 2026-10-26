/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#if !defined(LIBPREFS_H)
#define LIBPREFS_H

#if defined(__cplusplus)
 #define EX_C extern "C"
#else
 #define EX_C extern
#endif


#if !defined(_SUPPORT_DEFS_H)
 #include <SupportDefs.h>
#endif


typedef struct _PREFData * PREFData;
typedef struct _PREFHandle * PREFHandle;
typedef struct _PREFTransaction * PREFTransaction;

/*	The requested preference is not there	*/
#define PREF_NOT_FOUND	0xa0b00000
/*	The requested preference is too large for the buffer	*/
#define	PREF_TOO_LARGE	0xa0b00001
/*	Preferences have not been inited	*/
#define	PREF_NOT_INITED	0xa0b00002
/*	Someone is taking too long in a transaction	*/
#define PREF_TIME_OUT 0xa0b00003
/*	There is no implementation available (from glue)	*/
#define PREF_NOT_AVAILABLE	0xa0b00004
/*	Cannot remove for some reason	*/
#define PREF_REMOVE_FAILED	0xa0b00005


#define	PREF_CUR_VERSION	124
#define	PREF_MIN_VERSION	120

/*	Call this to determine what the current version of the		*/
/*	preferences library is.										*/
EX_C const char * PREFVersion(uint32 * out_version, uint32 * out_min_required);

/*	Call this before you use any other preference functions	*/
/*	Will create an empty set if not there						*/
/*	Note that slashes are NOT allowed in the application name	*/
EX_C status_t PREFInit(const char * application, PREFHandle * out_handle);

/*	Remove all traces of preferences stored under this			*/
/*	application name, permanently, from disk. This shuts down	*/
/*	the PREFInit -- don't call PREFShutdown() with this handle	*/
/*	afterwards! Fails if the reference count is > 1.			*/
EX_C status_t PREFRemoveApp(PREFHandle handle);

/*	Call this after you're done with all preference functions	*/
EX_C status_t PREFShutdown(PREFHandle handle);

/*	Call this to locate a specific set of preferences by name	*/
/*	Will create an empty set if not there						*/
/*	UserLevel specifies whether you want prefs to be stored	*/
/*	per user or for the app at large							*/
/*	Remember to balance with a call to PREFDisposeSet()		*/
/*	Note that slashes are used for separating levels of preferences	*/
/*	in a hierarchy that is defined by the using application.		*/
EX_C status_t PREFLoadSet(PREFHandle handle, const char * set, bool user_level, PREFData * data);

/*	Call this to save changed data in a set of preferences		*/
EX_C status_t PREFSaveSet(PREFData data);

/*	Call this function to re-load data from disk (presumably	*/
/*	from within a Listen() notification callback)				*/
EX_C status_t PREFReloadSet(PREFData data);

/*	Call this to un-load a specific set of preferences - you	*/
/*	must balance each call to PREFLoadSet() with a call to 	*/
/*	PREFDisposeSet(). It will set data to NULL if last.		*/
EX_C status_t PREFDisposeSet(PREFData * data);

/*	Remove a current preferences set from storage permanently.	*/
/*	Do not call PREFDisposeSet() with the data afterwards.		*/
/*	Fails if the reference count > 1.							*/
EX_C status_t PREFRemoveSet(PREFData data);

/*	Call this to set some arbitrary data of a preference		*/
EX_C status_t PREFSetData(PREFData data, const char * setting, const void * buffer, size_t size, uint32 type);

/*	Call this to delete a preference							*/
EX_C status_t PREFDelete(PREFData data, const char * setting);

/*	Call this to get a pointer to a copy of some arbitrary 	*/
/*	data you previously stored with PREFSetData(). You do NOT	*/
/*	own the pointer, and it will go away when you call 		*/
/*	PREFDisposeSet() or PREFShutdown().						*/
EX_C status_t PREFGetData(PREFData data, const char * setting, const void ** out_data, ssize_t * out_size, uint32 * out_type);

/*	To appease C++ users, this function must be implemented	*/
/*	by any libprefs implementation. It should return an actual	*/
/*	BMessage that stores the preferences data in question.	*/
/*	Changing data in this message and calling PREFSaveSet()	*/
/*	on the set it came from must save the changed data of	*/
/*	the BMessage. The BMessage must not be disposed of by	*/
/*	the user program.												*/
#if defined(__cplusplus)
class BMessage;
EX_C BMessage * PREFGetMessage(PREFData data);
#endif

/*	Use this function to retrieve a list of application signatures	*/
/*	that have preferences registered under the system.		*/
/*	cookie should start with a 0 value, and will be used by	*/
/*	the library to retain ordering in the iteration.			*/
/*	The handle is any handle you have successfully opened.	*/
EX_C status_t PREFListApplications(PREFHandle handle, bool user_level, uint32 * cookie, char * app_name, size_t size);

/*	Call PREFDisposeApplicationCookie to stop iterating	*/
EX_C status_t PREFDisposeApplicationCookie(PREFHandle handle, uint32 cookie);

/*	Use this function to list the sets that have preferences	*/
/*	registered for a specific preference application instance.	*/
/*	Rules for cookie is as for PREFListApplications.		*/
/*	The handle is the application you want to find preferences	*/
/*	for. 											*/
EX_C status_t PREFListSets(PREFHandle handle, bool user_level, uint32 * cookie, char * set_name, size_t size);

/*	Call PREFDisposeSetCookie to stop iterating	*/
EX_C status_t PREFDisposeSetCookie(PREFHandle handle, uint32 cookie);

/*	Use this function to list the data items in a preference 	*/
/*	set.														*/
/*	Rules for cookie is as for PREFListApplications.			*/
EX_C status_t PREFListData(PREFData data, uint32 * cookie, char * data_name, size_t size, uint32 * data_type);

/*	Call PREFDisposeSetCookie to stop iterating	*/
EX_C status_t PREFDisposeDataCookie(PREFData data, uint32 cookie);

/*	While you are in a preferences transaction, nobody else	*/
/*	can modify the storage representation of the preferences.	*/
/*	This may be important in multi-user, PREFListen()			*/
/*	situations.	Transactions "nest" correctly if you pass a	*/
/*	pointer to a current transaction - else you should pass a	*/
/*	pointer to a NULL PREFTransaction - NEVER just a NULL		*/
/*	pointer!													*/
/*	It is important to note that even if a transaction is		*/
/*	active in one thread, calling PREFSaveSet() from another	*/
/*	thread will not be blocked until that transaction			*/
/*	completes. However, all manipulations result in consistent	*/
/*	data structures, even if they happen in parallel.			*/
EX_C status_t PREFBeginTransaction(PREFData data, PREFTransaction * transaction);

/*	After you're done with your transaction (you should not	*/
/*	spend more than a second or so within a transaction) you 	*/
/*	must call PREFEndTransaction to release the transaction	*/
/*	lock and let others read and write the new data.			*/
EX_C status_t PREFEndTransaction(PREFTransaction transaction);

/*	PREFListen() allows dynamic updating of preferences. If	*/
/*	the library detects changes to the physical preferences,	*/
/*	it will call the callback specified. There can only be		*/
/*	one callback in effect per PREFData at one time. Call		*/
/*	this function with NULL for callback to turn off Listen.	*/
EX_C status_t PREFListen(PREFData data, void (*callback)(PREFData data, void * cookie), void * cookie);


#undef EX_C


#endif	/*	LIBPREFS_H	*/

