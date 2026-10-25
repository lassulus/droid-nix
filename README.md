# droid-nix

A Nix flake to make Android with [Nix-on-Droid](https://github.com/nix-community/nix-on-droid)
as capable as Termux: an up-to-date Nix-on-Droid app, Termux:API for Nix-on-Droid
and later termux-desktop, all built from source with Nix.

## Status

Milestone 1 (app rebase + reproducible build): done.

- App source: `nix-on-droid-app` branch `rebase-termux`, i.e. the Nix-on-Droid
  patches (rebrand to `com.termux.nix`, bootstrap download from URL, arm64)
  rebased onto current `termux/termux-app` master.
- `packages.x86_64-linux.nix-on-droid-app`: unsigned release APK (arm64-v8a),
  built in the Nix sandbox without network access.

## Building

```sh
nix build .#nix-on-droid-app
ls result/
# nix-on-droid-app-unsigned.apk          as produced by gradle
# nix-on-droid-app-unsigned-aligned.apk  zipaligned (4 bytes, 16 KiB pages for .so)
```

The toolchain comes from nixpkgs: `androidenv.composeAndroidPackages`
(platform 36, build-tools 35.0.0, NDK 29.0.14206865, matching termux-app's
gradle files; the SDK license is accepted in `flake.nix`), Gradle 9.2.1 and
JDK 17. Maven dependencies are pinned in `pkgs/nix-on-droid-app/deps.json`
and served through nixpkgs' `mitmCache`.

After changing the app's gradle dependencies, regenerate the lockfile
(needs network) from the repository root:

```sh
nix run .#update-deps
```

The app source is the `nix-on-droid-app` flake input; to build from a GitHub
fork, change its `url` line in `flake.nix` and run `nix flake update nix-on-droid-app`.

## Signing

Builds are unsigned. Signing happens outside of Nix so the key never enters the
store:

```sh
SIGN_KEYSTORE=/path/to/release.jks SIGN_KEY_ALIAS=nix-on-droid \
SIGN_KEYSTORE_PASSWORD=... [SIGN_KEY_PASSWORD=...] \
  nix run .#sign -- result/nix-on-droid-app-unsigned-aligned.apk nix-on-droid-app.apk
```

In CI the keystore and passwords will be repository secrets. The app and the
later Termux:API plugin must be signed with the **same key**: they share a
Linux user via `android:sharedUserId="com.termux.nix"`, and Android only allows
that between packages with the same signing certificate. Losing or rotating
the key means users have to uninstall and reinstall.

No keystore is part of this repository.

## Roadmap

1. Rebase nix-on-droid-app onto termux-app master and build the APK with Nix (this milestone).
2. Termux:API fork for Nix-on-Droid (`com.termux.nix.api`, same sharedUserId) plus a
   `termux-api` CLI package (`termux-notification` etc.) for Nix-on-Droid.
3. GitHub Actions: build, sign with the secret key, publish releases consumable by Obtainium.
4. Audit packages that Termux users rely on against termux-packages and fill gaps in nixpkgs/Nix-on-Droid.
5. Port termux-desktop (X11/Termux:X11 based desktop) as Nix packages/modules.
