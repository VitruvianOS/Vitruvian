# Virtualization {#virtualization}

QEMU with KVM gives near-native performance on Linux hosts and is the primary tested environment, though anything that supports QEMU should work.

## QEMU Install with KVM

### Linux

```bash
sudo apt install qemu-system-x86 qemu-kvmi
kvm-ok # Check that KVM is available
```

If KVM is not available, QEMU will still work but will run slower without hardware acceleration.

### macOS

```bash
brew install qemu
```

### Windows

- Install QEMU from <https://www.qemu.org/download/>
- Add QEMU to your PATH
  - Press the **Windows Key**, type `env`, and select **Edit the system environment variables**.
  - Click on the **Environment Variables...** button at the bottom right.
  - In the **System variables** section (bottom list), find and select the variable named **Path**, then click **Edit...**.
  - Click New and paste the default installation path for QEMU: `C:\Program Files\qemu`
  - Click **OK** on all windows to save and apply the changes.

## Running QEMU

### Boot a raw image

```bash
qemu-system-x86_64 \
  -cdrom vitruvian.img -boot menu=on \
  -m 8G -cpu host -smp sockets=1,cores=2,threads=2 --enable-kvm \
  -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
  -device virtio-net-pci,netdev=mynet \
  -audiodev id=snd0,driver=pa -device ich9-intel-hda  \
  -device hda-output,audiodev=snd0
```

Adjust `-m` (RAM in MB) and `-smp` (CPU cores) to your host. Remove `-enable-kvm` if KVM is unavailable.

### Boot an ISO

```bash
qemu-system-x86_64 \
  -cdrom vitruvian.iso -boot menu=on \
  -m 8G -cpu host -smp sockets=1,cores=2,threads=2 --enable-kvm \
  -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
  -device virtio-net-pci,netdev=mynet \
  -audiodev id=snd0,driver=pa -device ich9-intel-hda  \
  -device hda-output,audiodev=snd0
```

or on Windows

### Boot a raw image

```powershell
qemu-system-x86_64.exe `
-accel whpx,kernel-irqchip=on `
-drive file=vitruvian.img,format=raw,if=virtio `
-boot menu=on `
-m 8G -cpu host -smp sockets=1,cores=2,threads=2 `
-netdev user,id=mynet,hostfwd=tcp::2222-:22 `
-device virtio-net-pci,netdev=mynet `
-audiodev id=snd0,driver=dsound `
-device ich9-intel-hda -device hda-output,audiodev=snd0

```

### Boot an ISO

```powershell
qemu-system-x86_64.exe `
-accel whpx,kernel-irqchip=on `
-drive file=vitruvian.iso,format=raw,if=virtio `
-boot menu=on `
-m 8G -cpu host -smp sockets=1,cores=2,threads=2 `
-netdev user,id=mynet,hostfwd=tcp::2222-:22 `
-device virtio-net-pci,netdev=mynet `
-audiodev id=snd0,driver=dsound `
-device ich9-intel-hda -device hda-output,audiodev=snd0

```

## Useful flags

| Flag                 | Purpose                                                      |
| -------------------- | ------------------------------------------------------------ |
| `-display gtk`       | Use a GTK window instead of SDL                              |
| `-display spice-app` | Use SPICE for better clipboard and display integration       |
| `-cpu host`          | Expose host CPU features to the guest (better compatibility) |
| `-snapshot`          | Run without writing changes back to the image                |
| `-serial stdio`      | Print serial output to the terminal                          |
