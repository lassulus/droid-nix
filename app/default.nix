# Unsigned release APK of droid-nix: upstream termux-app with ./patches applied.
#
# Update deps.json (needs network, run from the flake root):
#   nix run .#update-deps
{
  lib,
  stdenv,
  runtimeShell,
  gradle-packages,
  jdk17_headless,
  android,
  termux-app,
  bootstrapZip, # directory with bootstrap-aarch64.zip, see ../bootstrap
}:
let
  # termux-app pins gradle 9.2.1 in gradle-wrapper.properties and builds with JDK 17 in CI.
  gradle =
    (gradle-packages.mkGradle {
      version = "9.2.1";
      hash = "sha256-cvRMn468sa9Dg49F7lxKqcVESJizRoqz9K97YHbFvD8=";
      defaultJava = jdk17_headless;
    }).wrapped;

  # Applied in file name order (git format-patch numbering).
  patches = lib.sort (a: b: a < b) (lib.filesystem.listFilesRecursive ./patches);

  # The versionName set by the patch series, so it stays the single source of truth.
  versionName = lib.head (
    lib.concatMap (
      line:
      let
        m = builtins.match ''\+[[:space:]]*versionName "([^"]+)".*'' line;
      in
      lib.optional (m != null) (lib.head m)
    ) (lib.concatMap (p: lib.splitString "\n" (builtins.readFile p)) patches)
  );
in
stdenv.mkDerivation (finalAttrs: {
  pname = "droid-nix-app";
  version = versionName;
  src = termux-app;
  inherit patches;

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

  env = {
    ANDROID_HOME = android.sdk;
    ANDROID_SDK_ROOT = android.sdk;
  };

  # The first start unpacks this asset (patch 0003).
  postPatch = ''
    install -D -m 0644 ${bootstrapZip}/bootstrap-aarch64.zip app/src/main/assets/bootstrap-aarch64.zip
  '';

  preConfigure = ''
    export ANDROID_USER_HOME="$TMPDIR/android-user-home"
  '';

  gradleFlags = [
    "-Dorg.gradle.workers.max=2"
    "-Dfile.encoding=utf-8"
    # AGP otherwise fetches a prebuilt (non-patchelfed) aapt2 from maven
    "-Pandroid.aapt2FromMavenOverride=${android.sdk}/build-tools/${android.versions.buildTools}/aapt2"
    # assembleRelease runs lint vital, which fetches maven.google.com's
    # master-index/group-index and the Play SDK index. Those change with every
    # upstream release, so their pinned hashes would break the build.
    "-x"
    "lintVitalRelease"
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
    cp "''${apks[0]}" $out/droid-nix-unsigned.apk
    ${android.buildTools}/zipalign -P 16 -f 4 \
      $out/droid-nix-unsigned.apk $out/droid-nix-unsigned-aligned.apk
    ${android.buildTools}/zipalign -c -P 16 4 $out/droid-nix-unsigned-aligned.apk

    runHook postInstall
  '';

  meta = {
    description = "droid-nix Android app (unsigned release APK, arm64-v8a)";
    homepage = "https://github.com/lassulus/droid-nix";
    license = lib.licenses.gpl3Only;
    platforms = [ "x86_64-linux" ];
    sourceProvenance = with lib.sourceTypes; [
      fromSource
      binaryBytecode # maven dependencies via mitm cache
    ];
  };
})
