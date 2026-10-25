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

Each release has one signed `droid-nix-<version>.apk` and a
`bootstrap-aarch64.zip`. You can also download the APK from the releases page.

On first start the app asks for the bootstrap URL. The default is
`https://github.com/lassulus/droid-nix/releases/latest/download/bootstrap-aarch64.zip`.
The app downloads and unpacks it. Then the first login:

1. creates `~/.config/droid-nix` from this flake's default template,
2. runs `nix run github:lassulus/droid-nix -- switch --flake ~/.config/droid-nix`.

After that, edit `~/.config/droid-nix/droid-nix.nix` and run `droid-nix switch`.
`droid-nix help` lists the commands: build, switch, generations, rollback and
switch-generation.

## The bootstrap

`bootstrap-aarch64.zip` is a minimal root filesystem that the app unpacks into
`/data/data/us.lassul.droidnix/files/usr`. It contains:

- a Nix store with Nix 2.20.5, bash and cacert,
- a static `proot` (termux/proot) that provides `/nix` and the other
  bind mounts,
- the `login` scripts that run the first-time setup described above.

Zip entries carry no file modes, so `EXECUTABLES.txt` and `SYMLINKS.txt` list
the executables and symlinks for the app to restore.

```sh
nix build .#bootstrap-aarch64   # result/bootstrap-aarch64.zip
```

The bootstrap is built with the `nixpkgs-for-bootstrap` input, pinned to the
same nixpkgs commit as upstream Nix-on-Droid (49ee0e94, nixos-24.05 as of
2024-07-06). Nix, bash and cacert are the aarch64-linux packages from
cache.nixos.org. proot is termux's fork, cross-compiled from x86_64-linux with
gcc as a static musl binary (`pkgsCross.aarch64-multiplatform-musl.pkgsStatic`,
see `bootstrap/proot.nix`). On-device generations refer to proot by the store
path of that same expression: the device only evaluates it and finds it in the
store the bootstrap shipped. Bumping `nixpkgs-for-bootstrap` changes that path,
and existing devices cannot build the new one themselves.

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
`app/patches`:

| patch | |
|---|---|
| 0001 | rebrand: package id `us.lassul.droidnix`, label `droid-nix`, version |
| 0002 | Nix snowflake launcher icon |
| 0003 | download the bootstrap zip from a URL instead of embedding Termux's |
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
