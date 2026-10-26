#!/usr/bin/env bash
# proot baseline for hello-native.sh, run in the normal droid-nix (under-proot)
# shell:
#   bash /android/sdcard/Download/sl4/hello-proot.sh
# Same expression and nix, timed the same way (bash arithmetic is 64-bit).
exec > /android/sdcard/Download/sl4/out-hello-proot.txt 2>&1
NIX=/nix/store/1r5r28fm7fdsy7ncr4wswb7998d0vswh-nix-2.34.9/bin
DATE=/nix/store/m2ar7c5ylhgg8vp7dfdx9bhjfvki5xdg-coreutils-9.12/bin/date
d=/nix/sud-loader
cp /android/sdcard/Download/sl4/hello.nix $d/hello.nix
read -r ctx < /proc/self/attr/current; echo "context=$ctx"
while read -r k v; do case $k in TracerPid:|Seccomp:|Seccomp_filters:) echo "$k $v";; esac; done < /proc/self/status
S=$($DATE +%s)
r=""
for i in 1 2 3; do
  s=$($DATE +%s%N)
  $NIX/nix-build $d/hello.nix --argstr nonce proot-$S-$i --option substitute false --no-out-link > $d/hres 2> $d/hlog
  rc=$?
  e=$($DATE +%s%N)
  ms=$(( (e - s) / 1000000 )); r="$r $ms"
  echo "run $i: exit=$rc ms=$ms out=$(< $d/hres)"
  cp $d/hlog /android/sdcard/Download/sl4/hello-proot-$i.log
  tail -5 $d/hlog
  [ $rc = 0 ] && "$(< $d/hres)/bin/hello"
  cat $d/hres >> $d/outs
done
echo "hello nix-build (ms):$r"
echo DONE
