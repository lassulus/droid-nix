# Copyright (c) 2019-2024, see AUTHORS. Licensed under MIT License, see LICENSE.

{ config, lib, pkgs, initialPackageInfo, ... }:

with lib;

let
  cfg = config.environment.files;

  login = pkgs.callPackage ./login.nix { inherit config; };

  loginInner = pkgs.callPackage ./login-inner.nix {
    inherit config initialPackageInfo;
  };
in

{

  ###### interface

  options = {

    environment.files = {
      login = mkOption {
        type = types.package;
        readOnly = true;
        internal = true;
        description = "Login script.";
      };

      loginInner = mkOption {
        type = types.package;
        readOnly = true;
        internal = true;
        description = "Login-inner script.";
      };

      launcher = mkOption {
        type = types.package;
        # Set to bootstrap/launcher by the bootstrap and by
        # lib.droidNixConfiguration, so that on-device generations refer to
        # the launcher store path the bootstrap ships.
        internal = true;
        description = "<literal>launcher</literal> package.";
      };
    };

  };


  ###### implementation

  config = {

    build.activation = {
      installLogin = ''
        if ! diff /bin/login ${login} > /dev/null; then
          $DRY_RUN_CMD mkdir $VERBOSE_ARG --parents /bin
          $DRY_RUN_CMD cp $VERBOSE_ARG ${login} /bin/.login.tmp
          $DRY_RUN_CMD chmod $VERBOSE_ARG u+w /bin/.login.tmp
          $DRY_RUN_CMD mv $VERBOSE_ARG /bin/.login.tmp /bin/login
        fi
      '';

      installLoginInner = ''
        if (test -e /usr/lib/.login-inner.new && ! diff /usr/lib/.login-inner.new ${loginInner} > /dev/null) || \
            (! test -e /usr/lib/.login-inner.new && ! diff /usr/lib/login-inner ${loginInner} > /dev/null); then
          $DRY_RUN_CMD mkdir $VERBOSE_ARG --parents /usr/lib
          $DRY_RUN_CMD cp $VERBOSE_ARG ${loginInner} /usr/lib/.login-inner.tmp
          $DRY_RUN_CMD chmod $VERBOSE_ARG u+w /usr/lib/.login-inner.tmp
          $DRY_RUN_CMD mv $VERBOSE_ARG /usr/lib/.login-inner.tmp /usr/lib/.login-inner.new
        fi
      '';

      # Every execve in a session re-execs /bin/launcher, and the session's
      # seccomp filter only exempts the syscall stub of the launcher it
      # started with. So login replaces it only when no launcher runs.
      installLauncher = ''
        if (test -e /bin/.launcher.new && ! diff /bin/.launcher.new ${cfg.launcher}/bin/launcher > /dev/null) || \
            (! test -e /bin/.launcher.new && ! diff /bin/launcher ${cfg.launcher}/bin/launcher > /dev/null); then
          $DRY_RUN_CMD mkdir $VERBOSE_ARG --parents /bin
          $DRY_RUN_CMD cp $VERBOSE_ARG ${cfg.launcher}/bin/launcher /bin/.launcher.tmp
          $DRY_RUN_CMD chmod $VERBOSE_ARG u+w /bin/.launcher.tmp
          $DRY_RUN_CMD mv $VERBOSE_ARG /bin/.launcher.tmp /bin/.launcher.new
        fi
      '';
    };

    environment.files = {
      inherit login loginInner;
    };

  };

}
