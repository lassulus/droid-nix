# Static aarch64 binaries for the in-process /nix interception experiment
# (issue #1), cross-built with the musl toolchain proot is built with.
{ nixpkgs }:

let
  pkgs = (import nixpkgs { system = "x86_64-linux"; }).pkgsCross.aarch64-multiplatform-musl.pkgsStatic;
in

pkgs.stdenv.mkDerivation {
  name = "sud-experiment";
  src = pkgs.lib.fileset.toSource {
    root = ./.;
    fileset = pkgs.lib.fileset.fileFilter (f: f.hasExt "c" || f.hasExt "h" || f.hasExt "S" || f.name == "Makefile") ./.;
  };
  hardeningDisable = [ "all" ];
  makeFlags = [ "CC=${pkgs.stdenv.cc.targetPrefix}cc" "PREFIX=$(out)" ];
}
