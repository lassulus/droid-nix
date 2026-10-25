# Sign an APK with apksigner. All key material comes from the environment,
# so CI can feed it from secrets. Nothing is stored in the repo or the store.
#
#   SIGN_KEYSTORE           path to the keystore (jks/pkcs12)
#   SIGN_KEY_ALIAS          key alias inside the keystore
#   SIGN_KEYSTORE_PASSWORD  keystore password
#   SIGN_KEY_PASSWORD       key password (defaults to SIGN_KEYSTORE_PASSWORD)
#
# Usage: sign <unsigned-aligned.apk> <signed.apk>
{
  writeShellApplication,
  jdk17_headless,
  android,
}:
writeShellApplication {
  name = "sign";
  runtimeInputs = [ jdk17_headless ];
  text = ''
    if [ "$#" -ne 2 ]; then
      echo "usage: sign <unsigned-aligned.apk> <signed.apk>" >&2
      exit 64
    fi
    : "''${SIGN_KEYSTORE:?SIGN_KEYSTORE must point to the keystore}"
    : "''${SIGN_KEY_ALIAS:?SIGN_KEY_ALIAS must be set}"
    : "''${SIGN_KEYSTORE_PASSWORD:?SIGN_KEYSTORE_PASSWORD must be set}"
    export SIGN_KEY_PASSWORD="''${SIGN_KEY_PASSWORD:-$SIGN_KEYSTORE_PASSWORD}"
    export SIGN_KEYSTORE_PASSWORD

    in="$1"
    out="$2"
    # Alignment must happen before signing; re-aligning is idempotent.
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    ${android.buildTools}/zipalign -P 16 -f 4 "$in" "$tmp/aligned.apk"

    ${android.buildTools}/apksigner sign \
      --ks "$SIGN_KEYSTORE" \
      --ks-key-alias "$SIGN_KEY_ALIAS" \
      --ks-pass env:SIGN_KEYSTORE_PASSWORD \
      --key-pass env:SIGN_KEY_PASSWORD \
      --out "$out" \
      "$tmp/aligned.apk"
    ${android.buildTools}/apksigner verify --print-certs "$out"
  '';
}
