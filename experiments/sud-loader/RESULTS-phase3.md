# In-process /nix interception on Android: phase 3 (issue #1)

Phase 2 ([RESULTS.md](RESULTS.md)) ran PIE `/nix/store` binaries without
proot, but stopped at non-PIE targets, final-component-only symlink
resolution and partial syscall coverage. Phase 3 does the three recommended
next steps in place, in `loader.c`. The result: gcc compiles natively, and a
real `nix-build` that compiles and runs C finishes end to end under the
launcher.

Device and setup are the same as in phase 2: Pixel 8a, Android 16, kernel
6.1.145, app `us.lassul.droidnix` (`untrusted_app_27`), date 2026-10-26.
Native runs used the failsafe session (`Seccomp_filters 1`, `TracerPid 0`).
proot runs used the normal droid-nix shell (`Seccomp_filters 2`, traced by
proot).

## What changed

| change | how |
| --- | --- |
| non-PIE (`ET_EXEC`) targets | The loader is linked at 0x1000000000 (64 GiB, `-Ttext-segment`), far above the ET_EXEC range and below the 39-bit mmap area. ET_EXEC images are mapped at their own vaddr with `MAP_FIXED_NOREPLACE`, which fails with a clear error instead of overwriting anything. AT_PHDR comes from PT_PHDR or the PT_LOAD that holds it. The ET_EXEC rejection is gone. |
| path canonicalisation | One resolver, `canon()`, replaces `translate()`/`xlate()`. It walks every component in the guest view, starting at `/` or the guest path of the dirfd/cwd. Each component the kernel would follow is checked with readlink. A symlink's target is spliced in front of the rest, and an absolute target restarts at `/`, so `/nix/...` targets are re-translated wherever they appear. `..` pops a component of the already-resolved path, so `/nix/..` is `/`, not the prefix. A trailing `/` and a final `.` keep their meaning. A missing intermediate component fails with the kernel's errno. A missing final one is passed on (O_CREAT, mkdir). This is proot's approach. |
| relative fast path | A relative path with no `..` and no symlink among the followed components resolves the same in both views. It goes to the kernel unchanged against the real dirfd. A single component with NOFOLLOW (what fts in find/du/tar does) costs no extra syscall. |
| `/proc` magic links | `/proc/<pid>/fd/N`, `cwd` etc. name kernel objects (pipes, sockets, deleted files), not paths. At the first symlink under `/proc`, the rest of the path is left to the kernel. |
| syscall coverage | All path-taking syscalls of the aarch64 table are handled, each with the right dirfd and follow rule (table below), including both paths of linkat/renameat(2)/move_mount and the linkpath of symlinkat. |
| execve errors | Before re-exec'ing itself, the handler checks what the kernel would return: canonical path, X_OK, regular file, ELF or `#!` (up to the kernel's 5 interpreter levels). So execvp's PATH search gets ENOENT/EACCES/ENOEXEC back instead of a re-exec'd loader dying with 127. |
| execveat | Dirfd-relative and AT_EMPTY_PATH execveat are turned into an absolute guest path. AT_SYMLINK_NOFOLLOW gives ELOOP. |
| re-exec marker | The re-exec passes `--reexec` in argv instead of the `SUD_LOADER_ACTIVE` env var. nix clears the builder's environment, so the env marker would have stacked a second filter. The target environment is no longer touched. |
| `#!` argv | No fixed 96-argument array any more. The argv is built on the stack with room for the interpreter levels. |
| clone3 → ENOSYS | Found while running gcc (see below). |
| termios2 ioctls | Found while running nix-build (see below). |

### Path syscalls (aarch64 asm-generic table)

