# Copyright (c) 2019-2024, see AUTHORS. Licensed under MIT License, see LICENSE.

{ config, lib, pkgs, initialPackageInfo, ... }:

with lib;

let
  login = pkgs.callPackage ./login.nix { inherit config; };

  loginInner = pkgs.callPackage ./login-inner.nix {
    inherit config initialPackageInfo;
  };
in

{

  ###### interface

  options = {

    environment.loginHook = mkOption {
      type = types.lines;
      default = "";
      internal = true;
      description = ''
        Shell code run at every login (a new session, RUN_COMMAND, or the
        app's login without a session at boot) and after every switch, e.g.
        to start background programs: Android has no service manager. It
        must not block or fail.
      '';
    };

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
    };

  };


  ###### implementation

  config = {

    build.activation = {
      installLogin = ''
        if ! diff /bin/login ${login} > /dev/null; then
          mkdir --parents /bin
          cp ${login} /bin/.login.tmp
          chmod u+w /bin/.login.tmp
          mv /bin/.login.tmp /bin/login
        fi
      '';

      # A link to this generation's login-inner, replaced atomically: running
      # sessions keep reading the file they opened, the next one gets this.
      installLoginInner = ''
        mkdir --parents /usr/lib
        ln --symbolic --force ${loginInner} /usr/lib/.login-inner.tmp
        mv /usr/lib/.login-inner.tmp /usr/lib/login-inner
      '';
    };

    build.activationAfter = mkIf (config.environment.loginHook != "") {
      inherit (config.environment) loginHook;
    };

    environment.files = {
      inherit login loginInner;
    };

  };

}
