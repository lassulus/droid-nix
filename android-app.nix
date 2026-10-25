# Identity of the droid-nix Android app, shared by the bootstrap and the
# on-device modules. Must match the applicationId in app/patches.
rec {
  packageName = "us.lassul.droidnix";
  # The app's private data directory; the bootstrap is unpacked to files/usr.
  dataDir = "/data/data/${packageName}";
}
