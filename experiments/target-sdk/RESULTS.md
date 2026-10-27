# Raising targetSdkVersion: probes

Question: Google Play Protect blocks installing droid-nix 0.6.0 ("built for
an older version of Android") because it targets API 28, as Termux does.
Apps that target API 29 or later run in a SELinux domain where they may not
execute files in their data directory. Can droid-nix target the current API
level (36) when the launcher runs all programs?

Device: Pixel 8a, Android 16, kernel 6.1. Date: 2026-10-28.

## How it was run

| File | What |
| --- | --- |
| `probe.c`, `default.nix` | the probes, a static aarch64 musl binary |
| `verify.sh`, `hello-c.nix`, `hello.nix` | the checks of the target 36 app in the droid-nix shell |
| `mem.sh` | memory of an idle `nix repl` and of the session's bash |

`probe.c` (built by `default.nix`, a static aarch64 musl binary) was shipped
in a test APK as `lib/arm64-v8a/libprobe.so`. That APK was the app with
`targetSdkVersion=36` (`app/patches/0006`, which also fixes the crashes at
start that the higher target causes). Android extracts the library into the
app's native library directory (`useLegacyPackaging`). The probe ran from
there in the app's failsafe session (`/system/bin/sh` in the app's domain),
with the app data directory `files/probe` as argument. Each probe runs in a
forked child. Results are in the table below.

```
process context   u:r:untrusted_app:s0:c62,c257,c512,c768
Seccomp_filters   1 (Android's app filter)
native library    .../lib/arm64/libprobe.so  [u:object_r:apk_data_file:s0]
data file         files/probe/probe-copy     [u:object_r:app_data_file:s0:c62,...]
```

0.6.0 (target 28) ran in `untrusted_app_27`. With target 36 the app runs in
plain `untrusted_app`, the domain of apps that target API 34 or later.

## Results

| Probe | target 36 (`untrusted_app`) |
| --- | --- |
| execve of an app data file | **no** (EACCES; avc denied `execute_no_trans`) |
| mmap PROT_EXEC of an app data file (MAP_PRIVATE) | yes (avc *granted* `execute`, audited) |
| mprotect adding PROT_EXEC to a mapping of an app data file | yes (avc *granted* `execute`) |
| access(X_OK) of an app data file | yes (avc *granted* `execute`) |
| execve of the native library | yes |
| mmap PROT_EXEC of the native library | yes |
| anonymous mmap RWX | yes |
| anonymous RW, then mprotect RX | yes |
| a file read into anonymous RW memory, then RX | yes |
| memfd mmap PROT_EXEC (MAP_PRIVATE and MAP_SHARED) | yes |
| memfd execveat (fexecve) | **no** (EACCES; avc denied `open` of the memfd via /proc/self/fd) |
| NO_NEW_PRIVS + seccomp RET_TRAP filter, SIGSYS handler | yes |

The policy denies only `execute_no_trans` on `app_data_file`: execve. The
`execute` permission, which mmap and mprotect with PROT_EXEC and
access(X_OK) check, is still allowed. It is audited, so each such mapping
logs an `avc: granted { execute }` line (logcat, tag of the process). The
audit is likely there to measure how much apps rely on it.

## Consequence for droid-nix

The launcher never execve's a program from the data directory. A trapped
execve re-execs the launcher itself, and its ELF loader maps the target and
its ld.so with mmap. Under target 36 only two things execve app data files:

1. The kernel's execve of the launcher. Moving the launcher into the APK as
   `lib/arm64-v8a/liblauncher.so` fixes that: native libraries are
   `apk_data_file` and may be executed.
2. Termux starting `$PREFIX/bin/login`, a `#!/system/bin/sh` script. The app
   now runs it with `/system/bin/sh` (app patch 0007).

The loader's file-backed mappings and the shared libraries that ld.so maps
later work unchanged. The planned anonymous copies of executable mappings
are not needed and were not implemented. They are not needed on this device
and Android version, and could not have been tested. If a later Android
denies `execute` too, the probes above say what is left: anonymous memory
may be made executable, so the launcher would have to copy file contents
into anonymous memory (the loader's segments, plus trapped PROT_EXEC mmap
and mprotect calls on files). That would cost per-process memory, since the
page cache could no longer share the pages.

