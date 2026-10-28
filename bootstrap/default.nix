# Copyright (c) 2019-2024, see AUTHORS. Licensed under MIT License, see LICENSE.

# The aarch64 bootstrap zip, assembled on nativeSystem: a static nix and a
# static busybox (/bin/sh) in a registered store, CA certificates and the
# login scripts of the initial build. Everything else comes from
# cache.nixos.org on the first login. The launcher is built here too, but
# the APK ships it as a native library (../app).
{ nixpkgs
, nativeSystem  # system to assemble the bootstrap on
, droidNixFlakeURL ? null
}:

let
  pkgs = import nixpkgs { system = nativeSystem; };

  # Static aarch64 binaries, cross-compiled with gcc against musl (there is
  # no static aarch64 nix in cache.nixos.org).
  static = pkgs.pkgsCross.aarch64-multiplatform-musl.pkgsStatic;

  # The nix command alone: nix-everything also pulls in nix-nswrapper (12 MB),
  # which the app cannot use (no user namespaces, sandbox = false).
  nix = (static.nixVersions.nixComponents_2_34.overrideScope (_: prev: {
    nix-util = prev.nix-util.overrideAttrs (old: {
      patches = old.patches or [ ] ++ [ ./nix-util-syscall-h.patch ];
    });
    # Its symbol table is 4 MB of the static binary.
    nix-cli = prev.nix-cli.overrideAttrs { stripAllList = [ "bin" ]; };
  })).nix-cli;

  initialPackageInfo = {
    inherit nix;
    inherit (static) busybox;
  };

  urlOptionValue = url: envVar:
    let
      envValue = builtins.getEnv envVar;
    in
    pkgs.lib.mkIf
      (envValue != "" || url != null)
      (if url == null then envValue else url);

  modules = import ../modules {
    inherit pkgs;

    config = {
      imports = [ ../modules/build/initial-build.nix ];

      _module.args = { inherit initialPackageInfo; };

      # login-inner execs the first generation's login-inner instead.
      user.shell = "${initialPackageInfo.busybox}/bin/sh";

      build.flake.droid-nix = urlOptionValue droidNixFlakeURL "DROID_NIX_FLAKE_URL";
    };
  };
in

rec {
  launcher = static.callPackage ./launcher { };

  bootstrap = pkgs.callPackage ./bootstrap.nix {
    inherit (modules) config;
    inherit initialPackageInfo;
  };

  bootstrapZip = pkgs.callPackage ./bootstrap-zip.nix {
    inherit bootstrap;
    targetSystem = "aarch64-linux";
  };
}
