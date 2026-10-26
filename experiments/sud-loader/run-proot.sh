#!/usr/bin/env bash
# proot baseline for the same macro benchmarks, run in the normal droid-nix
# (under-proot) shell:
#   bash /android/sdcard/Download/sl/run-proot.sh
# Same store paths and files as run-native.sh, timed the same way.
exec > /android/sdcard/Download/sl/out-proot.txt 2>&1

GLIBC=/nix/store/nhphqkv18x8fpf3flz0bsm1h554pawn7-glibc-2.44-57
NS=/nix/store/1r5r28fm7fdsy7ncr4wswb7998d0vswh-nix-2.34.9/bin/nix-store
FIND=/nix/store/3jplw390ifmb9rc4kfv9vvxdfi33mrnq-findutils-4.11.0/bin/find
DU=/nix/store/m2ar7c5ylhgg8vp7dfdx9bhjfvki5xdg-coreutils-9.12/bin/du
TAR=/nix/store/0xhhzmnjfc0ws5vwkvlnyhkaa8f92v65-gnutar-1.35/bin/tar
DATE=/nix/store/m2ar7c5ylhgg8vp7dfdx9bhjfvki5xdg-coreutils-9.12/bin/date

read -r ctx < /proc/self/attr/current; echo "context=$ctx"
while read -r k v; do case $k in TracerPid:|Seccomp:|Seccomp_filters:) echo "$k $v";; esac; done < /proc/self/status

run() { label=$1; shift; r=""; for i in 1 2 3; do
  s=$($DATE +%s%N); "$@" >/dev/null 2>&1; e=$($DATE +%s%N)
  r="$r $(( (e - s) / 1000000 ))"; done; echo "$label:$r"; }

run verify $NS   --verify-path $GLIBC
run find   $FIND $GLIBC
run du     $DU   -s $GLIBC
run tar    $TAR  cf /dev/null $GLIBC
echo DONE
