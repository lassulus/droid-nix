{
  description = "droid-nix: a terminal app with Nix for Android";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
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
      sources = pkgs.callPackage ./app/sources.nix { };
      bootstrap = import ./bootstrap {
        inherit nixpkgs;
        nativeSystem = system;
      };
    in
    {
      packages.${system} = {
        droid-nix-app = pkgs.callPackage ./app {
          inherit android;
          inherit (sources) termux-app termux-api;
          inherit (bootstrap) bootstrapZip launcher;
        };
        # Directory with bootstrap-aarch64.zip, assembled on x86_64-linux.
        bootstrap-aarch64 = bootstrap.bootstrapZip;
        sign = pkgs.callPackage ./pkgs/sign.nix { inherit android; };
        default = self.packages.${system}.droid-nix-app;
      };

      # The switch tool, run on the device by the first login and by users.
      packages.aarch64-linux = rec {
        droid-nix = nixpkgs.legacyPackages.aarch64-linux.callPackage ./pkgs/droid-nix { };
        default = droid-nix;
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
        # Update app/sources.json (needs network); run from the flake root.
        update-termux-app = {
          type = "app";
          program = "${sources.termux-app.updateScript}/bin/update-termux-app";
          meta.description = "Update termux-app in app/sources.json, to REV or upstream HEAD";
        };
        update-termux-api = {
          type = "app";
          program = "${sources.termux-api.updateScript}/bin/update-termux-api";
          meta.description = "Update termux-api in app/sources.json, to REV or upstream HEAD";
        };
      };

      # On-device configuration, see templates/default.
      lib.droidNixConfiguration =
        {
          pkgs,
          modules ? [ ],
          extraSpecialArgs ? { },
          home-manager-path ? null,
        }:
        if pkgs.stdenv.hostPlatform.system != "aarch64-linux" then
          throw "droid-nix only supports aarch64-linux, got ${pkgs.stdenv.hostPlatform.system}"
        else
          import ./modules {
            inherit pkgs extraSpecialArgs home-manager-path;
            config.imports = modules;
          };

      templates.default = {
        path = ./templates/default;
        description = "droid-nix on-device configuration";
      };

      # Evaluates the template configuration.
      checks.${system}.template = pkgs.writeText "droid-nix-template-drv" (builtins.unsafeDiscardOutputDependency
        (self.lib.droidNixConfiguration {
          pkgs = nixpkgs.legacyPackages.aarch64-linux;
          modules = [ ./templates/default/droid-nix.nix ];
        }).activationPackage.drvPath
      );
    };
}
