# Installing software

## Add packages to the configuration

Add packages from nixpkgs to `environment.packages` in
`~/.config/droid-nix/droid-nix.nix`:

```nix
  environment.packages = with pkgs; [
    vim
    git
    gnugrep
    procps # ps, pgrep, top
  ];
```

and run `droid-nix switch`.

In `ps` and `pgrep`, every process shows up as `liblauncher.so` followed by
the real command line, so match with `-f` (`pgrep -af NAME`).

## Run a program once

```sh
nix run nixpkgs#hello
nix shell nixpkgs#cowsay -c cowsay moo
```

`nixpkgs` here is nixpkgs-unstable from the global flake registry, which
Nix downloads first. To use the nixpkgs of your configuration instead, pin
it in `~/.config/droid-nix/flake.nix`:

```nix
    droidNixConfigurations.default = droid-nix.lib.droidNixConfiguration {
      pkgs = import nixpkgs { system = "aarch64-linux"; };
      modules = [
        ./droid-nix.nix
        { nix.registry.nixpkgs.flake = nixpkgs; }
      ];
    };
```

`nix profile add nixpkgs#jq` also works and survives `droid-nix switch`, but
packages in `environment.packages` are easier to keep track of.

## Search for packages

Use https://search.nixos.org/packages; the attribute name shown there goes
into `environment.packages` or after `nixpkgs#`. `nix search nixpkgs NAME`
works too, but needs several GB of memory.
