# Copyright (c) 2019-2022, see AUTHORS. Licensed under MIT License, see LICENSE.

{ config, lib, pkgs, ... }:

with lib;

let
  cfg = config.build;

  profileDirectory = "/nix/var/nix/profiles/droid-nix";

  # Programs that always should be available on the activation
  # script's PATH.
  activationBinPaths = lib.makeBinPath [
    pkgs.bash
    pkgs.coreutils
    pkgs.diffutils
    pkgs.findutils
    pkgs.gnugrep
    pkgs.gnused
    pkgs.ncurses # For `tput`.
    config.nix.package
  ];

  mkActivationCmds = activation: concatStringsSep "\n" (
    mapAttrsToList
      (name: value: ''
        noteEcho "Activating ${name}"
        ${value}
      '')
      activation
  );

  activationScript = pkgs.writeScript "activation-script" ''
    #!${pkgs.runtimeShell}

    set -eu
    set -o pipefail

    export PATH="${activationBinPaths}"
    _NOD_GENERATION_DIR="$(realpath "$(dirname "$0")")"
    cd "$HOME"

    ${builtins.readFile ../lib-bash/color-echo.sh}

    ${mkActivationCmds cfg.activationBefore}
    ${mkActivationCmds cfg.activation}
    ${mkActivationCmds cfg.activationAfter}
  '';
in

{

  ###### interface

  options = {

    build = {
      activation = mkOption {
        default = { };
        type = types.attrs;
        description = ''
          Activation scripts for the droid-nix environment.
        '';
      };

      activationBefore = mkOption {
        default = { };
        type = types.attrs;
        description = ''
          Activation scripts for the droid-nix environment that
          need to be run first.
        '';
      };

      activationAfter = mkOption {
        default = { };
        type = types.attrs;
        description = ''
          Activation scripts for the droid-nix environment that
          need to be run last.
        '';
      };

      activationPackage = mkOption {
        type = types.package;
        readOnly = true;
        internal = true;
        description = "Derivation with activation script.";
      };

      etc = mkOption {
        type = types.package;
        internal = true;
        description = "Package containing /etc files.";
      };

      sessionInit = mkOption {
        type = types.package;
        internal = true;
        description = ''
          Package containing the session-init script in
          <code>/etc/profile.d/droid-nix-session-init.sh</code>.
        '';
      };
    };

  };


  ###### implementation

  config = {

    build = {
      activationAfter.linkProfile = ''
        generationDir="$(dirname $0)"

        if [[ $generationDir =~ ^${profileDirectory}-([0-9]+)-link$ ]]; then
          nix-env --profile "${profileDirectory}" --switch-generation "''${BASH_REMATCH[1]}"
        else
          nix-env --profile "${profileDirectory}" --set "$_NOD_GENERATION_DIR"
        fi
      '';

      activationPackage =
        pkgs.runCommand
          "droid-nix-generation"
          {
            preferLocalBuild = true;
            allowSubstitutes = false;
          }
          ''
            mkdir --parents $out/filesystem/{bin,usr/{bin,lib}}

            cp ${activationScript} $out/activate

            ln --symbolic ${config.build.etc}/etc $out/etc
            ln --symbolic ${config.environment.path} $out/droid-nix-path

            ln --symbolic ${config.environment.files.login} $out/filesystem/bin/login
            ln --symbolic ${config.environment.files.loginInner} $out/filesystem/usr/lib/login-inner

            ln --symbolic ${config.environment.binSh} $out/filesystem/bin/sh
            ln --symbolic ${config.environment.usrBinEnv} $out/filesystem/usr/bin/env
          '';
    };

  };

}
