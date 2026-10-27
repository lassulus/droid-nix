# Copyright (c) 2019-2022, see AUTHORS. Licensed under MIT License, see LICENSE.

{ config, lib, pkgs, ... }:

with lib;

let
  cfg = config.environment;
in

{

  ###### interface

  options = {

    environment = {
      binSh = mkOption {
        type = types.str;
        readOnly = true;
        description = "Path to <filename>/bin/sh</filename> executable.";
      };

      usrBinEnv = mkOption {
        type = types.str;
        readOnly = true;
        description = "Path to <filename>/usr/bin/env</filename> executable.";
      };
    };

  };


  ###### implementation

  config = {

    build.activationBefore = {
      linkBinSh = ''
        mkdir --parents /bin
        ln --symbolic --force ${cfg.binSh} /bin/.sh.tmp
        mv /bin/.sh.tmp /bin/sh
      '';

      linkUsrBinEnv = ''
        mkdir --parents /usr/bin
        ln --symbolic --force ${cfg.usrBinEnv} /usr/bin/.env.tmp
        mv /usr/bin/.env.tmp /usr/bin/env
      '';
    };

    environment = {
      binSh = "${pkgs.bashInteractive}/bin/sh";
      usrBinEnv = "${pkgs.coreutils}/bin/env";
    };

  };

}
