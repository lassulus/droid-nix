# A small derivation that compiles and runs C with gcc from the local store
# (no network, no stdenv): the end-to-end nix build test for the launcher.
#   nix-build build.nix --argstr nonce <unique>   (a new nonce forces a rebuild)
# Store paths are this device's; adjust the hashes for another store.
{ nonce ? "0" }:
let
  sp = builtins.storePath;
  bash = sp "/nix/store/czx7cscpnqqmc9nwmbpnv2zcvck64mmf-bash-5.3p20";
  coreutils = sp "/nix/store/m2ar7c5ylhgg8vp7dfdx9bhjfvki5xdg-coreutils-9.12";
  gcc = sp "/nix/store/6gj4dzcv6gaxh5raqyrq7jmy605qaj3z-gcc-wrapper-16.2.0";
in
derivation {
  name = "sud-hello-c";
  inherit nonce;
  system = "aarch64-linux";
  builder = "${bash}/bin/bash";
  args = [
    "-c"
    ''
      set -eu
      export PATH=${coreutils}/bin:${gcc}/bin
      mkdir -p $out/bin
      printf '#include <stdio.h>\nint main(void){puts("hello from a nix build");return 0;}\n' > hello.c
      gcc -O2 -o $out/bin/hello hello.c
      $out/bin/hello > $out/msg
    ''
  ];
}