## On the device: the target 36 app

An APK built from commit 2894d54 (patches 0006 and 0007, launcher as
`liblauncher.so`, 18.56 MB; the bootstrap zip is 15.0 MB, 35.1 MB
unpacked, as before) was signed with a throwaway key. It was installed the
way a user sideloads, through the system package installer (VIEW intent on
the APK in Downloads). The installer showed the usual "unknown apps" notice.
Play Protect then showed "App scan recommended: Play Protect hasn't seen
this app before" with "Scan app", "Don't install app" and, under "More
details", "Install without scanning". There was **no** "Unsafe app blocked
... built for an older version of Android". "Install without scanning" gave
"App installed" (`targetSdk=36`). A new APK of any target would get the
scan prompt; the old-SDK block is gone. The test bootstrap fetched this
flake from a local tarball (`adb reverse`), so the first switch used the new
login.

`verify.sh` (with `hello-c.nix`, `hello.nix`) in the droid-nix shell,
after the first start:

| check | result |
| --- | --- |
| first start (unpack, template, first switch) | done, about 3.5 min, ends at the bash prompt |
| session | `untrusted_app:s0:c63,...`, `TracerPid: 0`, `Seccomp_filters: 2`, cmdline `<nativeLibraryDir>/liblauncher.so --reexec -b ...` |
| `nix --version` | `nix (Nix) 2.34.9`, 98 ms |
| `nix run nixpkgs#hello` | `Hello, world!` (31 s, including fetching nixpkgs) |
| small C nix-build (`hello-c.nix`, gcc substituted) | built, exit 0 (35.5 s including the 102 MiB download) |
| GNU hello from source, first (substitutes stdenv, source) | built, exit 0, 91.5 s |
| GNU hello from source, `--option substitute false` | **89682 / 89460 / 90310 ms**, median 89.7 s |
| 0.6.0 (target 28), same build (`experiments/sud-loader`, phase 4) | 89555 / 89761 / 89644 ms, median 89.6 s |

Memory (`mem.sh`): `/proc/<pid>/smaps_rollup` of an idle `nix repl` (after
its startup) and of the session's bash. Same script for both versions.

| | 0.6.0 (target 28) | target 36 |
| --- | ---: | ---: |
| nix repl Rss | 30424 kB | 30976 kB |
| nix repl Pss | 29118 kB | 29672 kB |
| nix repl Pss_Anon | 5792 kB | 5800 kB |
| bash Rss | 3584 kB | 3596 kB |
| bash Pss | 1646 kB | 1612 kB |

Mappings are still file-backed, so memory and speed are the same as 0.6.0.
The small differences come from the newer nixpkgs of the fresh install (a
different nix build in the profile).

Logcat during the run: 581 `avc: granted { execute }` lines for
`app_data_file` (the audited PROT_EXEC mappings, rate limited by the
logger). The few denials (`kvm` open, `cgroup_v2` search,
`proc_overcommit_memory` read, `ioctl` on app data files) did not affect
any of the checks above.

## Breakage from the higher target (seen on the device)

| target | what broke | fix (app patch 0006) |
| --- | --- | --- |
| 34 | crash at start: `registerReceiver` needs RECEIVER_EXPORTED/NOT_EXPORTED | TermuxActivity's receiver is NOT_EXPORTED |
| 31 | crash: PendingIntents need FLAG_IMMUTABLE/MUTABLE (crash report notification, service notification) | FLAG_IMMUTABLE |
| 34 | starting a foreground service needs a type | TermuxService: `specialUse` + FOREGROUND_SERVICE_SPECIAL_USE |
| 29 | Termux execs `$PREFIX/bin/login` (a `#!/system/bin/sh` script) directly | patch 0007: run it with `/system/bin/sh` |

Not changed, because nothing broke in these tests: RunCommandService
(RUN_COMMAND intent) calls `startForeground` without a type, and
TermuxPluginUtils builds PendingIntents without a mutability flag. Both
crash on target 34 and 31 once they are used. Without the
POST_NOTIFICATIONS runtime permission (33), the session notification is
not shown, but the foreground service runs. The terminal still lays out
below the status bar, so edge-to-edge (35) did not break it.
