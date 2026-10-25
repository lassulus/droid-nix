# Copyright (c) 2019-2024, see AUTHORS. Licensed under MIT License, see LICENSE.

{ pkgs
, config
, extraSpecialArgs ? { }
, home-manager-path ? null
}:

with pkgs.lib;

let
  droidNixModules = import ./module-list.nix {
    inherit pkgs home-manager-path;
  };

  rawModule = evalModules {
    modules = [ config ] ++ droidNixModules;
    specialArgs = extraSpecialArgs;
    class = "droidNix";
  };

  failedAssertions = map (x: x.message) (filter (x: !x.assertion) rawModule.config.assertions);

  module =
    if failedAssertions != [ ]
    then throw "\nFailed assertions:\n${concatMapStringsSep "\n" (x: "- ${x}") failedAssertions}"
    else showWarnings rawModule.config.warnings rawModule;
in

{
  inherit (module.config.build) activationPackage;
  inherit (module) config options;
  inherit pkgs;
}
