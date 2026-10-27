# Memory of an idle `nix repl` (after its startup) and of the session's bash,
# from /proc/<pid>/smaps_rollup. In the droid-nix shell:
#   bash /android/sdcard/Download/t/mem.sh <label>
# Writes /sdcard/Download/t/mem-<label>.txt (needs the MANAGE_EXTERNAL_STORAGE
# app op).
out=/android/sdcard/Download/t/mem-$1.txt
sleep 40 | nix repl > /dev/null 2>&1 &
pid=$!
sleep 20
{
  echo "== nix repl, pid $pid"
  tr '\0' ' ' < /proc/$pid/cmdline
  echo
  cat /proc/$pid/smaps_rollup
  echo "== bash, pid $$"
  cat /proc/$$/smaps_rollup
} > $out
wait
echo DONE >> $out
