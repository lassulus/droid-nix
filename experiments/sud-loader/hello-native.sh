# GNU hello from source (hello.nix) through the launcher, 3 runs with fresh
# outputs, in the app's FAILSAFE (native, no-proot) session:
#   sh /sdcard/Download/sl4/hello-native.sh
# Once before, substitute stdenv and the source tarball through the launcher
# (phase 4 did this natively, with substitution on):
#   $L $NIX/nix-build nix-build /nix/sud-loader/hello.nix --argstr nonce first --no-out-link
# /system/bin/sh is mksh, whose $(( )) is 32-bit: nanosecond differences
# wrap after 4.29 s, so ms are computed from seconds since the start plus
# the millisecond digits.
usr=/data/data/us.lassul.droidnix/files/usr
d=$usr/nix/sud-loader
cp /sdcard/Download/sl4/loader $d/loader && chmod 755 $d/loader
cp /sdcard/Download/sl4/hello.nix $d/hello.nix
. /sdcard/Download/sl4/binds.sh
L="$d/loader $B"
exec > /sdcard/Download/sl4/out-hello-native.txt 2>&1
unset TMPDIR
NIX=/nix/store/1r5r28fm7fdsy7ncr4wswb7998d0vswh-nix-2.34.9/bin
T0=$(toybox date +%s)
ms() { t=$(toybox date +%s%N); sec=${t%?????????}; ns=${t#$sec}; echo $(( (sec - T0) * 1000 + 1${ns%??????} - 1000 )); }
r=""
for i in 1 2 3; do
  s=$(ms)
  $L $NIX/nix-build nix-build /nix/sud-loader/hello.nix --argstr nonce native-$T0-$i --option substitute false --no-out-link > $d/hres 2> $d/hlog
  rc=$?
  e=$(ms)
  r="$r $((e - s))"
  echo "run $i: exit=$rc ms=$((e - s)) out=$(cat $d/hres)"
  cp $d/hlog /sdcard/Download/sl4/hello-native-$i.log
  grep -v 'GC Warning' $d/hlog | tail -5
  [ $rc = 0 ] && $L $(cat $d/hres)/bin/hello hello
  cat $d/hres >> $d/outs
done
echo "hello nix-build (ms):$r"
echo DONE
