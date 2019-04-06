## V\OS README

V\OS is a modern Operating System inspired by BeOS.
The V\OS goal is to provide a modern user experience on top of recent hardware. Targeted at Desktops and Mobile devices, aims to provide a sane and powerful user experience free from unneeded complexities.

#### Video backends

There's no app_server backend at this moment.

Planned to be supported:
* Fbdev and drm framebuffer
* Possible Wayland backend

#### Getting the source code

git clone https://github.com/Barrett17/V-OS.git

#### Building V\OS

After cloning the repo, open a Terminal and enter in the sources directory.

```
mkdir generated/
cd generated/
../configure
ninja -j2
```

#### Contributing

Contributing to the project is open to anyone, feel free to send a pull request.

[Master Development Ticket](https://github.com/Barrett17/V-OS/issues/1)
