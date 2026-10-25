<p align="center">
  <img src="https://v-os.dev/img/vitruvianos-logo.svg" width="220" alt="VitruvianOS">
</p>
<img src="https://numerio.goatcounter.com/count?p=/vitruvian-readme" alt="" width="0" height="0">

**Vitruvian** (VitruvianOS, or just **V\OS**) is an operating system that brings the simplicity of a modern BeOS inspired system and the power of Linux to modern hardware.

#### Goals

* **Fast, reactive, and easy-to-use interface** — Minimal latency from input to response, intuitive navigation, no bloat.
* **Highly integrated Desktop Environment** — The desktop, applications, and system services work as a coherent whole.

---

<img width="1646" height="951" alt="The V\OS desktop with Tracker, MediaPlayer, NetPlus and the Network preflet" src=".github/screenshot.jpg" />

### Overview

V runs on Linux and takes its look and its programming model from BeOS. You get Linux hardware support with a desktop that stays responsive. BeOS/Haiku applications build and run with few or no source changes.

We ship custom kernel modules and, on amd64, a PREEMPT_RT kernel to keep latency low. The regular kernel works too.

Images boot from ext4, and the live ISO uses SquashFS. XFS and Btrfs also work.

Release images are built for amd64 and arm64, and the arm64 ones include the Raspberry Pi 4 and 5. There's a riscv64 port in the nightly builds. See the [boards site](https://boards.v-os.dev/) for the full list.

### Nexus

Nexus is a set of Linux kernel modules that give V its BeOS-style primitives, ports and node monitoring among them. It's a submodule at `src/system/kernel/nexus` and ships as the `nexus-dkms` package. More in the [Nexus repository](https://github.com/VitruvianOS/Nexus).

### Talk to the people who use it

* [Discourse](https://discourse.v-os.dev/), the forum
* [Telegram chat](https://t.me/vitruvian_official_chat), plus a [channel](https://t.me/vitruvian_official) for announcements
* [GitHub issues](https://github.com/VitruvianOS/Vitruvian/issues) for bugs
* The [mailing list](https://www.freelists.org/list/vitruvian), if you like email

### Installation

Grab an image from [download.v-os.dev](https://download.v-os.dev/), write it to a USB stick and boot it. The [installation guide](https://docs.v-os.dev/docs/installation/) has the details. If you'd rather not touch real hardware yet, [run it in a VM](https://docs.v-os.dev/docs/getting-started/virtualization/).

### Getting started

* [Building from source](https://docs.v-os.dev/docs/getting-started/building/)
* [Coding guidelines](https://docs.v-os.dev/docs/development/coding-guidelines/)
* [Filesystem layout](https://docs.v-os.dev/docs/development/filesystem-layout/)
* [All the docs](https://docs.v-os.dev/)
* [Packages](https://packages.v-os.dev/)

Pull requests are welcome. Have a look at the coding guidelines first.

### Donate

V\OS is built by volunteers. If you'd like to chip in, there's a [Donate](https://v-os.dev/donate/) page.

### License

VitruvianOS is released under a hybrid [GPL](https://www.gnu.org/licenses/gpl-3.0.html) / [MIT](https://opensource.org/licenses/MIT) license.
