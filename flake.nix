{
  description = "Nix-on-Droid as capable as Termux: app, plugins and packages built from source";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    # The nix-on-droid-app fork rebased onto termux-app master.
    # Switch to the GitHub fork by changing only this line, e.g.
    #   url = "github:<owner>/nix-on-droid-app/rebase-termux";
    nix-on-droid-app = {
      url = "git+file:///var/lib/hermes/workspace/nix-on-droid-app?ref=rebase-termux";
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
        nix-on-droid-app = pkgs.callPackage ./pkgs/nix-on-droid-app {
          inherit android;
          src = inputs.nix-on-droid-app;
        };
        sign = pkgs.callPackage ./pkgs/sign.nix { inherit android; };
        default = self.packages.${system}.nix-on-droid-app;
      };

      apps.${system} = {
        sign = {
          type = "app";
          program = "${self.packages.${system}.sign}/bin/sign";
          meta.description = "Sign an APK with a keystore given via SIGN_* env vars";
        };
        # Regenerates pkgs/nix-on-droid-app/deps.json (needs network); run from the flake root.
        update-deps = {
          type = "app";
          program = "${self.packages.${system}.nix-on-droid-app.mitmCache.updateScript}";
          meta.description = "Regenerate the gradle deps.json lockfile of nix-on-droid-app";
        };
      };
    };
}
