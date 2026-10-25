# Copyright (c) 2019-2024, see AUTHORS. Licensed under MIT License, see LICENSE.

{ pkgs
, home-manager-path
}:

[
  ./build/activation.nix
  ./build/config.nix
  ./environment/android-integration.nix
  ./environment/ca.nix
  ./environment/etc
  ./environment/links.nix
  ./environment/login
  ./environment/networking.nix
  ./environment/nix.nix
  ./environment/path.nix
  ./environment/session-init.nix
  ./environment/shell.nix
  ./home-manager.nix
  ./terminal.nix
  ./time.nix
  ./user.nix
  (pkgs.path + "/nixos/modules/misc/assertions.nix")

  {
    _file = ./module-list.nix;
    _module.args = {
      inherit home-manager-path;
      androidApp = import ../android-app.nix;
      pkgs = pkgs.lib.mkDefault pkgs;
    };
  }
]
