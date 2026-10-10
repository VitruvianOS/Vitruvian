# Filesystem Layout {#filesystem-layout}

Vitruvian runs on a standard Linux filesystem hierarchy but maps BeOS/Haiku path conventions onto it so that application source code written for BeOS or Haiku works without path changes.

## Boot filesystem

The default boot filesystem is **ext4** (raw disk images) and **SquashFS** (live images). Both support Linux extended attributes, which are required for the BeOS attribute store.

Modern ext4 has grown into a good fit for our metadata needs. The `ea_inode` feature lets a single extended attribute occupy an entire inode instead of the file inode's spare space, so large attrs — embedded icons, MIME hints, cached metadata — do not spill or fragment. Combined with `large_dir` and `metadata_csum`, ext4 covers the whole BFS attribute store without special tuning. It also shrinks and grows, which matters for constrained boards and dual-boot resizes.

Full support for **XFS** and **Btrfs** is on the roadmap, tied to the DriveSetup rewrite. Both will land as first-class install targets alongside ext4.

## Path mapping

BeOS and Haiku expose their filesystem tree under `/boot`. Vitruvian maps `/boot` to the Linux root `/`:

| BeOS/Haiku path | Vitruvian path |
| --------------- | -------------- |
| `/boot`         | `/`            |
| `/boot/home`    | `/home`        |
| `/boot/system`  | `/system`      |

`/system/home` is a symlink to `/home`, so code that constructs paths via either convention works.

Standard Linux paths (`/usr`, `/lib`, `/etc`, `/proc`, `/dev`) coexist and are used by the Debian-derived userland and `apt` as usual.

## Extended attributes

BeOS relies on filesystem extended attributes for file metadata, MIME types, application signatures, and the attribute database that powers live queries. Vitruvian stores these as Linux xattrs in the `user.` namespace:

- Attribute data: `user.<name>`
- Attribute type tag (Haiku `uint32`): `user.<name>.type`

Access is via `fgetxattr`, `fsetxattr`, `fremovexattr`, `flistxattr` on the node's open fd. `BNode::ReadAttr`, `WriteAttr`, `GetAttrInfo` wrap these. Attribute iteration (`BNode::GetNextAttr`) walks `flistxattr` output and strips the `user.` prefix.

More detail on individual directories and the attribute namespace is coming.
