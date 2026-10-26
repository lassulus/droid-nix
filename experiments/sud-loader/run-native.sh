# Correctness + macro benchmarks for the launcher, run in the app's FAILSAFE
# (native, no-proot) session:
#   sh /sdcard/Download/sl/run-native.sh
# /system/bin/sh (toybox) in the app domain; all real work goes through the
# loader. Store paths are this device's; adjust the hashes for another store.
# Loader contract: loader TARGET [argv0 argv1 ...]   (argv0 = program's argv[0])
usr=/data/data/us.lassul.droidnix/files/usr
L=$usr/nix/sud-loader/loader
cp /sdcard/Download/sl/loader $L && chmod 755 $L
exec > /sdcard/Download/sl/out-native.txt 2>&1

HELLO=/nix/store/dnz35byzl8yw7s81gzqhdq33dds220mx-hello-2.12.3/bin/hello
CO=/nix/store/m2ar7c5ylhgg8vp7dfdx9bhjfvki5xdg-coreutils-9.12/bin
BASH=/nix/store/czx7cscpnqqmc9nwmbpnv2zcvck64mmf-bash-5.3p20/bin/bash
NIX=/nix/store/1r5r28fm7fdsy7ncr4wswb7998d0vswh-nix-2.34.9/bin
GLIBC=/nix/store/nhphqkv18x8fpf3flz0bsm1h554pawn7-glibc-2.44-57
FIND=/nix/store/3jplw390ifmb9rc4kfv9vvxdfi33mrnq-findutils-4.11.0/bin/find
TAR=/nix/store/0xhhzmnjfc0ws5vwkvlnyhkaa8f92v65-gnutar-1.35/bin/tar

echo "== context / seccomp (this toybox shell) =="
cat /proc/self/attr/current; echo
while read k v; do case $k in TracerPid:|Seccomp:|Seccomp_filters:) echo "$k $v";; esac; done < /proc/self/status

echo; echo "=== correctness ==="
echo "-- hello"; $L $HELLO $HELLO; echo "  exit=$?"
echo "-- bash pipeline + subshell"; $L $BASH bash -c "echo one two | $CO/cat; (echo subshell-\$((3+4)))"; echo "  exit=$?"
echo "-- ls /nix/store"; $L $CO/ls $CO/ls /nix/store | head -3; echo "  exit=$?"
echo "-- ls -l a store file"; $L $CO/ls $CO/ls -l $HELLO; echo "  exit=$?"
echo "-- nix --version"; $L $NIX/nix nix --version; echo "  exit=$?"
echo "-- nix-store --verify-path"; $L $NIX/nix-store nix-store --verify-path $GLIBC && echo "  (valid)"; echo "  exit=$?"
echo "-- /proc/self/exe"; $L $CO/readlink readlink /proc/self/exe; echo "  exit=$?"
echo "-- gcc (non-PIE target, expect clean error)"; $L /nix/store/812525ks0y12m1nzrxwlrnq945skg7ll-gcc-16.2.0/bin/gcc gcc --version; echo "  exit=$?"

echo; echo "=== benchmarks (ms, 3 runs) over glibc-2.44-57 ==="
run() { label=$1; shift; r=""; for i in 1 2 3; do
  s=$(toybox date +%s%N); "$@" >/dev/null 2>&1; e=$(toybox date +%s%N)
  r="$r $(( (e - s) / 1000000 ))"; done; echo "$label:$r"; }
run verify $L $NIX/nix-store nix-store --verify-path $GLIBC
run find   $L $FIND find $GLIBC
run du     $L $CO/du du -s $GLIBC
run tar    $L $TAR tar cf /dev/null $GLIBC
echo DONE
