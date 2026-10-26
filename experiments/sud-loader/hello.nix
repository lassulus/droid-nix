# GNU hello from source with nixpkgs' stdenv (configure, make, make check,
# make install, fixup): the larger real build for the launcher (phase 4).
#   nix-build hello.nix --argstr nonce <unique>   (a new nonce forces a rebuild)
# nixpkgs is the nixpkgs-unstable source already in this device's store
# (26.11, rev 674a2028); stdenv and the source tarball are substituted.
{ nonce ? "0", nixpkgs ? /nix/store/5h4wc9v7j6jbsi9spqlxvbmr825cbcip-source }:
(import nixpkgs { system = "aarch64-linux"; }).hello.overrideAttrs (_: { inherit nonce; })
