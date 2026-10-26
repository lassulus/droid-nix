# Bind semantics check (phase 4): run with the same bash once under the
# launcher and once under proot; the two outputs must be identical.
#   native: $L $B $BASH bash binds-check.sh OUT    (see run-native.sh)
#   proot:  bash binds-check.sh OUT                (see run-proot.sh)
# Only guest paths are printed, except where a host path is the expected
# answer (link contents, which neither side rewrites).
exec > "$1" 2>&1
export PATH=/nix/store/m2ar7c5ylhgg8vp7dfdx9bhjfvki5xdg-coreutils-9.12/bin
export LC_ALL=C  # the two sessions have different locale settings (ls order)
usr=/data/data/us.lassul.droidnix/files/usr
t() { echo "-- $*"; "$@" 2>&1 | head -4; echo "   rc=${PIPESTATUS[0]}"; }

echo "== /etc, /bin, /usr, /proc fakes"
t cat /etc/resolv.conf
t realpath /etc/resolv.conf
t readlink /etc/static
t cat /etc/nix/nix.conf
t readlink /bin/sh
t /bin/sh -c 'echo sh: $BASH_VERSION'
t ls /usr/lib
t cat /proc/stat
t cat /proc/uptime
t ls -d /proc/self/fd

echo "== /tmp and /dev/shm: guest and host views are the same files"
t rm -rf /tmp/sud-b /dev/shm/sud-b
t mkdir /tmp/sud-b
t touch /tmp/sud-b/f /dev/shm/sud-b
t ls $usr/tmp/sud-b/f $usr/dev/shm/sud-b
t rm /dev/shm/sud-b

echo "== /android is the host root"
t ls -d /android/system/bin/sh /android/data/data/us.lassul.droidnix/files/usr/nix/store
t realpath /android/etc/hosts
t realpath /android/sdcard/.. /android/..

echo "== canonical paths through binds"
t realpath /tmp/.. /nix/../tmp /tmp/sud-b/../../etc/nix /dev/shm/..
t realpath $usr/tmp/sud-b $usr/nix/store
t ln -s /tmp/sud-b /tmp/sud-b/abs
t ln -s ../../etc /tmp/sud-b/rel
t ln -s $usr/tmp/sud-b/f /tmp/sud-b/host
t realpath /tmp/sud-b/abs/f /tmp/sud-b/rel/nix /tmp/sud-b/host
t readlink /tmp/sud-b/abs /tmp/sud-b/rel /tmp/sud-b/host

echo "== cwd (getcwd and /proc/self/cwd are reverse-translated)"
for dir in /tmp/sud-b /etc/nix /dev/shm /usr /android /android/data /nix/store /proc /; do
  cd $dir && echo "cd $dir: pwd=$(pwd -P) cwd=$(readlink /proc/self/cwd)"
done

echo "== relative paths that step into a bind"
cd / && t ls -d tmp/sud-b etc/nix nix/store usr/lib android/system
cd /dev && t ls -d shm
cd /proc && t cat stat
cd /tmp/sud-b && t ls -d ../sud-b/f rel/nix/nix.conf abs/f
cd /tmp && t /nix/store/3jplw390ifmb9rc4kfv9vvxdfi33mrnq-findutils-4.11.0/bin/find sud-b -maxdepth 1 -name abs

echo "== fd paths"
exec 3< /tmp/sud-b/f
t readlink /proc/self/fd/3
exec 3<&-

t rm -r /tmp/sud-b
echo DONE
