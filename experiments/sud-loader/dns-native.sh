# DNS in the native session (failsafe): substitute glibc's getent from
# cache.nixos.org through the launcher, then resolve with it. Both need
# /etc/resolv.conf, which only the /etc bind provides.
#   sh /sdcard/Download/sl4/dns-native.sh
usr=/data/data/us.lassul.droidnix/files/usr
d=$usr/nix/sud-loader
. /sdcard/Download/sl4/binds.sh
L="$d/loader $B"
exec > /sdcard/Download/sl4/out-dns-native.txt 2>&1
unset TMPDIR
NIX=/nix/store/1r5r28fm7fdsy7ncr4wswb7998d0vswh-nix-2.34.9/bin
NIXPKGS=/nix/store/5h4wc9v7j6jbsi9spqlxvbmr825cbcip-source
echo "-- nix-build -A getent (substitutes)"
$L $NIX/nix-build nix-build $NIXPKGS -A getent --no-out-link > $d/gres 2> $d/glog
echo "  exit=$? out=$(cat $d/gres)"
grep -v 'GC Warning' $d/glog | tail -5
cat $d/gres >> $d/outs
G=$(cat $d/gres)/bin/getent
echo "-- getent hosts cache.nixos.org github.com"
$L $G getent hosts cache.nixos.org github.com; echo "  exit=$?"
echo "-- getent ahostsv4 example.org"
$L $G getent ahostsv4 example.org | head -2; echo "  exit=$?"
echo "-- getent passwd droid-nix (/etc/passwd from the bind)"
$L $G getent passwd droid-nix; echo "  exit=$?"
echo "-- the same without the /etc bind (phase 3 view): no /etc/resolv.conf"
$d/loader -b $usr/nix:/nix $G getent hosts cache.nixos.org; echo "  exit=$?"
echo DONE
