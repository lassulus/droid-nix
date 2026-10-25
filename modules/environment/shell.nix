# Copyright (c) 2019-2021, see AUTHORS. Licensed under MIT License, see LICENSE.

{ config, lib, pkgs, ... }:

with lib;

{

  ###### interface

  options = { };


  ###### implementation

  config = {

    environment.etc = {
      "profile".text = ''
        . "${config.build.sessionInit}/etc/profile.d/droid-nix-session-init.sh"
      '';

      "zshenv".text = ''
        . "${config.build.sessionInit}/etc/profile.d/droid-nix-session-init.sh"
      '';
    };

  };

}
