# droid-nix

droid-nix is a terminal app for Android that runs Nix. The app is small and
our own, built on [Termux](https://github.com/termux/termux-app)'s terminal
emulator and view, with [Termux:API](https://github.com/termux/termux-api)
built in. The userland is a Nix bootstrap and a declarative on-device
configuration, both derived from
[Nix-on-Droid](https://github.com/nix-community/nix-on-droid).

droid-nix is a separate app with package id `us.lassul.droidnix` and its own
authorities, permissions and intent actions. It can be installed next to Termux
and Nix-on-Droid without interfering with either. It needs Android 8 or later
and only supports aarch64 (arm64-v8a) devices.

## Usage

- [Getting started](docs/getting-started.md): install, first start, the
  configuration, `droid-nix switch`, phone storage, going back
- [Installing software](docs/packages.md): packages, `nix run`, searching
- [Running an SSH server](docs/ssh.md)
- [Termux:API](docs/termux-api.md): notifications, clipboard, location, ...
- [home-manager, git and an editor](docs/home-manager.md)
- [RUN_COMMAND](#run_command-tasker-and-other-apps): running commands from
  other apps

## Install

Install with [Obtainium](https://github.com/ImranR98/Obtainium) using the
repository URL:

    https://github.com/lassulus/droid-nix

Each release has one signed `droid-nix-<version>.apk`. You can also download
it from the releases page. The APK contains the bootstrap and the launcher.

The app targets API level 36 (Android 16). Unlike Termux, which stays at 28,
it is not blocked by Google Play Protect as "built for an older version of
Android". Play Protect may still offer to scan it as an app it has not seen
before ("Install without scanning" is under "More details").

On first start the app unpacks the bootstrap; this needs no network. Then the
first login:

1. creates `~/.config/droid-nix` from this flake's default template,
2. runs `nix run github:lassulus/droid-nix -- switch --flake ~/.config/droid-nix`.

After that, edit `~/.config/droid-nix/droid-nix.nix` and run `droid-nix switch`
(build and activate) or `droid-nix build` (only build). `--flake PATH` selects
another flake; all other arguments are passed to `nix build`, e.g.
`droid-nix switch --show-trace`.

Each switch adds a generation to the profile `/nix/var/nix/profiles/droid-nix`.
To go back to generation N:

```sh
nix-env --profile /nix/var/nix/profiles/droid-nix --list-generations
/nix/var/nix/profiles/droid-nix-N-link/activate
```

Most packages come from cache.nixos.org. `droid-nix switch` only builds a few
small derivations. Larger builds from source on the phone can hit a limit:
Android 12 and later kill an app's child processes beyond 32 (across all
apps), and the session ends with "Process completed (signal 9)". To lift it,
turn on "Disable child process restrictions" in the developer options, or run
`adb shell settings put global settings_enable_monitor_phantom_procs false`.

There are no upgrade paths for the bootstrap: a new APK does not touch an
existing install. If an update breaks something, uninstall and reinstall the
app. Version 0.8.0 (the app of our own) does not install over 0.7.x, which
had a shared user id: uninstall 0.7.x first.

## The bootstrap

`bootstrap-aarch64.zip` is a minimal root filesystem that the app unpacks into
`/data/data/us.lassul.droidnix/files/usr`. It is 15 MB zipped, 35 MB
unpacked, and contains:

- a Nix store with a static Nix 2.34.9 and a static busybox (`/bin/sh`
  for the first login),
- `/etc` with only what the first switch needs: `nix.conf`, `resolv.conf`,
  `hosts` and the CA certificates,
- the `login` scripts that run the first-time setup described above.

Everything else (bash, coreutils, the user's packages) comes from
cache.nixos.org with the first switch. Zip entries carry no file modes, so
`EXECUTABLES.txt` and `SYMLINKS.txt` list the executables and symlinks for
the app to restore.

```sh
nix build .#bootstrap-aarch64   # result/bootstrap-aarch64.zip
```

The bootstrap uses the flake's `nixpkgs` input. Nix, busybox and the launcher
(which the APK carries, not the bootstrap) are cross-compiled from x86_64-linux with gcc as static musl binaries
(`pkgsCross.aarch64-multiplatform-musl.pkgsStatic`); cache.nixos.org has no
static aarch64 Nix. Nothing on the device refers to these store paths after
the first switch.

## The launcher

`bootstrap/launcher/launcher.c` runs every process of a droid-nix session in
the view of the bind table in `modules/environment/login/login.nix` (`/nix`,
`/etc`, `/tmp`, ... and `/android` for the real root). It does what proot
did, in-process instead of with ptrace: a seccomp `SECCOMP_RET_TRAP` filter
on the path syscalls, a SIGSYS handler that translates the paths, and a
userspace ELF loader that starts each new program, so every execve keeps the
handler. A session runs under it when `/proc/self/status` shows `TracerPid: 0`
and `Seccomp_filters: 2` (Android's and the launcher's). How it came about,
its measurements and its limits are in
[issue 1](https://github.com/lassulus/droid-nix/issues/1).

Apps that target API 29 or later may not execve files in their data
directory; mmap with `PROT_EXEC` of those files is still allowed. The
launcher never execve's a program there: it maps each program and its ld.so
itself, and a trapped execve re-execs the launcher. So the APK ships the launcher as the native library
`lib/arm64-v8a/liblauncher.so`, which Android extracts to a directory the app
may execute from. The app passes that directory to the login as
`$DROID_NIX_NATIVE_LIB_DIR` and runs the login script with `/system/bin/sh`.

Hard links, which Android denies to apps, are emulated like proot's
`--link2symlink`: the first `ln a b` moves `a` into the link store
`/nix/.l2s` and makes `a` and `b` symlinks to it, with the link count kept
next to it. `stat`, `lstat`, `readlink`, `getdents` and `/proc/self/exe` see
a regular file with that count as `st_nlink`; `rm` and `mv` over a name count
down, and the last name removes the file. A file that keeps one name stays
in the link store; programs that write files with link and unlink (libgit2
in nix's tarball cache) leave such files there.

Known differences from proot:

- no `--sysvipc` emulation; `mount` and `umount` fail with ENOSYS, as
  Android denies them to apps (proot emulates bind mounts);
- the first hard link of a file needs write access to its directory (it is
  moved), so `nix store optimise` fails on the read-only store, as under
  proot; linking an fd (`AT_EMPTY_PATH`, `/proc/self/fd/N`) is not
  emulated; `/proc/self/fd/N` of a file opened through a hard link shows its
  path in the link store;
- a program that installs its own SIGSYS handler conflicts with the
  launcher's;
- `ps` shows `liblauncher.so` as the name and the launcher's command line
  for every process.

The app data path (`/data/data/us.lassul.droidnix`) and the package id are
defined once on the Nix side, in `android-app.nix`.

## On-device configuration

The flake exports `lib.droidNixConfiguration` and `templates.default`:

```nix
droidNixConfigurations.default = droid-nix.lib.droidNixConfiguration {
  pkgs = import nixpkgs { system = "aarch64-linux"; };
  modules = [ ./droid-nix.nix ];
  # optional, enables the home-manager.config option:
  # home-manager-path = home-manager.outPath;
};
```

The module options match Nix-on-Droid's: `environment.packages`, `environment.etc`,
`nix.*`, `user.shell`, `terminal.*`, `time.timeZone`, `networking.*`,
`android-integration.*` and `home-manager.*`. droid-nix supports flakes only
(no channels) and has no `system.stateVersion`. It does not pull in
home-manager itself.

### Termux:API

The app carries Termux:API, there is no separate app to install. The
commands come with

```nix
android-integration.termux-api.enable = true;
```

which installs all of
[termux-api-package](https://github.com/termux/termux-api-package)'s
commands (`pkgs/android-integration/termux-api.nix`):

```sh
termux-notification -t hello -c "from droid-nix"
termux-battery-status
echo hi | termux-clipboard-set; termux-clipboard-get
termux-location -p network
```

A command that needs a runtime permission (location, camera, microphone,
contacts, SMS, call log, phone) asks for it the first time; run it again
after granting. droid-nix's own terminal is in the foreground when it sends a
request, so `termux-location` does not need "Allow all the time".

## The app

`packages.x86_64-linux.droid-nix-app` builds an unsigned release APK from
`app/`, a small gradle project (Java, package `us.lassul.droidnix`):

| `app/src/main/java/us/lassul/droidnix/` | |
|---|---|
| `TerminalActivity` | the terminal, the sessions drawer, the context menu, edge to edge with the system bars and the keyboard |
| `TerminalClient` | keys (Ctrl+Alt shortcuts, the volume keys as Ctrl and Fn), clipboard, bell |
| `ExtraKeys` | the extra keys rows, in Termux's `extra-keys` format |
| `Props` | `~/.droid-nix/droid-nix.properties` |
| `TerminalService` | the sessions as a foreground service, the notification, the wake lock, running commands |
| `Installer` | unpacks the bootstrap asset; `~/storage` |
| `Shell` | the environment, the login, `#!` scripts |
| `AmServer` | termux-am's socket server (`files/am.sock`) |
| `OpenReceiver` | termux-open, xdg-open |
| `HomeDocumentsProvider` | the home directory in Android's Files app and file picker |
| `RunCommandService`, `BackgroundCommand` | RUN_COMMAND, commands without a terminal |

Upstream code it builds, unpatched, from the `termux-app` flake input:
`terminal-emulator` and `terminal-view` (and their `libtermux.so`, built
with the NDK in Nix). The build copies `bootstrap-aarch64.zip` into the
app's assets and the launcher into its native libraries, which makes the APK
18.1 MB.

Upstream Termux:API, pinned as the `termux-api` flake input, is the module
`app/termux-api` with the patches in `app/termux-api-patches`. It compiles
against `app/termux-shared-compat`, the part of Termux's termux-shared that
it uses, under the same class names and with droid-nix's values; a new
upstream call fails at compile time and shows what to add there.

| patch | |
|---|---|
| 0001 | the receiver class name (the package is not com.termux.api); no log level from Termux:API's preferences |
| 0002 | target API level 36: plain text toasts, mutable reply PendingIntent, notification actions and job scripts through `$PREFIX/bin/login`, no background location check |

The CLI (`termux-api` helper) runs `am broadcast` through termux-am to
`us.lassul.droidnix/com.termux.api.TermuxApiReceiver`, which is not exported:
only the app itself (the am socket in its process) can call it. These
permissions are for Termux:API; the runtime ones are only granted when a
command asks for them:

- location: `ACCESS_FINE_LOCATION`, `ACCESS_COARSE_LOCATION`,
  `ACCESS_BACKGROUND_LOCATION`;
- `CAMERA`, `RECORD_AUDIO`, `BODY_SENSORS`, `USE_BIOMETRIC` (and
  `USE_FINGERPRINT` from androidx.biometric);
- `READ_CONTACTS`, `READ_CALL_LOG`, `READ_SMS`, `SEND_SMS`, `READ_PHONE_STATE`,
  `CALL_PHONE`;
- `ACCESS_WIFI_STATE`, `CHANGE_WIFI_STATE`, `NFC`, `TRANSMIT_IR`,
  `SET_WALLPAPER`, `WRITE_SETTINGS` (brightness, granted in the system
  settings);
- `us.lassul.droidnix.sharedfiles.READ_WRITE` (signature, guards
  `termux-share`'s content provider).

```sh
nix build .#droid-nix-app
ls result/
# droid-nix-unsigned.apk          as produced by gradle
# droid-nix-unsigned-aligned.apk  zipaligned (4 bytes, 16 KiB pages for .so)
```

The toolchain comes from nixpkgs: `androidenv` (platform 36, build-tools
35.0.0, NDK 29.0.14206865, see `pkgs/android-sdk.nix`), Gradle 9.2.1 and JDK 17.
Maven dependencies are pinned in `app/deps.json`. After changing gradle
dependencies, regenerate it (needs network) with `nix run .#update-deps`.

The app version (`versionName`/`versionCode`) is set in `app/build.gradle`.
The release workflow refuses a tag that does not match it.

### Terminal settings

`~/.droid-nix/droid-nix.properties` takes these keys of Termux's
`termux.properties`, with the same values: `extra-keys`, `extra-keys-style`,
`extra-keys-text-all-caps`, `allow-external-apps`, `bell-character`,
`back-key`, `volume-keys`, `terminal-transcript-rows`,
`terminal-cursor-style`, `terminal-cursor-blink-rate`,
`terminal-margin-horizontal`, `terminal-margin-vertical`,
`terminal-toolbar-height`, `hide-soft-keyboard-on-startup`,
`soft-keyboard-toggle-behaviour`, `enforce-char-based-input`,
`ctrl-space-workaround`, `terminal-onclick-url-open`,
`disable-hardware-keyboard-shortcuts` and
`shortcut.{create,next,previous,rename}-session`. `terminal.colors` and
`terminal.font` link `colors.properties` and `font.ttf` next to it.
`termux-reload-settings` applies changes to the running app.

### RUN_COMMAND (Tasker and other apps)

Other apps run commands with Termux's
[RUN_COMMAND intent](https://github.com/termux/termux-app/wiki/RUN_COMMAND-Intent),
with `us.lassul.droidnix` in place of `com.termux` in the action, extras and
permission names, and the service class `us.lassul.droidnix.RunCommandService`.
The result directory extras (`RUN_COMMAND_RESULT_*`) are not supported, a
PendingIntent is. droid-nix allows external apps by default, the permission
grant below is the gate (`allow-external-apps=false` in
`~/.droid-nix/droid-nix.properties` turns them off):

- grant the calling app the `us.lassul.droidnix.permission.RUN_COMMAND`
  permission (its app info, permissions, or
  `adb shell pm grant <package> us.lassul.droidnix.permission.RUN_COMMAND`);
- run commands through the login script, which sets up the Nix environment:
  path `/data/data/us.lassul.droidnix/files/usr/bin/login`, arguments e.g.
  `bash`, `-c`, `<command>`.

Since the app targets API 36, Android refuses a caller in the background (no
visible activity) unless the caller has one of Android's exemptions for
starting foreground services, e.g. it is not battery optimized. A command in a new session opens droid-nix in front only if
droid-nix may "Display over other apps", as in Termux.

### Updating termux-app and Termux:API

Set the new rev in the `termux-app` or `termux-api` input in `flake.nix`, run
`nix flake update termux-app` (or `termux-api`) and `nix run .#update-deps`,
and rebuild. terminal-emulator and terminal-view are used unpatched. For
Termux:API, a compile error names what to add to `app/termux-shared-compat`,
and the patches are kept like this:

```sh
git clone https://github.com/termux/termux-api && cd termux-api
git checkout <old rev from flake.nix>
git am /path/to/droid-nix/app/termux-api-patches/*.patch
git rebase <new rev>               # resolve conflicts, keep the series short
rm /path/to/droid-nix/app/termux-api-patches/*.patch
git format-patch --zero-commit --no-signature -o /path/to/droid-nix/app/termux-api-patches <new rev>
```

## Signing

Builds are unsigned. Signing happens outside of Nix so the key never enters the
store:

```sh
SIGN_KEYSTORE=/path/to/release.p12 SIGN_KEY_ALIAS=... \
SIGN_KEYSTORE_PASSWORD=... [SIGN_KEY_PASSWORD=...] \
  nix run .#sign -- result/droid-nix-unsigned-aligned.apk droid-nix.apk
```

CI (`.github/workflows/release.yml`) runs on `v*` tags and on manual dispatch.
It decodes the `SIGN_KEYSTORE_B64` secret (base64 PKCS12) into a temporary
file and signs with `SIGN_KEYSTORE_PASSWORD` and `SIGN_KEY_ALIAS`. On tags it
publishes the release.

Losing or rotating the key means users have to uninstall and reinstall.

## Roadmap

1. Done: Termux:API, built into the app, and its commands
   (`android-integration.termux-api.enable`).
2. Audit the packages Termux users rely on against termux-packages and fill the
   gaps.
3. Port termux-desktop (Termux:X11 based desktop) as Nix packages/modules.

## License

The Nix code derived from Nix-on-Droid is MIT licensed (`LICENSE`, `AUTHORS`).
The app (`app/`) is GPLv3 like Termux, whose code it builds on. See `NOTICE`.
