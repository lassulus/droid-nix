# Unsigned release APK of nix-on-droid-app, built from source.
#
# Update deps.json (needs network, run from the flake root):
#   nix build .#nix-on-droid-app.mitmCache.updateScript && ./result
{
  lib,
  stdenv,
  runtimeShell,
  gradle-packages,
  jdk17_headless,
  android,
  src,
}:
let
  # termux-app pins gradle 9.2.1 in gradle-wrapper.properties and builds with JDK 17 in CI.
  gradle =
    (gradle-packages.mkGradle {
      version = "9.2.1";
      hash = "sha256-cvRMn468sa9Dg49F7lxKqcVESJizRoqz9K97YHbFvD8=";
      defaultJava = jdk17_headless;
    }).wrapped;

  buildGradle = builtins.readFile "${src}/app/build.gradle";
  versionName = lib.head (
    lib.concatMap (
      line:
      let
        m = builtins.match ''[[:space:]]*versionName "([^"]+)".*'' line;
      in
      lib.optional (m != null) (lib.head m)
    ) (lib.splitString "\n" buildGradle)
  );
in
stdenv.mkDerivation (finalAttrs: {
  pname = "nix-on-droid-app";
  version = versionName;
  inherit src;

  nativeBuildInputs = [
    gradle
    jdk17_headless
  ];

  mitmCache = gradle.fetchDeps {
    pkg = finalAttrs.finalPackage;
    data = ./deps.json;
    # The NDK's prebuilt make hardcodes /bin/sh, which the build sandbox has
    # but the update script's bwrap does not.
    bwrapFlags = ''--ro-bind "$PWD" "$PWD" --ro-bind ${runtimeShell} /bin/sh'';
  };

  # The app's build.gradle also packages x86_64 (for emulators); nix-on-droid
  # only ships aarch64 bootstraps, so build arm64-v8a only.
  postPatch = ''
    substituteInPlace app/build.gradle \
      --replace-fail "abiFilters 'arm64-v8a', 'x86_64'" "abiFilters 'arm64-v8a'"
  '';

  env = {
    ANDROID_HOME = android.sdk;
    ANDROID_SDK_ROOT = android.sdk;
  };

  preConfigure = ''
    export ANDROID_USER_HOME="$TMPDIR/android-user-home"
  '';

  gradleFlags = [
    "-Dorg.gradle.workers.max=2"
    "-Dfile.encoding=utf-8"
    # AGP otherwise fetches a prebuilt (non-patchelfed) aapt2 from maven
    "-Pandroid.aapt2FromMavenOverride=${android.sdk}/build-tools/${android.versions.buildTools}/aapt2"
  ];
  # Keep memory use predictable on small builders.
  enableParallelBuilding = false;
  enableParallelUpdating = false;

  gradleBuildTask = ":app:assembleRelease";
  # nixDownloadDeps resolves every configuration, which fails for AGP projects
  # (ambiguous variants) and would miss artifacts AGP resolves lazily, so record
  # exactly what the real build fetches.
  gradleUpdateTask = finalAttrs.gradleBuildTask;

  installPhase = ''
    runHook preInstall

    apks=(app/build/outputs/apk/release/*.apk)
    if [ "''${#apks[@]}" -ne 1 ]; then
      echo "expected exactly one release APK, got: ''${apks[*]}" >&2
      exit 1
    fi
    mkdir -p $out
    cp "''${apks[0]}" $out/nix-on-droid-app-unsigned.apk
    ${android.buildTools}/zipalign -P 16 -f 4 \
      $out/nix-on-droid-app-unsigned.apk $out/nix-on-droid-app-unsigned-aligned.apk
    ${android.buildTools}/zipalign -c -P 16 4 $out/nix-on-droid-app-unsigned-aligned.apk

    runHook postInstall
  '';

  meta = {
    description = "Nix-on-Droid Android app (unsigned release APK, arm64-v8a)";
    homepage = "https://github.com/nix-community/nix-on-droid-app";
    license = lib.licenses.gpl3Only;
    platforms = [ "x86_64-linux" ];
    sourceProvenance = with lib.sourceTypes; [
      fromSource
      binaryBytecode # maven dependencies via mitm cache
    ];
  };
})
