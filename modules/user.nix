# Copyright (c) 2019-2022, see AUTHORS. Licensed under MIT License, see LICENSE.

{ config, lib, pkgs, androidApp, ... }:

with lib;

let
  cfg = config.user;
in

{

  ###### interface

  options = {

    user = {
      group = mkOption {
        type = types.str;
        default = "droid-nix";
        description = "Group name.";
      };

      home = mkOption {
        type = types.path;
        readOnly = true;
        description = "Path to home directory.";
      };

      shell = mkOption {
        type = types.path;
        default = "${pkgs.bashInteractive}/bin/bash";
        defaultText = literalExpression "${pkgs.bashInteractive}/bin/bash";
        description = "Path to login shell.";
      };

      userName = mkOption {
        type = types.str;
        default = "droid-nix";
        description = "User name.";
      };
    };

  };


  ###### implementation

  config = {

    # Android gives every installation its own uid, so /etc/group and
    # /etc/passwd are written here instead of being part of the build.
    build.activation.setUpUser = ''
      cat > /etc/group.tmp <<EOF
      root:x:0:
      ${cfg.group}:x:$(id -g):${cfg.userName}
      EOF
      chmod 644 /etc/group.tmp
      mv --force /etc/group.tmp /etc/group

      cat > /etc/passwd.tmp <<EOF
      root:x:0:0:System administrator:${config.build.installationDir}/root:/bin/sh
      ${cfg.userName}:x:$(id -u):$(id -g):${cfg.userName}:${cfg.home}:${cfg.shell}
      EOF
      chmod 644 /etc/passwd.tmp
      mv --force /etc/passwd.tmp /etc/passwd
    '';

    user = {
      home = "${androidApp.dataDir}/files/home";
    };

  };

}
