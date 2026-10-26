# In-process syscall interception on Android: phase 1 results (issue #1)

Question: instead of proot's ptrace-based path translation, can droid-nix's own
processes intercept their own syscalls in-process (like gVisor systrap /
lazypoline), inside the normal app sandbox?

Device: Pixel 8a, Android 16, kernel 6.1.145-android14-11, app
`us.lassul.droidnix` (release build, SELinux domain `untrusted_app_27`).
Date: 2026-10-26.

## How it was run

`default.nix` cross-builds static aarch64 musl binaries with the
nixpkgs-for-bootstrap toolchain:

```
nix build --impure --expr 'import ./experiments/sud {
  nixpkgs = (builtins.getFlake "git+file://$PWD").inputs.nixpkgs-for-bootstrap; }'
```

| File | What |
| --- | --- |
| `stub.S` | the only syscall instructions exempt from interception (`raw_syscall6`, `sig_restorer`), between `allow_start`/`allow_end` |
| `common.h` | SIGSYS handler (rewrites `/nix/...` to `<prefix>/nix/...` for openat/newfstatat, forwards through the stub), SUD enable, seccomp filter |
| `probe_sud.c` | a) syscall user dispatch |
| `probe_seccomp.c` | b) NO_NEW_PRIVS + `SECCOMP_RET_TRAP` filter for getpid, except from the exempt IP range |
| `probe_mmap0.c` | c) `mmap(0, MAP_FIXED)` |
| `probe_interp.c` | d) execve of a hand-made ELF with `PT_INTERP=/nonexistent/ld-fake.so.1` (plus a no-interp control that exits 42) |
| `bench.c` | microbenchmarks, `bench plain\|sud\|seccomp <prefix> <store-file>` |
| `run-proot.sh`, `run-native.sh` | the scripts run in the app |

The sigaction goes through a raw `rt_sigaction` with `SA_RESTORER` pointing at
the stub. That way the handler's sigreturn also comes from the exempt range.

Three environments:

* **app, under proot**: the normal droid-nix shell (`bash run-proot.sh`).
  TracerPid = proot, `Seccomp_filters: 2` (Android's plus proot's).
* **app, native**: the app's failsafe session, which runs `/system/bin/sh`
  directly in the app domain without proot (started with
  `am start -S -n us.lassul.droidnix/com.termux.app.TermuxActivity --ez us.lassul.droidnix.app.failsafe_session true`).
  It has the same SELinux context (`u:r:untrusted_app_27:s0:c57,c257,c512,c768`),
  `TracerPid: 0` and `Seccomp_filters: 1` (Android's app filter). The binaries
  ran from `files/usr/nix/sud-exp`, and the real store is
  `/data/data/us.lassul.droidnix/files/usr/nix/store`.
* **adb shell**: `u:r:shell:s0`, no seccomp filter, binaries in
  `/data/local/tmp`, with a fake `/data/local/tmp/sud/fake/nix/store/x/file`.

## Probe results

| Probe | app, native (failsafe) | app, under proot | adb shell |
| --- | --- | --- | --- |
| a) `PR_SET_SYSCALL_USER_DISPATCH` | **EINVAL** | **EINVAL** | **EINVAL** |
| b) NNP + seccomp `RET_TRAP` filter | NNP ok, filter ok, **SIGSYS arrives** (si_code=1 SYS_SECCOMP), handler answers getpid=4242, getpid from the exempt range returns the real pid | NNP ok, filter ok, but **no SIGSYS reaches the handler**: getpid returns **-38 (ENOSYS)**; the exempt range works | same as native: works, exception works |
| c) mmap at 0 | **EPERM** | **EPERM** | **EPERM** |
| d) PT_INTERP missing | control runs (exit 42); bad interp: **ENOENT** | control runs; bad interp: **ENOENT** | control runs; bad interp: **ENOENT** |

Notes:

* a) The kernel has no syscall user dispatch on arm64 in 6.1. The prctl falls
  through to the `!CONFIG_GENERIC_ENTRY` stub that returns EINVAL, and 6.1
  arm64 does not use the generic entry code. This is a kernel limit, not an
  Android policy, so it is the same in every domain. SUD-based designs
  (lazypoline) are out on current Android kernels.
* b) The app may stack its own filter on top of Android's app filter. The
  `instruction_pointer` exemption works. Under proot the kernel does deliver
  the trap, but proot-termux intercepts it. proot sees every
  `SIGSYS`/`SYS_SECCOMP` signal in its tracee
  (`src/tracee/event.c:646`) and assumes it came from Android's
  seccomp policy. `handle_seccomp_event` (`src/tracee/seccomp.c`) then emulates
  a few syscalls and otherwise sets the result to `-ENOSYS` and swallows the
  signal. Without patching proot, seccomp-trap interception therefore cannot be
  layered under proot. It has to replace proot.
* c) There are no zero-page trampolines (zpoline style). That technique is
  x86-only anyway.
