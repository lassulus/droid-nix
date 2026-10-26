# One untimed native hello build under a static strace (failsafe session),
# to see whether the build makes hard links at all (Android denies link()
# to apps; proot emulates it with --link2symlink):
#   sh /sdcard/Download/sl4/hello-strace.sh
# strace: pkgsCross.aarch64-multiplatform-musl.pkgsStatic.strace, pushed as
# /sdcard/Download/sl4/strace.
usr=/data/data/us.lassul.droidnix/files/usr
d=$usr/nix/sud-loader
cp /sdcard/Download/sl4/strace $d/strace && chmod 755 $d/strace
. /sdcard/Download/sl4/binds.sh
exec > /sdcard/Download/sl4/out-hello-strace.txt 2>&1
unset TMPDIR
NIX=/nix/store/1r5r28fm7fdsy7ncr4wswb7998d0vswh-nix-2.34.9/bin
$d/strace -f -qq -e trace=linkat,symlinkat -e signal=none -o $d/hello-strace.txt \
  $d/loader $B $NIX/nix-build nix-build /nix/sud-loader/hello.nix --argstr nonce strace-$(toybox date +%s) \
  --option substitute false --no-out-link > $d/hres 2> $d/hlog
echo "exit=$? out=$(cat $d/hres)"
cat $d/hres >> $d/outs
echo "linkat calls: $(grep -c ' linkat(' $d/hello-strace.txt)"
grep ' linkat(' $d/hello-strace.txt | head -20
echo "symlinkat calls: $(grep -c ' symlinkat(' $d/hello-strace.txt)"
grep ' symlinkat(' $d/hello-strace.txt | head -5
echo DONE
