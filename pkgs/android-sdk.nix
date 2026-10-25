# Android SDK matching what termux-app's gradle files ask for
# (gradle.properties: compileSdkVersion, ndkVersion; AGP 8.13 default build-tools).
{ androidenv }:
let
  versions = {
    platform = "36";
    buildTools = "35.0.0";
    ndk = "29.0.14206865";
  };
  composition = androidenv.composeAndroidPackages {
    platformVersions = [ versions.platform ];
    buildToolsVersions = [ versions.buildTools ];
    includeNDK = true;
    ndkVersions = [ versions.ndk ];
    includeCmake = false;
    includeEmulator = false;
    includeSystemImages = false;
    includeSources = false;
  };
in
{
  inherit versions composition;
  sdk = "${composition.androidsdk}/libexec/android-sdk";
  # Just build-tools (aapt2, zipalign, apksigner) without the whole SDK closure.
  buildTools = "${builtins.head composition.build-tools}/libexec/android-sdk/build-tools/${versions.buildTools}";
}
