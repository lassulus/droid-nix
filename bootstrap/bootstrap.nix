# Copyright (c) 2019-2024, see AUTHORS. Licensed under MIT License, see LICENSE.

{ runCommand, closureInfo, nix, bash, launcher, config, initialPackageInfo }:

let
  closure = closureInfo {
    rootPaths = [ initialPackageInfo.nix initialPackageInfo.busybox ];
  };
in

runCommand "bootstrap" { } ''
  mkdir --parents $out/{bin,dev/shm,etc,root,tmp,usr/lib,nix/store}

  # The closure in a store with store dir /nix/store whose files and state
  # live under $out (a chroot store), registered in its database.
  for i in $(< ${closure}/store-paths); do
    cp --archive "$i" "$out$i"
  done
  export HOME=$TMPDIR
  ${nix}/bin/nix-store --store "$out" --load-db < ${closure}/registration
  # Space nix keeps for the garbage collector; it creates it again on demand.
  rm $out/nix/var/nix/db/reserved
  chmod --recursive u+w $out/nix

  mkdir --parents $out/nix/var/nix/{profiles,gcroots}/per-user/${config.user.userName}

  ln --symbolic ${initialPackageInfo.busybox}/bin/sh $out/bin/sh

  install -D -m 0755 ${launcher}/bin/launcher $out/bin/launcher

  cp ${config.environment.files.login} $out/bin/login
  cp ${config.environment.files.loginInner} $out/usr/lib/login-inner

  ${bash}/bin/bash ${../modules/environment/etc/setup-etc.sh} $out/etc ${config.build.etc}/etc

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
