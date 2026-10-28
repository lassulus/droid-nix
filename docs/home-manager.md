# home-manager, git and an editor

## Without home-manager

Add `git` and an editor to `environment.packages` and set `EDITOR` in
`~/.config/droid-nix/droid-nix.nix`:

```nix
  environment.packages = with pkgs; [ vim git ];
  environment.sessionVariables.EDITOR = "vim";
```

Configure git as usual, e.g. `git config --global user.name "Jane Doe"`.
New sessions see the new `EDITOR`.

## With home-manager

droid-nix does not bring home-manager itself. Add it as an input in
`~/.config/droid-nix/flake.nix` and pass it on:

```nix
{
  description = "droid-nix configuration";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    droid-nix = {
      url = "github:lassulus/droid-nix";
      inputs.nixpkgs.follows = "nixpkgs";
    };

    home-manager = {
      url = "github:nix-community/home-manager";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { nixpkgs, droid-nix, home-manager, ... }: {

    droidNixConfigurations.default = droid-nix.lib.droidNixConfiguration {
      pkgs = import nixpkgs { system = "aarch64-linux"; };
      modules = [
        ./droid-nix.nix
        ./home.nix
      ];
      home-manager-path = home-manager.outPath;
    };

  };
}
```

`~/.config/droid-nix/home.nix`:

```nix
{ ... }:

{
  home-manager.useGlobalPkgs = true;
  home-manager.backupFileExtension = "hm-bak";

  home-manager.config = { pkgs, ... }: {
    home.stateVersion = "26.05";

    programs.git = {
      enable = true;
      settings.user.name = "Jane Doe";
      settings.user.email = "jane@example.org";
    };

    home.sessionVariables.EDITOR = "vim";
  };
}
```

Then `droid-nix switch`. The output ends with home-manager's activation
("Starting Home Manager activation", "Creating home file links in ...");
the switch took 12 seconds after the inputs were fetched, including
home-manager's manual page. `git config user.name` prints `Jane Doe`, and
`~/.config/git/config` links to the store. "User systemd daemon not
running. Skipping reload." is expected.
