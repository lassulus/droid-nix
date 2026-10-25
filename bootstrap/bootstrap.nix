# Copyright (c) 2019-2024, see AUTHORS. Licensed under MIT License, see LICENSE.

{ lib, runCommand, nixDirectory, prootTermux, bash, pkgs, config, initialPackageInfo }:

let
  pinnedProotStatic = import ../modules/environment/login/proot-static-path.nix;
in

# On-device generations refer to proot-termux by a pinned store path, which
# only exists on the device because the bootstrap ships it.
assert lib.assertMsg (pinnedProotStatic == "${prootTermux}") ''
  modules/environment/login/proot-static-path.nix pins
    ${pinnedProotStatic}
  but the bootstrap ships
    ${prootTermux}
  Update the pinned path.
'';

runCommand "bootstrap" { } ''
  mkdir --parents $out/{.l2s,bin,dev/shm,etc,root,tmp,usr/{bin,lib}}
  mkdir --parents $out/nix

  cp --recursive ${nixDirectory}/store $out/nix/store
  cp --recursive ${nixDirectory}/var $out/nix/var
  chmod --recursive u+w $out/nix

  mkdir --parents $out/nix/var/nix/{profiles,gcroots}/per-user/${config.user.userName}

  ln --symbolic ${initialPackageInfo.bash}/bin/sh $out/bin/sh

  install -D -m 0755 ${prootTermux}/bin/proot-static $out/bin/proot-static

  cp ${config.environment.files.login} $out/bin/login
  cp ${config.environment.files.loginInner} $out/usr/lib/login-inner

  ${bash}/bin/bash ${../modules/environment/etc/setup-etc.sh} $out/etc ${config.build.activationPackage}/etc

  cp --dereference --recursive $out/etc/static $out/etc/.static.tmp
  rm $out/etc/static
  mv $out/etc/.static.tmp $out/etc/static

  find $out -executable -type f | sed s@^$out/@@ > $out/EXECUTABLES.txt

  find $out -type l | while read -r LINK; do
    LNK=''${LINK#$out/}
    TGT=$(readlink "$LINK")
    echo "$TGT←$LNK" >> $out/SYMLINKS.txt
    rm "$LINK"
  done
''