| syscalls | path args | final component followed |
| --- | --- | --- |
| openat, openat2 | dirfd+path | unless O_NOFOLLOW or O_CREAT\|O_EXCL |
| newfstatat, utimensat, statx, faccessat2, fchmodat2, fchownat, open_tree, open_tree_attr, mount_setattr, setxattrat, getxattrat, listxattrat, removexattrat, file_getattr, file_setattr | dirfd+path | unless AT_SYMLINK_NOFOLLOW |
| faccessat, fchmodat | dirfd+path | yes |
| mkdirat, mknodat, unlinkat, readlinkat | dirfd+path | no |
| name_to_handle_at | dirfd+path | only with AT_SYMLINK_FOLLOW |
| fspick | dirfd+path | unless FSPICK_SYMLINK_NOFOLLOW |
| linkat | both dirfd+path pairs | old only with AT_SYMLINK_FOLLOW, new no |
| renameat, renameat2 | both dirfd+path pairs | no |
| move_mount | both dirfd+path pairs | per MOVE_MOUNT_F/T_SYMLINKS |
| symlinkat | newdirfd+linkpath (target is literal content) | no |
| chdir, chroot, truncate, statfs, acct, swapon, swapoff, getxattr, setxattr, listxattr, removexattr | path | yes |
| lgetxattr, lsetxattr, llistxattr, lremovexattr | path | no |
| umount2 | path | unless UMOUNT_NOFOLLOW |
| pivot_root | both paths | yes |
| mount | target; source only for MS_BIND/MS_MOVE | yes |
| quotactl | special; addr for Q_QUOTAON | yes |
| fsconfig | value+aux for FSCONFIG_SET_PATH(_EMPTY) | yes |
| inotify_add_watch | path | unless IN_DONT_FOLLOW |
| fanotify_mark | dirfd+path | unless FAN_MARK_DONT_FOLLOW |
| bind, connect | AF_UNIX `sun_path` (not abstract) | bind no, connect yes |
| execve, execveat | path / dirfd+path | yes (unless AT_SYMLINK_NOFOLLOW) |
| getcwd, readlinkat | results reverse-translated | |

Not handled: unix datagram destinations in sendto/sendmsg/sendmmsg (trapping
every send for a case nix does not use), paths inside `bpf()` attrs (bpffs,
not available to apps), and the removed, privileged `lookup_dcookie`.
openat2 with `RESOLVE_*` flags is translated lexically only, so the kernel can
enforce the flags. A `/nix`-absolute symlink inside such a path is not
re-translated.

## Two more properties of the app domain

Like `set_robust_list`/`rseq` in phase 2, these only showed up on real
workloads, and any in-process runtime here has to handle them.

