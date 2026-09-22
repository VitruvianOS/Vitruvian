# API Changes {#api-changes}

Known differences between Vitruvian and the BeOS/Haiku APIs. Source-level compatibility is the goal; most applications compile and run unchanged. This page tracks the exceptions.

## Images

- `image_id` values are local to the application (not system-global as in Haiku).

## Processes

- `start_watching_system` requires superuser privileges.

## Threads

- `suspend_thread` is forbidden. Any application calling it will be killed.
  Haiku spawns threads suspended; Vitruvian uses `pthread_create` which starts immediately. The run-state model differs.

## Virtual references

- `entry_ref` and `node_ref` can be _virtual_ (backed by a Nexus vref) or _real_ (plain Linux `st_dev`/`st_ino`). See the [Nexus]({{< relref "/docs/reference/nexus#virtual-references-vref" >}}) page for details.
- `BMessage::AddNodeRef` is a stub returning `B_ERROR`. Use `AddRef` with an `entry_ref` instead.

## Filesystem

- See [Filesystem Layout]({{< relref "filesystem-layout" >}}) for path mapping and attribute storage.
- `_kern_entry_ref_to_path` is fully implemented for virtual refs (the common case). For real refs with non-root inodes, path resolution is limited.

## Keycodes

- Vitruvian uses **evdev keycodes** throughout, no Haiku/Linux translation layer.
- `B_KEY_DOWN` `"key"` field = `ev.code` (evdev keycode).
- `B_F1_KEY`, `B_LEFT_ARROW`, `B_RETURN`, etc. are redefined to their evdev values.
- Modifier mapping: xkb `Shift` → `B_SHIFT_KEY`, `Ctrl` → `B_CONTROL_KEY`, `Alt` → `B_COMMAND_KEY`.

## OpenGL

- `libopengl.so` is a separate kit. It is not part of `libbe.so`. Applications using `BGLView` must link against `opengl` and `GLU` explicitly.
- `BGLView::Draw` is the application's draw method. The renderer's `Draw(BRect)` is a no-op.

## libbe

This section tracks generic differences in the C++ API provided by `libbe`. It will grow as the compatibility layer matures.
