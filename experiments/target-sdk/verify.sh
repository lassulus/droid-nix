# The device checks for the target 36 app, in the droid-nix shell (bash):
#   bash /android/sdcard/Download/t/verify.sh
# with hello-c.nix and hello.nix next to it. Writes verify.txt there (needs
# the MANAGE_EXTERNAL_STORAGE app op). GNU hello is built once with
# substitution (stdenv, source), then 3 times from source without.
d=/android/sdcard/Download/t
exec > $d/verify.txt 2>&1
ms() { local t=$EPOCHREALTIME; echo $(( ${t/./} / 1000 )); }
run() {
  local s=$(ms)
  "$@"
  local rc=$?
  echo "-> exit $rc, $(( $(ms) - s )) ms"
}

echo "== session"
cat /proc/self/attr/current; echo
while read -r k v; do
  case $k in TracerPid:|Seccomp_filters:) echo "$k $v" ;; esac
done < /proc/self/status
tr '\0' ' ' < /proc/$$/cmdline | cut -c1-120; echo

echo "== nix --version"
run nix --version
echo "== nix run nixpkgs#hello"
run nix run nixpkgs#hello
echo "== small C build"
run nix-build $d/hello-c.nix --argstr nonce c-$EPOCHSECONDS --no-out-link
echo "== GNU hello, substituting stdenv and the source"
run nix-build $d/hello.nix --argstr nonce first-$EPOCHSECONDS --no-out-link
for i in 1 2 3; do
  echo "== GNU hello from source, run $i"
  s=$(ms)
  nix-build $d/hello.nix --argstr nonce run-$EPOCHSECONDS-$i --option substitute false \
    --no-out-link > /tmp/hres 2> /tmp/hlog
  echo "-> exit $?, $(( $(ms) - s )) ms"
  tail -3 /tmp/hlog
  "$(cat /tmp/hres)/bin/hello"
done
echo DONE
