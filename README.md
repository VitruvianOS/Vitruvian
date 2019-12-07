## V\OS README

Vitruvian is a modern operating system written in C++.

V\OS aims to provide a user experience that is free from unneeded complexities
while still retaining the power of the linux kernel.

#### Video backends

* Support for linux framebuffer.

#### Pre-requisite software

* gcc >= 8
* ninja
* cmake >= 3.13
* libinput >= 1.10

```
sudo apt install cmake ninja-build libfreetype6-dev libinput-dev git autoconf automake texinfo flex bison build-essential unzip zip less zlib1g-dev libtool mtools gcc-multilib
```

#### Getting the source code

git clone https://github.com/Barrett17/V-OS.git

#### Building V\OS

After cloning the repo, open a Terminal and enter in the sources directory.

```
mkdir generated.x86/
cd generated.x86/
../configure
ninja -j2
```

#### Contributing

Contributing to the project is open to anyone, feel free to send a pull request.

[Master Development Ticket](https://github.com/Barrett17/V-OS/issues/1)
