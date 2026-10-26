# Correctness + macro benchmarks for the launcher, run in the app's FAILSAFE
# (native, no-proot) session:
#   sh /sdcard/Download/sl4/run-native.sh
# /system/bin/sh (toybox) in the app domain; all real work goes through the
# loader. Store paths are this device's; adjust the hashes for another store.
# Loader contract: loader [-b host:guest]... TARGET [argv0 argv1 ...]
# (argv0 = program's argv[0]); the binds are login.nix's, from binds.sh.
usr=/data/data/us.lassul.droidnix/files/usr
d=$usr/nix/sud-loader
mkdir -p $d && cp /sdcard/Download/sl4/loader $d/loader && chmod 755 $d/loader
cp /sdcard/Download/sl4/binds-check.sh $d/binds-check.sh
. /sdcard/Download/sl4/binds.sh
L="$d/loader $B"
exec > /sdcard/Download/sl4/out-native.txt 2>&1

HELLO=/nix/store/dnz35byzl8yw7s81gzqhdq33dds220mx-hello-2.12.3/bin/hello
CO=/nix/store/m2ar7c5ylhgg8vp7dfdx9bhjfvki5xdg-coreutils-9.12/bin
BASH=/nix/store/czx7cscpnqqmc9nwmbpnv2zcvck64mmf-bash-5.3p20/bin/bash
NIX=/nix/store/1r5r28fm7fdsy7ncr4wswb7998d0vswh-nix-2.34.9/bin
GLIBC=/nix/store/nhphqkv18x8fpf3flz0bsm1h554pawn7-glibc-2.44-57
FIND=/nix/store/3jplw390ifmb9rc4kfv9vvxdfi33mrnq-findutils-4.11.0/bin/find
TAR=/nix/store/0xhhzmnjfc0ws5vwkvlnyhkaa8f92v65-gnutar-1.35/bin/tar
GCC=/nix/store/6gj4dzcv6gaxh5raqyrq7jmy605qaj3z-gcc-wrapper-16.2.0/bin/gcc
RGCC=/nix/store/812525ks0y12m1nzrxwlrnq945skg7ll-gcc-16.2.0
PROF=/nix/var/nix/profiles
G=/nix/sud-loader/t   # guest path of the scratch dir $d/t

echo "== context / seccomp (this toybox shell) =="
cat /proc/self/attr/current; echo
while read k v; do case $k in TracerPid:|Seccomp:|Seccomp_filters:) echo "$k $v";; esac; done < /proc/self/status

echo; echo "=== phase 2 correctness ==="
echo "-- hello"; $L $HELLO $HELLO; echo "  exit=$?"
echo "-- bash pipeline + subshell"; $L $BASH bash -c "echo one two | $CO/cat; (echo subshell-\$((3+4)))"; echo "  exit=$?"
echo "-- ls /nix/store"; $L $CO/ls $CO/ls /nix/store | head -3; echo "  exit=$?"
echo "-- ls -l a store file"; $L $CO/ls $CO/ls -l $HELLO; echo "  exit=$?"
echo "-- nix --version"; $L $NIX/nix nix --version; echo "  exit=$?"
echo "-- nix-store --verify-path"; $L $NIX/nix-store nix-store --verify-path $GLIBC && echo "  (valid)"; echo "  exit=$?"
echo "-- /proc/self/exe"; $L $CO/readlink readlink /proc/self/exe; echo "  exit=$?"
echo "-- cd into a store dir, pwd"; $L $BASH bash -c "cd $HELLO/..; $CO/pwd"; echo "  exit=$?"

echo; echo "=== non-PIE (ET_EXEC) ==="
echo "-- real gcc driver --version"; $L $RGCC/bin/gcc gcc --version | head -1; echo "  exit=$?"
echo "-- cc1 --version"; $L $RGCC/libexec/gcc/aarch64-unknown-linux-gnu/16.2.0/cc1 cc1 --version </dev/null | head -1; echo "  exit=$?"
mkdir -p $d/t
printf '#include <stdio.h>\nint main(void){printf("hello from gcc %%d\\n", 6*7);return 42;}\n' > $d/t/t.c
echo "-- gcc-wrapper compile + link"; $L $BASH bash -c "export PATH=$CO; $GCC -O2 -o $G/t.out $G/t.c"; echo "  exit=$?"
echo "-- run the result"; $L $G/t.out; echo "  exit=$?"

