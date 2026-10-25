# Copyright (c) 2019-2024, see AUTHORS. Licensed under MIT License, see LICENSE.

{ config, lib, ... }:

with lib;

let
  defaultDroidNixFlake = "github:lassulus/droid-nix";
in

{

  ###### interface

  options = {

    build.flake = {
      droid-nix = mkOption {
        type = types.str;
        default = defaultDroidNixFlake;
        description = "Flake URL of droid-nix, used for the template and the first switch.";
      };

      inputOverrides = mkEnableOption "" // {
        description = ''
          Whether to override the droid-nix input URL in the initial <filename>flake.nix</filename>.
        '';
      };
    };

  };


  ###### implementation

  config = {

    build = {
      initialBuild = true;

      flake.inputOverrides = config.build.flake.droid-nix != defaultDroidNixFlake;
    };

    # /etc/group and /etc/passwd need to be build on target machine because
    # uid and gid need to be determined.
    environment.etc = {
      "group".enable = false;
      "passwd".enable = false;
      "UNINTIALISED".text = "";
    };

  };

}
