# 縁 enishi

A stacking filesystem for Linux that puts several disks under one mount point.

> **Status: work in progress.** The kernel module builds its superblock
> and has a complete in-memory alias table, but no file operation is wired up
> yet. Nothing here is usable for real data. See [Status](#status).

## Why

`link(2)` does not work across filesystems: it fails with `EXDEV`, and tools
that wanted a hard link quietly fall back to copying. On a home media server
this is an everyday problem. Downloads land on one disk, the library lives on
another, and every file that should exist once ends up stored twice.
Especially when you are unable to move all your data into a resource pool (like ZFS).

enishifs (`縁`, *enishi*: a tie between things) joins the disks into a single
filesystem. Each disk keeps its own format and its own data, enishifs has no
on-disk format at all. What it adds is one namespace on top, in which a name on
one disk can stand for a file on another.

## Concepts

**岸 kishi** ("shore") is an underlying filesystem attached to enishifs.

**結び musubi** ("knot") is a path alias (not a symbolic link; all operation to one path will be pass through to the destination path, even 'rm')

- A musubi covers everything below it (recursive)
- When musubi are nested, the deepest one wins:

  ```
  a     -> X
  a/b/c -> Y
  a/b/z     resolves to   X/b/z
  a/b/c/d   resolves to   Y/d
  ```

- A musubi takes priority over whatever the kishi has at that path.
- Operations go through it: removing the file at `from` removes the target.
- It may dangle. If the target disappears the musubi stays, and the name comes
  back when the target does. Only removing the musubi itself drops the alias.

A mounted enishifs typically looks like this. The layout is chosen by
userspace, the kernel has no opinion about it:

```
/mnt/enishi
├── root/          the base kishi, e.g. an ext4 image
└── kishi/
    ├── nvme1/     one kishi per disk
    └── hdd0/
```

## Design

**Mechanism in the kernel, policy in userspace.** The kernel knows how to
attach a kishi and how to resolve a musubi, and nothing else. It never reads
or parses metadata from a disk. Userspace (libenishi and the CLI) owns the
storage format, hands the kishi and musubi down when mounting, and decides the
layout, naming and read-only policy.

**Saving is userspace's job too.** The musubi table lives in kernel memory.
Entries made at runtime, for example by `link(2)`, are queued in a *delta*
that userspace drains and writes to disk:

- The delta can only say "this was added". Reading an entry hands it over for
  good, the kernel does not track how far a reader got.
- Removing, changing or moving an entry that was already saved makes the delta
  invalid. Userspace then exports the whole table instead.

**Cross-disk hard links are built from the two primitives** (planned). Linking
to a file on another disk creates a real hard link on the disk that holds the
data, under a hidden `.enfs/<UUID>.hardlink`, plus a musubi from the new name
to it. `.enfs` is protected by the filesystem itself, independent of
permissions.

**Fail fast.** If a disk managed by enishifs goes away, the whole filesystem
fails until it is back. It never pretends that files are empty or missing.

Limits that are intentional: 64-bit only, and one supported kernel series
(6.12 LTS). VFS internals change with every release, the module follows this
one.

## Status

| Piece | State |
|---|---|
| Design protocol | Done |
| Musubi | Done |
| Basic FS Operations | WIP |
| Extra FS Operations | not started |
| Kishi Resource Pool | not started |
| ioctl interface | WIP |
| libenishi and CLI | WIP |
| Xnu Port | not started |

## Userspace interface

Not yet implemented. `enishi-kernel/enishifs.h` holds the current sketch of what
libenishi will expose:

| Call | Purpose |
|---|---|
| `KEnishiAddKishi`, `KEnishiAddMusubi` | hand a kishi or a musubi down to the kernel |
| `KEnishiHasDelta`, `KEnishiGetDeltaNext` | drain the entries made at runtime |
| `KEnishiExportAll` | read the whole table, the fallback when the delta is invalid |
| `KEnishiClearDelta` | declare the current table as saved |
| `KEnishiLock`, `KEnishiUnlock` | fail every operation, or resume |

The command line tool is planned along these lines, details will change:

```bash
enishi create enfs.img                            # metadata container
enishi pool add enfs.img base.img --base          # becomes root/
enishi pool add enfs.img /dev/nvme1 --mount nvme1 # becomes kishi/nvme1
enishi mount enfs.img /mnt/enishi

enishi musubi create -d nvme1 --src some/file.txt --dst a.txt
enishi metadata save /mnt/enishi
```

## AI Use
This project use AI to generate all the documentations. The author, lamadaemon, has manually reviewed and have made necessary changes. The author takes the full responsibility for any code signed by the author's PGP Keys (`BA8D23A037204A87`).

## License

This project supports [996.ICU](https://996.icu).

This project is licensed under the GPL, which is the only license that applies.

The author supports 996.ICU and hopes that everyone using this project will respect the spirit of the Anti-996 License and the rights of workers.

This is a request, not a requirement. Not following it has no consequences.