## V\OS README

V\OS is an Operating System inspired by Haiku.
The V\OS goal is to provide a modern user experience on top of recent hardware. Targeted at Desktops and Mobile devices, aims to provide a sane and powerful user experience free from unneeded complexities.

#### V\OS Distributions

The build system will be able to build standalone images including the whole system and kernel,
and binaries that can be used on Linux distributions to run the system as a multiplatform toolkit.

#### Video backends

Supported:
* XServer (to be eventually replaced by Wayland)
* DirectFB (planned to be deprecated in favor of the linuxfb driver)

Unsupported:
* Linux direct framebuffer
* Wayland

#### ABI ####

Applications ABI will remain unstable until R1. No binary compatibility with Haiku is planned at this stage.

#### Haiku compatibility

V\OS isn't an Haiku clone. Some parts of Haiku may not be supported due to the introduction
of newever replacements. If there is enough demand some compatibility may be built in future
to allow older applications to run.

#### Building V\OS

autoconf && ./configure && make

#### Contributing

Contributing to the operating system is open to anyone, feel free to send a pull request.

[Master Development Ticket](https://github.com/Barrett17/V-OS/issues/1)

#### Development and Porting rules

* The code must follow the Haiku Style Guidelines.
* When porting code from Haiku is mandatory to proceed reimplementing syscalls on top of linux.
* Proposals to extend the API are welcome and should be discussed in a ticket.
* Keep the history clean when possible and use git pull --rebase.
