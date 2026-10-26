# In-process /nix interception on Android: phase 2 launcher (issue #1)

Phase 1 showed that droid-nix's own processes can intercept their own path
syscalls in-process with a seccomp `SECCOMP_RET_TRAP` filter and a SIGSYS
handler, ~70–175× faster than proot per syscall, but that running real
`/nix/store` binaries without proot needs a userspace ELF loader. Phase 2 is
that loader: a minimal prototype that runs real PIE `/nix/store` binaries
natively in the app, with no proot, and compares them with proot on real
workloads.

Device: Pixel 8a, Android 16, kernel 6.1.145-android14-11, app
`us.lassul.droidnix` (release build, SELinux domain `untrusted_app_27`,
`TracerPid 0`, `Seccomp_filters 1` = only Android's app filter). Date:
2026-10-26. Prefix `/data/data/us.lassul.droidnix/files/usr`.

## What the launcher is

`default.nix` cross-builds one static, **non-PIE**, fixed-address aarch64 musl
binary with the nixpkgs-for-bootstrap toolchain (same as `experiments/sud`):

```
nix build --impure --expr 'import ./experiments/sud-loader {
  nixpkgs = (builtins.getFlake "git+file://$PWD").inputs.nixpkgs-for-bootstrap; }'
```

| File | What |
| --- | --- |
| `stub.S` | the exempt syscall instructions (`raw_syscall6`, `sig_restorer`) between `allow_start`/`allow_end`, plus `jump_to` |
| `loader.c` | everything: seccomp filter, SIGSYS handler + path translation, userspace ELF loader, shebang handling, execve re-exec |
| `Makefile`, `default.nix` | build (`-static -no-pie -fno-pie`) |
| `run-native.sh` | correctness + benchmarks, run in the failsafe (native) session |
| `run-proot.sh` | the same benchmarks under proot |

It is non-PIE and linked at a fixed address so its stub sits at the same
address in every process; the one seccomp filter (which survives execve and
cannot be removed) keeps exempting that fixed IP range forever.

On start it: installs the SIGSYS handler; on the first process installs
NO_NEW_PRIVS + one `RET_TRAP` filter for the path-taking syscalls (plus
execve/execveat, chdir, getcwd, readlinkat, and rt_sigaction/rt_sigprocmask),
exempting the stub; unblocks SIGSYS; resolves any `#!` interpreter; maps the
target PIE and its `/nix/store` interpreter itself; and jumps to the
interpreter. A re-exec (env `SUD_LOADER_ACTIVE`) does not stack a second
filter. A trapped execve re-execs the launcher with the translated target, so
every new image gets the handler again.

## Correctness (launcher, failsafe/native session)

All run through the launcher, no proot. Every case works:

| case | result |
| --- | --- |
| `/nix/store/…-hello/bin/hello` | `Hello, world!`, exit 0 |
| `bash -c "echo one two \| cat; (echo subshell-$((3+4)))"` | `one two` / `subshell-7`, exit 0 (forks + execs) |
| coreutils `ls /nix/store` | lists the store, exit 0 |
| coreutils `ls -l` of a store file | shows the `/nix/store/...` path (reverse-translated), exit 0 |
| `nix --version` | `nix (Nix) 2.34.9`, exit 0 |
| `nix-store --verify-path <glibc>` | valid, exit 0 |
| `readlink /proc/self/exe` | prints the target path (detranslated), exit 0 |
| `cd` into a store dir then `pwd` | returns the `/nix/...` path (getcwd reverse-translated) |
| gcc driver (non-PIE `ET_EXEC`) | clean error, not a crash (see limitations) |

The `GC Warning: Could not open /proc/stat` from nix is cosmetic (bdwgc
probing `/proc/stat`); nix works.

## Early checks

| check | result |
| --- | --- |
| multithreaded program | **works**: nix runs its threads + boehm GC |
| fork, then a trapped syscall in the child | **works**: fork preserves the handler and filter (verified directly) |
| fork + exec / execve re-exec | **works**: bash pipelines and nix both fork and exec |
| program that resets/blocks SIGSYS (bash) | **works, by defending it**: see below |
| `/proc/self/exe` | **works**: readlink returns the target, not the launcher |
| non-PIE (`ET_EXEC`) target | **fails cleanly**: rejected with an error, see limitations |

Five things had to be solved to get there; each is a real property of this
app domain, not an incidental bug:

* **Android's app filter itself traps some syscalls.** `set_robust_list` and
  `rseq`, which glibc/musl issue at startup and after fork, are
  `SECCOMP_RET_TRAP` in `untrusted_app_27` (verified: calling `set_robust_list`
  with only Android's filter installed SIGSYS-kills). A second filter can only
  make a syscall *more* restricted, so we cannot un-trap them. The handler
  therefore emulates proot: any syscall that reaches it but is not one we
  translate (i.e. Android trapped it) is failed with `-ENOSYS` instead of being
  reissued — reissuing would trap again and, inside the handler where SIGSYS is
  masked, be force-killed. glibc tolerates this exactly as it does under proot.
* **A re-exec from inside the handler carries a masked SIGSYS across execve.**
  The new image (and the target it jumps to) would start with SIGSYS blocked,
  and a seccomp trap on a blocked SIGSYS is force-killed regardless of the
  handler. The loader unblocks SIGSYS on every start.
* **bash resets signal state in its forked children.** It resets the SIGSYS
  disposition to default and blocks signals around fork, which would drop our
  handler and kill the children. We trap `rt_sigaction` (force any change to
  SIGSYS to keep our action) and `rt_sigprocmask` (clear SIGSYS from any
  block/setmask), so pipelines survive. A program that genuinely needs its own
  SIGSYS handler would conflict with this.
* **Absolute `/nix` symlinks in the store.** `bin/nix` is a symlink to an
  absolute `/nix/store/...` path; the kernel follows it to the literal `/nix`
  (which does not exist) → ENOENT. The handler resolves the symlink chain at
  the final path component itself, re-translating `/nix` targets.
* **`#!` scripts** (the gcc wrapper, most nix CLI tools) need their interpreter
  path translated. The loader does binfmt_script in userspace.

## Macro benchmarks: launcher vs proot

Same workload, same files, same timing (`date +%s%N`), 3 runs each, median
wall time. Target: the `glibc-2.44-57` store path (1059 files, 48 MB).
Launcher runs in the failsafe native session; proot runs in the normal
droid-nix shell (`Seccomp_filters 2`).

| workload | launcher (ms) | proot (ms) | speedup |
| --- | ---: | ---: | ---: |
| `nix-store --verify-path` | 180 | 983 | 5.5× |
| `find` over the subtree | 58 | 403 | 7.0× |
| `du -s` | 63 | 1349 | 21× |
| `tar cf /dev/null` | 63 | 1439 | 23× |
| small gcc compile | — | — | not supported (ET_EXEC, below) |

Raw runs (ms):

* launcher — verify 180/185/177, find 59/52/58, du 63/60/70, tar 63/63/65
* proot — verify 905/983/1088, find 403/405/310, du 1349/1227/1369, tar 1739/980/1439

The gap tracks the path-syscall density: `du`/`tar`/`find` stat and open every
entry, where proot's ptrace round-trip dominates (~21–23×); `verify-path`
spends more of its time reading and hashing file contents (native in both), so
the relative win is smaller (5.5×) but the launcher is still several times
faster. These macro ratios are consistent with phase 1's ~70–175× per-syscall
microbenchmarks diluted by real work and process startup.

## Limitations

* **Non-PIE (`ET_EXEC`) targets are unsupported.** They load at their own fixed
  vaddr (~0x400000), which collides with the non-PIE loader's own image; the
  loader rejects them with a clear error rather than crashing. gcc's bootstrap
  `xgcc`/`cc1` are `ET_EXEC`, so the gcc compile benchmark is not reached.
  (hello, bash, coreutils, findutils, gnutar, nix are all PIE and work.) Fixing
  this means linking the loader at a high fixed address out of the ET_EXEC
  range.
* **Symlink resolution is final-component only.** It follows a symlink chain at
  the last component of an absolute path; interior `/nix`-absolute components
  and dirfd-relative walks through such symlinks are not resolved. Full
  per-component canonicalisation (what proot does) is the main remaining work.
* **A program that installs its own SIGSYS handler or blocks SIGSYS by other
  means would break**, because interception is in-process. proot (ptrace,
  external) is immune to this; it is the fundamental trade-off of the approach.
* The handler translates a single path argument per syscall and both paths for
  link/rename; it does not yet cover every path-taking syscall or dirfd-relative
  `/nix` resolution.

## What this means for issue #1

* **The core bet pays off.** Real PIE `/nix/store` binaries — coreutils, bash
  with pipelines, and multithreaded nix — run natively in the app domain with
  no proot and no ptrace, and real store traversals (`du`, `tar`, `find`,
  `nix-store --verify-path`) are **5–23× faster than proot**. The seccomp
  `RET_TRAP` + in-process SIGSYS handler + userspace ELF loader is a viable
  replacement for proot's path translation on this kernel.
* **Android's app seccomp policy is part of the design, not just ours.** It
  traps `set_robust_list`/`rseq`, so any in-process (or proot) runtime here must
  emulate those; and because it is the outermost filter, our translation can
  only ever add restrictions, never remove Android's.
* **The hard, proot-like part is path canonicalisation through `/nix`-absolute
  symlinks.** The prototype does the common final-component case; a full
  per-component resolver is what stands between this and a drop-in proot
  replacement.
* **Non-PIE support is a one-line link-address change away** and should be done
  before any gcc/toolchain workload.

## Recommended next step

1. Link the loader at a high fixed address so `ET_EXEC` targets stop colliding,
   then redo the gcc compile benchmark.
2. Replace the final-component symlink resolver with full per-component path
   canonicalisation (absolute and dirfd-relative), the one piece that still
   makes proot more correct.
3. Widen the handler's path-syscall coverage and audit dirfd-relative `/nix`
   access, then run a real `nix build` end to end under the launcher and
   compare build times with proot.
