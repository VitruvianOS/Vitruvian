# Coding Guidelines {#coding-guidelines}

Vitruvian inherits Haiku's coding conventions, which descend from the original BeOS API style. Consistency matters more than personal preference; when touching existing code, match the surrounding style.

## Indentation

Use **tabs**, not spaces. A tab displays as 4 characters. Continuation lines are tab-indented to align under the opening paren, never with spaces.

```cpp
virtual status_t  GetPixelClockLimits(display_mode* mode,
                      uint32* _low, uint32* _high) = 0;
```

## Control flow: always on its own line

Never write the single-line form:

```cpp
// WRONG
if (!req) return B_NO_MEMORY;
if (!dev) return;
if (!prop) continue;
```

Always:

```cpp
if (!req)
    return B_NO_MEMORY;

if (!dev)
    return;

if (!prop)
    continue;
```

Braces around a single-statement body are optional and usually omitted.

## Braces

Opening braces go on their own line for function and class **definitions**. For control flow (`if`, `for`, `while`), the brace goes at the end of the line.

```cpp
void
NodeMonitor::HandleEvent(uint32 opcode)
{
    if (opcode == B_ENTRY_CREATED) {
        NotifyListeners();
    }
}
```

Function return types go on the line above the qualified name:

```cpp
status_t
NodeMonitor::StartWatching(const node_ref* ref)
{
    ...
}
```

## No unnecessary block-scope braces

Don't introduce `{ }` for visual grouping if the scope isn't needed.

```cpp
// WRONG
{
    BRect full(0, 0, w - 1, h - 1);
    _BlitRect(src, dst, full);
}

// RIGHT
BRect full(0, 0, w - 1, h - 1);
_BlitRect(src, dst, full);
```

Exception: when you genuinely want a local variable's lifetime constrained.

## `virtual` only on declarations

`virtual` belongs in the class declaration in the header. The out-of-class definition does **not** repeat it.

Don't use `override` in this codebase (it predates broad C++11 usage in Haiku).

## Naming

| Kind                  | Style             | Example              |
| --------------------- | ----------------- | -------------------- |
| Classes and structs   | `UpperCamelCase`  | `NodeMonitor`        |
| Methods and functions | `UpperCamelCase`  | `StartWatching()`    |
| Private methods       | `_UpperCamelCase` | `_ProbeCursor()`     |
| Member variables      | `fUpperCamelCase` | `fListenerCount`     |
| Static members        | `sUpperCamelCase` | `sVBlankUnsupported` |
| Globals               | `gUpperCamelCase` | `gAppServerPort`     |
| Local variables       | `lowerCamelCase`  | `nodeRef`            |
| Parameters            | `lowerCamelCase`  | `maxCount`           |
| Constants             | `kUpperCamelCase` | `kMaxListeners`      |
| Macros                | `ALL_CAPS`        | `B_ENTRY_CREATED`    |

Do not use `myMethod` or `my_method` for public Haiku API.

## Pointers and references

Attach `*` and `&` to the type, not the variable:

```cpp
BRect*     rect;        // RIGHT
BRect *rect;            // WRONG (C style)
const BRect& frame;     // RIGHT
```

## NULL vs nullptr

Use `NULL` for pointer literals. This codebase predates `nullptr` adoption. Match surrounding code.

## Error handling

Functions that can fail return `status_t`. Do not use C++ exceptions anywhere.

```cpp
status_t
MyClass::Init()
{
    fData = new(std::nothrow) DataBuffer();
    if (fData == NULL)
        return B_NO_MEMORY;

    return B_OK;
}
```

Objects that can fail to initialize expose an `InitCheck()` method returning `status_t`. Callers must check it before using the object. Never silently ignore error return values.

Common error codes:

| Code            | Meaning                 |
| --------------- | ----------------------- |
| `B_OK`          | success                 |
| `B_ERROR`       | generic failure         |
| `B_NO_MEMORY`   | allocation failed       |
| `B_BAD_VALUE`   | invalid argument        |
| `B_NO_INIT`     | object not initialised  |
| `B_UNSUPPORTED` | feature not implemented |
| `B_TIMED_OUT`   | wait timeout            |
| `B_INTERRUPTED` | EINTR-equivalent        |

Early-return on failure rather than nesting `if (success) { ... }`.

## Lock/Unlock pattern

BeOS uses explicit `Lock()`/`Unlock()` rather than RAII guards:

```cpp
if (!fFloatingOverlaysLock.Lock())
    return;

// ... critical section ...

fFloatingOverlaysLock.Unlock();
```

Use `BAutolock` or scoped helpers only when the existing surrounding code does.

## Classes

- Destructors of non-final base classes must be `virtual`.
- Prefer `std::nothrow` `new` over plain `new`. Check for `NULL` instead of catching `std::bad_alloc`.
- Keep the public interface minimal. Implementation details are private.

## Comments

Explain **why**, not what. Terse, 1-3 lines is the norm.

```cpp
// RIGHT: explains the constraint
// VMOVNTDQ requires 32-byte aligned dst; fall through to memcpy otherwise.

// WRONG: restates the code
// Loop over the rows and copy each row.
```

Use `//` for single-line comments. Use `/* */` for license headers and the occasional block comment in C files.

## Unused parameters

Drop the name, don't `(void)` cast:

```cpp
// RIGHT
status_t
foo(uint32_t fb_id, const BRect* /*dirty_rects*/, uint32_t /*nrects*/)
{
    ...
}

// AVOID
status_t
foo(uint32_t fb_id, const BRect* dirty_rects, uint32_t nrects)
{
    (void)dirty_rects;
    (void)nrects;
    ...
}
```

## Include order

Blank line between groups:

```cpp
#include "MyClass.h"           // 1. own header

#include <stdio.h>             // 2. C system
#include <unistd.h>

#include <new>                 // 3. C++ system
#include <atomic>

#include <Locker.h>            // 4. project public (BeOS API)
#include <OS.h>

#include "HelperPrivate.h"     // 5. project private
```

Header guard: `_FOO_H` (single underscore, all-caps):

```cpp
#ifndef _DRM_INTERFACE_H
#define _DRM_INTERFACE_H
...
#endif // _DRM_INTERFACE_H
```

## File headers

All new files must include the appropriate license header. Kernel and system components use MIT or GPL headers; Tracker and Deskbar code uses the Open Tracker License. When in doubt, ask on [Telegram](https://t.me/vitruvian_official_chat).

Don't add author lines yourself; those are added by people taking real ownership of the code.

## General

- No trailing whitespace.
- Two blank lines between top-level definitions.
- One blank line between method definitions.
- Avoid deeply nested logic. Early returns keep code flat and readable.
- Keep functions short and focused. If a function needs a comment explaining its sections, it should probably be split.