echo; echo "=== path canonicalisation ==="
echo "-- host path with an interior /nix symlink (~/.nix-profile)"
$L $CO/ls ls -d $HOME/.nix-profile/bin; echo "  exit=$?"
echo "-- interior chain under /nix (profiles/droid-nix -> -1-link -> /nix/store/...)"
$L $CO/cat cat $PROF/droid-nix/etc/nix/nix.conf; echo "  exit=$?"
echo "-- .. after an absolute /nix symlink: lists the -etc store dir (just 'etc')"
$L $CO/ls ls $PROF/droid-nix/etc/..; echo "  exit=$?"
$L $BASH bash -c "cd -P $PROF/droid-nix/etc/..; $CO/pwd"; echo "  exit=$?"
echo "-- .. above /nix is the guest root (expect /system listed)"
$L $CO/ls ls -d /nix/../system; echo "  exit=$?"
$L $BASH bash -c "cd /nix/..; $CO/pwd; cd /nix; $CO/ls -d ../system"; echo "  exit=$?"
echo "-- relative paths through symlinks from a /nix cwd"
$L $BASH bash -c "cd $PROF; $CO/ls -d droid-nix/etc/nix per-user/droid-nix/profile"; echo "  exit=$?"
echo "-- dirfd-relative walk (find -L follows via fstatat/openat on dirfds)"
$L $FIND find -L $PROF/droid-nix/ -maxdepth 2 -name etc; echo "  exit=$?"
echo "-- execvp PATH search skips a missing dir (needs ENOENT from execve)"
$L $CO/env env PATH=/nonexistent:$CO true; echo "  exit=$?"
echo "-- execve of a missing file returns ENOENT to the caller"
$L $BASH bash -c "/nix/store/nonexistent/bin/x; echo bash-saw=\$?"; echo "  exit=$?"
echo "-- symlink/link/rename/unlink/mkdir/rmdir on /nix paths"
$L $BASH bash -c "
  cd /tmp 2>/dev/null; P=$CO
  \$P/mkdir -p $G/a/b && \$P/ln -s /nix/store/dnz35byzl8yw7s81gzqhdq33dds220mx-hello-2.12.3 $G/a/hl &&
  \$P/ls $G/a/hl/bin && \$P/readlink $G/a/hl &&
  \$P/mv $G/a/hl $G/a/b/hl2 && \$P/ls $G/a/b/hl2/bin/hello &&
  echo x > $G/a/f && \$P/ln -s $G/a/f $G/a/lf && \$P/cat $G/a/lf &&
  \$P/rm $G/a/lf $G/a/b/hl2 && \$P/rmdir $G/a/b && \$P/rm -r $G/a && \$P/ls $G"
echo "  exit=$?"
rm -rf $d/t/a
echo "-- hard link (linkat, both paths): Android denies link() to apps, so expect"
echo "   EACCES like toybox ln without the loader, not ENOENT from a wrong path"
echo x > $d/t/hf; ln $d/t/hf $d/t/hf2 2>&1 | toybox sed 's/.*: //'; echo "  toybox exit=$?"
$L $CO/ln ln $G/hf $G/hf2; echo "  exit=$?"
$L $CO/ln ln $G/missing $G/hf2; echo "  exit=$? (ENOENT expected)"
rm -f $d/t/hf $d/t/hf2

echo; echo "=== binds (phase 4): see binds-native.txt, diffed against binds-proot.txt"
$L $BASH bash /nix/sud-loader/binds-check.sh /sdcard/Download/sl4/binds-native.txt; echo "  exit=$?"
echo "-- DNS: see dns-native.sh"
echo "-- relative path into a bind from cwd / (phase 3 gave ENOENT)"
$L $BASH bash -c "cd /; $CO/ls -d nix/store tmp"; echo "  exit=$?"

echo; echo "=== benchmarks (ms, 3 runs) over glibc-2.44-57 ==="
run() { label=$1; shift; r=""; for i in 1 2 3; do
  s=$(toybox date +%s%N); "$@" >/dev/null 2>&1; e=$(toybox date +%s%N)
  r="$r $(( (e - s) / 1000000 ))"; done; echo "$label:$r"; }
run verify $L $NIX/nix-store nix-store --verify-path $GLIBC
run find   $L $FIND find $GLIBC
run du     $L $CO/du du -s $GLIBC
run tar    $L $TAR tar cf /dev/null $GLIBC
run gcc    $L $BASH bash -c "export PATH=$CO; $GCC -O2 -o $G/t.out $G/t.c"
echo DONE
