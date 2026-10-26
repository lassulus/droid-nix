# nix-build of build.nix (compiles + runs C with gcc) through the launcher, in
# the app's FAILSAFE (native, no-proot) session:
#   sh /sdcard/Download/sl3/build-native.sh
# The native session has no proot /etc bind, so nix gets its config from the
# droid-nix etc store path directly (NIX_CONF_DIR), and TMPDIR is the
# directory proot binds to /tmp (the failsafe default /data/local/tmp is not
# writable by the app).
usr=/data/data/us.lassul.droidnix/files/usr
d=$usr/nix/sud-loader
L=$d/loader
cp /sdcard/Download/sl3/loader $L && chmod 755 $L
cp /sdcard/Download/sl3/build.nix $d/build.nix
exec > /sdcard/Download/sl3/out-build-native.txt 2>&1
NIX=/nix/store/1r5r28fm7fdsy7ncr4wswb7998d0vswh-nix-2.34.9/bin
CO=/nix/store/m2ar7c5ylhgg8vp7dfdx9bhjfvki5xdg-coreutils-9.12/bin
export NIX_CONF_DIR=/nix/store/mq71jmkk4yig16piqpn4wr6z8q6gbczm-etc/etc/nix
export TMPDIR=$usr/tmp
S=$(toybox date +%s)
r=""
for i in 1 2 3; do
  s=$(toybox date +%s%N)
  $L $NIX/nix-build nix-build /nix/sud-loader/build.nix --argstr nonce native-$S-$i --option substitute false --no-out-link > $d/res 2> $d/log
  rc=$?
  e=$(toybox date +%s%N)
  ms=$(( (e - s) / 1000000 )); r="$r $ms"
  echo "run $i: exit=$rc ms=$ms out=$(cat $d/res)"
  grep -v 'GC Warning' $d/log | tail -15
  [ $rc = 0 ] && $L $CO/cat cat $(cat $d/res)/msg
  cat $d/res >> $d/outs
done
echo "nix-build (ms):$r"
echo DONE
