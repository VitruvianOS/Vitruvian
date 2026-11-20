# NetPlus GM bridge test

A userscript and a page for checking the NetPlus `GM.*` bridge by hand:
that GM userscripts can use it, and that a web page's own scripts cannot.
Nothing here is built; the files are copied into a running system.

| File | What it is |
|---|---|
| `gm-bridge-test/` | A userscript extension that calls `GM.setValue`, `GM.getValue` and `GM.xmlHttpRequest` and shows each result in a yellow bar at the top of the page |
| `hostile.html` | A page whose own script looks for the bridge (`window.webkit.messageHandlers.netplusGM`) and the GM prelude (`window.__netplusGM`), and tries to read a local file through the bridge if it finds it |

The userscript matches `https://example.com/*` and `http://127.0.0.1/*`
(match patterns name the host only; the port is not part of them), so
it also runs on `hostile.html` when that is served from 127.0.0.1. That is
the case that matters: a page a GM script runs on.

The bar reports fetches by status and length only, never their content, so
a screenshot of it shows nothing about the tester's network.

## Steps

On a VitruvianOS system with NetPlus (a live session is fine), as the
desktop user:

1. Install the extension:

   ```sh
   mkdir -p ~/config/settings/NetPlus/extensions
   cp -r gm-bridge-test ~/config/settings/NetPlus/extensions/
   ```

2. Serve the page from loopback, in a Terminal:

   ```sh
   cd /path/to/netplus-gm && python3 -m http.server 8766 --bind 127.0.0.1
   ```

3. Quit NetPlus if it runs (extensions load at startup), then open
   `http://127.0.0.1:8766/hostile.html` in it.

## Expected result

Page text (the page's own script):

    no handler, no prelude

Yellow bar (the GM userscript):

| Field | Expected | Why |
|---|---|---|
| `set` | `ok` | GM storage works |
| `get` | `stored` | and reads back |
| `file` | `scheme not allowed` | only http and https are fetched |
| `loopback` | `local address not allowed` | 127.0.0.0/8 is refused |
| `lan` | `local address not allowed` | 192.168.0.0/16 is refused |
| `web` | `200 (N bytes)` | a public site is fetched |

`web` shows `failed, status 0` on a network without IPv6 when the site
publishes an IPv6 address (QEMU user networking, for one): the Services Kit
HTTP request connects to the first resolved address only and does not fall
back to IPv4. That is outside the GM bridge; an IPv4-only host shows 200.

## What the unfixed code shows

Page text: `EXPOSED: handler=object prelude=undefined`. The bar shows
`threw=ReferenceError: Can't find variable: GM`, and NetPlus's stderr has

    signal 'script-message-received-with-reply::netplusGM' is invalid for
    instance ... of type 'WebKitUserContentManager'

## In the headless debug VM

`docs/agents/runtime-debugging.md` describes the VM. With it running, copy
the files in over QEMU's host address (10.0.2.2) from a host-side
`python3 -m http.server 8766 --bind 127.0.0.1` in this directory, serve
`hostile.html` inside the guest as above, and start NetPlus as the session
user with Tracker's environment:

```sh
T=$(pgrep -x Tracker); xargs -0 -a /proc/$T/environ sh -c \
  'exec setpriv --reuid=995 --regid=995 --init-groups env -i "$@" /system/apps/NetPlus http://127.0.0.1:8766/hostile.html' x &
```

Then `docker/debug/vmctl.py shot FILE.png`.