* d) The kernel resolves PT_INTERP itself, and the seccomp handler never gets
  to rewrite it. Running `/nix/store` dynamic ELFs without proot needs a
  userspace ELF loader, as expected.

## Microbenchmarks

ns/call, median of 5 runs × 1,000,000 iterations. `plain` means no interception.
Native `plain` uses the real path (`/data/data/us.lassul.droidnix/files/usr/nix/...`).
`seccomp` uses the `/nix/...` path, and the SIGSYS handler prepends the prefix
and reissues the syscall from the exempt stub. In `seccomp` only getpid, openat
and newfstatat trap (close does not); `sigsys/iter=1.00` was verified for
every seccomp row. Store file: `/nix/store/brnhnyb8…-bash-interactive-5.3p20/bin/bash`.

| | getpid | stat("/nix/store") | openat+close |
| --- | ---: | ---: | ---: |
| app native, plain | 146 | 926 | 2,152 |
| app native, seccomp trap + rewrite | 1,066 | 1,986 | 3,357 |
| app under proot (default, proot seccomp mode) | 146¹ | **346,945**² | **231,664** |
| app under proot, seccomp trap | n/a³ | n/a³ | n/a³ |
| app, SUD + rewrite | n/a⁴ | n/a⁴ | n/a⁴ |
| adb shell, plain | 128 | 808 | 1,971 |
| adb shell, seccomp trap + rewrite | 1,040 | 1,888 | 3,147 |

Raw runs: proot stat 185212, 346945, 350966, 362406, 322758. proot
openat+close 239926, 231664, 227996, 238123, 231545. Native seccomp:
getpid 1109/1064/1066/1066/1067, stat 1986/1988/1986/1986/1989,
openat+close 3369/3371/3344/3355/3357.

1. proot's own filter doesn't trace getpid, so getpid runs at native speed.
2. The first run was ~185 µs, and the later runs got slower (~320–360 µs). This is
   still ~200× native either way.
3. Not measurable. The filter installs, but proot swallows the SIGSYS and the
   calls return ENOSYS (see probe b), so the bench's pre-check
   `stat` fails with errno 38.
4. Not measurable: the prctl fails with EINVAL.

Each trap costs about **0.9–1.2 µs** (the trapped getpid minus plain getpid is
~920 ns, and the rewrite adds ~100–200 ns on top). For path syscalls that means:

* stat: 347 µs under proot vs 2.0 µs with an in-process trap, **~175× faster**
* openat+close: 232 µs vs 3.4 µs, **~70× faster**

Even if a real implementation spent 10× more time in the handler than this toy
prefix rewrite (cwd/dirfd tracking, readlink/getcwd detranslation, bind
lookups), it would still be more than an order of magnitude faster than proot.

## What this means for issue #1

* **Feasible: seccomp `SECCOMP_RET_TRAP` + in-process SIGSYS handler.** It
  works in the normal app sandbox (`untrusted_app_27`). It needs no privileges
  and no ptrace, and it stacks on Android's app filter.
* **Not feasible on this kernel: syscall user dispatch** (arm64 has no SUD in
  6.1), and nothing that needs page 0.
* **It must replace proot, not run under it.** proot-termux eats the SIGSYS.
  A hybrid would need a proot patch that tells Android-policy SIGSYS apart from
  the tracee's own filter. That is fragile and also keeps the ptrace cost.
* **A userspace ELF loader is required.** Probe d confirms it. execve also
  shapes the design:
  * seccomp filters and NO_NEW_PRIVS survive execve, but the SIGSYS handler
    does not. So a new image would die on its first trapped syscall unless
    every execve goes through our loader (trap execve and re-exec the
    loader with the target as an argument). The loader then installs the
    handler before any trapped syscall.
  * The filter's exempt IP range is fixed when it is installed and stays in
    force after execve. The loader's syscall stub must therefore sit at the
    same fixed address in every process (a non-PIE loader linked at a fixed
    address). Otherwise each exec would have to stack another filter
    (filters can't be removed; there is a cap on total filter size).
* Every trap costs ~1 µs, so trap only path-taking syscalls plus
  execve/chdir/getcwd-style ones. Leave getpid/read/write etc. untrapped, as
  proot does.

## Recommended next step

Phase 2 is a minimal prototype launcher. It is a static, fixed-address loader
that does the following:

1. installs the SIGSYS handler and one seccomp `RET_TRAP` filter for the
   path syscalls + execve, exempting its own stub's fixed IP range;
2. maps the target ELF and its `/nix/store/...` interpreter itself (userspace
   loader) and jumps to it;
3. turns trapped `execve` into a re-exec of itself with the target.

Run it from the failsafe-style native path (no proot) and repeat the earlier
macro benchmarks (nix-store --verify, du, find, tar, gcc) against proot. Things
to check early: the signal-safety of the handler in multithreaded programs
(nix itself), programs that install their own SIGSYS handler or seccomp
filters, `vfork`, and `/proc/self/exe` detranslation.
