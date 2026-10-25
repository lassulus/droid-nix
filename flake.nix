{
  description = "droid-nix: a Termux-based terminal app with Nix for Android";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    # Upstream termux-app; app/patches is applied on top of it.
    termux-app = {
      url = "github:termux/termux-app/8629e632fcb95da272221be327db653fb24befe9";
      flake = false;
    };
  };

  outputs =
    { self, nixpkgs, ... }@inputs:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs {
        inherit system;
        config = {
          # The Android SDK is unfree; accepting its license is part of this expression.
          android_sdk.accept_license = true;
          allowUnfree = true;
        };
      };
      android = pkgs.callPackage ./pkgs/android-sdk.nix { };
    in
    {
      packages.${system} = {
        droid-nix-app = pkgs.callPackage ./app {
          inherit android;
          inherit (inputs) termux-app;
        };
        sign = pkgs.callPackage ./pkgs/sign.nix { inherit android; };
        default = self.packages.${system}.droid-nix-app;
      };

      apps.${system} = {
        sign = {
          type = "app";
          program = "${self.packages.${system}.sign}/bin/sign";
          meta.description = "Sign an APK with a keystore given via SIGN_* env vars";
        };
        # Regenerates app/deps.json (needs network); run from the flake root.
        update-deps = {
          type = "app";
          program = "${self.packages.${system}.droid-nix-app.mitmCache.updateScript}";
          meta.description = "Regenerate the gradle deps.json lockfile of the app";
        };
      };
    };
}
