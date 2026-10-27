# The probe as a static aarch64 musl binary, cross-built like the launcher.
{ nixpkgs }:

let
  pkgs = (import nixpkgs { system = "x86_64-linux"; }).pkgsCross.aarch64-multiplatform-musl.pkgsStatic;
in

pkgs.stdenv.mkDerivation {
  name = "target-sdk-probe";
  src = pkgs.lib.fileset.toSource {
    root = ./.;
    fileset = ./probe.c;
  };
  hardeningDisable = [ "all" ];
  buildPhase = "${pkgs.stdenv.cc.targetPrefix}cc -O2 -Wall -static -o probe probe.c";
  installPhase = "install -D -m 0755 probe $out/bin/probe";
}
