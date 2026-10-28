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

## Go back to an older generation

```sh
nix-env --profile /nix/var/nix/profiles/droid-nix --list-generations
/nix/var/nix/profiles/droid-nix-1-link/activate
```

The next `droid-nix switch` builds `droid-nix.nix` again, so fix or revert
it first.
