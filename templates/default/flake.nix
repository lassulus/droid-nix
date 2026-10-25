{
  description = "droid-nix configuration";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    droid-nix = {
      url = "github:lassulus/droid-nix";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { nixpkgs, droid-nix, ... }: {

    droidNixConfigurations.default = droid-nix.lib.droidNixConfiguration {
      pkgs = import nixpkgs { system = "aarch64-linux"; };
      modules = [ ./droid-nix.nix ];
      # To use home-manager.config, add a home-manager input and pass
      # home-manager-path = home-manager.outPath;
    };

  };
}
