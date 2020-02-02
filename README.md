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
#### How to start
In order to start you need to run the following commands from the generated.x64 folder:

```bash
./src/apps/testharness/clean_shm.sh
./generated.x64/src/servers/registrar/registrar &
./generated.x64/src/servers/app/app_server &
./generated.x64/src/servers/input/input_server.out & 
./generated.x64/src/apps/deskbar/Deskbar &
```

#### FAQ 

1. ** app_server quit with the following message: could not inizialize font manager **

This means that  the app_server can't find the fonts. If you have them make sure to copy the fonts folder in /os/system/data

2. ** I tried to start it but when i launch it i just get the background and it quit immediately after **

That is normal, and even if it looks like that it has quitted, it is not true, and is running correctly.

#### Contributing

Contributing to the project is open to anyone, feel free to send a pull request.

[Master Development Ticket](https://github.com/Barrett17/V-OS/issues/1)
