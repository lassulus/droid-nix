# Unsigned release APK of droid-nix: the app in this directory, built with
# upstream termux-app's terminal-emulator and terminal-view.
#
# Update deps.json (needs network, run from the flake root):
#   nix run .#update-deps
{
  lib,
  stdenv,
  runCommand,
  gradle-packages,
  jdk17_headless,
  android,
  termux-app,
  bootstrapZip, # directory with bootstrap-aarch64.zip, see ../bootstrap
  launcher, # the static launcher, see ../bootstrap/launcher
}:
let
  gradle =
    (gradle-packages.mkGradle {
      version = "9.2.1";
      hash = "sha256-cvRMn468sa9Dg49F7lxKqcVESJizRoqz9K97YHbFvD8=";
      defaultJava = jdk17_headless;
    }).wrapped;

  # build.gradle is the single source of truth for the version.
  versionName = lib.head (
    lib.concatMap (
      line:
      let
        m = builtins.match ''[[:space:]]*versionName "([^"]+)".*'' line;
      in
      lib.optional (m != null) (lib.head m)
    ) (lib.splitString "\n" (builtins.readFile ./build.gradle))
  );

  # terminal-emulator's JNI part: the pty and the session's process.
  libtermux = runCommand "libtermux" { } ''
    mkdir $out
    ${android.ndkBin}/aarch64-linux-android26-clang -std=c11 -Wall -Wextra -Werror -Os \
      -fno-stack-protector -shared -Wl,--gc-sections \
      -o $out/libtermux.so ${termux-app}/terminal-emulator/src/main/jni/termux.c
  '';
in
stdenv.mkDerivation (finalAttrs: {
  pname = "droid-nix-app";
  version = versionName;

  src = lib.fileset.toSource {
    root = ./.;
    fileset = lib.fileset.unions [
      ./settings.gradle
      ./build.gradle
      ./gradle.properties
      ./proguard-rules.pro
      ./src
      ./terminal-emulator
      ./terminal-view
    ];
  };

  nativeBuildInputs = [
    gradle
    jdk17_headless
  ];

  mitmCache = gradle.fetchDeps {
    pkg = finalAttrs.finalPackage;
    data = ./deps.json;
  };

  env = {
    ANDROID_HOME = android.sdk;
    ANDROID_SDK_ROOT = android.sdk;
  };

  # The first start unpacks the bootstrap asset. The launcher is a "native
  # library" only so that it is installed where the app may execve it (apps
  # targeting API 29+ may not execve their data files).
  postPatch = ''
    for module in terminal-emulator terminal-view; do
      mkdir $module/src
      cp -r --no-preserve=mode ${termux-app}/$module/src/main $module/src/main
    done
    rm -r terminal-emulator/src/main/jni
    install -D -m 0644 ${bootstrapZip}/bootstrap-aarch64.zip src/main/assets/bootstrap-aarch64.zip
    install -D -m 0755 ${launcher}/bin/launcher src/main/jniLibs/arm64-v8a/liblauncher.so
    install -D -m 0755 ${libtermux}/libtermux.so src/main/jniLibs/arm64-v8a/libtermux.so
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

  gradleBuildTask = ":assembleRelease";
  # nixDownloadDeps resolves every configuration, which fails for AGP projects
  # (ambiguous variants) and would miss artifacts AGP resolves lazily, so record
  # exactly what the real build fetches.
  gradleUpdateTask = finalAttrs.gradleBuildTask;

  installPhase = ''
    runHook preInstall

    apks=(build/outputs/apk/release/*.apk)
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
