# Installing software

## Add packages to the configuration

Add packages from nixpkgs to `environment.packages` in
`~/.config/droid-nix/droid-nix.nix`:

```nix
{ config, lib, pkgs, ... }:

{
  environment.packages = with pkgs; [
    vim
    git
    gnugrep
    gnused
    findutils
    procps # ps, pgrep, top
    which
    ncurses # clear
  ];

  # Backup etc files instead of failing to activate generation if a file already exists in /etc
  environment.etcBackupExtension = ".bak";

  # Set up nix for flakes
  nix.extraOptions = ''
    experimental-features = nix-command flakes
  '';

  time.timeZone = "Europe/Berlin";
}
```

Then run `droid-nix switch`. On a Pixel 8a this switch downloaded git, its
Perl and Python dependencies and the rest from cache.nixos.org and took 37
seconds. The new commands work in the running shell right away.

Plain `ps` and `ps aux` fail with "Unable to get system boot time" because
Android does not let apps read `/proc/stat`. `ps -eo pid,args`,
`pgrep -af NAME` and `top` work. Every process shows up as
`liblauncher.so` followed by the real command line, so match with `-f`.

## Run a program once

`nix run` and `nix shell` fetch a package without adding it to your
configuration:

```sh
nix run nixpkgs#hello
nix shell nixpkgs#cowsay -c cowsay moo
```

The first `nix run nixpkgs#...` took 31 seconds: `nixpkgs` refers to
nixpkgs-unstable in the global flake registry, and Nix downloads that
channel first. Later runs reuse it (`nix run nixpkgs#hello` again: 0.2
seconds), until Nix checks for a newer version after an hour.

To make `nixpkgs#...` use the same nixpkgs as your configuration (no extra
download, same versions), pin it in the registry in `flake.nix`:

```nix
    droidNixConfigurations.default = droid-nix.lib.droidNixConfiguration {
      pkgs = import nixpkgs { system = "aarch64-linux"; };
      modules = [
        ./droid-nix.nix
        { nix.registry.nixpkgs.flake = nixpkgs; }
      ];
    };
```

## nix profile

Do not use `nix profile add` (or `nix-env -i`) next to droid-nix.
`nix profile add nixpkgs#jq` works, but it converts the user profile, and
every `droid-nix switch` after that fails:

```
Activating installPackages
nix profile remove
error: No packages specified.
```

To recover, roll the user profile back to the generation before the first
`nix profile` command and switch again; the packages added with
`nix profile` are gone after that:

```sh
nix-env --rollback
droid-nix switch
```

Add packages to `environment.packages` instead.

## Search for packages

```sh
nix search nixpkgs ripgrep
```

This evaluates all of nixpkgs on the phone. The first search took 48 seconds
and used 4.2 GB of memory (maximum resident set size) on a Pixel 8a with
8 GB. Nix caches the result: a second search for another name took 6.5
seconds, until nixpkgs changes. On a phone with less memory the search may
be killed. Use https://search.nixos.org/packages instead; the attribute
name shown there is what goes into `environment.packages` or after
`nixpkgs#`.
