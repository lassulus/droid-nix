{ runCommandCC
, runtimeShell
, adb
}:

# The adb in nixpkgs is built without mDNS; adb-ports.c finds the ports.
runCommandCC "droid-nix-adb-connect" { } ''
  mkdir -p $out/libexec
  $CC -O2 -Wall -s -o $out/libexec/adb-ports ${./adb-ports.c}
  install -D -m755 ${./adb-connect.sh} $out/bin/droid-nix-adb-connect
  substituteInPlace $out/bin/droid-nix-adb-connect \
    --subst-var-by runtimeShell ${runtimeShell} \
    --subst-var out \
    --subst-var-by adb ${adb}/bin/adb
''
