# A minimal third-party app that sends droid-nix RUN_COMMAND intents
# (unsigned, aligned APK). Built without gradle: aapt2, javac, d8.
# How it was built and used: ../RESULTS.md.
{ nixpkgs }:

let
  pkgs = import nixpkgs {
    system = "x86_64-linux";
    config = {
      android_sdk.accept_license = true;
      allowUnfree = true;
    };
  };
  android = pkgs.callPackage ../../../pkgs/android-sdk.nix { };
  androidJar = "${android.sdk}/platforms/android-${android.versions.platform}/android.jar";
in

pkgs.runCommand "run-command-test.apk"
  {
    nativeBuildInputs = [
      pkgs.jdk17_headless
      pkgs.zip
    ];
  }
  ''
    ${android.buildTools}/aapt2 link -I ${androidJar} --manifest ${./AndroidManifest.xml} -o base.apk
    mkdir classes
    cp ${./RunCommandTest.java} RunCommandTest.java
    javac --release 8 -classpath ${androidJar} -d classes RunCommandTest.java
    ${android.buildTools}/d8 --release --min-api 26 --lib ${androidJar} --output . classes/us/lassul/droidnix/runcommandtest/*.class
    zip -q base.apk classes.dex
    ${android.buildTools}/zipalign -P 16 -f 4 base.apk $out
  ''
