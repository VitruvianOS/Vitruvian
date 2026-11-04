# Nexus {#nexus}

Nexus is the Vitruvian kernel subsystem that bridges the Linux kernel with the BeOS/Haiku runtime. It's implemented as a set of Linux kernel modules (loaded at boot) that emulate the IPC and filesystem APIs Haiku provides natively in its monolithic kernel.

Haiku applications expect ports, semaphores, shared memory areas, thread control, virtual file references, and node monitoring. None of these exist in Linux in BeOS-compatible form. Nexus fills that gap.

## Device layout

Each Nexus subsystem is exposed as a character device:

| Device                    | Subsystem                  |
| ------------------------- | -------------------------- |
| `/dev/nexus`              | ports, semaphores, threads |
| `/dev/nexus_area`         | shared memory areas        |
| `/dev/nexus_node_monitor` | filesystem watches         |

Every process opens its own file descriptors to these devices at startup via `BKernelPrivate::Team::Init()`. File descriptors are process-local and not inherited across fork; children re-initialize via a constructor.

## IPC primitives

### Ports

Haiku-style message ports: bounded queues with read/write operations.

- `NEXUS_PORT_CREATE`: create a new port
- `NEXUS_PORT_CLOSE`: close a port (no new writers)
- `NEXUS_PORT_DELETE`: destroy a port
- `NEXUS_PORT_READ` / `NEXUS_PORT_WRITE`: send and receive messages
- `NEXUS_PORT_FIND`: look up a port by name
- `NEXUS_PORT_INFO` / `NEXUS_PORT_MESSAGE_INFO`: query port state

Limits: max queue depth 4096, max message size 256 KB.

### Semaphores

Standard counting semaphores:

- `NEXUS_SEM_CREATE` / `NEXUS_SEM_DELETE`
- `NEXUS_SEM_ACQUIRE` / `NEXUS_SEM_RELEASE`
- `NEXUS_SEM_COUNT` / `NEXUS_SEM_INFO` / `NEXUS_SEM_NEXT_INFO`

### Threads

Thread lifecycle and communication:

- `NEXUS_THREAD_SPAWN`: create a new thread
- `NEXUS_THREAD_SET_NAME` / `NEXUS_THREAD_RESUME`
- `NEXUS_THREAD_READ` / `NEXUS_THREAD_WRITE` / `NEXUS_THREAD_HAS_DATA`
- `NEXUS_THREAD_WAITFOR` / `NEXUS_THREAD_WAIT_NEWBORN`
- `NEXUS_THREAD_SET_RETURN_CODE`

Note: Haiku spawns threads in suspended state; `resume_thread()` starts them. Vitruvian's `pthread_create` starts immediately, so the run-state model differs.

### Areas

Shared memory regions backed by `memfd_create` + `mmap`:

- `NEXUS_AREA_CREATE` / `NEXUS_AREA_CLONE` / `NEXUS_AREA_DELETE`
- `NEXUS_AREA_FIND` / `NEXUS_AREA_GET_INFO` / `NEXUS_AREA_GET_NEXT`
- `NEXUS_AREA_RESIZE` / `NEXUS_AREA_SET_PROTECTION` / `NEXUS_AREA_TRANSFER`

Cloning an area maps the same memfd into the target process.

## Node monitor

Provides filesystem event notifications compatible with the BeOS `node_monitor` API:

- `B_ENTRY_CREATED`: file or directory created
- `B_ENTRY_REMOVED`: file or directory removed
- `B_ENTRY_MOVED`: file or directory renamed or moved
- `B_STAT_CHANGED`: metadata changed (size, mtime, etc.)
- `B_ATTR_CHANGED`: extended attribute added, modified, or removed
- `B_DEVICE_MOUNTED` / `B_DEVICE_UNMOUNTED`: volume mounted or unmounted

Nexus hooks into Linux's `fsnotify` subsystem and translates events into `B_NODE_MONITOR` messages. This is what allows Tracker to watch directories and respond to changes in real time, exactly as it would on Haiku.

## Team registration

Every process that uses Nexus opens the devices and registers itself as a team. On process exit, the kernel module cleans up all resources owned by that team (ports, semaphores, areas, vrefs).

## Deployment

Nexus ships as a **DKMS package** (`nexus-dkms`). It auto-rebuilds on kernel update. The DKMS source copy at `debian/nexus-dkms/usr/src/nexus-1/nexus/` must be kept in sync with the main `nexus/` tree: every ioctl addition and every fix goes in both places.

## Source

Nexus source lives in the Vitruvian repo at `src/system/kernel/nexus/nexus/`. Module files:

- `nexus_core.c`: ioctl dispatch, team registration
- `port.c`: port implementation
- `sem.c`: semaphore implementation
- `vref.c`: virtual reference implementation
- `node_monitor.c`: filesystem watches
- `area.c`: shared memory
- `nexus.h`: all ioctl definitions and structs (shared with userspace)

Userspace wrappers are in `src/system/libroot2/`: `port.cpp`, `sem.cpp`, `area.cpp`, `thread.cpp`, `fs/vref.cpp`.

The standalone repository is at [github.com/Numerio/Nexus](https://github.com/Numerio/Nexus), included as a submodule.
