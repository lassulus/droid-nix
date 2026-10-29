#!@runtimeShell@

# Wireless debugging listens on a new port after every Wi-Fi connect.
# After a reconnect the old service can still be announced with a dead
# port: try each until one is connected.

ports=$(@out@/libexec/adb-ports) || exit 1
for port in $ports; do
  out=$(@adb@ connect "localhost:$port")
  if @adb@ -s "localhost:$port" get-state > /dev/null 2>&1; then
    echo "$out"
    # Drop the ports of earlier Wi-Fi connections, or plain `adb shell`
    # finds more than one device.
    @adb@ devices | while read -r serial state; do
      case $serial in
        "localhost:$port") ;;
        localhost:*) @adb@ disconnect "$serial" > /dev/null ;;
      esac
    done
    exit 0
  fi
  echo "$out" >&2
done
exit 1
