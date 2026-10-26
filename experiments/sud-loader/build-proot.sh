#!/usr/bin/env bash
# proot baseline for build-native.sh, run in the normal droid-nix (under-proot)
# shell:
#   bash /android/sdcard/Download/sl4/build-proot.sh
# Same expression and nix, timed the same way.
exec > /android/sdcard/Download/sl4/out-build-proot.txt 2>&1
NIX=/nix/store/1r5r28fm7fdsy7ncr4wswb7998d0vswh-nix-2.34.9/bin
DATE=/nix/store/m2ar7c5ylhgg8vp7dfdx9bhjfvki5xdg-coreutils-9.12/bin/date
d=/nix/sud-loader
read -r ctx < /proc/self/attr/current; echo "context=$ctx"
while read -r k v; do case $k in TracerPid:|Seccomp:|Seccomp_filters:) echo "$k $v";; esac; done < /proc/self/status
echo "profile nix: $(readlink -f "$(command -v nix-build)")"
S=$($DATE +%s)
r=""
for i in 1 2 3; do
  s=$($DATE +%s%N)
  $NIX/nix-build $d/build.nix --argstr nonce proot-$S-$i --option substitute false --no-out-link > $d/res 2> $d/log
  rc=$?
  e=$($DATE +%s%N)
  ms=$(( (e - s) / 1000000 )); r="$r $ms"
  echo "run $i: exit=$rc ms=$ms out=$(< $d/res)"
  tail -15 $d/log
  [ $rc = 0 ] && cat "$(< $d/res)/msg"
  cat $d/res >> $d/outs
done
echo "nix-build (ms):$r"
echo DONE
