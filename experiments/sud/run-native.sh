# Run in the app's failsafe session (/system/bin/sh in the app domain, no proot):
#   sh /sdcard/Download/sud/run-native.sh
# Needs run-proot.sh to have copied the probes to files/usr/nix/sud-exp first.
usr=/data/data/us.lassul.droidnix/files/usr
dir=$usr/nix/sud-exp
out=/sdcard/Download/sud/out-native.txt
exec > $out 2>&1
echo "context=$(cat /proc/self/attr/current)"
grep -E '^(TracerPid|Seccomp|Seccomp_filters):' /proc/self/status
file=$(cd $usr && ls -d nix/store/*-bash-*/bin/bash | head -1)
file=/$file
echo "store-file=$file"
cd $dir
./probe_sud
./probe_seccomp
./probe_mmap0
./probe_interp $dir
./bench plain $usr "$file"
./bench seccomp $usr "$file"
echo DONE
