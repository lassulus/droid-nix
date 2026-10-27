# A small derivation that compiles and runs C with gcc (from nixpkgs, no
# stdenv): the quick nix build test.
#   nix-build hello-c.nix --argstr nonce <unique>   (a new nonce forces a rebuild)
{ nonce }:
let
  p = (builtins.getFlake "nixpkgs").legacyPackages.aarch64-linux;
in
derivation {
  name = "hello-c";
  inherit nonce;
  system = "aarch64-linux";
  builder = "${p.bash}/bin/bash";
  args = [
    "-c"
    ''
      set -eu
      export PATH=${p.coreutils}/bin:${p.gcc}/bin
      mkdir -p $out/bin
      printf '#include <stdio.h>\nint main(void){puts("hello from a nix build");return 0;}\n' > hello.c
      gcc -O2 -o $out/bin/hello hello.c
      $out/bin/hello > $out/msg
    ''
  ];
}
