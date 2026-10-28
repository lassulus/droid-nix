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
| `run-command/` | a minimal app that sends RUN_COMMAND intents (see the last section) |

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

The terminal still lays out below the status bar, so edge-to-edge (35)
did not break it. RUN_COMMAND and notifications: next section.

## RUN_COMMAND under target 36

Other apps (Tasker etc.) run commands with the RUN_COMMAND intent:
RunCommandService checks `allow-external-apps`, makes itself a foreground
service and passes the command on to TermuxService, which runs it in the
background or in a new terminal session and sends the result back through
the caller's PendingIntent. Errors go to the caller or, without a
PendingIntent, to a notification. What the target broke (app patch 0008):

| target | what broke | fix |
| --- | --- | --- |
| 34 | RunCommandService's `startForeground` without a foreground service type (crash on every RUN_COMMAND) | `specialUse`, like TermuxService |
| 31 | TermuxPluginUtils' error notification PendingIntents without a mutability flag (crash on every error) | FLAG_IMMUTABLE |
| 29 | droid-nix callers run `$PREFIX/bin/login <command>`, a `#!/system/bin/sh` script the app may not execve | scripts whose `#!` names a `/system` interpreter run with it |
| 33 | POST_NOTIFICATIONS is a runtime permission, never requested, so no notifications | the activity asks for it when created |
| 31 | a caller in the background may not `startForegroundService` another app's service without an exemption | none possible in droid-nix: the exception is thrown in the caller; documented in the README |

Termux upstream (master) still targets 28 and has none of these fixes.

`run-command/` is a minimal caller (built with aapt2, javac, d8; target 36,
holds `us.lassul.droidnix.permission.RUN_COMMAND`, given with `pm grant`).
Its activity sends from the foreground, its `Trigger` receiver (`am
broadcast`) from the background, its `Result` receiver logs the result
bundle.

```sh
nix build --impure --expr 'import ./experiments/target-sdk/run-command {
  nixpkgs = (builtins.getFlake (toString ./.)).inputs.nixpkgs; }'
# sign result (apksigner), adb install, then
adb shell pm grant us.lassul.droidnix.runcommandtest us.lassul.droidnix.permission.RUN_COMMAND
adb shell "am start -n us.lassul.droidnix.runcommandtest/.RunCommandTest \
  --esa args bash,-c,'echo hi' --ez background true --ez result true --ei id 1"
adb shell "am broadcast -n 'us.lassul.droidnix.runcommandtest/.RunCommandTest\$Trigger' ..."
adb logcat -s RunCommandTest
```

All on the Pixel 8a with an APK of the patch series (throwaway key, fresh
install, first start from a local flake tarball):

| check | result |
| --- | --- |
| first start | the notification permission dialog shows; allowed |
| caller without the permission | SecurityException in the caller |
| `allow-external-apps` unset | refused, forced error notification ("requires allow-external-apps ...", screenshot); with a PendingIntent the caller gets err 2 and that message |
| `allow-external-apps=true`, app reopened | accepted |
| background command (`login bash -c 'date > ~/rc-bg; echo ...; echo oops >&2; exit 3'`) from the foreground, with result | `exitCode=3 stdout=[hello from aarch64] stderr=[oops]`, about 0.2 s |
| same from the background (receiver) | caller gets ForegroundServiceStartNotAllowedException (`Background started FGS: Disallowed`); droid-nix exempt from battery optimization does not help |
| same, caller exempt from battery optimization | allowed (`SYSTEM_ALLOW_LISTED`), result back, `~/rc-bg` was written by the first command |
| droid-nix not running (force-stopped), caller in foreground / exempt caller in background | RunCommandService starts TermuxService (`uidState: FGS, BFSL`), result back |
| new terminal session with result, without "Display over other apps" | session runs, activity not opened (upstream's toast), result: transcript, `exitCode=5` |
| same with "Display over other apps" (`appops set ... SYSTEM_ALERT_WINDOW allow`) | the session opens in front (screenshot), result as above |
| missing executable, no PendingIntent | error notification and toast (screenshot); tapping it opens the error report |
| session notification | "droid-nix · 1 session" shows |

Right after the first start, a RUN_COMMAND through `login` ran an
interactive bash that waited on its stdin: the bootstrap's login-inner
always starts bash, and the first switch's login-inner is only installed by
a login while no launcher runs. After closing all sessions (here: force
stop) it worked. This is how droid-nix's login works, not the target.
