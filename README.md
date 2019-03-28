## V\OS README

V\OS is a modern Operating System inspired by BeOS.
The V\OS goal is to provide a modern user experience on top of recent hardware. Targeted at Desktops and Mobile devices, aims to provide a sane and powerful user experience free from unneeded complexities.

#### Video backends

There's no app_server backend at this moment.

Planned to be supported:
* Fbdev and drm framebuffer
* Possible Wayland backend

#### Building V\OS

From the sources directory:

mkdir build/
cd build/
cmake -GNinja ..
ninja -j2

#### Contributing

Contributing to the operating system is open to anyone, feel free to send a pull request.

[Master Development Ticket](https://github.com/Barrett17/V-OS/issues/1)
