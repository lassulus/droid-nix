# nix-build of build.nix (compiles + runs C with gcc) through the launcher, in
# the app's FAILSAFE (native, no-proot) session:
#   sh /sdcard/Download/sl4/build-native.sh
# Since phase 4 the launcher has login.nix's binds (binds.sh), so nix finds
# /etc/nix/nix.conf and /tmp as under proot: no NIX_CONF_DIR/TMPDIR settings.
# The failsafe session itself exports TMPDIR=/data/local/tmp (not writable by
# the app); it is unset so nix uses its default /tmp, as login-inner's
# environment would.
usr=/data/data/us.lassul.droidnix/files/usr
d=$usr/nix/sud-loader
cp /sdcard/Download/sl4/loader $d/loader && chmod 755 $d/loader
. /sdcard/Download/sl4/binds.sh
L="$d/loader $B"
cp /sdcard/Download/sl4/build.nix $d/build.nix
exec > /sdcard/Download/sl4/out-build-native.txt 2>&1
NIX=/nix/store/1r5r28fm7fdsy7ncr4wswb7998d0vswh-nix-2.34.9/bin
CO=/nix/store/m2ar7c5ylhgg8vp7dfdx9bhjfvki5xdg-coreutils-9.12/bin
unset TMPDIR
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
