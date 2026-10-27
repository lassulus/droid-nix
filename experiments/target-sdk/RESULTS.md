# Raising targetSdkVersion: probes

Question: Google Play Protect blocks installing droid-nix 0.6.0 ("built for
an older version of Android") because it targets API 28, as Termux does.
Apps that target API 29 or later run in a SELinux domain where they may not
execute files in their data directory. Can droid-nix target the current API
level (36) when the launcher runs all programs?

Device: Pixel 8a, Android 16, kernel 6.1. Date: 2026-10-28.

## How it was run

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

## Breakage from the higher target (seen on the device)

| target | what broke | fix (app patch 0006) |
| --- | --- | --- |
| 34 | crash at start: `registerReceiver` needs RECEIVER_EXPORTED/NOT_EXPORTED | TermuxActivity's receiver is NOT_EXPORTED |
| 31 | crash: PendingIntents need FLAG_IMMUTABLE/MUTABLE (crash report notification, service notification) | FLAG_IMMUTABLE |
| 34 | starting a foreground service needs a type | TermuxService: `specialUse` + FOREGROUND_SERVICE_SPECIAL_USE |
