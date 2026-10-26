# The launcher (launcher.c) as a static aarch64-linux musl binary,
# cross-compiled from x86_64-linux with nixpkgs' gcc cross toolchain. The
# bootstrap ships it and on-device generations refer to the same store path,
# so both take it from here.
{ nixpkgs }:

let
  pkgs = (import nixpkgs { system = "x86_64-linux"; }).pkgsCross.aarch64-multiplatform-musl.pkgsStatic;
in

pkgs.stdenv.mkDerivation {
  name = "launcher";

  src = pkgs.lib.fileset.toSource {
    root = ./.;
    fileset = pkgs.lib.fileset.unions [ ./launcher.c ./stub.S ];
  };

  hardeningDisable = [ "all" ];

  # Non-PIE, static, at a fixed address: the syscall stub must sit at the same
  # address in every process so one seccomp filter can exempt it across
  # execve. 64 GiB is far above where non-PIE (ET_EXEC) targets live (aarch64
  # default 0x400000) and below the mmap area of a 39-bit address space, so
  # the launcher can map ET_EXEC targets at their own link addresses.
  buildPhase = ''
    ${pkgs.stdenv.cc.targetPrefix}cc -O2 -Wall -static -no-pie -fno-pie \
      -Wl,-Ttext-segment=0x1000000000 -o launcher launcher.c stub.S
  '';

  installPhase = "install -D -m 0755 launcher $out/bin/launcher";
}
