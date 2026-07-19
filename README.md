<p align="center">
  <img src="https://v-os.dev/img/vitruvianos-logo.svg" width="220" alt="VitruvianOS">
</p>
<img src="https://numerio.goatcounter.com/count?p=/vitruvian-readme" alt="" width="0" height="0">

**Vitruvian** (**VitruvianOS** or simply **V\OS**) is the human-centric Operating System.

#### Goals

* **Fast, reactive, and easy-to-use interface** — Minimal latency from input to response, intuitive navigation, no bloat.
* **Highly integrated Desktop Environment** — The desktop, applications, and system services work as a coherent whole.

---

<img width="1284" height="804" alt="0 4 1-thumbnail" src="https://github.com/user-attachments/assets/3eb7a173-ef91-440a-9d0d-de194283fd89" />

### Overview

Vitruvian is an operating system based on Linux, heavily inspired by BeOS, bringing the elegance and simplicity of a classic operating system to modern hardware. It leverages Linux's hardware support while maintaining the responsive, user-friendly nature of BeOS.

Custom-built kernel modules and real-time patches deliver a low-latency desktop experience. Vitruvian supports the BeOS/Haiku API on Linux with minimal to no changes required to application source code.

The default boot filesystem is ext4, with SquashFS for live images. Both support Linux extended attributes, which Vitruvian uses to carry BFS-style metadata. Full support for XFS and Btrfs is on the roadmap, tied to the DriveSetup rewrite. The default kernel ships with PREEMPT_RT real-time patches; non-RT kernels are also supported. Filesystem indexing and live queries are planned for a future release.

### Nexus

Nexus is the Vitruvian kernel subsystem that bridges Linux with the BeOS/Haiku runtime, implemented as a set of custom Linux kernel modules. It is included as a submodule at `src/system/kernel/nexus` and distributed as the `nexus-dkms` package.

See the [Nexus repository](https://github.com/VitruvianOS/Nexus) for more detail.

### Join the Community

#### Telegram
- **Discussions**: https://t.me/vitruvian_official_chat
- **Updates**: https://t.me/vitruvian_official

#### Mailing list
https://www.freelists.org/list/vitruvian

### Installation

See https://v-os.dev/download/ and https://wiki.v-os.dev/docs/getting-started/how-to-install/

### Getting Started

* [Building](https://wiki.v-os.dev/docs/getting-started/building/)
* [Coding Guidelines](https://wiki.v-os.dev/docs/development/coding-guidelines/)
* [Filesystem Layout](https://wiki.v-os.dev/docs/development/filesystem-layout/)
* [Full Wiki](https://wiki.v-os.dev/)

We welcome contributions from the community. Check the wiki for guidelines and open issues on GitHub.

### Donate

If you'd like to support the project, see the [Donate](https://wiki.v-os.dev/docs/reference/donate/) page.

### License

VitruvianOS is released under a hybrid [GPL](https://www.gnu.org/licenses/gpl-3.0.html) / [MIT](https://opensource.org/licenses/MIT) license scheme.
