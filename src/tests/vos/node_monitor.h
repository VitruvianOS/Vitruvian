/*
 * Copyright 2024 Dario Casalinuovo. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

/*
 * haiku_node_monitor.h - Haiku-compatible node monitoring for Linux
 * 
 * Shared header for kernel module, eBPF programs, and userspace.
 */

#ifndef _HAIKU_NODE_MONITOR_H
#define _HAIKU_NODE_MONITOR_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/ioctl.h>
#else
#include <stdint.h>
#include <sys/ioctl.h>
//typedef uint8_t  __u8;
//typedef uint32_t __u32;
//typedef uint64_t __u64;
//typedef int32_t  __s32;
//typedef int64_t  __s64;
#endif

/* ============================================================================
 * Haiku constants - matching NodeMonitor.h
 * ============================================================================
 */

/* Watch flags */
#define B_STOP_WATCHING         0x0000
#define B_WATCH_NAME            0x0001
#define B_WATCH_STAT            0x0002
#define B_WATCH_ATTR            0x0004
#define B_WATCH_DIRECTORY       0x0008
#define B_WATCH_ALL             0x000f
#define B_WATCH_MOUNT           0x0010
#define B_WATCH_INTERIM_STAT    0x0020
#define B_WATCH_CHILDREN        0x0040  /* Undocumented Haiku flag */

/* Notification opcodes */
#define B_ENTRY_CREATED         1
#define B_ENTRY_REMOVED         2
#define B_ENTRY_MOVED           3
#define B_STAT_CHANGED          4
#define B_ATTR_CHANGED          5
#define B_DEVICE_MOUNTED        6
#define B_DEVICE_UNMOUNTED      7

/* B_STAT_CHANGED "fields" bitmask */
#define B_STAT_MODE             0x0001
#define B_STAT_UID              0x0002
#define B_STAT_GID              0x0004
#define B_STAT_SIZE             0x0008
#define B_STAT_ACCESS_TIME      0x0010
#define B_STAT_MODIFICATION_TIME 0x0020
#define B_STAT_CREATION_TIME    0x0040
#define B_STAT_CHANGE_TIME      0x0080

/* B_ATTR_CHANGED "cause" values */
#define B_ATTR_CAUSE_CREATED    1
#define B_ATTR_CAUSE_REMOVED    2
#define B_ATTR_CAUSE_CHANGED    3

/* Message 'what' code */
#define B_NODE_MONITOR          0x4e444d4e  /* 'NDMN' */


/* ============================================================================
 * ioctl interface
 * ============================================================================
 */

/* Basic limits */
#define HAIKU_NAME_MAX 256
#define HAIKU_ATTR_MAX 256

/* Request structures */
struct haiku_watch_request {
    int32_t fd;          /* File descriptor to watch */
    uint32_t flags;      /* B_WATCH_* flags */
    int32_t port;        /* Haiku port_id */
    uint32_t token;      /* Handler token */
} __attribute__((packed));

struct haiku_unwatch_request {
    uint64_t device;     /* dev_t (use 64-bit for stable userspace ABI) */
    uint64_t node;       /* ino_t */
    int32_t port;        /* Haiku port_id */
    uint32_t token;      /* Handler token */
} __attribute__((packed));

struct haiku_stop_notifying_request {
    int32_t port;        /* Haiku port_id */
    uint32_t token;      /* Handler token */
} __attribute__((packed));

/* Ioctl commands */
#define HAIKU_NODE_MONITOR_MAGIC 'H'
#define HAIKU_IOC_START_WATCHING \
    _IOW(HAIKU_NODE_MONITOR_MAGIC, 1, struct haiku_watch_request)
#define HAIKU_IOC_STOP_WATCHING \
    _IOW(HAIKU_NODE_MONITOR_MAGIC, 2, struct haiku_unwatch_request)
#define HAIKU_IOC_STOP_NOTIFYING \
    _IOW(HAIKU_NODE_MONITOR_MAGIC, 3, struct haiku_stop_notifying_request)

#define HAIKU_NODE_MONITOR_DEVICE "haiku_node_monitor"

/* Event structures */

enum haiku_event_type {
    HAIKU_EVT_CREATE = 1,
    HAIKU_EVT_DELETE,
    HAIKU_EVT_MOVE,
    HAIKU_EVT_MODIFY,
    HAIKU_EVT_ATTRIB,
    HAIKU_EVT_XATTR,
    HAIKU_EVT_MOUNT,
    HAIKU_EVT_UNMOUNT,
};

struct haiku_event {
    uint32_t type;            /* haiku_event_type */
    uint32_t mask;            /* Original fsnotify mask */
    uint64_t device;          /* dev_t */
    uint64_t inode;           /* ino_t of affected file */
    uint64_t dir_inode;       /* ino_t of parent directory */
    uint64_t cookie;          /* For move correlation */
    uint32_t stat_fields;     /* For B_STAT_CHANGED */
    uint32_t attr_cause;      /* For B_ATTR_CHANGED: B_ATTR_CREATED/REMOVED/CHANGED */
    char name[HAIKU_NAME_MAX];      /* Entry name (NUL-terminated if shorter) */
    char attr_name[HAIKU_ATTR_MAX]; /* xattr name (from eBPF) */
    uint64_t old_dir_inode;   /* For moves */
    char old_name[HAIKU_NAME_MAX];
} __attribute__((packed));

struct xattr_event {
    uint64_t device;
    uint64_t inode;
    uint32_t cause;          /* B_ATTR_CREATED/REMOVED/CHANGED */
    char name[HAIKU_ATTR_MAX];
} __attribute__((packed));
#endif /* _HAIKU_NODE_MONITOR_H */
