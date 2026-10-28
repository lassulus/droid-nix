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

    # The first generation's login-inner shows it.
    environment.motd = null;

    # /etc/group and /etc/passwd need to be build on target machine because
    # uid and gid need to be determined. The bootstrap only keeps what the
    # first switch needs (nix.conf, DNS, CA certificates); that switch
    # installs the full /etc.
    environment.etc = {
      "group".enable = false;
      "passwd".enable = false;
      "UNINTIALISED".text = "";
      "nix/registry.json".enable = false;
      "profile".enable = false;
      "zshenv".enable = false;
      "services".enable = false;
      "protocols".enable = false;
      "zoneinfo".enable = false;
      "ssl/certs/ca-bundle.crt".enable = false;
      "pki/tls/certs/ca-bundle.crt".enable = false;
    };

  };

}
