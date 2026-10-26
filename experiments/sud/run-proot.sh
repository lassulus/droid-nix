#!/usr/bin/env bash
# Run inside the droid-nix shell (under proot):
#   bash /android/sdcard/Download/sud/run-proot.sh
# Copies the probes to /nix/sud-exp (= files/usr/nix/sud-exp, also usable from
# the failsafe session) and writes results to /android/sdcard/Download/sud/out-proot.txt.
src=/android/sdcard/Download/sud
dir=/nix/sud-exp
out=$src/out-proot.txt
mkdir -p $dir
for p in probe_sud probe_seccomp probe_mmap0 probe_interp bench; do
  cp $src/$p $dir/$p && chmod 755 $dir/$p
done
exec > $out 2>&1
read -r ctx < /proc/self/attr/current
echo "context=$ctx"
while read -r k v; do
  case $k in TracerPid:|Seccomp:|Seccomp_filters:) echo "$k $v" ;; esac
done < /proc/self/status
files=(/nix/store/*-bash-*/bin/bash)
file=${files[0]}
echo "store-file=$file"
cd $dir
./probe_sud
./probe_seccomp
./probe_mmap0
./probe_interp $dir
./bench plain "" "$file"
./bench seccomp "" "$file"
echo DONE
