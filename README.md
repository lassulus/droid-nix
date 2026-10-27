# droid-nix

droid-nix is a terminal app for Android that runs Nix. The app is
[Termux](https://github.com/termux/termux-app) with a small patch series on
top. The userland is a Nix bootstrap and a declarative on-device configuration,
both derived from [Nix-on-Droid](https://github.com/nix-community/nix-on-droid).

droid-nix is a separate app with package id `us.lassul.droidnix` and its own
authorities, permissions and intent actions. It can be installed next to Termux
and Nix-on-Droid without interfering with either. It only supports aarch64
(arm64-v8a) devices.

## Install

Install with [Obtainium](https://github.com/ImranR98/Obtainium) using the
repository URL:

    https://github.com/lassulus/droid-nix

Each release has one signed `droid-nix-<version>.apk`. You can also download
it from the releases page. The APK contains the bootstrap.

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
app.

## The bootstrap

`bootstrap-aarch64.zip` is a minimal root filesystem that the app unpacks into
`/data/data/us.lassul.droidnix/files/usr`. It is 15 MB zipped, 35 MB
unpacked, and contains:

- a Nix store with a static Nix 2.34.9 and a static busybox (`/bin/sh`
  for the first login),
- the static `launcher` that provides `/nix` and the other bind mounts
  (see below),
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
are cross-compiled from x86_64-linux with gcc as static musl binaries
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
its measurements and its limits are in `experiments/sud-loader`.

Hard links, which Android denies to apps, are emulated like proot's
`--link2symlink`: the first `ln a b` moves `a` into the link store
`/nix/.l2s` and makes `a` and `b` symlinks to it, with the link count kept
next to it. `stat`, `lstat`, `readlink`, `getdents` and `/proc/self/exe` see
a regular file with that count as `st_nlink`; `rm` and `mv` over a name count
down, and the last name removes the file. A file that keeps one name stays
in the link store; programs that write files with link and unlink (libgit2
in nix's tarball cache) leave such files there.

Known differences from proot:

- no `--sysvipc` emulation;
- the first hard link of a file needs write access to its directory (it is
  moved), so `nix store optimise` fails on the read-only store, as under
  proot; linking an fd (`AT_EMPTY_PATH`, `/proc/self/fd/N`) is not
  emulated; `/proc/self/fd/N` of a file opened through a hard link shows its
  path in the link store; two processes linking or unlinking the same file
  at once can miscount;
- a program that installs its own SIGSYS handler conflicts with the
  launcher's;
- `ps` shows `launcher` as the name and the launcher's command line for every
  process.

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

## The app

`packages.x86_64-linux.droid-nix-app` builds an unsigned release APK from
upstream termux-app, pinned as the `termux-app` flake input, plus the series in
`app/patches`. The build copies `bootstrap-aarch64.zip` into the app's assets,
which makes the APK 18.5 MB.

| patch | |
|---|---|
| 0001 | rebrand: package id `us.lassul.droidnix`, label `droid-nix`, version |
| 0002 | Nix snowflake launcher icon |
| 0003 | unpack droid-nix's bootstrap zip from the APK assets instead of Termux's |
| 0004 | set executable bits from `EXECUTABLES.txt` |
| 0005 | build for arm64-v8a only |

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

The app version (`versionName`/`versionCode`) is set in `app/patches/0001-*`.
The release workflow refuses a tag that does not match it.

### Updating termux-app

```sh
git clone https://github.com/termux/termux-app && cd termux-app
git checkout <old rev from flake.nix>
git am /path/to/droid-nix/app/patches/*.patch
git rebase <new rev>               # resolve conflicts, keep the series short
rm /path/to/droid-nix/app/patches/*.patch
git format-patch --zero-commit --no-signature -o /path/to/droid-nix/app/patches <new rev>
```

Then set the new rev in the `termux-app` input in `flake.nix`, run
`nix flake update termux-app` and `nix run .#update-deps`, and rebuild. To change
a patch, commit or `git commit --fixup` on that checkout and export the series
again the same way.

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

Future plugins (Termux:API for droid-nix) must be signed with the **same key**.
They share a Linux user via `android:sharedUserId="us.lassul.droidnix"`, and
Android only allows that between packages with the same signing certificate.
Losing or rotating the key means users have to uninstall and reinstall.

## Roadmap

1. Termux:API for droid-nix (`us.lassul.droidnix.api`, same sharedUserId) and
   the `termux-api` CLI (`termux-notification` etc.) as a Nix package, for
   notifications and other Android APIs.
2. Audit the packages Termux users rely on against termux-packages and fill the
   gaps.
3. Port termux-desktop (Termux:X11 based desktop) as Nix packages/modules.

## License

The Nix code derived from Nix-on-Droid is MIT licensed (`LICENSE`, `AUTHORS`).
The patches to termux-app are GPLv3 like Termux. See `NOTICE`.
