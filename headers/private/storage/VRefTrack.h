/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Lifecycle tracker for entry_ref / node_ref / vref operations.
 *
 * Activated by setting the environment variable VREF_TRACE to a directory
 * path (one file per process is written into that directory, named
 * "<comm>.<pid>.log"). When VREF_TRACE is unset all tracker calls are
 * cheap no-ops (single atomic load).
 *
 * The intent is to merge these per-process logs with kernel dmesg lines
 * (nexus_vref / nexus_port) by timestamp to reconstruct the full
 * lifecycle of every vref id seen during boot.
 */
#ifndef _V_REF_TRACK_H_
#define _V_REF_TRACK_H_


#include <SupportDefs.h>
#include <sys/types.h>


struct entry_ref;
struct node_ref;


namespace BPrivate {


enum vref_track_event {
	VREF_TRACK_EREF_CTOR_DEFAULT,
	VREF_TRACK_EREF_CTOR_DEVDIR,
	VREF_TRACK_EREF_CTOR_FD,
	VREF_TRACK_EREF_CTOR_NODE,
	VREF_TRACK_EREF_CTOR_COPY,
	VREF_TRACK_EREF_DTOR,
	VREF_TRACK_EREF_ASSIGN,
	VREF_TRACK_EREF_SET_NAME,
	VREF_TRACK_EREF_FLATTEN,
	VREF_TRACK_EREF_UNFLATTEN,

	VREF_TRACK_NREF_CTOR_DEFAULT,
	VREF_TRACK_NREF_CTOR_DEVNODE,
	VREF_TRACK_NREF_CTOR_FD,
	VREF_TRACK_NREF_CTOR_COPY,
	VREF_TRACK_NREF_DTOR,
	VREF_TRACK_NREF_ASSIGN,

	VREF_TRACK_VREF_ACQUIRE,
	VREF_TRACK_VREF_RELEASE,
	VREF_TRACK_VREF_OPEN,
	VREF_TRACK_VREF_ACQUIRE_FROM_FD,
	VREF_TRACK_VREF_CREATE,

	VREF_TRACK_MSG_ADD_REF,
	VREF_TRACK_MSG_FIND_REF,
	VREF_TRACK_MSG_COLLECT_CAP,
};


bool vref_track_enabled();

class VRefTrackAccess {
public:
	static dev_t vdevice(const entry_ref& r);
	static ino_t vdirectory(const entry_ref& r);
	static dev_t vdevice(const node_ref& r);
	static ino_t vnode(const node_ref& r);
};

void vref_track_eref(vref_track_event op, const entry_ref* ref,
	const void* caller);

void vref_track_nref(vref_track_event op, const node_ref* ref,
	const void* caller);

void vref_track_vref(vref_track_event op, uint32 id, const void* origin,
	const void* caller);

void vref_track_flatten(vref_track_event op, const entry_ref* eref,
	const node_ref* nref, const void* buffer, size_t size,
	const void* caller);

void vref_track_msg(vref_track_event op, uint32 what, const char* name,
	const entry_ref* eref, const void* caller);


// Convenience: fetch return address of caller (one frame up).
#define VREF_TRACK_CALLER ((const void*)__builtin_return_address(0))


// Cheap fast-path macros — evaluate side-effect-free arguments only if
// tracking is enabled. Entirely compiled out unless ENABLE_VREF_TRACKING
// is defined at build time. Even when compiled in, the runtime fast path
// is a single atomic load against the VREF_TRACE env-var probe.
#ifdef ENABLE_VREF_TRACKING

#define VREF_TRACK_EREF(op, ref) \
	do { if (BPrivate::vref_track_enabled()) \
		BPrivate::vref_track_eref((op), (ref), VREF_TRACK_CALLER); \
	} while (0)

#define VREF_TRACK_NREF(op, ref) \
	do { if (BPrivate::vref_track_enabled()) \
		BPrivate::vref_track_nref((op), (ref), VREF_TRACK_CALLER); \
	} while (0)

#define VREF_TRACK_VREF(op, id, origin) \
	do { if (BPrivate::vref_track_enabled()) \
		BPrivate::vref_track_vref((op), (uint32)(id), (origin), \
			VREF_TRACK_CALLER); \
	} while (0)

#define VREF_TRACK_FLATTEN(op, eref, nref, buf, sz) \
	do { if (BPrivate::vref_track_enabled()) \
		BPrivate::vref_track_flatten((op), (eref), (nref), (buf), (sz), \
			VREF_TRACK_CALLER); \
	} while (0)

#define VREF_TRACK_MSG(op, what, name, eref) \
	do { if (BPrivate::vref_track_enabled()) \
		BPrivate::vref_track_msg((op), (uint32)(what), (name), (eref), \
			VREF_TRACK_CALLER); \
	} while (0)

#else // !ENABLE_VREF_TRACKING

#define VREF_TRACK_EREF(op, ref)				do { (void)(ref); } while (0)
#define VREF_TRACK_NREF(op, ref)				do { (void)(ref); } while (0)
#define VREF_TRACK_VREF(op, id, origin)			do { (void)(id); } while (0)
#define VREF_TRACK_FLATTEN(op, eref, nref, buf, sz) \
	do { (void)(buf); (void)(sz); } while (0)
#define VREF_TRACK_MSG(op, what, name, eref)	do { (void)(name); } while (0)

#endif // ENABLE_VREF_TRACKING


} // namespace BPrivate


#endif // _V_REF_TRACK_H_
