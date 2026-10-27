# Copyright (c) 2019-2024, see AUTHORS. Licensed under MIT License, see LICENSE.

# The aarch64 bootstrap zip, assembled on nativeSystem. Its packages come from
# the aarch64-linux nixpkgs binary cache, except the launcher (see launcher/).
{ nixpkgs
, nativeSystem  # system to assemble the bootstrap on
, droidNixFlakeURL ? null
}:

let
  system = "aarch64-linux"; # system to compile for
  nixDirectory = callPackage ./nix-directory.nix { inherit system; };
  initialPackageInfo = import "${nixDirectory}/nix-support/package-info.nix";

  pkgs = import nixpkgs { system = nativeSystem; };

  urlOptionValue = url: envVar:
    let
      envValue = builtins.getEnv envVar;
    in
    pkgs.lib.mkIf
      (envValue != "" || url != null)
      (if url == null then envValue else url);

  modules = import ../modules {
    inherit pkgs;

    config = {
      imports = [ ../modules/build/initial-build.nix ];

      _module.args = { inherit initialPackageInfo; };

      # Fix invoking bash after initial build.
      user.shell = "${initialPackageInfo.bash}/bin/bash";

      build.flake.droid-nix = urlOptionValue droidNixFlakeURL "DROID_NIX_FLAKE_URL";
    };
  };

  callPackage = pkgs.lib.callPackageWith (
    pkgs // customPkgs // {
      inherit (modules) config;
      inherit callPackage nixpkgs nixDirectory initialPackageInfo;
      targetSystem = system;
    }
  );

  customPkgs = {
    bootstrap = callPackage ./bootstrap.nix { };
    bootstrapZip = callPackage ./bootstrap-zip.nix { };
    launcher = import ./launcher { inherit nixpkgs; };
  };
in

customPkgs
