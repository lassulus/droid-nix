# Getting started

## Install the app

droid-nix needs an arm64 (aarch64) phone. Download `droid-nix-<version>.apk`
from the [releases page](https://github.com/lassulus/droid-nix/releases), or
add `https://github.com/lassulus/droid-nix` to
[Obtainium](https://github.com/ImranR98/Obtainium) to get updates.

Open the APK. What Android shows depends on where you open it from:

1. The first time an app (browser, Files, Obtainium) installs an APK,
   Android asks you to allow "Install unknown apps" for that app.
2. "Your phone and personal data are more vulnerable to attack by unknown
   apps...": tap **Continue**.
3. "Install this app?" with the droid-nix icon: tap **Install**.
4. Google Play Protect: "App scan recommended. Play Protect hasn't seen this
   app before". Either tap **Scan app**, or open **More details** and tap
   **Install without scanning**.
5. "App installed": tap **Open**.

## First start

The first start needs a network connection. It took 2 minutes 10 seconds on
Wi-Fi on a Pixel 8a and downloads about 200 MB.

1. The app unpacks the bootstrap from the APK. This needs no network.
2. Android asks "Allow droid-nix to send you notifications?". Allow it if
   you want the session notification (it has the wake lock button, see
   [ssh.md](ssh.md)) and notifications from `termux-notification`.
3. The terminal shows:

   ```
   Setting default user profile...
   Sourcing Nix environment...
   Installing flake from default template...
   wrote: ".../home/.config/droid-nix/droid-nix.nix"
   wrote: ".../home/.config/droid-nix/flake.nix"
   Installing first droid-nix generation...
   ```

   This fetches droid-nix from `github:lassulus/droid-nix`, fetches nixpkgs,
   and builds and activates the first generation of your configuration.
   Almost everything comes from cache.nixos.org; only a few small
   derivations (the profile, `/etc`) are built on the phone.
4. It ends with "Congratulations! Now you have Nix installed ...", "Welcome
   to droid-nix!" and a `-bash-5.3$` prompt.

Each session, the first one included, is a login shell that reads
`~/.bash_profile` and uses `user.shell`. The droid-nix notification has
**Exit** and **Acquire wakelock**.

## What you have

The default configuration installs bash, coreutils, `less`, `vim`, `nix`
and the `droid-nix` command. Everything else is missing until you add it:
there is no `grep`, `sed`, `find`, `ps`, `which`, `clear`, `man`, `curl`,
`git` or `ssh`. See [packages.md](packages.md).

Paths:

- home: `/data/data/us.lassul.droidnix/files/home` (`~`);
- your configuration: `~/.config/droid-nix` (`flake.nix`, `droid-nix.nix`,
  `flake.lock`);
- Android's root filesystem: `/android` (the phone's storage is
  `/android/sdcard`, but droid-nix has no storage permission by default).

## Change the configuration

Edit `~/.config/droid-nix/droid-nix.nix`, for example with `vim`, and run:

```sh
droid-nix switch
```

This builds the configuration and activates it. New commands are available
right away in the running shell. A switch that only adds a few packages took
about 40 seconds. `droid-nix build` only builds and leaves a `result` link.
Other arguments go to `nix build`, e.g. `droid-nix switch --show-trace`.

To update nixpkgs and droid-nix:

```sh
nix flake update --flake ~/.config/droid-nix
droid-nix switch
```

The flake is a normal directory; you can put it under git (add `git` to
your packages first).

## Go back to an older generation

Each switch adds a generation of the profile
`/nix/var/nix/profiles/droid-nix`. List them and activate an older one:

```sh
nix-env --profile /nix/var/nix/profiles/droid-nix --list-generations
/nix/var/nix/profiles/droid-nix-1-link/activate
```

The next `droid-nix switch` builds `droid-nix.nix` again, so fix or revert
the file before you switch.
