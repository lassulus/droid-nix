# GNU hello from source with nixpkgs' stdenv (configure, make, make check,
# make install, fixup), as in experiments/sud-loader/hello.nix.
#   nix-build hello.nix --argstr nonce <unique>   (a new nonce forces a rebuild)
{ nonce }:
(builtins.getFlake "nixpkgs").legacyPackages.aarch64-linux.hello.overrideAttrs (_: { inherit nonce; })
