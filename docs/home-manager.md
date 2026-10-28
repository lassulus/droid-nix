# home-manager, git and an editor

## Without home-manager

```nix
  environment.packages = with pkgs; [ vim git ];
  environment.sessionVariables.EDITOR = "vim";
```

and configure git as usual (`git config --global user.name "Jane Doe"`).

## With home-manager

Add home-manager as an input in `~/.config/droid-nix/flake.nix` and pass it
on:

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

Then run `droid-nix switch`. "User systemd daemon not running. Skipping
reload." is expected.