* **clone3 cannot be supported in-process.** gcc's driver starts cc1 with
  glibc's `posix_spawn`, which uses
  `clone3(CLONE_VM|CLONE_VFORK|CLONE_CLEAR_SIGHAND)`. `CLONE_CLEAR_SIGHAND`
  resets our SIGSYS handler in the child, so its first trapped syscall killed
  it ("internal compiler error: Bad system call signal terminated program
  cc1"; found with a static strace). A clone3 with a new stack also cannot be
  reissued from inside the handler, because the child would return into
  handler frames on an empty stack. The handler fails clone3 with ENOSYS, as
  common seccomp sandboxes (Docker, systemd, Chrome) do. glibc then falls back
  to clone(), which is not trapped, runs at the call site and keeps the
  handler. pthread_create takes the same fallback.
* **Android's app SELinux policy denies the termios2 ioctls.** glibc ≥ 2.42
  issues `TCGETS2`/`TCSETS2`/…, but `untrusted_app` may only use the classic
  `TCGETS`/`TCSETS`/`TCSETSW`/`TCFLSH` on ttys. nix's builder pty failed with
  "getting pseudoterminal attributes: Permission denied". droid-nix's proot
  rewrites these ioctls (its `__ANDROID__` path in `syscall/enter.c`), which
  is why nix works under proot. The filter now traps `ioctl` only for those
  five cmds (it checks the cmd argument, so every other ioctl stays
  untrapped). The handler issues the classic form and fills termios2's speed
  fields from the CBAUD bits. TCSETSF(2) becomes TCFLSH(TCIFLUSH) + TCSETSW.

Hard links are denied to apps outright (EACCES, the reason proot runs with
`--link2symlink`). The launcher reports the same EACCES. It does not emulate
link2symlink.

## Correctness (launcher, failsafe/native session)

`run-native.sh`. Every case gives the expected result:

| case | result |
| --- | --- |
| phase 2 set: hello, bash pipeline + subshell, `ls /nix/store`, `ls -l`, `nix --version`, `nix-store --verify-path` glibc, `readlink /proc/self/exe`, `cd` + `pwd` | all as in phase 2, exit 0 |
| real gcc driver (ET_EXEC) `--version` | `gcc (GCC) 16.2.0`, exit 0 |
| gcc-wrapper compile + link of a C file (driver, cc1, as, collect2, ld) | exit 0 |
| run the compiled program | `hello from gcc 42`, exit 42 (as written) |
| host path with an interior `/nix` symlink: `ls -d ~/.nix-profile/bin` | resolves, exit 0 |
| interior chain: `cat /nix/var/nix/profiles/droid-nix/etc/nix/nix.conf` (profile → -1-link → store → etc → store) | prints nix.conf |
| `ls /nix/var/nix/profiles/droid-nix/etc/..` (`..` after an absolute `/nix` symlink) | `etc`, the contents of the `-etc` store dir (lexical would list the generation) |
| `cd -P …/droid-nix/etc/..; pwd` | `/nix/store/mq71…-etc` |
| `ls -d /nix/../system`; `cd /nix/..; pwd`; `cd /nix; ls -d ../system` | exists; `/`; exists (`..` above /nix is the guest root) |
| relative paths through symlinks from a `/nix` cwd | resolve |
| `find -L` over the profile (dirfd-relative fstatat/openat through symlinks) | finds both `etc` dirs |
| `env PATH=/nonexistent:$CO true` (execvp skips a missing dir) | exit 0 (phase 2 would die with 127) |
| bash runs a missing `/nix/store/...` path | bash reports "No such file or directory", `$?` 127 |
| mkdir -p, `ln -s` to `/nix/store/...`, follow it, readlink, `mv` into a subdir, follow again, rm, rmdir, `rm -r` | all work |
| `ln` (hard link) | EACCES, same as toybox `ln` without the loader; a missing source gives ENOENT |

## Macro benchmarks: launcher vs proot

Same workloads, files and timing (`date +%s%N`), 3 runs each, median.
verify/find/du/tar are over the `glibc-2.44-57` store path as in phase 2.
`gcc` is the gcc-wrapper compiling and linking a 2-line C file (`-O2`).
`nix-build` builds `build.nix` (bash builder, gcc-wrapper compiles C, the
builder runs the result) with a fresh nonce each run and
`--option substitute false`, so every run really builds.

| workload | launcher (ms) | proot (ms) | speedup |
| --- | ---: | ---: | ---: |
| `nix-store --verify-path` | 195 | 1023 | 5.2× |
| `find` over the subtree | 53 | 381 | 7.2× |
| `du -s` | 67 | 1329 | 20× |
| `tar cf /dev/null` | 66 | 1458 | 22× |
| small gcc compile + link | 433 | 1484 | 3.4× |
| **`nix-build` of a C derivation** | **654** | **2559** | **3.9×** |

Raw runs (ms):

* launcher: verify 192/195/195, find 56/53/53, du 69/67/59, tar 69/66/60, gcc 433/425/435
* launcher nix-build: 615/654/659
* proot: verify 909/1058/1023, find 406/381/371, du 919/1497/1329, tar 1458/1401/1616, gcc 1534/1484/1414
* proot nix-build: 2615/2458/2559 (an earlier proot session gave 2565/2580/2548)

**No regression against phase 2.** The launcher medians were verify 180,
find 58, du 63, tar 63. find/du/tar are unchanged within run-to-run noise:
they use dirfd-relative single components, which take the fast path. verify
is about 8% slower (195 vs 180; an earlier run this session gave
179/192/183). It opens absolute store paths, and these now cost one readlink
per component, which is the price of resolving interior symlinks.

The native nix-build needs two environment settings that the proot shell gets
from its binds: `NIX_CONF_DIR` pointing at the droid-nix `-etc` store path
(no `/etc` bind), and `TMPDIR=$usr/tmp` (the failsafe's `/data/local/tmp` is
not writable by the app). Without `/etc/resolv.conf` there is also no DNS,
which is why substitution is off in both runs. The launcher translates only
`/nix`, so these are setup for the experiment, not launcher behaviour.

## Limitations (current)

Phase 4 fixed "only `/nix` is translated": the launcher has proot's binds
and the native nix-build needs no `NIX_CONF_DIR`/`TMPDIR` (see
[RESULTS-phase4.md](RESULTS-phase4.md)). The rest still holds:

* **clone3 always fails with ENOSYS.** libcs fall back to clone(). A program
  that uses clone3 without a fallback would fail.
* **A program that installs its own SIGSYS handler, or blocks SIGSYS by means
  we do not trap**, would break. That includes `sa_mask` of another handler,
  sigsuspend/ppoll/pselect6/epoll_pwait masks, and a sigreturn with an
  edited mask. This is the fundamental trade-off of in-process interception
  (as in phase 2).
* **Hard links are EACCES** (Android policy). There is no link2symlink
  emulation as in proot.
* Not handled: unix datagram sends to a path, bpffs paths, lookup_dcookie.
  openat2 with RESOLVE_* is translated lexically only.
* `/proc/self/exe` is answered for readlinkat only. Opening it gives the
  loader, and `/proc/<pid>/exe` of other processes shows the loader too.
  `/proc/self/maps` shows host paths.
* The loader's image sits at a fixed 0x1000000000. A program that maps
  something there with MAP_FIXED would overwrite it. gcc's PCH code uses that
  address only as a hint, which the kernel then moves.
* termios2 arbitrary (BOTHER) speeds are not carried over by the ioctl
  rewrite (irrelevant for ptys).
* A directory fd whose directory has been deleted resolves to its
  `/proc/self/fd` name ("… (deleted)") on the slow path.

## What this means for issue #1

* **The launcher now runs the full toolchain and nix's build loop with no
  proot.** That covers non-PIE gcc, cc1, as and ld, glibc posix_spawn, nix's
  builder fork and env clearing, its pty and its seccomp filter. A real
  `nix-build` that compiles and runs C completes **3.9× faster than proot**
  (654 vs 2559 ms), gcc alone is 3.4× faster, and store traversals stay
  5–22× faster.
* **Correct path semantics cost little.** The proot-style per-component walk
  only runs where a symlink or `..` could change the answer. The common
  dirfd-relative case stays a single trapped syscall. Absolute-path-heavy
  verify got about 8% slower.
* **The app domain keeps adding requirements.** phase 2 found trapped
  set_robust_list/rseq. Phase 3 found clone3's CLONE_CLEAR_SIGHAND and the
  SELinux-denied termios2 ioctls. All of them are emulations proot already
  does or has to do. Any replacement runtime has to carry this
  Android-specific list, and it should grow with tests on real workloads,
  not by guessing.

## Recommended next step

1. Give the launcher the rest of proot's binds (`/etc`, `/tmp`, `/bin`,
   `/usr`, `/dev/shm`) as a small prefix table in the same `canon()`, so the
   native session needs no `NIX_CONF_DIR`/`TMPDIR` setup and has DNS.
2. Run a larger real build under the launcher (a stdenv `mkDerivation` with
   configure/make, e.g. `hello` from source with the local store) to find
   the next app-domain properties. Decide on link2symlink emulation from what
   breaks.
3. Then try the launcher as droid-nix's login path behind a switch, with
   proot kept as the fallback.
