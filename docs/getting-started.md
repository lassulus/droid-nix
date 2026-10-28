# Getting started

## Install

droid-nix needs an arm64 phone. Download `droid-nix-<version>.apk` from the
[releases page](https://github.com/lassulus/droid-nix/releases), or add
`https://github.com/lassulus/droid-nix` to
[Obtainium](https://github.com/ImranR98/Obtainium) to get updates. Open the
APK and install it; allow installing unknown apps and confirm Play Protect
if Android asks.

## First start

The first start needs a network connection and downloads about 200 MB. It
creates your configuration from a template, builds it and ends at a shell
prompt. Allow notifications if you want the session notification and
`termux-notification`.

The default configuration has bash, coreutils, `less`, `vim`, `nix` and the
`droid-nix` command. To add more, see [packages.md](packages.md).

Your home is `/data/data/us.lassul.droidnix/files/home`, your
configuration is in `~/.config/droid-nix`.

## Change the configuration

Edit `~/.config/droid-nix/droid-nix.nix` and run:

```sh
droid-nix switch
```

New commands are available in the running shell right away.
`droid-nix build` only builds. Other arguments go to `nix build`, e.g.
`droid-nix switch --show-trace`.

To update nixpkgs and droid-nix:

```sh
nix flake update --flake ~/.config/droid-nix
droid-nix switch
```

## Phone storage

To use the phone's shared storage (Downloads, DCIM, ...), add:

```nix
  android-integration.termux-setup-storage.enable = true;
```

switch and run `termux-setup-storage`. Android opens "All files access"
for droid-nix: turn on **Allow access to manage all files** and go back.
`~/storage` then has links such as `shared` (the whole storage),
`downloads`, `dcim` and `pictures`.

## The terminal

Swipe from the left edge for the sessions (with gesture navigation, that
swipe is Android's back gesture: add `DRAWER` to the extra keys, see below,
or press Ctrl+Alt+→ on a keyboard):
"New session", a long tap on it for a failsafe session (Android's
`/system/bin/sh`, for when the Nix environment is broken), a long tap on a
session to rename it. A long tap on the text selects it; "More" in the
selection menu has URL selection, sharing the transcript and "Keep screen
on". With a keyboard, Ctrl+Alt+C opens a session and Ctrl+Alt+N/P switch.
The volume down key is Ctrl, volume up with WASD gives the arrow keys.

The extra keys, the cursor and the like are set in
`~/.droid-nix/droid-nix.properties` with Termux's `termux.properties` keys
(see the README), the colors and font with `terminal.colors` and
`terminal.font` in the configuration.

Your home directory also shows up as "droid-nix" in Android's Files app and
in the file picker of other apps.

## Go back to an older generation

```sh
nix-env --profile /nix/var/nix/profiles/droid-nix --list-generations
/nix/var/nix/profiles/droid-nix-1-link/activate
```

The next `droid-nix switch` builds `droid-nix.nix` again, so fix or revert
it first.
